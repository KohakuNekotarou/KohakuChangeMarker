//========================================================================================
//
//  KESCMHideUnchanged.cpp
//
//  「Hide Unchanged Spreads」の実装(KESCMActionComponent.cpp から分離。2026-08-13 の
//  model/UI 分割 第1段 Task 2)。変更マークの無いスプレッドを kHideSpreadCmdBoss で隠し、
//  自分が隠した分「だけ」を控えて元に戻す。Target/Source の両側を同じ分類で扱う。
//
//  ★分離では関数の中身を1行も変えていない。変えたのは「どのファイルに座るか」と「誰から見えるか」だけ。
//    トグル本体は元 KESCMActionComponent::DoHideUnchangedToggle で、自由関数
//    KESCMHideUnchangedToggle になった(呼び手は DoAction の case の1行)。★本体ごと移したのは、
//    本体が書く5本の static(トグル・両側の IDataBase* と UID 控え)を KESCMResetHideUnchanged が
//    消すため——本体を UI 側に残すと、同じ状態が分割の両側に割れる。
//
//  model 側: kHideSpreadCmdBoss を出す=文書を変える(両文書が dirty になる)。
//  ⚠この時点では確認アラート(CAlert)がまだこのファイルに居る=第2段の宿題。ステータス行は Task 9 で
//    相手にするのは Task 7/9(ステータス行は通知へ反転する)。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// 一般:
#include "CAlert.h"
#include "PMString.h"

// Hide Unchanged Spreads(kHideSpreadCmdBoss)用:
#include "CmdUtils.h"				// CreateCommand/ProcessCommand(モデル変更は必ず Command 経由)
#include "ICommand.h"
#include "IBoolData.h"				// kHideSpreadCmdBoss は専用 CmdData を持たず汎用 IBoolData で方向指定(kLockLayerCmdBoss と同型)
#include "UIDList.h"
#include "ISpread.h"
#include "ISpreadList.h"
#include "SpreadID.h"				// kHideSpreadCmdBoss / IID_IHIDESPREADBOOLDATA
#include "IDataBase.h"				// GetRootUID()(Hide Unchanged のスプレッド走査)
#include <vector>
#include <map>
#include <set>

// プロジェクト内:
#include "KESCMID.h"
#include "KESCMLoc.h"		// 実行時の日本語切替(文書を変える前の確認アラート)
#include "KESCMHideUnchanged.h"
#include "KESCMCore.h"		// KESCMIsDocDBOpen / KESCMArmedSourceDB
#include "KESCMModelNotify.h"	// KESCMNotifyStatus - the model tells the UI, it never calls it (Task 9)
#include "KESCMDrawEventHandler.h"	// sDB/sEntries(「変更あり」の判定材料)
#include "KESCMPageMap.h"	// KESCMBuildPairing(除外対応表、Source 側の分類で使用)
							// ＋ KESCMPageMapIsRegistered / KESCMPageMapHasAnyRegistered
#include "KESCMScrollMap.h"	// KESCMScrollMapInvalidateAll(隠し/戻しの後に地図を描き直す)

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

//========================================================================================
// Hide Unchanged Spreads(フライアウトのチェック式トグル)
//========================================================================================

// 状態メッセージをパネルのステータス行へ(既存の Split Target と同じ英語・非翻訳の流儀)。
static void KESCMHideStatus(const char* text)
{
	PMString msg(text);
	msg.SetTranslatable(kFalse);
	KESCMNotifyStatus(msg);
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

/* KESCMHideUnchangedToggle(KESCMHideUnchanged.h で宣言) — フライアウト「Hide Unchanged Spreads」。
   (2026-08-13 に KESCMActionComponent::DoHideUnchangedToggle から移動。中身は無変更。)
   OFF→ON: 確認ダイアログ(Yes/No、ロケール連動文言)→ Yes なら、比較マーク(sEntries)が1ページも
   無いスプレッドを集めて kHideSpreadCmdBoss で一括で隠し、UID を控えてチェック ON。続けて Source 側も
   同じ分類(平坦ページ番号対応)で自動的に隠す(両文書とも dirty になる)。
   ON→OFF: 控えた分だけ両文書とも再表示(確認なし)。
   ガード: 比較マークが無い(Start 前/変更ゼロ)なら何もしない。特に「変更のあったスプレッドが1つも
   ない」場合は全スプレッドを隠すことになり、InDesign は全スプレッド非表示を許さないため中止する
   (Source 側のみ全対象になった場合は Source 側だけスキップ)。 */
void KESCMHideUnchangedToggle()
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
	// ★対応表(tPages/sPages)は下の「Source 側も隠す」でもそのまま使う。以前はここと下で
	//   KESCMBuildPairing を2回呼び、同じ表を2度作っていた(2026-08-06 監査 C-2 で1回に統合)。
	//   対応表の構築はページ数に比例するので、大きい文書ほど無駄が効いていた。
	IDataBase* const srcDB = KESCMArmedSourceDB();
	const bool16 hasSource = (srcDB != nil && srcDB != db);
	std::vector<UID> tPages, sPages, tOverflow, sOverflow;
	if (hasSource)
		KESCMBuildPairing(db, srcDB, tPages, sPages, &tOverflow, &sOverflow);
	const std::set<UID> tOverflowSet(tOverflow.begin(), tOverflow.end());

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
		// "This feature modifies the document file. Continue?" / 日本語 UI では日本語(KESCMLoc)。
		// ★文書を変更する前の確認なので、意味を取り違えられないよう日本語 UI では日本語で出す
		//   (2026-08-06 ユーザー指示)。
		KESCMLoc::Text(kKESCMHideConfirmKey, KESCMJa::kHideConfirm),
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
	int32 srcHiddenCount = 0;
	bool16 srcSkippedAll = kFalse;
	if (hasSource)
	{
		// 対応表(tPages/sPages)は関数先頭で1回だけ作ったものを使う(2026-08-06 監査 C-2)。
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
	KESCMNotifyStatus(msg);

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

// KESCMResetHideUnchanged(KESCMHideUnchanged.h で宣言) — トグル状態のリセット(Target/Source 両側)。
//   restoreSpreads=kTrue: 控えているスプレッドを再表示してから状態を捨てる(再比較/Stop/手動 OFF/
//     クローズスイープ)。文書の生存は内部で確認するので、片方が閉じていても安全(生存側のみ再表示)。
//   restoreSpreads=kFalse: db に一切触れず状態だけ捨てる。
void KESCMResetHideUnchanged(bool16 restoreSpreads)
{
	KESCMRestoreHiddenList(sHiddenDB, sHiddenSpreads, restoreSpreads);
	KESCMRestoreHiddenList(sHiddenSrcDB, sHiddenSrcSpreads, restoreSpreads);
	sHideUnchangedOn = kFalse;
}

// KESCMGetHideUnchangedOn(KESCMHideUnchanged.h で宣言) — メニューのチェックマーク用。
// (2026-08-13 新設。トグルの旗はこのファイルに閉じ、UI 側の UpdateActionStates はここへ聞く。)
bool16 KESCMGetHideUnchangedOn()
{
	return sHideUnchangedOn;
}

IDataBase* KESCMGetHideUnchangedDB()
{
	return sHiddenDB;
}

IDataBase* KESCMGetHideUnchangedSrcDB()
{
	return sHiddenSrcDB;
}

// KESCMHideUnchanged.cpp 終わり。
