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

// Hide Unchanged Spreads(kHideSpreadCmdBoss)用:
#include "CmdUtils.h"				// CreateCommand/ProcessCommand(モデル変更は必ず Command 経由)
#include "ICommand.h"
#include "IBoolData.h"				// kHideSpreadCmdBoss は専用 CmdData を持たず汎用 IBoolData で方向指定(kLockLayerCmdBoss と同型)
#include "UIDList.h"
#include "ISpread.h"
#include "ISpreadList.h"
#include "SpreadID.h"				// kHideSpreadCmdBoss / IID_IHIDESPREADBOOLDATA
#include "ISession.h"				// GetExecutionContextSession(リセット時の文書生存確認)
#include "IApplication.h"
#include "IDocumentList.h"			// FindDocByDataBase(閉じた db を deref しないための生存確認)
#include <vector>
#include <map>
#include <set>

#include "IDataBase.h"				// GetRootUID()(Hide Unchanged のスプレッド走査)

// (Split Target(90/10)機能は 2026-07-04 撤去。専用 include 群も削除。
//  仕組みは docs/ai-notes/kescm-split-target-mechanism.md と git 履歴 69c4b07 に保存)

// プロジェクト内:
#include "KESCMID.h"
#include "KESCMCore.h"		// KESCMOpenAboutURL
#include "KESCMDrawEventHandler.h"	// sEntries/sDB/sShowOldNumbers(Hide Unchanged と旧番号バッジの状態参照)
#include "KESCMPageMap.h"	// KESCMPageMapToggleSelectedPages / KESCMPageMapUpdateToggleState(追加/削除ページ登録トグル)
							// ＋ KESCMBuildPairing(除外対応表、Hide Unchanged の Source 側分類で使用)
#include "KESCMPageCheck.h"	// KESCMPageCheckToggleSelectedPages / KESCMPageCheckUpdateToggleState(「KESCM: Check」の✓トグル)
#include "KESCMPageNumberMarker.h"	// KESCMGetIgnorePageNumberMarker/KESCMSetIgnorePageNumberMarker(ノンブル除外トグル)
#include "KESCMThumbnailRefresh.h"	// KESCMTryRefreshPagesPanelThumbnails(Source サムネイルの枠を即 ON/OFF)
#include "KESCMPeek.h"				// KESCMBaseScreenOpacity(Hold to Hide Marks 切替時に常時表示の基準不透明度を反映)
#include "KESCMScrollMap.h"		// KESCMScrollMapInvalidateAll(Hide Unchanged 切替後に地図を描き直す)
#include "KESCMPanelState.h"		// KESCMSavePanelState(フライアウト「Save Panel Settings」)

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

// 「Hide Unchanged Spreads」トグルの状態。ON の間、sHiddenSpreads = 自分が隠したスプレッド UID の控え
// (OFF でこれ「だけ」を再表示する。ユーザーがページパネル等で別途隠したスプレッドは巻き込まない)。
// sHiddenDB は隠した先の文書。他の static と同じくセッション内のみ保持(kHideSpreadCmdBoss は永続変更
// なので、隠し状態そのものは .indd に残る=dirty 化はユーザー了承済みの仕様)。
// Source 側も同じ分類で自動的に隠す(sHiddenSrcDB / sHiddenSrcSpreads に別控え。Source も dirty になる)。
static bool16 sHideUnchangedOn = kFalse;
static std::vector<UID> sHiddenSpreads;
static IDataBase* sHiddenDB = nil;
static std::vector<UID> sHiddenSrcSpreads;
static IDataBase* sHiddenSrcDB = nil;

/** ChangeMarker プラグインのメニュー項目に対する IActionComponent の実装。
*/
class KESCMActionComponent : public CActionComponent
{
public:
	KESCMActionComponent(IPMUnknown* boss) : CActionComponent(boss) {}

	/** Execute the requested menu action. */
	void DoAction(IActiveContext* ac, ActionID actionID, GSysPoint mousePoint = kInvalidMousePoint, IPMUnknown* widget = nil);

	/** チェック式トグル(kCustomEnabling)のチェックマークを現在の状態に合わせて更新する。 */
	virtual void UpdateActionStates(IActiveContext* ac, IActionStateList* listToUpdate, GSysPoint mousePoint = kInvalidMousePoint, IPMUnknown* widget = nil);

private:
	void DoAbout();
	void DoAboutScript();
	void DoUsage();
	void DoHideUnchangedToggle();
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

		// フライアウト先頭の「Start / Stop」: 比較の開始/解除トグル(旧パネルボタン→2026-07-10 メニュー化)。
		// 実体は KESCMPanelObserver.cpp の自由関数(arm 状態を見て開始 or 解除、実行後にパネル更新)。
		case kKESCMPopupStartStopActionID:
			KESCMToggleStartStop();
			break;

		// フライアウトの「Print comparison marks」: 印刷マーク ON/OFF トグル(旧パネルのチェックボックス
		// →2026-07-10 メニュー化)。実体は KESCMPanelObserver.cpp の自由関数。
		case kKESCMPopupPrintMarksActionID:
			KESCMTogglePrintMarks();
			break;

		// フライアウトの「Marks opacity 25% / 75%」(ラジオ風): 選んだ方の不透明度に設定する。
		// 実体は KESCMPanelObserver.cpp の自由関数(印刷フラグは維持し不透明度だけ変更)。
		case kKESCMPopupOpacity25ActionID:
			KESCMSetMarkOpacity25(kTrue);
			break;
		case kKESCMPopupOpacity75ActionID:
			KESCMSetMarkOpacity25(kFalse);
			break;

		case kKESCMPopupAboutScriptActionID:
			this->DoAboutScript();
			break;

		case kKESCMPopupUsageActionID:
			this->DoUsage();
			break;

		// 「Show Marks on Source」トグル: フラグを反転して Source 文書を再描画するだけ(表示判定と描画は
		// KESCMDrawEventHandler::HandleDrawEvent の Source 分岐。ON の間は常時表示・OPPでも表示・印刷にも
		// 出る。不透明度はパネルの 25%/75% 選択に連動)。Start のたびに既定 ON へ戻る(KESCMDoMarkChangesDoc)。
		case kKESCMPopupShowSrcMarksActionID:
		{
			KESCMDrawEventHandler::sSrcMarksOn = !KESCMDrawEventHandler::sSrcMarksOn;
			KESCMInvalidateDB(KESCMDrawEventHandler::sSrcDB);
			// ★レイアウトビューだけでなく Pages パネルの Source サムネイルも即時更新する。Source 側の枠は
			//   wantSrcMarks(=sSrcMarksOn)に依存し、サムネイル(isThumb)でも強制表示されないため、トグルで
			//   サムネイルを作り直さないと OFF にしても枠が残る/ON にしても出ない。対象ページは Source の
			//   変更/overflow/登録集合(KESCMCollectChangedPageUIDs が引く)で、枠が出得るページと一致する。
			KESCMTryRefreshPagesPanelThumbnails(KESCMDrawEventHandler::sSrcDB);
			PMString msg(KESCMDrawEventHandler::sSrcMarksOn ? "Source marks: on." : "Source marks: off.");
			msg.SetTranslatable(kFalse);
			KESCMSetStatus(msg);
			break;
		}

		// 「Hide Unchanged Spreads」トグル: OFF→ON は確認ダイアログ→変更なしスプレッドを隠す。
		// ON→OFF は自分が隠した分だけ再表示。本体は DoHideUnchangedToggle。
		case kKESCMPopupHideUnchangedActionID:
			this->DoHideUnchangedToggle();
			break;

		// 「Show Original Page Numbers」トグル: フラグを反転して再描画するだけ(バッジの表示判定と描画は
		// KESCMDrawEventHandler::HandleDrawEvent。表示は枠と同じ可視条件=印刷マークONの常時表示、または
		// ツール左hold中)。再描画は隠しの当事者になりやすい Target(sDB)と Source を対象にする(他の文書は
		// 次の自然な再描画で反映される)。
		case kKESCMPopupShowOldNumsActionID:
		{
			KESCMDrawEventHandler::sShowOldNumbers = !KESCMDrawEventHandler::sShowOldNumbers;
			KESCMInvalidateDB(KESCMDrawEventHandler::sDB);
			if (KESCMArmedSourceDB() != KESCMDrawEventHandler::sDB)
				KESCMInvalidateDB(KESCMArmedSourceDB());
			PMString msg(KESCMDrawEventHandler::sShowOldNumbers ? "Show original page numbers: on." : "Show original page numbers: off.");
			msg.SetTranslatable(kFalse);
			KESCMSetStatus(msg);
			break;
		}

		// 「Sync Layout Views」トグル: レイアウトビュー同期の ON/OFF(既定 ON)。実体は KESCMPeek.cpp の
		// KESCMSetLayoutSync(購読の付け外し+ON時は即時に一度そろえる)。作動条件は2モード(判定は
		// KESCMSyncOtherDocViewportsTo のガード):
		//   (A) Start 中: Target↔Source 間のみ・追加/削除補正あり(2026-07-11)。
		//   (B) Stop 中 + KESCM ツール選択中: アクティブ文書へ他の全文書を同期・補正なし(2026-07-15)。
		// トグル ON でも上記いずれの条件も満たさなければ(Stop かつツール非選択)購読はするが同期は no-op。
		case kKESCMPopupSyncViewsActionID:
		{
			KESCMSetLayoutSync(!KESCMGetLayoutSync());
			PMString msg;
			if (KESCMGetLayoutSync())
				msg = "Sync layout views: on.";
			else
				msg = "Sync layout views: off.";
			msg.SetTranslatable(kFalse);
			KESCMSetStatus(msg);
			break;
		}

		// 「Show Scrollbar Map」トグル: 文書窓の縦スクロールバー脇に変更位置の地図 strip を出すか(既定 ON)。
		// ON にしたら現在の比較対象(sDB/sSrcDB)へ即 attach して表示、OFF にしたら全窓から即 detach。
		// 未 arm(sDB=nil)で ON にした場合は attach が no-op=次の Start で自然に出る(フラグは ON のまま)。
		case kKESCMPopupScrollMapActionID:
		{
			const bool16 on = !KESCMGetScrollMapEnabled();
			KESCMSetScrollMapEnabled(on);
			if (on)
			{
				if (KESCMDrawEventHandler::sDB    != nil) KESCMScrollMapAttach(KESCMDrawEventHandler::sDB);
				if (KESCMDrawEventHandler::sSrcDB != nil) KESCMScrollMapAttach(KESCMDrawEventHandler::sSrcDB);
				KESCMScrollMapInvalidateAll();
			}
			else
				KESCMScrollMapDetachAll();	// 既存 strip を全窓から撤去
			PMString msg(on ? "Scrollbar map: on." : "Scrollbar map: off.");
			msg.SetTranslatable(kFalse);
			KESCMSetStatus(msg);
			break;
		}

		// 「Hold to Hide Marks」トグル: 枠表示の極性反転(フラグ反転のみ)。ON=画面に枠を常時表示し、
		// ツール左hold中だけ隠す(押下/解放は KESCMPeek.cpp のトラッカー(KESCMTrackerRevealBegin/End)が sMarksTempHidden を上下)。
		// OFF=従来(既定非表示・押下中だけ表示)。画面のみ=印刷は Print comparison marks が別管理。
		// 切替時に一時退避を解除し、常時表示の基準不透明度(常時表示ON中は25%/75%)を反映して sDB を再描画。
		case kKESCMPopupHoldToHideMarksActionID:
		{
			KESCMDrawEventHandler::sAlwaysShowMarks = !KESCMDrawEventHandler::sAlwaysShowMarks;
			KESCMDrawEventHandler::sMarksTempHidden = kFalse;	// モード切替時は一時退避を解除
			KESCMDrawEventHandler::sMarkScreenOpacity = KESCMBaseScreenOpacity();	// 常時表示の不透明度を即反映
			KESCMInvalidateDB(KESCMDrawEventHandler::sDB);
			PMString msg(KESCMDrawEventHandler::sAlwaysShowMarks ? "Hold to Hide Marks: on." : "Hold to Hide Marks: off.");
			msg.SetTranslatable(kFalse);
			KESCMSetStatus(msg);
			break;
		}

		// 「Ignore Page Number Marker」トグル: ノンブル(自動ページ番号)マーカーを含むフレームを
		// 比較(CMYKピクセル差分)から除外するか(既定ON)。フラグを反転し、既にStart済みなら
		// 登録トグルと同じ理由で全体再比較して即座に反映する。
		case kKESCMPopupIgnorePageNumActionID:
		{
			KESCMSetIgnorePageNumberMarker(!KESCMGetIgnorePageNumberMarker());
			PMString msg(KESCMGetIgnorePageNumberMarker() ? "Ignore page number marker: on." : "Ignore page number marker: off.");
			msg.SetTranslatable(kFalse);
			if (KESCMDrawEventHandler::sDB != nil && KESCMDrawEventHandler::sSrcDB != nil)
			{
				PMString report;
				KESCMDoMarkChangesDoc(KESCMDrawEventHandler::sDB, KESCMDrawEventHandler::sSrcDB, report);
				msg.Append(" (recompared)");
			}
			KESCMSetStatus(msg);
			break;
		}

		// フライアウトの「Save Panel Settings」: 現在の設定系トグルを独自 JSON でローカルへ保存し、
		// 保存先パスをダイアログ表示する(実体は KESCMPanelState.cpp)。読み込みはパネル初回オープン時。
		case kKESCMPopupSavePanelStateActionID:
			KESCMSavePanelState();
			break;

		// ページパネルのページ右クリック「KESCM: Register as Added/Removed Pages」トグル。
		// 選択ページを「比較相手なし」として登録/解除する(実体は KESCMPageMap.cpp。このステップでは
		// 登録の保持とチェック表示まで。比較の除外対応表への反映は次ステップ)。
		case kKESCMPageMapToggleActionID:
			KESCMPageMapToggleSelectedPages();
			break;

		// ページパネルのページ右クリック「KESCM: Check」トグル。選択ページに✓印を付け外しする
		// (実体は KESCMPageCheck.cpp。✓の描画は KESCMDrawEventHandler の isThumb 分岐)。
		case kKESCMPageCheckToggleActionID:
			KESCMPageCheckToggleSelectedPages();
			break;

		// ページパネルのページ右クリック「KESCM: Refresh Page Comparison」(実行アクション)。選択ページの
		// 比較を再検出して枠/サムネイルを更新する(旧 Ctrl+ミドルのスプレッド再比較を移設。2026-07-13)。
		// 実体は KESCMPeek.cpp。結果をステータス行に短く出す。
		case kKESCMPageRefreshCompareActionID:
		{
			int32 nPages = 0, nChanged = 0;
			if (KESCMRefreshComparisonForSelectedPages(&nPages, &nChanged))
			{
				PMString msg("refreshed ");
				msg.SetTranslatable(kFalse);
				msg.AppendNumber(nPages);
				msg.Append(" (changed ");
				msg.AppendNumber(nChanged);
				msg.Append(")");
				KESCMSetStatus(msg);
			}
			else
			{
				// 有効化判定(KESCMRefreshComparisonAvailable)は選択の中身まで見ないため、選択が空/全ページ
				// 未対応(Added/Removed 登録等)だと何も処理せず kFalse で戻る。その場合も無反応にせず
				// 「今回は何も再比較しなかった」ことをステータス行に出す(前回の refreshed 表示の残留による
				// 成功誤認を防ぐ。2026-07-15)。
				PMString msg("refresh: no comparable pages.");
				msg.SetTranslatable(kFalse);
				KESCMSetStatus(msg);
			}
			break;
		}

		// フライアウトの「Save Check & Register」: Start中の Target/Source の現在の Check(✓)+ Register
		// (Added/Removed)を独自 JSON(KESCM\KESCMPageChecks.json, v2)へマージ保存し、保存先パスをステータス行に
		// 出す(実体 KESCMPageCheck.cpp)。
		case kKESCMPopupSaveChecksActionID:
			KESCMPageCheckSaveToFile();
			break;

		// フライアウトの「Load Check & Register」: Start中だけ有効。上記 JSON から Register を両文書へ適用→再比較→
		// Check(今もマーク付きのページだけ)を復元する(実体 KESCMPageCheck.cpp)。
		case kKESCMPopupLoadChecksActionID:
			KESCMPageCheckLoadFromFile();
			break;

		default:
			break;
	}
}

/* UpdateActionStates — フライアウトのチェック式トグル5つ(「Show Marks on Source」「Hide Unchanged
   Spreads」「Show Original Page Numbers」「Sync Layout Views」「Ignore Page Number Marker」)は
   常に有効、ON なら kSelectedAction を
   立てる(docwatch の DocWchActionComponent::UpdateActionStates と同じ流儀)。ページパネル右クリックの
   「KESCM: Register as Added/Removed Pages」だけは選択依存の有効/無効・中間チェック・動的ラベルが
   あるため KESCMPageMapUpdateToggleState(KESCMPageMap.cpp)へ委譲する。 */
void KESCMActionComponent::UpdateActionStates(IActiveContext* /*ac*/, IActionStateList* listToUpdate, GSysPoint /*mousePoint*/, IPMUnknown* /*widget*/)
{
	for (int32 i = 0; i < listToUpdate->Length(); i++)
	{
		const ActionID action = listToUpdate->GetNthAction(i);
		if (action == kKESCMPopupStartStopActionID)
		{
			// arm 状態でメニュー名を出し分け(arm 中=Stop / 未 arm=Start)。常に有効。
			// (kSelectedAction は付けない=チェックマークではなく名前そのものを切り替える。)
			const bool16 armed = KESCMIsArmed() && (KESCMArmedTargetDB() != nil);
			PMString name(armed ? "Stop" : "Start");
			name.SetTranslatable(kFalse);
			listToUpdate->SetNthActionName(i, name);
			listToUpdate->SetNthActionState(i, kEnabledAction);
		}
		else if (action == kKESCMPopupPrintMarksActionID)
		{
			int16 actionState = kEnabledAction;
			if (KESCMGetPrintMarks())
				actionState |= kSelectedAction;	// ON ならチェックマーク
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupOpacity25ActionID)
		{
			// ラジオ風: 現在 25% ならこの項目に✓(75% と相互排他)。
			int16 actionState = kEnabledAction;
			if (KESCMGetMarkOpacity25())
				actionState |= kSelectedAction;
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupOpacity75ActionID)
		{
			// ラジオ風: 現在 75%(=!25%)ならこの項目に✓。
			int16 actionState = kEnabledAction;
			if (!KESCMGetMarkOpacity25())
				actionState |= kSelectedAction;
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupHideUnchangedActionID)
		{
			int16 actionState = kEnabledAction;
			if (sHideUnchangedOn)
				actionState |= kSelectedAction;
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupShowOldNumsActionID)
		{
			int16 actionState = kEnabledAction;
			if (KESCMDrawEventHandler::sShowOldNumbers)
				actionState |= kSelectedAction;
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupSyncViewsActionID)
		{
			int16 actionState = kEnabledAction;
			if (KESCMGetLayoutSync())
				actionState |= kSelectedAction;
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupScrollMapActionID)
		{
			int16 actionState = kEnabledAction;
			if (KESCMGetScrollMapEnabled())
				actionState |= kSelectedAction;	// ON(既定)ならチェックマーク
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupHoldToHideMarksActionID)
		{
			int16 actionState = kEnabledAction;
			if (KESCMDrawEventHandler::sAlwaysShowMarks)
				actionState |= kSelectedAction;	// ON ならチェックマーク
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupShowSrcMarksActionID)
		{
			int16 actionState = kEnabledAction;
			if (KESCMDrawEventHandler::sSrcMarksOn)
				actionState |= kSelectedAction;
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupIgnorePageNumActionID)
		{
			int16 actionState = kEnabledAction;
			if (KESCMGetIgnorePageNumberMarker())
				actionState |= kSelectedAction;
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPageMapToggleActionID)
		{
			// ページパネル右クリックの登録トグル: 有効/無効(選択の有無)・チェック(全登録=✓/
			// 一部=中間)・ラベル(Target=Added/Source=Removed)をまとめて KESCMPageMap.cpp 側で設定。
			KESCMPageMapUpdateToggleState(listToUpdate, i);
		}
		else if (action == kKESCMPageCheckToggleActionID)
		{
			// ページパネル右クリックの「KESCM: Check」トグル: 有効/無効(Start中+Target/Source+選択)と
			// チェック(全部✓/一部=中間)を KESCMPageCheck.cpp 側で設定。
			KESCMPageCheckUpdateToggleState(listToUpdate, i);
		}
		else if (action == kKESCMPageRefreshCompareActionID)
		{
			// ページパネル右クリックの「KESCM: Refresh Page Comparison」(トグルではない実行アクション):
			// Start中(arm済み)かつ前面文書が Target/Source のときだけ有効化。それ以外はグレーアウト。
			listToUpdate->SetNthActionState(i, KESCMRefreshComparisonAvailable() ? kEnabledAction : kDisabled_Unselected);
		}
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

/* DoUsage — パネルのフライアウト「使い方」。操作リファレンス(=旧パネルの説明文)を表示する。 */
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

//========================================================================================
// Hide Unchanged Spreads(フライアウトのチェック式トグル)
//========================================================================================

// 状態メッセージをパネルのステータス行へ(既存の Split Target と同じ英語・非翻訳の流儀)。
static void KESCMHideStatus(const char* text)
{
	PMString msg(text);
	msg.SetTranslatable(kFalse);
	KESCMSetStatus(msg);
}

// uids を1つの kHideSpreadCmdBoss で隠す/再表示する。hide=kTrue で隠す。
// ★IBoolData の方向は kTrue=隠す(2026-07-04 実機確認済み。kLockLayerCmdBoss と同型)。
static ErrorCode KESCMProcessHideSpreadCmd(IDataBase* db, const std::vector<UID>& uids, bool16 hide)
{
	if (db == nil || uids.empty())
		return kFailure;

	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kHideSpreadCmdBoss));
	if (cmd == nil)
		return kFailure;

	UIDList list(db);
	for (size_t i = 0; i < uids.size(); ++i)
		list.Append(uids[i]);
	cmd->SetItemList(list);

	InterfacePtr<IBoolData> data(cmd, IID_IBOOLDATA);
	if (data == nil)
		return kFailure;
	data->Set(hide);

	return CmdUtils::ProcessCommand(cmd);
}

/* DoHideUnchangedToggle — フライアウト「Hide Unchanged Spreads」。
   OFF→ON: 確認ダイアログ(Yes/No、ロケール連動文言)→ Yes なら、比較マーク(sEntries)が1ページも
   無いスプレッドを集めて kHideSpreadCmdBoss で一括で隠し、UID を控えてチェック ON。続けて Source 側も
   同じ分類(平坦ページ番号対応)で自動的に隠す(両文書とも dirty になる)。
   ON→OFF: 控えた分だけ両文書とも再表示(確認なし)。
   ガード: 比較マークが無い(Start 前/変更ゼロ)なら何もしない。特に「変更のあったスプレッドが1つも
   ない」場合は全スプレッドを隠すことになり、InDesign は全スプレッド非表示を許さないため中止する
   (Source 側のみ全対象になった場合は Source 側だけスキップ)。 */
void KESCMActionComponent::DoHideUnchangedToggle()
{
	// ON→OFF: 自分が隠した分だけ再表示して状態を捨てる。
	if (sHideUnchangedOn)
	{
		KESCMResetHideUnchanged(kTrue);
		KESCMScrollMapInvalidateAll();	// スクロールバー地図を再表示後の配置で描き直す(2026-07-11)
		KESCMHideStatus("Hide Unchanged: hidden spreads restored.");
		return;
	}

	// OFF→ON。
	IDataBase* db = KESCMDrawEventHandler::sDB;
	if (db == nil)
	{
		// Start 前。
		KESCMHideStatus("Hide Unchanged: Start first.");
		return;
	}

	// ★「/」が付く overflow ページ(登録されていないのに、文書間のページ数差で比較相手が無い=未比較の
	//   ページ)を含むスプレッドは、変更ありページや登録済み("Added")ページと同じく隠さない
	//   (未比較の見落としを防ぐ。ユーザー要望 2026-07-06)。ここで Target 側の overflow 集合を作る
	//   (Source 側は下の分類が対応表外ページを既に「変更あり」扱いにしているので隠れない)。
	std::set<UID> tOverflowSet;
	{
		IDataBase* srcForOverflow = KESCMArmedSourceDB();
		if (srcForOverflow != nil && srcForOverflow != db)
		{
			std::vector<UID> ovT, ovS, tOverflow, sOverflow;
			KESCMBuildPairing(db, srcForOverflow, ovT, ovS, &tOverflow, &sOverflow);
			tOverflowSet.insert(tOverflow.begin(), tOverflow.end());
		}
	}

	// sEntries が空でも、登録済み(比較相手なし="Added")ページや overflow ページがあれば続行する
	// (それら自体が「変更あり=残す」扱いになるため)。全部無ければ全スプレッドが対象になり、
	// 全スプレッド非表示は InDesign が許さないので中止する。
	if (KESCMDrawEventHandler::sEntries.empty() && !KESCMPageMapHasAnyRegistered(db) && tOverflowSet.empty())
	{
		KESCMHideStatus("Hide Unchanged: no changes to hide.");
		return;
	}

	// 変更なしスプレッド = 所属ページが1つも sEntries に載っていないスプレッド。
	InterfacePtr<ISpreadList> spreadList(db, db->GetRootUID(), UseDefaultIID());
	if (spreadList == nil)
	{
		KESCMHideStatus("Hide Unchanged: spread list not available.");
		return;
	}
	std::vector<UID> unchanged;
	int32 visibleCount = 0;		// 現在表示中(=隠れていない)のスプレッド数。全スプレッド非表示ガードの分母
	const int32 ns = spreadList->GetSpreadCount();
	for (int32 s = 0; s < ns; ++s)
	{
		const UID spreadUID = spreadList->GetNthSpreadUID(s);
		InterfacePtr<ISpread> spread(db, spreadUID, UseDefaultIID());
		if (spread == nil)
			continue;
		// 既に隠れているスプレッド(ユーザーがページパネル等で隠した分)は対象にしない。
		// 自分のリストに入れると ON→OFF でユーザーが隠した分まで再表示してしまうため。
		// 隠し状態は kSpreadBoss 上の IBoolData(IID_IHIDESPREADBOOLDATA、kTrue=隠し中)で読む。
		InterfacePtr<IBoolData> hideFlag(db, spreadUID, IID_IHIDESPREADBOOLDATA);
		if (hideFlag != nil && hideFlag->GetBool())
			continue;
		++visibleCount;
		bool16 changed = kFalse;
		const int32 np = spread->GetNumPages();
		for (int32 p = 0; p < np; ++p)
		{
			const UID pageUID = spread->GetNthPageUID(p);
			// 登録済み(比較相手なし="Added")ページは比較対象外で sEntries には載らないが、緑枠つきの
			// 「変更あり」ページとして扱う。overflow ページ("/"、文書間のページ数差で未比較)も同様に
			// 「変更あり=残す」扱いにして、隠さない(誤って未比較ページを隠すのを防ぐ)。
			if (KESCMDrawEventHandler::sEntries.count(pageUID) > 0 ||
			    KESCMPageMapIsRegistered(db, pageUID) ||
			    tOverflowSet.count(pageUID) > 0)
			{
				changed = kTrue;
				break;
			}
		}
		if (!changed)
			unchanged.push_back(spreadUID);
	}

	if (unchanged.empty())
	{
		KESCMHideStatus("Hide Unchanged: all changed; none to hide.");
		return;
	}
	if ((int32)unchanged.size() >= visibleCount)
	{
		// 保険(sEntries が非空なら通常ここへは来ない): 表示中スプレッドを全部隠すことになる場合は中止
		// (InDesign は全スプレッド非表示を許さない。分母は「現在表示中」の数=手動で隠し済みの分は除く)。
		KESCMHideStatus("Hide Unchanged: can't hide all spreads.");
		return;
	}

	// 確認ダイアログ(kHideSpreadCmdBoss は永続変更=文書が dirty になり、隠し状態は保存ファイルにも残る)。
	// 文言はロケール連動(enUS=英語/jaJP=日本語)。ボタンは Windows の制約で標準 Yes/No のみ。
	const int16 clicked = CAlert::ModalAlert
	(
		kKESCMHideConfirmKey,		// "This feature modifies the document file. Continue?" / 日本語版
		kYesString,
		kNoString,
		kNullString,
		1,							// Yes を既定ボタンに
		CAlert::eWarningIcon
	);
	if (clicked != 1)
		return;						// No: チェックも付けず何もしない

	const ErrorCode err = KESCMProcessHideSpreadCmd(db, unchanged, kTrue /*hide*/);
	if (err != kSuccess)
	{
		KESCMHideStatus("Hide Unchanged: hide command failed.");
		return;
	}

	sHideUnchangedOn = kTrue;
	sHiddenDB = db;
	sHiddenSpreads = unchanged;

	// ---- Source 側も同じ分類で自動的に隠す ----
	// 「変更あり」を除外対応表(登録済み=比較相手なしページを除いた順番対応)経由で Source ページの
	// 集合にし、Source のスプレッドを走査して、変更ありページに対応するページを1つも含まない
	// スプレッドを隠す。対応表に無い Source ページ(登録済み=削除ページ扱い)は安全側で「変更あり」
	// 扱いにする(縁枠合成(ステップ3)が入るまでの暫定方針)。
	// Source 側が失敗/スキップでも Target 側の隠しはそのまま生かす(致命ではないため)。
	IDataBase* srcDB = KESCMArmedSourceDB();
	int32 srcHiddenCount = 0;
	bool16 srcSkippedAll = kFalse;
	if (srcDB != nil && srcDB != db)
	{
		std::vector<UID> tPages, sPages;
		KESCMBuildPairing(db, srcDB, tPages, sPages);
		std::map<UID, bool16> srcChangedMap;	// 対応表にあるSourceページ→対応Targetページが変更ありか
		for (size_t i = 0; i < tPages.size(); ++i)
			srcChangedMap[sPages[i]] = (KESCMDrawEventHandler::sEntries.count(tPages[i]) > 0) ? kTrue : kFalse;

		InterfacePtr<ISpreadList> srcSpreadList(srcDB, srcDB->GetRootUID(), UseDefaultIID());
		if (srcSpreadList != nil)
		{
			std::vector<UID> srcUnchanged;
			int32 srcVisibleCount = 0;	// Source 側の全スプレッド非表示ガードの分母(表示中のみ)
			const int32 nss = srcSpreadList->GetSpreadCount();
			for (int32 s = 0; s < nss; ++s)
			{
				const UID srcSpreadUID = srcSpreadList->GetNthSpreadUID(s);
				InterfacePtr<ISpread> srcSpread(srcDB, srcSpreadUID, UseDefaultIID());
				if (srcSpread == nil)
					continue;
				const int32 np = srcSpread->GetNumPages();
				// 手動で隠し済みの Source スプレッドは巻き込まない(Target 側と同じ方針)。
				InterfacePtr<IBoolData> srcHideFlag(srcDB, srcSpreadUID, IID_IHIDESPREADBOOLDATA);
				if (srcHideFlag != nil && srcHideFlag->GetBool())
					continue;
				++srcVisibleCount;
				bool16 srcChanged = kFalse;
				for (int32 p = 0; p < np; ++p)
				{
					const UID srcPageUID = srcSpread->GetNthPageUID(p);
					std::map<UID, bool16>::const_iterator mit = srcChangedMap.find(srcPageUID);
					if (mit == srcChangedMap.end() || mit->second)
					{
						srcChanged = kTrue;
						break;
					}
				}
				if (!srcChanged)
					srcUnchanged.push_back(srcSpreadUID);
			}

			if (!srcUnchanged.empty())
			{
				if ((int32)srcUnchanged.size() >= srcVisibleCount)
				{
					// 全スプレッド非表示は不可(例: 変更が Target の追加ページに集中していて、対応する
					// Source ページが存在しない場合は全 Source スプレッドが「変更なし」になり得る)。
					// その場合は Source 側だけスキップし、Target 側の隠しは生かす。
					srcSkippedAll = kTrue;
				}
				else if (KESCMProcessHideSpreadCmd(srcDB, srcUnchanged, kTrue /*hide*/) == kSuccess)
				{
					sHiddenSrcDB = srcDB;
					sHiddenSrcSpreads = srcUnchanged;
					srcHiddenCount = (int32)srcUnchanged.size();
				}
			}
		}
	}

	PMString msg("Hide Unchanged: hid ");
	msg.SetTranslatable(kFalse);
	msg.AppendNumber((int32)unchanged.size());
	msg.Append(" target spread(s)");
	if (srcHiddenCount > 0)
	{
		msg.Append(" + ");
		msg.AppendNumber(srcHiddenCount);
		msg.Append(" source spread(s)");
	}
	msg.Append(".");
	if (srcSkippedAll)
		msg.Append(" Source not hidden (would hide all its spreads).");
	KESCMSetStatus(msg);

	// スクロールバー地図を隠し後の配置で描き直す(隠しスプレッドは地図から除外される。Target/Source 両窓)。
	KESCMScrollMapInvalidateAll();
}

// 文書の生存確認は共有ヘルパ KESCMIsDocDBOpen(KESCMCore.h)を使う(旧ここ static、2026-07-10 共有化)。

// 片側(db+控えリスト)の再表示と状態破棄。restore=kTrue かつ db が生存している場合のみ再表示コマンドを
// 打つ(途中で削除されたスプレッドは ISpread クエリが nil になるのでスキップ)。db が閉じていれば
// deref せず黙って状態だけ捨てる。db は参照渡しで nil に戻す。
static void KESCMRestoreHiddenList(IDataBase*& db, std::vector<UID>& list, bool16 restore)
{
	if (restore && db != nil && !list.empty() && KESCMIsDocDBOpen(db))
	{
		std::vector<UID> alive;
		for (size_t i = 0; i < list.size(); ++i)
		{
			InterfacePtr<ISpread> spread(db, list[i], UseDefaultIID());
			if (spread != nil)
				alive.push_back(list[i]);
		}
		if (!alive.empty())
			KESCMProcessHideSpreadCmd(db, alive, kFalse /*unhide*/);
	}
	list.clear();
	db = nil;
}

// KESCMResetHideUnchanged(KESCMCore.h で宣言) — トグル状態のリセット(Target/Source 両側)。
//   restoreSpreads=kTrue: 控えているスプレッドを再表示してから状態を捨てる(再比較/Stop/手動 OFF/
//     クローズスイープ)。文書の生存は内部で確認するので、片方が閉じていても安全(生存側のみ再表示)。
//   restoreSpreads=kFalse: db に一切触れず状態だけ捨てる。
void KESCMResetHideUnchanged(bool16 restoreSpreads)
{
	KESCMRestoreHiddenList(sHiddenDB, sHiddenSpreads, restoreSpreads);
	KESCMRestoreHiddenList(sHiddenSrcDB, sHiddenSrcSpreads, restoreSpreads);
	sHideUnchangedOn = kFalse;
}

IDataBase* KESCMGetHideUnchangedDB()
{
	return sHiddenDB;
}

IDataBase* KESCMGetHideUnchangedSrcDB()
{
	return sHiddenSrcDB;
}

// (KESCMDoSplitTarget(Split Target 90/10)は 2026-07-04 撤去。実装全文と実測知見は
//  docs/ai-notes/kescm-split-target-mechanism.md と git 履歴 69c4b07 に保存=他プラグインへの転用候補)

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
