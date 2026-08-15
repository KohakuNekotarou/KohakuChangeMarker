//========================================================================================
//
//  KESCMPeek.cpp
//
//  ツール(左ボタン)peek の実装(KESCMScriptProvider.cpp から分離)。peek 状態、旧版べた載せの表示、
//  選択ページの部分再比較、起動/終了サービス、KESCMCore.h で宣言した arm/disarm/状態アクセサの入口を
//  持つ。旧・中ボタンの IEventWatcher は撤去済み(2026-07-13)。
//
//  ★2026-08-13 の model/UI 分割 第1段 Task 1 で、UI 側の3領域がここから出ていった:
//    ・ビューポート同期(Sync Layout Views / Align Other Views) → KESCMViewSync.cpp
//    ・Alt+左の CMYK カーソル                                   → KESCMCmykCursor.cpp
//    ・ジェスチャ判定と押下中の表示切替(RevealBegin/End)        → KESCMPeekGesture.cpp
//    出ていった側が持つ状態(押下中の表示・CMYK・同期キャッシュ)は、それぞれのファイルの中で閉じている。
//    ここから触るときは各ヘッダーが公開している入口を呼ぶ(下の Shutdown / arm / disarm が実例)。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// オブジェクトモデル:
#include "PersistUtils.h"
#include "IDataBase.h"
#include "IDocument.h"
#include "IApplication.h"
#include "IDocumentList.h"
#include "ISpread.h"
#include "ISession.h"

// ツール / 起動:
#include "IStartupShutdownService.h"
#include "CPMUnknown.h"
#include "LayoutUIID.h"
#include "DocumentContextID.h"

// ジオメトリ:
// ★2026-08-15(第2段 Task 4B): IControlView.h / IPanorama.h / PMMatrix.h を落とした。
//   ビューとパノラマを引いてズームを読む3行が呼び手(UI)へ出たので、このファイルには使い手が居なくなった。
#include "PMPoint.h"
#include "PMReal.h"

// 選択ページ再比較の進捗バー:
#include "ProgressBar.h"			// TaskProgressBar(多ページの Refresh に進捗＋キャンセル)
#include "ErrorUtils.h"				// PMSetGlobalErrorCode(キャンセル後にエラーを持ち越さない)

#include <map>
#include <vector>

// プロジェクト内インクルード:
#include "KESCMID.h"
#include "KESCMConstants.h"
#include "KESCMDrawEventHandler.h"   // エンジンの共有 static
                                     // (★「＋ KESCMQueryPanorama」と書いてあったが、それは 2026-08-13 に
                                     //  KESCMViewLookup.h へ移った後の陳腐化コメント。2026-08-15 に訂正)
#include "KESCMCore.h"               // arm/disarm/状態 宣言
#include "KESCMModelNotify.h"	// KESCMNotifyStatus - the model tells the UI, it never calls it (Task 9)
// ★★2026-08-15(第2段 Task 4B): **KESCMViewLookup.h の include を落とした**。ここが最後まで残っていた
//   model→UI の逆流2件のうちの1本(もう1本は KESCMColorSampler.cpp)。KESCMQueryViewUnderMouse /
//   KESCMQueryMouseContentPoint / KESCMQueryPanorama の3本を呼んでいた。
//   ⇒ ビュー解決は呼び手(UI)へ出し、この .cpp は「渡された点のスプレッドを覗く」だけを担う。
#include "KESCMPageMap.h"            // KESCMBuildPairing(比較の除外対応表)/KESCMPageMapReadSelection/KESCMPageMapSweepClosedDocs
#include "KESCMPageCheck.h"          // KESCMPageCheckClearAllDocs / KESCMPageCheckSweepClosedDocs(✓の後片付け)
#include "KESCMPageNumberMarker.h"   // KESCMInvalidatePageNumberMarkerRects(ノンブル除外矩形キャッシュの破棄)
// (★KESCMPanelState.h / KESCMPanelAlpha.h / KESCMTrackerHud.h / KESCMCmykCursor.h は 2026-08-13
//  Task 8 で外した＝起動/終了の UI の仕事ごと KESCMUIStartup.cpp へ移したため)
// (★★KESCMThumbnailRefresh.h / KESCMScrollMap.h / KESCMChangeNav.h / KESCMThumbIdleTask.h /
//  KESCMViewSync.h / KESCMPeekGesture.h は 2026-08-13 Task 10 で外した＝サムネイル・地図・Prev/Next・
//  遅延再生成・同期キャッシュ・覗き状態は全部 UI の持ち物で、通知(KESCMNotifyDocs)を受けた
//  KESCMModelChangeObserver がやるようになった)
#include "KESCMStoryList.h"          // KESCMStoryList::ShutdownCleanup(行が抱える PMString を終了時に手放す)
#include "KESCMHideUnchanged.h"      // KESCMResetHideUnchanged / 隠している文書の getter(2026-08-13 に移動)
#include "KESCMPeek.h"

//========================================================================================
// ツール(左ボタン)peek — 共有状態とヘルパ。
//   ツール左ボタンを押している間だけ、マウス下スプレッドの旧版を不透明べた載せし、離すと隠す。
//   比較相手の旧ドキュメントは先に KESCMDoArmMousePeek(KESCMCore.h)で登録しておく(パネルの Start
//   ボタンが呼ぶ)。トラッカー入口(KESCMTrackerRevealBegin/End。KESCMPeekGesture.cpp)がこの arm 状態を
//   見る。
//========================================================================================
static IDataBase* sPeekTargetDB = nil;	// 表示中(新)ドキュメント。使用前に「まだ開いているか」を検証する。
static IDataBase* sPeekSourceDB = nil;	// peek 中に重ねる旧ドキュメント。
static bool16     sPeekArmed    = kFalse;

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

// **渡された点**のスプレッドの旧版べた載せを表示する。
//   targetDB=表示中(新)ドキュメント, sourceDB=重ねる旧ドキュメント。
//   そのスプレッドが既にキャッシュ済みなら再利用(即時)。未キャッシュなら旧キャッシュを捨てて、その
//   スプレッドだけをその場でラスタ化(保持は常に1スプレッド)。成功時に sShowOriginal を立てて再描画。
//   (2026-07-25 監査: 旧・中ボタン watcher/スクリプト報告用の戻り値 KESCMPeekResult と
//    outSpread/outPages は、唯一の呼び出し側が全て捨てていたため撤去して void 化)
//   ★2026-08-13: 呼び手(KESCMTrackerBeginPeek)が KESCMPeekGesture.cpp へ移ったので static を外した。
//     宣言は KESCMPeek.h。
//   ★★2026-08-15(第2段 Task 4B): **ビュー解決3本を呼び手(UI)へ出して座標と倍率を引数で受け取る形にした**
//     (旧 KESCMPeekShowUnderMouse)。落としたのは「どのビューか・その倍率は・マウスはどこか」の**観測**だけで、
//     「その倍率をどの dpi に翻訳するか」の**方針**(下限 50% の頭打ち・16〜300dpi クランプ)はここに残した
//     ＝計算式は1文字も動いていない。引数の意味は KESCMPeek.h を参照。
void KESCMPeekShowAt(IDataBase* targetDB, IDataBase* sourceDB,
                     const PMReal& mx, const PMReal& my,
                     const PMReal& viewScale, const PMReal& uiZoom)
{
	if (targetDB == nil || sourceDB == nil)
		return;

	// 呼び手が測ったズーム(content→window スケール=ズーム×デバイス倍率)から、画面と 1:1 になる解像度を決める。
	// dpi = 72 × スケール。1:1 のとき最も綺麗(画像px=画面px)。
	PMReal curScale = abs(viewScale);
	if (curScale <= 0) curScale = 1.0;

	// 【低ズームの下限=UI 50%】UIズーム(ユーザーに見える拡大率, デバイス倍率を含まない)が 50% を下回る時は
	// 「50% 相当の解像度」で頭打ちにする。50%以上は画面と 1:1 のままくっきり。50%未満は画像が画面より高精細に
	// なり、縮小blit(点サンプリング)で多少粗くなる(=10% などは汚くてよい、という方針)。下限を UI% で決めるので
	// デバイス倍率に依らず、画面に見える 50% がそのまま境界になる。パノラマ不明時は 1:1(従来=全ズーム綺麗)
	// ＝呼び手はその場合 uiZoom に 0 を渡す(下の if を素通りして effScale = curScale のまま)。
	PMReal effScale = curScale;
	if (uiZoom > 0)
	{
		const PMReal deviceScale = curScale / uiZoom;			// 画面デバイス倍率(=curScale/uiZoom)
		const PMReal flooredZoom = (uiZoom < PMReal(0.5)) ? PMReal(0.5) : uiZoom;	// UI 50% で頭打ち
		effScale = flooredZoom * deviceScale;
	}

	PMReal peekDpi = PMReal(72.0) * effScale;
	if (peekDpi < 16.0)  peekDpi = 16.0;	// 安全下限(degenerate 回避。通常は効かない)
	if (peekDpi > 300.0) peekDpi = 300.0;	// 過大メモリ防止(300dpi A4 ≒ 35MB/頁)

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
		const PMReal d = abs(effScale - KESCMDrawEventHandler::sOrigScale);
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
// 更新する。source 対応は除外対応表(通常ページ=登録済みを除いた順番対応 / マスター=名前対応。
// 2026-08-13 にマスターぶんを合流させた)で引く。
//   ・各ページを MakeEntry で取り直し(編集後の差分に更新)。変化が無くなったページは古い枠を消す。
//   ・旧版画像キャッシュ(sOrigImages)は古いので破棄(次の peek で作り直し)。
//   ・✓ の剪定/レイアウト・スクロールバー地図・Pages パネルサムネイルの更新まで行う。
//   実際に再比較したページ数(対応表に無い登録済みページ等の skip を除く)を outProcessed に、
//   うち変化ページ数を outChanged に返す。戻り=1ページ以上処理したか。
//   ★ページ数が多いときは進捗バー＋キャンセルを出す(2026-07-27)。キャンセルされたら outCancelled に
//     kTrue を返し、「そこまで更新した分は残して」止める(Start の比較と違い全部は捨てない。理由は
//     下のループ内コメント)。更新済みページの反映(✓剪定・再描画・サムネイル)は中断時も行う。
//   ★旧 Ctrl+ミドル(マウス下スプレッド再比較)の中核をページ指定へ一般化したもの(2026-07-13 移設)。
static bool16 KESCMRefreshComparisonCore(IDataBase* targetDB, IDataBase* sourceDB,
                                         const std::vector<UID>& targetPages,
                                         int32* outProcessed, int32* outChanged, bool16* outCancelled,
                                         int32* outFailed)
{
	if (outProcessed) *outProcessed = 0;
	if (outChanged)   *outChanged = 0;
	if (outCancelled) *outCancelled = kFalse;
	if (outFailed)    *outFailed = 0;
	if (targetDB == nil || sourceDB == nil || targetPages.empty())
		return kFalse;

	// ★ラスタ化は未組版ストーリーの lazy recompose を誘発し得る=組めば dirty になる。Start 経路
	//   (KESCMDoMarkChangesDoc)と同じく、入る前が clean なら出るとき clean へ戻す(2026-08-06 再点検)。
	IDataBase::SaveRestoreModifiedState targetDirtyGuard(targetDB);
	IDataBase::SaveRestoreModifiedState sourceDirtyGuard(sourceDB);

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

	// ★マスタースプレッドの対応も同じ表に入れる(2026-08-13)。ページ UID は文書内で一意なので通常
	//   ページの対応と1つの map に同居できる(Sync 側の KESCMEnsureSyncPairing と同じ形)。
	//   これを入れるまで、マスターページを選んで Refresh すると対応表に無く「対象0件」で黙って
	//   何も起きなかった——2026-08-11 に比較(KESCMDoMarkChangesDoc)がマスターを扱うようになり、
	//   **枠は出るのに部分再比較だけ届かない**という食い違いになっていた。
	//   ★比較の対応表(KESCMCore.cpp)と同じ2本立て(通常=順番対応 / マスター=名前対応)を通す。
	{
		std::vector<UID> mT, mS;
		KESCMBuildMasterPairing(targetDB, sourceDB, mT, mS);
		for (size_t k = 0; k < mT.size(); ++k)
			targetToSource[mT[k]] = mS[k];
	}

	// 指定ページを再比較して枠を更新。触れたページ(target とその source 対応)を集めておき、後で Pages
	// パネルのサムネイルを per-UID Purge する。変化あり/なしの両方を入れる=変化なしに戻って sEntries から
	// 外れたページも古いリングを確実に消せるようにするため。
	// ★実際に比較するページを先に確定する(進捗バーの総数に使う。対応表に無い=登録済み(比較相手なし)
	//   ページはここで落ちる)。Start 経路(KESCMCore.cpp の toRaster)と同じ「先に対象を確定してから回す」形。
	std::vector<UID> toCompareT, toCompareS;
	toCompareT.reserve(targetPages.size());
	toCompareS.reserve(targetPages.size());
	for (size_t i = 0; i < targetPages.size(); ++i)
	{
		std::map<UID, UID>::const_iterator mi = targetToSource.find(targetPages[i]);
		if (mi == targetToSource.end())
			continue;	// 登録済み(比較相手なし)ページ等は再比較対象外
		toCompareT.push_back(targetPages[i]);
		toCompareS.push_back(mi->second);
	}

	// 対象0件(選択が登録済み=比較相手なしページばかり)ならここで戻る。総数0の進捗バーを作らずに済み、
	// 下の後処理(✓剪定・再描画・サムネイル)も走らせない=以前と同じ「何もしない」振る舞いになる。
	if (toCompareT.empty())
		return kFalse;

	// ★ページ数が多いときだけ進捗バー＋キャンセルを出す(2026-07-27)。しきい値と、自前でしきい値を
	//   持つ理由は kKESCMProgressBarMinPages(KESCMConstants.h)を参照。
	const int32 compareCount = (int32)toCompareT.size();
	const bool8 showBar = (compareCount >= kKESCMProgressBarMinPages) ? kTrue : kFalse;
	PMString barTitle(compareCount == 1 ? "Refreshing 1 page..." : "Refreshing pages...");
	barTitle.SetTranslatable(kFalse);
	TaskProgressBar progress(barTitle, compareCount, showBar);
	progress.DisableChildProgressBars(kTrue);	// ラスタ化の内部処理が自分のバーを出すのを抑える

	int32 changedCount = 0;
	int32 failedCount = 0;
	bool16 cancelled = kFalse;
	std::vector<UID> touchedTargetPages, touchedSourcePages;
	for (size_t i = 0; i < toCompareT.size(); ++i)
	{
		PMString item("Page ");
		item.AppendNumber((int32)(i + 1));
		item.Append(" / ");
		item.AppendNumber(compareCount);
		item.SetTranslatable(kFalse);	// 数値入りなので翻訳対象にしない
		progress.DoTask(item);			// ★1件進める(前の1件の完了もここで反映される)

		const UID tUID = toCompareT[i];
		const UID sUID = toCompareS[i];
		touchedTargetPages.push_back(tUID);
		touchedSourcePages.push_back(sUID);
		bool16 changed = kFalse;
		const ErrorCode mkErr = KESCMDrawEventHandler::MakeEntry(UIDRef(targetDB, tUID), UIDRef(sourceDB, sUID), changed);
		if (mkErr != kSuccess)
		{
			// ★比較できなかった(ページサイズ不一致・ラスタ化失敗・OOM)ときは既存エントリに触らない
			//   (2026-08-06 再点検): changed==kFalse を「変化が無くなった」と読んで DropOneEntry すると、
			//   一時的な失敗で前回の正しい枠が黙って消える。古い枠は残し、件数を failed としてステータスへ。
			++failedCount;
		}
		else if (changed)
			++changedCount;
		else
		{
			// 変化が無くなったページ → 古い枠が残っていれば消す(更新で消えるべき)。エントリと同時に
			// Source 側対応表(sSrcPageToTarget[sUID])も掃除する共通ヘルパへ統一(ドロップ処理を1本化)。
			KESCMDrawEventHandler::DropOneEntry(tUID, sUID);
		}

		// ★キャンセル判定は「1ページを比較し終えた安全な場所」で行う(WasCancelled はイベントを回すので
		//   ラスタ化の途中では見ない)。引数 kFalse = グローバルエラー状態を立てない(立てると後続の
		//   コマンドが巻き添えで失敗する)。
		// ★Start の比較と違い、ここは「そこまで更新した分を残して止める」。この機能はもともと
		//   「選んだページだけを最新にする」部分更新なので、途中でやめても残るのは「更新した数ページ＋
		//   まだ古い数ページ」= 選択範囲を狭めて実行したのと同じ状態にしかならない(Start のように
		//   「比較済みと未比較が混在した文書全体」を作ってしまうことはない)。
		if (progress.WasCancelled(kFalse))
		{
			cancelled = kTrue;
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);	// 中断で立った可能性のあるエラーを持ち越さない
			break;
		}
	}
	if (outCancelled) *outCancelled = cancelled;

	// 報告用の処理数=実際に MakeEntry/DropOneEntry まで到達したページ数(対応表に無くて対象から外れた
	// 選択ページは数えない。キャンセル時はそこまでに処理した数。ステータス行の「refreshed N」が実態と
	// 一致するように。2026-07-15)。
	if (outProcessed) *outProcessed = (int32)touchedTargetPages.size();
	if (touchedTargetPages.empty())
		return kFalse;

	// 旧版画像キャッシュは古いので破棄(次の peek で現ズームで作り直し)。
	KESCMDrawEventHandler::DropAllOrig();

	// ★「KCM: Check」の✓: この部分再比較でマーク(枠)が消えたページのチェックも忘れる(ユーザー指定
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

	// ★★2026-08-13(Task 10): スクロールバー地図・Pages パネルのサムネイル・Prev/Next の位置は
	//   すべて UI の持ち物なので、この独立再比較路(KESCMDoMarkChangesDoc を通らない)でも通知1本にする。
	//   ⚠**navReset は kFalse** ---- 選択ページだけの部分再比較で巡回の基準点を捨てると、1ページ直す
	//     たびに Prev/Next が先頭へ戻る。文書は変わっていないので基準点は有効なまま。
	//   ⚠ touchedTargetPages / touchedSourcePages(触れたページだけの絞り込み)は**通知では運べない**ので、
	//     UI は両文書の全ページを作り直す。★戻すには通知にページ集合を載せる＝2026-08-13 の Task 12 で
	//     「IKESCMMarkData では戻せない」ことが判明した(触れたページは現在状態から復元できない)。
	//     理由の全文は KESCMThumbnailRefresh.h の KESCMPurgeAllPageThumbs。
	//   ⚠ 旧実装が Source 側を「sSrcMarksOn か srcHadChecks のときだけ」更新していたのは per-UID Purge の
	//     節約のため。全ページ Purge になったので条件ごと落としてある(常に両方を作り直す＝取りこぼしなし)。
	KESCMNotifyDocs(kKESCMMarksRebuiltMessage, targetDB, sourceDB, kFalse /*navReset*/);

	if (outChanged) *outChanged = changedCount;
	if (outFailed)  *outFailed = failedCount;
	return kTrue;
}

// ページパネルの選択ページの「ページ比較」を再検出して更新する(ページ右クリック「KCM: Refresh Page
// Comparison」の実体。旧 Ctrl+ミドルの移設先)。arm 済み(Start 後)かつ前面文書が Target のときだけ動く
// (★2026-07-15 Target 限定化=ユーザー指定。旧仕様の Source→Target 写像経路は撤去)。
// outPages=実際に再比較したページ数(対応表に無い登録済みページ等は数えない)、
// outChanged=うち変化したページ数。戻り=1ページ以上処理したか。
bool16 KESCMRefreshComparisonForSelectedPages(int32* outPages, int32* outChanged, bool16* outCancelled, int32* outFailed)
{
	if (outPages)     *outPages = 0;
	if (outChanged)   *outChanged = 0;
	if (outCancelled) *outCancelled = kFalse;
	if (outFailed)    *outFailed = 0;

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
	// ★includeMasters=kTrue(2026-08-13): マスタースプレッドも比較対象なので部分再比較の対象にする。
	//   対応表側(KESCMRefreshComparisonCore)にマスターのペアを入れるのと対で意味を持つ。
	if (!KESCMPageMapReadSelection(db, selPages, kTrue /*includeMasters*/) || db != targetDB)
		return kFalse;

	// 再比較コアは Target ページで駆動する(前面=Target のみなので選択ページがそのまま対象)。
	std::vector<UID> targetPages = selPages;

	int32 processed = 0, changed = 0, failed = 0;
	bool16 cancelled = kFalse;
	const bool16 ok = KESCMRefreshComparisonCore(targetDB, sourceDB, targetPages, &processed, &changed, &cancelled, &failed);
	// キャンセルの有無は Core の成否に関わらず返す。※Core が kFalse を返すのは「対象0件」= cancelled が
	//   kFalse の経路だけなので実際には両立しないが、戻り値の意味に依存せず伝えておく(防御)。
	if (outCancelled) *outCancelled = cancelled;
	if (!ok)
		return kFalse;

	// (Prev/Next 間の位置表示とボタン有効/無効の更新は、上の KESCMRefreshComparisonCore が投げる
	//  kKESCMMarksRebuiltMessage に含まれる ---- 2026-08-13・Task 10 でここの直接呼びを畳んだ。)

	// ★Story Edits の一覧も同じ理由でここから作り直す(2026-08-10)。**選択ページぶんだけ**の更新には
	//   できない——1つのストーリーが、再比較したページとしなかったページにまたがって流れうるので、
	//   ページ単位に割れない。丸ごと作り直すのは共有関数 KESCMRebuildStoryEdits の仕事。
	// ⚠これを入れるまで、Refresh の後だけ一覧が編集前の状態のまま残っていた(実測して判明)。
	KESCMRebuildStoryEdits(targetDB, sourceDB);

	if (outPages)   *outPages = processed;
	if (outChanged) *outChanged = changed;
	if (outFailed)  *outFailed = failed;
	return kTrue;
}

// 「KCM: Refresh Page Comparison」メニューを有効化してよいか(UpdateActionStates 用)。
// arm 済み(Start 後)かつ前面文書が Target のとき kTrue(★2026-07-15 Target 限定化=ユーザー指定。
// コンテキストメニューは無効項目を出さないため、Source 側の右クリックでは項目自体が消える想定)。
// 選択の有無までは見ない(ページ右クリックは通常そのページを選択済みで、未選択でも DoAction 側が
// 安全に no-op しステータス行へ "no comparable pages" を出す)。
// ★実行側(KESCMRefreshComparisonForSelectedPages)は KESCMPageMapReadSelection の db で判定するが、
//   そちらも同じ KESCMActiveDocDB() を使う(KESCMPageMap.cpp)ので、両者の「対象文書」は必ず一致する。
//   違いは「選択の有無を見るか」だけ(2026-08-06 の監査で確認)。
//   ★2026-08-06 ブロック9 監査 A-1: 両者とも旧実装は Utils<ILayoutUIUtils>()->GetFrontDocument() で、
//   ここのコメントは「向こうも同じものを使っている」ことを一致の根拠にしていた。向こうを公式ルート
//   (ActiveContext 経由)へ寄せたので、こちらも同時に合わせる=一致の根拠を保つ([[one-question-one-place]])。
bool16 KESCMRefreshComparisonAvailable()
{
	if (!KESCMIsArmed())
		return kFalse;
	IDataBase* targetDB = KESCMArmedTargetDB();
	IDataBase* sourceDB = KESCMArmedSourceDB();
	if (targetDB == nil || sourceDB == nil)
		return kFalse;
	IDataBase* db = KESCMActiveDocDB();
	return (db != nil && db == targetDB) ? kTrue : kFalse;
}

// ★armed 中の Target/Source が IDocumentList に現存するかの最終ライン防御(2026-07-15 復活)。
//   旧・中ボタン watcher はジェスチャ毎にこの検査を行っていたが、ツール移行で失われていた。
//   通常はクローズ responder(KESCMHandleDocsClosed)が先回りして disarm するため失格には到達しないが、
//   responder が漏れた場合に解放済み IDataBase をサンプリング/peek へ渡さないための保険。
//   失格なら KESCMHandleDocsClosed() で Stop 相当のフルクリーンアップ(sPeek* 解除を含む)をして kFalse。
//   ★2026-08-13: 呼び手(ジェスチャ/CMYK カーソル)が別ファイルへ移ったので static を外した。宣言は KESCMPeek.h。
bool16 KESCMArmedDocsAlive()
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

//========================================================================================
// KESCMPeekStartup
//   アプリ起動/終了サービス。中ボタンウォッチャは撤去した(2026-07-13)ので起動時の処理は無く、
//   終了時に保持リソース(遅延サムネイル idle task・マーク/旧版画像バッファ・peek arm 状態・
//   レイアウト同期フラグ)を片付けるためだけに残している。
//   ★2026-08-13 の分割後も**このサービスは1本のまま**(第1段では新しい ClassID を使わない)。
//     出ていった3ファイルの後片付けは、それぞれが公開する Shutdown 入口を下から呼ぶ。
//
// ⚠★★★2026-08-14: **kModelPlugIn 化(第2段)の前に、このサービスの threading policy を確定させること。**
//   ガイド vol1-07「Threading and startup/shutdown services」の本文:
//     "Returning IPlugIn::kMultipleThreads means the service **will be called on both the main thread
//      and background thread startup and shutdown.**"
//     "kCStartupShutdownProviderImpl derives its implementation of GetThreadingPolicy from
//      CServiceProvider. **If the startup/shutdown service boss resides in a model plug-in, the service
//      will be called on both main and background thread startup and shutdown.**"
//   ⇒ 下の Shutdown() は **DropAll() ほかで比較状態を丸ごと消す**ので、これが BG スレッドの終了ごとに
//     呼ばれると **PDF を1本書き出すたびにマークが全部消える**。
//   ⚠KESCM.fr:162 が指定しているのは **kLazyStartupShutdownProviderImpl** で、これは**上のガイドの
//     3択に載っていない**(kCMainThreadStartupShutdownProviderImpl / kCMTStartupShutdownProviderImpl /
//     kCStartupShutdownProviderImpl)。IStartupShutdownService.h:44-47 も「起動を遅くしたくなければ
//     Lazy を使え」としか書いておらず **threading policy には触れていない**
//     ⇒ **どちらに転ぶかは実測でしか分からない**(第2段計画書 Task 11B で測る)。
//   ★BG でも呼ばれるなら kCMainThreadStartupShutdownProviderImpl へ替える。
//     ⚠**この Shutdown() の側に「BG なら何もしない」ガードを入れて塞ぐのは筋が悪い**
//       ---- 本当の終了時にも取りこぼしうる。**サービスの宣言側(.fr)で解決する。**
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
	// 初期値 kFalse なので、ここで明示的に ON にしなければ OFF のまま。保存済み設定の復元で
	// syncLayoutViews=true を復元したユーザーだけ ON になる(その復元は下記のとおり UI 側へ移った)。

	// ★★2026-08-13(Task 8): **起動時の仕事は3つとも UI 側へ移した** ---- パネル設定の復元
	//   (KESCMLoadPanelStateIfPresent)・一括クローズの購読(KESCMAttachDocsClosedObserver)・
	//   半透明の追随購読(KESCMAttachPanelVisibilityObserver)。行き先は KESCMUIStartup.cpp。
	//   ⇒ **model 側の起動処理は空になった。** 分けてみて初めて「元の起動処理は丸ごと UI だった」と
	//     分かった形で、これは分割が正しい線で入っている証拠でもある。
	//   ⚠**空でもこのメソッドは残す**(IStartupShutdownService の契約)。第2段で model 側に起動時の
	//     仕事ができたらここへ足す。
}

void KESCMPeekStartup::Shutdown()
{
	// ★★★2026-08-15（第2段 Task 11B）＝**この関数はメインスレッドでしか呼ばれない**。
	//   KESCM は `kModelPlugIn` になったので、放っておくとガイド vol1-07 のとおり
	//   **バックグラウンドスレッドの起動・終了ごとにも呼ばれる**＝下の DropAll 以下が走り、
	//   **PDF を書き出すたびにマークが全部消える**。
	//   ⇒ 塞いだのは **`.fr` の宣言側**（`kCMainThreadStartupShutdownProviderImpl`）。
	//   ⚠**ここに「BG なら何もしない」ガードを入れてはいけない**——本当の終了時にも取りこぼしうる。
	//     スレッド方針はサービスの宣言で表すのが筋で、ガイドが threading policy を用意しているのはそのため。
	//   ★手本＝Adobe 製 DiagnosticLog（`DiagLogClass.fr:93-100`）が **model プラグインのまま
	//     startup/shutdown だけをメインスレッド限定にし、その理由をコメントに書いている**。

	// ★★2026-08-13(Task 8): **UI の後片付け5件はここから UI 側へ移した** ---- 遅延サムネイル
	//   idle task の解放 / 一括クローズの保留の破棄 / 半透明の購読解除 / 半透明タイマーの停止 /
	//   押下中 HUD のフォント返却。行き先は KESCMUIStartup.cpp で、**順序も向こうで保っている**
	//   (購読を外してから道具を畳む ---- 消えかけのコードで Update が走るのを避けるため)。
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
	KESCMPageCheckClearAllDocs();			// 「KCM: Check」の✓
	KESCMResetHideUnchanged(kFalse);		// Hide Unchanged の控え(kFalse=文書には一切触らない)
	KESCMInvalidatePageNumberMarkerRects();	// ノンブル除外矩形のキャッシュ(2026-08-06 の監査 E-3)
	// ★Story Edits の一覧(2026-08-10)。★★他と違い**中身が PMString** なので、これを忘れると
	//   unload 時に静的な PMString がデストラクトされる ---- KBS が3度続けて忘れて記録した形
	//   (KBSResultTree.h:76-77)。UI には触らない(行を捨てるだけ)ので終了処理中でも安全。
	KESCMStoryList::ShutdownCleanup();
	// ★peek の arm 状態もここで落とす。残したままだと、終了処理後に kAfterCloseDoc responder が
	// 発火した場合、KESCMHandleDocsClosed が stale な sPeek* から comparisonDocClosed=true を
	// 再計算し得る(通常の終了順=文書クローズ→Shutdown では起きないはずだが防御的にリセット。
	// ポインタは nil 代入のみ=deref しない)。
	sPeekArmed = kFalse;
	sPeekTargetDB = nil;
	sPeekSourceDB = nil;

	// (★同期キャッシュの破棄・同期フラグの後始末・CMYK の後片付け・ステータス記憶の消去も
	//  2026-08-13 Task 8 で UI 側 KESCMUIStartup.cpp へ移した。どれも UI のファイルが持つ状態。
	//  ⚠**ステータス記憶(gSessionStatus)だけは第2段の予定が違う**: 設計書 §3.3 のとおり文字列の保持は
	//    model 側へ来る(app.kcmStatus がパネルを閉じていても答える仕様のため)＝Task 9 で移る。)

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

	// ★2026-08-13(Task 10): 同期キャッシュの破棄(KESCMViewSync)と覗き状態の初期化(KESCMPeekGesture)は
	//   どちらも UI 側の状態なので、ここでは呼ばない。arm の直後に KESCMStartComparisonFor が
	//   kKESCMMarksRebuiltMessage を投げ、それを受けた UI が自分の状態を初期化する。
	sPeekTargetDB = targetDB;
	sPeekSourceDB = sourceDB;
	sPeekArmed = kTrue;
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

	// ★2026-08-13(Task 10): arm 側と対称に、同期キャッシュの破棄と覗き状態の解除は UI に任せる
	//   ---- disarm の直後に KESCMStopComparison が kKESCMMarksClearedMessage を投げる。
	sPeekArmed = kFalse;
	sPeekTargetDB = nil;
	sPeekSourceDB = nil;
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
	// ★2026-08-13(Task 10): キャッシュの持ち主は UI(KESCMViewSync)なので、捨てるのは末尾の通知を
	//   受けた UI。**「無条件」という性質**は、その通知を changed で絞らないことで保っている。
	// ノンブル除外矩形のキャッシュも同じ理由で捨てる(キーに db ポインタを含むので、閉じた文書の
	// エントリを残さない。2026-08-06 の監査 E-3)。生存側の分は次の描画で1回測り直すだけ。
	KESCMInvalidatePageNumberMarkerRects();

	// ★終了堅牢化(2026-07-15): アプリが終了処理中(kQuitting/kShuttingDown)にこのレスポンダへ来たら、
	// UI 仕事(strip の widget 除去・InvalidateViews・サムネイル idle 予約・パネル/ステータス更新)を
	// 全てスキップし、状態(メモリ)の破棄だけにする。終了中のウィンドウ/パネル解体順はプラットフォーム
	// 依存で、特に Mac(Cocoa)は Windows と異なるため、解体中の widget へ触るのが Mac 限定
	// crash-on-quit の典型形。通常の quit は close-all(まだ kRunning)→Terminate の順なので、対話的な
	// クローズと quit の close-all フェーズでは従来どおりフルクリーンアップが走る(挙動変更なし)。
	const bool16 quitting = KESCMAppIsQuitting();

	// ★一括クローズ(複数文書を続けて閉じる)の最中は、UI の後片付けを保留して全部閉じ終わってから
	//   1回だけ流す(2026-07-27)。状態(メモリ)の破棄は保留せずその場で行うので、閉じた db を持ち越さない
	//   従来どおりの安全性は保たれる。
	//   ★★2026-08-13(Task 10): **この判定は UI 側へ移した**。KESCMBatchCloseInProgress() は UI 側が
	//     持つ状態で、model がそれを聞くこと自体が逆流だった。今は末尾の通知を受けた observer が
	//     「保留するか、今やるか」を決める。model に残っているのは quitting の判定だけ
	//     ---- こちらは KESCMAppIsQuitting()＝model 側の問いで、コマンドを打つかどうかにも効く。

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

	// ★2026-08-13(Task 10): 生存側の db は関数末尾の通知にも載せるので、宣言をブロックの外へ出した。
	//   ⚠ここに入るのは**生存確認(FindDocByDataBase)を通ったポインタだけ**。閉じた db は決して拾わない
	//   ---- 通知の受け手はこれを deref する(閉じた IDataBase* はアドレスが再利用される)。
	IDataBase* survivorTargetDB = nil;
	IDataBase* survivorOrigDB   = nil;
	IDataBase* survivorSrcDB    = nil;	// Source側枠(Show Marks on Source)が出ている文書

	if (comparisonDocClosed)
	{
		// DropAll/DropAllOrig で nil にする前に、まだ開いている側の db を控えておく(生存確認済みなので
		// 後で安全に InvalidateViews できる)。
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
		// (覗き状態の解除＝KESCMResetPeekGestureState は UI 側の状態なので、末尾の通知を受けた UI がやる)
		KESCMDrawEventHandler::sMarksTempHidden = kFalse;	// Hold to Hide Marks の一時退避も解除
		KESCMDrawEventHandler::sSrcMarksTempHidden = kFalse;	// Source 側の一時退避も解除
		KESCMDrawEventHandler::sMarksVisible = kFalse;
		// ★2026-07-11(ユーザー報告): Stop ボタンは登録(Add/Remove)を全解除するのに、比較文書(Source等)を
		//   閉じて比較が終わった時は登録が残っていた。ここは「Stop 相当のフルクリーンアップ」なので、
		//   Stop(KESCMDoClearMarks)と同じく登録も丸ごと忘れる。これを怠ると、生存側 Target/Source に
		//   古い登録が残り、次の Start でペアリングに紛れ込む(map 空にするだけ=deref なし)。
		KESCMPageMapClearAllDocs();
		KESCMPageCheckClearAllDocs();	// 「KCM: Check」の✓も同様に全消去(Start 中限定)
		// ★Story Edits の一覧も捨てる(2026-08-10)。行は Target 側の story UID とページ UID を持って
		//   いるので、その文書が閉じた後は指す先が無い。★画面側(ツリーと見出し)は下の
		//   KESCMRefreshPanel が実状態から作り直すので、ここは状態を捨てるだけでよい
		//   ＝終了処理中(quitting)に来ても安全(vector を空にするだけ・deref しない)。
		// ⚠**「閉じたのが比較対象か」をここで判定し直さない**。閉じた文書の UIDRef や IDataBase* は
		//   アドレスが再利用されるので同一性判定に使えない([[uidref-reuse-after-close]])。上の
		//   comparisonDocClosed が既に生存確認(FindDocByDataBase)で答えを出しているので相乗りする。
		KESCMStoryList::Clear();
		// ★巡回の基準点も忘れる(2026-08-06 再点検)。Stop(KESCMDoClearMarks)は KESCMResetNav を呼ぶのに、
		//   この「Stop 相当のフルクリーンアップ」だけ抜けていた。閉じた文書のページ UID を基準点に残すと、
		//   次の対象文書で UID が偶然一致して途中から巡回が始まり得る。
		//   ★2026-08-13(Task 10): 基準点は UI 側の状態なので、末尾の通知に navReset として乗せる。
		changed = kTrue;

		// ★2026-08-13(Task 10): ここにあった画面側の後片付け ---- strip の撤去(または Find Overset が
		//   単独 ON 中なら赤帯の描き直し)・生存側の再描画・**次の idle へ遅延させる**サムネイル作り直し
		//   ---- は、すべて末尾の kKESCMComparisonDocsClosedMessage を受けた UI がやる。
		//   ⚠遅延させる理由(2026-07-08 実機): 閉じたのが Target で生存側がこれからアクティブ化する場合、
		//     その場で ForceRedraw しても前面切替の過渡で再生成が起こりきらず枠が残る。
		//   ⚠「終了中は触らない」「一括クローズ中は保留する」の判断も UI へ移した(どちらも UI の都合)。

		// 生存している側のレイアウトビューを再描画して枠を即座に消すのは model の仕事(描画データを
		// 持っているのはこちら)。★終了中は窓ごと消えるので触らない。
		if (!quitting)
		{
			PMString s("marks cleared");	// Stop ボタン(DoClear)と同じメッセージ
			s.SetTranslatable(kFalse);
			KESCMNotifyStatus(s);

			KESCMInvalidateDB(survivorTargetDB);
			if (survivorOrigDB != survivorTargetDB)
				KESCMInvalidateDB(survivorOrigDB);
			if (survivorSrcDB != survivorTargetDB && survivorSrcDB != survivorOrigDB)
				KESCMInvalidateDB(survivorSrcDB);
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
	KESCMPageCheckSweepClosedDocs();	// 「KCM: Check」の✓も、閉じた文書の分を状態だけ捨てる(deref なし)

	// ★★2026-08-13(Task 10): 画面側の後片付けは**ここ1本の通知**にまとめた。
	//
	// ⚠**changed を見ずに無条件で投げる。** ビュー同期のページ矩形/除外対応キャッシュ
	//   (KESCMViewSync)は「どの文書が閉じても捨てる」ものだったため ---- 以前はこの関数の頭で
	//   KESCMInvalidateSyncCaches() を無条件に呼んでいた。パネルの表示合わせも冪等なので、
	//   比較と無関係な文書が閉じたときに通っても害は無い。
	//
	// ⚠**終了中(quitting)でも投げる。** 「終了中は widget に触らない」「一括クローズ中は保留して
	//   全部閉じ終わってから1回だけ流す」は**どちらも UI の都合**なので、UI 側の observer が
	//   KESCMAppIsQuitting() と KESCMBatchCloseInProgress() を見て決める。
	//   ★これが逆流を断つ肝: KESCMBatchCloseInProgress() は UI 側の状態で、**model がそれを聞いて
	//     いたこと自体が逆流だった**。
	//
	// 付随データ＝比較が終わったときだけ、**生存している側**の db を最大3つ(Target / 旧版べた載せ /
	// Source 側枠)。閉じた db は決して載せない。navReset も「比較が終わった」ときだけ立てる。
	KESCMNotifyDocs(kKESCMComparisonDocsClosedMessage,
	                survivorTargetDB, survivorOrigDB, survivorSrcDB,
	                comparisonDocClosed /*navReset*/);
}
