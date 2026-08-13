//========================================================================================
//
//  KESCMModelNotify.cpp
//
//  model→UI の唯一の通り道(2026-08-13・model/UI 分割 第1段 Task 9 で新設)。
//  詳しい理由は KESCMModelNotify.h の冒頭。
//
//  ★ここは **model 側**。UI のヘッダーを1本も include しない ---- それがこのファイルの存在理由。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "ISession.h"			// GetExecutionContextSession(終了処理中は nil になり得る)
#include "IApplication.h"
#include "ISubject.h"

#include "KESCMID.h"
#include "KESCMModelNotify.h"

//----------------------------------------------------------------------------------------
// 今セッションで最後に出したステータス文字列。
//
// ★★**保持は model 側**(設計書 §3.3 の決定)。理由は2つ:
//   ①`app.kcmStatus`(KESCMScriptProvider.cpp ＝ model 側)が**パネルを閉じていても答える**という
//     既存の仕様。widget から読む作りだと、閉じている間は空になる。
//   ②パネルは再表示のたびに widget を作り直すので、widget に持たせた値は生き残らない
//     (StaticMultiLineTextWidget の内容はワークスペースに永続化されるため、逆に**前回セッションの
//     文字列が残る**という別の困りごともある。AutoAttach がここの値で必ず上書きする)。
//
// ⚠2026-08-13 に KESCMPanelObserver.cpp(UI 側)からここへ移した。表示だけが UI に残る。
//----------------------------------------------------------------------------------------
static PMString sSessionStatus;
static bool16   sStatusForceRedraw = kFalse;

// アプリの subject を引く。終了処理中は session/app が引けないので、その場合は nil を返して
// 呼び手が静かに諦める(KESCM 全体の共通規約=閉じた/消えた相手は触らない)。
static ISubject* KESCMQueryAppSubject()
{
	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return nil;
	InterfacePtr<IApplication> app(session->QueryApplication());
	if (app == nil)
		return nil;
	return (ISubject*)app->QueryInterface(IID_ISUBJECT);
}

// KESCMNotify(KESCMModelNotify.h で宣言)
void KESCMNotify(ClassID theChange)
{
	InterfacePtr<ISubject> subject(KESCMQueryAppSubject());
	if (subject == nil)
		return;		// 終了処理中など。誰も聞いていないのと同じ＝何もしないのが正しい

	// ★protocol は自作の IID_IKESCMMODELCHANGEOBSERVER。これで「この通知は KESCM の UI 宛」と分かる。
	//   ⚠既存3本の Observer が **IID_IAPPLICATION の通知**を受けているのと違い、こちらは
	//     自作 protocol で送る＝本体の通知と混ざらない。
	subject->Change(theChange, IID_IKESCMMODELCHANGEOBSERVER);
}

// KESCMNotifyStatus(KESCMModelNotify.h で宣言)
void KESCMNotifyStatus(const PMString& s, bool16 forceRedrawNow)
{
	sSessionStatus = s;
	sStatusForceRedraw = forceRedrawNow;
	KESCMNotify(kKESCMStatusTextMessage);
}

// KESCMStoreSessionStatus(KESCMModelNotify.h で宣言) — 通知を出さずに覚えるだけ。
// ★呼び手は UI 側の KESCMSetStatus ただ1つ。UI の操作で出したメッセージは UI が自分で描くので
//   通知を通す必要が無いが、**覚える場所は1つ**でなければならない(app.kcmStatus が答える値・
//   パネル再表示時に復元する値)。⚠ここで通知を出すと observer→KESCMSetStatus→ここ、と輪になる。
void KESCMStoreSessionStatus(const PMString& s)
{
	sSessionStatus = s;
}

// KESCMStatusWantsForceRedraw(KESCMModelNotify.h で宣言)
bool16 KESCMStatusWantsForceRedraw()
{
	return sStatusForceRedraw;
}

// KESCMGetSessionStatus(KESCMModelNotify.h で宣言)
void KESCMGetSessionStatus(PMString& out)
{
	out = sSessionStatus;
	out.SetTranslatable(kFalse);	// 状態表示は組み立て済みの文で翻訳キーではない
}

// KESCMClearSessionStatus(KESCMModelNotify.h で宣言)
void KESCMClearSessionStatus()
{
	sSessionStatus.Clear();
	sStatusForceRedraw = kFalse;
}

// KESCMModelNotify.cpp 終わり。
