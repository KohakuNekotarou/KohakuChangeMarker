//========================================================================================
//
//  KESCMModelChangeObserver.cpp
//
//  model が投げた通知を受けて画面を作り直す **UI 側**の1本(2026-08-13・model/UI 分割 第1段 Task 9)。
//  送り手は KESCMModelNotify.cpp、通知の種類は KESCMID.h の kKESCM*Message。
//
//  ★同居先は kActiveContextBoss。既存3本(レイアウト同期 / 一括クローズ / パネル表示)と同じ実証済みの
//    構成で、**新しい機構は何も足していない**。
//
//  ⚠★★**Task 9 ではステータス行だけを繋ぐ。** 残り5種の分岐は**空のまま置いてある**——1タスクで全部
//    繋ぐと、壊れたときにどの通知が原因か分からなくなるため。埋めるのは Task 10。
//    ★空の分岐は「まだ何もしない」ことが意図であって、書き忘れではない。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "CObserver.h"
#include "ISession.h"
#include "IApplication.h"
#include "IActiveContext.h"
#include "ISubject.h"
#include "PMString.h"

#include "KESCMID.h"
#include "KESCMModelNotify.h"		// KESCMGetSessionStatus / KESCMStatusWantsForceRedraw(model 側の値を読む)
#include "KESCMUIShared.h"			// KESCMSetStatus(表示。UI 内部専用)

/* model の通知を受ける UI 側のオブザーバ。kActiveContextBoss に IID_IKESCMMODELCHANGEOBSERVER として
   同居させている(同居先の理由はレイアウト同期オブザーバと同じ=実証済みの構成)。購読先はアプリの subject。 */
class KESCMModelChangeObserver : public CObserver
{
public:
	KESCMModelChangeObserver(IPMUnknown* boss) : CObserver(boss, IID_IKESCMMODELCHANGEOBSERVER) {}
	~KESCMModelChangeObserver() {}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);
};

CREATE_PMINTERFACE(KESCMModelChangeObserver, kKESCMModelChangeObserverImpl)

void KESCMModelChangeObserver::Update(const ClassID& theChange, ISubject* /*theSubject*/, const PMIID& protocol, void* /*changedBy*/)
{
	// 自作 protocol で送られたものだけを見る(本体の通知と混ざらない)。
	if (protocol != IID_IKESCMMODELCHANGEOBSERVER)
		return;

	if (theChange == kKESCMStatusTextMessage)
	{
		// ★文字列は model 側が持っている。ここは「表示する」だけ。
		PMString s;
		KESCMGetSessionStatus(s);
		KESCMSetStatus(s, KESCMStatusWantsForceRedraw());
		return;
	}

	// ---- 以下は Task 10 で埋める(今は意図的に何もしない) ----
	// kKESCMMarksRebuiltMessage      → KESCMRefreshPanel / KESCMRefreshNavPosition /
	//                                  KESCMScrollMapInvalidateAll / KESCMTryRefreshPagesPanelThumbnails
	// kKESCMMarksClearedMessage      → 同上
	// kKESCMPageFlagsChangedMessage  → KESCMRefreshThumbnailsForPages / KESCMScrollMapInvalidateAll
	// kKESCMStoryEditsRebuiltMessage → KESCMStoryTreeRebuild / KESCMUpdateStorySectionLabel
	// kKESCMOversetRescannedMessage  → KESCMRefreshNavPosition / KESCMScrollMapInvalidateAll /
	//                                  KESCMTryRefreshPagesPanelThumbnails
	// ★それまでは model 側が今までどおり直接呼んでいる(逆流のまま)。だから**画面は正しく動く**。
}

// アプリ subject への購読を付ける(UI 側 Startup から1回)。既存の KESCMAttachDocsClosedObserver と
// 同じ形。IsAttached を先に聞くので重ねて呼んでも二重に付かない。
void KESCMAttachModelChangeObserver()
{
	ISession* session = GetExecutionContextSession();
	IActiveContext* ctx = (session != nil) ? session->GetActiveContext() : nil;
	if (ctx == nil)
		return;
	InterfacePtr<IObserver> obs((IObserver*)ctx->QueryInterface(IID_IKESCMMODELCHANGEOBSERVER));
	if (obs == nil)
		return;
	InterfacePtr<IApplication> app(session->QueryApplication());
	InterfacePtr<ISubject> subject(app, IID_ISUBJECT);
	if (subject == nil)
		return;
	if (!subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IKESCMMODELCHANGEOBSERVER, IID_IKESCMMODELCHANGEOBSERVER))
		subject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IKESCMMODELCHANGEOBSERVER, IID_IKESCMMODELCHANGEOBSERVER);
}

// 終了時に購読を外す。★**パネル周りを畳むより前**に呼ぶこと(KESCMUIStartup.cpp の順序)。
// 購読している間セッションが握っているのは**この .pln の中へのポインタ**で、終了処理中の通知は
// 消えかけのコードで Update を走らせる ---- KESCMDetachPanelVisibilityObserver と同じ理屈。
void KESCMDetachModelChangeObserver()
{
	ISession* session = GetExecutionContextSession();
	IActiveContext* ctx = (session != nil) ? session->GetActiveContext() : nil;
	if (ctx == nil)
		return;
	InterfacePtr<IObserver> obs((IObserver*)ctx->QueryInterface(IID_IKESCMMODELCHANGEOBSERVER));
	if (obs == nil)
		return;
	InterfacePtr<IApplication> app(session->QueryApplication());
	InterfacePtr<ISubject> subject(app, IID_ISUBJECT);
	if (subject == nil)
		return;
	if (subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IKESCMMODELCHANGEOBSERVER, IID_IKESCMMODELCHANGEOBSERVER))
		subject->DetachObserver(ISubject::kRegularAttachment, obs, IID_IKESCMMODELCHANGEOBSERVER, IID_IKESCMMODELCHANGEOBSERVER);
}

// KESCMModelChangeObserver.cpp 終わり。
