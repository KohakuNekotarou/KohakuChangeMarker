//========================================================================================
//
//  KESCMPeek.cpp
//
//  ツール(左ボタン)peek の実装(KESCMScriptProvider.cpp から分離)。peek 状態、トラッカー(KESCMTracker.cpp)
//  から呼ばれるジェスチャ入口(KESCMTrackerRevealBegin/End ほか)、起動/終了サービス、KESCMCore.h で宣言した
//  arm/disarm/状態アクセサの入口を持つ。旧・中ボタンの IEventWatcher は撤去済み(2026-07-13)。
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
#include "ISession.h"
#include "IWindow.h"
#include "IWindowUtils.h"
#include "IDocumentPresentation.h"
#include "ILayoutViewUtils.h"		// GetAllLayoutViews(Split Window両ペインのIControlView*取得)
#include "ILayoutUIUtils.h"			// MakeZoomCmd(kZoomToCmdBoss。ビューポート同期のズーム)
#include "CmdUtils.h"				// ProcessCommand(ズームコマンド実行)
#include "ICommand.h"

// レイアウトビュー同期(Sync Layout Views)用:
#include "CObserver.h"				// 同期オブザーバの基底(手本=work/KESLayoutScrollObserver.cpp)
#include "ISubject.h"				// AttachObserver/DetachObserver/IsAttached
#include "IActiveContext.h"			// IID_IACTIVECONTEXT / ContextInfo(文書切替の検知)
#include "widgetid.h"				// IID_IPANORAMA / kScrollToMessage・kScrollByMessage・kScaleToMessage・kScaleByMessage

// 一括クローズ(複数文書を続けて閉じる)の集約用:
#include "IBoolData.h"				// セッション上の IID_IKFILESCLOSING(「今どれかの文書が閉じている最中か」)
#include "LinksUIID.h"				// ★公開ヘッダー: IID_IKFILESCLOSING / kPendingDocumentsClosedMsg(本体 Links UI が提供)

// ツール / 起動:
#include "IStartupShutdownService.h"
#include "CPMUnknown.h"
#include "LayoutUIID.h"
#include "DocumentContextID.h"

// ジオメトリ / ビュー:
#include "IControlView.h"
#include "IPanorama.h"
#include "PMMatrix.h"
#include "PMPoint.h"
#include "PMRect.h"					// ページのペーストボード矩形(旧 Alt+ミドルの追加/削除補正)
#include "PMReal.h"
#include "TransformUtils.h"

// カスタムビットマップカーソル(Alt+左 CMYK 情報をカーソルにも描く):
// (ICursorUtils.h は KESCMCheckGlyph.h 経由で入る=直接シンボルを使わないため直 include は撤去 2026-07-25)
#include "IGraphicsPort.h"			// setrgbcolor/rectfill/selectfont/show
#include "IFontMgr.h"				// 既定フォント取得
#include "IPMFont.h"

#include <map>
#include <vector>
#include <cstring>				// std::memset(カーソルバッファの透明クリア)
#include <chrono>				// steady_clock(ドラッグ中ライブ再サンプルのスロットル)

// プロジェクト内インクルード:
#include "KESCMID.h"
#include "KESCMConstants.h"
#include "KESCMDrawEventHandler.h"   // エンジンの共有 static ＋ KESCMQueryPanorama
#include "KESCMColorSampler.h"       // KESCMSampleCmykUnderMouse
#include "KESCMCheckGlyph.h"         // KESCMDrawCheckGlyph(✓描画を CMYK カーソルと共有)
#include "KESCMCore.h"               // arm/disarm/状態 宣言
#include "KESCMPageMap.h"            // KESCMBuildPairing(同期の除外対応表キャッシュ)/KESCMPageMapHasAnyRegistered/KESCMPageMapSweepClosedDocs
#include "KESCMPageCheck.h"          // KESCMPageCheckClearAllDocs / KESCMPageCheckSweepClosedDocs(✓の後片付け)
#include "KESCMThumbnailRefresh.h"   // クローズ後、生存側の Pages パネルサムネイルから枠を消す
#include "KESCMScrollMap.h"          // スプレッド再比較後にスクロールバー地図を最新化
#include "KESCMChangeNav.h"          // KESCMRefreshNavPosition(スプレッド再比較後に Prev/Next 位置を最新化)
#include "KESCMThumbIdleTask.h"      // クローズ後の再生成を次のidleに遅延(前面切替の過渡を避ける)
#include "KESCMPanelState.h"         // KESCMLoadPanelStateIfPresent(起動時に保存済みパネル設定を復元)
#include "KESCMPeek.h"

//========================================================================================
// ツール(左ボタン)peek — 共有状態とヘルパ。
//   ツール左ボタンを押している間だけ、マウス下スプレッドの旧版を不透明べた載せし、離すと隠す。
//   比較相手の旧ドキュメントは先に KESCMDoArmMousePeek(KESCMCore.h)で登録しておく(パネルの Start
//   ボタンが呼ぶ)。トラッカー入口(KESCMTrackerRevealBegin/End)がこの arm 状態を見る。全部を1つの
//   翻訳単位に置くことで、入口が MakeOrigImage / マウス下スプレッド判定 / sOrigImages を直接再利用できる。
//========================================================================================
static IDataBase* sPeekTargetDB = nil;	// 表示中(新)ドキュメント。使用前に「まだ開いているか」を検証する。
static IDataBase* sPeekSourceDB = nil;	// peek 中に重ねる旧ドキュメント。
static bool16     sPeekArmed    = kFalse;

// Alt+左(CMYK 色ピック)の押下中モード。押下時に「マウス下の文書」で決めて固定し、RevealEnd で捨てる
// (押下の外では常に nil/既定)。★2026-07-26 にユーザー指定で3通りへ拡張(旧 sSoloCmykDB 1本を置換):
//   Start 中・Target 窓 … hover=Target / other=Source(比較2行、1行目 "t")
//   Start 中・Source 窓 … hover=Source / other=Target(比較2行、1行目 "s")
//   Start 中・第3の文書 / Stop 中 … hover=その文書 / other=nil(単独1行)
// 押下中に別の窓へドラッグしても基準は切り替えない(行の上下が入れ替わらないように。外れている間は
// サンプラが窓の同一性ガードで弾き「値なし ---」になる)。ポインタは照合専用で deref しない。
static IDataBase* sCmykHoverDB       = nil;		// マウスが乗っている側=1行目に出す文書
static IDataBase* sCmykOtherDB       = nil;		// 比較相手(nil=単独モード)
static bool16     sCmykHoverIsTarget = kFalse;	// hover が Target(新)側か=ページ対応の向きとラベル t/s

// Shift+左=旧版を不透明(100%)で / Shift+Alt+左=旧版を 50% で重ねて peek。
// 押下中だけ表示し、ツール左ボタンを離すと消す(修飾キーは離してもよい)。判定はツール左ボタン押下時に1回見るだけ。
static const PMReal kKESCMPeekSemiOpacity = 0.5;	// Shift+Alt+左時の旧版の不透明度(0..1)
static bool16 sPeekActive        = kFalse;	// Shift/Shift+Alt+左を押し込み中(=覗き表示中)か
static bool16 sSingleShowing     = kFalse;	// 修飾なしのツール左hold中(=全マークを選択不透明度25%/75%で一時表示中)か。離すと隠す＋基準opacityへ

// 画面マークの「基準」不透明度(=ツール左ボタンを押していない常時表示時の値)。
//   印刷マークON中はパネルで選択中の不透明度(25%/75%。画面と印刷の見た目を一致)、印刷OFFは 1.0。
//   ツール左ボタンを離したら sMarkScreenOpacity をこの値へ戻す。
PMReal KESCMBaseScreenOpacity()
{
	// 印刷マーク ON、または「Hold to Hide Marks」(枠を画面に常時表示)ON のときは、常時表示の枠を
	// パネル選択の 25%/75% で描く(押下中の一時表示と見た目を揃える)。どちらも OFF なら 1.0(不透明)。
	return (KESCMDrawEventHandler::sPrintMarks || KESCMDrawEventHandler::sAlwaysShowMarks)
	       ? KESCMDrawEventHandler::SelectedMarkOpacity() : PMReal(1.0);
}

// 前面レイアウトビューで「マウス下スプレッド」の旧版べた載せを表示する。
//   targetDB=表示中(新)ドキュメント, sourceDB=重ねる旧ドキュメント。
//   そのスプレッドが既にキャッシュ済みなら再利用(即時)。未キャッシュなら旧キャッシュを捨てて、その
//   スプレッドだけをその場でラスタ化(保持は常に1スプレッド)。成功時に sShowOriginal を立てて再描画。
//   (2026-07-25 監査: 旧・中ボタン watcher/スクリプト報告用の戻り値 KESCMPeekResult と
//    outSpread/outPages は、唯一の呼び出し側が全て捨てていたため撤去して void 化)
static void KESCMPeekShowUnderMouse(IDataBase* targetDB, IDataBase* sourceDB)
{
	if (targetDB == nil || sourceDB == nil)
		return;

	// マウスが乗っているレイアウトビュー(Split Window対応、KESCMQueryViewUnderMouse参照)。
	InterfacePtr<IControlView> view(KESCMQueryViewUnderMouse());
	if (view == nil)
		return;

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
		return;

	// マウス下のスプレッド/ページを特定(平坦通し番号も取得)。共有ヘルパ KESCMFindPageUnderMouse に集約。
	KESCMPageHit hit;
	if (!KESCMFindPageUnderMouse(targetDB, mx, my, hit))
		return;

	const int32 np          = hit.numPages;
	InterfacePtr<ISpread> spread(targetDB, hit.spreadUID, UseDefaultIID());
	if (spread == nil)
		return;

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
			return;
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

	if (!cached)
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
			KESCMDrawEventHandler::MakeOrigImage(tRef, sRef, peekDpi);	// 失敗ページは重ねずスキップ(従来同挙動)
		}
	}
	KESCMDrawEventHandler::sShowOriginal = kTrue;

	KESCMInvalidateDB(targetDB);
}


// ページ比較の部分更新(共通コア): targetPages(= targetDB 上のページUID列)を再比較して枠(リング)を
// 更新する。source 対応は除外対応表(登録済みページを除いた順番対応)で引く。
//   ・各ページを MakeEntry で取り直し(編集後の差分に更新)。変化が無くなったページは古い枠を消す。
//   ・旧版画像キャッシュ(sOrigImages)は古いので破棄(次の peek で作り直し)。
//   ・✓ の剪定/レイアウト・スクロールバー地図・Pages パネルサムネイルの更新まで行う。
//   実際に再比較したページ数(対応表に無い登録済みページ等の skip を除く)を outProcessed に、
//   うち変化ページ数を outChanged に返す。戻り=1ページ以上処理したか。
//   ★旧 Ctrl+ミドル(マウス下スプレッド再比較)の中核をページ指定へ一般化したもの(2026-07-13 移設)。
static bool16 KESCMRefreshComparisonCore(IDataBase* targetDB, IDataBase* sourceDB,
                                         const std::vector<UID>& targetPages,
                                         int32* outProcessed, int32* outChanged)
{
	if (outProcessed) *outProcessed = 0;
	if (outChanged)   *outChanged = 0;
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
	// 報告用の処理数=実際に MakeEntry/DropOneEntry まで到達したページ数(上の continue で skip した
	// 選択ページは数えない。ステータス行の「refreshed N」が実態と一致するように。2026-07-15)。
	if (outProcessed) *outProcessed = (int32)touchedTargetPages.size();
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
	// (2文書とも Purge のみ→最後に1回だけ ForceRedraw。2026-07-25 監査: 多重実行の削減)
	KESCMRefreshThumbnailsForPages(targetDB, touchedTargetPages, kFalse /*redrawNow*/);
	// Source 側サムネイルのリングは Show Marks on Source(sSrcMarksOn)ON のときだけ出る。ただし ✓ は
	// sSrcMarksOn と無関係にサムネイルへ出るので、prune 前に Source にチェックがあった場合も更新する。
	if (KESCMDrawEventHandler::sSrcMarksOn || srcHadChecks)
		KESCMRefreshThumbnailsForPages(sourceDB, touchedSourcePages, kFalse /*redrawNow*/);
	KESCMForceRedrawPagesPanelNow();

	if (outChanged) *outChanged = changedCount;
	return kTrue;
}

// ページパネルの選択ページの「ページ比較」を再検出して更新する(ページ右クリック「KESCM: Refresh Page
// Comparison」の実体。旧 Ctrl+ミドルの移設先)。arm 済み(Start 後)かつ前面文書が Target のときだけ動く
// (★2026-07-15 Target 限定化=ユーザー指定。旧仕様の Source→Target 写像経路は撤去)。
// outPages=実際に再比較したページ数(対応表に無い登録済みページ等は数えない)、
// outChanged=うち変化したページ数。戻り=1ページ以上処理したか。
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

	// ページパネルの選択ページを読む(Register/Check と共通のリーダー。KESCMPageMap.cpp)。選択が属する
	// 前面文書が Target でなければ何もしない(Source では項目自体を出さない/無効にする。
	// KESCMRefreshComparisonAvailable と対)。
	IDataBase* db = nil;
	std::vector<UID> selPages;
	if (!KESCMPageMapReadSelection(db, selPages) || db != targetDB)
		return kFalse;

	// 再比較コアは Target ページで駆動する(前面=Target のみなので選択ページがそのまま対象)。
	std::vector<UID> targetPages = selPages;

	int32 processed = 0, changed = 0;
	if (!KESCMRefreshComparisonCore(targetDB, sourceDB, targetPages, &processed, &changed))
		return kFalse;

	// この経路は KESCMDoMarkChangesDoc を通らない独立再比較なので、Prev/Next 間の位置表示と
	// ボタン有効/無効もここで最新化する(選択ページの再比較で変更ページ集合が増減し得るため)。
	KESCMRefreshNavPosition();

	if (outPages)   *outPages = processed;
	if (outChanged) *outChanged = changed;
	return kTrue;
}

// 「KESCM: Refresh Page Comparison」メニューを有効化してよいか(UpdateActionStates 用)。
// arm 済み(Start 後)かつ前面文書が Target のとき kTrue(★2026-07-15 Target 限定化=ユーザー指定。
// コンテキストメニューは無効項目を出さないため、Source 側の右クリックでは項目自体が消える想定)。
// 選択の有無までは見ない(ページ右クリックは通常そのページを選択済みで、未選択でも DoAction 側が
// 安全に no-op しステータス行へ "no comparable pages" を出す)。
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
	return (db != nil && db == targetDB) ? kTrue : kFalse;
}


// マーク(枠/変更数)の表示を切り替えた後、マークが属するドキュメント(sDB)を再描画して
// 即反映する。arm の有無に依らず使えるよう、peek 用の sPeekTargetDB ではなく sDB を使う(arm 不要)。
static void KESCMInvalidateMarksDoc()
{
	KESCMInvalidateDB(KESCMDrawEventHandler::sDB);
}

// マウス下のドキュメントが、arm 済みの対象(Target)文書と一致するか。CMYK サンプリング
// (旧 Shift＋Ctrl＋Alt＋ミドル)とスプレッド枠の部分更新(旧 Ctrl＋ミドル)はヒットテストを sPeekTargetDB の
// ページ座標に対して行うため、マウス下が Source 側や無関係な第3文書のウィンドウだと、そちらの
// ローカル座標を対象文書のページ座標として誤って解釈してしまう。対象文書のウィンドウ上で操作した時
// だけ反応させる。
// ★以前は Utils<ILayoutUIUtils>()->GetFrontDocument()(「front(アクティブ)なドキュメント」)で
// 判定していたが、Split Window の新しい側(kLayoutSecondaryPanelWidgetID)を操作しても OWL 内部の
// アクティブ状態追跡が元側のままになるらしく、判定に失敗していた(ユーザー実測で確認)。
// KESCMSyncScrollOtherWindowsUnderMouse と同じ QueryWindowUnderPoint ベースの判定に統一し、
// マウス位置そのものからドキュメントを特定する(アクティブ状態を一切参照しない)。
// 共通部: マウス直下のドキュメントウィンドウの db を返す(無ければ nil)。Target/Source 判定の
// 差分は比較先 db だけなので、窓解決を1本に畳んだ(2026-07-25 監査で重複解消)。
static IDataBase* KESCMQueryDocDbUnderMouse()
{
	GSysPoint globalPt = Utils<IEventUtils>()->GetGlobalMouseLocation();

	InterfacePtr<IWindow> hitWindow(Utils<IWindowUtils>()->QueryWindowUnderPoint(globalPt, kFalse));
	if (hitWindow == nil)
		return nil;

	InterfacePtr<IDocumentPresentation> hitPres(hitWindow, UseDefaultIID());
	if (hitPres == nil)
		return nil;

	return hitPres->GetDocumentUIDRef().GetDataBase();
}

static bool16 KESCMFrontViewIsOverTarget()
{
	return (sPeekTargetDB != nil && KESCMQueryDocDbUnderMouse() == sPeekTargetDB) ? kTrue : kFalse;
}

// マウス下のドキュメントウィンドウが Source(比較の旧側=常時表示枠を載せている sSrcDB)かどうか。
// 「Hold to Hide Marks」＋「Show Marks on Source」併用時、Source 窓でツール左ボタンを押したときだけ Source 枠を
// 一時退避させるための窓判定(Target 版 KESCMFrontViewIsOverTarget と対称)。Source マークの所属は sSrcDB を
// 正とする(arm の sPeekSourceDB と同一文書だが、判定はマークの実 db に紐づける)。
static bool16 KESCMFrontViewIsOverSource()
{
	return (KESCMDrawEventHandler::sSrcDB != nil &&
	        KESCMQueryDocDbUnderMouse() == KESCMDrawEventHandler::sSrcDB) ? kTrue : kFalse;
}

//========================================================================================
// ビューポート同期エンジン(共有)
//   手本パノラマの「見えている状態」= 実効ズーム(GetXScaleFactor(kTrue)、モニタPPI補正込み。
//   kZoomToCmdBoss の scaleFactor と同じ次元)+可視中心の content 座標 を、比較相手のドキュメントの
//   全レイアウトビューへ複製する(同一文書のビュー=スプリット相方は対象外)。
//   旧 Alt+ミドル(単発)とフライアウト「Sync Layout Views」(自動)の両方がこの1本を使う。
//   ★2026-07-11(ユーザー指定): 発動は「比較を Start 中(sPeekArmed)」かつ「手本・宛先とも Target/Source」の
//   ときだけ。未 Start や第3文書は関数先頭のガードで弾く(=同期しない)。以前は arm と無関係に全文書へ
//   複製していたが、Target↔Source 間のみへ限定した。
//========================================================================================

// 再入ガード: 複製そのものが対象ビューで kScaleTo/kScrollTo 等の通知を発生させ、同期オブザーバが
// それを拾って同期し返す(無限ループ/ピンポン)のを防ぐ。複製ループの間だけ kTrue。
static bool16 sLayoutSyncBroadcasting = kFalse;

// (KESCMFindDocDbForView は 2026-07-25 に KESCMCore.cpp の共有ヘルパへ移動。宣言は KESCMCore.h。
//  色サンプラの窓同一性ガードでも使うため。本ファイルの呼び出しは全てそのまま)

//========================================================================================
// ★ビューポート同期のホットパス用 短命キャッシュ(2026-07-25 追補)
//
//   Sync Layout Views は「どれかのビューがスクロール/ズームするたび」に通知が飛ぶ。スクロールを
//   ドラッグしている間は毎秒数十回この経路を通るが、旧実装はその都度
//     ・文書の全ページ列挙(ISpreadList → ISpread → GetNthPageUID)
//     ・各ページの IGeometry 取得 + InnerToPasteboardMatrix(ページ数ぶんの行列演算)
//     ・除外対応表の再構築(KESCMBuildPairing = 両文書の全ページ走査 + 登録判定)
//   をやり直していた。ページ数に比例した仕事が1通知ごとに乗るので、長い文書ほど追従が重くなる。
//   これらはいずれも「スクロールしている間は変わらない」ものなので、短時間だけ覚えて使い回す。
//
//   無効化は2本立て:
//     ①明示 … KESCMInvalidateSyncCaches()(arm/disarm・同期 OFF・文書クローズ・Shutdown)
//     ②時間 … kKESCMSyncCacheTtlMs(250ms)経過で自動失効。ページの追加/削除やスプレッドの
//              隠し/再表示に追従するための保険(スクロールを止めれば必ず作り直される)。
//
//   ★古いキャッシュを使っても壊れない設計にしてある: ずれ得るのは「追従側のスクロール位置」だけで、
//     次の通知か 250ms 後には正しい値に戻る。db ポインタは照合にしか使わず deref しない。
//========================================================================================
static const long long kKESCMSyncCacheTtlMs = 250;

// 1文書ぶんの「ページ UID とそのペーストボード矩形」。pages と rects は同じ並び。
// 幾何を取れなかったページは空矩形(幅・高さ 0)にしておき、判定側で自然に落とす。
struct KESCMPageRectCache
{
	IDataBase*          db;
	std::vector<UID>    pages;
	std::vector<PMRect> rects;
	KESCMPageRectCache() : db(nil) {}
};
// 枠は2つで足りる: arm 中の同期は Target↔Source の2文書だけを行き来する。
static KESCMPageRectCache sPageRectCache[2];

// 除外対応表(登録ページを除いた順番対応)の両方向マップ。arm 中の (Target, Source) 対に紐づく。
static std::map<UID, UID> sSyncPairT2S;
static std::map<UID, UID> sSyncPairS2T;
static IDataBase* sSyncPairTargetDB = nil;
static IDataBase* sSyncPairSourceDB = nil;
static bool16     sSyncPairBuilt    = kFalse;

// 手本ビューの「前回複製した状態」。同じ状態の通知が続けて来たら複製ごと省く(下の Update 参照)。
// ポインタは同一性の照合にしか使わない(deref しない)。
static IPanorama* sLastSrcPano      = nil;
static PMReal     sLastSrcZoom      = 0.0;
static PBPMPoint  sLastSrcCenter;
static bool16     sHaveLastSrcState = kFalse;

static bool16                                  sSyncCacheValid = kFalse;
static std::chrono::steady_clock::time_point   sSyncCacheStamp;

// 同期キャッシュを丸ごと捨てる(KESCMPeek.h で宣言)。
void KESCMInvalidateSyncCaches()
{
	sSyncCacheValid = kFalse;
	for (int i = 0; i < 2; ++i)
	{
		sPageRectCache[i].db = nil;
		sPageRectCache[i].pages.clear();
		sPageRectCache[i].rects.clear();
	}
	sSyncPairT2S.clear();
	sSyncPairS2T.clear();
	sSyncPairTargetDB = nil;
	sSyncPairSourceDB = nil;
	sSyncPairBuilt    = kFalse;
	sLastSrcPano      = nil;
	sHaveLastSrcState = kFalse;
	KESCMForgetViewDbHint();	// view→db の「直前ヒット」ヒントも一緒に捨てる
}

// 通知1回ぶんの入口で呼ぶ。TTL を過ぎていたらキャッシュを捨てて世代を切り直す。
static void KESCMSyncCacheBeginTick()
{
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	if (sSyncCacheValid)
	{
		const long long ms =
			std::chrono::duration_cast<std::chrono::milliseconds>(now - sSyncCacheStamp).count();
		if (ms < kKESCMSyncCacheTtlMs)
			return;			// まだ有効
		KESCMInvalidateSyncCaches();
	}
	sSyncCacheValid = kTrue;	// 新しい世代を開始
	sSyncCacheStamp = now;
}

// db のページ矩形表を返す(未作成なら作る)。db が nil なら nil。
static const KESCMPageRectCache* KESCMGetPageRects(IDataBase* db);

//----------------------------------------------------------------------------------------
// ページ pageUID(db 内)のペーストボード矩形を得る。パノラマの content 座標=ペーストボード座標
// (PBPMPoint)と同じ空間。ページは回転しないので inner bbox の 2 隅を変換して min/max を取る。
//----------------------------------------------------------------------------------------
static bool16 KESCMPagePasteboardRectRaw(IDataBase* db, UID pageUID, PMRect& outPB)
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
// db のページ矩形表を返す(キャッシュ。未作成なら1回だけ全ページを実測して作る)。
// ★ここが同期ホットパスの重い部分を丸ごと肩代わりする: 旧実装は通知のたびに全ページの IGeometry と
//   InnerToPasteboardMatrix を引き直していた。2枠しか持たないのは、arm 中の同期が Target↔Source の
//   2文書だけを行き来するため(それ以外が来たら古い枠を捨てて作り直す=最悪でも従来と同じ仕事量)。
//----------------------------------------------------------------------------------------
static const KESCMPageRectCache* KESCMGetPageRects(IDataBase* db)
{
	if (db == nil)
		return nil;
	for (int i = 0; i < 2; ++i)
		if (sPageRectCache[i].db == db)
			return &sPageRectCache[i];

	// 空き枠を優先し、両方埋まっていたら 0 番を作り直す。
	const int slot = (sPageRectCache[0].db == nil) ? 0 : ((sPageRectCache[1].db == nil) ? 1 : 0);
	KESCMPageRectCache& c = sPageRectCache[slot];
	c.db = db;
	c.pages.clear();
	c.rects.clear();
	KESCMCollectPageUIDs(db, c.pages);
	c.rects.resize(c.pages.size());
	for (size_t i = 0; i < c.pages.size(); ++i)
	{
		if (!KESCMPagePasteboardRectRaw(db, c.pages[i], c.rects[i]))
			c.rects[i] = PMRect(0, 0, 0, 0);	// 幾何が取れないページ=空矩形にして下の判定から落とす
	}
	return &c;
}

//----------------------------------------------------------------------------------------
// ページ pageUID(db 内)のペーストボード矩形(キャッシュ経由)。同期経路はこちらを使う。
// 空矩形(幾何が取れなかったページ)は kFalse を返し、旧実装の「取得失敗」と同じ扱いになる。
//----------------------------------------------------------------------------------------
static bool16 KESCMPagePasteboardRect(IDataBase* db, UID pageUID, PMRect& outPB)
{
	if (pageUID == kInvalidUID)
		return kFalse;
	const KESCMPageRectCache* c = KESCMGetPageRects(db);
	if (c == nil)
		return kFalse;
	for (size_t i = 0; i < c->pages.size(); ++i)
	{
		if (c->pages[i] != pageUID)
			continue;
		const PMRect& r = c->rects[i];
		if (r.Right() <= r.Left() && r.Bottom() <= r.Top())
			return kFalse;	// 空矩形=幾何が取れなかったページ
		outPB = r;
		return kTrue;
	}
	return kFalse;
}

//----------------------------------------------------------------------------------------
// db 内で、ペーストボード点 pb を内包するページ UID を返す。内包が無ければ中心が最も近いページ
// (ページ間の隙間/ペーストボード上をビュー中心が指しているとき)。ページが無ければ kInvalidUID。
// ページ矩形はキャッシュから読むので、通知のたびの全ページ実測は起きない。
//----------------------------------------------------------------------------------------
static UID KESCMFindPageAtPasteboard(IDataBase* db, const PBPMPoint& pb)
{
	const KESCMPageRectCache* c = KESCMGetPageRects(db);
	if (c == nil)
		return kInvalidUID;
	UID best = kInvalidUID;
	PMReal bestDist2(0);
	bool16 haveBest = kFalse;
	for (size_t i = 0; i < c->pages.size(); ++i)
	{
		const PMRect& r = c->rects[i];
		if (r.Right() <= r.Left() && r.Bottom() <= r.Top())
			continue;	// 空矩形=幾何が取れなかったページ
		if (pb.X() >= r.Left() && pb.X() <= r.Right() && pb.Y() >= r.Top() && pb.Y() <= r.Bottom())
			return c->pages[i];	// 内包するページが確定
		const PMReal cx = (r.Left() + r.Right()) / PMReal(2.0);
		const PMReal cy = (r.Top()  + r.Bottom()) / PMReal(2.0);
		const PMReal dx = pb.X() - cx, dy = pb.Y() - cy;
		const PMReal d2 = dx * dx + dy * dy;
		if (!haveBest || d2 < bestDist2) { bestDist2 = d2; best = c->pages[i]; haveBest = kTrue; }
	}
	return best;
}

//----------------------------------------------------------------------------------------
// 除外対応表(登録ページを除いた順番対応)の両方向マップを用意する(キャッシュ。未作成なら1回だけ作る)。
// ★旧実装は KESCMMapTargetToSource / KESCMMapSourceToTarget が呼ばれるたびに KESCMBuildPairing を
//   まるごと作り直していた(=両文書の全ページ列挙 + 登録判定 + 線形探索)。同期の通知は毎秒数十回
//   来るので、ここがページ数に比例した固定費になっていた。1世代(250ms または明示無効化まで)に
//   1回だけ作り、以後は map の O(log n) 探索で引く。
//----------------------------------------------------------------------------------------
static void KESCMEnsureSyncPairing(IDataBase* targetDB, IDataBase* sourceDB)
{
	if (sSyncPairBuilt && sSyncPairTargetDB == targetDB && sSyncPairSourceDB == sourceDB)
		return;
	sSyncPairT2S.clear();
	sSyncPairS2T.clear();
	sSyncPairTargetDB = targetDB;
	sSyncPairSourceDB = sourceDB;
	sSyncPairBuilt    = kTrue;	// 対応が空(全ページ登録済み等)でも「作った」ことは覚える
	if (targetDB == nil || sourceDB == nil)
		return;
	std::vector<UID> pairT, pairS;
	KESCMBuildPairing(targetDB, sourceDB, pairT, pairS);
	for (size_t i = 0; i < pairT.size(); ++i)
	{
		sSyncPairT2S[pairT[i]] = pairS[i];
		sSyncPairS2T[pairS[i]] = pairT[i];
	}
}

//----------------------------------------------------------------------------------------
// 旧 Alt+ミドル/自動同期の追加/削除補正: 手本(srcDocDb)のビュー中心にあるページを、比較ペアリング
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
	// ★対応表はキャッシュから引く(2026-07-25 追補)。挙動は KESCMMapTargetToSource/SourceToTarget と同一で、
	//   「対応表に無い=相手なし」を skip として扱う点も変わらない。
	KESCMEnsureSyncPairing(sPeekTargetDB, sPeekSourceDB);
	const std::map<UID, UID>& pairTable = t2s ? sSyncPairT2S : sSyncPairS2T;
	std::map<UID, UID>::const_iterator pairIt = pairTable.find(srcPage);
	if (pairIt == pairTable.end() || pairIt->second == kInvalidUID)
	{
		outSkip = kTrue;	// ★相手なし(Added 等): 追従側は動かさない
		return srcCenter;
	}
	const UID dstPage = pairIt->second;

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
// ★フライアウト「Sync Layout Views」の自動同期は本仕様として kTrue で呼ぶ(比較 arm 中は比較ペアの
// 相手ページ同士がきっちり並ぶ)。ペア外は関数内で生同期にフォールバック。
// (★2026-07-15: 旧「左ダブルクリック=arm 不問・全文書同期」用の limitToArmedPair=kFalse 分岐は、
//  ジェスチャ自体の全廃で呼び出しゼロになっていたため削除。同期は常に Start 中の Target↔Source 限定。)
static void KESCMSyncOtherDocViewportsTo(IPanorama* srcPano, IDataBase* srcDocDb, bool16 applyPageOffset = kFalse)
{
	if (srcPano == nil)
		return;

	// キャッシュ世代の更新はここでも行う(2026-07-25 追補)。Update 経由なら既に呼ばれているので no-op だが、
	// Align Other Views / Sync ON の初回そろえはこの関数を直接呼ぶため、TTL をここでも効かせておく。
	KESCMSyncCacheBeginTick();

	// 同期の作動条件を2モードで判定する:
	//   (A) 比較を Start 中(arm 済み): 従来通り Target↔Source の間だけ・追加/削除補正あり(ユーザー指定 2026-07-11)。
	//       手本(操作した)ビューが Target/Source のどちらでもない第3文書なら同期しない。
	//   (B) 未 Start(Stop 中): アクティブ(操作した)文書へ他の全文書を同期する。
	//       AddRemove(追加/削除補正)は扱わない=applyPageOffset を強制 kFalse。
	//       ★2026-07-24 変更: 旧仕様は「Stop 中は KESCM ツール選択中のみ同期」(誤同期回避が目的)だったが、
	//       この関数に来る時点で ONトグル(sLayoutSyncOn)は必ず ON 確定なので、それを作動条件と見なし、
	//       ツールの選択状態に関係なく同期する(ユーザー指定)。
	const bool16 armed = (sPeekArmed && sPeekTargetDB != nil && sPeekSourceDB != nil);
	bool16 stopBroadSync = kFalse;
	if (armed)
	{
		if (srcDocDb != sPeekTargetDB && srcDocDb != sPeekSourceDB)
			return;
	}
	else
	{
		stopBroadSync   = kTrue;
		applyPageOffset = kFalse;	// ★AddRemove(追加/削除補正)は掛けない(ユーザー指定)
	}

	// 手本ビューの「見えている状態」を読む。ズームは実効スケール(kTrue=モニタPPI補正込み)。
	// ズームコマンド(kZoomToCmdBoss)が扱う scaleFactor と同じ次元なので、読み書きが対称になる。
	const PMReal  srcZoom   = srcPano->GetXScaleFactor(kTrue);
	const PBPMPoint srcCenter(srcPano->GetContentLocationAtFrameCenter());

	ISession* session = GetExecutionContextSession();	// 終了処理中は nil になり得る(2026-07-25 追補 統一)
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return;

	// ★宛先文書を先に確定する(2026-07-25 追補)。
	//   arm 中(A) … 手本は上のガードで Target/Source のどちらかに確定しているので、宛先は「対の相手」
	//                1文書しかない。旧実装はそれを求めるために毎回 IDocumentList を端から端まで回し、
	//                文書ごとに ::GetUIDRef(doc).GetDataBase() を作っていた。スクロール追従は毎秒数十回
	//                この関数を通るので、相手を直接名指しして生存確認1回に畳む。
	//   Stop 中(B) … 手本以外の全文書が宛先なので、従来どおり列挙する。
	std::vector<IDataBase*> dstDbs;
	dstDbs.reserve(2);
	if (!stopBroadSync)
	{
		IDataBase* dstDb = (srcDocDb == sPeekTargetDB) ? sPeekSourceDB : sPeekTargetDB;
		// 相手がまだ開いているかだけ確認する(旧実装は docList 全走査が暗黙に保証していた条件)。
		// ★FindDocByDataBase へのポインタ比較のみ=閉じた db を deref しない(KESCM 共通規約)。
		if (dstDb != nil && dstDb != srcDocDb && docList->FindDocByDataBase(dstDb) != nil)
			dstDbs.push_back(dstDb);
	}
	else
	{
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
			dstDbs.push_back(db);
		}
	}
	if (dstDbs.empty())
		return;	// 複製先が無い(=何もしない)。再入ガードを立てる前に抜ける

	// 再入ガードを RAII で立てる(2026-07-25 監査で変更): 複製ループ中の ProcessCommand が万一 throw
	// してもフラグが立ちっぱなし(=以後の同期が永久に無効化)にならない。
	struct KESCMSyncBroadcastGuard
	{
		KESCMSyncBroadcastGuard()  { sLayoutSyncBroadcasting = kTrue; }	// ここからの通知は自分発なのでオブザーバは無視する
		~KESCMSyncBroadcastGuard() { sLayoutSyncBroadcasting = kFalse; }
	} broadcastGuard;

	for (size_t di = 0; di < dstDbs.size(); ++di)
	{
		IDataBase* db = dstDbs[di];

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
	// (sLayoutSyncBroadcasting は broadcastGuard のデストラクタが戻す)
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
//   ③同期エンジンは 旧 Alt+ミドルと共通(kZoomToCmdBoss+実効スケール対称読み書き=本日実機確定の手順)。
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
	// session は終了処理中に nil になり得る(2026-07-25 追補 に KESCM 全体で統一)。
	ISession* session = GetExecutionContextSession();
	IActiveContext* ctx = (session != nil) ? session->GetActiveContext() : nil;
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
	ISession* session = GetExecutionContextSession();	// 終了処理中は nil になり得る
	IActiveContext* ctx = (session != nil) ? session->GetActiveContext() : nil;
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

	// 通知元(=手本)のパノラマ。theSubject はレイアウトビュー boss の subject なので、
	// 同じ boss から IPanorama / IControlView を引ける。
	InterfacePtr<IPanorama> srcPano(theSubject, UseDefaultIID());
	if (srcPano == nil)
		return;

	// ★同一状態の通知を弾く(2026-07-25 追補)。1回のスクロール/ズーム操作で kScrollTo と kScrollBy のように
	//   複数の通知が続けて届くことがあり、その全部で複製一式(ページ対応の解決 + 全宛先ビューの走査)を
	//   走らせるのは無駄。手本の (パノラマ, 実効ズーム, 可視中心) が前回複製したときと完全に同じなら
	//   何も変わっていないので即戻る。この判定はパノラマ2回読みだけで済む=最も安いふるい。
	//   ★取りこぼしは無い: 宛先側だけが動いた場合は、その宛先ビュー自身からも通知が来て、そちらが
	//     手本として処理される(手本と宛先は固定ではない)。
	//   ★sLastSrcPano はポインタ照合にしか使わない(deref しない)。別ビューが同じアドレスを再利用
	//     しても、ズーム/中心まで一致しない限り弾かれないので実害は無い。250ms の TTL でも失効する。
	KESCMSyncCacheBeginTick();	// TTL 超過ならこの中でキャッシュ一式(前回状態を含む)が捨てられる
	const PMReal    curZoom = srcPano->GetXScaleFactor(kTrue);
	const PBPMPoint curCenter(srcPano->GetContentLocationAtFrameCenter());
	if (sHaveLastSrcState && sLastSrcPano == (IPanorama*)srcPano)
	{
		PMReal dz = curZoom - sLastSrcZoom;                 if (dz < 0) dz = -dz;
		PMReal dcx = curCenter.X() - sLastSrcCenter.X();    if (dcx < 0) dcx = -dcx;
		PMReal dcy = curCenter.Y() - sLastSrcCenter.Y();    if (dcy < 0) dcy = -dcy;
		if (dz <= PMReal(0.0) && dcx <= PMReal(0.0) && dcy <= PMReal(0.0))
			return;	// 手本は前回複製時から1ミリも動いていない
	}

	InterfacePtr<IControlView> srcView(theSubject, UseDefaultIID());
	if (srcView == nil)
		return;
	IDataBase* srcDocDb = KESCMFindDocDbForView(srcView);
	if (srcDocDb == nil)
		return;	// 所属文書を特定できない(クローズ途中等)。同期しない

	// ここから実際に複製する=この状態を「前回複製した状態」として記録する。
	sLastSrcPano      = (IPanorama*)srcPano;
	sLastSrcZoom      = curZoom;
	sLastSrcCenter    = curCenter;
	sHaveLastSrcState = kTrue;

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

	KESCMInvalidateSyncCaches();	// ON/OFF のどちらでも、次の同期は最新の実測から始める(2026-07-25 追補)

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

// KESCMAlignOtherViewsToActiveNow(KESCMCore.h で宣言) — フライアウト/ショートカットの実行アクション。
// アクティブ(最前面)レイアウトビューの位置+拡大率を他文書の全ビューへ1回そろえる。Sync Layout Views
// トグルの ON/OFF とは独立で、OFF でも押せば1回だけそろう(トグル ON 時の初回そろえ=上の KESCMSetLayoutSync
// と同じ手本ビュー取得+同じ同期エンジン呼び出し)。applyPageOffset=kTrue を渡すので、Start 中(arm)は
// KESCMSyncOtherDocViewportsTo のガードにより Target↔Source 間でページの Add/Remove 補正が掛かり(賢く一致)、
// 未 Start 時は同関数が補正を kFalse に強制して他の全文書へ生同期する。
bool16 KESCMAlignOtherViewsToActiveNow()
{
	// 明示アクションなので、キャッシュの鮮度に関係なく必ず今の実測でそろえる(2026-07-25 追補)。
	KESCMInvalidateSyncCaches();

	InterfacePtr<IControlView> front(Utils<ILayoutUIUtils>()->QueryFrontView());
	if (front == nil)
		return kFalse;
	InterfacePtr<IPanorama> pano(KESCMQueryPanorama(front));
	IDataBase* db = KESCMFindDocDbForView(front);
	if (pano == nil || db == nil)
		return kFalse;
	// ★Start 中(arm)は同期エンジンが Target↔Source 間だけに限定し、最前面が第3文書なら何もせず戻る。
	//   その場合に「そろえた」と誤って成功表示を出さないよう、engine と同じ条件をここで先読みして kFalse を
	//   返す(2026-07-24。下の KESCMSyncOtherDocViewportsTo の armed ガードと必ず同条件に保つ)。
	if (sPeekArmed && sPeekTargetDB != nil && sPeekSourceDB != nil &&
	    db != sPeekTargetDB && db != sPeekSourceDB)
		return kFalse;
	KESCMSyncOtherDocViewportsTo(pano, db, kTrue /*applyPageOffset(arm時のみ補正・未arm時は関数内でkFalse強制)*/);
	return kTrue;
}


//========================================================================================
// トラッカー(左ボタン)用の共有入口。KESCM ツール選択中の左ボタン押下/解放から呼ばれる
// (KESCMTracker.cpp)。修飾なし押下=マーク reveal を基本に、修飾キーで peek/CMYK を切り替える。
// ここはファイル内の peek 状態(sSingleShowing)と描画状態(KESCMDrawEventHandler::sMarks*)に
// アクセスできる。
//
// ★由来(2026-07-12〜13): もとは中ボタン＋修飾キーのジェスチャだったものをツールの左ボタンへ移植した。
//   修飾なし=マーク一時表示 / Hold to Hide Marks の窓別 temp-hide(Target/Source) /
//   Shift+左=旧版べた載せ peek 100% / Shift+Alt+左=peek 50% / Alt+左(単独)=CMYK 生値サンプリング。
//   中ボタン経路(および Ctrl 系のパネル/再比較ジェスチャ)は撤去済み(2026-07-13)。再比較はページ
//   右クリックメニュー「KESCM: Refresh Page Comparison」へ移設。
//========================================================================================

// ★armed 中の Target/Source が IDocumentList に現存するかの最終ライン防御(2026-07-15 復活)。
//   旧・中ボタン watcher はジェスチャ毎にこの検査を行っていたが、ツール移行で失われていた。
//   通常はクローズ responder(KESCMHandleDocsClosed)が先回りして disarm するため失格には到達しないが、
//   responder が漏れた場合に解放済み IDataBase をサンプリング/peek へ渡さないための保険。
//   失格なら KESCMHandleDocsClosed() で Stop 相当のフルクリーンアップ(sPeek* 解除を含む)をして kFalse。
static bool16 KESCMArmedDocsAlive()
{
	if (!sPeekArmed || sPeekTargetDB == nil || sPeekSourceDB == nil)
		return kFalse;
	ISession* session = GetExecutionContextSession();	// 終了処理中は nil になり得る(2026-07-25 追補 統一)
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList == nil ||
	    docList->FindDocByDataBase(sPeekTargetDB) == nil ||
	    docList->FindDocByDataBase(sPeekSourceDB) == nil)
	{
		KESCMHandleDocsClosed();
		return kFalse;
	}
	return kTrue;
}

// トラッカー(左ボタン)用の peek 開始。arm 済み(Start 後)かつ Target 窓上のときだけ、マウス下スプレッドの
// 旧版を opacity(1.0=不透明 / 0.5=半透明)で重ねる。ハンドツールへの一時切替はしない(トラッカーが既に
// マウスをキャプチャ済みで、ドラッグは ContinueTracking へ行くため不要)。
static void KESCMTrackerBeginPeek(PMReal opacity)
{
	if (!KESCMArmedDocsAlive() || !KESCMFrontViewIsOverTarget())
		return;	// 未 Start / 比較文書が閉じ済み / Target 窓以外では反応しない(旧・中ボタン peek 分岐と同じ条件)
	sPeekActive = kTrue;
	KESCMDrawEventHandler::sPeekOpacity = opacity;	// 旧版べた載せの不透明度(描画時に参照)
	sSingleShowing = kFalse;
	KESCMDrawEventHandler::sMarksVisible = kFalse;	// 覗き中は枠等を出さない(旧版だけ)
	KESCMPeekShowUnderMouse(sPeekTargetDB, sPeekSourceDB);
}

//========================================================================================
// Alt+左「色比較」の CMYK 情報を、パネル状態行に加えて**カーソル自身**にも描く。
//   カーソルは OS 描画=ドキュメント窓枠を超えマウス追従(仕組み: CursorSpec のコールバックで
//   自前バッファに AGM 描画する「カスタムビットマップカーソル」。ChangeModalCursor はトラッカー
//   =独自ツールを持つ KESCM だから使える特典)。CreateCursorBitmapProc は引数でデータを渡せない
//   ので、描く文字列は file-static sCmykCursorText に置きコールバックから読む。
//   ★これはまず「出るか」を見る実装スパイク(2026-07-13)。座標系(y方向)・alpha・サイズは実機で調整。
//========================================================================================
static PMString sCmykCursorText;			// "… t\n… s"(LF区切り2行、末尾ラベル t/s。1行目=マウスが乗っている窓の側)。色サンプル成功時に格納。
static bool16   sCmykCursorPending = kFalse;	// 直近の BeginTracking で CMYK カーソルを出すべきか

// Alt+左ドラッグ中だけ保持する既定フォント(取得=RevealBegin の Alt 分岐、解放=RevealEnd)。
// ドラッグ中のカーソル再描画(≦20回/秒)が毎回 IFontMgr の名前引きをしないためのキャッシュ(2026-07-15)。
// ★file-static の InterfacePtr にはしない: 静的破棄タイミングの Release はオブジェクトモデル消滅後で
//   危険なため、生ポインタ+RevealEnd での明示解放(押下の外では常に nil)にする。
static IPMFont* sCmykCursorFont = nil;

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

// CMYK カーソルの上部に載せる✓。色は arm 状態だけで出し分ける(常時ツールカーソル KESCMCursorProvider.cpp と
// 同じ規則。ユーザー要望 2026-07-24 / 2026-07-26 に「Start 中はどの文書の上でも黒」で確定): Start(arm 済み)は
// 黒✓、Stop(未 arm)は白抜き✓(黒フチ+白本体=KESCMCheckCursorInactiveBitmapProc と同一パラメータ)。
// ★Start 中の第3の文書は表示こそ単独1行(Stop と同じ)だが、✓は黒のまま=「比較は動いている」を示す。
static void KESCMDrawCmykCursorCheck(IGraphicsPort* gPort)
{
	if (sPeekArmed)
		KESCMDrawCheckGlyph(gPort);											// 黒✓(Start)
	else
		KESCMDrawCheckGlyph(gPort, PMReal(1.0), PMReal(0.0), PMReal(5.0));	// 白抜き✓(Stop)
}

// CursorSpec のコールバック。カーソル描画系が呼ぶ(UIスレッド)。bitmapBuffer は呼び出し側が
// (最大カーソルサイズ)²×4 で確保済み。*width/*height は入力=最大サイズ(hiRes 時は 2 倍)、出力=実使用サイズ。
static void KESCMCmykCursorBitmapProc(uchar* bitmapBuffer, uint32* width, uint32* height, bool16* hasAlpha, bool16 hiRes)
{
	// 前処理(確保全域の透明クリア+論理最大サイズ取得)は ✓カーソルと共有(KESCMCheckGlyph.h)。
	// 背景は透明のまま=黒い箱を出さない(ユーザー指定 2026-07-13)。
	uint32 maxLogW = 0, maxLogH = 0;
	KESCMCursorBitmapBegin(bitmapBuffer, *width, *height, hiRes, maxLogW, maxLogH);

	// 表示文字列(数値2行、各行末尾にラベル t/s)を分解し、最長行から「幅いっぱいに収まる大きめフォント」を
	// 決める(ユーザー要望 2026-07-13: カーソル最大サイズまで使って cmyk＋数値を大きく)。
	PMString line1, line2;
	KESCMSplitTwoLines(sCmykCursorText, line1, line2);
	const int32 chars1 = line1.NumUTF16TextChars();
	const int32 chars2 = line2.NumUTF16TextChars();

	// ★空文字ガード(2026-07-25 追加)。文字列が空のまま呼ばれると下の maxChars が 1 になり、fs が
	//   (maxLogW-8)*100/58 = 100〜200pt まで跳ね上がって、巨大な "C M Y K" がカーソル全面に描かれる
	//   =見た目はまさに「ゴミ」。通常経路(InstallCmykCursor は値が採れた時だけ)では起きないが、
	//   カーソルキャッシュの再生成や DPI 変更で proc が呼ばれると露出しうるので保険を入れる。
	//   ★*width/*height/*hasAlpha を設定せずに return してはいけない(未設定だと最大サイズ・24bit RGB
	//     扱い等で本物のゴミになる)。ツール常時カーソルと同じ「✓だけの 24x24」に倒す。
	if (chars1 <= 0 && chars2 <= 0)
	{
		InterfacePtr<IGraphicsPort> gPortCheckOnly(KESCMCursorBitmapFinish(
			bitmapBuffer, width, height, hasAlpha, hiRes, 24u, 24u, maxLogW, maxLogH));
		if (gPortCheckOnly == nil)
			return;
		gPortCheckOnly->setopacity(PMReal(1.0), kFalse);
		KESCMDrawCmykCursorCheck(gPortCheckOnly);
		return;
	}

	int32 maxChars = (chars2 > chars1) ? chars2 : chars1;
	if (maxChars < 1) maxChars = 1;

	// フォントは使える最大幅から大きめに決める(1文字≒0.58em、下限7pt)。
	// ★上限キャップは撤廃で確定(2026-07-14 検証→2026-07-25 採用を明文化): 18→26→48pt と上げても実機で
	// 変化が無かった=maxLogW(カーソル最大論理サイズ=OS/カーソルマネージャ依存)からの逆算値が実質の
	// 上限として機能しており、人工的なキャップは不要。
	int32 fs = ((int32)maxLogW - 8) * 100 / (maxChars * 58);
	if (fs < 7)  fs = 7;

	// ★ビットマップ幅は「実際の内容幅」にタイトに合わせる。最大幅いっぱいに取ると右側に広い透明余白が
	// でき、その初回フレームがちらついて見える(ゴミ)ため。内容幅 = 左6 + 4列×ピッチ(2.1em) +
	// ラベル(t/s=1文字≒0.58em) + 右4 ≒ 10 + 8.98em(下の描画の pitch=2.1×fs と一致させること。
	// 2026-07-15: ラベルを tgt/src→t/s へ短縮したのに合わせ 10.14em→8.98em に更新=右端の透明余白を除去)。
	const int32 contentW = 10 + (fs * 898) / 100;	// fs>=7 保証(上のクランプ)により常に正
	uint32 logW = (uint32)contentW;					// クランプは KESCMCursorBitmapFinish が行う

	// ✓(上部 y≈18 まで)の下に「ヘッダー C M Y K + データ2行(Target/Source)」を積む。位置・高さは fs から。
	const int32 gap    = (fs * 130) / 100;	// 行間 ≒1.3em
	const int32 yHdr   = 22 + fs;			// ヘッダー行ベースライン(✓の下。全体を少し下げた=ユーザー要望 2026-07-13)
	const int32 yData1 = yHdr + gap;		// Target 行
	const int32 yData2 = yData1 + gap;		// Source 行
	// 最下段(Source 行 "src")はディセンダ(下に伸びる字)が無いので、ベースラインのすぐ下でビットマップを
	// 終える。下端の透明余白を残すと、そこに初回フレームのちらつき(ゴミ)が出る(ユーザー報告: 文字より
	// 約3px下に一瞬。2026-07-13)。ハロー(y+1)とAA ぶんだけ +2 で足りる。
	// ★solo(Stop 単独ピック=line2 空)は Target 行までで終える(2026-07-25 監査で修正): 常に yData2 基準だと
	//   使わない Source 行ぶんの透明帯が下に残り、上の「余白タイト化」方針と矛盾していた。
	int32 needH = ((chars2 > 0) ? yData2 : yData1) + 2;
	uint32 logH = (needH > 0) ? (uint32)needH : 60u;

	// サイズ確定(クランプ込み)+AGM ポート取得(✓カーソルと共有の後処理。KESCMCheckGlyph.h)。
	InterfacePtr<IGraphicsPort> gPort(KESCMCursorBitmapFinish(
		bitmapBuffer, width, height, hasAlpha, hiRes, logW, logH, maxLogW, maxLogH));
	if (gPort == nil)
		return;

	// 背景は透明(上で全域 ARGB=0 にクリア済み)。setopacity は以降のストローク/文字を不透明にするため。
	gPort->setopacity(PMReal(1.0), kFalse);
	/* 背景塗りは廃止=透明のまま。黒い箱を出さない(ユーザー指定 2026-07-13) */

	// ツール選択中と同じ✓を、共有ヘルパ KESCMDrawCheckGlyph でホットスポット(10,18)=✓の折れ点=
	// クリック点に描く(KESCMCursorProvider.cpp と同一形状/座標)。数値表示中もカーソル形状を残す
	// (ユーザー要望 2026-07-14)。★以前は「✓ を stroke で描くと初回フレームのちらつき(ゴミ)が出る」と
	// 考えて rectfill のドットに退避していたが(2026-07-13)、その後の調査でゴミの真因は stroke 描画では
	// なく BeginTracking の多段カーソル切替が OS のハードウェアカーソル合成にそのまま見えていたことだと
	// 判明した(対策は KESCMTracker.cpp の BeginTracking = サンプリングを切替の前へ出す)。stroke 自体は
	// 無罪なので✓に戻して問題ない。色の出し分けは KESCMDrawCmykCursorCheck に集約(空文字ガードと共有)。
	KESCMDrawCmykCursorCheck(gPort);

	// 上から: ヘッダー "C M Y K"(各列先頭にそろえる) / Target 数値 / Source 数値。数値は各値3桁で行頭
	// そろえ、末尾に t/s。フォント fs・行位置は上で計算済み。描画は KESCMShowHalo(白フチ＋黒本体)。
	// フォントは押下中キャッシュ(sCmykCursorFont。ドラッグ再描画≦20回/秒の名前引き回避)を使い、
	// 万一 nil ならローカルに引き直すフォールバック(2026-07-15)。
	IPMFont* font = sCmykCursorFont;
	InterfacePtr<IPMFont> fallbackFont;
	if (font == nil)
	{
		InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), UseDefaultIID());
		if (fontMgr != nil)
			fallbackFont = InterfacePtr<IPMFont>(fontMgr->QueryFont(fontMgr->GetDefaultFontName()));
		font = fallbackFont;
	}
	if (font != nil)
	{
		// 見出し行とデータ2行を同じ固定列(x0, pitch)で描いて桁を縦にそろえる(pitch=3桁+ギャップ)。
		// ヘッダーの C/M/Y/K が各3桁列の真上に来る(ユーザー要望の縦位置合わせ 2026-07-13)。
		const PMReal x0(6.0);
		const PMReal pitch = PMReal(fs) * PMReal(2.1);
		PMString hdr; hdr.SetTranslatable(kFalse); hdr.Append("C M Y K");
		KESCMDrawColumns(gPort, font, PMReal(fs), x0, pitch, PMReal(yHdr),   hdr);	// 見出し C M Y K
		KESCMDrawColumns(gPort, font, PMReal(fs), x0, pitch, PMReal(yData1), line1);	// 1行目=マウスが乗っている窓の側(t or s)
		KESCMDrawColumns(gPort, font, PMReal(fs), x0, pitch, PMReal(yData2), line2);	// 2行目=比較相手(単独モードでは空=自動スキップ)
	}
}

// KESCMTracker.cpp から使う入口。BeginTracking の CMYK 分岐が成功したら Pending が立ち、トラッカーが
// ChangeModalCursor(CursorSpec(KESCMTrackerCmykCursorProc(), …)) を呼ぶ。
bool16 KESCMTrackerHasPendingCmykCursor()          { return sCmykCursorPending; }
CreateCursorBitmapProc KESCMTrackerCmykCursorProc() { return &KESCMCmykCursorBitmapProc; }

// ツール常時✓カーソルの黒/白抜き判定(KESCMPeek.h 参照)。
// ★2026-07-26(ユーザー指定): 黒=「Start 中(比較文書が生存)」だけで決める。マウス下がどの文書かは見ない。
//   Alt+左の CMYK は Start 中ならどの窓でも値を出す(Target/Source 窓=比較2行、第3の文書=単独1行)ので、
//   以前の「Target 窓だけ黒」は実態と合わなくなった。Stop 中は従来どおり白抜き✓(黒フチ+白本体)。
//   viewUnderMouse が nil(レイアウトビュー上に居ない)なら白抜きのまま=カーソル形状の既定側に倒す。
bool16 KESCMToolCursorShouldBeBlack(IControlView* viewUnderMouse)
{
	if (viewUnderMouse == nil)
		return kFalse;
	return KESCMArmedDocsAlive();
}

// KESCMTrackerUpdateCmykDrag(KESCMPeek.h 参照) — ドラッグ中の CMYK ライブ更新。
// トラッカーの ContinueTracking(マウス移動)から呼ばれる。現在のマウス位置で CMYK を再サンプルし、
// 値が変わったら sCmykCursorText を更新して kTrue を返す(呼び出し側がカーソルを描き直す)。
// 連続ラスタ化で重くならないよう時間スロットル(既定 50ms ≒ 20回/秒)を掛ける。
// 前方宣言。定義は KESCMTrackerRevealBegin の直前(ページ外の「値なし c---」表示を作る)。
// hoverIsTarget= 1行目(=マウスが乗っている窓)のラベルが t か s か。
static void KESCMBuildCmykNoValue(PMString& out, bool16 hoverIsTarget);			// 比較: カーソル用(t/s)
static void KESCMBuildCmykNoValuePanel(PMString& out, bool16 hoverIsTarget);	// 比較: パネル用(見出し文字+t/s)
static void KESCMBuildCmykNoValueSolo(PMString& out);							// 単独: カーソル1行(ラベルなし)
static void KESCMBuildCmykNoValuePanelSolo(PMString& out);						// 単独: パネル1行(ラベルなし)

// 押下中に固定した CMYK 対象文書(sCmykHoverDB / sCmykOtherDB)がまだ開いているか。ドラッグ中に稀な経路で
// 文書が閉じても、解放済み IDataBase をサンプリングへ渡さないための最終ライン防御。
//   比較モード … hover/other は arm 済みの Target/Source なので arm 版の検査に委ねる
//                (KESCMArmedDocsAlive は失格時に Stop 相当のクリーンアップまでやる)。
//   単独モード … マウス下の1文書をドキュメントリストに照合するだけ(第3の文書や Stop 中なので arm と無関係)。
static bool16 KESCMCmykDocsAlive()
{
	if (sCmykHoverDB == nil)
		return kFalse;
	if (sCmykOtherDB != nil)
		return KESCMArmedDocsAlive();
	ISession* session = GetExecutionContextSession();	// 終了処理中は nil になり得る
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	return (docList != nil && docList->FindDocByDataBase(sCmykHoverDB) != nil) ? kTrue : kFalse;
}

bool16 KESCMTrackerUpdateCmykDrag()
{
	if (!sCmykCursorPending)	// Alt+左 CMYK モードでなければ何もしない
		return kFalse;

	// 押下時に固定したモード(hover/other)をそのまま使う。押下中に基準の窓は切り替えない。
	if (sCmykHoverDB == nil)
		return kFalse;
	const bool16 solo = (sCmykOtherDB == nil);

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

	// スロットル通過後(≦20回/秒)に文書の生存を検査してからサンプリングへ渡す
	// (ドラッグ中の文書クローズはスクリプト経由等の稀な経路。検査は KESCMCmykDocsAlive に集約)。
	if (!KESCMCmykDocsAlive())
		return kFalse;

	// 現在のマウス位置でサンプリング(KESCMSampleCmykUnderMouse は毎回マウス位置を読み直す)。単独モードは
	// other=nil で hover だけ(1行)。ページ外・押した窓から外れた・取得失敗なら「値なし(--- …)」表示にして
	// 拾えていないことを示す(ユーザー要望 2026-07-13。直前値を残さない=誤読防止)。
	PMString panelMsg, cursorMsg;
	if (!KESCMSampleCmykUnderMouse(sCmykHoverDB, sCmykOtherDB, sCmykHoverIsTarget, panelMsg, cursorMsg))
	{
		if (solo) { KESCMBuildCmykNoValueSolo(cursorMsg); KESCMBuildCmykNoValuePanelSolo(panelMsg); }
		else      { KESCMBuildCmykNoValue(cursorMsg, sCmykHoverIsTarget);
		            KESCMBuildCmykNoValuePanel(panelMsg, sCmykHoverIsTarget); }
	}
	if (cursorMsg == sCmykCursorText)	// 値が同じなら描き直し不要(パネルも同じ値なので更新不要)
		return kFalse;

	// パネルのステータス行もドラッグに追従させる(強制表示はしない。KESCMTrackerRevealBegin と同じ方針)。
	KESCMSetStatus(panelMsg);
	sCmykCursorText = cursorMsg;
	return kTrue;
}

// ページ外など CMYK を拾えないときに出す「値なし」表示("--- --- --- --- t/s")。ダッシュで
// 「ここでは色を拾えていない」ことが分かるようにする(ユーザー要望 2026-07-13)。ラベルは通常と同じ t/s で、
// 1行目は成功時と同じく hover 側(Target 窓なら t、Source 窓なら s。2026-07-26)。
static void KESCMBuildCmykNoValue(PMString& out, bool16 hoverIsTarget)
{
	out.Clear();
	out.SetTranslatable(kFalse);
	out.Append(hoverIsTarget ? "--- --- --- --- t" : "--- --- --- --- s");	// ラベルは t/s(KESCMColorSampler.cpp と同じ短縮。2026-07-14)
	out.AppendW(UTF32TextChar(0x0A));	// 改行 → 2行目へ
	out.Append(hoverIsTarget ? "--- --- --- --- s" : "--- --- --- --- t");
}

// パネル版の「値なし」表示。値ごとに見出し文字を添え t/s にする。KESCMSampleCmykUnderMouse
// 成功時のパネル表記(KESCMColorSampler.cpp の KESCMAppendCmykLabeled)と揃える(2026-07-14)。
static void KESCMBuildCmykNoValuePanel(PMString& out, bool16 hoverIsTarget)
{
	out.Clear();
	out.SetTranslatable(kFalse);
	out.Append(hoverIsTarget ? "C--- M--- Y--- K--- t" : "C--- M--- Y--- K--- s");
	out.AppendW(UTF32TextChar(0x0A));
	out.Append(hoverIsTarget ? "C--- M--- Y--- K--- s" : "C--- M--- Y--- K--- t");
}

// 単独ピック(Stop 中、または Start 中の第3の文書)用の「値なし」1行版。ラベル(t/s)なし=1文書のみ。カーソル側は
// KESCMSplitTwoLines が空の2行目を自動スキップするので、1行渡すだけで崩れない。
static void KESCMBuildCmykNoValueSolo(PMString& out)
{
	out.Clear();
	out.SetTranslatable(kFalse);
	out.Append("--- --- --- ---");
}
static void KESCMBuildCmykNoValuePanelSolo(PMString& out)
{
	out.Clear();
	out.SetTranslatable(kFalse);
	out.Append("C--- M--- Y--- K---");
}

// 修飾キー→ジェスチャの分類(KESCMPeek.h 参照)。★割当の定義はここ1本だけ: トラッカーの押下時分岐
// (KESCMTracker.cpp)・下の RevealBegin の分岐・temp-hide 判定がすべてこれを使う(2026-07-15 統合)。
KESCMGesture KESCMClassifyGesture(bool16 shiftDown, bool16 altDown, bool16 cmdDown, bool16 macCtrlDown)
{
	// Ctrl(cmd)を伴う左ボタンは未割当。再比較はページ右クリックメニューへ移設済み、パネル操作は
	// フライアウトへ移行済みで、いずれもトラッカーは扱わない。
	// ★Mac の Control も未割当(2026-07-25 追補): macOS では Control+クリックが副ボタン(コンテキスト
	//   メニュー)の標準ジェスチャなので、左ボタン押下として届いても reveal を横取りしない。
	//   MacCtrlDown() は Windows では常に kFalse なので Windows の挙動は不変。
	if (cmdDown || macCtrlDown)
		return kKESCMGestureNone;
	if (altDown && !shiftDown)
		return kKESCMGestureCmyk;		// Alt 単独: CMYK 色サンプリング
	if (shiftDown && altDown)
		return kKESCMGesturePeek50;		// Shift+Alt: 旧版べた載せ 50%
	if (shiftDown)
		return kKESCMGesturePeek100;	// Shift: 旧版べた載せ 100%
	return kKESCMGestureReveal;			// 修飾なし: reveal / temp-hide
}

void KESCMTrackerRevealBegin(bool16 shiftDown, bool16 altDown, bool16 cmdDown, bool16 macCtrlDown)
{
	sCmykCursorPending = kFalse;	// このプレスで CMYK カーソルを出すかは下の Cmyk 分岐で決める(既定=出さない)

	const KESCMGesture gesture = KESCMClassifyGesture(shiftDown, altDown, cmdDown, macCtrlDown);
	if (gesture == kKESCMGestureNone)
		return;	// 未割当(Ctrl/Command 系、Mac の Control)。トラッカーはキャプチャ済みだが描画状態は変えない。

	// ---- 「Hold to Hide Marks」モード(常時表示の極性反転)の窓別 temp-hide ----
	// 隠すジェスチャ=reveal と peek(修飾なし/Shift/Shift+Alt)。
	// ★CMYK(Alt 単独)は隠さない=枠を出したままサンプリング(旧・中ボタン Shift+Ctrl+Alt でも枠は
	// 隠れない仕様に一致)。押した窓の枠だけを隠す(Target/Source 別)。
	const bool16 tempHideGesture = (gesture != kKESCMGestureCmyk);
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
	if (gesture == kKESCMGestureCmyk)
	{
		// Alt+左(単独、Shift/Ctrl なし): クリック点の CMYK 生値(0..255)をサンプリングしカーソル自身に描画する。
		// ★発火条件=「マウス下にレイアウトビュー+文書がある」だけ(2026-07-26 にユーザー指定で拡張。以前は
		//   Start 中は Target 窓に限っていた)。押した窓で3通りに分岐する:
		//   Start 中・Target 窓  … 新・旧を比較(2行。1行目=Target "t" / 2行目=Source "s")
		//   Start 中・Source 窓  … 同じく比較だが向きが逆(1行目=Source "s" / 2行目=Target "t")
		//   Start 中・第3の文書 / Stop 中 … その1文書を単独ピック(1行、ラベルなし)
		// ★このブロックは基底 CTracker::BeginTracking より前に呼ばれる(KESCMTracker.cpp)。重いサンプリングを
		//   カーソル切替の前で終わらせ、切替を一瞬にするため(押下時のゴミ対策 2026-07-25)。
		InterfacePtr<IControlView> viewUnderMouse(KESCMQueryViewUnderMouse());
		IDataBase* const hoverDB = KESCMFindDocDbForView(viewUnderMouse);
		if (hoverDB != nil)
		{
			// カーソル再描画毎(≦20回/秒)の IFontMgr 名前引きを回避する押下中フォントキャッシュ(解放は RevealEnd)。
			if (sCmykCursorFont == nil)
			{
				InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), UseDefaultIID());
				sCmykCursorFont = (fontMgr != nil) ? fontMgr->QueryFont(fontMgr->GetDefaultFontName()) : nil;
			}

			// 押した窓の文書を Target / Source / それ以外 に分類してモードを固定する(解除は RevealEnd)。
			// 比較モードのときだけページ対応表キャッシュを用意する(サンプル毎の全ページ pairing 再構築を
			// 回避。向きも押下時のまま固定。破棄は RevealEnd)。単独モードはページ対応が無いので不要。
			IDataBase* otherDB      = nil;
			bool16     hoverIsTarget = kFalse;
			if (KESCMArmedDocsAlive())	// 比較中か(解放済み db との照合を避けるため生存検査を先に通す)
			{
				if (hoverDB == sPeekTargetDB)      { otherDB = sPeekSourceDB; hoverIsTarget = kTrue;  }
				else if (hoverDB == sPeekSourceDB) { otherDB = sPeekTargetDB; hoverIsTarget = kFalse; }
			}
			sCmykHoverDB       = hoverDB;
			sCmykOtherDB       = otherDB;
			sCmykHoverIsTarget = hoverIsTarget;

			const bool16 solo = (otherDB == nil);
			if (!solo)
				KESCMSampleCmykBeginDrag(hoverDB, otherDB, hoverIsTarget);

			PMString panelMsg, cursorMsg;
			if (!KESCMSampleCmykUnderMouse(hoverDB, otherDB, hoverIsTarget, panelMsg, cursorMsg))
			{
				// ページ外など: 拾えないことを示す(値なし --- 表示)。
				if (solo) { KESCMBuildCmykNoValueSolo(cursorMsg); KESCMBuildCmykNoValuePanelSolo(panelMsg); }
				else      { KESCMBuildCmykNoValue(cursorMsg, hoverIsTarget);
				            KESCMBuildCmykNoValuePanel(panelMsg, hoverIsTarget); }
			}
			// カーソル自身に CMYK を描く(トラッカーが ChangeModalCursor する)のに加えて、パネルのステータス行にも
			// 同じ値を出す。★KESCMSetStatus はパネルが非表示でも「強制的に表示」はしない(ON→表示中なら見える、
			// OFF→隠れたまま状態だけ覚える)。パネルを強制的に開かせることはしない(ユーザー指定)。
			KESCMSetStatus(panelMsg);
			sCmykCursorText    = cursorMsg;
			sCmykCursorPending = kTrue;
		}
		return;
	}
	if (gesture == kKESCMGesturePeek50)
	{
		// Shift+Alt+左: 旧版べた載せ peek を 50% で(旧・中ボタン Shift+Alt+ミドル)。
		KESCMTrackerBeginPeek(kKESCMPeekSemiOpacity);
		return;
	}
	if (gesture == kKESCMGesturePeek100)
	{
		// Shift+左: 旧版べた載せ peek を 100% 不透明で(旧・中ボタン Shift+ミドル)。
		KESCMTrackerBeginPeek(PMReal(1.0));
		return;
	}

	// ---- 修飾なし: 通常モードのマーク一時表示(reveal) ----
	// Hold to Hide モード中は上で temp-hide 済み=ここでは何もしない(reveal はしない)。
	if (KESCMDrawEventHandler::sAlwaysShowMarks)
		return;

	// 「マークがある」の判定は旧・中ボタンの修飾なし分岐と同一(anyMarkableContent 相当)。
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
	// 出さない。旧・中ボタンと同じ方針)。
	if (!KESCMFrontViewIsOverTarget())
		return;

	sSingleShowing = kTrue;
	KESCMDrawEventHandler::sMarkScreenOpacity = KESCMDrawEventHandler::SelectedMarkOpacity();	// パネルの 25%/75%
	KESCMDrawEventHandler::sMarksVisible = kTrue;	// 押下中だけ枠等を表示
	KESCMInvalidateMarksDoc();
}

void KESCMTrackerRevealEnd()
{
	// Alt+左(CMYK)の押下中キャッシュを返す/捨てる(取得は RevealBegin の Cmyk 分岐。押下の外では持たない)。
	if (sCmykCursorFont != nil)
	{
		sCmykCursorFont->Release();
		sCmykCursorFont = nil;
	}
	KESCMSampleCmykEndDrag();
	// 押下中に固定していた CMYK モード(hover/other)の保持を解除(押下の外では持たない)。
	sCmykHoverDB       = nil;
	sCmykOtherDB       = nil;
	sCmykHoverIsTarget = kFalse;

	// Alt+左(CMYK 色比較)を離したら、押下中にパネルのステータス行へ出していた CMYK 値を消す
	// (ユーザー要望 2026-07-15: ホールド終了でメッセージは消す)。sCmykCursorPending は押下中に
	// CMYK 値を出したときだけ立つので、色比較のときだけクリアし、reveal/peek や Check/Register 等
	// 他機能のステータスには触らない。
	// ★クリアは空文字ではなく「空白1文字」で行う(ユーザー指定 2026-07-15): 完全な空だと
	//   gSessionStatus が「未操作」と区別できず、次回パネルを開いたときに AutoAttach の
	//   CharCount()==0 判定で初回ヒントが再表示されてしまう。空白1文字なら見た目は空のまま
	//   「操作済み」を保てる(再表示でも空白が復元されるだけでヒントは出ない)。
	if (sCmykCursorPending)
	{
		PMString blank(" ");
		blank.SetTranslatable(kFalse);
		KESCMSetStatus(blank);
		sCmykCursorText.Clear();
		sCmykCursorPending = kFalse;
	}

	// 「Hold to Hide Marks」で押下中に隠していた常時表示の枠を戻す(離すと再表示)。押した窓に応じて
	// Target/Source どちらか(または両方)が立っている。モード OFF なら両方 kFalse なので無影響
	// (旧・中ボタン解放時の temp-hide 復元と同一)。
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
		// (再 peek は即時)。旧・中ボタン解放時の sPeekActive 復元と同一。
		sPeekActive = kFalse;
		if (KESCMDrawEventHandler::sShowOriginal)
		{
			KESCMDrawEventHandler::sShowOriginal = kFalse;
			KESCMInvalidateDB(sPeekTargetDB);
		}
	}
	else if (sSingleShowing)
	{
		// 通常モードの reveal 解除 → 枠表示を解除し、不透明度を基準値へ戻す＋非表示へ(旧・中ボタン解放時の
		// sSingleShowing 復元と同じ)。
		sSingleShowing = kFalse;
		KESCMDrawEventHandler::sMarksVisible = kFalse;
		KESCMDrawEventHandler::sMarkScreenOpacity = KESCMBaseScreenOpacity();
		KESCMInvalidateMarksDoc();
	}
}

//========================================================================================
// 一括クローズ(複数文書を続けて閉じる / アプリ終了の close-all)の後片付けを1回に畳む
//
//   kAfterCloseDoc は「閉じた文書ごと」に飛ぶ。そのたびに KESCMHandleDocsClosed が UI の後片付け
//   (スクロール地図 strip の撤去・InvalidateViews・サムネイル再生成の予約・パネル/ステータス更新)
//   まで行うと、N 文書を一度に閉じたときに N 回走る。状態(メモリ)の破棄はその場で行い、UI 側だけ
//   保留して、全部閉じ終わったところで1回だけ流す(=「集めてから1回」)。解体が進む場面で widget に
//   触る回数が減るので、終了時の堅牢性(特に Mac)にも効く。
//
//   ★「今どれかの文書が閉じている最中か」と「全部閉じ終わった」は本体(Links UI プラグイン)が
//     公開しており、こちらは読むだけでよい(公開ヘッダー LinksUIID.h):
//       ・IID_IKFILESCLOSING        = セッション boss 上の IBoolData(閉じ始めに kTrue、全部閉じたら kFalse)
//       ・kPendingDocumentsClosedMsg = 全部閉じ終わった瞬間にアプリの subject へ飛ぶ通知
//   ★Links UI が無い/無効な環境ではフラグを引けない。その場合は保留せず、従来どおり毎回その場で
//     片付ける(フォールバック=挙動は元のまま)。
//========================================================================================

static bool16 sDeferredCloseUiPending = kFalse;	// UI の後片付けを保留中か(完了通知で1回だけ流す)

// いま一括クローズの最中か(本体が管理するセッションフラグを読むだけ。引けなければ kFalse)。
static bool16 KESCMBatchCloseInProgress()
{
	ISession* session = GetExecutionContextSession();	// 終了処理中は nil になり得る
	if (session == nil)
		return kFalse;
	InterfacePtr<IBoolData> filesClosing(session, IID_IKFILESCLOSING);
	return (filesClosing != nil && filesClosing->GetBool()) ? kTrue : kFalse;
}

// 保留していた UI の後片付けを1回だけ流す(一括クローズ完了時)。
static void KESCMFlushDeferredCloseUi()
{
	if (!sDeferredCloseUiPending)
		return;
	sDeferredCloseUiPending = kFalse;

	if (KESCMAppIsQuitting())
		return;		// 終了中は UI に触らない(状態は Shutdown が破棄する)

	// Find Overset が(走査文書が生存したまま)単独 ON なら地図は残す。それ以外は撤去する
	// (KESCMHandleDocsClosed 側で即時に行っていた処理と同じ判断)。
	if (KESCMDrawEventHandler::sOversetOn)
		KESCMScrollMapInvalidateAll();
	else
		KESCMScrollMapDetachAll();

	PMString s("marks cleared");	// Stop ボタン(DoClear)と同じメッセージ
	s.SetTranslatable(kFalse);
	KESCMSetStatus(s);

	// ★生き残っている文書は、保留した時点のものと同じとは限らない(一括クローズなので、その後さらに
	//   閉じられている)。閉じた db を持ち越さないよう、控えたポインタは使わず「今開いている文書」を
	//   その場で列挙する。マークは既に破棄済みなので、無関係な文書を再描画しても枠は描かれない。
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList != nil)
	{
		const int32 nDocs = docList->GetDocCount();
		for (int32 i = 0; i < nDocs; ++i)
		{
			IDocument* doc = docList->GetNthDoc(i);
			if (doc == nil)
				continue;
			IDataBase* db = ::GetUIDRef(doc).GetDataBase();
			KESCMInvalidateDB(db);
			KESCMScheduleThumbRefresh(db);	// 遅延サムネイル再生成(同じ db は集約される)
		}
	}

	KESCMRefreshPanel();
}

/** 一括クローズ完了(kPendingDocumentsClosedMsg)を受けるだけのオブザーバ。.fr の AddIn で
    kActiveContextBoss に IID_IKESCMDOCSCLOSEDOBSERVER として同居させている(同居先の理由は上の
    レイアウト同期オブザーバと同じ=実証済みの構成)。購読先はアプリの subject。 */
class KESCMDocsClosedObserver : public CObserver
{
public:
	KESCMDocsClosedObserver(IPMUnknown* boss) : CObserver(boss, IID_IKESCMDOCSCLOSEDOBSERVER) {}
	~KESCMDocsClosedObserver() {}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);
};

CREATE_PMINTERFACE(KESCMDocsClosedObserver, kKESCMDocsClosedObserverImpl)

void KESCMDocsClosedObserver::Update(const ClassID& theChange, ISubject* /*theSubject*/, const PMIID& protocol, void* /*changedBy*/)
{
	if (protocol == IID_IAPPLICATION && theChange == kPendingDocumentsClosedMsg)
		KESCMFlushDeferredCloseUi();
}

// アプリ subject への購読を付ける(Startup から1回)。アプリもオブザーバの同居先も
// セッションと同じ寿命なので、終了時に明示 detach はしない(detach 自体がクラッシュ要因になる。
// レイアウト同期オブザーバの Shutdown 方針と同じ)。
static void KESCMAttachDocsClosedObserver()
{
	ISession* session = GetExecutionContextSession();
	IActiveContext* ctx = (session != nil) ? session->GetActiveContext() : nil;
	if (ctx == nil)
		return;
	InterfacePtr<IObserver> obs((IObserver*)ctx->QueryInterface(IID_IKESCMDOCSCLOSEDOBSERVER));
	if (obs == nil)
		return;
	InterfacePtr<IApplication> app(session->QueryApplication());
	InterfacePtr<ISubject> subject(app, IID_ISUBJECT);
	if (subject == nil)
		return;
	if (!subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IAPPLICATION, IID_IKESCMDOCSCLOSEDOBSERVER))
		subject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IAPPLICATION, IID_IKESCMDOCSCLOSEDOBSERVER);
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
	// ★レイアウトビュー同期は既定 OFF(ユーザー指定 2026-07-24。旧・既定 ON を撤回)。sLayoutSyncOn は
	// 初期値 kFalse なので、ここで明示的に ON にしなければ OFF のまま。保存済み設定(下の読み込み)で
	// syncLayoutViews=true を復元したユーザーだけ ON になる。

	// ★続けて保存済みパネル設定(独自 JSON)をここで読み込む(ユーザー指定 2026-07-15)。
	// 同期は Stop 中でもトグル ON なら動くため、「パネル初回オープン時に復元」の従来タイミングだと、
	// ON を保存したユーザーは起動〜パネルを開くまでの間だけ同期が止まってしまう。起動時に読み込めば
	// その窓が無くなる(保存が無ければ上の既定 OFF のまま)。
	// 各トグルの復元先は全部エンジン側のフラグ/購読で、パネルにも文書にも依存しない=起動時に安全
	// (KESCMDoSetPrintMarks は db=nil のフラグのみ、ScrollMap/IgnoreMarker/HoldToHide 等は平の代入)。
	// 内部の「セッション一度きり」ガードにより、パネル AutoAttach からの既存呼び出しは no-op のまま残る
	// (起動サービスの順序が万一変わっても取りこぼさない保険)。
	KESCMLoadPanelStateIfPresent();

	// 一括クローズ完了(kPendingDocumentsClosedMsg)の購読を開始する。以後、複数文書を続けて閉じても
	// UI の後片付けは「全部閉じ終わってから1回」に畳まれる(上の集約ブロック参照)。
	KESCMAttachDocsClosedObserver();
}

void KESCMPeekStartup::Shutdown()
{
	// 遅延サムネイル更新の idle task を解放(予約中なら RemoveTask してから)。
	KESCMShutdownThumbIdleTask();
	// 一括クローズの保留も捨てる(終了後に流れることは無いが、状態を残さない)。
	sDeferredCloseUiPending = kFalse;
	// HUD(sprite)の one-shot タイマーとフォント参照も確実に返す(生関数ポインタを残さない)。
	KESCMTrackerShutdownHud();
	// 保持していたマーク/旧版画像バッファを解放(終了時もきれいに片付ける)。
	KESCMDrawEventHandler::DropAll();
	KESCMDrawEventHandler::DropAllOrig();
	// ★残りの静的コンテナも同じ方針で空にする(2026-07-25 監査の積み残しを同日の追補で対応)。
	//   DropAll/DropAllOrig は比較系(sEntries/sOrigImages/対応表/overflow)しか触らないため、
	//   Find Overset の集合・登録(Add/Remove)・チェック(✓)・Hide Unchanged の控えは
	//   プラグイン unload 時の静的デストラクタまで heap を持ち越していた。Windows では実害なしの
	//   実績だが、Mac は unload 順が異なるので「生きたバッファを静的破棄まで残さない」方針
	//   (file-static PMString を Clear するのと同じ理由)へ揃える。
	//   いずれもポインタは deref せず、コンテナを空にするだけ=終了処理中でも安全。
	KESCMDrawEventHandler::DropOverset();	// sOversetPages / sOversetLocs
	KESCMPageMapClearAllDocs();				// 登録(Added/Removed)
	KESCMPageCheckClearAllDocs();			// 「KESCM: Check」の✓
	KESCMResetHideUnchanged(kFalse);		// Hide Unchanged の控え(kFalse=文書には一切触らない)
	KESCMInvalidateSyncCaches();			// 同期のページ矩形表・対応表・前回状態(2026-07-25 追補)
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

	// ★file-static PMString を空にして、プラグイン unload 時の静的デストラクタを実質 no-op にする
	// (Windows では実害なしの実績だが、Mac は unload 順が異なるため heap バッファを持ち越さない方が
	// 安全。2026-07-15 終了堅牢化)。
	sCmykCursorText.Clear();
	KESCMClearSessionStatus();	// パネルのステータス記憶(gSessionStatus)も同様に空へ

	// ★Alt+左ホールド中にアプリが終了する経路(スクリプト quit 等)では RevealEnd を通らず
	//   sCmykCursorFont が生きたまま残るので、ここで解放する(2026-07-25 監査で追加。通常経路では
	//   押下の外は常に nil なので no-op)。
	if (sCmykCursorFont != nil)
	{
		sCmykCursorFont->Release();
		sCmykCursorFont = nil;
	}
	// 同じ経路で残りうる押下中モードの文書ポインタも捨てる(deref しない照合専用だが、
	// 終了後に解放済みポインタを持ち越さない。2026-07-26)。
	sCmykHoverDB       = nil;
	sCmykOtherDB       = nil;
	sCmykHoverIsTarget = kFalse;
	KESCMSampleCmykEndDrag();	// 押下中のページ対応表キャッシュ(hover→other)も同様に破棄

	// 旧ページ番号バッジのフォントキャッシュも同じ理由(静的破棄前の明示解放)でここで捨てる(2026-07-25)。
	KESCMReleaseOldNumFontCache();
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

	KESCMInvalidateSyncCaches();	// 比較対象の組み合わせが変わる=同期キャッシュは作り直し(2026-07-25 追補)

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

	KESCMInvalidateSyncCaches();	// 対象が無くなる=同期キャッシュを捨てる(2026-07-25 追補)

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
	// session は終了処理中に nil になり得る(2026-07-25 追補 に KESCM 全体で統一)。引けなければ
	// 生存判定そのものができないので、何も片付けずに戻る(状態は Shutdown が破棄する)。
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return;

	// 文書が閉じた=ページ構成も db ポインタも当てにならないので、同期キャッシュは無条件に捨てる
	// (2026-07-25 追補。コンテナを空にするだけ=deref しないので終了処理中でも安全)。
	KESCMInvalidateSyncCaches();

	// ★終了堅牢化(2026-07-15): アプリが終了処理中(kQuitting/kShuttingDown)にこのレスポンダへ来たら、
	// UI 仕事(strip の widget 除去・InvalidateViews・サムネイル idle 予約・パネル/ステータス更新)を
	// 全てスキップし、状態(メモリ)の破棄だけにする。終了中のウィンドウ/パネル解体順はプラットフォーム
	// 依存で、特に Mac(Cocoa)は Windows と異なるため、解体中の widget へ触るのが Mac 限定
	// crash-on-quit の典型形。通常の quit は close-all(まだ kRunning)→Terminate の順なので、対話的な
	// クローズと quit の close-all フェーズでは従来どおりフルクリーンアップが走る(挙動変更なし)。
	const bool16 quitting = KESCMAppIsQuitting();

	// ★一括クローズ(複数文書を続けて閉じる)の最中は、UI の後片付けを保留して全部閉じ終わってから
	//   1回だけ流す(2026-07-27)。状態(メモリ)の破棄は保留せずその場で行うので、閉じた db を持ち越さない
	//   従来どおりの安全性は保たれる。フラグを引けない環境(Links UI 無効)では kFalse=従来動作。
	const bool16 deferUi = !quitting && KESCMBatchCloseInProgress();
	const bool16 doUiNow = !quitting && !deferUi;

	// ★Find Overset(比較とは独立): 走査した文書が閉じていたら十字状態を捨てる。sOversetDB は描画時に
	//   ポインタ一致だけを見る(deref しない)が、閉じたまま残すと別文書へアドレスが再利用された時に
	//   誤って十字を描き得るので、ここで能動的に掃除する。メモリ破棄のみ=終了中(quitting)でも安全。
	//   他文書には元々出していないので再描画も不要(閉じた文書の十字は窓ごと消える)。
	if (KESCMDrawEventHandler::sOversetOn && KESCMDrawEventHandler::sOversetDB != nil &&
	    docList->FindDocByDataBase(KESCMDrawEventHandler::sOversetDB) == nil)
	{
		KESCMDrawEventHandler::DropOverset();
	}

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
		//   ★終了中(quitting)はスキップ: 解体中の窓の widget 除去/SetFrame が危険な上、窓ごと消えるので
		//   取り外す意味も無い(以下の UI 仕事も同様にスキップ)。
		if (doUiNow)
		{
			// ★Find Overset が(走査文書が生存したまま)単独 ON 中なら地図を残す(赤帯だけ描き直す)。
			//   overset 文書自身が閉じた場合は上(1586 付近)で DropOverset 済み=sOversetOn が false なので
			//   通常どおり撤去される(2026-07-24)。
			if (KESCMDrawEventHandler::sOversetOn)
				KESCMScrollMapInvalidateAll();
			else
				KESCMScrollMapDetachAll();
		}
		changed = kTrue;

		if (doUiNow)
		{
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
			// ★終了中は kFalse=スプレッド再表示コマンド(kHideSpreadCmdBoss)を打たず状態だけ捨てる。
			// 終了のティアダウン中にモデル変更コマンドを流すのは危険(通常 quit では close-all が
			// kRunning 中に済むので、この kFalse 経路に来るのは異常系のみ=挙動変更なし)。
			KESCMResetHideUnchanged(quitting ? kFalse : kTrue);
			changed = kTrue;
		}
	}

	// 「比較相手なしページ」登録(KESCMPageMap)の後片付け: 閉じた文書の分を状態だけ捨てる
	// (deref なし。パネル表示には関与しないので changed は立てない)。
	KESCMPageMapSweepClosedDocs();
	KESCMPageCheckSweepClosedDocs();	// 「KESCM: Check」の✓も、閉じた文書の分を状態だけ捨てる(deref なし)

	// 何か片付けたらパネルの ON/OFF 表示を実状態に合わせる(①「ON 固着」の解消)。
	// ★終了中はパネル widget へ触らない(パネルも解体中の可能性がある)。
	// ★一括クローズ中は保留し、全部閉じ終わった通知でまとめて流す(上の集約ブロック)。
	if (changed)
	{
		if (doUiNow)
			KESCMRefreshPanel();
		else if (deferUi)
			sDeferredCloseUiPending = kTrue;
	}
}
