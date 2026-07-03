//========================================================================================
//
//  KESCMActionComponent.cpp
//
//  プラグインのメニューアクションを処理する: 「プラグインについて」エントリと、パネルのフライアウトの
//  「このプラグインについて」エントリ。BasicPanel サンプル(BscPnlActionComponent.cpp)を手本にしている。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// 一般:
#include "CActionComponent.h"
#include "CAlert.h"
#include "PMString.h"

// Split Target(90/10)機能用:
#include "IDocumentUIUtils.h"		// Utils<IDocumentUIUtils>()->GetActiveDocumentPresentation/GetFrontmostPresentationForDocument
#include "IDocumentPresentation.h"
#include "IPanelControlData.h"		// FindWidget(kLayoutSplitterPanelWidgetID)
#include "IControlView.h"			// GetFrame()
#include "LayoutUIID.h"				// kLayoutSplitterPanelWidgetID
#include "ISplitterPanelControlData.h"	// source/open/interfaces/ui。GetSplitterEdge/GetSplitterPercent等
#include "ISplitterPanelController.h"	// source/open/interfaces/ui。SetSplitterEdge/SyncPanelsToSplitter
#include "ILayoutViewUtils.h"		// Show/HideSplitLayoutView, IsSplitLayoutViewShown, GetAllLayoutViews(上位Facade)
#include "IGalleyUtils.h"			// InGalleyOrStory(ガレー/ストーリー編集中はスプリットレイアウトの概念が異なるためガード)
#include "IDataBase.h"				// GetDocumentUIDRef().GetDataBase()

// プロジェクト内:
#include "KESCMID.h"
#include "KESCMCore.h"		// KESCMOpenAboutURL

// ★注意: source/public/includes/URLUtils.h は "namespace URLUtils { PUBLIC_DECL void GoToURL(...); }" と
// 宣言しているが、これはヘッダーとバイナリの不一致(Public.lib 側の実エクスポート名と食い違っている)。
// build/win/objrx64/Public.lib の生シンボルを確認したところ、実際にリンク可能な名前は
// "?GoToURL@GoToURLUtils@@YAXAEBVPMString@@F@Z" = void GoToURLUtils::GoToURL(const PMString&, bool16)
// であり、URLUtils 名前空間版は存在しない(リンクエラー確認済み)。ヘッダーは信用せず、実バイナリに
// 合わせてここで自前に前方宣言する。
namespace GoToURLUtils
{
	PUBLIC_DECL void GoToURL(const PMString& goToURL, bool16 isAGoURL);
}

/** ChangeMarker プラグインのメニュー項目に対する IActionComponent の実装。
*/
class KESCMActionComponent : public CActionComponent
{
public:
	KESCMActionComponent(IPMUnknown* boss) : CActionComponent(boss) {}

	/** Execute the requested menu action. */
	void DoAction(IActiveContext* ac, ActionID actionID, GSysPoint mousePoint = kInvalidMousePoint, IPMUnknown* widget = nil);

private:
	void DoAbout();
	void DoAboutScript();
	void DoUsage();
	void DoSplitTarget();	// Target文書をSplit Windowで90/10に分割し、元側を5%ズームにする
};

/* Binds the C++ implementation class onto its ImplementationID. */
CREATE_PMINTERFACE(KESCMActionComponent, kKESCMActionComponentImpl)

/* DoAction */
void KESCMActionComponent::DoAction(IActiveContext* /*ac*/, ActionID actionID, GSysPoint /*mousePoint*/, IPMUnknown* /*widget*/)
{
	switch (actionID.Get())
	{
		case kKESCMAboutActionID:
		case kKESCMPopupAboutThisActionID:
			this->DoAbout();
			break;

		case kKESCMPopupAboutScriptActionID:
			this->DoAboutScript();
			break;

		case kKESCMPopupUsageActionID:
			this->DoUsage();
			break;

		case kKESCMPopupSplitTargetActionID:
			this->DoSplitTarget();
			break;

		default:
			break;
	}
}

/* DoAbout */
void KESCMActionComponent::DoAbout()
{
	CAlert::ModalAlert
	(
		kKESCMAboutBoxStringKey,	// Alert string
		kOKString,					// OK button
		kNullString,				// No second button
		kNullString,				// No third button
		1,							// Set OK button to default
		CAlert::eInformationIcon	// Information icon
	);
}

/* DoAboutScript — パネルのフライアウト「スクリプトについて」。スクリプトAPIは撤去済みなので、その旨を表示する。 */
void KESCMActionComponent::DoAboutScript()
{
	CAlert::ModalAlert
	(
		kKESCMScriptHelpStringKey,	// Alert string ("No scripts are currently available.")
		kOKString,					// OK button
		kNullString,				// No second button
		kNullString,				// No third button
		1,							// Set OK button to default
		CAlert::eInformationIcon	// Information icon
	);
}

/* DoUsage — パネルのフライアウト「使い方」。中ボタン操作リファレンス(=旧パネルの説明文)を表示する。 */
void KESCMActionComponent::DoUsage()
{
	CAlert::ModalAlert
	(
		kKESCMHintKey,				// Alert string (gesture reference; formerly the panel hint)
		kOKString,					// OK button
		kNullString,				// No second button
		kNullString,				// No third button
		1,							// Set OK button to default
		CAlert::eInformationIcon	// Information icon
	);
}

// DoSplitTarget — パネルのフライアウト「Split Target (90/10)」。
//   KESCM の Target 文書(Start 済みの比較対象、KESCMArmedTargetDB)を Split Window で2分割し、
//   新しく現れた側(kLayoutSecondaryPanelWidgetID)を全体の90%、元から表示していた側
//   (kLayoutWidgetBoss)を10%にする。さらに元の側は概観用として拡大率を5%にする。
//   ★前提(Split Test の実測結果からの類推、要目視確認): SetSplitterEdge に「全体の10%」を渡すと
//   境界(先頭側=index0)が10%地点に来る。先頭側が元のペイン(kLayoutWidgetBoss)である前提だが、
//   もし逆(新しいペインが10%になる)場合は下の target 計算を (1.0 - 0.10) に反転すればよい。
//   ドキュメントモデルには一切触れない(UI状態のみ)ので Command 化は不要。
void KESCMActionComponent::DoSplitTarget()
{
	IDataBase* targetDB = KESCMArmedTargetDB();
	if (targetDB == nil)
	{
		PMString msg("Split Target: ChangeMarker is not armed (press Start first).");
		msg.SetTranslatable(kFalse);
		CAlert::InformationAlert(msg);
		return;
	}

	IDocumentPresentation* pres = Utils<IDocumentUIUtils>()->GetFrontmostPresentationForDocument(targetDB);
	if (pres == nil)
	{
		PMString msg("Split Target: no open presentation for the Target document.");
		msg.SetTranslatable(kFalse);
		CAlert::InformationAlert(msg);
		return;
	}

	if (Utils<IGalleyUtils>() != nil && Utils<IGalleyUtils>()->InGalleyOrStory(pres))
	{
		PMString msg("Split Target: the Target document is in Galley/Story view; cannot split.");
		msg.SetTranslatable(kFalse);
		CAlert::InformationAlert(msg);
		return;
	}

	if (!Utils<ILayoutViewUtils>()->IsSplitLayoutViewShown(pres))
		Utils<ILayoutViewUtils>()->ShowSplitLayoutView(pres);

	InterfacePtr<IPanelControlData> panelData(pres, UseDefaultIID());
	IControlView* splitterView = (panelData != nil) ? panelData->FindWidget(kLayoutSplitterPanelWidgetID) : nil;
	if (splitterView == nil)
	{
		PMString msg("Split Target: kLayoutSplitterPanelWidgetID not found even after ShowSplitLayoutView.");
		msg.SetTranslatable(kFalse);
		CAlert::InformationAlert(msg);
		return;
	}

	InterfacePtr<ISplitterPanelControlData> splitterData(splitterView, UseDefaultIID());
	InterfacePtr<ISplitterPanelController>  splitterCtrl(splitterData, UseDefaultIID());
	if (splitterData == nil || splitterCtrl == nil)
	{
		PMString msg("Split Target: ISplitterPanelControlData/ISplitterPanelController query failed.");
		msg.SetTranslatable(kFalse);
		CAlert::InformationAlert(msg);
		return;
	}

	// 元の側を10%、新しい側を90%にする(横幅=IsVerticalSplitter()==trueのケースで実測済み)。
	const bool16 vertical = splitterData->IsVerticalSplitter();
	const PMRect frame = splitterView->GetFrame();
	const PMReal totalExtent = vertical ? frame.Width() : frame.Height();
	const int32 target = ::ToInt32(totalExtent * PMReal(0.10));
	splitterCtrl->SetSplitterEdge(target);
	splitterCtrl->SyncPanelsToSplitter(kTrue, kFalse);

	// 元の側(kLayoutWidgetBoss)の拡大率を5%にする。GetAllLayoutViews で両ペインの IControlView* を
	// 取得し、WidgetID で元側を特定してからズームする。
	K2Vector<IControlView*> layoutViews;
	IDataBase* db = pres->GetDocumentUIDRef().GetDataBase();
	Utils<ILayoutViewUtils>()->GetAllLayoutViews(layoutViews, nil, db);

	IControlView* originalView = nil;
	for (int32 i = 0; i < (int32)layoutViews.size(); ++i)
	{
		if (layoutViews[i] != nil && layoutViews[i]->GetWidgetID() == kLayoutWidgetBoss)
		{
			originalView = layoutViews[i];
			break;
		}
	}

	if (originalView != nil)
	{
		K2Vector<IControlView*> zoomViews;
		zoomViews.push_back(originalView);
		K2Vector<PBPMPoint> zoomPoints;	// 空 = ビュー中心を基準にズーム
		Utils<ILayoutViewUtils>()->ZoomLayoutViews(zoomViews, zoomPoints, kTrue, PMReal(0.05));
	}

	PMString msg("Split Target: done.\n");
	msg.SetTranslatable(kFalse);
	msg.Append("IsVerticalSplitter = "); msg.Append(vertical ? "true" : "false"); msg.Append("\n");
	msg.Append("splitter edge set to "); msg.AppendNumber(target); msg.Append(" of "); msg.AppendNumber(totalExtent); msg.Append("\n");
	msg.Append(originalView != nil ? "Original pane found; zoomed to 5%." : "Original pane NOT found (kLayoutWidgetBoss lookup failed); zoom skipped.");
	CAlert::InformationAlert(msg);
}

// KESCMOpenAboutURL(KESCMCore.h で宣言) — パネルのイラストクリックから呼ばれる。「このプラグインに
// ついて」本文と同じ配布元URL(kKESCMRepoURL)を既定のブラウザで開く。ドキュメントモデルには一切
// 触れない(=OSへの外部起動要求のみ)ため、Command 化は不要。
// GoToURLUtils::GoToURL は IURLAccess(hyperlink 用の内部インターフェイス)経由で Win/Mac 双方の既定
// ブラウザを起動する InDesign 純正のユーティリティ関数(PUBLIC_DECL、boss/IID 取得不要)。
// isAGoURL=kFalse は Adobe の "go.adobe.com" 短縮リンク専用フラグで、通常の外部URLでは使わない。
void KESCMOpenAboutURL()
{
	PMString url(kKESCMRepoURL);
	url.SetTranslatable(kFalse);
	GoToURLUtils::GoToURL(url, kFalse);
}

// KESCMActionComponent.cpp 終わり。
