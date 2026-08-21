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
//     (欄の内容はワークスペースに永続化されるため、逆に**前回セッションの文字列が残る**という
//     別の困りごともある。AutoAttach がここの値で必ず上書きする。⚠2026-08-20 に欄が自前描画へ
//     替わり、持ち物は永続の ITextControlData でなく**非永続の IKESCMStatusTextData** になった
//     ＝残るのは「widget の文字列」ではなく「ワークスペースに残った widget そのもの」だが、
//     いずれにせよ上書きするこの経路が答えなので、作法は変わらない)。
//
// ⚠2026-08-13 に KESCMPanelObserver.cpp(UI 側)からここへ移した。表示だけが UI に残る。
//----------------------------------------------------------------------------------------
// ⚠**ここに残る static は「セッションの状態」だけ**(2026-08-20 に 1本→4本 へ分かれたが、
//   答えているのは同じ1つの値＝メッセージ欄に出ている文)。通知の付随物(どの文書か・巡回を捨てるか・
//   即時再描画か)は 2026-08-15 の API 監査 B2 で **static をやめ、Change の changedBy へ移した**
//   (理由は KESCMModelNotify.h の KESCMNotifyPayload)。この文字列だけが残る理由は上の①②＝
//   **通知の付随物ではなく、通知と無関係にいつでも答える値**だから。
// ★★2026-08-20: **1本から4本へ**。パネルのメッセージ欄が自前描画になり、変更行をクリックした
//   ときのメッセージは「見出し／前の文脈／変更された文字／後の文脈」に分かれて届く
//   (KESCMStatusTextView.cpp)。**分け目を持つ場所は、文字列を持つ場所と同じでなければならない**
//   ＝別々に持つと、パネルを開き直したときに文と色分けが食い違いうる。
//   ★普通のメッセージは sStatusMid だけが埋まる＝**「1本の文字列」はこの形の特別な場合**。
//   ⚠4本とも KESCMClearSessionStatus() の列挙に載っていること(下)。
static PMString sStatusLabel;
static PMString sStatusPre;
static PMString sStatusMid;
static PMString sStatusPost;

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
void KESCMNotify(ClassID theChange, const KESCMNotifyPayload* payload)
{
	InterfacePtr<ISubject> subject(KESCMQueryAppSubject());
	if (subject == nil)
		return;		// 終了処理中など。誰も聞いていないのと同じ＝何もしないのが正しい

	// ★protocol は自作の IID_IKESCMMODELCHANGEOBSERVER。これで「この通知は KESCM の UI 宛」と分かる。
	//   ⚠既存3本の Observer が **IID_IAPPLICATION の通知**を受けているのと違い、こちらは
	//     自作 protocol で送る＝本体の通知と混ざらない。
	// ★★第3引数 changedBy に付随データの**アドレス**を載せる(ISubject.h:150)。受け手には
	//   IObserver::Update の第4引数としてそのまま届く。Change は同期なので、呼び手のスタックに
	//   置いた構造体が配り終わるまで生きている＝寿命管理が要らない。
	//   ⚠payload が nil のこともある(付随物を持たない通知)。受け手は必ず nil を見ること。
	subject->Change(theChange, IID_IKESCMMODELCHANGEOBSERVER, (void*)payload);
}

//----------------------------------------------------------------------------------------
// 通知の付随データ(2026-08-13・Task 10 / ★2026-08-15 の API 監査 B2 で static から payload へ)。
//
// ★★**static はもう無い。** 「どの文書か」「Prev/Next の基準点を捨てるか」は呼び手のスタックに
//   構造体を1つ置き、そのアドレスを Change の changedBy に載せて配る。Change は同期なので、
//   配り終わるまで構造体は生きている ---- ∴ **後始末そのものが要らない**(旧実装が配布後に
//   4本を nil へ戻していたのは、static だったからやらねばならなかった仕事)。
// ⚠受け手は payload を**持ち越さない**。閉じた文書の db を後から触ると落ちる(KESCM 全体の共通規約)。
//----------------------------------------------------------------------------------------

// KESCMNotifyDocs(KESCMModelNotify.h で宣言) — 2文書版。
void KESCMNotifyDocs(ClassID theChange, IDataBase* docA, IDataBase* docB, bool16 navReset)
{
	KESCMNotifyPayload payload;
	payload.fDocA     = docA;
	payload.fDocB     = docB;
	payload.fNavReset = navReset;

	KESCMNotify(theChange, &payload);
}

// KESCMNotifyDocs(KESCMModelNotify.h で宣言) — 3文書版。
// ⚠**2文書版へは合流しない**(合流すると docC を渡す口が無い)。旧実装は static だったので
//   「docC を先に置いてから2文書版を呼ぶ」ことができたが、payload は呼び手が丸ごと組む。
void KESCMNotifyDocs(ClassID theChange, IDataBase* docA, IDataBase* docB, IDataBase* docC, bool16 navReset)
{
	KESCMNotifyPayload payload;
	payload.fDocA     = docA;
	payload.fDocB     = docB;
	payload.fDocC     = docC;
	payload.fNavReset = navReset;

	KESCMNotify(theChange, &payload);
}

// KESCMNotifyPages(KESCMModelNotify.h で宣言) — 1文書＋「絵が変わったページ集合」版。
// ★2026-08-16(API 監査 B4)。**集合はコピーせずアドレスだけを載せる**——Change は同期なので、
//   呼び手のスタック(あるいは呼び手が持っている変数)がそのまま配り終わるまで生きている。
//   文書ポインタ2本を運ぶのと**まったく同じ仕組み**で、新しい機構は1つも要らなかった。
void KESCMNotifyPages(ClassID theChange, IDataBase* doc, const std::set<UID>& pages)
{
	KESCMNotifyPayload payload;
	payload.fDocA   = doc;
	payload.fPagesA = &pages;

	KESCMNotify(theChange, &payload);
}

// KESCMNotifyDocsPages(KESCMModelNotify.h で宣言) — 2文書＋各文書の「絵が変わったページ集合」版。
// ★2026-08-16(API 監査 B5)。B4 の1文書版(上)と**まったく同じ仕組み**で、違いは集合が2つ載ることだけ。
//   ⚠2文書版を別関数にしてあるのは、片方だけ集合を持つ呼び手を作らせないため——受け手は
//     「集合がある＝そのページだけ見ればよい」と読むので、**半分だけ正しい集合は取りこぼしになる**。
void KESCMNotifyDocsPages(ClassID theChange,
                          IDataBase* docA, const std::set<UID>& pagesA,
                          IDataBase* docB, const std::set<UID>& pagesB,
                          bool16 navReset)
{
	KESCMNotifyPayload payload;
	payload.fDocA     = docA;
	payload.fDocB     = docB;
	payload.fPagesA   = &pagesA;
	payload.fPagesB   = &pagesB;
	payload.fNavReset = navReset;

	KESCMNotify(theChange, &payload);
}

// KESCMNotifyStatus(KESCMModelNotify.h で宣言)
// ★文字列は static のまま(セッションの状態＝app.kcmStatus がいつでも答える値)、
//   「今すぐ描き直せ」だけが payload に乗る(その通知に限った付随物)。
void KESCMNotifyStatus(const PMString& s, bool16 forceRedrawNow)
{
	// ★model が出すメッセージに分け目は無い＝本文1片。前のメッセージの見出しや文脈が残ると、
	//   新しい文の周りに古い言葉が並ぶので、書かない3本は**空にする**(消し忘れは残留になる)。
	sStatusLabel.Clear();
	sStatusPre.Clear();
	sStatusMid = s;
	sStatusPost.Clear();

	KESCMNotifyPayload payload;
	payload.fStatusForceRedraw = forceRedrawNow;

	KESCMNotify(kKESCMStatusTextMessage, &payload);
}

// KESCMStoreSessionStatus(KESCMModelNotify.h で宣言) — 通知を出さずに覚えるだけ。
// ★呼び手は UI 側の KESCMSetStatus ただ1つ。UI の操作で出したメッセージは UI が自分で描くので
//   通知を通す必要が無いが、**覚える場所は1つ**でなければならない(app.kcmStatus が答える値・
//   パネル再表示時に復元する値)。⚠ここで通知を出すと observer→KESCMSetStatus→ここ、と輪になる。
void KESCMStoreSessionStatus(const PMString& s)
{
	// ★分け目の無いメッセージ＝本文1片(上の KESCMNotifyStatus と同じ理由で他の3本を空にする)。
	sStatusLabel.Clear();
	sStatusPre.Clear();
	sStatusMid = s;
	sStatusPost.Clear();
}

// KESCMStoreSessionStatusSegments(KESCMModelNotify.h で宣言) — 分け目つきで覚えるだけ。
// ★呼び手は UI 側の KESCMSetStatusSegments ただ1つ(変更行のジャンプが「もう一方の側」を出す経路)。
void KESCMStoreSessionStatusSegments(const PMString& label, const PMString& pre,
									 const PMString& mid, const PMString& post)
{
	sStatusLabel = label;
	sStatusPre   = pre;
	sStatusMid   = mid;
	sStatusPost  = post;
}

// KESCMGetSessionStatus(KESCMModelNotify.h で宣言)
void KESCMGetSessionStatus(PMString& out)
{
	// ★4片を**画面に見えているとおりに**繋ぐ＝見出しの後ろで改行し、本文は前の文脈・変更された
	//   文字・後の文脈の順。分け目の無いメッセージでは3本が空なので、答えはその文字列そのもの
	//   ＝app.kcmStatus の答えは分割の前と1文字も変わらない。
	out.Clear();
	if (!sStatusLabel.IsEmpty())
	{
		out.Append(sStatusLabel);
		out.Append("\n");
	}
	out.Append(sStatusPre);
	out.Append(sStatusMid);
	out.Append(sStatusPost);
	out.SetTranslatable(kFalse);	// 状態表示は組み立て済みの文で翻訳キーではない
}

// KESCMGetSessionStatusSegments(KESCMModelNotify.h で宣言)
void KESCMGetSessionStatusSegments(PMString& outLabel, PMString& outPre,
								   PMString& outMid, PMString& outPost)
{
	outLabel = sStatusLabel;	outLabel.SetTranslatable(kFalse);
	outPre   = sStatusPre;		outPre.SetTranslatable(kFalse);
	outMid   = sStatusMid;		outMid.SetTranslatable(kFalse);
	outPost  = sStatusPost;		outPost.SetTranslatable(kFalse);
}

// KESCMClearSessionStatus(KESCMModelNotify.h で宣言)
void KESCMClearSessionStatus()
{
	// ⚠★★**4本とも**。1本でも落とすと、そのぶんのヒープが終了時に残る
	//   (2026-08-18 の model B8 / UI B-U5 で実際に見つかった形)。
	sStatusLabel.Clear();
	sStatusPre.Clear();
	sStatusMid.Clear();
	sStatusPost.Clear();
}

// KESCMModelNotify.cpp 終わり。
