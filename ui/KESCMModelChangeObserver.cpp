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

#include "KCMUIID.h"
#include "Utils.h"					// Utils<IKESCMCompareFacade>()
#include "IKESCMCompareFacade.h"	// GetSessionStatus / IsAppQuitting(第1段 Task 11)
#include "KESCMModelNotify.h"		// KESCMNotifyPayload ---- ★**型だけを借りる**(2026-08-15・API 監査 B2)
									// ⚠このヘッダーが宣言する**自由関数**は model 側にしか実体が無いので
									//   別 .pln からはリンクできない。**struct の定義はリンクを要さない**
									//   ので型だけは借りてよい ---- IKESCMMarkData.h が KESCMOversetScan.h
									//   から KESCMOversetLoc を借りているのと同じ形。**関数は呼ばないこと。**
#include "KESCMUIShared.h"			// KESCMSetStatus(表示。UI 内部専用) / KESCMRefreshPanel
// ★ここから下は**全部 UI 側のヘッダー**。この observer は「通知を受けて画面を作り直す」係なので、
//   UI を呼ぶのが仕事＝逆流ではない。model 側を読む KESCMCore.h も、UI→model という許された向き。
#include "IKESCMMarkData.h"			// GetOversetOn(Find Overset が単独 ON 中かどうか＝model の状態を読む)
#include "KESCMPeekGesture.h"		// KESCMResetPeekGestureState / KESCMBatchCloseInProgress / KESCMDeferCloseUi
#include "KESCMThumbIdleTask.h"		// KESCMScheduleThumbRefresh(クローズ後の作り直しを次の idle へ)
#include "KESCMThumbnailRefresh.h"	// KESCMPurgeAllPageThumbs / KESCMRefreshThumbnailsForPages /
									// KESCMForceRedrawPagesPanelNow
#include "KESCMChangeNav.h"			// KESCMResetNav / KESCMRefreshNavPosition
#include "KESCMScrollMap.h"			// KESCMScrollMapAttach / DetachAll / InvalidateAll
#include "KESCMStoryTree.h"			// KESCMStoryTreeRebuild
#include "KESCMStorySection.h"		// KESCMUpdateStorySectionLabel
#include "KESCMViewSync.h"			// KESCMInvalidateSyncCaches

#include <set>						// 同一文書比較のときに2つのページ集合を合わせる(API 監査 B5)

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

void KESCMModelChangeObserver::Update(const ClassID& theChange, ISubject* /*theSubject*/, const PMIID& protocol, void* changedBy)
{
	// 自作 protocol で送られたものだけを見る(本体の通知と混ざらない)。
	if (protocol != IID_IKESCMMODELCHANGEOBSERVER)
		return;

	// ★★通知の付随データ。model が ISubject::Change の第3引数に載せてきたものが、そのまま
	//   Update の第4引数として届く(ISubject.h:150。本体の実例＝linksui の
	//   EditOriginalResumeObserver.cpp:127)。2026-08-15 の API 監査 B2 まで、これは model 側の
	//   static に置かれ Facade 5本で取りに行っていた ---- **通知は ClassID しか運べない**という
	//   誤った前提のため。
	// ⚠**nil のことがある**(付随物を持たない通知)。空の値を1つ用意して参照を振り替えることで、
	//   分岐ごとの nil 判定を書かずに済ませる。
	const KESCMNotifyPayload  kEmptyPayload;
	const KESCMNotifyPayload& n = (changedBy != nil) ? *(const KESCMNotifyPayload*)changedBy
													 : kEmptyPayload;

	if (theChange == kKESCMStatusTextMessage)
	{
		// ★文字列は model 側が持っている(app.kcmStatus がいつでも答える値＝セッションの状態)。
		//   一方「今すぐ描き直せ」はこの通知に限った付随物なので payload から読む。
		PMString s;
		Utils<IKESCMCompareFacade>()->GetSessionStatus(s);
		KESCMSetStatus(s, n.fStatusForceRedraw);
		return;
	}

	// ---- ここから下が Task 10 で埋めた分(2026-08-13) ----
	//
	// ★★**どれも「何が起きたか」しか ClassID では分からない。** 対象の文書は payload の fDocA/fDocB。
	//   ⚠Stop の後は GetArmedTargetDB が nil なので、「掃除すべき2文書」は付随データからしか
	//   分からない ---- そこが Attach 系と違う。

	if (theChange == kKESCMMarksRebuiltMessage || theChange == kKESCMMarksClearedMessage)
	{
		IDataBase* const docA = n.fDocA;		// Target
		IDataBase* const docB = n.fDocB;		// Source(同一文書の比較なら docA と同じ)

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

		// Pages パネルのサムネイル。★★2026-08-16(API 監査 B5): **部分再比較(Refresh Page Comparison)は
		// 触れたページ集合を載せてくる**ので、そのページだけを per-UID Purge する。
		// ⚠**全ページ Purge が残るのは2つの場合だけ**:
		//   ①全再比較(KESCMDoMarkChangesDoc)と Stop ---- 絞り込みに要るのは「再比較の**前**に枠が
		//     付いていた旧集合」で、この通知が出る時点で既に捨てられている。⇒ **運べないのではなく、
		//     載せる物が手元に無い**(載せるには model 側で先に退避する必要がある＝未実施)。
		//   ②集合が付いていない通知(送り手が集合を持たない場合の逃げ道)。
		// ★旧記述「旧マーク集合を通知で運べないため」は誤り＝changedBy で運べる(2026-08-15 の監査 B2)。
		// ⚠**片方だけ集合が付く通知は作らない**(KESCMNotifyDocsPages が必ず両方まとめて載せる)。
		//   docA だけ絞って docB は全ページ、のような混在にすると、どちらが正しいのか読めなくなる。
		// ForceRedraw は2文書ぶんを畳んで最後の1回だけ(2026-07-25 のバッチ化を壊さない)。
		if (n.fPagesA != nil && n.fPagesB != nil)
		{
			if (docB == docA)
			{
				// 同一文書どうしの比較。2つの集合は同じ文書のページなので、合わせて1回で Purge する
				// (下の分岐のまま docB を落とすと、Source 側として触れたページが**取りこぼしになる**)。
				std::set<UID> both(*n.fPagesA);
				both.insert(n.fPagesB->begin(), n.fPagesB->end());
				if (docA != nil) KESCMRefreshThumbnailsForPages(docA, both, kFalse /*redrawNow*/);
			}
			else
			{
				if (docA != nil) KESCMRefreshThumbnailsForPages(docA, *n.fPagesA, kFalse /*redrawNow*/);
				if (docB != nil) KESCMRefreshThumbnailsForPages(docB, *n.fPagesB, kFalse /*redrawNow*/);
			}
		}
		else
		{
			if (docA != nil)                 KESCMPurgeAllPageThumbs(docA, kFalse /*redrawNow*/);
			if (docB != nil && docB != docA) KESCMPurgeAllPageThumbs(docB, kFalse /*redrawNow*/);
		}
		KESCMForceRedrawPagesPanelNow();

		KESCMRefreshPanel();				// Target/Source 名・アイコン・Prev/Next の有効無効

		// ⚠巡回基準点を捨てるのは「全再比較」と「Stop」だけ。差分再比較(登録トグル)で捨てると
		//   ページを登録するたびに Prev/Next が先頭へ戻る＝目に見える挙動変化になる。
		if (n.fNavReset)
			KESCMResetNav();
		KESCMRefreshNavPosition();
		return;
	}

	if (theChange == kKESCMPageFlagsChangedMessage)
	{
		// Register(Added/Removed)や Check(✓)が変わった＝サムネイルの絵と地図の点だけが変わる。
		// パネルの表示内容も Prev/Next の位置も、この通知では変わらない。
		IDataBase* const docA = n.fDocA;
		IDataBase* const docB = n.fDocB;

		// ★★2026-08-16(API 監査 B4): **どのページが変わったかが通知に載ってくる**ようになったので、
		//   そのページだけを per-UID Purge する。Task 10 以来ここは db の全ページを作り直していた
		//   ---- 理由が「通知は ClassID しか運べない」という**誤った前提**だったため
		//   (2026-08-15 の監査 B2 で覆っていたのに、この分岐まで訂正が配られていなかった)。
		//   ⚠fPagesA が nil の通知は従来どおり全ページ(送り手が集合を持たない場合の逃げ道)。
		if (n.fPagesA != nil)
		{
			KESCMRefreshThumbnailsForPages(docA, *n.fPagesA, kFalse /*redrawNow*/);
		}
		else
		{
			if (docA != nil)                 KESCMPurgeAllPageThumbs(docA, kFalse /*redrawNow*/);
			if (docB != nil && docB != docA) KESCMPurgeAllPageThumbs(docB, kFalse /*redrawNow*/);
		}
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
		IDataBase* const docA = n.fDocA;	// 今の走査対象
		IDataBase* const docB = n.fDocB;	// 直前の走査対象(切り替え時のみ。無ければ nil)
		// ★★2026-08-16(API 監査 B5): 送り手が「＋の絵が変わりうるページ」を載せてくるので per-UID
		//   Purge。同一文書の走り直しなら fPagesA が**新 ∪ 旧**、別文書へ移ったなら fPagesB が
		//   前の文書の旧集合(KESCMOversetApply.cpp)。⚠集合が無い通知は従来どおり全ページ。
		if (n.fPagesA != nil && n.fPagesB != nil)
		{
			if (docA != nil) KESCMRefreshThumbnailsForPages(docA, *n.fPagesA, kFalse /*redrawNow*/);
			if (docB != nil && docB != docA)
				KESCMRefreshThumbnailsForPages(docB, *n.fPagesB, kFalse /*redrawNow*/);
		}
		else
		{
			if (docA != nil)                 KESCMPurgeAllPageThumbs(docA, kFalse /*redrawNow*/);
			if (docB != nil && docB != docA) KESCMPurgeAllPageThumbs(docB, kFalse /*redrawNow*/);
		}
		KESCMForceRedrawPagesPanelNow();

		if (docA != nil)
			KESCMScrollMapAttach(docA);	// 比較していなくても地図は出す(単独点検の経路)
		KESCMScrollMapInvalidateAll();

		if (n.fNavReset)
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
		// ★2026-08-15(API 監査 B2)以降、この分岐が Facade に聞くのは IsAppQuitting の**1回だけ**に
		//   なったので InterfacePtr で引き回すのをやめた(3回以上なら引く＝[[utils-boss-facade-access]])。
		const bool16 comparisonEnded = n.fNavReset;
		if (comparisonEnded)
		{
			KESCMResetPeekGestureState();	// 押下中の覗き状態
			KESCMResetNav();				// 巡回の基準点(閉じた文書のページ UID を持ち越さない)
		}

		// ★終了中は widget へ触らない。窓もパネルも解体中でありうる ---- 解体中の widget を触るのが
		//   Mac 限定 crash-on-quit の典型形。窓ごと消えるので strip を外す意味も無い。
		if (Utils<IKESCMCompareFacade>()->IsAppQuitting())
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
			KESCMScheduleThumbRefresh(n.fDocA);
			KESCMScheduleThumbRefresh(n.fDocB);
			KESCMScheduleThumbRefresh(n.fDocC);
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
