//========================================================================================
//
//  KESCMPanelObserver.cpp
//
//  ChangeMarker 操作パネルの IObserver。
//  ★2026-07-10 に Start/Clear・印刷トグル・25%/75% はフライアウトメニューへ移行済みで、現在のパネルは
//    Target:/Source: の文書名ラベル・Prev/Next ボタン・ステータス行・イラストアイコンだけを持つ。
//    ここが担うのは (a)Prev/Next のボタン押下 (b)AutoAttach での実状態反映(固定既定値は書かない=
//    [[panel-autoattach-read-real-state]]) (c)Start/Stop 実行の実体(KESCMToggleStartStop。フライアウトの
//    Start/Stop と Load の Stop 戻しから呼ばれる)。
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
#include "IActiveContext.h"
#include "IDocument.h"
#include "IDocumentList.h"

// 一般:
#include "CObserver.h"
#include "widgetid.h"				// kTrueStateMessage / kFalseStateMessage
#include "PersistUtils.h"			// ::GetUIDRef

// プロジェクト内:
#include "KESCMID.h"
#include "KESCMCore.h"
#include "KESCMChangeNav.h"			// KESCMGotoNextChange / KESCMGotoPrevChange(◀ Prev / Next ▶ ボタン)
#include "KESCMScrollMap.h"			// スクロールバー地図strip(Startで注入/Stopで取り外し)
#include "KESCMDrawEventHandler.h"	// KESCMDrawEventHandler::sOversetOn(Stop 時に Find Overset 単独 ON なら地図を残す判定)
#include "KESCMPanelState.h"		// KESCMLoadPanelStateIfPresent(読込の主経路は起動時=KESCMPeekStartup。ここは保険)
#include "KESCMPanelAlpha.h"		// KESCMApplyPanelTranslucency(パネル再表示時に半透明を貼り直す)

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
// 今セッションで最後に表示したステータス文字列。
// StaticMultiLineTextWidget の内容はワークスペースに永続化されるため、InDesign を再起動して
// アイコン状態のパネルを開くと前回セッションの文字列(例: "marks start / pages compared=22")が残って
// しまう。そこで「今セッションで表示したメッセージ」だけをここに覚えておき、AutoAttach で必ず
// 上書きする。未操作(空文字)の場合は AutoAttach が初期ヒントを表示する(2026-07-25 コメント現行化。
// 旧記述「空なら何も表示されない」は初期ヒント導入前のもの)。
//----------------------------------------------------------------------------------------
namespace { PMString gSessionStatus; }

//----------------------------------------------------------------------------------------
// ローカルヘルパ
//----------------------------------------------------------------------------------------

// (アクティブ(前面)文書=比較の Target の解決は KESCMActiveDoc(KESCMCore)に統合。2026-07-25 重複解消)

// 比較の Target/Source を解決する唯一の場所。アクティブ(前面)文書=Target、それ以外で最初に開いて
// いる文書=Source。両方引けたときだけ kTrue を返す(引けなかった側は nil のまま返るので、呼び出し側は
// どちらが欠けたかで文言を選べる)。
// ★ここに集約する理由: 「比較を始められるか」をメニューの有効/無効(KESCMCanStartComparison)と実行
//   (KESCMToggleStartStop)の2か所で別々に書くと、必ずどこかでずれる([[one-question-one-place]])。
static bool16 KESCMResolveComparisonPair(IDocument*& outTarget, IDocument*& outSource);

// target 以外で最初に開いている文書 = 比較の Source(旧版)。
static IDocument* KESCMFirstOtherDoc(IDocument* target)
{
	InterfacePtr<IApplication> app(GetExecutionContextSession() ? GetExecutionContextSession()->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return nil;
	const int32 n = docList->GetDocCount();
	for (int32 i = 0; i < n; ++i)
	{
		IDocument* d = docList->GetNthDoc(i);
		if (d != nil && d != target)
			return d;
	}
	return nil;
}

// 上で宣言した解決子の実体。
static bool16 KESCMResolveComparisonPair(IDocument*& outTarget, IDocument*& outSource)
{
	outTarget = KESCMActiveDoc();
	outSource = (outTarget != nil) ? KESCMFirstOtherDoc(outTarget) : nil;
	return (outTarget != nil && outSource != nil) ? kTrue : kFalse;
}

// 比較を開始できるか(KESCMCore.h で宣言)。フライアウトの「Start」を有効にしてよいかの判定で、
// 実行側 KESCMToggleStartStop と同じ解決子を通る。
bool16 KESCMCanStartComparison()
{
	IDocument* target = nil;
	IDocument* source = nil;
	return KESCMResolveComparisonPair(target, source);
}

// db を所有する文書の表示名(そのまま返す。ラベル幅への収まりは widget の ellipsize が引き受ける)。
// ★旧記述「JSX パネルと同様、ラベルに収まるよう短縮する」は 2026-08-06 の監査(ブロック8 A-2)で
//   自前短縮を撤去した時点で陳腐化していた(2026-08-06 の再確認で現行化)。呼び出しは下の
//   Target/Source ラベル2箇所だけで、どちらも .fr が kEllipsizeMiddle。
static PMString KESCMDocNameFromDB(IDataBase* db)
{
	PMString name;
	name.SetTranslatable(kFalse);
	if (db == nil)
		return name;

	InterfacePtr<IApplication> app(GetExecutionContextSession() ? GetExecutionContextSession()->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return name;

	IDocument* d = docList->FindDocByDataBase(db);
	if (d != nil)
		d->GetName(name);

	// ★長い名前の切り詰めはここでは行わない。widget 側の ellipsize に一任する(KESCM.fr の
	//   kKESCMTargetTextWidgetID / kKESCMSourceTextWidgetID = kEllipsizeMiddle)。製品 Links パネルが
	//   リンクファイル名に採っているのと同じ方式で、文字数ではなくフレーム幅で判断するため、
	//   日本語(全角)名でも正しく収まり「Target: 」ラベルと拡張子の両端が残る。
	//   (2026-08-06 監査 A-2: 従来はここで文字数ベースに先頭を切っていたが、.fr は kEllipsizeEnd で
	//    末尾を切る設定=二重かつ逆方向に効いており、末尾を見せる目的が達成できていなかった。)
	return name;
}

//----------------------------------------------------------------------------------------
// アタッチ / デタッチ
//----------------------------------------------------------------------------------------

void KESCMPanelObserver::AutoAttach()
{
	// ★保存済みのパネル設定(独自 JSON)の読み込みは起動時(KESCMPeekStartup::Startup)へ前倒し済み
	//   (2026-07-15: 同期が Stop 中+ツール選択でも動くため、パネルを開く前でも保存設定を効かせる)。
	//   ここは起動サービスの順序が万一変わっても取りこぼさないための保険呼び出し(通常はセッション
	//   一度きりの内部ガードで no-op。途中変更を巻き戻すこともない)。
	KESCMLoadPanelStateIfPresent();

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

	// (印刷ON/OFF と 不透明度 25%/75% は 2026-07-10 にフライアウトメニューへ移行:
	//  kKESCMPopupPrintMarksActionID / kKESCMPopupOpacity25ActionID / kKESCMPopupOpacity75ActionID。
	//  それらの状態は UpdateActionStates が engine 実状態(KESCMGetPrintMarks/KESCMGetMarkOpacity25)を
	//  読んでチェックマークで反映するので、ここでパネルウィジェットを復元する必要はなくなった。)

	this->UpdateInfoDisplay();		// 開始済みなら Target/Source 名と ON アイコン、未開始なら名前なし+OFF

	// ステータス欄はワークスペースに永続化されるため、再起動後にアイコン状態から開くと前回
	// セッションの文字列が残る。今セッションで表示したメッセージ(未操作なら空)で必ず上書きする。
	// ★未操作(空)のとき=初めてパネルを開いたときは、使い方の初期ヒントを英語で表示する
	//   (ソース/ターゲットを開いてフライアウトメニューから Start、という案内)。Start 等を一度でも
	//   操作すれば gSessionStatus がそのメッセージで埋まり、以後ヒントは出ない。
	if (gSessionStatus.CharCount() == 0)
	{
		PMString hint("Open the target and source documents (the active one becomes the Target), then choose Start from the panel menu.");
		hint.SetTranslatable(kFalse);
		KESCMSetStatus(hint);	// (メンバ SetStatus は単純転送だったため撤去し直接呼ぶ 2026-07-25)
	}
	else
	{
		KESCMSetStatus(gSessionStatus);
	}

	// Prev/Next の間の現在位置表示とボタン有効/無効は、上の UpdateInfoDisplay(→KESCMApplyPanelInfo
	// →KESCMRefreshNavPosition)で今の実状態から作り直し済み。ワークスペースに永続化された前回の値は
	// そこで確実に上書きされるので、ここでの復元処理は不要。

	// 半透明トグルが ON なら貼り直す。パネルを開き直すと半透明の付け先である
	// トップレベル窓(OWL.Dock)が別物に変わるため([[win32-window-alpha-transparency]])。
	// ★2026-08-06: 対象が2つ(自パネル/本体のページパネル)になったので、ここは全対象を見る。
	//   自分のパネルが作り直された機会にページパネル側も貼り直しておく方が取りこぼしが無く、
	//   全部 OFF なら下のガードで何もしない。
	// ★OFF のときはこちらで弾いて呼ばない(2026-08-06 再点検)。Apply は OFF でも中で弾かれない
	//   (弾くのはドッキング中=対象窓なしのときだけ)ので、無条件に呼ぶと使っていない人にも
	//   窓探索(キャッシュ失効時は SDK への問い合わせ)+alpha 書き+影の SW_SHOWNA の費用を払わせる。
	//   MouseEnter/MouseLeave/フック/可視性オブザーバの各入口が OFF を弾くのと同じ方針。
	//   ⚠Apply 側に OFF ガードを入れてはいけない: メニューで OFF にした瞬間の 255 復元・影の再表示は
	//     Apply(OFF 状態での呼び出し)が担っている(KESCMActionComponent.cpp のトグル経路)。
	// ★ここは保険で、主たる追随は KESCMPanelAlpha.cpp のオブザーバ(kPaletteVisibilityChangedMessage)。
	//   ★注意: この AutoAttach は widget を作り直すたびに走るので、固定の既定値を書く場所ではない
	//   (KESCMGetPanelTranslucent の現在値を読んで反映するだけ)。
	//
	// ★起動時(KESCMPeekStartup::Startup)にはパネルマネージャがまだ立ち上がっておらず購読に
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
			default: break;
		}
	}
}

//----------------------------------------------------------------------------------------
// アクション
//----------------------------------------------------------------------------------------

// KESCMToggleStartStop(KESCMCore.h で宣言) — 比較の開始/解除トグル。旧パネルの Start/Stop ボタンの
// DoStart/DoClear を統合した自由関数で、フライアウト項目 kKESCMPopupStartStopActionID の DoAction から
// 呼ぶ。arm 済みなら解除、未 arm なら開始。表示更新は KESCMRefreshPanel(可視パネルを arm 状態へ)。
void KESCMToggleStartStop()
{
	const bool16 armed = KESCMIsArmed() && (KESCMArmedTargetDB() != nil);
	if (armed)
	{
		// 解除(旧 DoClear)。アクティブ文書は再描画にだけ使う(nil でも可)。KESCMDoClearMarks /
		// KESCMDoDisarmMousePeek は内部で「実際にマークが描かれていた文書」(sDB / arm 済み target)を
		// 控えて再描画するので、文書が1つも開いていなくても消去・解除は常に成立させる。
		IDocument* active = KESCMActiveDoc();
		IDataBase* db = (active != nil) ? ::GetUIDRef(active).GetDataBase() : nil;

		KESCMDoClearMarks(db);
		KESCMDoDisarmMousePeek(db);
		// スクロールバー地図: まず比較用の strip を全窓(Target/Source 両方)から取り外す。
		// ★Find Overset が単独 ON 中なら、続けて overset 文書(sOversetDB)だけへ strip を貼り直す
		//   (比較解除で Source 窓に strip が残らないようにする 2026-07-24)。あわせて overset を再走査して
		//   リフレッシュする(ユーザー報告: overset 有りで Start→編集で解消→Stop すると、集合が編集前のまま
		//   残り「まだ有る」と判断していた)。KESCMApplyOversetForDoc は sOversetDB を再走査し、サムネイル/
		//   地図の Attach+Invalidate/Prev-Next をまとめて更新する。
		KESCMScrollMapDetachAll();	// スクロールバー地図stripを全窓(Target/Source)から取り外す
		if (KESCMDrawEventHandler::sOversetOn)
			KESCMApplyOversetForDoc(KESCMDrawEventHandler::sOversetDB);
		PMString s("marks cleared"); s.SetTranslatable(kFalse);
		KESCMSetStatus(s);
	}
	else
	{
		// 開始(旧 DoStart)。アクティブ(前面)文書=Target、別の開いている文書=Source。
		// ★フライアウトの Start は文書が2つ揃っていなければ灰色なので(KESCMCanStartComparison=同じ
		//   解決子を通る)、通常ここで欠けることは無い。メニューを開いたまま文書が閉じた場合などの保険。
		IDocument* target = nil;
		IDocument* source = nil;
		if (!KESCMResolveComparisonPair(target, source))
		{
			// 実際に欠けているものを言う(target が居るなら足りないのは Source だけ。2026-08-06 再点検)。
			PMString s(target == nil ? "Target and source documents not found."
			                         : "Source document not found.");
			s.SetTranslatable(kFalse);
			KESCMSetStatus(s);
			return;
		}

		IDataBase* targetDB = ::GetUIDRef(target).GetDataBase();
		IDataBase* sourceDB = ::GetUIDRef(source).GetDataBase();

		PMString report;
		// 「Show Marks on Source」は Start のたびに既定 ON へ戻す(仕様)。再比較(登録トグル/Ignore 切替)で
		// 黙って ON に戻さないよう、KESCMDoMarkChangesDoc 側ではなく Start 経路のここで立てる(2026-07-25)。
		KESCMDrawEventHandler::sSrcMarksOn = kTrue;
		// ★比較をユーザーがキャンセルしたら(ページ数が多いときは進捗バーに Cancel が出る)Start しない。
		//   マークは KESCMDoMarkChangesDoc 側で破棄済みなので、arm も strip 注入もせず「押す前」の状態へ
		//   戻す(中途半端に arm だけ残して、枠が1つも無い Start 中を作らない)。
		if (KESCMDoMarkChangesDoc(targetDB, sourceDB, report) == kSuccess)
		{
			KESCMDoArmMousePeek(targetDB, sourceDB);
			KESCMScrollMapAttach(targetDB);	// Target の各文書窓にスクロールバー地図stripを注入
			KESCMScrollMapAttach(sourceDB);	// Source 窓にも表示(2026-07-11 ユーザー要望。strip 側が窓の文書を見て供給元を切替)
			// ★Find Overset が ON のままなら、Start 時に必ず比較 Target を再走査して overset を貼り直す(2026-07-24)。
			//   これで (a) Start 後も Prev/Next が「変更(枠)→ overset」を同じ Target 文書で巡れる(overset が
			//   sOversetDB!=sDB で黙って巡回対象から外れる不具合の防止)＋(b) 同一文書でも編集で増減した overset を
			//   リフレッシュできる(ユーザー報告: 同一文書だと再走査せず古い集合が残っていた)。
			if (KESCMDrawEventHandler::sOversetOn)
				KESCMApplyOversetForDoc(targetDB);
		}
		KESCMSetStatus(report);
	}

	KESCMRefreshPanel();	// Target/Source 名・アイコン・Prev/Next 有効無効を arm 状態へ更新
}

// KESCMSetMarkOpacity25(KESCMCore.h で宣言) — 枠の不透明度を 25%/75% に設定。旧パネルの opacity ラジオの
// 代わりに、フライアウト項目 kKESCMPopupOpacity25ActionID / kKESCMPopupOpacity75ActionID の DoAction から
// 呼ぶ。現在の印刷フラグ(KESCMGetPrintMarks)を維持したまま不透明度だけを反映する。ラジオ相当の見た目
// (選択中の項目に✓)はメニューを開いたときに UpdateActionStates が KESCMGetMarkOpacity25 を読んで反映する。
void KESCMSetMarkOpacity25(bool16 op25)
{
	IDocument* active = KESCMActiveDoc();
	IDataBase* db = (active != nil) ? ::GetUIDRef(active).GetDataBase() : nil;

	const bool16 flag = KESCMGetPrintMarks();	// 現在の印刷 ON/OFF を維持
	KESCMDoSetPrintMarks(flag, op25, db);

	PMString report;
	report.SetTranslatable(kFalse);
	report.Append(op25 ? "kescm: marks opacity 25%" : "kescm: marks opacity 75%");
	report.Append(flag ? "; will print (and stay visible on screen)"
	                   : "; screen-only (won't print)");
	KESCMSetStatus(report);
}

// KESCMTogglePrintMarks(KESCMCore.h で宣言) — 印刷マーク ON/OFF トグル。旧パネルのチェックボックスの
// 代わりに、フライアウト項目 kKESCMPopupPrintMarksActionID の DoAction から呼ぶ。現在の印刷フラグを反転し、
// 不透明度は現在の選択(KESCMGetMarkOpacity25)を維持して反映する。表示更新はステータス行のみ
// (チェックマークはメニューを開いたときに UpdateActionStates が KESCMGetPrintMarks を読んで反映する)。
void KESCMTogglePrintMarks()
{
	IDocument* active = KESCMActiveDoc();
	IDataBase* db = (active != nil) ? ::GetUIDRef(active).GetDataBase() : nil;

	const bool16 newFlag = !KESCMGetPrintMarks();
	const bool16 op25    = KESCMGetMarkOpacity25();
	KESCMDoSetPrintMarks(newFlag, op25, db);

	PMString report;
	report.SetTranslatable(kFalse);
	report.Append(op25 ? "kescm: marks opacity 25%" : "kescm: marks opacity 75%");
	report.Append(newFlag ? "; will print (and stay visible on screen)"
	                      : "; screen-only (won't print)");
	KESCMSetStatus(report);
}

//----------------------------------------------------------------------------------------
// 表示ヘルパ
//----------------------------------------------------------------------------------------

// 表示中の ChangeMarker パネルの IControlView を返す(隠れている/引けないときは nil)。
// ★下の KESCMRefreshPanel / KESCMSetStatus / KESCMSetNavPosition が
//   「session → app → panelMgr → GetVisiblePanel」の定型を丸ごと3回持っていたので一本化した
//   (2026-08-06 監査 C-1)。同じプラグイン内の KESCMGetVisiblePagesPanel
//   (KESCMThumbnailRefresh.h:24-27)と同じ作り＝Pages パネル側だけ解いてあった穴を埋める形。
// ★session の nil ガードもここで吸収する: 3つともクローズ responder から呼ばれ、アプリ終了の
//   ティアダウン中にも到達し得る(2026-07-25 に KESCM 全体で統一した規約)。
static IControlView* KESCMGetVisibleOwnPanel()
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

	const bool16 started = KESCMIsArmed() && (KESCMArmedTargetDB() != nil);

	// Target:/Source: ラベルは常時。名前は開始中のみ表示(英語固定: 現状英語のまま)。
	PMString target("Target:"); target.SetTranslatable(kFalse);
	if (started)
	{
		target.Append(" ");
		target.Append(KESCMDocNameFromDB(KESCMArmedTargetDB()));
	}
	PMString source("Source:"); source.SetTranslatable(kFalse);
	if (started && KESCMArmedSourceDB() != nil)
	{
		source.Append(" ");
		source.Append(KESCMDocNameFromDB(KESCMArmedSourceDB()));
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

	// Prev/Next(変更ページナビ)の有効/無効と、その間の現在位置表示(k/N・-・空)は
	// KESCMRefreshNavPosition に一元化(比較中かつ変更ページありのときだけ有効。無ければ無効+"/"、
	// 未 Start は無効+空)。attach 時・Start/Stop・文書クローズ/切替のすべてがこの関数を通るので
	// 初期状態から正しく反映される。値の作り方は KESCMChangeNav.cpp を参照。
	KESCMRefreshNavPosition();

	// (Start/Stop の切替はパネルボタンから撤去し、フライアウト項目 kKESCMPopupStartStopActionID の
	//  動的ラベル(UpdateActionStates)へ移行 2026-07-10。ここでのボタンラベル設定は不要になった。)
}

void KESCMPanelObserver::UpdateInfoDisplay()
{
	InterfacePtr<IPanelControlData> pcd(this, UseDefaultIID());
	KESCMApplyPanelInfo(pcd);
}

//========================================================================================
// KESCMRefreshPanel(KESCMCore.h で宣言)
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
// KESCMSetStatus(KESCMCore.h で宣言)
//   パネルのステータス行を更新する。メンバ SetStatus(自パネル)と同じ処理を自由関数として公開し、
//   クローズレスポンダ(KESCMHandleDocsClosed)からも Stop 相当のメッセージを出せるようにする。
//   パネルが隠れていてもセッション状態(gSessionStatus)は覚えておき、再表示時に復元する。
//========================================================================================
void KESCMSetStatus(const PMString& s, bool16 forceRedrawNow)
{
	gSessionStatus = s;	// パネルを隠して再表示したときに復元できるよう、今セッションの表示内容を覚えておく

	IControlView* panel = KESCMGetVisibleOwnPanel();
	if (panel == nil)
		return;		// パネルは隠れている(または終了処理中): 触る先が無い。
	InterfacePtr<IPanelControlData> pcd(panel, UseDefaultIID());
	if (pcd == nil)
		return;
	IControlView* cv = pcd->FindWidget(kKESCMStatusTextWidgetID);
	if (cv == nil)
		return;
	InterfacePtr<ITextControlData> tcd(cv, UseDefaultIID());
	if (tcd != nil)
		tcd->SetString(s);

	// この直後にブロッキング処理(比較ループ等)が続く場合、SetString の invalidate は次の
	// イベントループまで反映されない。busyMsg 表示のために今すぐ同期描画させる。
	if (forceRedrawNow)
		panel->ForceRedraw(nil, kTrue);
}

//========================================================================================
// KESCMGetSessionStatus(KESCMCore.h で宣言)
//   KESCMSetStatus が最後に出した文字列を返す。app.kcmStatus(KESCMScriptProvider.cpp)の値。
//   ★読む先は widget ではなく gSessionStatus なので、パネルが閉じていても答えられる
//     (スクリプトから実行して結果だけ読み取るテストは、パネルを開く必要が無い)。
//========================================================================================
void KESCMGetSessionStatus(PMString& out)
{
	out = gSessionStatus;
	out.SetTranslatable(kFalse);	// 状態表示は組み立て済みの文で翻訳キーではない
}

//========================================================================================
// KESCMClearSessionStatus(KESCMCore.h で宣言)
//   Shutdown 専用。gSessionStatus(file-static PMString)を空にして、プラグイン unload 時の
//   静的デストラクタを実質 no-op にする(UI には一切触らない。KESCMSetStatus は使わないこと=
//   あちらはパネル widget を探しに行くため終了処理中は不可)。
//========================================================================================
void KESCMClearSessionStatus()
{
	gSessionStatus.Clear();
}

//========================================================================================
// KESCMSetNavPosition(KESCMCore.h で宣言)
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

	// 位置表示。★inval は SetString がやっている(ITextControlData.h:53-54 の第2引数 invalidate は
	//   既定 kTrue =「specifies whether the control should be redrawn」)。ただし inval だけでは次の
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
