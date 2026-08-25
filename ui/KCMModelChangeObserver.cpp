//========================================================================================
//
//  KCMModelChangeObserver.cpp
//
//  model が投げた通知を受けて画面を作り直す **UI 側**の1本(2026-08-13・model/UI 分割 第1段 Task 9)。
//  送り手は KCMModelNotify.cpp、通知の種類は KCMBoundaryID.h の kKCM*Message
//  (★境界を跨ぐ ID なので両側が同じコピーを持つ。model 専用の KCMID.h ではない)。
//
//  ★同居先は kActiveContextBoss。既存3本(レイアウト同期 / 一括クローズ / パネル表示)と同じ実証済みの
//    構成で、**新しい機構は何も足していない**。
//
//  ★**通知7種すべてが繋がっている**(ステータス行が Task 9、残り6種が Task 10。2026-08-13)。
//    ⚠**種類は7・分岐は6**＝Marks の Rebuilt と Cleared だけが1つの分岐に入る(KCMBoundaryID.h の
//      kKCM*Message は7本)。以後この2つの数を混ぜないこと。
//    ⚠2026-08-16(監査 B-U2)にこの節を書き直した。旧記述は「**残りの分岐は空のまま置いてある**——
//    埋めるのは Task 10」で、**その Task 10 が同じ日に終わったあとも3日間そのまま**だった
//    ＝**下の "ここから下が Task 10 で埋めた分" の見出しが完了を書いているのに、冒頭は空だと
//      言い続けていた。** ⇒ ★**段階実装の「まだ」は、その段階が終わった日に消す**
//    (B9 で拾った「これから測る」と同じ型)。
//    ⚠★2026-08-18(不具合再検査 B-U2)＝**その旧記述が引いていた行番号 ":90" は、書いた日から既に
//      2行ずれていた**(B-U2 の時点の実体は :92)。行番号でよそを指す引用は**書いた日にしか正しくない**
//      ので、このファイルの2件とも**見出しの語で引く形**へ直した。
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
#include "Utils.h"					// Utils<IKCMCompareFacade>()
#include "IKCMCompareFacade.h"	// GetSessionStatus / IsAppQuitting(第1段 Task 11)
#include "KCMModelNotify.h"		// KCMNotifyPayload ---- ★**型だけを借りる**(2026-08-15・API 監査 B2)
									// ⚠このヘッダーが宣言する**自由関数**は model 側にしか実体が無いので
									//   別 .pln からはリンクできない。**struct の定義はリンクを要さない**
									//   ので型だけは借りてよい ---- IKCMMarkData.h が KCMOversetScan.h
									//   から KCMOversetLoc を借りているのと同じ形。**関数は呼ばないこと。**
#include "KCMUIShared.h"			// KCMSetStatus(表示。UI 内部専用) / KCMRefreshPanel
// ★ここから下は**全部 UI 側のヘッダー**。この observer は「通知を受けて画面を作り直す」係なので、
//   UI を呼ぶのが仕事＝逆流ではない。model 側を読む KCMCore.h も、UI→model という許された向き。
#include "IKCMMarkData.h"			// GetOversetOn(Find Overset が単独 ON 中かどうか＝model の状態を読む)
#include "KCMPeekGesture.h"		// KCMResetPeekGestureState / KCMBatchCloseInProgress / KCMDeferCloseUi
#include "KCMThumbIdleTask.h"		// KCMScheduleThumbRefresh(クローズ後の作り直しを次の idle へ)
#include "KCMThumbnailRefresh.h"	// KCMPurgeAllPageThumbs / KCMRefreshThumbnailsForPages /
									// KCMForceRedrawPagesPanelNow
#include "KCMChangeNav.h"			// KCMResetNav / KCMRefreshNavPosition
#include "KCMScrollMap.h"			// KCMScrollMapAttach / DetachAll / InvalidateAll
#include "KCMStoryTree.h"			// KCMStoryTreeRebuild
#include "KCMStorySection.h"		// KCMUpdateStorySectionLabel
#include "KCMStoryPressMarks.h"	// KCMStoryMarksRefresh(常時表示マークを比較結果に追随させる)
#include "KCMViewSync.h"			// KCMInvalidateSyncCaches

#include <set>						// 同一文書比較のときに2つのページ集合を合わせる(API 監査 B5)

/* model の通知を受ける UI 側のオブザーバ。kActiveContextBoss に IID_IKCMMODELCHANGEOBSERVER として
   同居させている(同居先の理由はレイアウト同期オブザーバと同じ=実証済みの構成)。購読先はアプリの subject。 */
class KCMModelChangeObserver : public CObserver
{
public:
	KCMModelChangeObserver(IPMUnknown* boss) : CObserver(boss, IID_IKCMMODELCHANGEOBSERVER) {}
	~KCMModelChangeObserver() {}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);
};

CREATE_PMINTERFACE(KCMModelChangeObserver, kKCMModelChangeObserverImpl)

void KCMModelChangeObserver::Update(const ClassID& theChange, ISubject* /*theSubject*/, const PMIID& protocol, void* changedBy)
{
	// 自作 protocol で送られたものだけを見る(本体の通知と混ざらない)。
	if (protocol != IID_IKCMMODELCHANGEOBSERVER)
		return;

	// ★★通知の付随データ。model が ISubject::Change の第3引数に載せてきたものが、そのまま
	//   Update の第4引数として届く(ISubject.h:150。本体の実例＝linksui の
	//   EditOriginalResumeObserver.cpp:127)。2026-08-15 の API 監査 B2 まで、これは model 側の
	//   static に置かれ Facade 5本で取りに行っていた ---- **通知は ClassID しか運べない**という
	//   誤った前提のため。
	// ⚠**nil のことがある**(付随物を持たない通知)。空の値を1つ用意して参照を振り替えることで、
	//   分岐ごとの nil 判定を書かずに済ませる。
	const KCMNotifyPayload  kEmptyPayload;
	const KCMNotifyPayload& n = (changedBy != nil) ? *(const KCMNotifyPayload*)changedBy
													 : kEmptyPayload;

	if (theChange == kKCMStatusTextMessage)
	{
		// ★文字列は model 側が持っている(app.kcmStatus がいつでも答える値＝セッションの状態)。
		//   一方「今すぐ描き直せ」はこの通知に限った付随物なので payload から読む。
		PMString s;
		Utils<IKCMCompareFacade>()->GetSessionStatus(s);
		KCMSetStatus(s, n.fStatusForceRedraw);
		return;
	}

	// ---- ここから下が Task 10 で埋めた分(2026-08-13) ----
	//
	// ★★**どれも「何が起きたか」しか ClassID では分からない。** 対象の文書は payload の fDocA/fDocB。
	//   ⚠Stop の後は GetArmedTargetDB が nil なので、「掃除すべき2文書」は付随データからしか
	//   分からない ---- そこが Attach 系と違う。

	if (theChange == kKCMMarksRebuiltMessage || theChange == kKCMMarksClearedMessage)
	{
		IDataBase* const docA = n.fDocA;		// Target
		IDataBase* const docB = n.fDocB;		// Source(同一文書の比較なら docA と同じ)

		// ビュー同期が持つページ矩形/除外対応表のキャッシュは、比較の組み合わせが変わると無効。
		// (呼び忘れても 250ms の TTL で追従するが、明示すれば次の1通知から正しい)
		KCMInvalidateSyncCaches();

		// ★Story モードの常時表示マークも「比較そのもの」なので、ここで作り直す(2026-08-22)。
		//   Stop なら arm が落ちているので中で消え、比較が成立したなら新しい結果で出し直す。
		//   ⚠Pixel モードの比較では下の StoryEditsRebuilt が飛ばないことがあるので、**両方の口で呼ぶ**
		//     ＝Story で出したまま Pixel に切り替えて比較したときに、古いマークが残らない。
		KCMStoryMarksRefresh();

		if (theChange == kKCMMarksRebuiltMessage)
		{
			// 比較が成立した＝両方の窓に strip を出す。Attach は「もう入っている窓には入れない」
			// 作りなので、比較のたびに通っても増殖しない。
			if (docA != nil)                 KCMScrollMapAttach(docA);
			if (docB != nil && docB != docA) KCMScrollMapAttach(docB);
			KCMScrollMapInvalidateAll();
		}
		else if (docA != nil || docB != nil)
		{
			// Stop: strip を全窓から取り外す。
			// ⚠★★**文書が載っていない Cleared では触らない**(2026-08-18・不具合再検査 B-U2)。
			//   送り手(KCMStopComparison の末尾)は「disarm を終えてからパネルの見た目だけ作り直す」
			//   ために docA/docB を nil で投げてくる。Stop の順序はこうなっている:
			//     ① KCMDoClearMarks が投げる Cleared(文書つき) …… ここで全窓から strip を撤去
			//     ② KCMApplyOversetForDoc …… Find Overset が単独 ON なら overset 文書へ**貼り直す**
			//     ③ この2本目の Cleared(文書なし)
			//   絞らないと③が②を剥がす ---- ①で既に全部外れている以上、**③の撤去が持ちうる効果は
			//   ②の打ち消しだけ**だった(＝二度手間ではなく、それ自体が不具合)。
			//   ★絞りの形は上の Attach 側(docA/docB の nil 判定)と同じ。**Attach は文書で絞られていたのに
			//     Detach だけ絞られていなかった**、というのがこの不具合の正体。
			KCMScrollMapDetachAll();
		}

		// Pages パネルのサムネイル。★★2026-08-16(API 監査 B5): **部分再比較(Refresh Page Comparison)は
		// 触れたページ集合を載せてくる**ので、そのページだけを per-UID Purge する。
		// ⚠**全ページ Purge が残るのは2つの場合だけ**:
		//   ①全再比較(KCMDoMarkChangesDoc)と Stop ---- 絞り込みに要るのは「再比較の**前**に枠が
		//     付いていた旧集合」で、この通知が出る時点で既に捨てられている。⇒ **運べないのではなく、
		//     載せる物が手元に無い**(載せるには model 側で先に退避する必要がある＝未実施)。
		//   ②集合が付いていない通知(送り手が集合を持たない場合の逃げ道)。
		// ★旧記述「旧マーク集合を通知で運べないため」は誤り＝changedBy で運べる(2026-08-15 の監査 B2)。
		// ⚠**片方だけ集合が付く通知は作らない**(KCMNotifyDocsPages が必ず両方まとめて載せる)。
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
				if (docA != nil) KCMRefreshThumbnailsForPages(docA, both, kFalse /*redrawNow*/);
			}
			else
			{
				if (docA != nil) KCMRefreshThumbnailsForPages(docA, *n.fPagesA, kFalse /*redrawNow*/);
				if (docB != nil) KCMRefreshThumbnailsForPages(docB, *n.fPagesB, kFalse /*redrawNow*/);
			}
		}
		else
		{
			if (docA != nil)                 KCMPurgeAllPageThumbs(docA);
			if (docB != nil && docB != docA) KCMPurgeAllPageThumbs(docB);
		}
		KCMForceRedrawPagesPanelNow();

		KCMRefreshPanel();				// Target/Source 名・アイコン・Prev/Next の有効無効

		// ⚠巡回基準点を捨てるのは「全再比較」と「Stop」だけ。差分再比較(登録トグル)で捨てると
		//   ページを登録するたびに Prev/Next が先頭へ戻る＝目に見える挙動変化になる。
		if (n.fNavReset)
			KCMResetNav();
		KCMRefreshNavPosition();
		return;
	}

	if (theChange == kKCMPageFlagsChangedMessage)
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
			KCMRefreshThumbnailsForPages(docA, *n.fPagesA, kFalse /*redrawNow*/);
		}
		else
		{
			if (docA != nil)                 KCMPurgeAllPageThumbs(docA);
			if (docB != nil && docB != docA) KCMPurgeAllPageThumbs(docB);
		}
		KCMForceRedrawPagesPanelNow();
		KCMScrollMapInvalidateAll();
		return;
	}

	if (theChange == kKCMStoryEditsRebuiltMessage)
	{
		// 一覧のモデル(KCMStoryList)は model 側で作り直し済み。ここは画面を合わせるだけ。
		// ★見出しの件数は KCMUpdateStorySectionLabel が arm 状態を見て決めるので、順に呼ぶだけでよい。
		//   パネルが閉じていてもセクションが畳まれていても、どちらも中で静かに諦める。
		KCMStoryTreeRebuild();
		KCMUpdateStorySectionLabel();

		// ★常時表示のマークは比較結果そのものなので、作り直されたら作り直す(2026-08-22)。
		//   ⚠ここが無いと「Refresh Story Comparison で直した行のマークが古いまま」になる＝
		//     直した本人には最も気づきにくい壊れ方。冪等なので、出していないときは何もしない。
		KCMStoryMarksRefresh();

		// ★★Prev/Next の「k/N」も作り直す(2026-08-24)。**Story モードの巡回対象はこの一覧の葉**なので、
		//   一覧が変われば分母が変わる ---- 「Refresh Story Comparison」で1行の子が増減したときがまさに
		//   それで、直した本人の画面に数だけが古いまま残る。
		//   ⚠比較の Start/Stop でも一覧は作り直されるが、そちらは直後に marks 側の通知(上の分岐)が同じ
		//     関数を呼ぶ＝二重に呼んでも同じ値になる(今の状態から作り直すだけの冪等な関数)。
		KCMRefreshNavPosition();
		return;
	}

	if (theChange == kKCMOversetRescannedMessage)
	{
		// あふれ走査の結果が変わった。⚠比較(marks)とは独立した機能なので、対象は1文書のことも
		//   2文書のこともある(走査先を切り替えた直後は「前の文書」の＋を消す必要がある＝docB)。
		IDataBase* const docA = n.fDocA;	// 今の走査対象
		IDataBase* const docB = n.fDocB;	// 直前の走査対象(切り替え時のみ。無ければ nil)
		// ★★2026-08-16(API 監査 B5): 送り手が「＋の絵が変わりうるページ」を載せてくるので per-UID
		//   Purge。同一文書の走り直しなら fPagesA が**新 ∪ 旧**、別文書へ移ったなら fPagesB が
		//   前の文書の旧集合(KCMOversetApply.cpp)。⚠集合が無い通知は従来どおり全ページ。
		if (n.fPagesA != nil && n.fPagesB != nil)
		{
			if (docA != nil) KCMRefreshThumbnailsForPages(docA, *n.fPagesA, kFalse /*redrawNow*/);
			if (docB != nil && docB != docA)
				KCMRefreshThumbnailsForPages(docB, *n.fPagesB, kFalse /*redrawNow*/);
		}
		else
		{
			if (docA != nil)                 KCMPurgeAllPageThumbs(docA);
			if (docB != nil && docB != docA) KCMPurgeAllPageThumbs(docB);
		}
		KCMForceRedrawPagesPanelNow();

		if (docA != nil)
			KCMScrollMapAttach(docA);	// 比較していなくても地図は出す(単独点検の経路)
		KCMScrollMapInvalidateAll();

		if (n.fNavReset)
			KCMResetNav();
		KCMRefreshNavPosition();		// Prev/Next の対象数(比較＋あふれ)を作り直す
		return;
	}

	if (theChange == kKCMComparisonDocsClosedMessage)
	{
		// ★★**この分岐だけ「いつやるか」を自分で決める。** 文書が閉じた後始末は、終了中なら触っては
		//   いけないし、一括クローズの最中なら全部閉じ終わるまで待つべきで、そのどちらも**UI の都合**。
		//   model は「文書が閉じて状態を捨てた」と言うだけ(2026-08-13・Task 10 でこちらへ移した)。

		// どの文書が閉じても要るもの ---- ページ構成も db ポインタも当てにならなくなった以上、
		// ビュー同期のページ矩形/除外対応キャッシュは捨てる。コンテナを空にするだけ＝終了中でも安全。
		KCMInvalidateSyncCaches();

		// 「比較が終わった(Stop 相当のフルクリーンアップが走った)」かどうかは navReset で分かる。
		// 比較と無関係な文書が閉じただけなら kFalse で、下の重い後片付けは要らない。
		// ★2026-08-15(API 監査 B2)以降、この分岐が Facade に聞くのは IsAppQuitting の**1回だけ**に
		//   なったので InterfacePtr で引き回すのをやめた(3回以上なら引く＝[[utils-boss-facade-access]])。
		const bool16 comparisonEnded = n.fNavReset;
		if (comparisonEnded)
		{
			KCMResetPeekGestureState();	// 押下中の覗き状態
			KCMResetNav();				// 巡回の基準点(閉じた文書のページ UID を持ち越さない)
		}

		// ★終了中は widget へ触らない。窓もパネルも解体中でありうる ---- 解体中の widget を触るのが
		//   Mac 限定 crash-on-quit の典型形。窓ごと消えるので strip を外す意味も無い。
		if (Utils<IKCMCompareFacade>()->IsAppQuitting())
			return;

		// ★一括クローズ(複数文書を続けて閉じる)の最中は保留し、全部閉じ終わった通知でまとめて
		//   1回だけ流す(2026-07-27 の仕組みをそのまま使う)。
		if (KCMBatchCloseInProgress())
		{
			KCMDeferCloseUi();
			return;
		}

		if (comparisonEnded)
		{
			// strip: ★Find Overset が(走査文書が生存したまま)単独 ON 中なら**残して**赤帯だけ描き直す。
			//   overset 文書自身が閉じた場合は model 側で DropOverset 済み＝sOversetOn が false なので
			//   通常どおり撤去される(2026-07-24)。
			if (Utils<IKCMMarkData>()->GetOversetOn())
				KCMScrollMapInvalidateAll();
			else
				KCMScrollMapDetachAll();

			// ★サムネイルは**その場で作り直さず次の idle へ遅延**させる。閉じたのが Target で生存側が
			//   これからアクティブ化する場合、前面切替の過渡では ForceRedraw しても再生成が起こりきらず
			//   枠が残る(2026-07-08 実機で確認)。nil はスケジューラ側で弾かれ、重複 db も集約される。
			KCMScheduleThumbRefresh(n.fDocA);
			KCMScheduleThumbRefresh(n.fDocB);
			KCMScheduleThumbRefresh(n.fDocC);
		}

		KCMRefreshPanel();	// パネルの ON/OFF 表示を実状態へ合わせる(「ON 固着」の解消)
		return;
	}
}

// アプリ subject への購読を付ける(UI 側 Startup から1回)。既存の KCMAttachDocsClosedObserver と
// 同じ形。IsAttached を先に聞くので重ねて呼んでも二重に付かない。
void KCMAttachModelChangeObserver()
{
	ISession* session = GetExecutionContextSession();
	IActiveContext* ctx = (session != nil) ? session->GetActiveContext() : nil;
	if (ctx == nil)
		return;
	InterfacePtr<IObserver> obs((IObserver*)ctx->QueryInterface(IID_IKCMMODELCHANGEOBSERVER));
	if (obs == nil)
		return;
	InterfacePtr<IApplication> app(session->QueryApplication());
	InterfacePtr<ISubject> subject(app, IID_ISUBJECT);
	if (subject == nil)
		return;
	if (!subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IKCMMODELCHANGEOBSERVER, IID_IKCMMODELCHANGEOBSERVER))
		subject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IKCMMODELCHANGEOBSERVER, IID_IKCMMODELCHANGEOBSERVER);
}

// 終了時に購読を外す。★**パネル周りを畳むより前**に呼ぶこと(KCMUIStartup.cpp の順序)。
// 購読している間セッションが握っているのは**この .pln の中へのポインタ**で、終了処理中の通知は
// 消えかけのコードで Update を走らせる ---- KCMDetachPanelVisibilityObserver と同じ理屈。
//
// ★★**なぜこちらは detach が要り、KCMDocsClosedObserver は要らないのか**(2026-08-16・監査 B-U2 で
//   明記した。同じ構成の observer が2つの方針に分かれるのに、その差がどこにも書かれていなかった):
//   **上の Update は6分岐あり、IsAppQuitting ガードを持つのは kKCMComparisonDocsClosedMessage の
//   分岐1つだけ** ---- 残る5分岐は終了中に走れば widget を触る。
//   ⇒ **入口で守れないので、通知そのものを止める。**
//   あちらは Update の中身が1本で、その入口が二重に守られている(KCMPeekGesture.cpp の
//   KCMAttachDocsClosedObserver のコメント)。**差は「同居先」でも「subject」でもなく、Update の中身。**
void KCMDetachModelChangeObserver()
{
	ISession* session = GetExecutionContextSession();
	IActiveContext* ctx = (session != nil) ? session->GetActiveContext() : nil;
	if (ctx == nil)
		return;
	InterfacePtr<IObserver> obs((IObserver*)ctx->QueryInterface(IID_IKCMMODELCHANGEOBSERVER));
	if (obs == nil)
		return;
	InterfacePtr<IApplication> app(session->QueryApplication());
	InterfacePtr<ISubject> subject(app, IID_ISUBJECT);
	if (subject == nil)
		return;
	if (subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IKCMMODELCHANGEOBSERVER, IID_IKCMMODELCHANGEOBSERVER))
		subject->DetachObserver(ISubject::kRegularAttachment, obs, IID_IKCMMODELCHANGEOBSERVER, IID_IKCMMODELCHANGEOBSERVER);
}

// KCMModelChangeObserver.cpp 終わり。
