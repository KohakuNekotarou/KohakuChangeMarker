//========================================================================================
//
//  KCMScrollMap.cpp
//
//  スクロールバー地図: 文書ウィンドウ(kLayoutPresentationBoss)の縦スクロールバー左隣に、
//  KCM の枠(変更マーク)ページ位置を示す細い strip を実行時注入する(VS の検索マーク風)。
//  SDK 裏取りの全記録は docs/ai-notes/scrollbar-minimap.md(SDK ルートからの相対パス)。要点:
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
//  ライフサイクル: Start(比較開始)で KCMScrollMapAttach、Stop/Clear で KCMScrollMapDetachAll。
//  ⚠2026-08-17 訂正(API 監査 B-U8): 旧記述は「KCMPanelObserver.cpp の KCMToggleStartStop から
//  呼ぶ」だったが、**KCMToggleStartStop は model 側(KCMComparisonRun.cpp)にあり、この UI 関数を
//  呼べない**。今は model が比較の開始/終了を**通知**し、UI 側の KCMModelChangeObserver が受けて
//  Attach/DetachAll する(フライアウトのトグル操作は KCMActionComponent から)。
//  strip へのポインタは一切保持しない(毎回 FindWidget で探す)ので、窓ごと閉じられて widget が
//  消えていても安全。
//
//  ★ビルド時リンク依存:
//    このファイルは 2 つの追加ライブラリを要求する(Dolly 既定の PMRuntime / Public だけでは未解決
//    シンボルになり、大量のリンクエラーで落ちる)。
//      - DV_WidgetBin ... 下で継承している DVControlView(自前描画ビュー基底。#include "DVControlView.h")
//      - WidgetBin    ... ::CreateObject + kViewRsrcType + AddWidget によるウィジェット実行時生成
//    Windows(.vcxproj)では AdditionalDependencies に WidgetBin.lib / DV_WidgetBin.lib を追加済み
//    (repo 内の控え = KCM/buildproj/。⚠2026-08-17 訂正: 旧記述の「_buildproj」は存在しない)。
//    ★Mac は追加不要(2026-07-25 追補で訂正): Mac 側の実体は libPublicPlugIn.a の 1 本だけで、
//    Windows で 4 つに分かれているもの(PMRuntime / Public / WidgetBin / DV_WidgetBin)がそこに統合
//    されている。DVControlView を使う SDK サンプル customdatalinkui の Xcode プロジェクトも、
//    InDesignModelAndUI.framework + libPublicPlugIn.a の 2 つしかリンクしていない。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"
#include "IPanelControlData.h"
#include "IWidgetParent.h"
#include "IDocumentPresentation.h"	// IID_IDOCUMENTPRESENTATION(文書ウィンドウ判定)
#include "ILayoutViewUtils.h"		// GetAllLayoutViews(Split Window 両ペイン+全窓の列挙)
// (ILayoutControlData.h は 2026-08-19 の不具合再検査 B-U8 で外した＝「そのビューは今どのスプレッドか」は
//  KCMViewLookup の KCMQuerySpreadUIDForView に一本化し、このファイルから直接の Query が消えた)
#include "IMasterSpreadList.h"		// マスタースプレッドかの判定(表示中スプレッドの切り分け)
#include "IPanorama.h"				// GetBounds(パノラマのスクロール全域=スクロールバーが表す範囲)
#include "IGraphicsPort.h"
#include "IGeometry.h"				// ページ矩形(pasteboard 写像用)
#include "IInterfaceColors.h"		// 背景をテーマ地色(kInterfacePaletteFill)に
#include "ISpreadList.h"			// スプレッド順の走査(隠しスプレッド除外のため)
#include "ISpread.h"
#include "IBoolData.h"				// スプレッドの隠し状態(IID_IHIDESPREADBOOLDATA)の読み取り
#include "SpreadID.h"				// IID_IHIDESPREADBOOLDATA(kSpreadBoss 上の IBoolData)

// General includes:
#include "K2Vector.h"
#include "Utils.h"
#include "CreateObject.h"			// ::CreateObject(db, RsrcSpec, IID)
#include "RsrcSpec.h"
#include "LocaleSetting.h"
#include "DVControlView.h"			// 自前描画ビューの基底(customdatalinkui と同じ)
#include "AGMGraphicsContext.h"
#include "AutoGSave.h"
#include "LayoutUIID.h"				// kVertScrollBarWidgetID / kLayoutWidgetID(レイアウトビューを名指しで引く)
#include "CoreResTypes.h"			// kViewRsrcType
#include "IGeometryFacade.h"		// GetItemBounds(ページ矩形をペーストボード座標で。手本=SnapTracker.cpp:610-616)
#include <algorithm>				// std::find(presentation の重複判定)
#include <vector>
#include <set>
#include <chrono>					// steady_clock(手動 Hide/Show 検出のスロットル。単調増加の壁時計=Win/Mac 共通で正しい。
									// 旧 std::clock は Win=壁時計/POSIX(Mac)=CPU時間 と意味が食い違うため置換)

// Project includes:
#include "KCMUIID.h"
#include "KCMScrollMap.h"
#include "IKCMCompareFacade.h"	// arm 状態(2026-08-13・分割 第1段 Task 11 で Facade 経由へ)
#include "IKCMMarkData.h"			// 変更ページ・overflow・overset の読み取り(赤/薄赤/濃赤の供給元)。2026-08-13 Task 12
                                    // ＋ GetRegisteredPages(Add/Remove 登録ページ=緑マーク。2026-08-13 Task 13)
#include "KCMViewLookup.h"		// KCMQuerySpreadUIDForView(「そのビューは今どのスプレッドか」の唯一の口。
									// 2026-08-19 の不具合再検査 B-U8 でこちらへ寄せた)

// strip の幅(px)。縦スクロールバーの左辺にこの幅で並べる(6→5px、ユーザー指定 2026-07-11。
// 移動はバー自体のクリックで足りるため表示は細めに)。
static const PMReal kKCMScrollMapWidth = 5.0;

// 帯マークの不透明度(0〜1、ユーザー要望 2026-07-11 で半透明化)。実際の合成は、帯が「自分で塗った
// 背景(テーマ地色)の上」にしか載らない性質を利用した混色(色'=α×マーク色+(1-α)×背景色)で行う。
// setopacity(IGraphicsPort.h:389)でも可能だが、混色は API の透明合成挙動に依存せず確実(見た目は同一)。
// 背景はテーマ連動(IInterfaceColors の kInterfacePaletteFill)なので、ライト/ダークどちらでも馴染む。
// 枠(変更ページ=赤)+登録ページ(緑)の不透明度。
static const PMReal kKCMScrollMapMarkAlpha     = 0.4;	// 枠(変更)はしっかり見せる(ユーザー指定 2026-07-13)
// overflow「/」ページ(相手が無いページ)の不透明度。枠と差を付けて薄く(ユーザー指定 2026-07-13)。
static const PMReal kKCMScrollMapOverflowAlpha = 0.15;	// 「/」は下地とよく混ぜて薄く(0.2→0.15、ユーザー指定 2026-07-13)
// Find Overset の帯の不透明度。変更帯(0.4)より混色を控えて濃い赤にする(ユーザー指定 2026-07-24。
// 「もう少し濃く」で 0.7→0.85)。
static const PMReal kKCMScrollMapOversetAlpha  = 0.85;	// overset は下地とほぼ混ぜず赤を強く

// ★トラックの追い込みマージン(実機で調整した採用値)。矢印ボタンの内側から、さらに上下
// それぞれこのぶんだけ詰めた範囲に地図を描く。つまみが実際に動ける範囲は、矢印ボタンの内側より
// もう少し狭い(バーの上下にボタンとは別の余白がある)ため、この分を引くと帯とつまみが最もよく揃う。
// 実機で 5.0 → 8.0(2026-07-29「良い感じ」) → 6.0 → 6.5(2026-07-30 ユーザー指定。8.0 から
// 「上下 2px ずつ減らす」で 6.0 にし、そこから半 px だけ戻して微調整) → 7.5(2026-08-07 ユーザー
// 指定「＋1」) → 8.0(同日ユーザー指定。結果として 07-29 に「良い感じ」と言っていた値に戻った)と
// 詰めてきた。0.0 にすると矢印ボタンの内側いっぱいに描く(この追い込みを入れる前の動作)。
// 効き所は Draw の写像部の 1 箇所だけで、trackTop と trackBottom の両方に同じ値が効く=必ず上下対称。
// (2026-07-30: 実験時の名残だった kKCMScrollMapTestInset から改名)
static const PMReal kKCMScrollMapTrackInset = 8.0;

// スクロールバー地図の有効/無効(フライアウト「Show Scrollbar Map」トグル。既定=ON)。
// OFF の間は Attach / NoticeDrawEvent を即 return させる(strip を注入しない・毎描画の指紋計算もしない)。
// トグルを OFF にした瞬間の既存 strip 撤去は、操作側(KCMActionComponent)が DetachAll を呼ぶ。
static bool16 sScrollMapOn = kTrue;

//========================================================================================
// KCMScrollMapView — strip の自前描画(IControlView 実装)
//========================================================================================

class KCMScrollMapView : public DVControlView
{
	typedef DVControlView inherited;

public:
	KCMScrollMapView(IPMUnknown* boss) : inherited(boss) {}
	virtual ~KCMScrollMapView() {}

	virtual void Draw(IViewPort* viewPort, SysRgn updateRgn);
};

CREATE_PERSIST_PMINTERFACE(KCMScrollMapView, kKCMScrollMapViewImpl)

// ★写像をスクロールバーに合わせるための実測値を、strip と同じ窓から実行時に読む(2026-07-29)。
//   outArrowH  = 縦スクロールバーの矢印ボタン(「^」「v」)の高さ。ボタンは正方形なので
//                「バーの frame の幅」がそのまま高さになる(実機キャプチャで確認: バー 15px 幅に対し
//                ボタン領域 16px。UI スケールが変わってもバー幅と一緒に変わるので固定値は使わない。
//                公開定数 kCC2017SpectrumScrollBarWidth=13 は固定値なので採らない)。
//   outPanoTop/outPanoBottom = パノラマのスクロール全域(content 座標の Y)。スクロールバーの全長が
//                表しているのはページの範囲ではなくこちら(ページの上下のペーストボード余白を含む)。
// どちらも取れないことがある(窓の構成が想定と違う/座標系が別)ので取得可否を返し、呼び出し側は
// 取れなかった分だけ従来の写像へフォールバックする=今より悪くならないようにする。
// strip はスクロールバーの兄弟として注入してあるので、親パネルを辿ればバーもレイアウトビューも見つかる
// (IPanorama を持つのはレイアウトビューだけ)。ポインタは保持せず毎 Draw で引き直す(窓ごと閉じられても安全)。
// この strip が載っている**ペイン**のレイアウトビュー。引けなければ nil。
//
// ★★★「この strip はどのペインのものか」を答える唯一の場所([[one-question-one-place]])。
//   strip は縦スクロールバーの兄弟として注入してあるので、親をたどれば必ず自分のペインに着く。
//   ⚠Split Window では **1つの presentation に2つのレイアウトビューが載る**
//   (`ILayoutViewUtils.h:65` が "will return both layout views in a split layout view if both shown"
//   と明記)。∴ presentation 単位で「最初に一致したビュー」を採ると、**隣のペインの答え**が返り得る。
//
// ★2026-08-19(不具合再検査 B-U8)にここへ集約した。それまで「今どのスプレッドを見ているか」だけは
//   presentation を突き合わせる別関数(KCMSpreadShownInPresentation)が答えており、**同じ問いに
//   答えが2つ**あった(Y の分母に使う panorama はこの関数と同じ道で引いていた)。
//   ★★**実測では両者は一致していた**(2026-08-19。Split Window にして**主ペインだけ**マスタースプレッドへ
//   動かし、2ペインが別のスプレッドを映す状態を作って測定＝`views=[245,238]` に対し両方の答えが 245)。
//   ⇒ **不具合ではなかった。** ただし一致の理由は「strip は必ず**主**ペインの縦スクロールバーの隣に
//   入る」×「GetAllLayoutViews は主ペインを先に返す」という**二重の偶然**で、どちらが崩れても
//   静かにずれる(片方のペインがマスターを出していると、載せるページと Y の分母が別ペインのものになる)。
//   ∴ 動作が同じうちに答えを1つへ寄せた。
static IControlView* KCMStripLayoutView(IControlView* strip)
{
	InterfacePtr<IWidgetParent> wp(strip, IID_IWIDGETPARENT);
	if (wp == nil)
		return nil;
	InterfacePtr<IPanelControlData> parentPanel(wp->GetParent(), UseDefaultIID());
	if (parentPanel == nil)
		return nil;

	// ★レイアウトビューは WidgetID で名指しに引く(製品 spellpanel/PrivateSpellingUtils.cpp:362 と同じ)。
	//   ⚠同じ関数の主ペイン側(:356)は FindWidget(kLayoutWidgetBoss)＝**ClassID** を渡しているが、
	//   kLayoutWidgetBoss(kClassIDSpace, kLayoutUIPrefix+3) と kLayoutWidgetID(kWidgetIDSpace, 同+3) は
	//   数値が同じでたまたま動いているだけなので、寄せる先は副ペイン側が使っている kLayoutWidgetID。
	//   引けなかったときは従来どおり兄弟を総なめする(窓の構成が想定と違っても今より悪くならない)。
	IControlView* layoutView = parentPanel->FindWidget(kLayoutWidgetID);
	if (layoutView != nil)
		return layoutView;

	const int32 numSiblings = parentPanel->Length();
	for (int32 i = 0; i < numSiblings; ++i)
	{
		IControlView* sib = parentPanel->GetWidget(i);
		if (sib == nil || sib == strip)
			continue;	// 縦スクロールバーは panorama を持たないので、下の判定で自然に外れる
		InterfacePtr<IPanorama> sibPano(sib, UseDefaultIID());
		if (sibPano != nil)
			return sib;	// パノラマを持つ最初の兄弟=レイアウトビュー
	}
	return nil;
}

static void KCMScrollMapProbeWindow(IControlView* strip, PMReal& outArrowH,
									  PMReal& outPanoTop, PMReal& outPanoBottom, bool16& outHasPano)
{
	outArrowH = 0;
	outPanoTop = outPanoBottom = 0;
	outHasPano = kFalse;

	InterfacePtr<IWidgetParent> wp(strip, IID_IWIDGETPARENT);
	if (wp == nil)
		return;
	InterfacePtr<IPanelControlData> parentPanel(wp->GetParent(), UseDefaultIID());
	if (parentPanel == nil)
		return;

	IControlView* sbView = parentPanel->FindWidget(kVertScrollBarWidgetID);
	if (sbView != nil)
		outArrowH = sbView->GetFrame().Width();

	// ★IPanorama の Query と nil 判定は残す: 欲しいのは「パノラマを持つビュー」であって widget 名ではない
	//   (WidgetID で引けても、そこにパノラマが載っているかは別の話)。bounds が不正なら従来の写像へ任せる。
	InterfacePtr<IPanorama> panorama(KCMStripLayoutView(strip), UseDefaultIID());	// nil でも可(InterfacePtr.h:459)
	if (panorama != nil)
	{
		const PMRect bounds = panorama->GetBounds();
		if (bounds.Bottom() > bounds.Top())
		{
			outPanoTop    = bounds.Top();
			outPanoBottom = bounds.Bottom();
			outHasPano    = kTrue;
		}
	}
}

// spreadUID が db のマスタースプレッドなら kTrue(2026-08-11)。
// ★IMasterSpreadList::GetMasterSpreadIndex(UID) は使わない: 「マスターでない UID を渡したとき何を
//   返すか」がヘッダーに書かれていない(IMasterSpreadList.h:101-107 は "Return the index" としか
//   言わない)。負が返る保証の無いものを判定に使わず、自分で突き合わせる。マスターは通常数枚なので安い。
static bool16 KCMIsMasterSpread(IDataBase* db, UID spreadUID)
{
	if (db == nil || spreadUID == kInvalidUID)
		return kFalse;
	InterfacePtr<IMasterSpreadList> ml(db, db->GetRootUID(), UseDefaultIID());
	if (ml == nil)
		return kFalse;
	const int32 nm = ml->GetMasterSpreadCount();
	for (int32 m = 0; m < nm; ++m)
		if (ml->GetNthMasterSpreadUID(m) == spreadUID)
			return kTrue;
	return kFalse;
}

// フェーズ2の実データ描画(表示専用。クリック移動等は付けない=ユーザー指定 2026-07-11)。
//   ・背景 = テーマ地色(kInterfacePaletteFill)
//   ・変更ページ(sEntries) = 赤の塗りつぶし
//   ・Add/Remove 登録ページ(IKCMMarkData::GetRegisteredPages。実体は model 側の
//     KCMPageMapCollectRegistered) = 緑の塗りつぶし
// 写像は「文書全体基準」(VS方式)。★2026-07-29 にスクロールバー実物へ合わせて基準を直した:
// 縦の範囲は strip の全高ではなく「つまみが動けるトラック(上下の矢印ボタンの内側)」、Y の分母は
// ページ矩形の全域ではなく「パノラマのスクロール全域(IPanorama::GetBounds。ペーストボード余白込み)」。
// これで帯の位置とつまみの位置が同じ尺になる(詳細は下の写像部のコメント)。各対象ページの Y 帯を
// そのまま帯マークにする(最低3px)のは従来どおり。スクロール位置・ズームに依存しないので、再描画は
// 比較結果が変わったとき(KCMScrollMapInvalidateAll)だけでよい。
// 隠しスプレッド(Hide Unchanged 等)はページ収集の時点で除外する(下記)ので、隠し使用中も
// 表示中スプレッドの現座標だけで正規化され、マーク位置は実表示と一致する。
void KCMScrollMapView::Draw(IViewPort* viewPort, SysRgn updateRgn)
{
	AGMGraphicsContext gc(viewPort, this, updateRgn);
	InterfacePtr<IGraphicsPort> gPort(gc.GetViewPort(), UseDefaultIID());
	if (gPort == nil)
		return;

	AutoGSave autoGSave(gPort);

	const PMRect frame(this->GetInnerContentFrame());

	// 背景: テーマ地色(取得失敗時は中間グレー)。session が終了処理中に nil でも InterfacePtr(p, iid) は
	// nil を許す(InterfacePtr.h:459)ので colors==nil になるだけ=下のグレーへフォールバックする。
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

	// この strip が属する窓の文書を特定し(presentation の GetDocumentUIDRef)、Target 窓か
	// Source 窓かでマークの供給元を切り替える(2026-07-11 ユーザー要望で Source 窓にも表示)。
	// どちらの文書でもない・未 arm・クローズ済みなら背景のみ。
	// ⚠2026-08-19(不具合再検査 B-U8)訂正＝「stripPres は下の『今どのスプレッドを見ているか』でも使う」と
	//   書いてあったが、その問いは KCMStripLayoutView(＝**ペイン**単位)へ移した。ここで presentation を
	//   引くのは**文書を知るため**だけ(GetDocumentUIDRef)。
	InterfacePtr<IWidgetParent> stripParent(this, IID_IWIDGETPARENT);
	InterfacePtr<IDocumentPresentation> stripPres(
		stripParent != nil ? (IDocumentPresentation*)stripParent->QueryParentFor(IID_IDOCUMENTPRESENTATION) : nil);
	IDataBase* const db = (stripPres != nil) ? stripPres->GetDocumentUIDRef().GetDataBase() : nil;
	// ★この Draw だけで3回聞くので InterfacePtr に1回受ける(Utils.h:74-80。2026-08-17 の API 監査 B-U8)。
	//   ここは strip の再描画のたびに通る＝下の marks と同じ扱いに揃える。
	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
	const bool16 isTarget = (db != nil && db == compare->GetArmedTargetDB());
	const bool16 isSource = (!isTarget && db != nil && db == compare->GetArmedSourceDB());
	// ★Find Overset の帯(2026-07-24): 比較(arm)とは独立に、走査した文書(sOversetDB)の窓にも
	//   overset ページを赤帯で出す。比較していない文書でもこの strip は描く(=オーバーセット検査だけでも
	//   地図が出る)。比較と同じ文書なら赤どうしで自然に重なる。
	InterfacePtr<IKCMMarkData> marks(Utils<IKCMMarkData>().QueryUtilInterface());
	const bool16 isOverset = (db != nil && marks->GetOversetOn() &&
		db == marks->GetOversetDB());
	if ((!isTarget && !isSource && !isOverset) || !compare->IsDocDBOpen(db))
		return;

	// 全ページの pasteboard Y 帯をスプレッド順・ページ順で集める。★隠しスプレッド(Hide Unchanged
	// Spreads / ページパネルの Hide Spread)は除外する: 隠すと表示中スプレッドは再配置(座標更新)される
	// のに、隠れたスプレッドは旧座標のまま残るため、含めると正規化が汚れて全マークがズレる
	// (ユーザー報告 2026-07-11。KCMFindPageUnderMouse のヒットテスト除外と同じ理由・同じ判定)。
	// ★★2026-08-17(API 監査 B-U8): **ここは IPageList へ寄せない。** 平坦なページ列を集めるだけなら
	//   IPageList が公式ルートで、KCMCollectPageUIDs は 2026-08-16 の B3 A-3 でそちらへ寄せてある。
	//   だが **IPageList は隠しスプレッドのページも含み、除外する口を持たない**(実測で確認済み＝
	//   `IPageList.h:104` の includePagesOfHiddenSpread は GetPageIndex にしか無い)。この地図は
	//   「隠れているページを載せない」ことが成立条件なので、スプレッドを1つずつ見て
	//   IID_IHIDESPREADBOOLDATA を聞ける ISpreadList の道が要る。⇒ **意図的な非対称であって寄せ漏れではない。**
	// ★★載せるページは「この窓が今どのスプレッドを見ているか」で切り替える(2026-08-11)。
	// マスタースプレッドは通常スプレッドとは別の座標空間に居るので、マスターを表示している窓に
	// 通常ページの帯を並べると、Y の分母(パノラマのスクロール全域=そのときはマスター側の範囲)と
	// 噛み合わず、まったく別の場所に帯が出る。マスター表示中はそのマスタースプレッドのページだけを
	// 載せる(枠も overset も無ければ何も描かれない=自然に空になる)。
	const UID shownSpread = KCMQuerySpreadUIDForView(KCMStripLayoutView(this));
	const bool16 showingMaster = KCMIsMasterSpread(db, shownSpread);

	std::vector<UID> pages;
	if (showingMaster)
	{
		// ★マスター側で隠しフラグを見ないのは、マスタースプレッドが Hide Spread の対象ではないため。
		InterfacePtr<ISpread> spread(db, shownSpread, UseDefaultIID());
		if (spread == nil)
			return;
		const int32 np = spread->GetNumPages();
		for (int32 p = 0; p < np; ++p)
			pages.push_back(spread->GetNthPageUID(p));
	}
	else
	{
		InterfacePtr<ISpreadList> spreadList(db, db->GetRootUID(), UseDefaultIID());
		if (spreadList == nil)
			return;
		const int32 ns = spreadList->GetSpreadCount();
		for (int32 s = 0; s < ns; ++s)
		{
			const UID spreadUID = spreadList->GetNthSpreadUID(s);
			InterfacePtr<IBoolData> hideFlag(db, spreadUID, IID_IHIDESPREADBOOLDATA);
			if (hideFlag != nil && hideFlag->GetBool())
				continue;	// 隠し中のスプレッドは地図に載せない(スクロールでも到達できない)
			InterfacePtr<ISpread> spread(db, spreadUID, UseDefaultIID());
			if (spread == nil)
				continue;
			const int32 np = spread->GetNumPages();
			for (int32 p = 0; p < np; ++p)
				pages.push_back(spread->GetNthPageUID(p));
		}
	}
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
		// ★ページ矩形をペーストボード座標で得るのは Facade の仕事(2026-08-06 ブロック12 監査で寄せた。
		//   ブロック10 で ChangeNav を寄せたときの論点が、ここと KCMCore/KCMPeek に残っていた)。
		//   手本 snapshot/SnapTracker.cpp:610-616 が**ページに対して**同じことをしている。旧実装は
		//   「GetPathBoundingBox + ::InnerToPasteboardMatrix + 自前 Transform」で同じ答えを組んでいた。
		//   ★上の nil 判定は残す: 「この UID は本当に幾何を持つ」を Facade は担保しない(手本も同じ順序)。
		//   ★下の入れ替えも残す: 旧実装がついでに担保していた正規化で、IGeometryFacade.h は返す矩形が
		//   正規化済みだとは明言していない。
		const PMRect box = Utils<Facade::IGeometryFacade>()->GetItemBounds(
			::GetUIDRef(geo), Transform::PasteboardCoordinates(), Geometry::PathBounds());
		PMReal a = box.Top(), b = box.Bottom();
		if (b < a) { PMReal t = a; a = b; b = t; }
		tops[i] = a; bottoms[i] = b;
		if (first) { minY = a; maxY = b; first = kFalse; }
		else { if (a < minY) minY = a; if (b > maxY) maxY = b; }
	}
	if (first || maxY <= minY)
		return;

	// マーク対象の集合。赤の供給元(2026-07-11 に overflow「/」も赤に含めるようユーザー指定):
	//   Target 窓 = 変更ページ(sEntries) + overflow(sOverflowT=登録されていないのに相手が無い「/」)
	//   Source 窓 = 変更ペアの Source 側(sSrcPageToTarget のキー) + overflow(sOverflowS)
	// 緑 = Add/Remove 登録ページ(その db のもの)。両方に該当したら赤を優先。
	// ★overflow キャッシュを現在の文書対へ合わせるのは IsOverflowPage の中でやる(2026-08-13 Task 12)。
	//   キャッシュがどの文書対のものかの照合も向こうが持つ＝ここからは「このページは overflow か」
	//   だけを聞く。合わせ直しは sDB/sSrcDB を書かないので、engineMatch の答えは変わらない。
	const bool16 engineMatch = isTarget ? (marks->GetMarkedTargetDB() == db)
	                                    : (marks->GetMarkedSourceDB() == db);
	std::set<UID> greens;
	marks->GetRegisteredPages(db, greens);

	// 帯の色(背景=テーマ地色との混色で半透明風)。枠(変更)=kKCMScrollMapMarkAlpha、
	// overflow「/」=kKCMScrollMapOverflowAlpha と、赤でも α を分けて濃さに差を付ける
	// (ユーザー指定 2026-07-13。枠はしっかり/「/」は薄く)。緑(登録)は枠と同じ α。
	const PMReal ma = kKCMScrollMapMarkAlpha;
	const PMReal oa = kKCMScrollMapOverflowAlpha;
	const PMReal redR = ma * PMReal(0.85) + (PMReal(1.0) - ma) * bgR;	// 枠(変更)=赤
	const PMReal redG = ma * PMReal(0.08) + (PMReal(1.0) - ma) * bgG;
	const PMReal redB = ma * PMReal(0.08) + (PMReal(1.0) - ma) * bgB;
	const PMReal ovrR = oa * PMReal(0.85) + (PMReal(1.0) - oa) * bgR;	// overflow「/」=薄い赤
	const PMReal ovrG = oa * PMReal(0.08) + (PMReal(1.0) - oa) * bgG;
	const PMReal ovrB = oa * PMReal(0.08) + (PMReal(1.0) - oa) * bgB;
	const PMReal osa = kKCMScrollMapOversetAlpha;						// overset=濃い赤(混色控えめ)
	const PMReal ovsR = osa * PMReal(0.85) + (PMReal(1.0) - osa) * bgR;
	const PMReal ovsG = osa * PMReal(0.08) + (PMReal(1.0) - osa) * bgG;
	const PMReal ovsB = osa * PMReal(0.08) + (PMReal(1.0) - osa) * bgB;
	const PMReal grnR = ma * PMReal(0.10) + (PMReal(1.0) - ma) * bgR;	// 登録=緑
	const PMReal grnG = ma * PMReal(0.70) + (PMReal(1.0) - ma) * bgG;
	const PMReal grnB = ma * PMReal(0.25) + (PMReal(1.0) - ma) * bgB;

	// ★写像の基準をスクロールバーに合わせる(2026-07-29 の修正。ユーザー報告のキャプチャで確定)。
	// 従来は「ページ矩形の Y 全域 → strip の全高」に線形写像していたが、実際のバーとは2点で食い違う:
	//   ① つまみが動けるのは上下の矢印ボタン(「^」「v」)の内側だけなのに、strip はバーと同じ全高に
	//      描いていた(上端で +ボタン高・中央で 0・下端で -ボタン高 の系統的なズレ。キャプチャでは
	//      1ページ目の帯が上矢印ボタンの真横=つまみが絶対に来られない位置に出ていた)。
	//   ② バーの全長が表すのはページの範囲ではなく「パノラマのスクロール全域」(ページの上下にある
	//      ペーストボード余白を含む)。
	// ①はバーの frame 幅(=正方形ボタンの高さ)、②は IPanorama::GetBounds() で、どちらも実行時に読む。
	PMReal arrowH(0), panoTop(0), panoBottom(0);
	bool16 hasPano = kFalse;
	KCMScrollMapProbeWindow(this, arrowH, panoTop, panoBottom, hasPano);

	// バーの frame は親ローカル座標、frame(GetInnerContentFrame)は strip 自身のローカル座標。通常は
	// 1:1 だが、縦の縮尺が違う場合に備えて strip の外形高さとの比で換算しておく。
	const PMReal outerH = this->GetFrame().Height();
	if (outerH > 0 && frame.Height() > 0)
		arrowH = arrowH * frame.Height() / outerH;

	// トラックの追い込み(実機で決めた採用値。宣言部のコメント参照)。つまみが動ける範囲は矢印ボタンの
	// 内側よりさらに少し狭いので、そのぶんを引いてから写像する。
	arrowH = arrowH + kKCMScrollMapTrackInset;

	PMReal trackTop    = frame.Top() + arrowH;		// つまみが動ける範囲(=地図を描くべき範囲)
	PMReal trackBottom = frame.Bottom() - arrowH;
	if (trackBottom - trackTop < PMReal(8.0))		// 窓が極端に低い/バーが引けない → 補正をあきらめて全高
	{
		trackTop    = frame.Top();
		trackBottom = frame.Bottom();
	}

	// 分母。パノラマ全域がページ全域を包含していれば採用する。包含していない=座標系が想定と違う
	// (または隠しスプレッド等で食い違う)ときは従来どおりページ矩形の全域を使う。
	PMReal spanTop = minY, spanBottom = maxY;
	if (hasPano && panoBottom > panoTop &&
		panoTop <= minY + PMReal(1.0) && panoBottom >= maxY - PMReal(1.0))
	{
		spanTop    = panoTop;
		spanBottom = panoBottom;
	}

	const PMReal scale = (trackBottom - trackTop) / (spanBottom - spanTop);

	// ★各ページの色区分と帯座標(y0/y1)を先に決め、優先度別の添字リスト(byLevel)へ振り分ける。見開きの
	//   2ページ(例: 4p と 5p)は同一スプレッドで pasteboard Y 帯が同じ位置に重なるため、単純にページ順で
	//   描くと後のページが上書きする(ユーザー報告 2026-07-24: 4p=overset・5p=変更 だと変更色が overset を
	//   上書きしていた)。そこで優先度(overset > 変更 > overflow > 登録)の低い順にまとめて描き、高い優先度が
	//   必ず上=勝つようにする。ページ内の優先(280行相当)は 1 ページ 1 レベルの決定で吸収し、別ページ同士の
	//   重なりは描画順で解決。level: 1=登録(緑) / 2=overflow「/」(薄赤) / 3=変更(赤) / 4=overset(濃赤)。
	std::vector<size_t> byLevel[5];	// [1..4]=そのレベルに属するページ添字(0は未使用)。描画は合計 N ループで済む
	std::vector<PMReal> y0s(pages.size()), y1s(pages.size());
	for (size_t i = 0; i < pages.size(); ++i)
	{
		if (bottoms[i] <= tops[i])
			continue;	// 幾何が取れなかったページ
		bool16 isRed = kFalse;			// 枠(変更ページ)由来の赤
		bool16 isOverflowRed = kFalse;	// overflow「/」由来の赤(枠とは別の薄い赤にする)
		if (engineMatch)
		{
			if (isTarget)
				isRed = marks->HasEntryForPage(pages[i]);
			else
				isRed = marks->IsSourcePageMarked(pages[i]);
		}
		if (!isRed && marks->IsOverflowPage(db, pages[i], isTarget))
		{
			isRed = kTrue;			// 純粋な overflow(変更ではない)だけ薄い赤にする
			isOverflowRed = kTrue;	// 変更ページが overflow にも入る場合は上で先に確定=枠色優先
		}
		const bool16 isGreen = (!isRed && greens.find(pages[i]) != greens.end());
		// ★overset ページ(この文書が sOversetDB のとき)=しっかりした赤(＋マークと揃える。ユーザー指定 2026-07-24)。
		const bool16 isOversetRed = (isOverset && marks->IsOversetPage(pages[i]));

		int32 c = 0;
		if (isOversetRed)   c = 4;					// overset = 最優先(最後に描いて上へ)
		else if (isRed)     c = isOverflowRed ? 2 : 3;	// 変更=3 / 純 overflow「/」=2
		else if (isGreen)   c = 1;					// 登録(緑)
		if (c == 0)
			continue;

		PMReal y0 = trackTop + (tops[i]    - spanTop) * scale;
		PMReal y1 = trackTop + (bottoms[i] - spanTop) * scale;
		if (y1 - y0 < PMReal(3.0))	// 細くなり過ぎたら中心を保って3pxに
		{
			const PMReal cy = (y0 + y1) / PMReal(2.0);
			y0 = cy - PMReal(1.5);
			y1 = cy + PMReal(1.5);
		}
		if (y0 < trackTop)    y0 = trackTop;		// 矢印ボタンの横には出さない(つまみが来られない位置)
		if (y1 > trackBottom) y1 = trackBottom;

		y0s[i] = y0;
		y1s[i] = y1;
		byLevel[c].push_back(i);	// c は 1..4(c==0 は上で continue 済み)
	}

	// 優先度の低い順(1→4)に描く=高い優先度が上(最後)に来て、同スプレッドの重なりでも必ず勝つ。
	// 色設定は各レベルで1回だけ。描画は byLevel の添字だけを辿るので全体で合計 N 回の fill で済む。
	for (int32 level = 1; level <= 4; ++level)
	{
		if (byLevel[level].empty())
			continue;
		switch (level)
		{
			case 1: gPort->setrgbcolor(grnR, grnG, grnB); break;	// 登録=緑
			case 2: gPort->setrgbcolor(ovrR, ovrG, ovrB); break;	// 純 overflow「/」= 薄い赤
			case 3: gPort->setrgbcolor(redR, redG, redB); break;	// 変更 = しっかりした赤
			case 4: gPort->setrgbcolor(ovsR, ovsG, ovsB); break;	// overset = 濃い赤(最優先)
		}
		for (size_t k = 0; k < byLevel[level].size(); ++k)
		{
			const size_t i = byLevel[level][k];
			gPort->rectpath(PMRect(frame.Left(), y0s[i], frame.Right(), y1s[i]));
			gPort->fill();
		}
	}
}

//========================================================================================
// 注入/取り外し
//========================================================================================

// db のレイアウトビュー群から、それぞれが属する文書ウィンドウ(presentation)を重複なしで集める。
// 戻りは presentation の IPanelControlData(addref 済み)を out に積む。
// GetAllLayoutViews の戻り(IControlView*)は既存 KCM コードと同じく非所有として扱う。
static void KCMCollectPresentationPanels(IDataBase* db, K2Vector<IPanelControlData*>& out)
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

		// 線形探索は std::find で書く(手本=Adobe 製品コード。K2Vector に対する実例=
		// open/components/incopyfileactions/InCopyDocFileHandler.cpp:268)。
		IPMUnknown* const presKey = (IPMUnknown*)(IDocumentPresentation*)pres;
		if (std::find(seen.begin(), seen.end(), presKey) != seen.end())
			continue;
		seen.push_back(presKey);

		InterfacePtr<IPanelControlData> panel(pres, UseDefaultIID());
		if (panel == nil)
			continue;
		panel->AddRef();
		out.push_back(panel);
	}
}

// KCMScrollMapAttach(KCMScrollMap.h 参照) — targetDB の各文書ウィンドウに strip を注入する。
void KCMScrollMapAttach(IDataBase* targetDB)
{
	if (!sScrollMapOn)
		return;	// 「Show Scrollbar Map」OFF 中は strip を注入しない(Start しても地図は出ない)
	if (targetDB == nil)
		return;

	K2Vector<IPanelControlData*> panels;
	KCMCollectPresentationPanels(targetDB, panels);

	for (int32 i = 0; i < (int32)panels.size(); ++i)
	{
		InterfacePtr<IPanelControlData> presPanel(panels[i]);	// 所有権引き取り(Release 担当)

		// 二重注入ガード(窓単位)
		if (presPanel->FindWidget(kKCMScrollMapWidgetID) != nil)
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
		// ★2026-08-17(API 監査 B-U9 の A-3)＝C スタイルキャストの ::CreateObject から**型つきの
		//   ::CreateObject2<IControlView>(db, spec)** へ(CreateObject.h:190-203)。
		//   **公式実例＝`widgetbin/treeview/CTreeViewWidgetMgr.cpp:519`** が行ごと同型
		//   (`::CreateObject2<IControlView>(::GetDataBase(this), fCurrentStyleRsrcSpec)`)。
		//   ⚠この形は IID を渡さず FACE::kDefaultIID を使う＝`IControlView` の既定 IID は
		//     IID_ICONTROLVIEW なので、旧コードが明示していた IID と同じものが要求される。
		//   ★ここは B-U8 の担当ファイルだが、**命題はブロックに属さない**(B5 の教訓)ので
		//     「CreateObject＋手キャスト」を数えたこの回にまとめて直した。
		InterfacePtr<IControlView> strip(::CreateObject2<IControlView>(
			::GetDataBase(sbParentPanel),
			RsrcSpec(LocaleSetting::GetLocale(), kKCMUIPluginID, kViewRsrcType, kKCMScrollMapRsrcID)));
		if (strip == nil)
			continue;

		sbParentPanel->AddWidget(strip);	// 末尾追加=描画順で最前面

		// バーの左隣・同じ高さ。座標はバーと同じ親ローカル。binding はバーのものをコピー
		// (右端固定+上下ストレッチ相当のはず。実際に何が入っているかはプローブで観察)。
		const PMRect sbFrame = sbView->GetFrame();
		const PMReal stripLeft = sbFrame.Left() - kKCMScrollMapWidth;
		PMRect stripFrame(stripLeft, sbFrame.Top(), sbFrame.Left(), sbFrame.Bottom());
		strip->SetFrame(stripFrame);
		strip->SetFrameBinding(sbView->GetFrameBinding());
		strip->ShowView();
		strip->Invalidate();

		// ★strip の列をレイアウトビューから「専有」する(実機で確認した残像対策 2026-07-11)。
		// レイアウトビューはスクロールを画面ピクセルのずらしコピー(blit)で高速化しており、ビューの
		// 領域に strip が重なっていると strip のピクセルごと横/縦にコピーされて残像になる。そこで、
		// strip 列に右端が食い込んでいる兄弟(=レイアウトビュー)の右端を strip の左端まで詰めて、
		// 重なりをゼロにする(縦スクロールバーと縦帯が重なる兄弟だけが対象。下端の横スクロールバーや
		// 上端のルーラーは縦範囲が重ならないので触らない)。取り外し時に元へ戻す(Detach 側)。
		const int32 numSiblings = sbParentPanel->Length();
		for (int32 c = 0; c < numSiblings; ++c)
		{
			IControlView* sib = sbParentPanel->GetWidget(c);
			if (sib == nil || sib == sbView || sib == (IControlView*)strip)
				continue;
			PMRect sf = sib->GetFrame();
			if (sf.Right() > stripLeft && sf.Left() < stripLeft &&
				sf.Top() < sbFrame.Bottom() && sf.Bottom() > sbFrame.Top())
			{
				sf.Right() = stripLeft;
				sib->SetFrame(sf);
				sib->Invalidate();
			}
		}
	}
}

// KCMScrollMapDetachAll(KCMScrollMap.h 参照) — 全文書の全ウィンドウから strip を取り外す。
void KCMScrollMapDetachAll()
{
	K2Vector<IPanelControlData*> panels;
	KCMCollectPresentationPanels(nil, panels);	// db=nil で全レイアウトビュー

	for (int32 i = 0; i < (int32)panels.size(); ++i)
	{
		InterfacePtr<IPanelControlData> presPanel(panels[i]);	// 所有権引き取り(Release 担当)

		IControlView* strip = presPanel->FindWidget(kKCMScrollMapWidgetID);
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

		// Attach 時に strip 列ぶん右端を詰めた兄弟(=レイアウトビュー)を元の幅へ戻す。
		// 「右端が strip の左端に(ほぼ)一致し、縦帯が重なる兄弟」= 詰めた本人。strip の右端
		// (=スクロールバーの左端)まで広げ直す。
		const PMRect stripFrame = strip->GetFrame();
		const int32 numSiblings = parentPanel->Length();
		for (int32 c = 0; c < numSiblings; ++c)
		{
			IControlView* sib = parentPanel->GetWidget(c);
			if (sib == nil || sib == strip)
				continue;
			PMRect sf = sib->GetFrame();
			const PMReal gap = abs(sf.Right() - stripFrame.Left());
			if (gap <= PMReal(0.5) &&
				sf.Top() < stripFrame.Bottom() && sf.Bottom() > stripFrame.Top())
			{
				sf.Right() = stripFrame.Right();
				sib->SetFrame(sf);
				sib->Invalidate();
			}
		}

		parentPanel->RemoveWidget(strip, kTrue, kTrue);
	}
}

// KCMScrollMapInvalidateAll(KCMScrollMap.h 参照) — 注入済みの全 strip を再描画する。
// ⚠2026-08-17 訂正(API 監査 B-U8): 旧記述は「呼び所は2箇所(①KCMDoMarkChangesDoc の末尾
// ②KCMPeek.cpp のスプレッド再比較)」だったが、**どちらも model 側でこの UI 関数を呼べない**。
// 分割で「比較が動いた」は通知になり、受け手の UI が地図を描き直す形になっている。
// 全数 Grep での現状は**8箇所**＝KCMModelChangeObserver.cpp(4＝全再比較/部分再比較/overset/クローズ)、
// KCMActionComponent.cpp(2＝地図トグル ON と Find Overset)、KCMPeekGesture.cpp(1＝一括クローズ完了)、
// **このファイル自身(1＝下の KCMScrollMapNoticeDrawEvent＝手動 Hide/Show とスプレッド切替の検出)**。
// ⚠2026-08-19(不具合再検査 B-U8)訂正＝2026-08-17 に「7箇所」と数えて3ファイルを名指ししたとき、
//   **自分のファイルの中にある8つ目を数え落としていた**。しかもそれは「他の7つでは捕まらない変化を
//   拾うための独立経路」＝下の一文がいちばん大事だと言っている当のもの。
//   ★**呼び手を数えるときは自分のファイルも母集合に入れる**(B-U3 で「呼び元は2つだけ」の3つ目が
//     同じファイルの180行上にいたのと同型)。
// ★数より大事なのは「独立経路が複数ある」ことで、それは分割後も変わっていない
// (＝比較の再実行を1か所で捕まえることはできないので、増えたら都度ここを呼ぶ)。
void KCMScrollMapInvalidateAll()
{
	K2Vector<IPanelControlData*> panels;
	KCMCollectPresentationPanels(nil, panels);	// db=nil で全レイアウトビュー

	for (int32 i = 0; i < (int32)panels.size(); ++i)
	{
		InterfacePtr<IPanelControlData> presPanel(panels[i]);	// 所有権引き取り(Release 担当)
		IControlView* strip = presPanel->FindWidget(kKCMScrollMapWidgetID);
		if (strip != nil)
			strip->Invalidate();
	}
}

//========================================================================================
// 手動 Hide/Show Spread の検出(スプレッド描画イベント便乗+スロットル)
//========================================================================================

// db の「スプレッド構成+隠しフラグ」の指紋。隠し/再表示・スプレッド増減で必ず値が変わる。
// db が nil/クローズ済みなら 0(=arm 解除後は両指紋 0 で安定し、比較は常に一致)。
static uint32 KCMHiddenFingerprint(IDataBase* db)
{
	if (db == nil || !Utils<IKCMCompareFacade>()->IsDocDBOpen(db))
		return 0;
	InterfacePtr<ISpreadList> spreadList(db, db->GetRootUID(), UseDefaultIID());
	if (spreadList == nil)
		return 0;
	uint32 h = 0;
	const int32 ns = spreadList->GetSpreadCount();
	for (int32 s = 0; s < ns; ++s)
	{
		const UID uid = spreadList->GetNthSpreadUID(s);
		InterfacePtr<IBoolData> hideFlag(db, uid, IID_IHIDESPREADBOOLDATA);
		const uint32 hidden = (hideFlag != nil && hideFlag->GetBool()) ? 1u : 0u;
		h = h * 131u + (uid.Get() << 1) + hidden;
	}
	return h;
}

// db の窓が「今どのマスタースプレッドを見ているか」の指紋(2026-08-11)。
// ★地図に載せるページは表示中スプレッドで変わる(マスター表示中はそのマスターのページだけ)のに、
//   スプレッドの切り替えは KCM のどのフックも通らない。必ず再描画は起こるので、隠しフラグと
//   同じ便乗経路で拾う。
// ★通常スプレッドは 0 に畳む: 通常スプレッドの間を移動しても地図は全ページを載せたままで中身が
//   変わらないため、そこで Invalidate しても再描画が無駄になるだけ。
static uint32 KCMShownMasterFingerprint(IDataBase* db)
{
	if (db == nil || !Utils<IKCMCompareFacade>()->IsDocDBOpen(db))
		return 0;
	K2Vector<IControlView*> views;
	Utils<ILayoutViewUtils>()->GetAllLayoutViews(views, nil, db);
	uint32 h = 0;
	for (int32 i = 0; i < (int32)views.size(); ++i)
	{
		// ★「そのビューは今どのスプレッドか」は KCMQuerySpreadUIDForView に一本化してある
		//   (2026-08-19・不具合再検査 B-U8。引けなければ kInvalidUID＝マスターではないと扱われる)。
		const UID shown = KCMQuerySpreadUIDForView(views[i]);	// nil ビューは中で弾く
		h = h * 131u + (KCMIsMasterSpread(db, shown) ? shown.Get() : 0u);
	}
	return h;
}

static std::chrono::steady_clock::time_point sHiddenCheckLast;	// 前回チェック時刻(スロットル用)
static bool16 sHiddenCheckStarted = kFalse;	// 一度でもチェックしたか(初回は必ず通す。time_point 既定値との比較を避ける)
// 指紋は「隠しフラグ構成」と「表示中マスタースプレッド」の合成(2026-08-11 に後者を追加)。
// どちらが変わっても地図の中身が変わるので、1本の数にまとめて比較する。
static uint32 sHiddenFingerT = 0;			// 前回の Target 側指紋
static uint32 sHiddenFingerS = 0;			// 前回の Source 側指紋
static uint32 sHiddenFingerO = 0;			// 前回の overset 走査文書側指紋(Find Overset 単独時の隠し追従)

// KCMScrollMapNoticeDrawEvent(KCMScrollMap.h 参照) — 描画イベントごとに呼ばれる軽量チェック。
// 250ms スロットル内は時刻比較1回で即 return。指紋が変わっていたら地図を Invalidate する
// (strip は専有列にいてレイアウトビューと重ならないので、描画イベント中の Invalidate でも
// スプレッド再描画→再検出の無限ループにはならない)。
void KCMScrollMapNoticeDrawEvent()
{
	if (!sScrollMapOn)
		return;		// 「Show Scrollbar Map」OFF 中は strip も無い=毎描画の指紋計算を省く
	// 未 arm でも Find Overset 単独なら strip があり得るので、その場合は続行する(2026-07-24)。
	// ★この関数だけで3回聞くので InterfacePtr に1回受ける(Utils.h:74-80。2026-08-17 の API 監査 B-U8)。
	//   ここは**描画イベントごと**に通る経路なので、marks と同じ扱いに揃える。
	InterfacePtr<IKCMMarkData> marks(Utils<IKCMMarkData>().QueryUtilInterface());
	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
	if (compare->GetArmedTargetDB() == nil &&
		!(marks->GetOversetOn() && marks->GetOversetDB() != nil))
		return;		// arm も overset も無い = strip も無い(指紋は無意味なので触らない)

	// スロットル(250ms)。steady_clock は単調増加なのでラップ/負 delta の心配は無い(旧 clock_t 版に
	// あった 32bit ラップ対策は不要になった)。初回(sHiddenCheckStarted=kFalse)は必ず通す。
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	if (sHiddenCheckStarted)
	{
		const long long deltaMs =
			std::chrono::duration_cast<std::chrono::milliseconds>(now - sHiddenCheckLast).count();
		if (deltaMs < 250)
			return;
	}
	sHiddenCheckStarted = kTrue;
	sHiddenCheckLast = now;

	IDataBase* const tDB = compare->GetArmedTargetDB();
	IDataBase* const sDB = compare->GetArmedSourceDB();
	IDataBase* const oDB = marks->GetOversetOn() ? marks->GetOversetDB() : nil;
	const uint32 ft = KCMHiddenFingerprint(tDB) * 31u + KCMShownMasterFingerprint(tDB);
	const uint32 fs = KCMHiddenFingerprint(sDB) * 31u + KCMShownMasterFingerprint(sDB);
	const uint32 fo = KCMHiddenFingerprint(oDB) * 31u + KCMShownMasterFingerprint(oDB);
	if (ft != sHiddenFingerT || fs != sHiddenFingerS || fo != sHiddenFingerO)
	{
		sHiddenFingerT = ft;
		sHiddenFingerS = fs;
		sHiddenFingerO = fo;
		KCMScrollMapInvalidateAll();	// 初回(0→現指紋)の1回だけ余計に走るが無害
	}
}

// ── 有効/無効フラグ(フライアウト「Show Scrollbar Map」トグル。既定 ON) ─────────────────
// フラグの反転に伴う strip の attach / detach は呼び手が担う。ここは値の保持だけ。
// ★呼び手は2つで、後始末をするのは①だけ＝理由と「不具合ではない」根拠は KCMScrollMap.h の
//   KCMSetScrollMapEnabled の宣言に書いてある(2026-08-19・不具合再検査 B-U8)。
bool16 KCMGetScrollMapEnabled()      { return sScrollMapOn; }
void   KCMSetScrollMapEnabled(bool16 on) { sScrollMapOn = on; }

// KCMScrollMap.cpp 終わり。
