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
#include "IActionStateList.h"		// UpdateActionStates(チェックマーク表示)。IPMUnknown 派生ではない
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
#include "IPanorama.h"				// GetBounds(ペーストボード範囲)/ScrollContentLocationToFrameCenter/GetYScaleFactor

// プロジェクト内:
#include "KESCMID.h"
#include "KESCMCore.h"		// KESCMOpenAboutURL
#include "KESCMDrawEventHandler.h"	// KESCMQueryPanorama(IControlView から IPanorama を辿る共有ヘルパ)

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

// 「Split Target on Start」トグルの状態(セッション内のみ保持、印刷チェック等の既存状態と同じ扱い)。
// kTrue なら Start 成功時に KESCMDoSplitTarget() が自動で走る。既定は ON。
static bool16 sSplitOnStart = kTrue;

bool16 KESCMGetSplitOnStart()
{
	return sSplitOnStart;
}

/** ChangeMarker プラグインのメニュー項目に対する IActionComponent の実装。
*/
class KESCMActionComponent : public CActionComponent
{
public:
	KESCMActionComponent(IPMUnknown* boss) : CActionComponent(boss) {}

	/** Execute the requested menu action. */
	void DoAction(IActiveContext* ac, ActionID actionID, GSysPoint mousePoint = kInvalidMousePoint, IPMUnknown* widget = nil);

	/** 「Split Target on Start」(kCustomEnabling)のチェックマークを sSplitOnStart に合わせて更新する。 */
	virtual void UpdateActionStates(IActiveContext* ac, IActionStateList* listToUpdate, GSysPoint mousePoint = kInvalidMousePoint, IPMUnknown* widget = nil);

private:
	void DoAbout();
	void DoAboutScript();
	void DoUsage();
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

		// 「Split Target on Start」トグル: フラグを反転する。ON にした時点で既に Start 済み(armed)
		// なら、次の Start を待たずにその場でスプリットを適用する。OFF にしてもスプリット済みの
		// ウィンドウは戻さない(手動で閉じられるため)。
		case kKESCMPopupSplitTargetActionID:
			sSplitOnStart = !sSplitOnStart;
			if (sSplitOnStart && KESCMIsArmed() && (KESCMArmedTargetDB() != nil))
				KESCMDoSplitTarget();
			break;

		default:
			break;
	}
}

/* UpdateActionStates — 「Split Target on Start」のチェックマーク。常に有効、ON なら kSelectedAction
   を立てる(docwatch の DocWchActionComponent::UpdateActionStates と同じ流儀)。 */
void KESCMActionComponent::UpdateActionStates(IActiveContext* /*ac*/, IActionStateList* listToUpdate, GSysPoint /*mousePoint*/, IPMUnknown* /*widget*/)
{
	int16 actionState = kEnabledAction;
	if (sSplitOnStart)
		actionState |= kSelectedAction;

	for (int32 i = 0; i < listToUpdate->Length(); i++)
	{
		if (listToUpdate->GetNthAction(i) == kKESCMPopupSplitTargetActionID)
			listToUpdate->SetNthActionState(i, actionState);
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

// KESCMDoSplitTarget(KESCMCore.h で宣言) — Start 成功時(および「Split Target on Start」を armed 中に
//   ON へ切り替えた時)に呼ばれる。
//   KESCM の Target 文書(Start 済みの比較対象、KESCMArmedTargetDB)を Split Window で2分割し、
//   新しく現れた側(kLayoutSecondaryPanelWidgetID)を全体の90%、元から表示していた側
//   (kLayoutWidgetBoss)を10%にする。さらに元の側は概観用として拡大率を5%にし、ペーストボード
//   X中央・Y上端が見えるようスクロールする(左右にはずれず、上寄せの俯瞰表示)。
//   ★前提(Split Test の実測結果からの類推、要目視確認): SetSplitterEdge に「全体の10%」を渡すと
//   境界(先頭側=index0)が10%地点に来る。先頭側が元のペイン(kLayoutWidgetBoss)である前提だが、
//   もし逆(新しいペインが10%になる)場合は下の target 計算を (1.0 - 0.10) に反転すればよい。
//   成功時は無音(Start の比較レポート表示を上書きしない)。失敗時のみパネルのステータス行に出す。
//   ドキュメントモデルには一切触れない(UI状態のみ)ので Command 化は不要。
void KESCMDoSplitTarget()
{
	IDataBase* targetDB = KESCMArmedTargetDB();
	if (targetDB == nil)
	{
		PMString msg("Split Target: ChangeMarker is not armed (press Start first).");
		msg.SetTranslatable(kFalse);
		KESCMSetStatus(msg);
		return;
	}

	IDocumentPresentation* pres = Utils<IDocumentUIUtils>()->GetFrontmostPresentationForDocument(targetDB);
	if (pres == nil)
	{
		PMString msg("Split Target: no open presentation for the Target document.");
		msg.SetTranslatable(kFalse);
		KESCMSetStatus(msg);
		return;
	}

	if (Utils<IGalleyUtils>() != nil && Utils<IGalleyUtils>()->InGalleyOrStory(pres))
	{
		PMString msg("Split Target: the Target document is in Galley/Story view; cannot split.");
		msg.SetTranslatable(kFalse);
		KESCMSetStatus(msg);
		return;
	}

	if (!Utils<ILayoutViewUtils>()->IsSplitLayoutViewShown(pres))
		Utils<ILayoutViewUtils>()->ShowSplitLayoutView(pres);

	InterfacePtr<IPanelControlData> panelData(pres, UseDefaultIID());
	IControlView* splitterView = (panelData != nil) ? panelData->FindWidget(kLayoutSplitterPanelWidgetID) : nil;
	if (splitterView == nil)
	{
		PMString msg("Split Target: splitter widget not found.");
		msg.SetTranslatable(kFalse);
		KESCMSetStatus(msg);
		return;
	}

	InterfacePtr<ISplitterPanelControlData> splitterData(splitterView, UseDefaultIID());
	InterfacePtr<ISplitterPanelController>  splitterCtrl(splitterData, UseDefaultIID());
	if (splitterData == nil || splitterCtrl == nil)
	{
		PMString msg("Split Target: splitter interfaces not available.");
		msg.SetTranslatable(kFalse);
		KESCMSetStatus(msg);
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

	// 元側ペインが見つからなくてもスプリット自体は成立しているので、ズーム/スクロールは静かにスキップする。
	if (originalView != nil)
	{
		K2Vector<IControlView*> zoomViews;
		zoomViews.push_back(originalView);
		K2Vector<PBPMPoint> zoomPoints;	// 空 = ビュー中心を基準にズーム
		Utils<ILayoutViewUtils>()->ZoomLayoutViews(zoomViews, zoomPoints, kTrue, PMReal(0.05));

		// 概観ペインの表示位置: X はペーストボード中央、Y はペーストボード上端(=左右にはずれず、上寄せ)。
		// ScrollContentLocationToFrameCenter は「指定した content 座標をビュー中心に置く」動作しか
		// 持たない(検証済み、KESCM/KESCL で使用実績あり)ため、狙った座標を直接センターに渡すのではなく、
		// 「センターに置いたときにペーストボード上端がビュー最上部に来る」ような座標を逆算して渡す。
		// 具体的には Y をペーストボード上端からビュー可視高さの半分だけ下にずらした座標を中心指定する。
		// ズーム直後の値を使うため、この計算は ZoomLayoutViews の後で行う。
		InterfacePtr<IPanorama> origPano(KESCMQueryPanorama(originalView));
		if (origPano != nil)
		{
			const PMRect pbBounds = origPano->GetBounds();	// ペーストボード範囲(content座標)
			const PMReal centerX = (pbBounds.Left() + pbBounds.Right()) / 2;
			const PMReal topY = pbBounds.Top();

			const PMReal yScale = origPano->GetYScaleFactor();	// content→window スケール(ズーム後の実効値)
			if (yScale > 0)
			{
				const PMReal halfViewContentHeight = (originalView->GetFrame().Height() / yScale) / 2;
				const PBPMPoint centerTarget(centerX, topY + halfViewContentHeight);
				origPano->ScrollContentLocationToFrameCenter(centerTarget, kTrue);
			}
		}
	}
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
