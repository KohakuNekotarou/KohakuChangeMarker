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

#include "IDataBase.h"				// GetRootUID()(Hide Unchanged のスプレッド走査)

// (Split Target(90/10)機能は 2026-07-04 撤去。専用 include 群も削除。
//  仕組みは docs/ai-notes/kescm-split-target-mechanism.md と git 履歴 69c4b07 に保存)

// プロジェクト内:
#include "KESCMID.h"
#include "KESCMCore.h"		// KESCMOpenAboutURL
#include "KESCMDrawEventHandler.h"	// sEntries/sDB/sShowOldNumbers(Hide Unchanged と旧番号バッジの状態参照)

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
		// ミドル押下中)。再描画は隠しの当事者になりやすい Target(sDB)と Source を対象にする(他の文書は
		// 次の自然な再描画で反映される)。
		case kKESCMPopupShowOldNumsActionID:
			KESCMDrawEventHandler::sShowOldNumbers = !KESCMDrawEventHandler::sShowOldNumbers;
			KESCMInvalidateDB(KESCMDrawEventHandler::sDB);
			if (KESCMArmedSourceDB() != KESCMDrawEventHandler::sDB)
				KESCMInvalidateDB(KESCMArmedSourceDB());
			break;

		// 「Sync Layout Views」トグル: レイアウトビュー同期の ON/OFF。実体は KESCMPeek.cpp の
		// KESCMSetLayoutSync(購読の付け外し+ON時は即時に一度そろえる)。Start(枠)とは無関係に使える。
		case kKESCMPopupSyncViewsActionID:
			KESCMSetLayoutSync(!KESCMGetLayoutSync());
			break;

		default:
			break;
	}
}

/* UpdateActionStates — チェック式トグル4つ(「Show Marks on Source」「Hide Unchanged Spreads」
   「Show Original Page Numbers」「Sync Layout Views」)のチェックマーク。常に有効、ON なら
   kSelectedAction を立てる(docwatch の DocWchActionComponent::UpdateActionStates と同じ流儀)。 */
void KESCMActionComponent::UpdateActionStates(IActiveContext* /*ac*/, IActionStateList* listToUpdate, GSysPoint /*mousePoint*/, IPMUnknown* /*widget*/)
{
	for (int32 i = 0; i < listToUpdate->Length(); i++)
	{
		const ActionID action = listToUpdate->GetNthAction(i);
		if (action == kKESCMPopupHideUnchangedActionID)
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
		else if (action == kKESCMPopupShowSrcMarksActionID)
		{
			int16 actionState = kEnabledAction;
			if (KESCMDrawEventHandler::sSrcMarksOn)
				actionState |= kSelectedAction;
			listToUpdate->SetNthActionState(i, actionState);
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
		KESCMHideStatus("Hide Unchanged: hidden spreads restored.");
		return;
	}

	// OFF→ON。
	IDataBase* db = KESCMDrawEventHandler::sDB;
	if (db == nil || KESCMDrawEventHandler::sEntries.empty())
	{
		// Start 前、または比較結果「変更ゼロ」。後者は全スプレッドが対象になってしまい、
		// 全スプレッド非表示は InDesign が許さないので、どちらもここで中止する。
		KESCMHideStatus("Hide Unchanged: no changed spreads to keep visible (press Start first; if no changes were found, nothing can be hidden).");
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
			if (KESCMDrawEventHandler::sEntries.count(spread->GetNthPageUID(p)) > 0)
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
		KESCMHideStatus("Hide Unchanged: every spread has changes; nothing to hide.");
		return;
	}
	if ((int32)unchanged.size() >= visibleCount)
	{
		// 保険(sEntries が非空なら通常ここへは来ない): 表示中スプレッドを全部隠すことになる場合は中止
		// (InDesign は全スプレッド非表示を許さない。分母は「現在表示中」の数=手動で隠し済みの分は除く)。
		KESCMHideStatus("Hide Unchanged: no changed spreads to keep visible; cannot hide all spreads.");
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
	// 「変更あり」を Target の平坦ページ番号の集合にし、Source のスプレッドを平坦番号で走査して、
	// 変更ありページに対応するページを1つも含まないスプレッドを隠す。新旧で見開き構成(スプレッドの
	// ページ配分)が違っても、比較自体が平坦ページ番号どうしの対応なので、この判定も同じ基準で崩れない。
	// Source 側が失敗/スキップでも Target 側の隠しはそのまま生かす(致命ではないため)。
	IDataBase* srcDB = KESCMArmedSourceDB();
	int32 srcHiddenCount = 0;
	bool16 srcSkippedAll = kFalse;
	if (srcDB != nil && srcDB != db)
	{
		std::vector<UID> tFlat;
		KESCMCollectPageUIDs(db, tFlat);
		std::vector<bool> changedIdx(tFlat.size(), false);
		for (size_t i = 0; i < tFlat.size(); ++i)
		{
			if (KESCMDrawEventHandler::sEntries.count(tFlat[i]) > 0)
				changedIdx[i] = true;
		}

		InterfacePtr<ISpreadList> srcSpreadList(srcDB, srcDB->GetRootUID(), UseDefaultIID());
		if (srcSpreadList != nil)
		{
			std::vector<UID> srcUnchanged;
			int32 srcVisibleCount = 0;	// Source 側の全スプレッド非表示ガードの分母(表示中のみ)
			int32 flat = 0;				// Source の平坦ページ番号(隠し済みスプレッドのページも数える)
			const int32 nss = srcSpreadList->GetSpreadCount();
			for (int32 s = 0; s < nss; ++s)
			{
				const UID srcSpreadUID = srcSpreadList->GetNthSpreadUID(s);
				InterfacePtr<ISpread> srcSpread(srcDB, srcSpreadUID, UseDefaultIID());
				if (srcSpread == nil)
					continue;
				const int32 np = srcSpread->GetNumPages();
				// 手動で隠し済みの Source スプレッドは巻き込まない(Target 側と同じ方針)。
				// ページ数の加算は続ける=平坦番号を崩さない。
				InterfacePtr<IBoolData> srcHideFlag(srcDB, srcSpreadUID, IID_IHIDESPREADBOOLDATA);
				if (srcHideFlag != nil && srcHideFlag->GetBool())
				{
					flat += np;
					continue;
				}
				++srcVisibleCount;
				bool16 srcChanged = kFalse;
				for (int32 p = 0; p < np; ++p)
				{
					const size_t gi = (size_t)(flat + p);
					if (gi < changedIdx.size() && changedIdx[gi])
					{
						srcChanged = kTrue;
						break;
					}
				}
				if (!srcChanged)
					srcUnchanged.push_back(srcSpreadUID);
				flat += np;
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
}

// 文書の生存確認(閉じた db は deref 禁止。IDocumentList へのポインタ比較のみで判定する。
// KESCMHandleDocsClosed の生存スイープと同じ流儀)。
static bool16 KESCMIsDocDBOpen(IDataBase* db)
{
	if (db == nil)
		return kFalse;
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	return (docList != nil && docList->FindDocByDataBase(db) != nil);
}

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
