//========================================================================================
//
//  KESCMPeek.cpp
//
//  ミドルボタン peek の実装(KESCMScriptProvider.cpp から分離)。peek 状態、ミドルボタン＋修飾キーを
//  スヌープする IEventWatcher、起動サービス、KESCMCore.h で宣言した arm/disarm/状態アクセサの入口を持つ。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// オブジェクトモデル:
#include "PersistUtils.h"
#include "IDataBase.h"
#include "IGeometry.h"
#include "IDocument.h"
#include "IEventUtils.h"
#include "IApplication.h"
#include "IDocumentList.h"
#include "ISpread.h"
#include "ISpreadList.h"
#include "IShape.h"
#include "ISession.h"
#include "IWindow.h"
#include "IWindowUtils.h"
#include "IDocumentPresentation.h"
#include "IPanelControlData.h"
#include "ILayoutViewUtils.h"		// GetAllLayoutViews(Split Window両ペインのIControlView*取得)
#include "ILayoutUIUtils.h"			// MakeZoomCmd(kZoomToCmdBoss。ビューポート同期のズーム)
#include "CmdUtils.h"				// ProcessCommand(ズームコマンド実行)
#include "ICommand.h"

// レイアウトビュー同期(Sync Layout Views)用:
#include "CObserver.h"				// 同期オブザーバの基底(手本=work/KESLayoutScrollObserver.cpp)
#include "ISubject.h"				// AttachObserver/DetachObserver/IsAttached
#include "IActiveContext.h"			// IID_IACTIVECONTEXT / ContextInfo(文書切替の検知)
#include "widgetid.h"				// IID_IPANORAMA / kScrollToMessage・kScrollByMessage・kScaleToMessage・kScaleByMessage

// イベント監視 / ツール / 起動:
#include "IEventWatcher.h"
#include "IEvent.h"
#include "IEventDispatcher.h"
#include "IStartupShutdownService.h"
#include "CreateObject.h"
#include "CPMUnknown.h"
#include "IToolBoxUtils.h"
#include "ITool.h"
#include "LayoutUIID.h"
#include "DocumentContextID.h"

// ジオメトリ / ビュー:
#include "IControlView.h"
#include "IPanorama.h"
#include "IWidgetParent.h"
#include "PMMatrix.h"
#include "PMPoint.h"
#include "PMReal.h"
#include "TransformUtils.h"

#include <vector>
#include <map>

// プロジェクト内インクルード:
#include "KESCMID.h"
#include "KESCMConstants.h"
#include "KESCMDrawEventHandler.h"   // エンジンの共有 static ＋ KESCMQueryPanorama
#include "KESCMColorSampler.h"       // KESCMSampleCmykUnderMouse
#include "KESCMCore.h"               // KESCMCollectPageUIDs ＋ arm/disarm/状態 宣言
#include "KESCMPeek.h"

//========================================================================================
// ミドルボタン peek — 共有状態とヘルパ。
//   ミドルボタンを押している間だけ、マウス下スプレッドの旧版を不透明べた載せし、離すと隠す。
//   IEventWatcher はグローバル(引数を持てない)ので、比較相手の旧ドキュメントは先に
//   KESCMDoArmMousePeek(KESCMCore.h)で登録しておく(パネルの Start ボタンが呼ぶ)。watcher
//   (KESCMPeekWatcher)がこの arm 状態を見る。全部を1つの翻訳単位に置くことで、watcher が MakeOrigImage /
//   マウス下スプレッド判定 / sOrigImages を直接再利用できる。
//========================================================================================
static IDataBase* sPeekTargetDB = nil;	// 表示中(新)ドキュメント。使用前に「まだ開いているか」を検証する。
static IDataBase* sPeekSourceDB = nil;	// peek 中に重ねる旧ドキュメント。
static bool16     sPeekArmed    = kFalse;

// Shift＋ミドル=旧版を不透明(100%)で / Shift+Alt＋ミドル=旧版を 50% で重ねて peek。
// 押下中だけ表示し、ミドルを離すと消す(修飾キーは離してもよい)。判定はミドル押下時に1回見るだけ。
static const PMReal kKESCMPeekSemiOpacity = 0.5;	// Shift+Alt＋ミドル時の旧版の不透明度(0..1)
static bool16 sPeekActive        = kFalse;	// Shift/Shift+Alt+ミドルを押し込み中(=覗き表示中)か
static bool16 sSingleShowing     = kFalse;	// 修飾なしミドル押下中(=全マークを選択不透明度25%/75%で一時表示中)か。離すと隠す＋基準opacityへ
// ミドル押下中だけハンドツール(掴んで移動)に一時切替。離すと元のツールへ戻す。
static ITool*  sSavedTool  = nil;	// 切替前のツール(ref を保持。Restore で Release)
static bool16  sHandActive = kFalse;	// ハンドツールに一時切替中か

// 画面マークの「基準」不透明度(=ミドルを押していない常時表示時の値)。
//   印刷マークON中はパネルで選択中の不透明度(25%/75%。画面と印刷の見た目を一致)、印刷OFFは 1.0。
//   ミドルを離したら sMarkScreenOpacity をこの値へ戻す。
PMReal KESCMBaseScreenOpacity()
{
	return KESCMDrawEventHandler::sPrintMarks
	       ? KESCMDrawEventHandler::SelectedMarkOpacity() : PMReal(1.0);
}

// peek 試行の結果(スクリプトの状態文字列用。watcher は無視する)。
enum KESCMPeekResult { kKESCMPeekNoView = 0, kKESCMPeekNoSpread = 1, kKESCMPeekShown = 2, kKESCMPeekNoChange = 3 };

// 前面レイアウトビューで「マウス下スプレッド」の旧版べた載せを表示する。
//   targetDB=表示中(新)ドキュメント, sourceDB=重ねる旧ドキュメント。
//   そのスプレッドが既にキャッシュ済みなら再利用(即時)。未キャッシュなら旧キャッシュを捨てて、その
//   スプレッドだけをその場でラスタ化(保持は常に1スプレッド)。成功時に sShowOriginal を立てて再描画。
//   outSpread/outPages は任意(nil 可)。
static KESCMPeekResult KESCMPeekShowUnderMouse(IDataBase* targetDB, IDataBase* sourceDB,
	int32* outSpread, int32* outPages)
{
	if (outSpread) *outSpread = -1;
	if (outPages)  *outPages = 0;
	if (targetDB == nil || sourceDB == nil)
		return kKESCMPeekNoView;

	// マウスが乗っているレイアウトビュー(Split Window対応、KESCMQueryViewUnderMouse参照)。
	InterfacePtr<IControlView> view(KESCMQueryViewUnderMouse());
	if (view == nil)
		return kKESCMPeekNoView;

	// 現在のズーム(content→window スケール=ズーム×デバイス倍率)から、画面と 1:1 になる解像度を決める。
	// dpi = 72 × スケール。1:1 のとき最も綺麗(画像px=画面px)。
	PMReal curScale = view->GetContentToWindowMatrix().GetXScale();
	if (curScale < 0) curScale = -curScale;
	if (curScale <= 0) curScale = 1.0;

	// 【低ズームの下限=UI 50%】UIズーム(ユーザーに見える拡大率, デバイス倍率を含まない)が 50% を下回る時は
	// 「50% 相当の解像度」で頭打ちにする。50%以上は画面と 1:1 のままくっきり。50%未満は画像が画面より高精細に
	// なり、縮小blit(点サンプリング)で多少粗くなる(=10% などは汚くてよい、という方針)。下限を UI% で決めるので
	// デバイス倍率に依らず、画面に見える 50% がそのまま境界になる。パノラマ不明時は 1:1(従来=全ズーム綺麗)。
	PMReal effScale = curScale;
	InterfacePtr<IPanorama> peekPano(KESCMQueryPanorama(view));
	if (peekPano != nil)
	{
		const PMReal uiZoom = peekPano->GetXScaleFactor(kFalse);	// UIズーム(例: 0.5=50%)
		if (uiZoom > 0)
		{
			const PMReal deviceScale = curScale / uiZoom;			// 画面デバイス倍率(=curScale/uiZoom)
			const PMReal flooredZoom = (uiZoom < PMReal(0.5)) ? PMReal(0.5) : uiZoom;	// UI 50% で頭打ち
			effScale = flooredZoom * deviceScale;
		}
	}

	PMReal peekDpi = PMReal(72.0) * effScale;
	if (peekDpi < 16.0)  peekDpi = 16.0;	// 安全下限(degenerate 回避。通常は効かない)
	if (peekDpi > 300.0) peekDpi = 300.0;	// 過大メモリ防止(300dpi A4 ≒ 35MB/頁)

	PMReal mx = 0.0, my = 0.0;
	if (!KESCMQueryMouseContentPoint(view, mx, my))
		return kKESCMPeekNoView;

	// マウス下のスプレッド/ページを特定(平坦通し番号も取得)。共有ヘルパ KESCMFindPageUnderMouse に集約。
	KESCMPageHit hit;
	if (!KESCMFindPageUnderMouse(targetDB, mx, my, hit))
		return kKESCMPeekNoSpread;

	const int32 s           = hit.spreadIndex;
	const int32 np          = hit.numPages;
	const int32 globalIndex = hit.globalPageBase;
	InterfacePtr<ISpread> spread(targetDB, hit.spreadUID, UseDefaultIID());
	if (spread == nil)
		return kKESCMPeekNoSpread;

	// 【未更新スプレッドの早期スキップ】このドキュメントで比較が実行済み(sDB==targetDB)で、かつ
	// このスプレッドのどのページも変化エントリ(sEntries)に無いなら、旧版は現行と同一=重ねる意味が
	// 無い。重いラスタ化を丸ごと省いて即 return する(旧版を出さない)。比較が未実行(sDB!=targetDB)
	// なら変化の有無を判定できないので、従来どおりラスタ化する(全スキップしない)。
	if (KESCMDrawEventHandler::sDB == targetDB)
	{
		bool16 anyChanged = kFalse;
		for (int32 p = 0; p < np; ++p)
			if (KESCMDrawEventHandler::sEntries.find(spread->GetNthPageUID(p)) !=
			    KESCMDrawEventHandler::sEntries.end())
			{ anyChanged = kTrue; break; }
		if (!anyChanged)
		{
			if (outSpread) *outSpread = s;
			if (outPages)  *outPages = 0;
			return kKESCMPeekNoChange;
		}
	}

	// このスプレッドは既に丸ごとキャッシュ済みか?(同じ db かつ 全ページが sOrigImages にある) → 再利用(即時)。
	bool16 cached = (KESCMDrawEventHandler::sOrigDB == targetDB);
	for (int32 p = 0; p < np && cached; ++p)
		if (KESCMDrawEventHandler::sOrigImages.find(spread->GetNthPageUID(p)) ==
		    KESCMDrawEventHandler::sOrigImages.end())
			cached = kFalse;
	// ズームが変わっていたら(キャッシュ時と解像度が合わない)作り直す。差が2%以内なら再利用。
	if (cached && KESCMDrawEventHandler::sOrigScale > 0)
	{
		PMReal d = effScale - KESCMDrawEventHandler::sOrigScale;
		if (d < 0) d = -d;
		if (d > KESCMDrawEventHandler::sOrigScale * PMReal(0.02))
			cached = kFalse;
	}

	int32 captured = 0;
	if (cached)
	{
		captured = np;	// ラスタ化不要=キャッシュがこのスプレッドを覆っている
	}
	else
	{
		// 旧ドキュメントの平坦ページUID列(スプレッド順・ページ順)。新→旧の通し番号対応に使う。
		// 実際にラスタ化するこの分岐でだけ必要なので、ここで集める(キャッシュヒット=同一スプレッドの
		// 再 peek が最頻ケースで、その度に旧文書の全スプレッド/ページを列挙するのは無駄だった)。
		std::vector<UID> sPages;
		KESCMCollectPageUIDs(sourceDB, sPages);

		KESCMDrawEventHandler::DropAllOrig();		// 覗くのは1スプレッドだけ=他は破棄
		KESCMDrawEventHandler::sOrigDB = targetDB;
		KESCMDrawEventHandler::sOrigScale = effScale;	// このラスタ化解像度を記録(再 peek の作り直し判定用)
		for (int32 p = 0; p < np; ++p)
		{
			const int32 gi = globalIndex + p;
			if (gi < (int32)sPages.size())
			{
				UIDRef tRef(targetDB, spread->GetNthPageUID(p));
				UIDRef sRef(sourceDB, sPages[gi]);
				if (KESCMDrawEventHandler::MakeOrigImage(tRef, sRef, peekDpi) == kSuccess)
					++captured;
			}
		}
	}
	KESCMDrawEventHandler::sShowOriginal = kTrue;

	KESCMInvalidateDB(targetDB);

	if (outSpread) *outSpread = s;
	if (outPages)  *outPages = captured;
	return kKESCMPeekShown;
}


// ミドル押下中だけ一時的にハンドツール(掴んで移動)へ切り替える。元のツールを覚えておく(離すと戻す)。
// 既に切替中なら何もしない(ハンド自身を「元のツール」として覚えてしまわないため)。
static void KESCMEnterHandTool()
{
	if (sHandActive)
		return;
	ITool* cur  = Utils<IToolBoxUtils>()->QueryActiveTool(kPointerToolBoss);	// +1 ref
	ITool* hand = Utils<IToolBoxUtils>()->QueryTool(kGrabberHandToolBoss);	// +1 ref
	if (hand != nil)
	{
		sSavedTool = cur;	// ref を保持(下の Restore で Release)。cur が nil でも可
		Utils<IToolBoxUtils>()->SetActiveTool(hand, kPointerToolBoss);
		hand->Release();
		sHandActive = kTrue;
	}
	else if (cur != nil)
	{
		cur->Release();
	}
}

// 覚えていた元のツールへ戻す(ハンドに切替えていた場合のみ)。
static void KESCMRestoreTool()
{
	if (!sHandActive)
		return;
	if (sSavedTool != nil)
	{
		Utils<IToolBoxUtils>()->SetActiveTool(sSavedTool, kPointerToolBoss);
		sSavedTool->Release();
		sSavedTool = nil;
	}
	sHandActive = kFalse;
}

// Shift／Ctrl＋ミドル押下を検出したときの共通処理: 「保持中だけ覗く」状態に入り、マウス下スプレッドの旧版を
// opacity(Shift=1.0 不透明 / Ctrl=0.5 半透明)で表示。覗き中もハンドツールにして「旧状態で掴んで移動」できるように。
// 覗き中は枠等(マーク)は不要なので sMarksVisible=kFalse のまま(既定が非表示)＝旧版だけが乗る。覗いている
// スプレッドは旧版が覆い、他スプレッドも非表示なので、画面全体が枠なしの「旧版/現行のみ」になる。
static void KESCMBeginPeekHold(PMReal opacity)
{
	sPeekActive = kTrue;
	KESCMDrawEventHandler::sPeekOpacity = opacity;	// 旧版の不透明度(描画時に旧版べた載せの描画ブロックが参照)
	sSingleShowing = kFalse;
	KESCMDrawEventHandler::sMarksVisible = kFalse;	// 覗き中は枠等を出さない(旧版だけ)
	KESCMEnterHandTool();	// 旧状態で掴んで移動
	KESCMPeekShowUnderMouse(sPeekTargetDB, sPeekSourceDB, nil, nil);
}




// Ctrl＋ミドルクリック(旧 Shift＋Ctrl=2026-07-04移動): マウス下スプレッドだけを再比較して枠(リング)を更新する(部分更新)。
//   targetDB=新(arm 済み表示中) / sourceDB=旧(arm 済み比較相手)。新→旧ページは平坦通し番号で対応。
//   ・各ページを MakeEntry で取り直し(編集後の差分に更新)。変化が無くなったページは古い枠を消す。
//   ・旧版画像キャッシュ(sOrigImages)は古いので破棄(次の peek で作り直し)。
//   見つかったスプレッドの index(0始まり)を outSpread に、変化ページ数を outChanged に返す。戻り=見つかったか。
static bool16 KESCMRefreshSpreadUnderMouse(IDataBase* targetDB, IDataBase* sourceDB, int32* outSpread, int32* outChanged)
{
	if (outSpread)  *outSpread = -1;
	if (outChanged) *outChanged = 0;
	if (targetDB == nil || sourceDB == nil)
		return kFalse;

	// マウスが乗っているレイアウトビュー(Split Window対応、KESCMQueryViewUnderMouse参照)。
	InterfacePtr<IControlView> view(KESCMQueryViewUnderMouse());
	PMReal mx = 0.0, my = 0.0;
	if (!KESCMQueryMouseContentPoint(view, mx, my))
		return kFalse;

	// マウス下のスプレッド/ページを特定(平坦通し番号も取得)。共有ヘルパ KESCMFindPageUnderMouse に集約。
	KESCMPageHit hit;
	if (!KESCMFindPageUnderMouse(targetDB, mx, my, hit))
		return kFalse;

	// 旧ドキュメントの平坦ページUID列(スプレッド順・ページ順)。
	std::vector<UID> sPages;
	KESCMCollectPageUIDs(sourceDB, sPages);

	// マークの所属ドキュメントを合わせる(別 doc にマークがあった場合のみ総入れ替え=通常は一致で何もしない)。
	if (KESCMDrawEventHandler::sDB != nil && KESCMDrawEventHandler::sDB != targetDB)
		KESCMDrawEventHandler::DropAll();
	KESCMDrawEventHandler::sDB = targetDB;

	InterfacePtr<ISpread> spread(targetDB, hit.spreadUID, UseDefaultIID());
	if (spread == nil)
		return kFalse;
	const int32 np = hit.numPages;

	// このスプレッドの各ページを再比較して枠を更新。新→旧は globalPageBase で対応。
	int32 changedCount = 0;
	for (int32 p = 0; p < np; ++p)
	{
		const int32 gi = hit.globalPageBase + p;
		if (gi >= (int32)sPages.size())
			continue;
		const UID tUID = spread->GetNthPageUID(p);
		bool16 changed = kFalse;
		KESCMDrawEventHandler::MakeEntry(UIDRef(targetDB, tUID), UIDRef(sourceDB, sPages[gi]), changed);
		if (changed)
			++changedCount;
		else
		{
			// 変化が無くなったページ → 古い枠が残っていれば消す(更新で消えるべき)。
			std::map<UID, KESCMOverlayEntry*>::iterator old = KESCMDrawEventHandler::sEntries.find(tUID);
			if (old != KESCMDrawEventHandler::sEntries.end())
			{ delete old->second; KESCMDrawEventHandler::sEntries.erase(old); }
		}
	}

	// 旧版画像キャッシュは古いので破棄(次の peek で現ズームで作り直し)。
	KESCMDrawEventHandler::DropAllOrig();

	KESCMInvalidateDB(targetDB);

	if (outSpread)  *outSpread = hit.spreadIndex;
	if (outChanged) *outChanged = changedCount;
	return kTrue;
}


// マーク(枠/変更数)の表示を切り替えた後、マークが属するドキュメント(sDB)を再描画して
// 即反映する。arm の有無に依らず使えるよう、peek 用の sPeekTargetDB ではなく sDB を使う(arm 不要)。
static void KESCMInvalidateMarksDoc()
{
	KESCMInvalidateDB(KESCMDrawEventHandler::sDB);
}

// マウス下のドキュメントが、arm 済みの対象(Target)文書と一致するか。CMYK サンプリング
// (Shift＋Ctrl＋Alt＋ミドル)とスプレッド枠の部分更新(Ctrl＋ミドル)はヒットテストを sPeekTargetDB の
// ページ座標に対して行うため、マウス下が Source 側や無関係な第3文書のウィンドウだと、そちらの
// ローカル座標を対象文書のページ座標として誤って解釈してしまう。対象文書のウィンドウ上で操作した時
// だけ反応させる。
// ★以前は Utils<ILayoutUIUtils>()->GetFrontDocument()(「front(アクティブ)なドキュメント」)で
// 判定していたが、Split Window の新しい側(kLayoutSecondaryPanelWidgetID)を操作しても OWL 内部の
// アクティブ状態追跡が元側のままになるらしく、判定に失敗していた(ユーザー実測で確認)。
// KESCMSyncScrollOtherWindowsUnderMouse と同じ QueryWindowUnderPoint ベースの判定に統一し、
// マウス位置そのものからドキュメントを特定する(アクティブ状態を一切参照しない)。
static bool16 KESCMFrontViewIsOverTarget()
{
	GSysPoint globalPt = Utils<IEventUtils>()->GetGlobalMouseLocation();

	InterfacePtr<IWindow> hitWindow(Utils<IWindowUtils>()->QueryWindowUnderPoint(globalPt, kFalse));
	if (hitWindow == nil)
		return kFalse;

	InterfacePtr<IDocumentPresentation> hitPres(hitWindow, UseDefaultIID());
	if (hitPres == nil)
		return kFalse;

	return (hitPres->GetDocumentUIDRef().GetDataBase() == sPeekTargetDB);
}

//========================================================================================
// ビューポート同期エンジン(共有)
//   手本パノラマの「見えている状態」= 実効ズーム(GetXScaleFactor(kTrue)、モニタPPI補正込み。
//   kZoomToCmdBoss の scaleFactor と同じ次元)+可視中心の content 座標 を、srcDocDb「以外」の
//   全ドキュメントの全レイアウトビューへ複製する(同一文書のビュー=スプリット相方は対象外)。
//   Alt+ミドル(単発)とフライアウト「Sync Layout Views」(自動)の両方がこの1本を使う。
//========================================================================================

// 再入ガード: 複製そのものが対象ビューで kScaleTo/kScrollTo 等の通知を発生させ、同期オブザーバが
// それを拾って同期し返す(無限ループ/ピンポン)のを防ぐ。複製ループの間だけ kTrue。
static bool16 sLayoutSyncBroadcasting = kFalse;

// view がどの文書のレイアウトビューかをポインタ照合で特定する(見つからなければ nil)。
// GetAllLayoutViews が返す IControlView* は同一ビューなら同一ポインタ(実測前提、本ファイルの
// 役割判定でも同じ前提を使用)。同期オブザーバが通知元ビューの所属文書を知るために使う。
static IDataBase* KESCMFindDocDbForView(IControlView* view)
{
	if (view == nil)
		return nil;
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
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
		K2Vector<IControlView*> views;
		Utils<ILayoutViewUtils>()->GetAllLayoutViews(views, nil, db);
		for (int32 vi = 0; vi < (int32)views.size(); ++vi)
			if (views[vi] == view)
				return db;
	}
	return nil;
}

static void KESCMSyncOtherDocViewportsTo(IPanorama* srcPano, IDataBase* srcDocDb)
{
	if (srcPano == nil)
		return;

	// 手本ビューの「見えている状態」を読む。ズームは実効スケール(kTrue=モニタPPI補正込み)。
	// ズームコマンド(kZoomToCmdBoss)が扱う scaleFactor と同じ次元なので、読み書きが対称になる。
	const PMReal  srcZoom   = srcPano->GetXScaleFactor(kTrue);
	const PBPMPoint srcCenter(srcPano->GetContentLocationAtFrameCenter());

	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return;

	sLayoutSyncBroadcasting = kTrue;	// ここからの通知は自分発なのでオブザーバは無視する

	const int32 docCount = docList->GetDocCount();
	for (int32 d = 0; d < docCount; ++d)
	{
		IDocument* doc = docList->GetNthDoc(d);
		if (doc == nil)
			continue;

		IDataBase* db = ::GetUIDRef(doc).GetDataBase();

		// ★手本の文書自身は丸ごと対象外(スプリット相方も含む。2026-07-04ユーザー指定:
		// 「他のドキュメントにだけ」)。
		if (db == srcDocDb)
			continue;

		K2Vector<IControlView*> views;
		Utils<ILayoutViewUtils>()->GetAllLayoutViews(views, nil, db);

		for (int32 vi = 0; vi < (int32)views.size(); ++vi)
		{
			IControlView* view = views[vi];
			if (view == nil)
				continue;

			InterfacePtr<IPanorama> pano(KESCMQueryPanorama(view));
			if (pano == nil)
				continue;

			// 既に手本と一致しているビューは触らない。スクロールドラッグ/ズーム操作中は通知が高頻度で
			// 来るため(自動同期経由)、一致済みビューへの再スクロール+forceRedraw を毎回打たない。
			// 中心の許容差はズーム換算で約1.5画面px(スクロール位置は画面px量子化されるため、pt固定の
			// 微小許容差だと低倍率で永遠に「不一致」になる)。この範囲のズレは見た目に出ない。
			PMReal zoomDiff = pano->GetXScaleFactor(kTrue) - srcZoom;
			if (zoomDiff < 0) zoomDiff = -zoomDiff;
			const bool16 zoomMatched = (zoomDiff <= PMReal(0.0001));
			if (zoomMatched)
			{
				const PMPoint curCenter = pano->GetContentLocationAtFrameCenter();
				PMReal dx = curCenter.X() - srcCenter.X(); if (dx < 0) dx = -dx;
				PMReal dy = curCenter.Y() - srcCenter.Y(); if (dy < 0) dy = -dy;
				const PMReal tol = (srcZoom > PMReal(0.0001)) ? (PMReal(1.5) / srcZoom) : PMReal(1.0);
				if (dx <= tol && dy <= tol)
					continue;	// 位置も拡大率も一致済み
			}

			// 拡大率を手本と同じ実効スケールへ。ズームは UI のズーム欄と同じ公式コマンド
			// (kZoomToCmdBoss)で行う。既定引数=ビュー中心基準。
			// ★ILayoutViewUtils::ZoomLayoutViews 直呼びは他文書のビューに効かない(実機確認)ため不可。
			if (!zoomMatched)
			{
				InterfacePtr<ICommand> zoomCmd(Utils<ILayoutUIUtils>()->MakeZoomCmd(view, srcZoom));
				if (zoomCmd != nil)
					CmdUtils::ProcessCommand(zoomCmd);
			}
			// 手本の可視中心と同じ content 座標をビュー中心へ=同じ拡大率なら同じ画面が同じように映る。
			// ズーム(コマンド)実行後に行うので、新しい倍率で正しくセンタリングされる。
			pano->ScrollContentLocationToFrameCenter(srcCenter, kTrue /*forceRedraw*/);
		}
	}

	sLayoutSyncBroadcasting = kFalse;
}

// Alt＋ミドル押下(momentary、旧 Ctrl＋ミドル=2026-07-04移動): ビューポート同期。マウス下のレイアウトビュー
// (アクティブである必要はない; IWindowUtils::QueryWindowUnderPoint でグローバル座標から直接引く)の
// 「見えている状態」= 実効ズーム(拡大率)+ビュー中心に映っている content(pasteboard)座標 を読み取り、
// ★「他のドキュメント」のウィンドウへだけ複製する(マウス下の文書自身のビュー=スプリット相方含めて
// 対象外。2026-07-04ユーザー指定)。つまり拡大率が同じなら、同じ座標の画面が同じように映る
// (旧仕様=マウス位置を他ウィンドウのセンターへスクロールするだけで、拡大率は触らなかった)。
// ChangeMarker の Start(sPeekArmed)とは無関係に常に使える。
//
// 用途: Target/Source のようにページ構成が近い文書を並べ、同じ場所を同じ倍率で見比べる。
// pasteboard 座標はドキュメント固有の値だが、構成が近ければそのまま流用できる(ズレは許容)。
// ズーム=ILayoutUIUtils::MakeZoomCmd(kZoomToCmdBoss=UIのズーム欄と同じ公式経路)+ProcessCommand。
// ★前実装の ILayoutViewUtils::ZoomLayoutViews 直呼びは他ドキュメントのビューに効かなかった(実機で
// 拡大率が変わらないのを確認)ため、コマンド経由へ変更。ズーム値は読み書きとも実効スケール
// GetXScaleFactor(kTrue)(モニタPPI補正込み。IZoomCmdData.h の kMinZoom=0.05〜kMaxZoom=40.0 と同じ次元)。
// スクロール=KESCL(KESCLFindInDoc.cpp)と同じ手口(QueryPanorama→ScrollContentLocationToFrameCenter)。
//
// ★Split Window(1文書2ペイン)対応:
// (1) 手本の判定を kLayoutWidgetBoss 固定にせず FindWidget(windowPt) でヒットテストし、マウスが
//     スプリットの新しい側にあればそちらを手本にする。
// (2) パノラマの読み書きに使うビューは必ず Utils<ILayoutViewUtils>()->GetAllLayoutViews() 経由で取得。
//     ★実測で判明した重要事項: kLayoutSecondaryPanelWidgetID を IPanelControlData::FindWidget() で
//     直接引いても、そのウィジェット自身はパノラマを持たず、祖先を辿っても見つからない(外側の
//     ラッパーに過ぎない)。実際にパノラマを持つオブジェクトは GetAllLayoutViews() が返す別オブジェクト。
static void KESCMSyncScrollOtherWindowsUnderMouse(IEvent* e)
{
	if (e == nil)
		return;

	const GSysPoint globalPt = e->GlobalWhere();

	InterfacePtr<IWindow> hitWindow(Utils<IWindowUtils>()->QueryWindowUnderPoint(globalPt, kFalse));
	if (hitWindow == nil)
		return;

	InterfacePtr<IDocumentPresentation> hitPres(hitWindow, UseDefaultIID());
	if (hitPres == nil)
		return;	// マウス下がドキュメントウィンドウではない(アプリフレーム／パレット等)

	InterfacePtr<IPanelControlData> hitPanelData(hitPres, UseDefaultIID());
	if (hitPanelData == nil)
		return;

	IControlView* primaryView = hitPanelData->FindWidget(kLayoutWidgetBoss);
	if (primaryView == nil)
		return;

	// ★スプリット表示中は、マウスが元側(kLayoutWidgetBoss)と新しい側(kLayoutSecondaryPanelWidgetID)の
	// どちらの上にあるかで「手本」にするビューが変わる。FindWidget(windowPt) のヒットテストで役割だけ
	// 判定する(キャンバス以外=ルーラ等に当たった場合は元側扱いにフォールバック)。primaryView は
	// 「グローバル→ウィンドウ座標への変換」にだけ使う(どの子ウィジェット経由でも同じウィンドウ座標系)。
	bool16 usedSecondary = kFalse;
	{
		const PMPoint globalPM((PMReal)globalPt.x, (PMReal)globalPt.y);
		const PMPoint winPM = primaryView->GlobalToWindow(globalPM);
		SysPoint winPt;
		winPt.x = ::ToInt32(winPM.X());
		winPt.y = ::ToInt32(winPM.Y());

		IControlView* pointHit = hitPanelData->FindWidget(winPt);
		if (pointHit != nil && pointHit->GetWidgetID() == kLayoutSecondaryPanelWidgetID)
			usedSecondary = kTrue;
	}

	IDataBase* hitDocDb = hitPres->GetDocumentUIDRef().GetDataBase();

	// 手本(マウス下)ビューの実体を特定する。FindWidget が返すオブジェクトはパノラマを持たない
	// ラッパーのことがある(上記(2))ので、パノラマの読み取りも GetAllLayoutViews 経由の実体で行う。
	// スプリット中(ビュー2つ)は元側/新しい側を消去法で選ぶ。非スプリット(1つ)はそのまま採用。
	// 万一どれとも同定できなければ先頭の非nilビューへフォールバック(何もしないよりよい)。
	IControlView* mapView = nil;
	{
		K2Vector<IControlView*> hitDocViews;
		Utils<ILayoutViewUtils>()->GetAllLayoutViews(hitDocViews, nil, hitDocDb);
		IControlView* firstView = nil;
		for (int32 i = 0; i < (int32)hitDocViews.size(); ++i)
		{
			if (hitDocViews[i] == nil)
				continue;
			if (firstView == nil)
				firstView = hitDocViews[i];
			const bool16 thisIsPrimary = (hitDocViews[i] == primaryView);
			const bool16 thisIsTheMapView = usedSecondary ? !thisIsPrimary : thisIsPrimary;
			if (thisIsTheMapView)
			{
				mapView = hitDocViews[i];
				break;
			}
		}
		if (mapView == nil)
			mapView = firstView;
	}
	InterfacePtr<IPanorama> srcPano(mapView != nil ? KESCMQueryPanorama(mapView) : nil);
	if (srcPano == nil)
		return;

	KESCMSyncOtherDocViewportsTo(srcPano, hitDocDb);
}


//========================================================================================
// レイアウトビュー同期(フライアウト「Sync Layout Views」チェック式トグル)
//   ON の間、全ドキュメントの全レイアウトビューの IPanorama subject を購読し、どれかが
//   スクロール/ズームしたら KESCMSyncOtherDocViewportsTo で他文書のビューへ複製する(自動・ライブ)。
//   さらに ActiveContext(文書切替)も購読し、新しく開いた文書のビューを購読へ追加する。
//   Start(枠)とは完全に独立=単独で ON にできる。
//
//   手本=ユーザー自作の旧KESプラグインの KESLayoutScrollObserver(work/ に原本)。改善点:
//   ①購読対象を「アクティブ文書の最初のプレゼンテーションの元側ペインのみ(QueryFrontView 一致時)」
//     から「全文書の全レイアウトビュー(スプリット新側・2枚目以降のウィンドウ込み)」へ拡大。
//     どのウィンドウを動かしてもそれが手本になり、QueryFrontView のアクティブ追跡ズレ
//     (Split Window で実測済みの罠)にも影響されない。
//   ②多重購読でも無限ループ/ピンポンしないよう、再入ガード(sLayoutSyncBroadcasting)で
//     複製中に発生する自分発の通知を無視する(手本は「単一ビューだけ購読」で回避していた)。
//   ③同期エンジンは Alt+ミドルと共通(kZoomToCmdBoss+実効スケール対称読み書き=本日実機確定の手順)。
//========================================================================================

static bool16 sLayoutSyncOn = kFalse;			// トグル状態(セッション内のみ保持)

// 同期オブザーバの実体を ActiveContext boss から引く(+1 ref、呼び出し側は InterfacePtr で受ける)。
// ★実証済み構成: .fr の AddIn で kActiveContextBoss に IID_IKESCMLAYOUTSYNCOBSERVER として同居させる
// (手本=ユーザー自作 KESLayoutScrollObserver と同じ)。
// ★当初の失敗の原因(特定済み): AttachObserver の第4引数 asObserver は「オブザーバ実装がそのboss上で
// 実際に載っているインターフェイスID」を渡す契約(ISubject.h の記述+IChangeManager は依存を
// (subject, observer, observerIID, interestedIn) で管理し boss+IID で引き直せる前提の設計。
// CSubject.h/IChangeManager.h は docs HTML のみに存在)。当初は実装を IID_IOBSERVER で載せた独立boss
// (CreateObject2)に、boss上に存在しない IID_IKESCMLAYOUTSYNCOBSERVER を asObserver として渡していた
// ため Update が届かなかった。現構成は実装が実際に IID_IKESCMLAYOUTSYNCOBSERVER で載っており整合する。
static IObserver* KESCMQueryLayoutSyncObserver()
{
	IActiveContext* ctx = GetExecutionContextSession()->GetActiveContext();
	if (ctx == nil)
		return nil;
	return (IObserver*)ctx->QueryInterface(IID_IKESCMLAYOUTSYNCOBSERVER);
}

// 全レイアウトビューへ購読を付ける(未購読のものだけ)。ON時と、ON中の文書切替時に呼ばれ、
// 新しく開いた文書・新しく現れたウィンドウのビューを取りこぼさない。
static void KESCMLayoutSyncAttachAllPanoramas()
{
	InterfacePtr<IObserver> obs(KESCMQueryLayoutSyncObserver());
	if (obs == nil)
		return;
	K2Vector<IControlView*> views;
	Utils<ILayoutViewUtils>()->GetAllLayoutViews(views, nil, nil);	// db=nil で全ドキュメントの全レイアウトビュー
	for (int32 i = 0; i < (int32)views.size(); ++i)
	{
		if (views[i] == nil)
			continue;
		InterfacePtr<ISubject> subject(views[i], UseDefaultIID());
		if (subject == nil)
			continue;
		if (!subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IPANORAMA, IID_IKESCMLAYOUTSYNCOBSERVER))
			subject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IPANORAMA, IID_IKESCMLAYOUTSYNCOBSERVER);
	}
}

// 全レイアウトビューから購読を外す(OFF時/Shutdown時)。既にクローズされたビューは
// GetAllLayoutViews に現れない=購読はビュー破棄と一緒に消えているので、生存分だけ外せばよい。
static void KESCMLayoutSyncDetachAllPanoramas()
{
	InterfacePtr<IObserver> obs(KESCMQueryLayoutSyncObserver());
	if (obs == nil)
		return;
	K2Vector<IControlView*> views;
	Utils<ILayoutViewUtils>()->GetAllLayoutViews(views, nil, nil);
	for (int32 i = 0; i < (int32)views.size(); ++i)
	{
		if (views[i] == nil)
			continue;
		InterfacePtr<ISubject> subject(views[i], UseDefaultIID());
		if (subject == nil)
			continue;
		if (subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IPANORAMA, IID_IKESCMLAYOUTSYNCOBSERVER))
			subject->DetachObserver(ISubject::kRegularAttachment, obs, IID_IPANORAMA, IID_IKESCMLAYOUTSYNCOBSERVER);
	}
}

// ActiveContext(文書切替の通知源)への購読を付け外しする。ActiveContext はセッションに1つの
// 永続オブジェクトなので、付け外しは ON/OFF 時の各1回でよい。
static void KESCMLayoutSyncAttachContext(bool16 attach)
{
	IActiveContext* ctx = GetExecutionContextSession()->GetActiveContext();
	if (ctx == nil)
		return;
	InterfacePtr<IObserver> obs((IObserver*)ctx->QueryInterface(IID_IKESCMLAYOUTSYNCOBSERVER));
	if (obs == nil)
		return;
	InterfacePtr<ISubject> subject(ctx, UseDefaultIID());
	if (subject == nil)
		return;
	const bool16 attached = subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IACTIVECONTEXT, IID_IKESCMLAYOUTSYNCOBSERVER);
	if (attach && !attached)
		subject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IACTIVECONTEXT, IID_IKESCMLAYOUTSYNCOBSERVER);
	else if (!attach && attached)
		subject->DetachObserver(ISubject::kRegularAttachment, obs, IID_IACTIVECONTEXT, IID_IKESCMLAYOUTSYNCOBSERVER);
}

/** レイアウトビュー同期オブザーバの実装。.fr の AddIn で kActiveContextBoss に
    IID_IKESCMLAYOUTSYNCOBSERVER として同居させている(手本と同じ実証済み構成)。 */
class KESCMLayoutSyncObserver : public CObserver
{
public:
	KESCMLayoutSyncObserver(IPMUnknown* boss) : CObserver(boss) {}
	~KESCMLayoutSyncObserver() {}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);
};

CREATE_PMINTERFACE(KESCMLayoutSyncObserver, kKESCMLayoutSyncObserverImpl)

void KESCMLayoutSyncObserver::Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy)
{
	if (!sLayoutSyncOn || sLayoutSyncBroadcasting)
		return;	// OFF、または自分発(複製中)の通知

	// 文書切替(IID_IDOCUMENT)またはアクティブビュー切替(IID_ICONTROLVIEW)で購読を付け直す
	// (未購読分にだけ付く。手本の KESLayoutScrollObserver は文書切替のみだったが、ビュー切替も見る
	// ことで、ON 中に作られた同一文書の新規ウィンドウ/Split Window の新側ペインも、クリックして
	// アクティブになった瞬間に購読される=そのペインを動かしても手本になれる)。
	if (protocol.Get() == IID_IACTIVECONTEXT)
	{
		IActiveContext::ContextInfo* info = (IActiveContext::ContextInfo*)changedBy;
		if (info != nil && (info->Key() == IID_IDOCUMENT || info->Key() == IID_ICONTROLVIEW))
			KESCMLayoutSyncAttachAllPanoramas();
		return;
	}

	if (protocol.Get() != IID_IPANORAMA)
		return;
	if (theChange != kScrollToMessage && theChange != kScrollByMessage &&
	    theChange != kScaleToMessage  && theChange != kScaleByMessage)
		return;

	// 通知元(=手本)のパノラマと所属文書。theSubject はレイアウトビュー boss の subject なので、
	// 同じ boss から IPanorama / IControlView を引ける。
	InterfacePtr<IPanorama> srcPano(theSubject, UseDefaultIID());
	InterfacePtr<IControlView> srcView(theSubject, UseDefaultIID());
	if (srcPano == nil || srcView == nil)
		return;
	IDataBase* srcDocDb = KESCMFindDocDbForView(srcView);
	if (srcDocDb == nil)
		return;	// 所属文書を特定できない(クローズ途中等)。同期しない

	KESCMSyncOtherDocViewportsTo(srcPano, srcDocDb);
}

// KESCMGetLayoutSync / KESCMSetLayoutSync(KESCMCore.h で宣言) — フライアウトトグルの実体。
bool16 KESCMGetLayoutSync()
{
	return sLayoutSyncOn;
}

void KESCMSetLayoutSync(bool16 on)
{
	if ((on && sLayoutSyncOn) || (!on && !sLayoutSyncOn))
		return;

	if (on)
	{
		// オブザーバは kActiveContextBoss に AddIn 済み(.fr)。取得できない環境なら ON にしない。
		InterfacePtr<IObserver> obs(KESCMQueryLayoutSyncObserver());
		if (obs == nil)
			return;
		sLayoutSyncOn = kTrue;
		KESCMLayoutSyncAttachContext(kTrue);
		KESCMLayoutSyncAttachAllPanoramas();

		// ON にした瞬間に一度そろえる(手本=最前面のレイアウトビュー)。以後は通知駆動のライブ同期。
		InterfacePtr<IControlView> front(Utils<ILayoutUIUtils>()->QueryFrontView());
		if (front != nil)
		{
			InterfacePtr<IPanorama> pano(KESCMQueryPanorama(front));
			IDataBase* db = KESCMFindDocDbForView(front);
			if (pano != nil && db != nil)
				KESCMSyncOtherDocViewportsTo(pano, db);
		}
	}
	else
	{
		sLayoutSyncOn = kFalse;
		KESCMLayoutSyncDetachAllPanoramas();
		KESCMLayoutSyncAttachContext(kFalse);
		// オブザーバ本体は kActiveContextBoss 所属(AddIn)なので、寿命管理は不要。
	}
}


//========================================================================================
// KESCMPeekWatcher
//   非消費のイベントウォッチャ。peek が arm 済み(kescmArmMousePeek)の間、Shift＋ミドルボタンを押すと
//   マウス下スプレッドの旧版べた載せを表示し、ミドルを離すと隠す。非消費=ミドルボタン本来の動作も走る。
//========================================================================================
class KESCMPeekWatcher : public CPMUnknown<IEventWatcher>
{
public:
	KESCMPeekWatcher(IPMUnknown* boss) : CPMUnknown<IEventWatcher>(boss), fWatching(kFalse) {}
	~KESCMPeekWatcher() {}

	IEventDispatcher::EventTypeList WatchEvent(IEvent* e);
	void StartWatching();
	void StopWatching();

private:
	bool16 fWatching;
};

CREATE_PMINTERFACE(KESCMPeekWatcher, kKESCMPeekWatcherImpl)

IEventDispatcher::EventTypeList KESCMPeekWatcher::WatchEvent(IEvent* e)
{
	// 興味=ミドル押下/解放のみ。毎回返す(空を返すと監視解除される)。Shift 判定は押下イベントで見る。
	IEventDispatcher::EventTypeList interest(IEvent::kMButtonDn, IEvent::kMButtonUp);

	if (e == nil)
		return interest;

	const IEvent::EventType type = e->GetType();
	if (type != IEvent::kMButtonDn && type != IEvent::kMButtonUp)
		return interest;

	// 旧版べた載せ(peek)の検証は arm 済みの時だけ。シングルの枠表示は arm 不要なので、ここで素通りさせる。
	if (sPeekArmed)
	{
		// arm 済みドキュメントがまだ開いているか検証(片方を閉じた後のダングリング参照を防ぐ)。
		// ★以前はここで peek arm の解除・旧版べた載せの破棄だけを個別に行い、マーク本体(sEntries/sDB)には
		// 触れていなかった。通常はドキュメントクローズ responder(KESCMHandleDocsClosed)がクローズ直後に
		// 先回りして片付けるためこの分岐へは実質到達しないが、保険として残す以上は KESCMHandleDocsClosed に
		// 一本化し、Stop 相当のフルクリーンアップ(マーク破棄＋パネル更新も)を確実に行う
		// (枠だけ残る／ボタンだけ変わるといった食い違いを防ぐ)。
		InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
		InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
		if (docList == nil ||
		    docList->FindDocByDataBase(sPeekTargetDB) == nil ||
		    docList->FindDocByDataBase(sPeekSourceDB) == nil)
		{
			KESCMHandleDocsClosed();
			return interest;
		}
	}

	if (type == IEvent::kMButtonDn)
	{
		if (sPeekArmed && e->ShiftKeyDown() && e->CmdKeyDown() && e->OptionAltKeyDown() && KESCMFrontViewIsOverTarget())
		{
			// Shift＋Ctrl＋Alt＋ミドル押下: クリック点の CMYK 生値(0..255)を新・旧でサンプリングし、
			// "Target C.. M.. Y.. K.." / "Source C.. …" をパネルのステータス行に表示する(次の操作まで残る)。
			// ★旧トースト表示(押下中だけカーソル脇に出す)は 2026-07-04 撤去。3キー同時は
			// この先頭分岐で捕まえる(後続の Shift/Ctrl/Alt 単独 peek より前に置く=単独分岐に吸われないため)。
			// ★対象(Target)文書のウィンドウ上でのみ反応(KESCMFrontViewIsOverTarget)。Source 側や無関係な
			// 第3文書のウィンドウでミドルクリックしても、素のミドル動作を邪魔しないよう何もしない。
			PMString colorMsg;
			if (KESCMSampleCmykUnderMouse(sPeekTargetDB, sPeekSourceDB, colorMsg))
			{
				// 結果はパネルのステータス行に出るので、パネルが閉じている/アイコン化されていると
				// ユーザーが気づけない。押下中だけ一時表示し、ミドルを離したら元の状態
				// (閉じていた/アイコン化)へ戻す(KESCMPanelTempShowEnd は kMButtonUp 側)。
				// 既に見えていた場合は何も変えない(離しても閉じない)。
				KESCMPanelTempShowBegin();
				KESCMSetStatus(colorMsg);
			}
		}
		else if (sPeekArmed && e->CmdKeyDown() && !e->ShiftKeyDown() && !e->OptionAltKeyDown() && KESCMFrontViewIsOverTarget())
		{
			// ★2026-07-04: Shift+Ctrl から Ctrl 単独へ移動(Shift+Ctrl ミドルは未割当に)。
			// Ctrl(=Win, CmdKeyDown)＋ミドル押下(momentary): マウス下スプレッドだけ枠を再検出して更新。
			// 旧版画像キャッシュは破棄(次 peek で作り直し)。完了したら「spread N markers refreshed」を
			// パネルのステータス行に表示する(旧トースト表示は 2026-07-04 撤去)。
			// ★arm 済み(Start 後)かつ対象(Target)文書のウィンドウ上でのみ反応(KESCMFrontViewIsOverTarget)。
			// 3キー同時(Shift+Ctrl+Alt=CMYK)は先頭分岐で捕まえる上、ここは !Shift 条件なので衝突しない。
			int32 sp = -1;
			if (KESCMRefreshSpreadUnderMouse(sPeekTargetDB, sPeekSourceDB, &sp, nil))
			{
				PMString msg("spread ");
				msg.SetTranslatable(kFalse);
				msg.AppendNumber(sp + 1);	// スプレッド番号(1始まり)
				msg.Append(" markers refreshed");
				KESCMSetStatus(msg);
			}
		}
		else if (sPeekArmed && e->ShiftKeyDown() && e->OptionAltKeyDown() && !e->CmdKeyDown() && KESCMFrontViewIsOverTarget())
		{
			// Shift＋Alt＋ミドル押下: 旧版べた載せ(peek)を 50% 透明で重ねる(現行ページと半々のゴースト比較)。
			// ★対象(Target)文書のウィンドウ上でのみ反応(KESCMFrontViewIsOverTarget)。Shift 単独/ Alt 単独の
			// 分岐より前に置く=吸われない。
			KESCMBeginPeekHold(kKESCMPeekSemiOpacity);
		}
		else if (sPeekArmed && e->ShiftKeyDown() && !e->CmdKeyDown() && KESCMFrontViewIsOverTarget())
		{
			// Shift＋ミドル押下: マウス下スプレッドの旧版べた載せ(peek)を不透明(100%)で開始。押下中だけ表示。
			// 判定はこの押下時の修飾キー状態のみ。以後キーを離しても変わらず、ミドルを離すと消える。
			// ★対象(Target)文書のウィンドウ上でのみ反応(KESCMFrontViewIsOverTarget)。CMYK サンプリングと同じ理由。
			// ★!Ctrl 条件: スプレッド再比較が Shift+Ctrl→Ctrl 単独へ移動(2026-07-04)した際、Shift+Ctrl が
			//   この分岐に落ちて 100% peek が誤発動しないように弾く(Shift+Ctrl ミドルは未割当=無反応)。
			//   Shift+Alt は上の 50% peek 分岐が先に捕まえるので、ここは実質 Shift 単独用。
			KESCMBeginPeekHold(PMReal(1.0));
		}
		else if (e->OptionAltKeyDown() && !e->ShiftKeyDown() && !e->CmdKeyDown())
		{
			// ★2026-07-04: Ctrl 単独から Alt 単独へ移動(Ctrl 単独は同日「スプレッド再比較」に再割当)。
			// Alt＋ミドル押下(momentary): 「地図」ナビゲーション。マウス下のウィンドウ
			// (アクティブでなくてもよい)のローカル座標へ、他の全ウィンドウをスクロールする。arm 状態に依らず常に使える。
			KESCMSyncScrollOtherWindowsUnderMouse(e);
		}
		else if (!e->ShiftKeyDown() && !e->CmdKeyDown() && !e->OptionAltKeyDown())
		{
			// シングル動作(修飾キーなしミドル押下中): 全マーク(リング＋変更数)をパネルで選択中の
			// 不透明度(25%/75%)で表示し、ハンドツールに切替えて「枠を見ながら掴んで移動」できるようにする。
			// 離す(kMButtonUp)と非表示＋不透明度を基準値へ戻す。
			// マークが何も無い(エントリ無し)時は反応しない=素のミドルクリックを邪魔しない。
			const bool16 haveContent = !KESCMDrawEventHandler::sEntries.empty();
			if (haveContent)
			{
				sSingleShowing = kTrue;
				KESCMDrawEventHandler::sMarkScreenOpacity = KESCMDrawEventHandler::SelectedMarkOpacity();	// パネルの 25%/75%
				KESCMDrawEventHandler::sMarksVisible = kTrue;	// 押下中だけ枠等を表示
				KESCMEnterHandTool();	// 枠を見ながら掴んで移動
				KESCMInvalidateMarksDoc();
			}
		}
		// (その他の組み合わせ(Shift+Ctrl ミドル等)や、Shift/Ctrl 系で arm 未済 → 何もしない=素のミドルを邪魔しない。
		//  2026-07-04 の再割当: Alt＋ミドル=地図ナビゲーション(旧 Ctrl 単独)、Ctrl 単独=スプレッド再比較(旧 Shift+Ctrl)。
		//  Shift+Ctrl ミドルと「枠を通常(不透明)で表示」(旧 Alt 単独)は未割当/撤去)
	}
	else // kMButtonUp
	{
		// ミドルを離したら、ハンドに切替えていた場合は元のツールへ戻す(シングル/ダブル共通)。
		KESCMRestoreTool();

		// CMYK比較(3キー+ミドル)で一時表示していたパネルを元の状態(閉/アイコン)へ戻す。
		// 一時表示していなければ無害な no-op。
		KESCMPanelTempShowEnd();

		if (sPeekActive)
		{
			// Shift／Shift+Alt＋ミドルを離した(ミドル解放) → 旧版を隠す(マークは触らない)。キャッシュは保持(再 peek は即時)。
			sPeekActive = kFalse;
			if (KESCMDrawEventHandler::sShowOriginal)
			{
				KESCMDrawEventHandler::sShowOriginal = kFalse;
				KESCMInvalidateDB(sPeekTargetDB);
			}
		}
		else if (sSingleShowing)
		{
			// ミドルのみの押下を離した → 枠表示を解除し、不透明度を基準値(印刷設定に応じた値)へ戻す＋非表示へ。
			sSingleShowing = kFalse;
			KESCMDrawEventHandler::sMarksVisible = kFalse;
			KESCMDrawEventHandler::sMarkScreenOpacity = KESCMBaseScreenOpacity();
			KESCMInvalidateMarksDoc();
		}
	}
	return interest;
}

void KESCMPeekWatcher::StartWatching()
{
	if (fWatching) return;
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IEventDispatcher> dispatcher(app, UseDefaultIID());
	if (dispatcher)
	{
		dispatcher->AddEventWatcher(this, IEventDispatcher::EventTypeList(IEvent::kMButtonDn, IEvent::kMButtonUp));
		fWatching = kTrue;
	}
}

void KESCMPeekWatcher::StopWatching()
{
	if (!fWatching) return;
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IEventDispatcher> dispatcher(app, UseDefaultIID());
	if (dispatcher)
		dispatcher->RemoveEventWatcher(this, IEventDispatcher::EventTypeList());	// 既定ctor=kAllEventTypes(全ビットON)=全種の監視を解除。「空」ではない(IEventDispatcher.h)
	fWatching = kFalse;
}


//========================================================================================
// KESCMPeekStartup
//   アプリ起動時に peek ウォッチャを生成して監視を開始する。
//========================================================================================
class KESCMPeekStartup : public CPMUnknown<IStartupShutdownService>
{
public:
	KESCMPeekStartup(IPMUnknown* boss) : CPMUnknown<IStartupShutdownService>(boss), fWatcher(nil) {}
	~KESCMPeekStartup() {}

	virtual void Startup();
	virtual void Shutdown();

private:
	IEventWatcher* fWatcher;
};

CREATE_PMINTERFACE(KESCMPeekStartup, kKESCMPeekStartupImpl)

void KESCMPeekStartup::Startup()
{
	fWatcher = ::CreateObject2<IEventWatcher>(kKESCMPeekWatcherBoss);
	if (fWatcher)
		fWatcher->StartWatching();
}

void KESCMPeekStartup::Shutdown()
{
	if (fWatcher)
	{
		fWatcher->StopWatching();
		fWatcher->Release();
		fWatcher = nil;
	}
	// 保持していたマーク/旧版画像バッファを解放(終了時もきれいに片付ける)。
	KESCMDrawEventHandler::DropAll();
	KESCMDrawEventHandler::DropAllOrig();
	// ハンドツール一時切替中(ミドル押下中)に終了した場合、保存していた元ツールの参照が残る。
	// 終了処理中なので SetActiveTool(KESCMRestoreTool)は呼ばず、参照の解放だけを行う。
	if (sSavedTool != nil)
	{
		sSavedTool->Release();
		sSavedTool = nil;
	}
	sHandActive = kFalse;

	// レイアウトビュー同期の後始末は「状態フラグを落とすだけ」にする(以後の通知は Update 先頭の
	// ガードで無視される)。★KESCMSetLayoutSync(kFalse) をここで呼んではならない: その経路は
	// GetActiveContext()/GetAllLayoutViews に触るが、アプリ終了処理中はセッション/コンテキストが
	// 解体中で deref すると 100% クラッシュする(実機で確認済み。ON のまま終了→必ず落ちた)。
	// 手本の KESLayoutScrollObserver も終了時は何も外さず無事故=購読(依存)は subject/observer の
	// boss 破棄と一緒に IChangeManager から消えるので、明示的な解除は不要。
	sLayoutSyncOn = kFalse;
}

//========================================================================================
// arm / disarm / 状態アクセサ(KESCMCore.h で宣言)。上の file-local な peek 状態を共有させるため、
// ここに置いている。
//========================================================================================

void KESCMDoArmMousePeek(IDataBase* targetDB, IDataBase* sourceDB)
{
	// arm 対象が変わったら古い peek キャッシュは捨てる。
	if (sPeekSourceDB != sourceDB || sPeekTargetDB != targetDB)
		KESCMDrawEventHandler::DropAllOrig();

	sPeekTargetDB = targetDB;
	sPeekSourceDB = sourceDB;
	sPeekArmed = kTrue;
	sPeekActive = kFalse;			// 覗き状態を初期化
	sSingleShowing = kFalse;
	KESCMDrawEventHandler::sMarksVisible = kFalse;	// 既定(非表示)へ。arm 中も枠は押下中だけ表示
}

void KESCMDoDisarmMousePeek(IDataBase* db)
{
	// nil化する前に、実際に arm されていた対象文書を控えておく。呼び出し側の db(=操作時のアクティブ
	// 文書)が前面で Source や無関係な第3文書に切り替わっていても、対象文書の枠が即座に消えるように
	// するため(タイル表示等で対象文書が同時に見えている場合に効く)。
	IDataBase* armedTargetDB = sPeekTargetDB;

	KESCMRestoreTool();	// ハンドに切替え中なら元のツールへ戻す
	sPeekArmed = kFalse;
	sPeekTargetDB = nil;
	sPeekSourceDB = nil;
	sPeekActive = kFalse;
	sSingleShowing = kFalse;
	KESCMDrawEventHandler::sMarksVisible = kFalse;	// 既定(非表示)のまま
	KESCMDrawEventHandler::DropAllOrig();	// sShowOriginal も OFF にし、キャッシュを解放

	KESCMInvalidateDB(armedTargetDB);
	if (db != armedTargetDB)
		KESCMInvalidateDB(db);
}

// パネルの状態アクセサ(arm 済み peek =「開始済み」状態を反映する)。
bool16     KESCMIsArmed()        { return sPeekArmed; }
IDataBase* KESCMArmedTargetDB()  { return sPeekTargetDB; }
IDataBase* KESCMArmedSourceDB()  { return sPeekSourceDB; }

//========================================================================================
// KESCMHandleDocsClosed(KESCMCore.h で宣言)
//   ドキュメントがクローズされた直後(kAfterCloseDoc レスポンダ)に呼ばれる。追跡中の全DB
//   (マーク sDB / 旧版 sOrigDB / peek arm の target・source)を IDocumentList で
//   生存確認する。どの db が閉じたかは信号から取れないため、この生存スイープで判定する
//   (HandleDrawEvent と同じ手法を、描画を待たずクローズ確定時に能動実行)。
//
//   ★比較(対象/元のどちらか)に関わる db が1つでも閉じていたら、個別の static だけを直すのではなく
//   Stop ボタン(DoClear 相当)のフルクリーンアップを行う。以前は sDB 自体が閉じた場合しかマークを
//   消さなかったため、「元」だけを閉じると peek arm は disarm されてボタン表示は Start に戻るのに、
//   対象文書はまだ開いているのでマーク(枠)が消えずに残る、という見た目の不整合があった。
//
//   ★重要: 閉じた db ポインタは「FindDocByDataBase への比較」だけに使い、絶対に deref しない。
//   閉じた文書の IDataBase は既に解放されている可能性があるため、その db 自体への後片付け
//   (KESCMDoDisarmMousePeek 等の InvalidateViews で deref する処理)は呼ばない。
//   ただし比較相手がまだ開いている場合(タイル表示等)、そちら側は生存確認済みなので
//   KESCMDoClearMarks と同様に InvalidateViews して枠を即座に消す(生き残り側の再描画漏れ対策)。
//========================================================================================
void KESCMHandleDocsClosed()
{
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return;

	bool16 changed = kFalse;

	// 比較に関わる db(マーク sDB / 旧版 sOrigDB / peek arm の target・source)のいずれかが閉じたか。
	const bool16 comparisonDocClosed =
		(KESCMDrawEventHandler::sDB     != nil && docList->FindDocByDataBase(KESCMDrawEventHandler::sDB)     == nil) ||
		(KESCMDrawEventHandler::sOrigDB != nil && docList->FindDocByDataBase(KESCMDrawEventHandler::sOrigDB) == nil) ||
		(sPeekArmed &&
		 ((sPeekTargetDB != nil && docList->FindDocByDataBase(sPeekTargetDB) == nil) ||
		  (sPeekSourceDB != nil && docList->FindDocByDataBase(sPeekSourceDB) == nil)));

	if (comparisonDocClosed)
	{
		// DropAll/DropAllOrig で nil にする前に、まだ開いている側の db を控えておく(生存確認済みなので
		// 後で安全に InvalidateViews できる)。閉じた方の db は決して拾わない。
		IDataBase* survivorTargetDB = nil;
		IDataBase* survivorOrigDB   = nil;
		if (KESCMDrawEventHandler::sDB != nil && docList->FindDocByDataBase(KESCMDrawEventHandler::sDB) != nil)
			survivorTargetDB = KESCMDrawEventHandler::sDB;
		if (KESCMDrawEventHandler::sOrigDB != nil && docList->FindDocByDataBase(KESCMDrawEventHandler::sOrigDB) != nil)
			survivorOrigDB = KESCMDrawEventHandler::sOrigDB;
		if (sPeekArmed)
		{
			if (survivorTargetDB == nil && sPeekTargetDB != nil && docList->FindDocByDataBase(sPeekTargetDB) != nil)
				survivorTargetDB = sPeekTargetDB;
			if (survivorOrigDB == nil && sPeekSourceDB != nil && docList->FindDocByDataBase(sPeekSourceDB) != nil)
				survivorOrigDB = sPeekSourceDB;
		}

		// Stop ボタン(DoClear)相当のフルクリーンアップ。
		KESCMDrawEventHandler::DropAll();		// sDB=nil、マークエントリ破棄
		KESCMDrawEventHandler::DropAllOrig();	// sOrigDB=nil、旧版べた載せ破棄
		KESCMRestoreTool();						// ハンドに切替え中なら元のツールへ戻す(db を触らない)
		sPeekArmed     = kFalse;
		sPeekTargetDB  = nil;
		sPeekSourceDB  = nil;
		sPeekActive    = kFalse;
		sSingleShowing = kFalse;
		KESCMDrawEventHandler::sMarksVisible = kFalse;
		changed = kTrue;

		PMString s("marks cleared");	// Stop ボタン(DoClear)と同じメッセージ
		s.SetTranslatable(kFalse);
		KESCMSetStatus(s);

		// Stop ボタン(KESCMDoClearMarks)と同じく、生存している側を再描画して枠を即座に消す。
		KESCMInvalidateDB(survivorTargetDB);
		if (survivorOrigDB != survivorTargetDB)
			KESCMInvalidateDB(survivorOrigDB);
	}

	// 「Hide Unchanged Spreads」トグルの後片付け(Target/Source 両側)。隠し先のどちらかが閉じた、
	// または比較関連の db が閉じてマークを全消しした(comparisonDocClosed=「変更なし」判定の根拠が
	// 消えた)場合は、リセットしてトグルを OFF に戻す。KESCMResetHideUnchanged(kTrue) は内部で文書の
	// 生存確認を行い、生存側のみ再表示・閉じた側は deref せず状態破棄するので、ここでは kTrue 一択で
	// よい。無関係な第3文書が閉じただけなら何もしない。
	IDataBase* hideDB    = KESCMGetHideUnchangedDB();
	IDataBase* hideSrcDB = KESCMGetHideUnchangedSrcDB();
	if (hideDB != nil || hideSrcDB != nil)
	{
		const bool16 hideTargetClosed = (hideDB    != nil && docList->FindDocByDataBase(hideDB)    == nil);
		const bool16 hideSourceClosed = (hideSrcDB != nil && docList->FindDocByDataBase(hideSrcDB) == nil);
		if (hideTargetClosed || hideSourceClosed || comparisonDocClosed)
		{
			KESCMResetHideUnchanged(kTrue);
			changed = kTrue;
		}
	}

	// 何か片付けたらパネルの ON/OFF 表示を実状態に合わせる(①「ON 固着」の解消)。
	if (changed)
		KESCMRefreshPanel();
}
