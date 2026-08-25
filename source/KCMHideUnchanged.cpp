//========================================================================================
//
//  KCMHideUnchanged.cpp
//
//  「Hide Unchanged Spreads」の実装(KCMActionComponent.cpp から分離。2026-08-13 の
//  model/UI 分割 第1段 Task 2)。変更マークの無いスプレッドを kHideSpreadCmdBoss で隠し、
//  自分が隠した分「だけ」を控えて元に戻す。Target/Source の両側を同じ分類で扱う。
//
//  ★★2026-08-18(不具合再検査 B10): 「変更マークの無い」を決める材料を、**全部 描画が見ているものと
//    同じ「比較した時点」の控え**に揃えた ---- あふれ集合は sOverflowT(キャッシュ)、Source 側の
//    除外対応表は sPrevPairTargetToSource(前回比較のペアリング)。以前は KCMBuildPairing を呼び直して
//    **今の**文書構成から計算しており、Start の後にページを足す/消して再比較していないと、
//    **画面の「/」と、隠す/隠さないの判定が食い違っていた。**
//
//  ★分離では関数の中身を1行も変えていない。変えたのは「どのファイルに座るか」と「誰から見えるか」だけ。
//    トグル本体は元 KCMActionComponent::DoHideUnchangedToggle で、自由関数
//    KCMHideUnchangedToggle になった(呼び手は DoAction の case の1行)。★本体ごと移したのは、
//    本体が書く5本の static(トグル・両側の IDataBase* と UID 控え)を KCMResetHideUnchanged が
//    消すため——本体を UI 側に残すと、同じ状態が分割の両側に割れる。
//
//  model 側: kHideSpreadCmdBoss を出す=文書を変える(両文書が dirty になる)。★Target と Source の2本は
//  1つの CmdUtils::SequenceContext に入れて打つ=Ctrl+Z 一回で両方が戻る(2026-08-16 の API 監査 B10)。
//  ⚠旧記述「この時点では確認アラート(CAlert)がまだこのファイルに居る=第2段の宿題／ステータス行は
//    Task 9 で通知へ反転する」は 2026-08-16 に撤回=どちらも決着済み。CAlert は**残す判断**(根拠は
//    KCMHideUnchangedToggle の中のコメント)、ステータス行は既に KCMNotifyStatus 経由で UI へ渡す。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// 一般:
#include "CAlert.h"
#include "PMString.h"

// Hide Unchanged Spreads(kHideSpreadCmdBoss)用:
#include "CmdUtils.h"				// CreateCommand/ProcessCommand(モデル変更は必ず Command 経由)/SequenceContext
#include "ICommand.h"
#include "ErrorUtils.h"				// GlobalErrorStatePreserver - 隠す/戻すは失敗しうるので、立てたエラーをここから外へ出さない
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
#include "KCMID.h"
#include "KCMLoc.h"		// 実行時の日本語切替(文書を変える前の確認アラート)
#include "KCMHideUnchanged.h"
#include "KCMCore.h"		// KCMIsDocDBOpen / KCMArmedSourceDB
#include "KCMModelNotify.h"	// KCMNotifyStatus - the model tells the UI, it never calls it (Task 9)
#include "KCMDrawEventHandler.h"	// sDB / sEntries / sOverflowT / sPrevPairTargetToSource(「変更あり」の判定材料)
							// ★★どれも「比較した時点」の控え＝**画面・サムネイル・地図が見ているものと同じ**。
							//   隠す/隠さないの判定を画面と揃えるための選択(2026-08-18 の不具合再検査 B10)。
#include "KCMPageMap.h"	// KCMPageMapIsRegistered / KCMPageMapHasAnyRegistered
							// (★KCMBuildPairing の呼びは B10 で無くなった＝あふれ集合も除外対応表も
							//  上の控えから読む。理由は KCMHideUnchangedToggle の中のコメント)
#include "KCMID.h"		// kKCMPageFlagsChangedMessage(通知の ID)
// ★2026-08-13(Task 10): UI 側ヘッダー KCMScrollMap.h の include を落とした。隠し/戻しの後に地図を
//   描き直すのは、通知を受けた UI の仕事。

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

// 戻し(再表示)の本体。公開関数 KCMResetHideUnchanged は void で、境界の Facade もそう宣言している
// ので、「再表示コマンドが何本失敗したか」はこの内部版だけが返す。トグルの ON→OFF はこれを直に呼ぶ
// ＝戻せなかったときにステータス行で「restored」と嘘をつかないため(2026-08-16 の API 監査 B10)。
static int32 KCMResetHideUnchangedCore(bool16 restoreSpreads);

//========================================================================================
// Hide Unchanged Spreads(フライアウトのチェック式トグル)
//========================================================================================

// 状態メッセージをパネルのステータス行へ(既存の Split Target と同じ英語・非翻訳の流儀)。
static void KCMHideStatus(const char* text)
{
	PMString msg(text);
	msg.SetTranslatable(kFalse);
	KCMNotifyStatus(msg);
}

// uids を1つの kHideSpreadCmdBoss で隠す/再表示する。hide=kTrue で隠す。
// ★IBoolData の方向は kTrue=隠す(2026-07-04 実機確認済み。kLockLayerCmdBoss と同型)。
static ErrorCode KCMProcessHideSpreadCmd(IDataBase* db, const std::vector<UID>& uids, bool16 hide)
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

/* KCMHideUnchangedToggle(KCMHideUnchanged.h で宣言) — フライアウト「Hide Unchanged Spreads」。
   (2026-08-13 に KCMActionComponent::DoHideUnchangedToggle から移動。中身は無変更。)
   OFF→ON: 確認ダイアログ(Yes/No、ロケール連動文言)→ Yes なら、比較マーク(sEntries)が1ページも
   無いスプレッドを集めて kHideSpreadCmdBoss で一括で隠し、UID を控えてチェック ON。続けて Source 側も
   同じ分類(平坦ページ番号対応)で自動的に隠す(両文書とも dirty になる)。
   ON→OFF: 控えた分だけ両文書とも再表示(確認なし)。
   ガード: 比較マークが無い(Start 前/変更ゼロ)なら何もしない。特に「変更のあったスプレッドが1つも
   ない」場合は全スプレッドを隠すことになり、InDesign は全スプレッド非表示を許さないため中止する
   (Source 側のみ全対象になった場合は Source 側だけスキップ)。 */
void KCMHideUnchangedToggle()
{
	// ON→OFF: 自分が隠した分だけ再表示して状態を捨てる。
	if (sHideUnchangedOn)
	{
		const int32 failed = KCMResetHideUnchangedCore(kTrue);
		// スクロールバー地図を再表示後の配置で描き直す(隠しスプレッドは地図から除外される。2026-07-11)。
		// ★2026-08-13(Task 10): 地図は UI の持ち物なので通知へ。スプレッドの表示/非表示は「ページの
		//   見え方が変わった」ことなので kKCMPageFlagsChangedMessage に相乗りする。
		//   ★文書は渡さない＝サムネイルの Purge は要らない(変わるのは strip の配置だけ)。
		KCMNotify(kKCMPageFlagsChangedMessage);
		// ★2026-08-16(API 監査 B10): 以前は結果を見ずに必ず "restored." と出していた＝戻せなかったときも
		//   「戻した」と報告していた。控えは成否にかかわらず捨てる(次の Start で作り直す)ので、失敗した
		//   ぶんはユーザーが自分でページパネルから再表示するしかない＝黙って消してよい失敗ではない。
		if (failed > 0)
			KCMHideStatus("Hide Unchanged: could not show all hidden spreads back.");
		else
			KCMHideStatus("Hide Unchanged: hidden spreads restored.");
		return;
	}

	// OFF→ON。
	IDataBase* db = KCMDrawEventHandler::sDB;
	if (db == nil)
	{
		// Start 前。
		KCMHideStatus("Hide Unchanged: Start first.");
		return;
	}

	// ★「/」が付く overflow ページ(登録されていないのに、文書間のページ数差で比較相手が無い=未比較の
	//   ページ)を含むスプレッドは、変更ありページや登録済み("Added")ページと同じく隠さない
	//   (未比較の見落としを防ぐ。ユーザー要望 2026-07-06)。
	//   (Source 側は下の分類が対応表外ページを既に「変更あり」扱いにしているので隠れない)。
	//
	// ★★2026-08-18(不具合再検査 B10) = **その「/」は、画面に出ている「/」と同じ集合でなければならない。**
	//   以前はここで KCMBuildPairing を呼び直し、**今の文書構成から**あふれを計算していた。ところが
	//   画面・サムネイル・スクロール地図・境界(IKCMMarkData::IsOverflowPage)はどれも overflow キャッシュ
	//   (sOverflowT/sOverflowS)を見ており、そちらは**比較した時点で固定**される ---- KCMDrawEventHandler.h
	//   の "生のページ挿入/削除(Start無し)には追従しない=次の Start/再比較まで固定(枠=リングと同じ挙動)"。
	//   ∴ Start の後にページを足す/消して再比較していないと、**画面に「/」が出ていないページを
	//   「変更あり」と数える**(逆もある)= 上の「『/』が付くスプレッドは隠さない」という約束が、画面が
	//   言っている「/」とは別のものを指していた。⇒ 描画と同じキャッシュを読む。
	//   ⚠EnsureOverflowCache は控えた (sDB,sSrcDB) が現在と一致していれば**何もしない**(=通常は読むだけ)。
	//     作り直す場合も内部でロックを取って swap するので、BG の描画と競合しない。
	// ⚠★Target は描画エンジンの sDB、Source は arm 状態(KCMArmedSourceDB=sPeekSourceDB)と、
	//   **2つの別々の場所**から取っている。食い違わない根拠は実測(2026-08-18・不具合再検査 B10):
	//   KCMDoDisarmMousePeek の呼び手は KCMStopComparison ただ1つで、そこは直前に
	//   KCMDoClearMarks(= sDB を落とす)を呼ぶ ---- ∴「arm だけ落ちて sDB が残る」状態は作れない。
	//   ⚠disarm を単独で呼ぶ経路を足した日に、この前提は消える([[one-question-one-place]])。
	IDataBase* const srcDB = KCMArmedSourceDB();
	const bool16 hasSource = (srcDB != nil && srcDB != db);
	KCMDrawEventHandler::EnsureOverflowCache();
	const std::set<UID>& tOverflowSet = KCMDrawEventHandler::sOverflowT;

	// sEntries が空でも、登録済み(比較相手なし="Added")ページや overflow ページがあれば続行する
	// (それら自体が「変更あり=残す」扱いになるため)。全部無ければ全スプレッドが対象になり、
	// 全スプレッド非表示は InDesign が許さないので中止する。
	if (KCMDrawEventHandler::sEntries.empty() && !KCMPageMapHasAnyRegistered(db) && tOverflowSet.empty())
	{
		KCMHideStatus("Hide Unchanged: no changes to hide.");
		return;
	}

	// 変更なしスプレッド = 所属ページが1つも sEntries に載っていないスプレッド。
	InterfacePtr<ISpreadList> spreadList(db, db->GetRootUID(), UseDefaultIID());
	if (spreadList == nil)
	{
		KCMHideStatus("Hide Unchanged: spread list not available.");
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
			if (KCMDrawEventHandler::sEntries.count(pageUID) > 0 ||
			    KCMPageMapIsRegistered(db, pageUID) ||
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
		KCMHideStatus("Hide Unchanged: all changed; none to hide.");
		return;
	}
	if ((int32)unchanged.size() >= visibleCount)
	{
		// 保険(sEntries が非空なら通常ここへは来ない): 表示中スプレッドを全部隠すことになる場合は中止
		// (InDesign は全スプレッド非表示を許さない。分母は「現在表示中」の数=手動で隠し済みの分は除く)。
		KCMHideStatus("Hide Unchanged: can't hide all spreads.");
		return;
	}

	// 確認ダイアログ(kHideSpreadCmdBoss は永続変更=文書が dirty になり、隠し状態は保存ファイルにも残る)。
	// 文言はロケール連動(enUS=英語/jaJP=日本語)。ボタンは Windows の制約で標準 Yes/No のみ。
	// ★★2026-08-15（第2段 Task 10）＝**model 側から窓を出している唯一のアラート**。残す判断の根拠:
	//   ・`CAlert` は `Public.lib` の静的クラスで、**UI プラグイン由来の boss ではない**
	//     ＝ガイド L101 の「BG で nil が返る」型ではない（リンカにも Grep にも出ないのはそのため）。
	//   ・この関数（KCMHideUnchangedToggle）は Facade 越しに**フライアウトの操作**からしか入らず、
	//     **BG で走る描画パスからは到達しない**（2026-08-15 に呼び出し全数を Grep して確認）。
	//   ⚠それでも「model が人に問うている」ことに変わりはない。**BG から呼ぶ経路を作るなら、
	//     問いを UI 側へ出して結果を引数で受け取る形へ変える**（Task 4B / 9B と同じ）。
	//     ⚠BG でモーダルを出そうとすると、応答する人が居ないまま**そのスレッドが止まる**。
	const int16 clicked = CAlert::ModalAlert
	(
		// "This feature modifies the document file. Continue?" / 日本語 UI では日本語(KCMLoc)。
		// ★文書を変更する前の確認なので、意味を取り違えられないよう日本語 UI では日本語で出す
		//   (2026-08-06 ユーザー指示)。
		KCMLoc::Text(kKCMHideConfirmKey, KCMJa::kHideConfirm),
		kYesString,
		kNoString,
		kNullString,
		1,							// Yes を既定ボタンに
		CAlert::eWarningIcon
	);
	if (clicked != 1)
		return;						// No: チェックも付けず何もしない

	// ---- ここから Target と Source の2本を「1つのシーケンス」で打つ ----
	// ★2026-08-16(API 監査 B10): 以前は文書ごとに別々の ProcessCommand で、undo が2段に割れていた。
	//   2026-08-16 に実機で測ったところ kHideSpreadCmdBoss は **1本につき1段** 積む(スプレッドを2つ
	//   別々に隠して Ctrl+Z を1回 → 2つ目だけが戻った)。つまり分けたままだと Ctrl+Z 一回で Source
	//   だけが戻り、Target は隠れたままなのに KCM の控えは「両方隠している」と言う。
	//   ⚠文書をまたぐ操作を文書ごとに別のシーケンスへ割ると、片方を undo したとき他方が「履歴からは
	//   消えるのに戻らない」形になる(2026-07-28 実測)。公式の受け皿 = CmdUtils::SequenceContext
	//   (CmdUtils.h:186-222 = 既にシーケンスがあれば合流する側。Adobe の実例 =
	//   conditionaltextui/ConditionSetDropDownObserver.cpp:476)。★シーケンス名は渡さない = undo メニュー
	//   の表記は本体の「スプレッドを隠す」のまま(KCM の UI 文字列は英語固定なので、日本語 UI の
	//   編集メニューに英語を混ぜない)。
	// ★宣言の順序に意味がある: GlobalErrorStatePreserver を先に作る = 後から作った seq が先に壊れる。
	//   seq が閉じる瞬間のグローバルエラーが kSuccess かどうかで commit か巻き戻しかが決まるので
	//   (CmdUtils.h:189-193)、失敗はすべてステータス行へ変換してからエラーを落とす。立てたまま次の
	//   コマンドを投げると protective shutdown になる(CmdUtils.h:72-77)。
	//   この形は KCM 内の先例と同じ ---- KCMBookCompare.cpp の見出し
	//   "THIS OPEN IS ALLOWED TO FAIL" と、KCMBookOpen.cpp の "THE WINDOW IS ALLOWED NOT TO APPEAR"。
	//   ★2026-08-18(不具合再検査 B10)に**行番号(:132-136 / :158-160)から見出しの語へ差し替えた** ----
	//   実測した実体は :142-143 と :169-170 で、**どちらも 10〜11 行ずれていた**(それぞれの側に後から
	//   足された説明が下を押し下げた)。★両方とも "***** ... *****" の見出しを元から持っていたので、
	//   B7 の処方(指される側に見出しを立て、見出しの語で引く)は**立てる必要すら無く、引く側が
	//   使っていなかっただけ**だった。
	ErrorCode err = kFailure;
	int32 srcHiddenCount = 0;
	bool16 srcSkippedAll = kFalse;
	bool16 srcFailed = kFalse;		// Source を隠すコマンドが失敗した(以前はここが完全に無言だった)
	{
		GlobalErrorStatePreserver hideErrorState;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		CmdUtils::SequenceContext hideSeq;

		err = KCMProcessHideSpreadCmd(db, unchanged, kTrue /*hide*/);
		if (err == kSuccess)
		{
			sHideUnchangedOn = kTrue;
			sHiddenDB = db;
			sHiddenSpreads = unchanged;

			// ---- Source 側も同じ分類で自動的に隠す ----
			// 「変更あり」を除外対応表(登録済み=比較相手なしページを除いた順番対応)経由で Source ページの
			// 集合にし、Source のスプレッドを走査して、変更ありページに対応するページを1つも含まない
			// スプレッドを隠す。対応表に無い Source ページ(登録済み=削除ページ扱い)は安全側で「変更あり」
			// 扱いにする(縁枠合成(ステップ3)が入るまでの暫定方針)。
			// Source 側が失敗/スキップでも Target 側の隠しはそのまま生かす(致命ではないため)。
			// ⚠だからこそ失敗をエラー状態に残せない = 残すと seq が閉じるとき Target の隠しごと巻き戻る。
			if (hasSource)
			{
				// ★★2026-08-18(不具合再検査 B10): 対応表も**比較した時点のもの**を使う。
				//   sPrevPairTargetToSource は「前回の比較で使った Target→Source のペアリング」で、
				//   下で参照する sEntries と**同じ1回の比較から**作られる(KCMCore.cpp が両方を同じ
				//   tPages/sPages の添字で埋め、末尾で swap する)。以前はここで KCMBuildPairing を
				//   呼び直していたので、Start の後にページ構成が変わると「**今の**対応表 × **比較時点の**
				//   マーク」という、どの瞬間にも存在しなかった組み合わせで Source を分類していた
				//   (Target 側の overflow を画面と揃えたのと同じ理由。関数先頭のコメント参照)。
				//   ⚠登録済み(Added/Removed)ページと overflow は元からこの表に載らない ---- find が
				//     外れて「変更あり」= 隠さない、に倒れる。この安全側の性質は従来と同じ。
				//   ⚠この表はマスターページの組も含む(KCMCore.cpp が通常ページの後ろに連結する)が、
				//     下の走査は ISpreadList = 通常スプレッドしか回らないので、余分な組は引かれない。
				std::map<UID, bool16> srcChangedMap;	// 対応表にあるSourceページ→対応Targetページが変更ありか
				const std::map<UID, UID>& pairing = KCMDrawEventHandler::sPrevPairTargetToSource;
				for (std::map<UID, UID>::const_iterator pit = pairing.begin(); pit != pairing.end(); ++pit)
					srcChangedMap[pit->second] = (KCMDrawEventHandler::sEntries.count(pit->first) > 0) ? kTrue : kFalse;

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
						else if (KCMProcessHideSpreadCmd(srcDB, srcUnchanged, kTrue /*hide*/) == kSuccess)
						{
							sHiddenSrcDB = srcDB;
							sHiddenSrcSpreads = srcUnchanged;
							srcHiddenCount = (int32)srcUnchanged.size();
						}
						else
							srcFailed = kTrue;
					}
				}
			}
		}

		// 失敗はこの下のステータス行に変換し終えたので、seq が閉じる前に落とす(消してよい条件 =
		// 「失敗が別の形で生き残っている」。握りつぶしではない)。
		if (err != kSuccess || srcFailed)
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	}

	if (err != kSuccess)
	{
		KCMHideStatus("Hide Unchanged: hide command failed.");
		return;
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
	if (srcFailed)
		msg.Append(" Source not hidden (hide command failed).");
	KCMNotifyStatus(msg);

	// スクロールバー地図を隠し後の配置で描き直す(隠しスプレッドは地図から除外される。Target/Source 両窓)。
	// ★2026-08-13(Task 10): 上の ON→OFF 側と同じ理由で通知へ。
	KCMNotify(kKCMPageFlagsChangedMessage);
}

// 文書の生存確認は共有ヘルパ KCMIsDocDBOpen(KCMCore.h)を使う(旧ここ static、2026-07-10 共有化)。

// 片側(db+控えリスト)の再表示と状態破棄。restore=kTrue かつ db が生存している場合のみ再表示コマンドを
// 打つ(途中で削除されたスプレッドは ISpread クエリが nil になるのでスキップ)。db が閉じていれば
// deref せず黙って状態だけ捨てる。db は参照渡しで nil に戻す。
// ★戻り値 kTrue = 再表示コマンドを打ったが失敗した(2026-08-16 の API 監査 B10。以前は戻り値を捨てて
//   いたので、戻せなかったことが呼び手にもユーザーにも一切伝わらなかった)。控えを捨てるのは成否に
//   関係なく従来どおり = 次の Start で作り直すものなので、持ち越しても意味がない。
static bool16 KCMRestoreHiddenList(IDataBase*& db, std::vector<UID>& list, bool16 restore)
{
	bool16 failed = kFalse;
	if (restore && db != nil && !list.empty() && KCMIsDocDBOpen(db))
	{
		std::vector<UID> alive;
		for (size_t i = 0; i < list.size(); ++i)
		{
			InterfacePtr<ISpread> spread(db, list[i], UseDefaultIID());
			if (spread != nil)
				alive.push_back(list[i]);
		}
		if (!alive.empty() && KCMProcessHideSpreadCmd(db, alive, kFalse /*unhide*/) != kSuccess)
			failed = kTrue;
	}
	list.clear();
	db = nil;
	return failed;
}

// KCMResetHideUnchangedCore(このファイルの上で前方宣言) — リセットの本体。戻り値 = 失敗した再表示
// コマンドの本数(0..2)。
//   restoreSpreads=kTrue: 控えているスプレッドを再表示してから状態を捨てる(再比較/Stop/手動 OFF/
//     クローズスイープ)。文書の生存は内部で確認するので、片方が閉じていても安全(生存側のみ再表示)。
//   restoreSpreads=kFalse: db に一切触れず状態だけ捨てる(コマンドを打たない = シーケンスも要らない)。
// ★2026-08-16(API 監査 B10): 隠すときと同じ理由で、Target と Source の再表示も1つのシーケンスに入れる
//   (ここは元々2本を連続で投げており、1本目が失敗するとエラーを立てたまま2本目を投げていた =
//   CmdUtils.h:72-77 が protective shutdown と書いている形そのもの)。
static int32 KCMResetHideUnchangedCore(bool16 restoreSpreads)
{
	int32 failed = 0;
	if (restoreSpreads)
	{
		GlobalErrorStatePreserver restoreErrorState;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		CmdUtils::SequenceContext restoreSeq;

		if (KCMRestoreHiddenList(sHiddenDB, sHiddenSpreads, kTrue))
			++failed;
		if (KCMRestoreHiddenList(sHiddenSrcDB, sHiddenSrcSpreads, kTrue))
			++failed;

		// 失敗は戻り値(とトグルのステータス行)に変換済み = seq が閉じる前に落とす。立てたまま閉じると
		// 成功した側の再表示まで巻き戻り、「戻したのに隠れたまま」になる(CmdUtils.h:189-193)。
		if (failed > 0)
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	}
	else
	{
		KCMRestoreHiddenList(sHiddenDB, sHiddenSpreads, kFalse);
		KCMRestoreHiddenList(sHiddenSrcDB, sHiddenSrcSpreads, kFalse);
	}
	sHideUnchangedOn = kFalse;
	return failed;
}

// KCMResetHideUnchanged(KCMHideUnchanged.h で宣言) — 上の本体の公開版。呼び手(再比較/Stop/クローズ
// スイープ)は「戻せたか」で振る舞いを変えないので void のまま = 境界の Facade も触らない。
void KCMResetHideUnchanged(bool16 restoreSpreads)
{
	(void)KCMResetHideUnchangedCore(restoreSpreads);
}

// KCMGetHideUnchangedOn(KCMHideUnchanged.h で宣言) — メニューのチェックマーク用。
// (2026-08-13 新設。トグルの旗はこのファイルに閉じ、UI 側の UpdateActionStates はここへ聞く。)
bool16 KCMGetHideUnchangedOn()
{
	return sHideUnchangedOn;
}

IDataBase* KCMGetHideUnchangedDB()
{
	return sHiddenDB;
}

IDataBase* KCMGetHideUnchangedSrcDB()
{
	return sHiddenSrcDB;
}

// KCMHideUnchanged.cpp 終わり。
