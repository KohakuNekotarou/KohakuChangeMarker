//========================================================================================
//
//  KESCMViewLookup.cpp
//
//  レイアウトビューに向かって聞く問い(KESCMCore.cpp から分離。2026-08-13 の model/UI 分割
//  第1段 Task 3)。マウスがビューの content 座標のどこに居るか・実際にマウスが乗っているのは
//  どのビューか(Split Window 対応)・そのビューはどの文書のものか、の3つ。
//
//  ★分離では関数の中身を1行も変えていない。変えたのは「どのファイルに座るか」と「誰から見えるか」だけ。
//    「直前にヒットした文書」ヒント(sLastViewHitDb)と、それを使う照合ヘルパも一緒に移した
//    ——読み書きする3関数が全部ここへ来るので、状態が分割の両側に割れることはない。
//
//  UI 側: どの関数も IControlView を受け取るか返すので、model プラグインからは触れない。
//  ⚠この時点では model 側(KESCMColorSampler.cpp / KESCMPeek.cpp)からも呼ばれている＝逆流。
//    その全量を確定させるのは Task 4、切るのは Task 9/10。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "PersistUtils.h"			// ::GetUIDRef(doc)(view→文書→db)
#include "ISession.h"				// GetExecutionContextSession(全文書走査のフォールバック)
#include "IApplication.h"			// QueryDocumentList(同上)
#include "IDocumentList.h"			// GetNthDoc / GetDocCount(同上)
#include "IDataBase.h"
#include "IDocument.h"
#include "IControlView.h"
#include "IEventUtils.h"			// GetGlobalMouseLocation(マウスの画面座標)
#include "IWindow.h"
#include "IWindowUtils.h"			// QueryWindowUnderPoint(マウス下の窓)
#include "IDocumentPresentation.h"
#include "IPanelControlData.h"		// FindWidget(ヒットテストと代表ビューの取得)
#include "LayoutUIID.h"				// kLayoutWidgetBoss / kLayoutSecondaryPanelWidgetID
#include "ILayoutViewUtils.h"		// GetAllLayoutViews(KESCMFindDocDbForView のフォールバック)
#include "ILayoutControlData.h"		// GetDocument(view→文書の公式ルート。KESCMFindDocDbForView)
#include "K2Vector.h"				// GetAllLayoutViews の out コンテナ(間接includeに頼らず明示)
#include "PMPoint.h"
#include "PMReal.h"

#include "KESCMViewLookup.h"
#include "KESCMCore.h"				// KESCMIsDocDBOpen(公式ルートで得た db の生存確認)

//========================================================================================
// マウス位置・ヒットテストの共有ヘルパ(peek と色サンプラが同じ流儀でカーソル位置を求める)。
//========================================================================================
bool16 KESCMQueryMouseContentPoint(IControlView* view, PMReal& outX, PMReal& outY)
{
	outX = 0.0; outY = 0.0;
	if (view == nil)
		return kFalse;
	// マウス: 画面 → 窓 → コンテンツ(ペーストボード)座標。
	GSysPoint gm = Utils<IEventUtils>()->GetGlobalMouseLocation();
	PMPoint pt((PMReal)gm.x, (PMReal)gm.y);
	pt = view->GlobalToWindow(pt);
	view->WindowToContentTransform(&pt);
	outX = pt.X();
	outY = pt.Y();
	return kTrue;
}

// マウス下のレイアウトビューを求める(Split Window対応)。KESCMViewLookup.h のコメント参照。
IControlView* KESCMQueryViewUnderMouse()
{
	GSysPoint globalPt = Utils<IEventUtils>()->GetGlobalMouseLocation();

	InterfacePtr<IWindow> hitWindow(Utils<IWindowUtils>()->QueryWindowUnderPoint(globalPt, kFalse));
	if (hitWindow == nil)
		return nil;

	InterfacePtr<IDocumentPresentation> hitPres(hitWindow, UseDefaultIID());
	if (hitPres == nil)
		return nil;

	InterfacePtr<IPanelControlData> hitPanelData(hitPres, UseDefaultIID());
	if (hitPanelData == nil)
		return nil;

	IControlView* primaryView = hitPanelData->FindWidget(kLayoutWidgetBoss);
	if (primaryView == nil)
		return nil;

	// primaryView は「グローバル→ウィンドウ座標への変換」にだけ使う(どの子ウィジェット経由でも同じ
	// ウィンドウ座標系になるため)。実際にマウス下にあるビューは FindWidget(windowPt) のヒットテストで
	// 特定する(キャンバス以外=ルーラ等に当たった場合は primaryView にフォールバック)。
	IControlView* hitView = primaryView;
	const PMPoint globalPM((PMReal)globalPt.x, (PMReal)globalPt.y);
	const PMPoint winPM = primaryView->GlobalToWindow(globalPM);
	SysPoint winPt;
	winPt.x = ::ToInt32(winPM.X());
	winPt.y = ::ToInt32(winPM.Y());

	IControlView* pointHit = hitPanelData->FindWidget(winPt);
	if (pointHit != nil &&
	    (pointHit->GetWidgetID() == kLayoutWidgetBoss || pointHit->GetWidgetID() == kLayoutSecondaryPanelWidgetID))
		hitView = pointHit;

	hitView->AddRef();	// QueryFrontView() と同じ「+1 ref、呼び出し側で Release」の契約に合わせる
	return hitView;
}

// view が db のレイアウトビュー群に含まれるか(1文書ぶんのポインタ照合)。下の2用途で共有する。
static bool16 KESCMViewBelongsToDb(IControlView* view, IDataBase* db)
{
	if (view == nil || db == nil)
		return kFalse;
	K2Vector<IControlView*> views;
	Utils<ILayoutViewUtils>()->GetAllLayoutViews(views, nil, db);	// 閉じた db なら空が返るだけ=安全
	for (int32 vi = 0; vi < (int32)views.size(); ++vi)
		if (views[vi] == view)
			return kTrue;
	return kFalse;
}

// ★直前にヒットした文書(2026-07-25 追補)。ホットパス最適化のためだけの「当たりを付ける」ヒント。
//   Sync Layout Views のスクロール追従とツールカーソルの黒/白判定は、同じビューについて連続で
//   (マウス移動のたび=数十回/秒)この関数を呼ぶ。毎回「全文書 × GetAllLayoutViews」を回すと
//   文書数ぶんの K2Vector 構築が積み上がるので、まず前回の db だけを試す。
//   ★誤りが混入しない作り: ヒントは「どの db から試すか」を決めるだけで、答えは必ず
//   KESCMViewBelongsToDb による実照合で確定する。外れたら従来どおり全走査へフォールバックする。
//   閉じた db が残っていても GetAllLayoutViews が空を返して外れるだけ(deref しない)。
static IDataBase* sLastViewHitDb = nil;

// 直前ヒントを捨てる(KESCMViewLookup.h で宣言)。文書クローズ・arm 切替・同期 OFF から呼ぶ。
void KESCMForgetViewDbHint()
{
	sLastViewHitDb = nil;
}

// view がどの文書のレイアウトビューかを特定する。KESCMViewLookup.h のコメント参照。
// (2026-07-25: KESCMPeek.cpp の file-static から共有ヘルパへ移動。色サンプラの窓ガードでも使うため)
IDataBase* KESCMFindDocDbForView(IControlView* view)
{
	if (view == nil)
		return nil;

	// ★①公式ルート(2026-08-06 の API 監査 A-1): レイアウトビュー boss は ILayoutControlData を持ち、
	//   そのビューが今表示している文書を直接返す。ビュー自身に聞くので列挙 API の結果に依存しない。
	//   契約=ILayoutControlData.h:181 / 手本=CPathCreationTracker.cpp:277-285(ほか
	//   CusDtLnkUIDDTargetFlavorHelper.cpp:197 / BscDNDCustomFlavorHelper.cpp:194)。
	//   boss に載っていることは実機ダンプで確認済み(kLayoutWidgetBoss + IID_ILAYOUTCONTROLDATA)。
	//   ★GetDocument() は AddRef しない生ポインタを返す(=Release 不要)。
	InterfacePtr<ILayoutControlData> layoutData(view, IID_ILAYOUTCONTROLDATA);
	if (layoutData != nil)
	{
		IDocument* doc = layoutData->GetDocument();
		if (doc != nil)
		{
			IDataBase* db = ::GetUIDRef(doc).GetDataBase();
			// ★生存確認を明示的に行う(2026-08-06 の自己レビューで追加)。従来の経路は IDocumentList を
			//   走査して照合していたので「返る db は必ず開いている文書のもの」が**暗黙に保証**されていた。
			//   公式ルートはビューに聞くだけなのでその保証が無く、黙って落とすと KESCM 全体の規約
			//   (閉じた db を持ち回らない/deref しない)が崩れる。ここで確認して従来の性質を保つ。
			//   引けなければ下のフォールバックへ流す(全走査でも見つからなければ nil)。
			if (KESCMIsDocDBOpen(db))
				return db;
		}
	}

	// ---- 以下は①が引けなかったときのフォールバック(従来実装) ----
	// ★Split Window の 2枚目ペインでも ILayoutControlData が引けるかは実機未確認なので、旧経路を
	//   残してある(引けるなら以下は一度も走らない)。実機で確認が取れたら丸ごと畳んでよい。

	// ②前回ヒットした文書を先に試す(連続呼び出しはほぼここで確定する)。
	if (sLastViewHitDb != nil && KESCMViewBelongsToDb(view, sLastViewHitDb))
		return sLastViewHitDb;

	// ③外れたら全文書を走査(従来どおり)。見つかった db を次回のヒントにする。
	ISession* session = GetExecutionContextSession();	// 終了処理中は nil になり得る(下の共通規約参照)
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return nil;
	const int32 docCount = docList->GetDocCount();
	for (int32 d = 0; d < docCount; ++d)
	{
		IDocument* doc = docList->GetNthDoc(d);
		if (doc == nil)
			continue;
		IDataBase* db = ::GetUIDRef(doc).GetDataBase();
		if (db == sLastViewHitDb)
			continue;	// ②で試して外れている
		if (KESCMViewBelongsToDb(view, db))
		{
			sLastViewHitDb = db;
			return db;
		}
	}
	return nil;
}

// KESCMViewLookup.cpp 終わり。
