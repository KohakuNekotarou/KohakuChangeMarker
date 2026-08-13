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
#include "Utils.h"					// Utils<IKESCMCompareFacade>()
#include "IKESCMCompareFacade.h"	// GetSessionStatus(2026-08-13・分割 第1段 Task 11 で Facade 経由へ)
#include "KESCMModelNotify.h"		// KESCMStatusWantsForceRedraw / 通知に載った付随データ(★これらは
									// まだ Facade に載っていない＝第2段の課題。計画書 Task 16 Step 2 参照)
#include "KESCMUIShared.h"			// KESCMSetStatus(表示。UI 内部専用) / KESCMRefreshPanel
// ★ここから下は**全部 UI 側のヘッダー**。この observer は「通知を受けて画面を作り直す」係なので、
//   UI を呼ぶのが仕事＝逆流ではない。model 側を読む KESCMCore.h も、UI→model という許された向き。
#include "KESCMCore.h"				// KESCMAppIsQuitting(arm 状態は上の Facade で聞く)
#include "IKESCMMarkData.h"			// GetOversetOn(Find Overset が単独 ON 中かどうか＝model の状態を読む)
#include "KESCMPeekGesture.h"		// KESCMResetPeekGestureState / KESCMBatchCloseInProgress / KESCMDeferCloseUi
#include "KESCMThumbIdleTask.h"		// KESCMScheduleThumbRefresh(クローズ後の作り直しを次の idle へ)
#include "KESCMThumbnailRefresh.h"	// KESCMPurgeAllPageThumbs / KESCMForceRedrawPagesPanelNow
#include "KESCMChangeNav.h"			// KESCMResetNav / KESCMRefreshNavPosition
#include "KESCMScrollMap.h"			// KESCMScrollMapAttach / DetachAll / InvalidateAll
#include "KESCMStoryTree.h"			// KESCMStoryTreeRebuild
#include "KESCMStorySection.h"		// KESCMUpdateStorySectionLabel
#include "KESCMViewSync.h"			// KESCMInvalidateSyncCaches

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
		Utils<IKESCMCompareFacade>()->GetSessionStatus(s);
		KESCMSetStatus(s, KESCMStatusWantsForceRedraw());
		return;
	}

	// ---- ここから下が Task 10 で埋めた分(2026-08-13) ----
	//
	// ★★**どれも「何が起きたか」しか受け取らない。** 対象の文書は KESCMNotifiedDocA/DocB で読む
	//   (通知に付随して model が置いたもの)。⚠Stop の後は KESCMArmedTargetDB が nil なので、
	//   「掃除すべき2文書」は付随データからしか分からない ---- そこが Attach 系と違う。

	if (theChange == kKESCMMarksRebuiltMessage || theChange == kKESCMMarksClearedMessage)
	{
		IDataBase* const docA = KESCMNotifiedDocA();		// Target
		IDataBase* const docB = KESCMNotifiedDocB();		// Source(同一文書の比較なら docA と同じ)

		// ビュー同期が持つページ矩形/除外対応表のキャッシュは、比較の組み合わせが変わると無効。
		// (呼び忘れても 250ms の TTL で追従するが、明示すれば次の1通知から正しい)
		KESCMInvalidateSyncCaches();

		if (theChange == kKESCMMarksRebuiltMessage)
		{
			// 比較が成立した＝両方の窓に strip を出す。Attach は「もう入っている窓には入れない」
			// 作りなので、比較のたびに通っても増殖しない。
			if (docA != nil)                 KESCMScrollMapAttach(docA);
			if (docB != nil && docB != docA) KESCMScrollMapAttach(docB);
			KESCMScrollMapInvalidateAll();
		}
		else
		{
			KESCMScrollMapDetachAll();		// Stop: strip を全窓から取り外す
		}

		// Pages パネルのサムネイル。⚠**全ページ Purge**である理由は KESCMThumbnailRefresh.h の
		// KESCMPurgeAllPageThumbs を参照(旧マーク集合を通知で運べないため。戻すには通知に集合を
		// 載せる＝Task 12 で「IKESCMMarkData では戻せない」と判明した。理由は同ヘッダー)。
		// ForceRedraw は2文書ぶんを畳んで最後の1回だけ(2026-07-25 のバッチ化を壊さない)。
		if (docA != nil)                 KESCMPurgeAllPageThumbs(docA, kFalse /*redrawNow*/);
		if (docB != nil && docB != docA) KESCMPurgeAllPageThumbs(docB, kFalse /*redrawNow*/);
		KESCMForceRedrawPagesPanelNow();

		KESCMRefreshPanel();				// Target/Source 名・アイコン・Prev/Next の有効無効

		// ⚠巡回基準点を捨てるのは「全再比較」と「Stop」だけ。差分再比較(登録トグル)で捨てると
		//   ページを登録するたびに Prev/Next が先頭へ戻る＝目に見える挙動変化になる。
		if (KESCMNotifiedNavReset())
			KESCMResetNav();
		KESCMRefreshNavPosition();
		return;
	}

	if (theChange == kKESCMPageFlagsChangedMessage)
	{
		// Register(Added/Removed)や Check(✓)が変わった＝サムネイルの絵と地図の点だけが変わる。
		// パネルの表示内容も Prev/Next の位置も、この通知では変わらない。
		IDataBase* const docA = KESCMNotifiedDocA();
		IDataBase* const docB = KESCMNotifiedDocB();
		if (docA != nil)                 KESCMPurgeAllPageThumbs(docA, kFalse /*redrawNow*/);
		if (docB != nil && docB != docA) KESCMPurgeAllPageThumbs(docB, kFalse /*redrawNow*/);
		KESCMForceRedrawPagesPanelNow();
		KESCMScrollMapInvalidateAll();
		return;
	}

	if (theChange == kKESCMStoryEditsRebuiltMessage)
	{
		// 一覧のモデル(KESCMStoryList)は model 側で作り直し済み。ここは画面を合わせるだけ。
		// ★見出しの件数は KESCMUpdateStorySectionLabel が arm 状態を見て決めるので、順に呼ぶだけでよい。
		//   パネルが閉じていてもセクションが畳まれていても、どちらも中で静かに諦める。
		KESCMStoryTreeRebuild();
		KESCMUpdateStorySectionLabel();
		return;
	}

	if (theChange == kKESCMOversetRescannedMessage)
	{
		// あふれ走査の結果が変わった。⚠比較(marks)とは独立した機能なので、対象は1文書のことも
		//   2文書のこともある(走査先を切り替えた直後は「前の文書」の＋を消す必要がある＝docB)。
		IDataBase* const docA = KESCMNotifiedDocA();	// 今の走査対象
		IDataBase* const docB = KESCMNotifiedDocB();	// 直前の走査対象(切り替え時のみ。無ければ nil)
		if (docA != nil)                 KESCMPurgeAllPageThumbs(docA, kFalse /*redrawNow*/);
		if (docB != nil && docB != docA) KESCMPurgeAllPageThumbs(docB, kFalse /*redrawNow*/);
		KESCMForceRedrawPagesPanelNow();

		if (docA != nil)
			KESCMScrollMapAttach(docA);	// 比較していなくても地図は出す(単独点検の経路)
		KESCMScrollMapInvalidateAll();

		if (KESCMNotifiedNavReset())
			KESCMResetNav();
		KESCMRefreshNavPosition();		// Prev/Next の対象数(比較＋あふれ)を作り直す
		return;
	}

	if (theChange == kKESCMComparisonDocsClosedMessage)
	{
		// ★★**この分岐だけ「いつやるか」を自分で決める。** 文書が閉じた後始末は、終了中なら触っては
		//   いけないし、一括クローズの最中なら全部閉じ終わるまで待つべきで、そのどちらも**UI の都合**。
		//   model は「文書が閉じて状態を捨てた」と言うだけ(2026-08-13・Task 10 でこちらへ移した)。

		// どの文書が閉じても要るもの ---- ページ構成も db ポインタも当てにならなくなった以上、
		// ビュー同期のページ矩形/除外対応キャッシュは捨てる。コンテナを空にするだけ＝終了中でも安全。
		KESCMInvalidateSyncCaches();

		// 「比較が終わった(Stop 相当のフルクリーンアップが走った)」かどうかは navReset で分かる。
		// 比較と無関係な文書が閉じただけなら kFalse で、下の重い後片付けは要らない。
		const bool16 comparisonEnded = KESCMNotifiedNavReset();
		if (comparisonEnded)
		{
			KESCMResetPeekGestureState();	// 押下中の覗き状態
			KESCMResetNav();				// 巡回の基準点(閉じた文書のページ UID を持ち越さない)
		}

		// ★終了中は widget へ触らない。窓もパネルも解体中でありうる ---- 解体中の widget を触るのが
		//   Mac 限定 crash-on-quit の典型形。窓ごと消えるので strip を外す意味も無い。
		if (KESCMAppIsQuitting())
			return;

		// ★一括クローズ(複数文書を続けて閉じる)の最中は保留し、全部閉じ終わった通知でまとめて
		//   1回だけ流す(2026-07-27 の仕組みをそのまま使う)。
		if (KESCMBatchCloseInProgress())
		{
			KESCMDeferCloseUi();
			return;
		}

		if (comparisonEnded)
		{
			// strip: ★Find Overset が(走査文書が生存したまま)単独 ON 中なら**残して**赤帯だけ描き直す。
			//   overset 文書自身が閉じた場合は model 側で DropOverset 済み＝sOversetOn が false なので
			//   通常どおり撤去される(2026-07-24)。
			if (Utils<IKESCMMarkData>()->GetOversetOn())
				KESCMScrollMapInvalidateAll();
			else
				KESCMScrollMapDetachAll();

			// ★サムネイルは**その場で作り直さず次の idle へ遅延**させる。閉じたのが Target で生存側が
			//   これからアクティブ化する場合、前面切替の過渡では ForceRedraw しても再生成が起こりきらず
			//   枠が残る(2026-07-08 実機で確認)。nil はスケジューラ側で弾かれ、重複 db も集約される。
			KESCMScheduleThumbRefresh(KESCMNotifiedDocA());
			KESCMScheduleThumbRefresh(KESCMNotifiedDocB());
			KESCMScheduleThumbRefresh(KESCMNotifiedDocC());
		}

		KESCMRefreshPanel();	// パネルの ON/OFF 表示を実状態へ合わせる(「ON 固着」の解消)
		return;
	}
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
