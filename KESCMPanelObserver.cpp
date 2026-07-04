//========================================================================================
//
//  KESCMPanelObserver.cpp
//
//  ChangeMarker 操作パネルの IObserver。work/changemarker-panel.jsx を再現する:
//    - Start ボタン : Target(=アクティブ文書)＋ Source(=もう一方の開いている文書)を解決し、
//                     変更ページ全部にマークを付け、ミドルボタン peek を arm する。
//    - Clear ボタン : オーバーレイを消去し、peek を disarm する。
//    - 印刷チェック : SetPrintMarks 経由で、マークを印刷するか(かつ画面に残すか)を切り替える。
//    - 25% / 75%    : 枠の不透明度の選択。ミドル押下中の表示・印刷ON中の常時表示・印刷出力の全部に効く
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
	IControlView* FindW(const WidgetID& wid);

	void DoStart();
	void DoClear();
	void ApplyPrintMarks();
	void UpdateInfoDisplay();
	void SetStatus(const PMString& s);

	bool16 IsSelected(const WidgetID& wid);
	void   SetSelected(const WidgetID& wid, bool16 sel);	// チェックボックス/ラジオを選択・解除(通知なし)
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
	InterfacePtr<IPanelControlData> pcd(this, UseDefaultIID());
	if (pcd == nil)
		return;

	this->AttachWidget(pcd, kKESCMToggleButtonWidgetID,       IBooleanControlData::kDefaultIID);
	this->AttachWidget(pcd, kKESCMPrintCheckWidgetID,         ITriStateControlData::kDefaultIID);
	this->AttachWidget(pcd, kKESCMOpacity25RadioWidgetID,     ITriStateControlData::kDefaultIID);
	this->AttachWidget(pcd, kKESCMOpacity75RadioWidgetID,     ITriStateControlData::kDefaultIID);
	// イラスト(ON/OFF アイコン、どちらか一方だけが可視)のクリックで「このプラグインについて」の配布元
	// URL を開く。RollOverIconButtonWidget ベースのボスは ITriStateControlData のクリックで
	// kTrueStateMessage を送る(pictureicon サンプル PicIcoRollOverButtonObserver と同じ流儀)。
	this->AttachWidget(pcd, kKESCMIconOnWidgetID,             ITriStateControlData::kDefaultIID);
	this->AttachWidget(pcd, kKESCMIconOffWidgetID,            ITriStateControlData::kDefaultIID);

	// ウィジェットを現在の共有状態へ復元する。パネルを隠して再表示すると AutoAttach が再実行される
	// ため、固定の既定値ではなく engine の実状態(KESCMGetPrintMarks/KESCMGetMarkOpacity25)を読んで反映する。
	// RadioButtonWidget は .fr で初期選択状態を持たないので、ここで必ずどちらか一方だけを選択する。
	// 不透明度ラジオはミドル押下表示にも効くため、印刷ON/OFFに依らず常に有効(グレーアウトしない)。
	const bool16 printOn = KESCMGetPrintMarks();
	const bool16 op25    = KESCMGetMarkOpacity25();
	this->SetSelected(kKESCMPrintCheckWidgetID,     printOn);
	this->SetSelected(kKESCMOpacity25RadioWidgetID, op25);
	this->SetSelected(kKESCMOpacity75RadioWidgetID, !op25);

	this->UpdateInfoDisplay();		// 開始済みなら Target/Source 名と ON アイコン、未開始なら名前なし+OFF

	// ステータス欄はワークスペースに永続化されるため、再起動後にアイコン状態から開くと前回
	// セッションの文字列が残る。今セッションで表示したメッセージ(未操作なら空)で必ず上書きし、
	// 一度も起動していなければ何も表示しない。
	this->SetStatus(gSessionStatus);
}

void KESCMPanelObserver::AutoDetach()
{
	InterfacePtr<IPanelControlData> pcd(this, UseDefaultIID());
	if (pcd == nil)
		return;

	this->DetachWidget(pcd, kKESCMToggleButtonWidgetID,       IBooleanControlData::kDefaultIID);
	this->DetachWidget(pcd, kKESCMPrintCheckWidgetID,         ITriStateControlData::kDefaultIID);
	this->DetachWidget(pcd, kKESCMOpacity25RadioWidgetID,     ITriStateControlData::kDefaultIID);
	this->DetachWidget(pcd, kKESCMOpacity75RadioWidgetID,     ITriStateControlData::kDefaultIID);
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

IControlView* KESCMPanelObserver::FindW(const WidgetID& wid)
{
	InterfacePtr<IPanelControlData> pcd(this, UseDefaultIID());
	return (pcd != nil) ? pcd->FindWidget(wid) : nil;
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
			// 単一トグル: 開始中なら解除、未開始なら開始。ラベルは UpdateInfoDisplay が切替。
			case kKESCMToggleButtonWidgetID:
				if (KESCMIsArmed() && (KESCMArmedTargetDB() != nil))
					this->DoClear();
				else
					this->DoStart();
				break;
			case kKESCMPrintCheckWidgetID:         this->ApplyPrintMarks(); break;
			// 25%/75% の切替: ミドル押下表示にも効くため、印刷ON/OFFに依らず常に即反映する。
			case kKESCMOpacity25RadioWidgetID:
				this->SetSelected(kKESCMOpacity75RadioWidgetID, kFalse);	// 相互排他(手動)
				this->ApplyPrintMarks();
				break;
			case kKESCMOpacity75RadioWidgetID:
				this->SetSelected(kKESCMOpacity25RadioWidgetID, kFalse);	// 相互排他(手動)
				this->ApplyPrintMarks();
				break;
			// イラストクリック → 「このプラグインについて」の配布元URLをブラウザで開く。
			case kKESCMIconOnWidgetID:
			case kKESCMIconOffWidgetID:
				KESCMOpenAboutURL();
				break;
			default: break;
		}
	}
	else if (theChange == kFalseStateMessage)
	{
		if (wid == kKESCMPrintCheckWidgetID)
			this->ApplyPrintMarks();
	}
}

//----------------------------------------------------------------------------------------
// アクション
//----------------------------------------------------------------------------------------

void KESCMPanelObserver::DoStart()
{
	IDocument* target = KESCMActiveDoc();
	if (target == nil)
	{
		PMString s("Target and source documents not found."); s.SetTranslatable(kFalse);
		this->SetStatus(s);
		return;
	}
	IDocument* source = KESCMFirstOtherDoc(target);
	if (source == nil)
	{
		PMString s("Target or source documents not found."); s.SetTranslatable(kFalse);
		this->SetStatus(s);
		return;
	}

	IDataBase* targetDB = ::GetUIDRef(target).GetDataBase();
	IDataBase* sourceDB = ::GetUIDRef(source).GetDataBase();

	PMString report;
	KESCMDoMarkChangesDoc(targetDB, sourceDB, report);
	KESCMDoArmMousePeek(targetDB, sourceDB);
	this->SetStatus(report);

	// フライアウトの「Split Target on Start」が ON なら、Target を 90/10 の Split Window にする。
	// 成功時は無音なので上の比較レポート表示は残る(失敗時のみステータス行が差し替わる)。
	if (KESCMGetSplitOnStart())
		KESCMDoSplitTarget();

	this->UpdateInfoDisplay();
}

void KESCMPanelObserver::DoClear()
{
	// アクティブ文書は再描画にだけ使う(nil でも可)。KESCMDoClearMarks / KESCMDoDisarmMousePeek は
	// 内部で「実際にマークが描かれていた文書」(sDB / arm 済み target)を控えて再描画するので、
	// 文書が1つも開いていなくても消去・解除は常に成立させる。以前は nil で早期 return していた
	// ため、文書ゼロの状態では Stop が効かず arm 状態が残る食い違いがあった。
	IDocument* active = KESCMActiveDoc();
	IDataBase* db = (active != nil) ? ::GetUIDRef(active).GetDataBase() : nil;

	KESCMDoClearMarks(db);
	KESCMDoDisarmMousePeek(db);
	PMString s("marks cleared"); s.SetTranslatable(kFalse);
	this->SetStatus(s);
	this->UpdateInfoDisplay();
}

void KESCMPanelObserver::ApplyPrintMarks()
{
	// アクティブ文書は再描画にだけ使う(nil でも可)。フラグ自体はエンジンの共有状態なので、文書が
	// 1つも開いていなくても常に反映する。以前は nil で早期 return していたため、チェックボックスの
	// 見た目だけ変わってエンジン状態(sPrintMarks/sMarkOpacity25)は変わらず、パネルを隠して再表示すると
	// AutoAttach が実状態を読み戻してチェックが元に戻る、という UI と実状態の食い違いがあった。
	IDocument* active = KESCMActiveDoc();
	IDataBase* db = (active != nil) ? ::GetUIDRef(active).GetDataBase() : nil;

	const bool16 flag = this->IsSelected(kKESCMPrintCheckWidgetID);
	const bool16 op25 = this->IsSelected(kKESCMOpacity25RadioWidgetID);
	KESCMDoSetPrintMarks(flag, op25, db);

	PMString report;
	report.SetTranslatable(kFalse);
	report.Append(op25 ? "kescm: marks opacity 25%" : "kescm: marks opacity 75%");
	report.Append(flag ? "; will print (and stay visible on screen)"
	                   : "; screen-only (won't print)");
	this->SetStatus(report);
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

	// トグルボタンのラベル: 開始中=Stop / 未開始=Start(英語固定)。
	IControlView* toggleView = pcd->FindWidget(kKESCMToggleButtonWidgetID);
	if (toggleView != nil)
	{
		InterfacePtr<ITextControlData> tcd(toggleView, UseDefaultIID());
		if (tcd != nil)
		{
			PMString label(started ? "Stop" : "Start");
			label.SetTranslatable(kFalse);
			tcd->SetString(label, kTrue /*invalidate*/, kFalse /*don't notify*/);
		}
	}
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

bool16 KESCMPanelObserver::IsSelected(const WidgetID& wid)
{
	IControlView* cv = this->FindW(wid);
	if (cv == nil)
		return kFalse;
	InterfacePtr<ITriStateControlData> ts(cv, UseDefaultIID());
	return (ts != nil) ? ts->IsSelected() : kFalse;
}

void KESCMPanelObserver::SetSelected(const WidgetID& wid, bool16 sel)
{
	IControlView* cv = this->FindW(wid);
	if (cv == nil)
		return;
	InterfacePtr<ITriStateControlData> ts(cv, UseDefaultIID());
	if (ts == nil)
		return;
	if (sel)
		ts->Select(kTrue /*invalidate*/, kFalse /*don't notify*/);
	else
		ts->Deselect(kTrue /*invalidate*/, kFalse /*don't notify*/);
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
// KESCMEnsurePanelShown(KESCMCore.h で宣言)
//   パネルが非表示(閉じている)か、アイコン化/最小化されている(IsPanelWithWidgetIDShown が kFalse を
//   返す状態。ドキュメント曰く「minimized palette では kFalse になる」ため、閉鎖とアイコン化の両方を
//   一つの判定で拾える)なら ShowPanelByWidgetID で表示する。既に見えていれば何もしない。
//========================================================================================
void KESCMEnsurePanelShown()
{
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	if (app == nil)
		return;
	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr == nil)
		return;
	if (!panelMgr->IsPanelWithWidgetIDShown(kKESCMPanelWidgetID))
		panelMgr->ShowPanelByWidgetID(kKESCMPanelWidgetID, kFalse);	// giveKeyFocus=kFalse: ミドル操作中にフォーカスを奪わない
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

// KESCMPanelObserver.cpp 終わり。
