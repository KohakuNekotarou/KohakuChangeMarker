//========================================================================================
//
//  KESCMPanelObserver.cpp
//
//  ChangeMarker 操作パネルの IObserver。
//  ★2026-07-10 に Start/Clear・印刷トグル・25%/75% はフライアウトメニューへ移行済みで、現在のパネルは
//    Target:/Source: の文書名ラベル・Prev/Next ボタン・ステータス行・イラストアイコンだけを持つ。
//    ここが担うのは (a)Prev/Next のボタン押下 (b)AutoAttach での実状態反映(固定既定値は書かない=
//    [[panel-autoattach-read-real-state]]) (c)パネルの表示更新の口(KESCMRefreshPanel / KESCMSetStatus /
//    KESCMSetNavPosition / KESCMGetVisibleOwnPanel)。
//  ★(c)にあった「Start/Stop 実行の実体」は 2026-08-13 に KESCMComparisonRun.cpp へ移した
//    (model/UI 分割 第1段 Task 4)＝**このファイルはパネルの表示だけを担う**。
//  Target:/Source: ラベルと ON/OFF アイコンは arm 済み(「開始済み」)状態を反映する。これはアプリ全体で
//  共有される(KESCMIsArmed/…)ので、パネルを開き直しても正しい状態が表示され続ける。
//
//  SnippetRunner のパネルオブザーバ(SnipRunPanelWidgetObserver.cpp)を手本にしている。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// インターフェイス:
#include "IControlView.h"
#include "IPanelControlData.h"
#include "ISubject.h"
#include "ITextControlData.h"
#include "ITriStateControlData.h"
#include "IBooleanControlData.h"
#include "ISession.h"				// GetExecutionContextSession(終了処理中は nil になり得るので型を明示して受ける)
#include "IApplication.h"			// QueryApplication
#include "IPanelMgr.h"				// QueryPanelManager / GetVisiblePanel(外部からのパネル更新)
// (IActiveContext.h は 2026-08-18 に撤去＝不具合再検査 B-U3。このファイルはアクティブコンテキストを
//  一度も引かない。分割前に「アクティブ文書から db を出す」処理がここに在った名残だった。)
#include "IDocument.h"
#include "IDocumentList.h"

// 一般:
#include "CObserver.h"
#include "widgetid.h"				// kTrueStateMessage / kFalseStateMessage
#include "IDataBase.h"				// GetSysFile(Target/Source のフルパス表示。生存確認の後だけ deref する)
#include "SDKFileHelper.h"			// IDFile -> パス文字列(Target/Source をフルパスで出す。2026-08-12)

// プロジェクト内:
#include "KCMUIID.h"
#include "IKESCMStatusTextData.h"	// ★メッセージ欄は自前描画＝文字列でなく4片を書き込む(2026-08-20)
#include "Utils.h"					// Utils<IKESCMCompareFacade>()
#include "IKESCMCompareFacade.h"	// ★arm 状態とステータス文字列を model に頼む窓口(2026-08-13 Task 11)。
								//  読み出しは GetSessionStatus、書き込みは StoreSessionStatus ---- 後者は
								//  2026-08-15(第2段)にここへ来た。それまでは KESCMModelNotify.h の自由関数を
								//  直に呼んでいたが、それは別 .pln からリンクできない。
#include "KESCMUIShared.h"	// panel / status line / nav readout / tool button (split from KESCMCore.h on 2026-08-13)
#include "KESCMChangeNav.h"			// KESCMGotoNextChange / KESCMGotoPrevChange(◀ Prev / Next ▶ ボタン)
// ★比較の開始/解除の6本は 2026-08-13 に KESCMComparisonRun.cpp へ移した(model/UI 分割 第1段 Task 4)。
//   それだけが使っていた include(KESCMScrollMap.h / KESCMDrawEventHandler.h / KESCMOversetApply.h /
//   PersistUtils.h)も一緒に移っている。⇒ このファイルは**パネルの表示だけ**を担う UI になった。
#include "KESCMPanelState.h"		// KESCMLoadPanelStateIfPresent(読込の主経路は起動時=KESCMUIStartup。ここは保険)
#include "KESCMPanelTitle.h"		// KESCMPanelTitle::Update(パネルを開いたときタブへ今のモードを書く)
#include "KESCMPanelAlpha.h"		// KESCMAttachPanelVisibilityObserver / KESCMApplyAllPanelTranslucency
									// (パネル再表示時に半透明を貼り直す)。⚠2026-08-19(B-U9)訂正＝ここは
									// KESCMApplyPanelTranslucency と書いていたが、このファイルが呼ぶのは
									// **All のほう**。同じ取り違えを KESCMPanelAlpha.h 側が 2026-08-17 に
									// 直しており、**その兄弟がここに残っていた**
#include "KESCMPathDisplay.h"		// KESCMPathForDisplay(Target:/Source: のパスを "/" 区切りで見せる)
#include "KESCMStorySection.h"		// KESCMUpdateStorySectionLabel(見出しの件数も arm 状態の表示の一部)
#include "KESCMStoryTree.h"			// KESCMStoryTreeRebuild(一覧の中身も同じく arm 状態で変わる)

/** ChangeMarker パネルのウィジェットを監視し、共有のオーバーレイ操作を駆動する。 */
class KESCMPanelObserver : public CObserver
{
public:
	KESCMPanelObserver(IPMUnknown* boss) : CObserver(boss) {}
	virtual ~KESCMPanelObserver() {}

	virtual void AutoAttach();
	virtual void AutoDetach();
	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);

private:
	void AttachWidget(const InterfacePtr<IPanelControlData>& pcd, const WidgetID& wid, const PMIID& iid);
	void DetachWidget(const InterfacePtr<IPanelControlData>& pcd, const WidgetID& wid, const PMIID& iid);

	void UpdateInfoDisplay();
};

CREATE_PMINTERFACE(KESCMPanelObserver, kKESCMPanelObserverImpl)

//----------------------------------------------------------------------------------------
// (★今セッションのステータス文字列を覚えるのは **model 側**の仕事になった＝2026-08-13 Task 9 で
//  KESCMModelNotify.cpp へ移動。理由は設計書 §3.3 ＝ app.kcmStatus(ScriptProvider＝model 側)が
//  **パネルを閉じていても答える**という仕様と、パネルは再表示のたびに widget を作り直すこと。
//  ★このファイルに残るのは**表示だけ**。KESCMSetStatus は書いた文字列を Facade の
//  StoreSessionStatus で model 側へ預け、AutoAttach は GetSessionStatus で読み戻す
//  (2026-08-15・第2段。それまでは KESCMModelNotify.h の自由関数を直に呼んでいた)。
//  ⚠StaticMultiLineTextWidget の内容はワークスペースに永続化されるので、再起動後にアイコン状態から
//  開くと**前回セッションの文字列が残る** ---- だから AutoAttach で必ず上書きする、という事情は不変。)
//----------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------
// ローカルヘルパ
//----------------------------------------------------------------------------------------

// db を所有する文書の**フルパス**(取れなければ文書名)。
//
// ★★2026-08-12 に「名前だけ」から変更(ユーザー指示「パネルの方も」)。理由はブック比較の
//   ダイアログと同じ＝**比べる2つは同じ仕事の版違いで、ファイル名まで同じことが多い**。名前だけだと
//   Target と Source が同じ文字列になり、2行が「どちらがどちらか」を何も語らなくなる。
//
// ★★同日に**末尾2つ(親フォルダー\ファイル名)→フルパス**へ再変更(ユーザー指示「フルパスにしてみて、
//   ...がつくかも」)。**溢れることを承知の上での指定**なので、収まりの判断は widget に一任し、ここでは
//   一切削らない。この行の幅は 208px しか無いのでフルパスはたいてい溢れ、対の `.fr` が
//   kEllipsizeBeginning ＝ **前が削られる**(`…\new\ch01.indd`)。⚠溢れた行では行頭の「Target: 」
//   ラベルも一緒に消える——2行の上下(上が Target・下が Source)がその代わりになる。
//   ★ブック比較ダイアログ側も同じくフルパス＋前方省略。**2か所とも同じ答えになった。**
//
// ★未保存の文書はファイルを持たない(IDataBase.h:270-273 が明記)ので、そのときは文書名へ落ちる。
// 呼び出しは下の Target/Source ラベル2箇所だけ。
static PMString KESCMDocPathFromDB(IDataBase* db)
{
	PMString name;
	name.SetTranslatable(kFalse);
	if (db == nil)
		return name;

	// ★★**生存確認が先**(2026-08-13 の再検査で順序を戻した)。この db は arm 中の Target/Source を
	//   生ポインタで持っているもので、文書が閉じた瞬間から `KESCMHandleDocsClosed` が disarm する
	//   までの間は**指す先が無い**。KESCM 全体の規約は「閉じた db は FindDocByDataBase への
	//   ポインタ比較だけに使い、絶対に deref しない」で、`GetSysFile()` はその deref にあたる。
	//   ⚠この関数はパネルの Update から呼ばれ、**終了処理中でもパネルの Update は走る**ことが
	//     2026-08-12 に実測されている(KESCMDetachPanelVisibilityObserver を新設した理由)。
	//   ★生存が確かめられた後の db なら deref してよい ---- 下の GetSysFile はその位置にある。
	InterfacePtr<IApplication> app(GetExecutionContextSession() ? GetExecutionContextSession()->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return name;

	IDocument* d = docList->FindDocByDataBase(db);
	if (d == nil)
		return name;			// 閉じた/知らない db = 何も返さない(触りもしない)

	// ★パスは db に直接聞く(生存確認済み)。名前は下の fallback で IDocument 経由。
	const IDFile* sysFile = db->GetSysFile();
	if (sysFile != nil)
	{
		SDKFileHelper helper(*sysFile);
		PMString path = helper.GetPath();
		if (!path.IsEmpty())
		{
			// ★2026-08-15(ユーザー要望): 区切りは "/" で見せる。日本語環境では "\" が円記号で
			//   描かれ、"…\new\ch01.indd" が "…¥new¥ch01.indd" と読めてしまうため。
			//   規則は KESCMPathDisplay.h の1か所だけ ---- ブック比較も同じ関数を通る
			//   (★**通り道は3つ**＝このパネルの2行／ダイアログの2行／比較前の確認アラート。
			//    KESCMPathDisplay.h の冒頭が数えているとおりで、旧「ブック比較の2行」はアラートを
			//    落としていた。2026-08-18・不具合再検査 B-U3 で全数を確認)。
			return KESCMPathForDisplay(path);
		}
	}

	d->GetName(name);

	// ★長い文字列の切り詰めはここでは行わない。widget 側の ellipsize に一任する(KCMUI.fr の
	//   kKESCMTargetTextWidgetID / kKESCMSourceTextWidgetID)。文字数ではなくフレーム幅で判断するので、
	//   日本語(全角)混じりでも正しく収まる。
	//   (2026-08-06 監査 A-2: 従来はここで文字数ベースに先頭を切っていたが、.fr は末尾を切る設定=
	//    二重かつ逆方向に効いており、末尾を見せる目的が達成できていなかった。)
	name.SetTranslatable(kFalse);
	return name;
}

//----------------------------------------------------------------------------------------
// アタッチ / デタッチ
//----------------------------------------------------------------------------------------

void KESCMPanelObserver::AutoAttach()
{
	// ★保存済みのパネル設定(独自 JSON)の読み込みは起動時(KESCMUIStartup::Startup)へ前倒し済み
	//   (2026-07-15: 同期が Stop 中+ツール選択でも動くため、パネルを開く前でも保存設定を効かせる)。
	//   ここは起動サービスの順序が万一変わっても取りこぼさないための保険呼び出し(通常はセッション
	//   一度きりの内部ガードで no-op。途中変更を巻き戻すこともない)。
	KESCMLoadPanelStateIfPresent();

	// ★★タブに今のモードを出す（2026-08-21）。**ここが「初めて書ける瞬間」**＝ラベルの書き先は
	//   パレットだが、そのパレットは `IPanelMgr::GetPanelFromWidgetID` がパネルを返すようになって
	//   初めて辿れる（起動時の復元ではまだ nil で、KESCMPanelTitle は黙って戻っている）。
	//   ⚠widget と違ってラベルはパレットの持ち物なので**開き直しても消えない**が、ここで書くのは
	//     安いうえ、上の復元でモードが変わっている場合の唯一の反映点になる。
	KESCMPanelTitle::Update();

	InterfacePtr<IPanelControlData> pcd(this, UseDefaultIID());
	if (pcd == nil)
		return;

	this->AttachWidget(pcd, kKESCMPrevChangeButtonWidgetID,   IBooleanControlData::kDefaultIID);
	this->AttachWidget(pcd, kKESCMNextChangeButtonWidgetID,   IBooleanControlData::kDefaultIID);
	// イラスト(ON/OFF アイコン、どちらか一方だけが可視)のクリックで「このプラグインについて」の配布元
	// URL を開く。RollOverIconButtonWidget ベースのボスは ITriStateControlData のクリックで
	// kTrueStateMessage を送る(pictureicon サンプル PicIcoRollOverButtonObserver と同じ流儀)。
	this->AttachWidget(pcd, kKESCMIconOnWidgetID,             ITriStateControlData::kDefaultIID);
	this->AttachWidget(pcd, kKESCMIconOffWidgetID,            ITriStateControlData::kDefaultIID);
	// ★ツール切替ボタン(Prev の左)。押すとツールボックスの琥珀のツールがアクティブになる。
	//   同じ RollOverIconButtonWidget 系の boss なので、受け方は上の2つとまったく同じ。
	this->AttachWidget(pcd, kKESCMToolButtonWidgetID,         ITriStateControlData::kDefaultIID);

	// (印刷ON/OFF と 不透明度 25%/75% は 2026-07-10 にフライアウトメニューへ移行:
	//  kKESCMPopupPrintMarksActionID / kKESCMPopupOpacity25ActionID / kKESCMPopupOpacity75ActionID。
	//  それらの状態は UpdateActionStates が engine 実状態(KESCMGetPrintMarks/KESCMGetMarkOpacity25)を
	//  読んでチェックマークで反映するので、ここでパネルウィジェットを復元する必要はなくなった。)

	this->UpdateInfoDisplay();		// 開始済みなら Target/Source 名と ON アイコン、未開始なら名前なし+OFF

	// ステータス欄はワークスペースに永続化されるため、再起動後にアイコン状態から開くと前回
	// セッションの文字列が残る。今セッションで表示したメッセージ(未操作なら空)で必ず上書きする。
	// ★未操作(空)のとき=初めてパネルを開いたときは、使い方の初期ヒントを英語で表示する
	//   (ソース/ターゲットを開いてフライアウトメニューから Start、という案内)。以後は Start 等の
	//   実メッセージが gSessionStatus を上書きしていくので、ヒストリとしては最後の1件だけが残る。
	// ⚠この分岐を「まだ何も操作していないか」の判定に使わないこと(2026-08-07 現行化。旧コメントは
	//   「Start 等を一度でも操作すれば埋まる」と書いていたが、実際は**ヒントを出した時点で
	//   KESCMSetStatus を通って gSessionStatus が埋まる**)。∴ 2回目以降は必ず else 側を通り、
	//   同じヒント文をそのまま復元する(画面の見え方は同じ)。★app.kcmStatus も同じ値を返すので、
	//   スクリプトから「未操作」を見分けることはできない。
	// ★★2026-08-20: **4片で取り戻す**。欄が自前描画になり、変更行をクリックしたときのメッセージは
	//   「見出し／前の文脈／変更された文字／後の文脈」に分かれている。連結した1本で復元すると、
	//   パネルを閉じて開き直しただけで**色分けだけが静かに消える**——同じ文が、通った経路によって
	//   別の見え方をすることになる。⇒ 覚える場所は今までどおり model 側の1か所で、そこが4片を持つ。
	//   ★普通のメッセージは真ん中の1片だけが埋まっているので、この経路を通っても見え方は変わらない。
	PMString savedLabel, savedPre, savedMid, savedPost;
	Utils<IKESCMCompareFacade>()->GetSessionStatusSegments(savedLabel, savedPre, savedMid, savedPost);	// ★覚えているのは model 側(2026-08-13 Task 9 で移動・Task 11 で Facade 経由へ)
	if (savedLabel.IsEmpty() && savedPre.IsEmpty() && savedMid.IsEmpty() && savedPost.IsEmpty())
	{
		PMString hint("Open the target and source documents (the active one becomes the Target), then choose Start from the panel menu.");
		hint.SetTranslatable(kFalse);
		KESCMSetStatus(hint);	// (メンバ SetStatus は単純転送だったため撤去し直接呼ぶ 2026-07-25)
	}
	else
	{
		KESCMSetStatusSegments(savedLabel, savedPre, savedMid, savedPost);
	}

	// Prev/Next の間の現在位置表示とボタン有効/無効は、上の UpdateInfoDisplay(→KESCMApplyPanelInfo
	// →KESCMRefreshNavPosition)で今の実状態から作り直し済み。ワークスペースに永続化された前回の値は
	// そこで確実に上書きされるので、ここでの復元処理は不要。

	// 半透明トグルが ON なら貼り直す。パネルを開き直すと半透明の付け先である
	// トップレベル窓(OWL.Dock)が別物に変わるため([[win32-window-alpha-transparency]])。
	// ★2026-08-06: 対象が2つ(自パネル/本体のページパネル)になったので、ここは全対象を見る。
	//   自分のパネルが作り直された機会にページパネル側も貼り直しておく方が取りこぼしが無い。
	// ★OFF のときはこちらで弾いて呼ばない(2026-08-06 再点検)。Apply は OFF でも中で弾かれない
	//   (弾くのはドッキング中=対象窓なしのときだけ)ので、無条件に呼ぶと使っていない人にも
	//   窓探索(キャッシュ失効時は SDK への問い合わせ)+alpha 書き+影の SW_SHOWNA の費用を払わせる。
	//   MouseEnter/MouseLeave/フック/可視性オブザーバの各入口が OFF を弾くのと同じ方針。
	//   ★対象ごとの OFF は KESCMApplyAllPanelTranslucency が飛ばす(2026-08-07 修正。片方 ON・
	//     片方 OFF で両者が同じフローティンググループにいると、OFF 側が同じ窓へ 255 を上書きして
	//     ON 側の半透明を打ち消していた)。∴ ここの条件は「**このパネル2つがどちらも OFF なら**
	//     呼ぶ意味が無い」の意味。
	//   ⚠2026-08-19(B-U9)訂正＝ここは「全部 OFF なら」と書いていたが、トグルは**3つ**あり
	//     (3つ目＝ブック比較ダイアログ・2026-08-13)、この条件はそれを見ていない。**見ないのが正しい**
	//     ＝ダイアログはパネルではないので、パネルの widget が作り直されても窓は無傷。
	//     ⇒ 数え落としているのは条件ではなく、この説明文のほうだった。
	//   ⚠Apply**For** 側に OFF ガードを入れてはいけない: メニューで OFF にした瞬間の 255 復元・
	//     影の再表示は、対象を名指しで呼ぶあちらが担っている(KESCMActionComponent.cpp のトグル経路)。
	// ★ここは保険で、主たる追随は KESCMPanelAlpha.cpp のオブザーバ(kPaletteVisibilityChangedMessage)。
	//   ★注意: この AutoAttach は widget を作り直すたびに走るので、固定の既定値を書く場所ではない
	//   (KESCMGetPanelTranslucent の現在値を読んで反映するだけ)。
	//
	// ★起動時(KESCMUIStartup::Startup)にはパネルマネージャがまだ立ち上がっておらず購読に
	//   失敗している可能性があるため、ここでも購読を試す(IsAttached ガードがあるので二重にならない)。
	KESCMAttachPanelVisibilityObserver();
	// (「乗っている」状態を落とす KESCMResetPanelHover の呼び出しは 2026-07-29 に撤去。判定を
	//  旗から Win32 の実測へ変えたので、widget を作り直しても落とすべき状態が無い。)
	if (KESCMGetPanelTranslucent() || KESCMGetPagesPanelTranslucent())
		KESCMApplyAllPanelTranslucency();
}

void KESCMPanelObserver::AutoDetach()
{
	InterfacePtr<IPanelControlData> pcd(this, UseDefaultIID());
	if (pcd == nil)
		return;

	this->DetachWidget(pcd, kKESCMPrevChangeButtonWidgetID,   IBooleanControlData::kDefaultIID);
	this->DetachWidget(pcd, kKESCMNextChangeButtonWidgetID,   IBooleanControlData::kDefaultIID);
	this->DetachWidget(pcd, kKESCMIconOnWidgetID,             ITriStateControlData::kDefaultIID);
	this->DetachWidget(pcd, kKESCMIconOffWidgetID,            ITriStateControlData::kDefaultIID);
	this->DetachWidget(pcd, kKESCMToolButtonWidgetID,         ITriStateControlData::kDefaultIID);	// ★AutoAttach と対で外す
}

void KESCMPanelObserver::AttachWidget(const InterfacePtr<IPanelControlData>& pcd, const WidgetID& wid, const PMIID& iid)
{
	IControlView* cv = pcd->FindWidget(wid);
	if (cv == nil)
		return;
	InterfacePtr<ISubject> subject(cv, UseDefaultIID());
	if (subject != nil)
		subject->AttachObserver(this, iid);
}

void KESCMPanelObserver::DetachWidget(const InterfacePtr<IPanelControlData>& pcd, const WidgetID& wid, const PMIID& iid)
{
	IControlView* cv = pcd->FindWidget(wid);
	if (cv == nil)
		return;
	InterfacePtr<ISubject> subject(cv, UseDefaultIID());
	if (subject != nil)
		subject->DetachObserver(this, iid);
}

//----------------------------------------------------------------------------------------
// Update のディスパッチ
//----------------------------------------------------------------------------------------

void KESCMPanelObserver::Update(const ClassID& theChange, ISubject* theSubject, const PMIID& /*protocol*/, void* /*changedBy*/)
{
	InterfacePtr<IControlView> cv(theSubject, UseDefaultIID());
	if (cv == nil)
		return;

	const WidgetID wid = cv->GetWidgetID();

	// ★★ツール切替ボタンだけは kFalseStateMessage も見る(2026-08-07 ユーザー報告
	//   「ツールボックスから押すと同期するが、パネルから押すと押しっぱなしの見え方にならない」)。
	//   原因: この widget は push button の挙動で、**クリック処理の最後に自分で状態を落とす**。
	//   順番はこうなっていた —— ①押下で kTrueStateMessage → ②下の case がツールを切り替える →
	//   ③ITool::Select が状態を kSelected にする → ④**widget がクリックを閉じる際に kUnselected へ戻す**。
	//   ④が最後に来るので押下表示が消える。ツールボックスから選んだときは①④が走らない＝残る、
	//   というユーザーの観測とも合う。
	//   ⇒ ④の直後に飛ぶ kFalseStateMessage で「実際にアクティブか」を見て塗り直す。
	//   ★実状態(KESCMIsOwnToolActive)を見るので、ツールが本当に切り替わらなかったときは
	//     押下表示も戻らない ＝ 見た目と実態がずれない。
	//   (トグル系 widget で kTrue/kFalse 両方のメッセージを見るのは製品コードでも定石＝レイヤーパネル。)
	if (theChange == kFalseStateMessage && wid.Get() == kKESCMToolButtonWidgetID)
	{
		KESCMSetToolButtonSelected(KESCMIsOwnToolActive());
		return;
	}

	if (theChange == kTrueStateMessage)
	{
		switch (wid.Get())
		{
			// ◀ Prev / Next ▶: 見るべきページ(変更/Added/未比較)へ Target ビューをスクロール。
			// (Start/Stop はパネルボタン→フライアウトメニュー kKESCMPopupStartStopActionID へ移行 2026-07-10)
			case kKESCMPrevChangeButtonWidgetID:  KESCMGotoPrevChange(); break;
			case kKESCMNextChangeButtonWidgetID:  KESCMGotoNextChange(); break;
			// (印刷ON/OFF と 不透明度 25%/75% はフライアウトメニューへ移行 2026-07-10。ここでは扱わない。)
			// イラストクリック → 「このプラグインについて」の配布元URLをブラウザで開く。
			case kKESCMIconOnWidgetID:
			case kKESCMIconOffWidgetID:
				KESCMOpenAboutURL();
				break;
			// ★ツール切替ボタン → このプラグインのツール(ツールボックスの琥珀のツール)を
			//   アクティブにする。ツールボックスでそれをクリックしたのと同じ状態になる。
			//   実体は KESCMTool.cpp(Utils<IToolBoxUtils>()->QueryTool → SetActiveTool)。
			case kKESCMToolButtonWidgetID:
			{
				// ★押した結果をステータス行に出す(2026-08-07 ユーザー要望)。SetActiveTool は
				//   「実際にアクティブになったか」を返すので、断られた場合も黙って終わらない。
				const bool16 activated = KESCMActivateOwnTool();

				// ★★出す名前は**ツールチップと同じ**(2026-08-07 ユーザー指定)。そうなるのは同じ
				//   文字列テーブルのキーを引いているから ＝ 名前を持つ場所は1つだけで、
				//   ツールボックスのツール名(KESCMTool::Init の SetName)・ツールチップ
				//   (KESCMIconTip::GetTipText)・この行の3か所が必ず一致する([[one-question-one-place]])。
				//   PMString::Translate() が「キー → 今のロケールの実文字列」に解決する(PMString.h:692-696)。
				PMString toolName(kKESCMToolStringKey);
				toolName.Translate();

				PMString msg;
				msg.SetTranslatable(kFalse);	// ★組み立て終わった文をもう一度キー扱いさせない
				if (activated)
				{
					msg.Append(toolName);
					msg.Append(" selected.");
				}
				else
				{
					msg.Append("Could not select ");
					msg.Append(toolName);
					msg.Append(".");
				}
				KESCMSetStatus(msg);
				break;
			}
			default: break;
		}
	}
}

//----------------------------------------------------------------------------------------
// 表示ヘルパ
//----------------------------------------------------------------------------------------

// 表示中の ChangeMarker パネルの IControlView を返す(隠れている/引けないときは nil)。
// ★下の KESCMRefreshPanel / KESCMSetStatus / KESCMSetNavPosition が
//   「session → app → panelMgr → GetVisiblePanel」の定型を丸ごと3回持っていたので一本化した
//   (2026-08-06 監査 C-1)。同じプラグイン内の **KESCMGetVisiblePagesPanel**
//   (KESCMThumbnailRefresh.h で宣言)と同じ作り＝Pages パネル側だけ解いてあった穴を埋める形。
//   (⚠旧引用 ":24-27" は空行を指していた＝2026-08-16 の監査 B-U3 で関数名へ。)
// ★session の nil ガードもここで吸収する: 3つとも**アプリ終了のティアダウン中に到達し得る**
//   (2026-07-25 に KESCM 全体で統一した規約)。
//   ⚠★根拠の書き換え(2026-08-18・不具合再検査 B-U3)。旧文は「3つともクローズ responder から
//     呼ばれ」と書いていたが、**それは model/UI 分割の前の話**で、今は成立しない ---- クローズの
//     掃除(KESCMHandleDocsClosed)は model 側にあり、別 .pln のこの3本を直に呼べない。今の呼び手は
//     KESCMRefreshPanel=通知の受け手(KESCMModelChangeObserver)と KESCMPeekGesture、
//     KESCMSetNavPosition=KESCMChangeNav だけ。
//   ★**結論のほうは生きている**: 終了処理中でもパネルの Update は走ることを 2026-08-12 に実測して
//     いる(KESCMDetachPanelVisibilityObserver を新設した理由＝下の KESCMDocPathFromDB のコメント)。
//     ∴ nil ガードは要る。**根拠が失効しても結論が失効するとは限らない**([[verify-claims-in-comments]])。
// ★2026-08-09: static を外して公開した(当時の置き場は KESCMCore.h。2026-08-13 の model/UI 分割で
//   **KESCMUIShared.h** へ移った)。4人目の使い手が別ファイルに現れたため
//   (KESCMStorySection.cpp = Story Edits セクションの開閉。パネルの寸法を触るのに同じパネルが要る)。
//   ここを複製すると「どのパネルを指すか」の判断が2か所に分かれるので、公開する方を選んだ。
IControlView* KESCMGetVisibleOwnPanel()
{
	ISession* session = GetExecutionContextSession();	// 終了処理中は nil になり得る
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	if (app == nil)
		return nil;
	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr == nil)
		return nil;
	return panelMgr->GetVisiblePanel(kKESCMPanelWidgetID);
}

// パネルの ON/OFF 表示(Target/Source 名・アイコン・トグルラベル)を現在の arm 状態
// (KESCMIsArmed 等)に合わせて更新する共通処理。メンバ UpdateInfoDisplay(自パネル)と外部の
// KESCMRefreshPanel(可視パネルをレスポンダから)双方から使うため、pcd を引数に取る自由関数にする。
static void KESCMApplyPanelInfo(const InterfacePtr<IPanelControlData>& pcd)
{
	if (pcd == nil)
		return;

	// ★5回聞くので InterfacePtr で1回引く(2026-08-16・監査 B-U3)。`Utils.h:74-80` が
	//   「several places で使うなら一度引いて InterfacePtr に持て、その方が QueryInterface と
	//   Release が1回で済む」と明記している。⚠**公式は回数を数字で示していない**——手元の
	//   「3回以上なら」は目安([[utils-boss-facade-access]])。同じプラグインの
	//   KESCMPanelState.cpp / KESCMActionComponent.cpp は既にこの形なので、割れを揃えた形。
	// ⚠nil 検査は**足していない**＝従来と同じ挙動を保つため(Utils<>()-> も nil なら同じく落ちる)。
	InterfacePtr<IKESCMCompareFacade> compare(Utils<IKESCMCompareFacade>().QueryUtilInterface());
	const bool16 started = compare->IsArmed() && (compare->GetArmedTargetDB() != nil);

	// Target:/Source: ラベルは常時。名前は開始中のみ表示(英語固定: 現状英語のまま)。
	PMString target("Target:"); target.SetTranslatable(kFalse);
	if (started)
	{
		target.Append(" ");
		target.Append(KESCMDocPathFromDB(compare->GetArmedTargetDB()));
	}
	PMString source("Source:"); source.SetTranslatable(kFalse);
	if (started && compare->GetArmedSourceDB() != nil)
	{
		source.Append(" ");
		source.Append(KESCMDocPathFromDB(compare->GetArmedSourceDB()));
	}

	IControlView* tView = pcd->FindWidget(kKESCMTargetTextWidgetID);
	if (tView != nil)
	{
		InterfacePtr<ITextControlData> tcd(tView, UseDefaultIID());
		if (tcd != nil) tcd->SetString(target);
	}
	IControlView* sView = pcd->FindWidget(kKESCMSourceTextWidgetID);
	if (sView != nil)
	{
		InterfacePtr<ITextControlData> tcd(sView, UseDefaultIID());
		if (tcd != nil) tcd->SetString(source);
	}

	// アイコン: 開始中=ON / 未開始=OFF を出し分ける(2枚を重ねて可視を切替)。
	// ★ShowView は見た目を消すだけでヒットテストは無効化しないため、隠れている方もクリックを拾って
	// KESCMOpenAboutURL が二重発火する(ブラウザタブが2つ開く)。Enable も可視状態と合わせて切り替え、
	// 隠れている方はクリックに反応しないようにする。
	IControlView* onView  = pcd->FindWidget(kKESCMIconOnWidgetID);
	IControlView* offView = pcd->FindWidget(kKESCMIconOffWidgetID);
	if (onView  != nil) { onView->ShowView(started ? kTrue : kFalse);  onView->Enable(started ? kTrue : kFalse); }
	if (offView != nil) { offView->ShowView(started ? kFalse : kTrue); offView->Enable(started ? kFalse : kTrue); }

	// ★★Story Edits の一覧と見出しもここで作り直す。**どちらも arm 状態を映す表示だから**、
	//   Target/Source ラベルやアイコンと同じ場所に属する。
	// ⚠★★これを比較の側(KESCMDoMarkChangesDoc / KESCMDoClearMarks)だけに置くと**必ずずれる**——
	//   Start は「比較 → 成功したら arm」、Stop は「マーク消去 → disarm」の順で、どちらも
	//   一覧を作る瞬間の arm 状態が**その後の状態と逆**になる。実機で出た症状は3つとも同じ原因だった
	//   (2026-08-10): 見出しに件数が出ない／Stop したのに "No edits" の行が残る／0件で Start しても
	//   "No edits" が出ない。arm の切り替わりの後に必ず通るのはこの関数なので、ここで揃える。
	//   ★比較側の呼び出しも残してある: Refresh Page Comparison は arm 状態を変えずに件数だけ
	//   変えるので、あちらはあちらで要る。
	KESCMStoryTreeRebuild();
	KESCMUpdateStorySectionLabel();

	// Prev/Next(変更ページナビ)の有効/無効と、その間の現在位置表示(k/N・-・空)は
	// KESCMRefreshNavPosition に一元化(比較中かつ変更ページありのときだけ有効。無ければ無効+"/"、
	// 未 Start は無効+空)。attach 時・Start/Stop・文書クローズ/切替のすべてがこの関数を通るので
	// 初期状態から正しく反映される。値の作り方は KESCMChangeNav.cpp を参照。
	KESCMRefreshNavPosition();

	// ★ツール切替ボタンの押下表示を**実状態**へ合わせる(2026-08-07)。パネルは表示のたびに widget を
	//   作り直すので、ここで固定の既定値(未選択)を書いてしまうと、ツールがアクティブなままパネルを
	//   開き直したときに押下表示が落ちる([[panel-autoattach-read-real-state]])。
	//   ★「今アクティブか」の判断は KESCMTool.cpp の1か所だけが持つ。
	IControlView* toolView = pcd->FindWidget(kKESCMToolButtonWidgetID);
	if (toolView != nil)
	{
		InterfacePtr<ITriStateControlData> tsd(toolView, UseDefaultIID());
		if (tsd != nil)
		{
			// 第3引数 kFalse = 通知を出さない(理由は下の KESCMSetToolButtonSelected と同じ)。
			tsd->SetState(KESCMIsOwnToolActive() ? ITriStateControlData::kSelected
												 : ITriStateControlData::kUnselected, kTrue, kFalse);
		}
	}

	// (Start/Stop の切替はパネルボタンから撤去し、フライアウト項目 kKESCMPopupStartStopActionID の
	//  動的ラベル(UpdateActionStates)へ移行 2026-07-10。ここでのボタンラベル設定は不要になった。)
}

//========================================================================================
// KESCMSetToolButtonSelected(KESCMUIShared.h で宣言)
//   パネルのツール切替ボタンを「押されている/いない」表示にする。ツールボックスのツール枠と同じ
//   見た目(くぼみ)になるのは、.fr でこの widget を kADBEIconSuiteButtonDrawWellType にしてあるため。
//
//   ★呼び元は3つ(2026-08-18・不具合再検査 B-U3 で数え直した。旧「KESCMTool::Select / Deselect の
//     2つだけ」は**同じファイルの3つ目を落としていた**):
//       ・KESCMTool::Select   … ツールがアクティブになった
//       ・KESCMTool::Deselect … ツールが降りた
//       ・**このファイルの Update(kFalseStateMessage)** … push button が自分で状態を落とした後の
//         塗り直し(2026-08-07 に足した経路。理由はそちらのコメント)
//   ★数は3つでも**答えの出どころは1つ**なのは変わらない ---- 3つとも「今アクティブか」を
//     KESCMIsOwnToolActive() に聞いてから渡すので、パネルとツールボックスが食い違う経路は
//     構造的に無い([[one-question-one-place]])。ツールボックスで選んでも、パネルのボタンで選んでも、
//     ショートカットでも、スクリプトでも、ITool::Select は必ず呼ばれる。
//========================================================================================
void KESCMSetToolButtonSelected(bool16 selected)
{
	IControlView* panel = KESCMGetVisibleOwnPanel();
	if (panel == nil)
		return;		// パネルは隠れている(または終了処理中): 触る先が無い。
	InterfacePtr<IPanelControlData> pcd(panel, UseDefaultIID());
	if (pcd == nil)
		return;

	IControlView* cv = pcd->FindWidget(kKESCMToolButtonWidgetID);
	if (cv == nil)
		return;

	InterfacePtr<ITriStateControlData> tsd(cv, UseDefaultIID());
	if (tsd == nil)
		return;

	// ★★第3引数 notifyOfChange = kFalse(ITriStateControlData.h:52)。
	//   ⚠kTrue のままだと状態変更で kTrueStateMessage が飛び、この Observer の Update が
	//     KESCMActivateOwnTool を呼び返す → SetActiveTool → ITool::Select → またここ、と往復する。
	//     ここは実状態を**映すだけ**なので、通知は要らない。
	tsd->SetState(selected ? ITriStateControlData::kSelected : ITriStateControlData::kUnselected, kTrue, kFalse);
	cv->ForceRedraw();		// 押下表示は即座に見えてほしい(次のイベントループまで待たせない)
}

void KESCMPanelObserver::UpdateInfoDisplay()
{
	InterfacePtr<IPanelControlData> pcd(this, UseDefaultIID());
	KESCMApplyPanelInfo(pcd);
}

//========================================================================================
// KESCMRefreshPanel(KESCMUIShared.h で宣言)
//   現在表示中の ChangeMarker パネルがあれば、その ON/OFF 表示を現在の arm 状態へ更新する。
//   パネルが隠れていれば何もしない(次に開いたとき AutoAttach が実状態を反映する)。
//   クローズレスポンダ(KESCMHandleDocsClosed)から、追跡文書が閉じてパネルを OFF に戻すときに呼ぶ。
//========================================================================================
void KESCMRefreshPanel()
{
	// ★session の nil ガード(終了処理中のティアダウン)も含めて KESCMGetVisibleOwnPanel が持つ。
	IControlView* panel = KESCMGetVisibleOwnPanel();
	if (panel == nil)
		return;		// パネルは隠れている(または終了処理中): 触る先が無い。
	InterfacePtr<IPanelControlData> pcd(panel, UseDefaultIID());
	KESCMApplyPanelInfo(pcd);
}

//========================================================================================
// KESCMSetStatus / KESCMSetStatusSegments(KESCMUIShared.h で宣言)
//   パネルのステータス行を更新する。メンバ SetStatus(自パネル)と同じ処理を自由関数として公開し、
//   クローズレスポンダ(KESCMHandleDocsClosed)からも Stop 相当のメッセージを出せるようにする。
//   パネルが隠れていてもセッション状態は覚えておき、再表示時に復元する。
//
// ★★2026-08-20: この欄は自前描画になった(KESCMStatusTextView.cpp)。入口は2つに増えたが、
//   **欄へ流し込む手順は下の1本だけ**＝widget を探す・書く・描き直させるが1か所にある。
//========================================================================================
namespace
{

/* KESCMWriteStatusToPanel
   4片をメッセージ欄へ流し込む。パネルが隠れていれば何もしない(再表示時に AutoAttach が、
   覚えている値から復元する)。

   ⚠★★**Invalidate を自分で呼ぶ。** stock の静的テキストでは ITextControlData::SetString が
     やっていた(第2引数 invalidate の既定が kTrue で、その @param が「specifies whether the
     control should be redrawn」)。自前描画の欄は**ただのデータ入れ物に書くだけ**なので、
     画面が古くなったことを誰も知らない。「書いたのに変わらない」の原因はここになる。
*/
void KESCMWriteStatusToPanel(const PMString& label, const PMString& pre,
							 const PMString& mid, const PMString& post, bool16 forceRedrawNow)
{
	IControlView* panel = KESCMGetVisibleOwnPanel();
	if (panel == nil)
		return;		// パネルは隠れている(または終了処理中): 触る先が無い。
	InterfacePtr<IPanelControlData> pcd(panel, UseDefaultIID());
	if (pcd == nil)
		return;
	IControlView* cv = pcd->FindWidget(kKESCMStatusTextWidgetID);
	if (cv == nil)
		return;
	InterfacePtr<IKESCMStatusTextData> data(cv, UseDefaultIID());
	if (data == nil)
		return;

	data->SetSegments(label, pre, mid, post);
	cv->Invalidate();

	// この直後にブロッキング処理(比較ループ等)が続く場合、Invalidate は次のイベントループまで
	// 画面に届かない。busyMsg 表示のために今すぐ同期描画させる。
	if (forceRedrawNow)
		panel->ForceRedraw(nil, kTrue);
}

}	// anonymous namespace

void KESCMSetStatus(const PMString& s, bool16 forceRedrawNow)
{
	// ★覚えるのは model 側(KESCMModelNotify.cpp)。パネルを隠して再表示したときの復元と、
	//   app.kcmStatus の答えが、そこ1か所から出る(2026-08-13 Task 9)。
	//   ⚠ここで通知は出さない ---- この関数は**通知を受けた側**でもあるので、輪になる。
	Utils<IKESCMCompareFacade>()->StoreSessionStatus(s);

	// ★普通のメッセージは**真ん中の1片**として渡す＝1色で描かれ、stock の静的テキストが
	//   描いていた絵と同じになる。だから 72 か所ある呼び手は1つも変えていない。
	const PMString kNothing;
	KESCMWriteStatusToPanel(kNothing, kNothing, s, kNothing, forceRedrawNow);
}

void KESCMSetStatusSegments(const PMString& label, const PMString& pre,
							const PMString& mid, const PMString& post)
{
	// ★覚える場所は上とまったく同じ1か所。連結して1本の文字列にするのは model 側なので、
	//   app.kcmStatus の答えは「見出し + 改行 + 本文」＝この欄に見えているとおりになる。
	Utils<IKESCMCompareFacade>()->StoreSessionStatusSegments(label, pre, mid, post);

	// ★forceRedrawNow は渡さない＝この経路は行のクリックで、直後にブロッキング処理が続かない。
	KESCMWriteStatusToPanel(label, pre, mid, post, kFalse);
}

// (★KESCMGetSessionStatus と KESCMClearSessionStatus は 2026-08-13 Task 9 で
//  **KESCMModelNotify.cpp(model 側)**へ移した。文字列を持つ場所と、それを答える場所
//  (app.kcmStatus＝ScriptProvider も model 側)を揃えるため。このファイルは表示だけを担う。
//  ⚠2026-08-15 以降、UI からその2本を**直に呼ぶことはできない**(別 .pln になるとリンクできない)
//  ＝IKESCMCompareFacade の GetSessionStatus / ClearSessionStatus を通す。)

//========================================================================================
// KESCMSetNavPosition(KESCMUIShared.h で宣言)
//   Prev/Next の間の現在位置表示(kKESCMNavPosTextWidgetID、例 "3/12")と、Prev/Next ボタンの
//   有効/無効をまとめて更新する。パネルが隠れていれば何もしない(再表示時に KESCMRefreshNavPosition が
//   実状態を反映する)。値の決定は呼び出し側(KESCMRefreshNavPosition)に集約。
//========================================================================================
void KESCMSetNavPosition(const PMString& posText, bool16 navButtonsEnabled)
{
	IControlView* panel = KESCMGetVisibleOwnPanel();
	if (panel == nil)
		return;		// パネルは隠れている(または終了処理中): 触る先が無い。
	InterfacePtr<IPanelControlData> pcd(panel, UseDefaultIID());
	if (pcd == nil)
		return;

	// 位置表示。★inval は SetString がやっている(ITextControlData::SetString の第2引数 invalidate は
	//   既定 kTrue で、その @param が「specifies whether the control should be redrawn」)。
	//   (⚠旧引用 ":53-54" は宣言の行で、引いている文言はその上の @param 行にあった＝2026-08-18・
	//    不具合再検査 B-U3 で名前で引く形へ。)ただし inval だけでは次の
	//   イベントループまで画面に届かないので、Start での変化や Next/Prev の値変更を即時反映させるため
	//   ForceRedraw で今すぐ描かせる(IControlView.h:281-286「Redraws the invalid region directly」。
	//   2026-07-15 ユーザー報告「1/5 が即時更新されない」。重複していた Invalidate() は 2026-08-06 の
	//   監査(ブロック8 A-3)で撤去)。
	IControlView* cv = pcd->FindWidget(kKESCMNavPosTextWidgetID);
	if (cv != nil)
	{
		InterfacePtr<ITextControlData> tcd(cv, UseDefaultIID());
		if (tcd != nil)
		{
			tcd->SetString(posText);
			cv->ForceRedraw();
		}
	}

	// Prev/Next ボタンの有効/無効(変更ページが無ければ押せないようにする=ユーザー指定 2026-07-15)。
	IControlView* prevView = pcd->FindWidget(kKESCMPrevChangeButtonWidgetID);
	IControlView* nextView = pcd->FindWidget(kKESCMNextChangeButtonWidgetID);
	if (prevView != nil) prevView->Enable(navButtonsEnabled);
	if (nextView != nil) nextView->Enable(navButtonsEnabled);
}

// KESCMPanelObserver.cpp 終わり。
