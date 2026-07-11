//========================================================================================
//
//  KESCMScrollMap.cpp
//
//  スクロールバー地図: 文書ウィンドウ(kLayoutPresentationBoss)の縦スクロールバー左隣に、
//  KESCM の枠(変更マーク)ページ位置を示す細い strip を実行時注入する(VS の検索マーク風)。
//  SDK 裏取りの全記録は docs/ai-notes/scrollbar-minimap.md(SDK ルート)。要点:
//    - 注入先: presentation の IPanelControlData(kOWLHostedPanelControlDataImpl)。
//      スクロールバー boss 自身は IPanelControlData を持たないので子にはできない。
//    - 実行時生成→注入の標準形は open/components/linksui/LinkInfoPanelObserver.cpp:281
//      (::CreateObject(db, RsrcSpec(..., kViewRsrcType, resID), IID_ICONTROLVIEW) →
//       AddWidget → SetFrame → SetFrameBinding)。
//    - リサイズ追従は枠組み保証(IControlView.h:150 の契約)。binding は縦スクロールバー自身の
//      GetFrameBinding() をコピーするのが最も堅い。
//    - 自前描画 widget boss の手本 = customdatalinkui kCusDtLnkUITreeCViewPanelWidgetBoss
//      (kGenericPanelWidgetBoss + 自前 IID_ICONTROLVIEW)。
//
//  フェーズ1(プローブ=オレンジ塗り)は 2026-07-11 実機表示OK。現在はフェーズ2=実データ描画:
//  変更ページ(sEntries)=赤 / Add/Remove 登録ページ=緑(色はユーザー指定)。表示専用で
//  クリック移動等は付けない(ユーザー指定 2026-07-11)→イベントハンドラ不要のシンプル構成。
//
//  ライフサイクル: Start(比較開始)で KESCMScrollMapAttach、Stop/Clear で KESCMScrollMapDetachAll
//  (KESCMPanelObserver.cpp の KESCMToggleStartStop から呼ぶ)。strip へのポインタは一切保持しない
//  (毎回 FindWidget で探す)ので、窓ごと閉じられて widget が消えていても安全。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"
#include "IPanelControlData.h"
#include "IWidgetParent.h"
#include "IDocumentPresentation.h"	// IID_IDOCUMENTPRESENTATION(文書ウィンドウ判定)
#include "ILayoutViewUtils.h"		// GetAllLayoutViews(Split Window 両ペイン+全窓の列挙)
#include "IGraphicsPort.h"
#include "IGeometry.h"				// ページ矩形(pasteboard 写像用)
#include "IInterfaceColors.h"		// 背景をテーマ地色(kInterfacePaletteFill)に

// General includes:
#include "K2Vector.h"
#include "Utils.h"
#include "CreateObject.h"			// ::CreateObject(db, RsrcSpec, IID)
#include "RsrcSpec.h"
#include "LocaleSetting.h"
#include "DVControlView.h"			// 自前描画ビューの基底(customdatalinkui と同じ)
#include "AGMGraphicsContext.h"
#include "AutoGSave.h"
#include "LayoutUIID.h"				// kVertScrollBarWidgetID
#include "CoreResTypes.h"			// kViewRsrcType
#include "TransformUtils.h"			// ::InnerToPasteboardMatrix
#include "PMMatrix.h"
#include <vector>
#include <set>

// Project includes:
#include "KESCMID.h"
#include "KESCMScrollMap.h"
#include "KESCMCore.h"				// KESCMArmedTargetDB / KESCMIsDocDBOpen / KESCMCollectPageUIDs
#include "KESCMDrawEventHandler.h"	// sEntries / sDB(変更ページ=赤マークの供給元)
#include "KESCMPageMap.h"			// KESCMPageMapCollectRegistered(Add/Remove 登録ページ=緑マーク)

// strip の幅(px)。縦スクロールバーの左辺にこの幅で並べる(6→5px、ユーザー指定 2026-07-11。
// 移動はバー自体のクリックで足りるため表示は細めに)。
static const PMReal kKESCMScrollMapWidth = 5.0;

//========================================================================================
// KESCMScrollMapView — strip の自前描画(IControlView 実装)
//========================================================================================

class KESCMScrollMapView : public DVControlView
{
	typedef DVControlView inherited;

public:
	KESCMScrollMapView(IPMUnknown* boss) : inherited(boss) {}
	virtual ~KESCMScrollMapView() {}

	virtual void Draw(IViewPort* viewPort, SysRgn updateRgn);
};

CREATE_PERSIST_PMINTERFACE(KESCMScrollMapView, kKESCMScrollMapViewImpl)

// フェーズ2の実データ描画(表示専用。クリック移動等は付けない=ユーザー指定 2026-07-11)。
//   ・背景 = テーマ地色(kInterfacePaletteFill)
//   ・変更ページ(sEntries) = 赤の塗りつぶし
//   ・Add/Remove 登録ページ(KESCMPageMapCollectRegistered) = 緑の塗りつぶし
// 写像は「文書全体基準」(VS方式): 全ページの pasteboard Y の全域[minY,maxY]を strip の全高に
// 正規化し、各対象ページの Y 帯をそのまま帯マークにする(最低3px)。スクロール位置・ズームに
// 依存しないので、再描画は比較結果が変わったとき(KESCMScrollMapInvalidateAll)だけでよい。
// ★既知の割り切り: Hide Unchanged Spreads で隠したスプレッドは pasteboard 座標が旧位置のまま
// 残る(メモリ kescm-hide-unchanged-spreads)ため、隠し使用中はマーク位置が実表示と多少ズレ得る。
void KESCMScrollMapView::Draw(IViewPort* viewPort, SysRgn updateRgn)
{
	AGMGraphicsContext gc(viewPort, this, updateRgn);
	InterfacePtr<IGraphicsPort> gPort(gc.GetViewPort(), UseDefaultIID());
	if (gPort == nil)
		return;

	AutoGSave autoGSave(gPort);

	const PMRect frame(this->GetInnerContentFrame());

	// 背景: テーマ地色(取得失敗時は中間グレー)
	PMReal bgR(0.5), bgG(0.5), bgB(0.5);
	{
		InterfacePtr<IInterfaceColors> colors(GetExecutionContextSession(), IID_IINTERFACECOLORS);
		if (colors != nil)
		{
			RealAGMColor bg;
			colors->GetRealAGMColor(kInterfacePaletteFill, bg);
			bgR = bg.red; bgG = bg.green; bgB = bg.blue;
		}
	}
	gPort->setrgbcolor(bgR, bgG, bgB);
	gPort->rectpath(frame);
	gPort->fill();

	// 対象文書 = 現在 arm 中の Target。未 arm・クローズ済みなら背景のみ。
	IDataBase* db = KESCMArmedTargetDB();
	if (db == nil || !KESCMIsDocDBOpen(db))
		return;

	// 全ページの pasteboard Y 帯を集める(順序は KESCMCollectPageUIDs=スプレッド順・ページ順)。
	std::vector<UID> pages;
	KESCMCollectPageUIDs(db, pages);
	if (pages.empty())
		return;

	std::vector<PMReal> tops(pages.size()), bottoms(pages.size());
	PMReal minY(0), maxY(0);
	bool16 first = kTrue;
	for (size_t i = 0; i < pages.size(); ++i)
	{
		tops[i] = bottoms[i] = PMReal(0);
		InterfacePtr<IGeometry> geo(db, pages[i], UseDefaultIID());
		if (geo == nil)
			continue;
		PMRect box = geo->GetPathBoundingBox();
		PMPoint pTop(box.GetHCenter(), box.Top());
		PMPoint pBot(box.GetHCenter(), box.Bottom());
		PMMatrix m = ::InnerToPasteboardMatrix(geo);
		m.Transform(&pTop);
		m.Transform(&pBot);
		PMReal a = pTop.Y(), b = pBot.Y();
		if (b < a) { PMReal t = a; a = b; b = t; }
		tops[i] = a; bottoms[i] = b;
		if (first) { minY = a; maxY = b; first = kFalse; }
		else { if (a < minY) minY = a; if (b > maxY) maxY = b; }
	}
	if (first || maxY <= minY)
		return;

	// マーク対象の集合。赤 = 変更ページ(sEntries; エンジンが同じ db を指しているときだけ)。
	// 緑 = Add/Remove 登録ページ。両方に該当したら赤を優先(変更が見える方が重要)。
	const bool16 entriesValid = (KESCMDrawEventHandler::sDB == db);
	std::set<UID> greens;
	KESCMPageMapCollectRegistered(db, greens);

	const PMReal scale = frame.Height() / (maxY - minY);
	for (size_t i = 0; i < pages.size(); ++i)
	{
		if (bottoms[i] <= tops[i])
			continue;	// 幾何が取れなかったページ
		const bool16 isRed = (entriesValid &&
			KESCMDrawEventHandler::sEntries.find(pages[i]) != KESCMDrawEventHandler::sEntries.end());
		const bool16 isGreen = (!isRed && greens.find(pages[i]) != greens.end());
		if (!isRed && !isGreen)
			continue;

		PMReal y0 = frame.Top() + (tops[i]    - minY) * scale;
		PMReal y1 = frame.Top() + (bottoms[i] - minY) * scale;
		if (y1 - y0 < PMReal(3.0))	// 細くなり過ぎたら中心を保って3pxに
		{
			const PMReal c = (y0 + y1) / PMReal(2.0);
			y0 = c - PMReal(1.5);
			y1 = c + PMReal(1.5);
		}
		if (y0 < frame.Top())    y0 = frame.Top();
		if (y1 > frame.Bottom()) y1 = frame.Bottom();

		if (isRed)
			gPort->setrgbcolor(PMReal(0.85), PMReal(0.08), PMReal(0.08));
		else
			gPort->setrgbcolor(PMReal(0.10), PMReal(0.70), PMReal(0.25));
		gPort->rectpath(PMRect(frame.Left(), y0, frame.Right(), y1));
		gPort->fill();
	}
}

//========================================================================================
// 注入/取り外し
//========================================================================================

// db のレイアウトビュー群から、それぞれが属する文書ウィンドウ(presentation)を重複なしで集める。
// 戻りは presentation の IPanelControlData(addref 済み)を out に積む。
// GetAllLayoutViews の戻り(IControlView*)は既存 KESCM コードと同じく非所有として扱う。
static void KESCMCollectPresentationPanels(IDataBase* db, K2Vector<IPanelControlData*>& out)
{
	K2Vector<IControlView*> views;
	Utils<ILayoutViewUtils>()->GetAllLayoutViews(views, nil, db);

	K2Vector<IPMUnknown*> seen;	// presentation の同一性判定(同じ IID の QI 結果同士なのでポインタ比較可)
	for (int32 i = 0; i < (int32)views.size(); ++i)
	{
		if (views[i] == nil)
			continue;
		InterfacePtr<IWidgetParent> wp(views[i], IID_IWIDGETPARENT);
		if (wp == nil)
			continue;
		InterfacePtr<IDocumentPresentation> pres(
			(IDocumentPresentation*)wp->QueryParentFor(IID_IDOCUMENTPRESENTATION));
		if (pres == nil)
			continue;

		bool16 dup = kFalse;
		for (int32 s = 0; s < (int32)seen.size(); ++s)
		{
			if (seen[s] == (IPMUnknown*)(IDocumentPresentation*)pres)
			{
				dup = kTrue;
				break;
			}
		}
		if (dup)
			continue;
		seen.push_back((IPMUnknown*)(IDocumentPresentation*)pres);

		InterfacePtr<IPanelControlData> panel(pres, UseDefaultIID());
		if (panel == nil)
			continue;
		panel->AddRef();
		out.push_back(panel);
	}
}

// KESCMScrollMapAttach(KESCMScrollMap.h 参照) — targetDB の各文書ウィンドウに strip を注入する。
void KESCMScrollMapAttach(IDataBase* targetDB)
{
	if (targetDB == nil)
		return;

	K2Vector<IPanelControlData*> panels;
	KESCMCollectPresentationPanels(targetDB, panels);

	for (int32 i = 0; i < (int32)panels.size(); ++i)
	{
		InterfacePtr<IPanelControlData> presPanel(panels[i]);	// 所有権引き取り(Release 担当)

		// 二重注入ガード(窓単位)
		if (presPanel->FindWidget(kKESCMScrollMapWidgetID) != nil)
			continue;

		// 縦スクロールバーを探す(FindWidget は既定で全子孫再帰)。無い窓はスキップ。
		IControlView* sbView = presPanel->FindWidget(kVertScrollBarWidgetID);
		if (sbView == nil)
			continue;

		// strip はスクロールバーの「直接の親」に追加する(座標系とリサイズ追従をバーと揃えるため)。
		InterfacePtr<IWidgetParent> sbWP(sbView, IID_IWIDGETPARENT);
		if (sbWP == nil)
			continue;
		InterfacePtr<IPanelControlData> sbParentPanel(sbWP->GetParent(), UseDefaultIID());
		if (sbParentPanel == nil)
			continue;

		// 実行時生成(linksui と同じ標準形)。db は親 widget 群と同じ UI データベース。
		InterfacePtr<IControlView> strip((IControlView*)::CreateObject(
			::GetDataBase(sbParentPanel),
			RsrcSpec(LocaleSetting::GetLocale(), kKESCMPluginID, kViewRsrcType, kKESCMScrollMapRsrcID),
			IID_ICONTROLVIEW));
		if (strip == nil)
			continue;

		sbParentPanel->AddWidget(strip);	// 末尾追加=描画順で最前面

		// バーの左隣・同じ高さ。座標はバーと同じ親ローカル。binding はバーのものをコピー
		// (右端固定+上下ストレッチ相当のはず。実際に何が入っているかはプローブで観察)。
		const PMRect sbFrame = sbView->GetFrame();
		PMRect stripFrame(sbFrame.Left() - kKESCMScrollMapWidth, sbFrame.Top(),
						  sbFrame.Left(), sbFrame.Bottom());
		strip->SetFrame(stripFrame);
		strip->SetFrameBinding(sbView->GetFrameBinding());
		strip->ShowView();
		strip->Invalidate();
	}
}

// KESCMScrollMapDetachAll(KESCMScrollMap.h 参照) — 全文書の全ウィンドウから strip を取り外す。
void KESCMScrollMapDetachAll()
{
	K2Vector<IPanelControlData*> panels;
	KESCMCollectPresentationPanels(nil, panels);	// db=nil で全レイアウトビュー

	for (int32 i = 0; i < (int32)panels.size(); ++i)
	{
		InterfacePtr<IPanelControlData> presPanel(panels[i]);	// 所有権引き取り(Release 担当)

		IControlView* strip = presPanel->FindWidget(kKESCMScrollMapWidgetID);
		if (strip == nil)
			continue;

		// strip の直接の親パネルから外す(deleteUID=kTrue で UI データベースからも削除。
		// linksui AddDeleteCaptionRowButtonObserver.cpp:157 と同じ作法)。
		InterfacePtr<IWidgetParent> wp(strip, IID_IWIDGETPARENT);
		if (wp == nil)
			continue;
		InterfacePtr<IPanelControlData> parentPanel(wp->GetParent(), UseDefaultIID());
		if (parentPanel == nil)
			continue;
		parentPanel->RemoveWidget(strip, kTrue, kTrue);
	}
}

// KESCMScrollMapInvalidateAll(KESCMScrollMap.h 参照) — 注入済みの全 strip を再描画する。
// 比較(KESCMDoMarkChangesDoc)の末尾から呼ばれ、Start/Ctrl+ミドル再比較/登録トグルの
// すべてで最新のマークに更新される(この3操作は全部あの関数を通る)。
void KESCMScrollMapInvalidateAll()
{
	K2Vector<IPanelControlData*> panels;
	KESCMCollectPresentationPanels(nil, panels);	// db=nil で全レイアウトビュー

	for (int32 i = 0; i < (int32)panels.size(); ++i)
	{
		InterfacePtr<IPanelControlData> presPanel(panels[i]);	// 所有権引き取り(Release 担当)
		IControlView* strip = presPanel->FindWidget(kKESCMScrollMapWidgetID);
		if (strip != nil)
			strip->Invalidate();
	}
}

// KESCMScrollMap.cpp 終わり。
