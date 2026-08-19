//========================================================================================
//
//  KESCMThumbIdleTask.cpp
//
//  Pages パネルサムネイルの再生成を「次の idle」に一度だけ遅延実行する idle task
//  (KESCMThumbIdleTask.h)。設計と背景はヘッダー参照。
//
//  ・セッション中 1 個の IIdleTask を生成して再利用(sThumbTask)。予約は入れ直す前に必ず
//    UninstallTask で外す(Remove→Add で発火時刻を最新化)。「今キューに載っているか」は基底
//    CIdleTask が fCurrentlyInstalled で持っている(CIdleTask.h:64)ので、こちらでは数えない
//    = 製品 spellpanel/DynSpellCheckEventWatcher.cpp:138,145 と同じ無条件呼び。
//  ・RunTask は保留 db(sPendingDBs)を取り出し、IDocumentList に生存している db にだけ
//    KESCMTryRefreshPagesPanelThumbnails を呼ぶ(閉じた db は触らない)。処理後は UninstallTask で
//    自分をキューから外す(kEndOfTime は契約違反。オブジェクトは保持=次回再利用)。
//  ・アプリ終了は KESCMShutdownThumbIdleTask で RemoveTask + Release。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "CIdleTask.h"

#include "IIdleTaskMgr.h"

#include <algorithm>
#include <vector>

#include "KCMUIID.h"
#include "KESCMThumbIdleTask.h"
#include "KESCMThumbnailRefresh.h"	// KESCMTryRefreshPagesPanelThumbnails
#include "Utils.h"					// Utils<IKESCMCompareFacade>()
#include "IKESCMCompareFacade.h"	// IsDocDBOpen(閉じた db を deref しない生存確認) / IsAppQuitting
									// (2026-08-14 Task 16 で Facade 経由へ)

// 切替が落ち着くのを待つ遅延(ms)。クローズ時の前面切替は速いので控えめでよい。取りこぼす場合は増やす。
static const uint32 kKESCMThumbRefreshDelayMs = 150;

// ---- 共有状態(この翻訳単位内で完結) ---------------------------------------------------
static IIdleTask*             sThumbTask = nil;	// 生成した idle task(1個を再利用)。所有(終了時 Release)
static std::vector<IDataBase*> sPendingDBs;		// 次回 RunTask で処理する db 集合
static bool16                 sShutdown  = kFalse;	// ★終了処理済みフラグ。Shutdown 後に responder 経由で
													// スケジュールされてもタスクを再生成しない(リーク+
													// ティアダウン中発火の防止。通常の終了順では到達しない
													// はずだが防御的に塞ぐ)

//========================================================================================
// KESCMThumbIdleTask — CIdleTask 派生の最小 idle task。RunTask/TaskName だけ実装
// (InstallTask/UninstallTask は CIdleTask が AddTask/RemoveTask を呼ぶ既定実装を提供)。
//========================================================================================
class KESCMThumbIdleTask : public CIdleTask
{
public:
	KESCMThumbIdleTask(IPMUnknown* boss) : CIdleTask(boss) {}

	virtual uint32 RunTask(uint32 flags, IdleTimer* idleTimer);
	virtual const char* TaskName() { return "KESCMThumbIdleTask"; }
};

CREATE_PMINTERFACE(KESCMThumbIdleTask, kKESCMThumbIdleTaskImpl)

uint32 KESCMThumbIdleTask::RunTask(uint32 flags, IdleTimer* /*idleTimer*/)
{
	// ★終了堅牢化(2026-07-15): アプリが終了処理中なら Pages パネルへ触らず、予約を捨てて自分を外す。
	// quit 中の doc close がこのタスクを予約→Shutdown(RemoveTask)より前に idle が回った場合の保険
	// (解体中のパネルへの purge+ForceRedraw が Mac 限定 crash-on-quit の典型形)。
	if (Utils<IKESCMCompareFacade>()->IsAppQuitting())
	{
		sPendingDBs.clear();
		this->UninstallTask();
		return 0;
	}

	// メニュー展開中・マウス追跡中・バックグラウンドでは触らない(状態が変わったら呼び直される)。
	if (flags & (IIdleTaskMgr::kInBackground | IIdleTaskMgr::kMenuUp | IIdleTaskMgr::kMouseTracking))
		return kOnFlagChange;

	// 保留 db を取り出して空にする(RunTask 中に再スケジュールされても取りこぼさない)。
	std::vector<IDataBase*> dbs;
	dbs.swap(sPendingDBs);

	bool16 purgedAny = kFalse;
	for (std::vector<IDataBase*>::iterator it = dbs.begin(); it != dbs.end(); ++it)
	{
		// 予約から idle までの間に閉じた db は触らない(deref 禁止=共有ヘルパ KESCMIsDocDBOpen)。
		if (Utils<IKESCMCompareFacade>()->IsDocDBOpen(*it))
		{
			KESCMTryRefreshPagesPanelThumbnails(*it, kFalse /*redrawNow*/);	// Purge のみ
			purgedAny = kTrue;
		}
	}
	if (purgedAny)
		KESCMForceRedrawPagesPanelNow();	// ForceRedraw は全 db の Purge 後に1回だけ(2026-07-25 バッチ化)

	// 契約(CIdleTask.h): kEndOfTime を返さず UninstallTask を呼ぶ。kEndOfTime だと IdleTaskMgr は
	// UninstallTask を経ずに外すため基底 fCurrentlyInstalled が true のまま残り、次回 InstallTask の
	// AddTask がスキップされて 2回目以降のクローズで遅延サムネイル更新が二度と走らなくなる。
	this->UninstallTask();
	// ★RunTask 中の再入予約を握りつぶさない(2026-08-06 再点検): 上の KESCMForceRedrawPagesPanelNow は
	//   同期描画で、描画イベントの保険掃除(閉じた文書の後片付け)経由で KESCMScheduleThumbRefresh が
	//   この最中に走ることがある。その予約(AddTask)は直後の UninstallTask で外れてしまい、保留 db が
	//   残ったまま二度と発火しない。保留が残っていたらここで入れ直す(空なら従来どおり降りるだけ)。
	if (!sPendingDBs.empty())
		this->InstallTask(kKESCMThumbRefreshDelayMs);
	return 0;	// 戻り値は無視される(オブジェクトは保持し次回再利用)。
}

//========================================================================================
// 公開エントリ
//========================================================================================
void KESCMScheduleThumbRefresh(IDataBase* db)
{
	if (db == nil || sShutdown || Utils<IKESCMCompareFacade>()->IsAppQuitting())
		return;		// ★Shutdown 後・アプリ終了中は再アーム禁止(タスク再生成リーク/ティアダウン中発火の防止)

	if (sThumbTask == nil)
		sThumbTask = ::CreateObject2<IIdleTask>(kKESCMThumbIdleTaskBoss);
	if (sThumbTask == nil)
		return;	// タスクを作れない=予約も立たないので保留リストにも積まない(2026-07-25: push を成功後へ移動)

	// 同じ db を重複登録しない。
	if (std::find(sPendingDBs.begin(), sPendingDBs.end(), db) == sPendingDBs.end())
		sPendingDBs.push_back(db);

	// 二重 AddTask は不可なので、入れ直す前に必ず外す(発火時刻も最新化される)。★「今載っているか」は
	// 数えない: 基底 CIdleTask が fCurrentlyInstalled で持っており(CIdleTask.h:64)、載っていない状態で
	// RemoveTask を呼んでも kEndOfTime が返るだけで無害(IIdleTaskMgr.h:95-98)。製品
	// spellpanel/DynSpellCheckEventWatcher.cpp:138,145 も同じ無条件呼び。
	sThumbTask->UninstallTask();
	sThumbTask->InstallTask(kKESCMThumbRefreshDelayMs);
}

void KESCMShutdownThumbIdleTask()
{
	sShutdown = kTrue;	// ★以後の KESCMScheduleThumbRefresh は no-op(再生成禁止)
	if (sThumbTask != nil)
	{
		sThumbTask->UninstallTask();	// RemoveTask(載っていなければ何も起きない)。Release より前に必ず
		sThumbTask->Release();
		sThumbTask = nil;
	}
	sPendingDBs.clear();
}

// KESCMThumbIdleTask.cpp 終わり。
