//========================================================================================
//
//  KESCMPanelObserver.cpp
//
//  ChangeMarker 操作パネルの IObserver。work/changemarker-panel.jsx を再現する:
//    - Start ボタン : Target(=アクティブ文書)＋ Source(=もう一方の開いている文書)を解決し、
//                     変更ページ全部にマークを付け、peek を arm する。
//    - Clear ボタン : オーバーレイを消去し、peek を disarm する。
//    - 印刷チェック : SetPrintMarks 経由で、マークを印刷するか(かつ画面に残すか)を切り替える。
//    - 25% / 75%    : 枠の不透明度の選択。ツール左hold中の表示・印刷ON中の常時表示・印刷出力の全部に効く
//                     (印刷ON/OFFに依らず常に有効)。
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
#include "IApplication.h"			// GetExecutionContextSession / QueryApplication
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
#include "KESCMPanelState.h"		// KESCMLoadPanelStateIfPresent(保存済み設定をパネル初回オープン時に読み込む)

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
	void SetStatus(const PMString& s);
};

CREATE_PMINTERFACE(KESCMPanelObserver, kKESCMPanelObserverImpl)

//----------------------------------------------------------------------------------------
// 今セッションで最後に表示したステータス文字列。
// StaticMultiLineTextWidget の内容はワークスペースに永続化されるため、InDesign を再起動して
// アイコン状態のパネルを開くと前回セッションの文字列(例: "marks start / pages compared=22")が残って
// しまう。そこで「今セッションで表示したメッセージ」だけをここに覚えておき、AutoAttach で必ず
// 上書きする。プラグインを一度も操作していなければ空文字なので何も表示されない。
//----------------------------------------------------------------------------------------
namespace { PMString gSessionStatus; }

//----------------------------------------------------------------------------------------
// ローカルヘルパ
//----------------------------------------------------------------------------------------

// アクティブ(前面)文書 = 比較の Target。
static IDocument* KESCMActiveDoc()
{
	IActiveContext* ac = GetExecutionContextSession() ? GetExecutionContextSession()->GetActiveContext() : nil;
	return ac ? ac->GetContextDocument() : nil;
}

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

// db を所有する文書の表示名(JSX パネルと同様、ラベルに収まるよう短縮する)。
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

	// 長すぎる名前は末尾を残して切り詰め(JSX の shortName 相当)。
	if (name.CharCount() > 26)
	{
		PMString* tail = name.Substring(name.CharCount() - 23, 23);
		PMString shortened("...");
		shortened.SetTranslatable(kFalse);
		if (tail != nil)
		{
			shortened.Append(*tail);
			delete tail;
		}
		name = shortened;
	}
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
		this->SetStatus(hint);
	}
	else
	{
		this->SetStatus(gSessionStatus);
	}

	// Prev/Next の間の現在位置表示とボタン有効/無効は、上の UpdateInfoDisplay(→KESCMApplyPanelInfo
	// →KESCMRefreshNavPosition)で今の実状態から作り直し済み。ワークスペースに永続化された前回の値は
	// そこで確実に上書きされるので、ここでの復元処理は不要。
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
		KESCMScrollMapDetachAll();	// スクロールバー地図stripを全窓から取り外す
		PMString s("marks cleared"); s.SetTranslatable(kFalse);
		KESCMSetStatus(s);
	}
	else
	{
		// 開始(旧 DoStart)。アクティブ(前面)文書=Target、別の開いている文書=Source。
		IDocument* target = KESCMActiveDoc();
		if (target == nil)
		{
			PMString s("Target and source documents not found."); s.SetTranslatable(kFalse);
			KESCMSetStatus(s);
			return;
		}
		IDocument* source = KESCMFirstOtherDoc(target);
		if (source == nil)
		{
			PMString s("Target or source documents not found."); s.SetTranslatable(kFalse);
			KESCMSetStatus(s);
			return;
		}

		IDataBase* targetDB = ::GetUIDRef(target).GetDataBase();
		IDataBase* sourceDB = ::GetUIDRef(source).GetDataBase();

		PMString report;
		KESCMDoMarkChangesDoc(targetDB, sourceDB, report);
		KESCMDoArmMousePeek(targetDB, sourceDB);
		KESCMScrollMapAttach(targetDB);	// Target の各文書窓にスクロールバー地図stripを注入
		KESCMScrollMapAttach(sourceDB);	// Source 窓にも表示(2026-07-11 ユーザー要望。strip 側が窓の文書を見て供給元を切替)
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

void KESCMPanelObserver::SetStatus(const PMString& s)
{
	KESCMSetStatus(s);
}

//========================================================================================
// KESCMRefreshPanel(KESCMCore.h で宣言)
//   現在表示中の ChangeMarker パネルがあれば、その ON/OFF 表示を現在の arm 状態へ更新する。
//   パネルが隠れていれば何もしない(次に開いたとき AutoAttach が実状態を反映する)。
//   クローズレスポンダ(KESCMHandleDocsClosed)から、追跡文書が閉じてパネルを OFF に戻すときに呼ぶ。
//========================================================================================
void KESCMRefreshPanel()
{
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	if (app == nil)
		return;
	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr == nil)
		return;
	IControlView* panel = panelMgr->GetVisiblePanel(kKESCMPanelWidgetID);
	if (panel == nil)
		return;		// パネルは隠れている: 触る先が無い。
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

	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	if (app == nil)
		return;
	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr == nil)
		return;
	IControlView* panel = panelMgr->GetVisiblePanel(kKESCMPanelWidgetID);
	if (panel == nil)
		return;		// パネルは隠れている: 触る先が無い。
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
// KESCMSetNavPosition(KESCMCore.h で宣言)
//   Prev/Next の間の現在位置表示(kKESCMNavPosTextWidgetID、例 "3/12")と、Prev/Next ボタンの
//   有効/無効をまとめて更新する。パネルが隠れていれば何もしない(再表示時に KESCMRefreshNavPosition が
//   実状態を反映する)。値の決定は呼び出し側(KESCMRefreshNavPosition)に集約。
//========================================================================================
void KESCMSetNavPosition(const PMString& posText, bool16 navButtonsEnabled)
{
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	if (app == nil)
		return;
	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr == nil)
		return;
	IControlView* panel = panelMgr->GetVisiblePanel(kKESCMPanelWidgetID);
	if (panel == nil)
		return;		// パネルは隠れている: 触る先が無い。
	InterfacePtr<IPanelControlData> pcd(panel, UseDefaultIID());
	if (pcd == nil)
		return;

	// 位置表示。★StaticTextWidget は SetString だけでは次の再描画契機まで古い値が残る(StaticMultiLineText
	//   と違い自動 invalidate されない)。Start での変化や Next/Prev の値変更を即時反映させるため、明示的に
	//   invalidate → 今すぐ再描画する(2026-07-15 ユーザー報告「1/5 が即時更新されない」)。
	IControlView* cv = pcd->FindWidget(kKESCMNavPosTextWidgetID);
	if (cv != nil)
	{
		InterfacePtr<ITextControlData> tcd(cv, UseDefaultIID());
		if (tcd != nil)
		{
			tcd->SetString(posText);
			cv->Invalidate();
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
