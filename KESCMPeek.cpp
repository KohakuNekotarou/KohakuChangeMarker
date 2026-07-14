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
#include "LayoutUIID.h"
#include "DocumentContextID.h"

// ジオメトリ / ビュー:
#include "IControlView.h"
#include "IPanorama.h"
#include "IWidgetParent.h"
#include "PMMatrix.h"
#include "PMPoint.h"
#include "PMRect.h"					// ページのペーストボード矩形(Alt+ミドルの追加/削除補正)
#include "PMReal.h"
#include "TransformUtils.h"

// カスタムビットマップカーソル(Alt+左 CMYK 情報をカーソルにも描く):
#include "ICursorUtils.h"			// QueryGraphicsPortForBitmap(自前バッファに AGM 描画)/CursorSpec も同梱
#include "IGraphicsPort.h"			// setrgbcolor/rectfill/selectfont/show
#include "IFontMgr.h"				// 既定フォント取得
#include "IPMFont.h"

#include <map>
#include <vector>
#include <set>
#include <algorithm>				// std::find(選択ページの重複除去)
#include <cstring>				// std::memset(カーソルバッファの透明クリア)
#include <chrono>				// steady_clock(ドラッグ中ライブ再サンプルのスロットル)

#include "UIDList.h"				// GetSelectedPages(ページパネル選択の公式取得)

// プロジェクト内インクルード:
#include "KESCMID.h"
#include "KESCMConstants.h"
#include "KESCMDrawEventHandler.h"   // エンジンの共有 static ＋ KESCMQueryPanorama
#include "KESCMColorSampler.h"       // KESCMSampleCmykUnderMouse
#include "KESCMCore.h"               // arm/disarm/状態 宣言
#include "KESCMPageMap.h"            // KESCMMapTargetToSource(除外対応表)/KESCMPageMapSweepClosedDocs
#include "KESCMPageCheck.h"          // KESCMPageCheckClearAllDocs / KESCMPageCheckSweepClosedDocs(✓の後片付け)
#include "KESCMThumbnailRefresh.h"   // クローズ後、生存側の Pages パネルサムネイルから枠を消す
#include "KESCMScrollMap.h"          // スプレッド再比較後にスクロールバー地図を最新化
#include "KESCMThumbIdleTask.h"      // クローズ後の再生成を次のidleに遅延(前面切替の過渡を避ける)
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

// 画面マークの「基準」不透明度(=ミドルを押していない常時表示時の値)。
//   印刷マークON中はパネルで選択中の不透明度(25%/75%。画面と印刷の見た目を一致)、印刷OFFは 1.0。
//   ミドルを離したら sMarkScreenOpacity をこの値へ戻す。
PMReal KESCMBaseScreenOpacity()
{
	// 印刷マーク ON、または「Hold to Hide Marks」(枠を画面に常時表示)ON のときは、常時表示の枠を
	// パネル選択の 25%/75% で描く(押下中の一時表示と見た目を揃える)。どちらも OFF なら 1.0(不透明)。
	return (KESCMDrawEventHandler::sPrintMarks || KESCMDrawEventHandler::sAlwaysShowMarks)
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
		// 新→旧のページ対応は除外対応表(登録済み=比較相手なしページを除いた順番対応)で引く。
		// 実際にラスタ化するこの分岐でだけ必要(キャッシュヒット=同一スプレッドの再 peek が最頻ケースで、
		// その度に対応表を作り直すのは無駄だった)。
		KESCMDrawEventHandler::DropAllOrig();		// 覗くのは1スプレッドだけ=他は破棄
		KESCMDrawEventHandler::sOrigDB = targetDB;
		KESCMDrawEventHandler::sOrigScale = effScale;	// このラスタ化解像度を記録(再 peek の作り直し判定用)
		// 対応表はスプレッド内で同一なのでループ前に1回だけ作る(ページごとの KESCMBuildPairing 再構築を回避)。
		std::vector<UID> pairT, pairS;
		KESCMBuildPairing(targetDB, sourceDB, pairT, pairS);
		std::map<UID, UID> targetToSource;
		for (size_t k = 0; k < pairT.size(); ++k)
			targetToSource[pairT[k]] = pairS[k];
		for (int32 p = 0; p < np; ++p)
		{
			const UID tPageUID = spread->GetNthPageUID(p);
			std::map<UID, UID>::const_iterator mi = targetToSource.find(tPageUID);
			if (mi == targetToSource.end())
				continue;
			UIDRef tRef(targetDB, tPageUID);
			UIDRef sRef(sourceDB, mi->second);
			if (KESCMDrawEventHandler::MakeOrigImage(tRef, sRef, peekDpi) == kSuccess)
				++captured;
		}
	}
	KESCMDrawEventHandler::sShowOriginal = kTrue;

	KESCMInvalidateDB(targetDB);

	if (outSpread) *outSpread = s;
	if (outPages)  *outPages = captured;
	return kKESCMPeekShown;
}


// ページ比較の部分更新(共通コア): targetPages(= targetDB 上のページUID列)を再比較して枠(リング)を
// 更新する。source 対応は除外対応表(登録済みページを除いた順番対応)で引く。
//   ・各ページを MakeEntry で取り直し(編集後の差分に更新)。変化が無くなったページは古い枠を消す。
//   ・旧版画像キャッシュ(sOrigImages)は古いので破棄(次の peek で作り直し)。
//   ・✓ の剪定/レイアウト・スクロールバー地図・Pages パネルサムネイルの更新まで行う。
//   変化ページ数を outChanged に返す。戻り=1ページ以上処理したか。
//   ★旧 Ctrl+ミドル(マウス下スプレッド再比較)の中核をページ指定へ一般化したもの(2026-07-13 移設)。
static bool16 KESCMRefreshComparisonCore(IDataBase* targetDB, IDataBase* sourceDB,
                                         const std::vector<UID>& targetPages, int32* outChanged)
{
	if (outChanged) *outChanged = 0;
	if (targetDB == nil || sourceDB == nil || targetPages.empty())
		return kFalse;

	// マークの所属ドキュメントを合わせる(別 doc にマークがあった場合のみ総入れ替え=通常は一致で何もしない)。
	if (KESCMDrawEventHandler::sDB != nil && KESCMDrawEventHandler::sDB != targetDB)
		KESCMDrawEventHandler::DropAll();
	KESCMDrawEventHandler::sDB = targetDB;

	// 除外対応表(登録済みページを除いた順番対応)を1回だけ作り、target→source を引けるようにする。
	std::vector<UID> pairT, pairS;
	KESCMBuildPairing(targetDB, sourceDB, pairT, pairS);
	std::map<UID, UID> targetToSource;
	for (size_t k = 0; k < pairT.size(); ++k)
		targetToSource[pairT[k]] = pairS[k];

	// 指定ページを再比較して枠を更新。触れたページ(target とその source 対応)を集めておき、後で Pages
	// パネルのサムネイルを per-UID Purge する。変化あり/なしの両方を入れる=変化なしに戻って sEntries から
	// 外れたページも古いリングを確実に消せるようにするため。
	int32 changedCount = 0;
	std::vector<UID> touchedTargetPages, touchedSourcePages;
	for (size_t i = 0; i < targetPages.size(); ++i)
	{
		const UID tUID = targetPages[i];
		std::map<UID, UID>::const_iterator mi = targetToSource.find(tUID);
		if (mi == targetToSource.end())
			continue;	// 登録済み(比較相手なし)ページ等は再比較対象外
		const UID sUID = mi->second;
		touchedTargetPages.push_back(tUID);
		touchedSourcePages.push_back(sUID);
		bool16 changed = kFalse;
		KESCMDrawEventHandler::MakeEntry(UIDRef(targetDB, tUID), UIDRef(sourceDB, sUID), changed);
		if (changed)
			++changedCount;
		else
		{
			// 変化が無くなったページ → 古い枠が残っていれば消す(更新で消えるべき)。エントリと同時に
			// Source 側対応表(sSrcPageToTarget[sUID])も掃除する共通ヘルパへ統一(ドロップ処理を1本化)。
			KESCMDrawEventHandler::DropOneEntry(tUID, sUID);
		}
	}
	if (touchedTargetPages.empty())
		return kFalse;

	// 旧版画像キャッシュは古いので破棄(次の peek で現ズームで作り直し)。
	KESCMDrawEventHandler::DropAllOrig();

	// ★「KESCM: Check」の✓: この部分再比較でマーク(枠)が消えたページのチェックも忘れる(ユーザー指定
	//   2026-07-11「枠が無くなったらチェックの記憶も外れる」)。★必ず下の KESCMInvalidateDB より前に呼ぶ
	//   (Invalidate 後に外すと古い ✓ でレイアウトが描き直される)。prune 前に Source 側のチェック有無を
	//   控える(最後の1個が外れた場合もサムネイルを確実に更新するため)。
	const bool16 srcHadChecks = KESCMPageCheckHasAny(sourceDB);
	KESCMPageCheckPruneToMarked();

	KESCMInvalidateDB(targetDB);
	// Source 側のレイアウトビューも再描画する。エントリの増減は Source の常時枠(Show Marks on Source)や
	// ✓(prune)の見た目も変えるため。
	if (sourceDB != targetDB)
		KESCMInvalidateDB(sourceDB);

	// スクロールバー地図 strip も最新化する(この部分再比較は KESCMDoMarkChangesDoc を通らない独立経路)。
	KESCMScrollMapInvalidateAll();

	// レイアウトビューだけでなく Pages パネルのサムネイルも即時更新する。触れたページだけを per-UID Purge
	// する KESCMRefreshThumbnailsForPages を使う(触っていない他ページのサムネイルは再生成しない)。変化
	// あり/なし両方を渡すので、変化なしに戻ったページの古いリングも確実に消える。
	KESCMRefreshThumbnailsForPages(targetDB, touchedTargetPages);
	// Source 側サムネイルのリングは Show Marks on Source(sSrcMarksOn)ON のときだけ出る。ただし ✓ は
	// sSrcMarksOn と無関係にサムネイルへ出るので、prune 前に Source にチェックがあった場合も更新する。
	if (KESCMDrawEventHandler::sSrcMarksOn || srcHadChecks)
		KESCMRefreshThumbnailsForPages(sourceDB, touchedSourcePages);

	if (outChanged) *outChanged = changedCount;
	return kTrue;
}

// ページパネルの選択ページの「ページ比較」を再検出して更新する(ページ右クリック「KESCM: Refresh Page
// Comparison」の実体。旧 Ctrl+ミドルの移設先)。arm 済み(Start 後)かつ前面文書が Target/Source のときだけ
// 動く。前面が Source のときは選択 Source ページを対応する Target ページへ写像してから再比較する(マークは
// Target 側に載るため)。outPages=処理したページ数、outChanged=うち変化したページ数。戻り=1ページ以上処理したか。
bool16 KESCMRefreshComparisonForSelectedPages(int32* outPages, int32* outChanged)
{
	if (outPages)   *outPages = 0;
	if (outChanged) *outChanged = 0;

	if (!KESCMIsArmed())
		return kFalse;
	IDataBase* targetDB = KESCMArmedTargetDB();
	IDataBase* sourceDB = KESCMArmedSourceDB();
	if (targetDB == nil || sourceDB == nil)
		return kFalse;

	// 選択が属する前面文書(=アクティブ文書)。Target/Source のどちらかでなければ何もしない。
	IDocument* doc = Utils<ILayoutUIUtils>()->GetFrontDocument();
	IDataBase* db = (doc != nil) ? ::GetDataBase(doc) : nil;
	if (db == nil || (db != targetDB && db != sourceDB))
		return kFalse;

	// ページパネルの選択ページ(実在ページのみ)を読む(「KESCM: Check」と同じ流儀)。
	UIDList sel(db);
	Utils<ILayoutUIUtils>()->GetSelectedPages(sel, kFalse /*masters除外*/, kTrue /*currentPageOnly*/, kTrue /*pagesOnly*/);
	std::vector<UID> flat;
	KESCMCollectPageUIDs(db, flat);
	std::set<UID> flatSet(flat.begin(), flat.end());

	std::vector<UID> selPages;
	const int32 n = sel.Length();
	for (int32 i = 0; i < n; ++i)
	{
		const UID u = sel[i];
		if (flatSet.count(u) > 0 && std::find(selPages.begin(), selPages.end(), u) == selPages.end())
			selPages.push_back(u);
	}
	if (selPages.empty())
		return kFalse;

	// 再比較コアは Target ページで駆動する。前面が Source なら Source→Target 写像で対象 Target ページを作る。
	std::vector<UID> targetPages;
	if (db == targetDB)
	{
		targetPages = selPages;
	}
	else	// db == sourceDB
	{
		for (size_t i = 0; i < selPages.size(); ++i)
		{
			UID tUID = kInvalidUID;
			if (KESCMMapSourceToTarget(targetDB, sourceDB, selPages[i], tUID) && tUID != kInvalidUID)
				targetPages.push_back(tUID);
		}
	}
	if (targetPages.empty())
		return kFalse;

	int32 changed = 0;
	if (!KESCMRefreshComparisonCore(targetDB, sourceDB, targetPages, &changed))
		return kFalse;

	if (outPages)   *outPages = (int32)targetPages.size();
	if (outChanged) *outChanged = changed;
	return kTrue;
}

// 「KESCM: Refresh Page Comparison」メニューを有効化してよいか(UpdateActionStates 用)。
// arm 済み(Start 後)かつ前面文書が Target/Source のとき kTrue。選択の有無までは見ない(ページ右クリックは
// 通常そのページを選択済みで、未選択でも DoAction 側が安全に no-op する)。
bool16 KESCMRefreshComparisonAvailable()
{
	if (!KESCMIsArmed())
		return kFalse;
	IDataBase* targetDB = KESCMArmedTargetDB();
	IDataBase* sourceDB = KESCMArmedSourceDB();
	if (targetDB == nil || sourceDB == nil)
		return kFalse;
	IDocument* doc = Utils<ILayoutUIUtils>()->GetFrontDocument();
	IDataBase* db = (doc != nil) ? ::GetDataBase(doc) : nil;
	return (db != nil && (db == targetDB || db == sourceDB)) ? kTrue : kFalse;
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

// マウス下のドキュメントウィンドウが Source(比較の旧側=常時表示枠を載せている sSrcDB)かどうか。
// 「Hold to Hide Marks」＋「Show Marks on Source」併用時、Source 窓でミドルを押したときだけ Source 枠を
// 一時退避させるための窓判定(Target 版 KESCMFrontViewIsOverTarget と対称)。Source マークの所属は sSrcDB を
// 正とする(arm の sPeekSourceDB と同一文書だが、判定はマークの実 db に紐づける)。
static bool16 KESCMFrontViewIsOverSource()
{
	if (KESCMDrawEventHandler::sSrcDB == nil)
		return kFalse;

	GSysPoint globalPt = Utils<IEventUtils>()->GetGlobalMouseLocation();

	InterfacePtr<IWindow> hitWindow(Utils<IWindowUtils>()->QueryWindowUnderPoint(globalPt, kFalse));
	if (hitWindow == nil)
		return kFalse;

	InterfacePtr<IDocumentPresentation> hitPres(hitWindow, UseDefaultIID());
	if (hitPres == nil)
		return kFalse;

	return (hitPres->GetDocumentUIDRef().GetDataBase() == KESCMDrawEventHandler::sSrcDB);
}

//========================================================================================
// ビューポート同期エンジン(共有)
//   手本パノラマの「見えている状態」= 実効ズーム(GetXScaleFactor(kTrue)、モニタPPI補正込み。
//   kZoomToCmdBoss の scaleFactor と同じ次元)+可視中心の content 座標 を、比較相手のドキュメントの
//   全レイアウトビューへ複製する(同一文書のビュー=スプリット相方は対象外)。
//   Alt+ミドル(単発)とフライアウト「Sync Layout Views」(自動)の両方がこの1本を使う。
//   ★2026-07-11(ユーザー指定): 発動は「比較を Start 中(sPeekArmed)」かつ「手本・宛先とも Target/Source」の
//   ときだけ。未 Start や第3文書は関数先頭のガードで弾く(=同期しない)。以前は arm と無関係に全文書へ
//   複製していたが、Target↔Source 間のみへ限定した。
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

//----------------------------------------------------------------------------------------
// ページ pageUID(db 内)のペーストボード矩形を得る。パノラマの content 座標=ペーストボード座標
// (PBPMPoint)と同じ空間。ページは回転しないので inner bbox の 2 隅を変換して min/max を取る。
//----------------------------------------------------------------------------------------
static bool16 KESCMPagePasteboardRect(IDataBase* db, UID pageUID, PMRect& outPB)
{
	if (db == nil || pageUID == kInvalidUID)
		return kFalse;
	InterfacePtr<IGeometry> geo(db, pageUID, UseDefaultIID());
	if (geo == nil)
		return kFalse;
	PMRect inner = geo->GetPathBoundingBox();
	PMMatrix m = ::InnerToPasteboardMatrix(geo);
	PMPoint p0(inner.Left(),  inner.Top());    m.Transform(&p0);
	PMPoint p1(inner.Right(), inner.Bottom()); m.Transform(&p1);
	const PMReal l = (p0.X() < p1.X()) ? p0.X() : p1.X();
	const PMReal r = (p0.X() < p1.X()) ? p1.X() : p0.X();
	const PMReal t = (p0.Y() < p1.Y()) ? p0.Y() : p1.Y();
	const PMReal b = (p0.Y() < p1.Y()) ? p1.Y() : p0.Y();
	outPB = PMRect(l, t, r, b);
	return kTrue;
}

//----------------------------------------------------------------------------------------
// db 内で、ペーストボード点 pb を内包するページ UID を返す。内包が無ければ中心が最も近いページ
// (ページ間の隙間/ペーストボード上をビュー中心が指しているとき)。ページが無ければ kInvalidUID。
//----------------------------------------------------------------------------------------
static UID KESCMFindPageAtPasteboard(IDataBase* db, const PBPMPoint& pb)
{
	if (db == nil)
		return kInvalidUID;
	std::vector<UID> flat;
	KESCMCollectPageUIDs(db, flat);
	UID best = kInvalidUID;
	PMReal bestDist2(0);
	bool16 haveBest = kFalse;
	for (size_t i = 0; i < flat.size(); ++i)
	{
		PMRect r;
		if (!KESCMPagePasteboardRect(db, flat[i], r))
			continue;
		if (pb.X() >= r.Left() && pb.X() <= r.Right() && pb.Y() >= r.Top() && pb.Y() <= r.Bottom())
			return flat[i];	// 内包するページが確定
		const PMReal cx = (r.Left() + r.Right()) / PMReal(2.0);
		const PMReal cy = (r.Top()  + r.Bottom()) / PMReal(2.0);
		const PMReal dx = pb.X() - cx, dy = pb.Y() - cy;
		const PMReal d2 = dx * dx + dy * dy;
		if (!haveBest || d2 < bestDist2) { bestDist2 = d2; best = flat[i]; haveBest = kTrue; }
	}
	return best;
}

//----------------------------------------------------------------------------------------
// Alt+ミドル/自動同期の追加/削除補正: 手本(srcDocDb)のビュー中心にあるページを、比較ペアリング
// (KESCMMapTargetToSource / KESCMMapSourceToTarget=登録ページを除外して残りを順番対応させるので、
// ページの追加/削除で番号がズレていても「本来の相手」を返す)で相手ページへ写像し、ページ中心からの
// 相対オフセットを保ったまま相手ページ上の座標へ変換する。これで「増減があっても比較対象のページ同士が
// 同じ位置に映る」。
//   ★outSkip: 手本中心が Added/Removed(登録)・overflow ページ=相手なしのときは kTrue を返す。
//     呼び出し側はこの宛先文書を同期しない(追従側を動かさず据え置く)。相手のいないページで追従側が
//     生座標へ飛ぶのを避けるため(ユーザー指定 2026-07-10)。
//   補正できないが skip でもない場合(未 arm / arm ペア以外の第3文書 / 中心がページ外 / 幾何取得失敗)は
//   srcCenter をそのまま返す=従来の生同期にフォールバックする(outSkip=kFalse)。
//----------------------------------------------------------------------------------------
static PBPMPoint KESCMCorrectedCenterForDoc(IDataBase* srcDocDb, IDataBase* dstDb,
                                            const PBPMPoint& srcCenter, bool16& outSkip)
{
	outSkip = kFalse;

	if (!sPeekArmed || sPeekTargetDB == nil || sPeekSourceDB == nil)
		return srcCenter;	// 未 arm: 生同期

	// ペアリング方向。arm ペア(Target/Source)以外の第3文書は補正しない=生同期。
	const bool16 t2s = (srcDocDb == sPeekTargetDB && dstDb == sPeekSourceDB);
	const bool16 s2t = (srcDocDb == sPeekSourceDB && dstDb == sPeekTargetDB);
	if (!t2s && !s2t)
		return srcCenter;

	// 手本ページ(srcDocDb 側でビュー中心にあるページ)。
	const UID srcPage = KESCMFindPageAtPasteboard(srcDocDb, srcCenter);
	if (srcPage == kInvalidUID)
		return srcCenter;	// 中心がページ外(ページ間の隙間等): 生同期

	// 相手ページを引く。Added/Removed(登録)・overflow は相手なし → このページでは同期しない(skip)。
	UID dstPage = kInvalidUID;
	const bool16 mapped = t2s
		? KESCMMapTargetToSource(sPeekTargetDB, sPeekSourceDB, srcPage, dstPage)
		: KESCMMapSourceToTarget(sPeekTargetDB, sPeekSourceDB, srcPage, dstPage);
	if (!mapped || dstPage == kInvalidUID)
	{
		outSkip = kTrue;	// ★相手なし(Added 等): 追従側は動かさない
		return srcCenter;
	}

	// ページ内相対位置(ページ中心からのオフセット)を保って相手ページ中心へ移す。
	PMRect srcRect, dstRect;
	if (!KESCMPagePasteboardRect(srcDocDb, srcPage, srcRect) ||
	    !KESCMPagePasteboardRect(dstDb,    dstPage, dstRect))
		return srcCenter;	// 幾何取得失敗: 生同期にフォールバック(skip はしない)
	const PMReal srcCX = (srcRect.Left() + srcRect.Right()) / PMReal(2.0);
	const PMReal srcCY = (srcRect.Top()  + srcRect.Bottom()) / PMReal(2.0);
	const PMReal dstCX = (dstRect.Left() + dstRect.Right()) / PMReal(2.0);
	const PMReal dstCY = (dstRect.Top()  + dstRect.Bottom()) / PMReal(2.0);
	return PBPMPoint(dstCX + (srcCenter.X() - srcCX), dstCY + (srcCenter.Y() - srcCY));
}

// applyPageOffset=kTrue のとき、各宛先文書へ複製する中心座標に上の追加/削除補正を掛ける。
// ★Alt+ミドルとフライアウト「Sync Layout Views」の自動同期は、どちらも本仕様として kTrue で呼ぶ
// (比較 arm 中は比較ペアの相手ページ同士がきっちり並ぶ)。未 arm/ペア外は関数内で生同期にフォールバック。
// 既定 kFalse は補正なし(生座標)の呼び口を残すためのもの。
static void KESCMSyncOtherDocViewportsTo(IPanorama* srcPano, IDataBase* srcDocDb, bool16 applyPageOffset = kFalse, bool16 limitToArmedPair = kTrue)
{
	if (srcPano == nil)
		return;

	// ★limitToArmedPair=kTrue(中ボタン Alt+ミドル・フライアウト「Sync Layout Views」のライブ同期):
	//   「比較を Start 中(arm 済み)」かつ「Target↔Source の間だけ」に限定する(ユーザー指定 2026-07-11)。
	//   ・未 Start(!sPeekArmed)、または arm 対の一方でも不明なら何もしない。
	//   ・手本(操作した)ビューが Target/Source のどちらでもない第3文書なら同期しない。
	// ★limitToArmedPair=kFalse(ツール+左ダブルクリック、ユーザー指定 2026-07-13): arm 不問。開いている
	//   全ドキュメント(手本=カーソル下の文書だけ除外)へ複製する。ページ補正は arm 中の Target/Source ペアの
	//   ときだけ KESCMCorrectedCenterForDoc が効き、その他は生同期にフォールバックする。
	if (limitToArmedPair)
	{
		if (!sPeekArmed || sPeekTargetDB == nil || sPeekSourceDB == nil)
			return;
		if (srcDocDb != sPeekTargetDB && srcDocDb != sPeekSourceDB)
			return;
	}

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

		// ★Target/Source 以外の第3文書へは複製しない(ユーザー指定 2026-07-11)。手本は上のガードで
		// 既に Target/Source のどちらかなので、宛先は「対の相手」1文書だけになる。
		// limitToArmedPair=kFalse(左ダブルクリック)のときはこの制限を外し、手本以外の全文書を宛先にする。
		if (limitToArmedPair && db != sPeekTargetDB && db != sPeekSourceDB)
			continue;

		// この宛先文書へ複製する中心座標。applyPageOffset のときは追加/削除補正(比較ペアの相手ページへ
		// 写像してページ内相対位置を保つ)を掛ける。手本中心が Added ページ(相手なし)なら skip=この宛先は
		// 同期しない(追従側を据え置く)。補正不能だが skip でもないなら srcCenter がそのまま返る(生同期)。
		PBPMPoint dstCenter = srcCenter;
		if (applyPageOffset)
		{
			bool16 skipThisDoc = kFalse;
			dstCenter = KESCMCorrectedCenterForDoc(srcDocDb, db, srcCenter, skipThisDoc);
			if (skipThisDoc)
				continue;	// ★手本中心が Added ページ(相手なし)=この宛先文書は同期しない
		}

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
				PMReal dx = curCenter.X() - dstCenter.X(); if (dx < 0) dx = -dx;
				PMReal dy = curCenter.Y() - dstCenter.Y(); if (dy < 0) dy = -dy;
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
			// 手本の可視中心と同じ content 座標(補正時は相手ページ上の対応座標)をビュー中心へ=
			// 同じ拡大率なら同じ画面が同じように映る。ズーム(コマンド)実行後に行うので、新しい倍率で
			// 正しくセンタリングされる。
			// 自動同期(高頻度通知)ではその都度の同期再描画を避け、invalidate だけして OS の描画
			// サイクルにまとめさせる(軽量化)。スクロール位置は同期更新されるので上の dedup 判定は
			// forceRedraw=kFalse でも正しく効く。
			pano->ScrollContentLocationToFrameCenter(dstCenter, kFalse /*forceRedraw*/);
		}
	}

	sLayoutSyncBroadcasting = kFalse;
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

	// ★本仕様(2026-07-10 確定): 自動同期(ライブ)にも追加/削除補正を掛ける。比較 arm 中は比較ペアの
	// 相手ページ同士がきっちり並ぶ(実機で使い勝手を確認済み)。未 arm/ペア外は関数内で生同期にフォールバック。
	KESCMSyncOtherDocViewportsTo(srcPano, srcDocDb, kTrue /*applyPageOffset*/);
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
				KESCMSyncOtherDocViewportsTo(pano, db, kTrue /*applyPageOffset(本仕様): 初回そろえも補正付き*/);
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
// トラッカー(左ボタン)用の共有入口。KESCM ツール選択中の左ボタン押下/解放から呼ばれる
// (KESCMTracker.cpp)。中ボタンの「修飾なし押下=マーク reveal」(上の WatchEvent kMButtonDn の
// 素のミドル分岐)と同じ挙動を、左ボタンでも使えるようにする最小の切り出し。ここはファイル内の
// peek 状態(sSingleShowing)と描画状態(KESCMDrawEventHandler::sMarks*)にアクセスできる。
//
// ★Step 1 (2026-07-12): 「修飾なし=マーク一時表示」を移植。
// ★Step 2 (2026-07-13): 「Hold to Hide Marks」極性反転(パネルメニューで Hold ON のとき、押下中だけ
//   常時表示の枠を隠す)を移植。窓別 temp-hide(Target/Source)まで WatchEvent kMButtonDn/Up と共通の挙動。
// ★Step 3 (2026-07-13): Shift+左=旧版べた載せ peek 100% / Shift+Alt+左=peek 50% を移植
//   (中ボタン Shift+ミドル / Shift+Alt+ミドル 相当)。
// ★Step 4 (2026-07-13): Alt+左(単独)=CMYK 生値サンプリング(中ボタン Shift+Ctrl+Alt+ミドル 相当)を移植。
//   Ctrl(cmd)系の各ジェスチャ(再比較/パネル)はまだ中ボタン専用。中ボタン側の WatchEvent は一切変更して
//   いない(両入力は併存)。
//========================================================================================

// トラッカー(左ボタン)用の peek 開始。arm 済み(Start 後)かつ Target 窓上のときだけ、マウス下スプレッドの
// 旧版を opacity(1.0=不透明 / 0.5=半透明)で重ねる。ハンドツールへの一時切替はしない(トラッカーが既に
// マウスをキャプチャ済みで、ドラッグは ContinueTracking へ行くため不要)。
static void KESCMTrackerBeginPeek(PMReal opacity)
{
	if (!sPeekArmed || !KESCMFrontViewIsOverTarget())
		return;	// 未 Start / Target 窓以外では反応しない(中ボタン peek 分岐と同じ条件)
	sPeekActive = kTrue;
	KESCMDrawEventHandler::sPeekOpacity = opacity;	// 旧版べた載せの不透明度(描画時に参照)
	sSingleShowing = kFalse;
	KESCMDrawEventHandler::sMarksVisible = kFalse;	// 覗き中は枠等を出さない(旧版だけ)
	KESCMPeekShowUnderMouse(sPeekTargetDB, sPeekSourceDB, nil, nil);
}

//========================================================================================
// Alt+左「色比較」の CMYK 情報を、パネル状態行に加えて**カーソル自身**にも描く。
//   カーソルは OS 描画=ドキュメント窓枠を超えマウス追従(仕組み: CursorSpec のコールバックで
//   自前バッファに AGM 描画する「カスタムビットマップカーソル」。ChangeModalCursor はトラッカー
//   =独自ツールを持つ KESCM だから使える特典)。CreateCursorBitmapProc は引数でデータを渡せない
//   ので、描く文字列は file-static sCmykCursorText に置きコールバックから読む。
//   ★これはまず「出るか」を見る実装スパイク(2026-07-13)。座標系(y方向)・alpha・サイズは実機で調整。
//========================================================================================
static PMString sCmykCursorText;			// "…tgt\n…src"(LF区切り2行)。色サンプル成功時に格納。
static bool16   sCmykCursorPending = kFalse;	// 直近の BeginTracking で CMYK カーソルを出すべきか

// LF(0x0A)で最大2行に分割する。
static void KESCMSplitTwoLines(const PMString& src, PMString& line1, PMString& line2)
{
	line1.Clear(); line1.SetTranslatable(kFalse);
	line2.Clear(); line2.SetTranslatable(kFalse);
	const int32 n = src.NumUTF16TextChars();
	const UTF16TextChar* buf = src.GrabUTF16Buffer(nil);
	bool16 second = kFalse;
	for (int32 i = 0; i < n; ++i)
	{
		if (buf[i] == 0x000A) { second = kTrue; continue; }
		if (!second) line1.AppendW(UTF32TextChar(buf[i]));
		else         line2.AppendW(UTF32TextChar(buf[i]));
	}
}

// CursorSpec のコールバック。カーソル描画系が呼ぶ(UIスレッド)。bitmapBuffer は呼び出し側が
// (最大カーソルサイズ)²×4 で確保済み。*width/*height は入力=最大サイズ(hiRes 時は 2 倍)、出力=実使用サイズ。
// カーソル文字列の1行(例 "Target\tC000 M000 Y000 K000")を、タブ(0x09)でラベルと数値に分割する。
// タブが無ければ全体を数値扱い(label 空)。ラベルと数値は別々の行に積んで描くので、2つの数値行は
// 同じ x から始まり自動的に桁が縦に揃う(カーソル最大幅の制約で1行に収まらないため。ユーザー報告 2026-07-13)。
static void KESCMSplitAtTab(const PMString& line, PMString& label, PMString& num)
{
	label.Clear(); label.SetTranslatable(kFalse);
	num.Clear();   num.SetTranslatable(kFalse);
	const int32 n = line.NumUTF16TextChars();
	const UTF16TextChar* buf = line.GrabUTF16Buffer(nil);
	bool16 afterTab = kFalse;
	for (int32 i = 0; i < n; ++i)
	{
		if (buf[i] == 0x0009) { afterTab = kTrue; continue; }	// タブ=ラベル/数値の区切り
		if (!afterTab) label.AppendW(UTF32TextChar(buf[i]));
		else           num.AppendW(UTF32TextChar(buf[i]));
	}
	if (!afterTab) { num = label; label.Clear(); }	// タブ無し=全体を数値列に
}

// スペース区切りの行を「表」状に描く。先頭4トークン(見出し C/M/Y/K、または3桁値)を x0+col*pitch の
// 固定列に、5トークン目以降(ラベル tgt/src)は4列目の右(x0+4*pitch)に置く。ヘッダー行とデータ行を同じ
// x0/pitch で描けば CMYK 見出しと数字の桁が必ず縦にそろう(フォント計測不要=ユーザー要望の縦位置合わせ
// 2026-07-13)。描画は KESCMShowHalo(白フチ＋黒本体)。
static void KESCMShowHalo(IGraphicsPort* gPort, IPMFont* font, const PMReal& size,
                          const PMReal& x, const PMReal& y, const PMString& s);	// 前方宣言

static void KESCMDrawColumns(IGraphicsPort* gPort, IPMFont* font, const PMReal& fs,
                             const PMReal& x0, const PMReal& pitch, const PMReal& y, const PMString& row)
{
	PMString tok; tok.SetTranslatable(kFalse);
	int32 col = 0;
	const int32 n = row.NumUTF16TextChars();
	const UTF16TextChar* b = row.GrabUTF16Buffer(nil);
	for (int32 i = 0; i <= n; ++i)
	{
		if (i < n && b[i] != 0x0020)	// スペース以外は現在のトークンに積む
		{
			tok.AppendW(UTF32TextChar(b[i]));
			continue;
		}
		if (tok.NumUTF16TextChars() > 0)	// 区切り(スペース or 行末)でトークン確定
		{
			const int32 c = (col < 4) ? col : 4;	// 5番目以降(ラベル)は4列目の右へ
			KESCMShowHalo(gPort, font, fs, x0 + pitch * PMReal(c), y, tok);
			++col;
			tok.Clear();
			tok.SetTranslatable(kFalse);
		}
	}
}

// (x,y) に文字列を描く(空なら何もしない)。
static void KESCMShowHalo(IGraphicsPort* gPort, IPMFont* font, const PMReal& size,
                          const PMReal& x, const PMReal& y, const PMString& s)
{
	const int32 n = s.NumUTF16TextChars();
	if (n <= 0 || font == nil)
		return;
	const UTF16TextChar* b = s.GrabUTF16Buffer(nil);
	gPort->selectfont(font, size);

	// 白フチ(8方向に1pxずらして白で描く)→ 黒本体。透明背景でも明暗どちらの下地でも読める。
	static const int kDX[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
	static const int kDY[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
	const PMReal o(1.0);
	gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));
	for (int i = 0; i < 8; ++i)
		gPort->show(x + PMReal(kDX[i]) * o, y + PMReal(kDY[i]) * o, (uint32)n, b);
	gPort->setrgbcolor(PMReal(0.0), PMReal(0.0), PMReal(0.0));
	gPort->show(x, y, (uint32)n, b);
}

static void KESCMCmykCursorBitmapProc(uchar* bitmapBuffer, uint32* width, uint32* height, bool16* hasAlpha, bool16 hiRes)
{
	const uint32 maxAllocW = *width;	// 呼び出し側が確保した正方バッファ(max×max)
	const uint32 maxAllocH = *height;
	const uint32 scale   = hiRes ? 2u : 1u;
	const uint32 maxLogW = maxAllocW / scale;
	const uint32 maxLogH = maxAllocH / scale;

	// 背景を透明にする(黒い箱を出さない=ユーザー指定 2026-07-13)。QueryGraphicsPortForBitmap は既存内容を
	// 消さないので、まず確保全域を ARGB=0 にクリアしてから描く(✓カーソルと同じ作法)。
	std::memset(bitmapBuffer, 0, (size_t)maxAllocW * (size_t)maxAllocH * 4u);

	// 表示文字列を先に分解し、最長行から「幅いっぱいに収まる大きめフォント」を決める(ユーザー要望
	// 2026-07-13: カーソル最大サイズまで使って cmyk＋数値を大きく)。ラベルは末尾の t/s。
	PMString line1, line2, lab1, num1, lab2, num2;
	KESCMSplitTwoLines(sCmykCursorText, line1, line2);
	KESCMSplitAtTab(line1, lab1, num1);
	KESCMSplitAtTab(line2, lab2, num2);
	int32 maxChars = num1.NumUTF16TextChars();
	if (num2.NumUTF16TextChars() > maxChars) maxChars = num2.NumUTF16TextChars();
	if (maxChars < 1) maxChars = 1;

	// フォントは使える最大幅から大きめに決める(1文字≒0.58em、下限7pt)。
	// ★上限キャップ撤廃(2026-07-14、検証用): 18→26→48pt と上げても実機で変化が無かったため、上限に
	// 到達する前に maxLogW(カーソル最大論理サイズ=OS/カーソルマネージャ依存で SDK からは不明)からの
	// 逆算値自体が頭打ちになっている疑いが強い。上限を外して、この式が実際にどこまで出すかを見る。
	int32 fs = ((int32)maxLogW - 8) * 100 / (maxChars * 58);
	if (fs < 7)  fs = 7;

	// ★ビットマップ幅は「実際の内容幅」にタイトに合わせる。最大幅いっぱいに取ると右側に広い透明余白が
	// でき、その初回フレームがちらついて見える(ゴミ)ため。内容幅 = 左6 + 4列×ピッチ(2.1em) +
	// ラベル(tgt≒1.74em) + 右4 ≒ 10 + 10.14em(下の描画の pitch=2.1×fs と一致させること)。
	int32 contentW = 10 + (fs * 1014) / 100;
	uint32 logW = (contentW > 0) ? (uint32)contentW : maxLogW;
	if (logW > maxLogW) logW = maxLogW;

	// ✓(上部 y≈18 まで)の下に「ヘッダー C M Y K + データ2行(Target/Source)」を積む。位置・高さは fs から。
	const int32 gap    = (fs * 130) / 100;	// 行間 ≒1.3em
	const int32 yHdr   = 22 + fs;			// ヘッダー行ベースライン(✓の下。全体を少し下げた=ユーザー要望 2026-07-13)
	const int32 yData1 = yHdr + gap;		// Target 行
	const int32 yData2 = yData1 + gap;		// Source 行
	// 最下段(Source 行 "src")はディセンダ(下に伸びる字)が無いので、ベースラインのすぐ下でビットマップを
	// 終える。下端の透明余白を残すと、そこに初回フレームのちらつき(ゴミ)が出る(ユーザー報告: 文字より
	// 約3px下に一瞬。2026-07-13)。ハロー(y+1)とAA ぶんだけ +2 で足りる。
	int32 needH = yData2 + 2;
	uint32 logH = (needH > 0) ? (uint32)needH : 60u;
	if (logH > maxLogH) logH = maxLogH;

	const uint32 actW = logW * scale;
	const uint32 actH = logH * scale;
	*width    = actW;
	*height   = actH;
	*hasAlpha = kTrue;

	InterfacePtr<IGraphicsPort> gPort(Utils<ICursorUtils>()->QueryGraphicsPortForBitmap(
		bitmapBuffer, actW, actH, kTrue /*hasAlpha*/, hiRes));
	if (gPort == nil)
		return;

	// 背景は透明(上で全域 ARGB=0 にクリア済み)。setopacity は以降のストローク/文字を不透明にするため。
	gPort->setopacity(PMReal(1.0), kFalse);
	/* 背景塗りは廃止=透明のまま。黒い箱を出さない(ユーザー指定 2026-07-13) */

	// ツール選択中と同じ✓(KESCMCheckCursorBitmapProc と同一形状/座標)をホットスポット(10,18)=✓の
	// 折れ点=クリック点に描く。数値表示中もカーソル形状を残す(ユーザー要望 2026-07-14)。
	// ★以前は「✓ を stroke で描くと初回フレームのちらつき(ゴミ)が出る」と考えて rectfill のドットに
	// 退避していたが(2026-07-13)、その後の調査でゴミの真因は stroke 描画ではなく BeginTracking の
	// 多段カーソル切替が OS のハードウェアカーソル合成にそのまま見えていたことだと判明し、
	// KESCMTracker.cpp 側で ICursorMgr::Hide/Show により解決済み(2026-07-14)。stroke 自体は無罪なので
	// ✓に戻して問題ない。
	const PMReal ax(5.0), ay(12.0);	// 短腕の先(左上)
	const PMReal bx(10.0), by(18.0);	// 頂点(折れ点)=ホットスポットと一致
	const PMReal cx(20.0), cy(5.0);	// 長腕の先(右上)
	gPort->setlinecap(1);	// round cap
	gPort->setlinejoin(1);	// round join
	gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));	// 白フチ
	gPort->setlinewidth(PMReal(3.5));
	gPort->newpath();
	gPort->moveto(ax, ay);
	gPort->lineto(bx, by);
	gPort->lineto(cx, cy);
	gPort->stroke();
	gPort->setrgbcolor(PMReal(0.0), PMReal(0.0), PMReal(0.0));	// 黒本体
	gPort->setlinewidth(PMReal(2.4));
	gPort->newpath();
	gPort->moveto(ax, ay);
	gPort->lineto(bx, by);
	gPort->lineto(cx, cy);
	gPort->stroke();

	// 上から: ヘッダー "C M Y K"(各列先頭にそろえる) / Target 数値 / Source 数値。数値は各値3桁で行頭
	// そろえ、末尾に t/s。フォント fs・行位置は上で計算済み。描画は KESCMShowHalo(白フチ＋黒本体)。
	InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), UseDefaultIID());
	InterfacePtr<IPMFont> font(fontMgr != nil ? fontMgr->QueryFont(fontMgr->GetDefaultFontName()) : nil);
	if (font != nil)
	{
		// 見出し行とデータ2行を同じ固定列(x0, pitch)で描いて桁を縦にそろえる(pitch=3桁+ギャップ)。
		// ヘッダーの C/M/Y/K が各3桁列の真上に来る(ユーザー要望の縦位置合わせ 2026-07-13)。
		const PMReal x0(6.0);
		const PMReal pitch = PMReal(fs) * PMReal(2.1);
		PMString hdr; hdr.SetTranslatable(kFalse); hdr.Append("C M Y K");
		KESCMDrawColumns(gPort, font, PMReal(fs), x0, pitch, PMReal(yHdr),   hdr);	// 見出し C M Y K
		KESCMDrawColumns(gPort, font, PMReal(fs), x0, pitch, PMReal(yData1), num1);	// Target
		KESCMDrawColumns(gPort, font, PMReal(fs), x0, pitch, PMReal(yData2), num2);	// Source
	}
}

// KESCMTracker.cpp から使う入口。BeginTracking の CMYK 分岐が成功したら Pending が立ち、トラッカーが
// ChangeModalCursor(CursorSpec(KESCMTrackerCmykCursorProc(), …)) を呼ぶ。
bool16 KESCMTrackerHasPendingCmykCursor()          { return sCmykCursorPending; }
CreateCursorBitmapProc KESCMTrackerCmykCursorProc() { return &KESCMCmykCursorBitmapProc; }

// KESCMTrackerUpdateCmykDrag(KESCMPeek.h 参照) — ドラッグ中の CMYK ライブ更新。
// トラッカーの ContinueTracking(マウス移動)から呼ばれる。現在のマウス位置で CMYK を再サンプルし、
// 値が変わったら sCmykCursorText を更新して kTrue を返す(呼び出し側がカーソルを描き直す)。
// 連続ラスタ化で重くならないよう時間スロットル(既定 50ms ≒ 20回/秒)を掛ける。
// 前方宣言。定義は KESCMTrackerRevealBegin の直前(ページ外の「値なし c---」表示を作る)。
static void KESCMBuildCmykNoValue(PMString& out);				// カーソル用(t/s)
static void KESCMBuildCmykNoValuePanel(PMString& out);			// パネル用(見出し行+Target/Source)

bool16 KESCMTrackerUpdateCmykDrag()
{
	if (!sCmykCursorPending)	// Alt+左 CMYK モードでなければ何もしない
		return kFalse;
	if (!sPeekArmed || sPeekTargetDB == nil || sPeekSourceDB == nil)
		return kFalse;

	// スロットル(50ms)。steady_clock は単調増加なのでラップの心配なし。初回は必ず通す。
	static std::chrono::steady_clock::time_point sLast;
	static bool16 sStarted = kFalse;
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	if (sStarted)
	{
		const long long ms =
			std::chrono::duration_cast<std::chrono::milliseconds>(now - sLast).count();
		if (ms < 50)
			return kFalse;
	}
	sStarted = kTrue;
	sLast = now;

	// 現在のマウス位置で新/旧をサンプリング(KESCMSampleCmykUnderMouse は毎回マウス位置を読み直す)。
	// ページ外・取得失敗なら「値なし(c--- …)」表示にして、拾えていないことが分かるようにする
	// (ユーザー要望 2026-07-13。直前値を残さない=誤読防止)。
	PMString panelMsg, cursorMsg;
	if (!KESCMSampleCmykUnderMouse(sPeekTargetDB, sPeekSourceDB, panelMsg, cursorMsg))
	{
		KESCMBuildCmykNoValue(cursorMsg);
		KESCMBuildCmykNoValuePanel(panelMsg);
	}
	if (cursorMsg == sCmykCursorText)	// 値が同じなら描き直し不要(パネルも同じ値なので更新不要)
		return kFalse;

	// パネルのステータス行もドラッグに追従させる(強制表示はしない。KESCMTrackerRevealBegin と同じ方針)。
	KESCMSetStatus(panelMsg);
	sCmykCursorText = cursorMsg;
	return kTrue;
}

// ページ外など CMYK を拾えないときに出す「値なし」表示("c--- m--- y--- k--- t/s")。ダッシュで
// 「ここでは色を拾えていない」ことが分かるようにする(ユーザー要望 2026-07-13)。ラベルは通常と同じ t/s。
static void KESCMBuildCmykNoValue(PMString& out)
{
	out.Clear();
	out.SetTranslatable(kFalse);
	out.Append("--- --- --- --- t");	// ラベルは t/s(KESCMColorSampler.cpp と同じ短縮。2026-07-14)
	out.AppendW(UTF32TextChar(0x0A));	// 改行 → 2行目へ
	out.Append("--- --- --- --- s");
}

// パネル版の「値なし」表示。値ごとに見出し文字を添え tgt/src 略語にする。KESCMSampleCmykUnderMouse
// 成功時のパネル表記(KESCMColorSampler.cpp の KESCMAppendCmykLabeled)と揃える(2026-07-14)。
static void KESCMBuildCmykNoValuePanel(PMString& out)
{
	out.Clear();
	out.SetTranslatable(kFalse);
	out.Append("C--- M--- Y--- K--- tgt");
	out.AppendW(UTF32TextChar(0x0A));
	out.Append("C--- M--- Y--- K--- src");
}

void KESCMTrackerRevealBegin(bool16 shiftDown, bool16 altDown, bool16 cmdDown)
{
	sCmykCursorPending = kFalse;	// このプレスで CMYK カーソルを出すかは下の Alt 分岐で決める(既定=出さない)


	// Ctrl(cmd)を伴う左ボタンは未対応(再比較/パネル/CMYK は中ボタン専用)。素のミドルを邪魔しないのと
	// 同様、ここでは何もしない(トラッカーはキャプチャ済みだが描画状態は変えない)。
	if (cmdDown)
		return;

	// ---- 「Hold to Hide Marks」モード(常時表示の極性反転)の窓別 temp-hide ----
	// 中ボタン WatchEvent kMButtonDn 冒頭の tempHideGesture と同一。隠すジェスチャ=修飾なし or Shift
	// (Shift+Alt も Shift を含む)。cmd は上で除外済み。★Alt 単独(CMYK)は隠さない=枠を出したままサンプリング
	// (中ボタン Shift+Ctrl+Alt でも枠は隠れない仕様に一致)。押した窓の枠だけを隠す(Target/Source 別)。
	const bool16 tempHideGesture = (!shiftDown && !altDown) || shiftDown;
	if (KESCMDrawEventHandler::sAlwaysShowMarks && tempHideGesture)
	{
		if (!KESCMDrawEventHandler::sMarksTempHidden && KESCMFrontViewIsOverTarget())
		{
			KESCMDrawEventHandler::sMarksTempHidden = kTrue;
			KESCMInvalidateMarksDoc();	// Target(sDB)を再描画
		}
		if (KESCMDrawEventHandler::sSrcMarksOn && !KESCMDrawEventHandler::sSrcMarksTempHidden &&
		    KESCMFrontViewIsOverSource())
		{
			KESCMDrawEventHandler::sSrcMarksTempHidden = kTrue;
			KESCMInvalidateDB(KESCMDrawEventHandler::sSrcDB);	// Source(sSrcDB)を再描画
		}
	}

	// ---- ジェスチャ分岐 ----
	if (altDown && !shiftDown)
	{
		// Alt+左(単独、Shift/Ctrl なし): クリック点の CMYK 生値(0..255)を新・旧でサンプリングし、
		// "Target C.. M.. Y.. K.." / "Source C.. …" をカーソル自身に描画する。
		// 中ボタン Shift+Ctrl+Alt+ミドル 相当。arm 済み(Start 後)かつ Target 窓上でのみ反応。
		if (sPeekArmed && KESCMFrontViewIsOverTarget())
		{
			PMString panelMsg, cursorMsg;
			if (!KESCMSampleCmykUnderMouse(sPeekTargetDB, sPeekSourceDB, panelMsg, cursorMsg))
			{
				KESCMBuildCmykNoValue(cursorMsg);	/* ページ外など: 拾えないことを示す(値なし c--- 表示) */
				KESCMBuildCmykNoValuePanel(panelMsg);
			}
			{
				// 色比較はカーソル自身に CMYK を描く(トラッカーが ChangeModalCursor する)のに加えて、
				// 念のためパネルのステータス行にも同じ値を出す(ユーザー要望 2026-07-14)。
				// ★KESCMSetStatus はパネルが非表示でも「強制的に表示」はしない(ON→表示中なら見える、
				// OFF→隠れたまま状態だけ覚える)。パネルを強制的に開かせることはしない
				// (ユーザー指定: OFFのままでよい、ONにはしない)。
				KESCMSetStatus(panelMsg);
				sCmykCursorText    = cursorMsg;
				sCmykCursorPending = kTrue;
			}
		}
		return;
	}
	if (shiftDown && altDown)
	{
		// Shift+Alt+左: 旧版べた載せ peek を 50% で(中ボタン Shift+Alt+ミドル相当)。
		KESCMTrackerBeginPeek(kKESCMPeekSemiOpacity);
		return;
	}
	if (shiftDown)
	{
		// Shift+左: 旧版べた載せ peek を 100% 不透明で(中ボタン Shift+ミドル相当)。
		KESCMTrackerBeginPeek(PMReal(1.0));
		return;
	}

	// ---- 修飾なし: 通常モードのマーク一時表示(reveal) ----
	// Hold to Hide モード中は上で temp-hide 済み=ここでは何もしない(reveal はしない)。
	if (KESCMDrawEventHandler::sAlwaysShowMarks)
		return;

	// 「マークがある」の判定は WatchEvent の修飾なし分岐と同一(anyMarkableContent 相当)。
	// overflow 集合は現在の (sDB,sSrcDB) 用へ合わせてから読む。
	KESCMDrawEventHandler::EnsureOverflowCache();
	const bool16 haveContent =
		!KESCMDrawEventHandler::sEntries.empty() ||
		!KESCMDrawEventHandler::sOverflowT.empty() ||
		!KESCMDrawEventHandler::sOverflowS.empty() ||
		(KESCMDrawEventHandler::sDB    != nil && KESCMPageMapHasAnyRegistered(KESCMDrawEventHandler::sDB)) ||
		(KESCMDrawEventHandler::sSrcDB != nil && KESCMPageMapHasAnyRegistered(KESCMDrawEventHandler::sSrcDB));
	if (!haveContent)
		return;

	// 通常モード(マーク非表示→押下中だけ表示)。Target 窓の上でだけ reveal する(Source や無関係な窓では
	// 出さない。中ボタンと同じ方針)。
	if (!KESCMFrontViewIsOverTarget())
		return;

	sSingleShowing = kTrue;
	KESCMDrawEventHandler::sMarkScreenOpacity = KESCMDrawEventHandler::SelectedMarkOpacity();	// パネルの 25%/75%
	KESCMDrawEventHandler::sMarksVisible = kTrue;	// 押下中だけ枠等を表示
	KESCMInvalidateMarksDoc();
}

void KESCMTrackerRevealEnd()
{
	// 「Hold to Hide Marks」で押下中に隠していた常時表示の枠を戻す(離すと再表示)。押した窓に応じて
	// Target/Source どちらか(または両方)が立っている。モード OFF なら両方 kFalse なので無影響
	// (WatchEvent kMButtonUp の temp-hide 復元と同一)。
	if (KESCMDrawEventHandler::sMarksTempHidden)
	{
		KESCMDrawEventHandler::sMarksTempHidden = kFalse;
		KESCMInvalidateMarksDoc();	// Target(sDB)を再描画
	}
	if (KESCMDrawEventHandler::sSrcMarksTempHidden)
	{
		KESCMDrawEventHandler::sSrcMarksTempHidden = kFalse;
		KESCMInvalidateDB(KESCMDrawEventHandler::sSrcDB);	// Source(sSrcDB)を再描画
	}

	if (sPeekActive)
	{
		// Shift／Shift+Alt+左を離した → 旧版べた載せを隠す(マークは触らない)。キャッシュは保持
		// (再 peek は即時)。中ボタン kMButtonUp の sPeekActive 復元と同一。
		sPeekActive = kFalse;
		if (KESCMDrawEventHandler::sShowOriginal)
		{
			KESCMDrawEventHandler::sShowOriginal = kFalse;
			KESCMInvalidateDB(sPeekTargetDB);
		}
	}
	else if (sSingleShowing)
	{
		// 通常モードの reveal 解除 → 枠表示を解除し、不透明度を基準値へ戻す＋非表示へ(WatchEvent の
		// sSingleShowing 復元と同じ)。
		sSingleShowing = kFalse;
		KESCMDrawEventHandler::sMarksVisible = kFalse;
		KESCMDrawEventHandler::sMarkScreenOpacity = KESCMBaseScreenOpacity();
		KESCMInvalidateMarksDoc();
	}
}

//========================================================================================
// KESCMPeekStartup
//   アプリ起動/終了サービス。中ボタンウォッチャは撤去した(2026-07-13)ので起動時の処理は無く、
//   終了時に保持リソース(遅延サムネイル idle task・マーク/旧版画像バッファ・peek arm 状態・
//   レイアウト同期フラグ)を片付けるためだけに残している。
//========================================================================================
class KESCMPeekStartup : public CPMUnknown<IStartupShutdownService>
{
public:
	KESCMPeekStartup(IPMUnknown* boss) : CPMUnknown<IStartupShutdownService>(boss) {}
	~KESCMPeekStartup() {}

	virtual void Startup();
	virtual void Shutdown();
};

CREATE_PMINTERFACE(KESCMPeekStartup, kKESCMPeekStartupImpl)

void KESCMPeekStartup::Startup()
{
	// 中ボタンウォッチャ撤去により起動時の処理は無し(このサービスは終了時の後片付けのために残す)。
}

void KESCMPeekStartup::Shutdown()
{
	// 遅延サムネイル更新の idle task を解放(予約中なら RemoveTask してから)。
	KESCMShutdownThumbIdleTask();
	// 保持していたマーク/旧版画像バッファを解放(終了時もきれいに片付ける)。
	KESCMDrawEventHandler::DropAll();
	KESCMDrawEventHandler::DropAllOrig();
	// ★peek の arm 状態もここで落とす。残したままだと、終了処理後に kAfterCloseDoc responder が
	// 発火した場合、KESCMHandleDocsClosed が stale な sPeek* から comparisonDocClosed=true を
	// 再計算し得る(通常の終了順=文書クローズ→Shutdown では起きないはずだが防御的にリセット。
	// ポインタは nil 代入のみ=deref しない)。
	sPeekArmed = kFalse;
	sPeekTargetDB = nil;
	sPeekSourceDB = nil;

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
	KESCMDrawEventHandler::sMarksTempHidden = kFalse;	// Hold to Hide Marks の一時退避も初期化(押下中フラグの取りこぼし対策)
	KESCMDrawEventHandler::sSrcMarksTempHidden = kFalse;	// Source 側の一時退避も初期化
	KESCMDrawEventHandler::sMarksVisible = kFalse;	// 既定(非表示)へ。arm 中も枠は押下中だけ表示
}

void KESCMDoDisarmMousePeek(IDataBase* db)
{
	// nil化する前に、実際に arm されていた対象文書を控えておく。呼び出し側の db(=操作時のアクティブ
	// 文書)が前面で Source や無関係な第3文書に切り替わっていても、対象文書の枠が即座に消えるように
	// するため(タイル表示等で対象文書が同時に見えている場合に効く)。
	IDataBase* armedTargetDB = sPeekTargetDB;

	sPeekArmed = kFalse;
	sPeekTargetDB = nil;
	sPeekSourceDB = nil;
	sPeekActive = kFalse;
	sSingleShowing = kFalse;
	KESCMDrawEventHandler::sMarksTempHidden = kFalse;	// Hold to Hide Marks の一時退避を解除
	KESCMDrawEventHandler::sSrcMarksTempHidden = kFalse;	// Source 側の一時退避も解除
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

	// 比較に関わる db(マーク sDB / 旧版 sOrigDB / Source側枠 sSrcDB / peek arm の target・source)の
	// いずれかが閉じたか(sSrcDB は実質 sPeekSourceDB と同じ文書だが、arm 状態に依存しない保険として見る)。
	const bool16 comparisonDocClosed =
		(KESCMDrawEventHandler::sDB     != nil && docList->FindDocByDataBase(KESCMDrawEventHandler::sDB)     == nil) ||
		(KESCMDrawEventHandler::sOrigDB != nil && docList->FindDocByDataBase(KESCMDrawEventHandler::sOrigDB) == nil) ||
		(KESCMDrawEventHandler::sSrcDB  != nil && docList->FindDocByDataBase(KESCMDrawEventHandler::sSrcDB)  == nil) ||
		(sPeekArmed &&
		 ((sPeekTargetDB != nil && docList->FindDocByDataBase(sPeekTargetDB) == nil) ||
		  (sPeekSourceDB != nil && docList->FindDocByDataBase(sPeekSourceDB) == nil)));

	if (comparisonDocClosed)
	{
		// DropAll/DropAllOrig で nil にする前に、まだ開いている側の db を控えておく(生存確認済みなので
		// 後で安全に InvalidateViews できる)。閉じた方の db は決して拾わない。
		IDataBase* survivorTargetDB = nil;
		IDataBase* survivorOrigDB   = nil;
		IDataBase* survivorSrcDB    = nil;	// Source側枠(Show Marks on Source)が出ている文書
		if (KESCMDrawEventHandler::sDB != nil && docList->FindDocByDataBase(KESCMDrawEventHandler::sDB) != nil)
			survivorTargetDB = KESCMDrawEventHandler::sDB;
		if (KESCMDrawEventHandler::sOrigDB != nil && docList->FindDocByDataBase(KESCMDrawEventHandler::sOrigDB) != nil)
			survivorOrigDB = KESCMDrawEventHandler::sOrigDB;
		if (KESCMDrawEventHandler::sSrcDB != nil && docList->FindDocByDataBase(KESCMDrawEventHandler::sSrcDB) != nil)
			survivorSrcDB = KESCMDrawEventHandler::sSrcDB;
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
		sPeekArmed     = kFalse;
		sPeekTargetDB  = nil;
		sPeekSourceDB  = nil;
		sPeekActive    = kFalse;
		sSingleShowing = kFalse;
		KESCMDrawEventHandler::sMarksTempHidden = kFalse;	// Hold to Hide Marks の一時退避も解除
		KESCMDrawEventHandler::sSrcMarksTempHidden = kFalse;	// Source 側の一時退避も解除
		KESCMDrawEventHandler::sMarksVisible = kFalse;
		// ★2026-07-11(ユーザー報告): Stop ボタンは登録(Add/Remove)を全解除するのに、比較文書(Source等)を
		//   閉じて比較が終わった時は登録が残っていた。ここは「Stop 相当のフルクリーンアップ」なので、
		//   Stop(KESCMDoClearMarks)と同じく登録も丸ごと忘れる。これを怠ると、生存側 Target/Source に
		//   古い登録が残り、次の Start でペアリングに紛れ込む(map 空にするだけ=deref なし)。
		KESCMPageMapClearAllDocs();
		KESCMPageCheckClearAllDocs();	// 「KESCM: Check」の✓も同様に全消去(Start 中限定)
		// ★スクロールバー地図 strip も Stop と同様に取り外す(2026-07-11 セルフレビューで発見)。
		//   これを怠ると、生存側の窓に孤児 strip が残り、レイアウトビューも 5px 詰めたままになる。
		//   DetachAll は「今開いている窓」だけを走査する(閉じた窓の widget は窓ごと消えている)ので安全。
		KESCMScrollMapDetachAll();
		changed = kTrue;

		PMString s("marks cleared");	// Stop ボタン(DoClear)と同じメッセージ
		s.SetTranslatable(kFalse);
		KESCMSetStatus(s);

		// Stop ボタン(KESCMDoClearMarks)と同じく、生存している側を再描画して枠を即座に消す
		// (Source 側の常時枠が出ていた文書も含む)。
		KESCMInvalidateDB(survivorTargetDB);
		if (survivorOrigDB != survivorTargetDB)
			KESCMInvalidateDB(survivorOrigDB);
		if (survivorSrcDB != survivorTargetDB && survivorSrcDB != survivorOrigDB)
			KESCMInvalidateDB(survivorSrcDB);

		// 生存側の Pages パネルサムネイルからも枠/斜線を消す。★その場(同期)で呼ぶと、閉じたのが
		// ターゲットで生存側がこれからアクティブ化する場合、パネルの前面切替の過渡で ForceRedraw が
		// 再生成を起こしきれず枠が残る(2026-07-08 実機で確認)。そこで「次の idle」に遅延させ、切替が
		// 落ち着いてから purge＋ForceRedraw する(KESCMScheduleThumbRefresh)。DropAll 済みなので
		// 再生成される isThumb 描画は早期 return し枠は描かれない。survivor* は生存確認済みポインタ
		// のみ=閉じた db は決して渡さない(nil はスケジューラ側で弾く。重複 db も集約される)。
		KESCMScheduleThumbRefresh(survivorTargetDB);
		if (survivorOrigDB != survivorTargetDB)
			KESCMScheduleThumbRefresh(survivorOrigDB);
		if (survivorSrcDB != survivorTargetDB && survivorSrcDB != survivorOrigDB)
			KESCMScheduleThumbRefresh(survivorSrcDB);
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

	// 「比較相手なしページ」登録(KESCMPageMap)の後片付け: 閉じた文書の分を状態だけ捨てる
	// (deref なし。パネル表示には関与しないので changed は立てない)。
	KESCMPageMapSweepClosedDocs();
	KESCMPageCheckSweepClosedDocs();	// 「KESCM: Check」の✓も、閉じた文書の分を状態だけ捨てる(deref なし)

	// 何か片付けたらパネルの ON/OFF 表示を実状態に合わせる(①「ON 固着」の解消)。
	if (changed)
		KESCMRefreshPanel();
}
