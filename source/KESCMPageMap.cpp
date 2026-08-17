//========================================================================================
//
//  KESCMPageMap.cpp
//
//  ページ対応(追加/削除ページ)モジュール。ページパネルでページを選択→右クリックのトグル
//  「KCM: Register as Added/Removed Pages」で「比較相手なしページ」として登録/解除する。
//  アクティブ文書が Target なら「追加ページ」/Source なら「削除ページ」だが、概念はどちらも
//  同じ「比較相手なし」(対応表からの除外)なので、入れ物は文書DBごとの UID セット1種類。
//
//  - 選択の取得: ★Utils<ILayoutUIUtils>()->GetSelectedPages()(公式API、ILayoutUIUtils.h:183)。
//    bPagesOnly=kTrue でスプレッド選択も所属ページUIDへ展開、bIncludeMasters は呼び出し側が決める
//    (この Register は kFalse=マスター除外。理由は KESCMPageMap.h の includeMasters のコメント)。
//    ★実使用例は3つあるが目的で引数が分かれる: 製品 PageTransitionsPanelObserver.cpp:672 は
//    bPagesOnly=kFalse(スプレッド単位のトランジションが目的でページ/スプレッド混在を欲しがる)、
//    codesnippets/SnpModifyLayoutGrid.cpp:959 と SnpInspectLayoutGrid.cpp:690 は既定(=kTrue)。
//    KESCM はページ単位で対応表を作るので kTrue が正しい(2026-08-06 ブロック9 監査で確認)。
//    ★旧実装の自前 IUIDListControlData 読み(kPagesPanelWidgetBoss 直上)は「ページアイコン選択」
//    しか拾えず、見開き(スプレッド)として選択されると空になり項目が出なかった(2026-07-05 実機)。
//    パネルには文書ページ用/マスター用のサブパネルが2つあり、選択の置き場は1本ではない。
//  - メニュー: ★**ui/KCMUI.fr**(:891-926)がページパネルのページ右クリックメニュー(内部名
//    RtMenuPagesPanel、2026-07-05 実機確定)へトグル項目を追加している。内部名は非翻訳キーなので
//    全ロケール共通。⚠2026-08-17 訂正: ここは分割前の「KESCM.fr」のままだった ---- メニューは
//    第2段(Task 6B-2)で UI 側へ出ており、model 側 KESCM.fr に RtMenuPagesPanel は1件も無い。
//    チェック/有効無効/動的ラベルは kCustomEnabling → KESCMPageMapGetToggleState。
//  - 登録の保持: セッション内のみ(文書ファイルには保存しない=dirty にもならない)。文書クローズ時は
//    KESCMHandleDocsClosed からの KESCMPageMapSweepClosedDocs で状態だけ捨てる(deref なし)。
//  - ステップ2(2026-07-05): 除外対応表(KESCMBuildPairing/KESCMMapTargetToSource/
//    KESCMMapSourceToTarget)。登録済みページを平坦列から除いて残り同士を順番に対応させ、
//    比較(KESCMDoMarkChangesDoc)・peek旧版取得・スプレッド再比較・CMYK色サンプラ・
//    Hide UnchangedのSource側分類の5箇所が、素の平坦列 zip からこの対応表経由に置き換わった。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IDataBase.h"
#include "ILayoutUIUtils.h"		// GetSelectedPages(ページパネル選択の公式取得)
#include "IMasterSpreadList.h"	// GetMasterSpreadCount / GetNthMasterSpreadUID / FindMasterByName
#include "IMasterSpread.h"		// GetPrefix / GetBasename(マスタースプレッドの名前対応)
#include "ISpread.h"			// GetNumPages / GetNthPageUID(マスタースプレッド内のページ)
#include "Utils.h"
#include "UIDList.h"
#include "PMString.h"

#include <set>
#include <vector>

#include "KESCMCore.h"			// KESCMCollectPageUIDs / KESCMCollectMasterPageUIDs / KESCMArmedTargetDB / KESCMArmedSourceDB
								// (ステータス行は 2026-08-13 Task 9 で KESCMNotifyStatus＝通知へ移った)
#include "KESCMModelNotify.h"	// KESCMNotifyStatus - the model tells the UI, it never calls it (Task 9)
#include "KESCMComparisonRun.h"	// KESCMToggleStartStop(2026-08-13 に KESCMCore.h から移動)
#include "KESCMPageMap.h"
#include "KESCMDocUidSet.h"		// 「文書DB→ページUID集合」の共通の入れ物(✓側と共有。2026-08-06 監査 C-1)
#include "KESCMID.h"				// kKESCMPageFlagsChangedMessage(通知の ID)
// ★2026-08-13(Task 10): UI 側ヘッダー KESCMThumbnailRefresh.h の include を落とした。サムネイルを
//   作り直すのは通知を受けた UI の仕事。

// 登録済み「比較相手なしページ」: 文書DB → ページUIDの集合。セッション内のみ。
// 空になった文書のエントリは即座に消える(KESCMDocUidSet の規約)。
static KESCMDocUidSet sRegistered;

// ヘルパ: vector<UID> の線形 contains。★用途は「これまでに積んだ選択(outPages)との重複除去」だけで、
// 相手は高々選択数なので線形でよい。★文書の全ページ列(flat)との突合はこれを使わない(set で引く=
// 下の KESCMPageMapReadSelection。2026-08-06 ブロック9 監査 C-6: 旧実装は選択数だけを見て線形にして
// いたが、探索対象はページ数側なので 1,000ページ×100選択で 10 万回になっていた)。
static bool16 KESCMVecContains(const std::vector<UID>& v, UID u)
{
	for (size_t k = 0; k < v.size(); ++k)
	{
		if (v[k] == u)
			return kTrue;
	}
	return kFalse;
}

//========================================================================================
// KESCMPageMapReadSelection(KESCMPageMap.h で宣言) — ページパネルの選択を読む共通リーダー。
// outDB=選択が属する文書(=アクティブ文書)、outPages=文書のページ列に実在する選択ページUID。
// 有効なページが1つ以上あれば kTrue。
// 取得は公式 API Utils<ILayoutUIUtils>()->GetSelectedPages():
//   ・bIncludeMasters=引数 includeMasters … マスターページ/マスタースプレッドを読むか(下記)
//   ・bPagesOnly=kTrue …… 見開き全体の選択(パネル内部ではスプレッド扱い)も所属ページUIDへ展開
//   ・bCurrentPageOnly=kTrue はパネル非表示時のフォールバック規定(このメニューはパネルからしか
//     開けないため実質使われない)
// 返ったUIDは念のため文書の平坦ページ列と突合し、重複も除去する。
// ★Register(ここ)/Check(KESCMPageCheck.cpp)/Refresh(KESCMPeek.cpp)の3機能共通(2026-07-15 統合)。
//
// ★★includeMasters(2026-08-13。既定 kFalse=従来どおり通常ページのみ)。意味と、どちらを渡すかの
//   判断基準はヘッダー KESCMPageMap.h のコメントに書いてある(3機能で答えが割れるので引数にした)。
//   ⚠**除外は2段構えだった**: GetSelectedPages の bIncludeMasters だけでなく、突合相手の
//   KESCMCollectPageUIDs にもマスターが入らない(2026-08-16 以降は `IPageList.h:81` の契約
//   "does not include master pages" が根拠。それ以前は ISpreadList を回していたから)。片方だけ
//   直してもマスターは flatSet に無く落ちるので、**必ず両方を同じ includeMasters で揃える**。
//   マスターページの列は KESCMCollectMasterPageUIDs が後ろへ連結する(out をクリアしない契約)。
//========================================================================================
bool16 KESCMPageMapReadSelection(IDataBase*& outDB, std::vector<UID>& outPages, bool16 includeMasters)
{
	outDB = nil;
	outPages.clear();

	// ページパネルの表示対象=アクティブ文書。その db を UIDList に仕込んで渡す契約
	// (ILayoutUIUtils.h:178 "UIDList must be set up with proper database")。
	// ★db は KESCMActiveDocDB()(=IActiveContext::GetContextDocument)で引く(2026-08-06 ブロック9 監査 A-1)。
	//   公式の GetSelectedPages 実例も ActiveContext 経由 = codesnippets/SnpModifyLayoutGrid.cpp:951-958
	//   (製品 dynamicdocumentsui/PageTransitionsPanelObserver.cpp:665-671 は ILayoutControlData 経由の別流儀)。
	//   ⚠旧実装の Utils<ILayoutUIUtils>()->GetFrontDocument() は契約が「frontmost *layout* presentation の
	//   文書」(ILayoutUIUtils.h:95-98)で、ストーリーエディタ窓が最前面のときアクティブ文書と食い違い得る
	//   (=ページパネルが見せている文書とは別の db で選択 UID を解釈してしまう)。
	IDataBase* db = KESCMActiveDocDB();
	if (db == nil)
		return kFalse;

	// ★★★2026-08-15（第2段 Task 10）＝**なぜ UI 由来の Utils が model 側に残っているのか**
	//
	//  `ILayoutUIUtils` は名前のとおり **UI プラグイン由来**で、ガイド vol1-07 L101 の
	//  「UI プラグインの boss はバックグラウンドスレッドから実体化できず nil が返る」に当たる。
	//  にもかかわらずここに残しているのは、**この関数が BG から到達しないことを実測したから**:
	//
	//    ・呼び手は KESCMPageMapToggleSelectedPages と KESCMPageMapGetToggleState の2つだけ。
	//      どちらも Facade（IKESCMPageFlagsFacade）越しに **UI のメニュー操作**から入る。
	//    ・BG で走るのは描画パス（KESCMDrawEventHandler::HandleDrawEvent）だけで、そこが呼ぶ
	//      ページマップ系は **KESCMPageMapIsRegistered / KESCMPageMapHasAnyRegistered の読み取り2本**
	//      のみ（2026-08-15 に呼び出し全数を Grep して確認）。この関数へは辿り着かない。
	//
	//  ⚠**「今は届かない」であって「構造的に届かない」ではない。** 次のどれかをやるなら、
	//    ここは真っ先に見直す対象になる:
	//      ①描画パスから選択を読む ②この関数を新しい経路から呼ぶ ③InDesign Server 対応
	//    （現在の `.fr` は `{ kInDesignProduct }` のみ＝Server では読み込まれない）。
	//  ★見直すときの形は決まっている＝**Task 4B / 9B と同じ「観測は UI・方針は model」**
	//    ＝UI が選択を取り、model は UIDList を引数で受け取る。
	UIDList sel(db);
	Utils<ILayoutUIUtils>()->GetSelectedPages(sel, includeMasters, kTrue /*currentPageOnly*/, kTrue /*pagesOnly*/);

	// 突合相手は「文書の全ページ」なので set で引く(上の KESCMVecContains のコメント参照)。
	std::vector<UID> flat;
	KESCMCollectPageUIDs(db, flat);
	if (includeMasters)
		KESCMCollectMasterPageUIDs(db, flat);	// ★マスターは後ろへ連結される(out をクリアしない)
	const std::set<UID> flatSet(flat.begin(), flat.end());
	const int32 n = sel.Length();
	for (int32 i = 0; i < n; ++i)
	{
		const UID u = sel[i];
		if (flatSet.count(u) > 0 && !KESCMVecContains(outPages, u))
			outPages.push_back(u);
	}
	if (outPages.empty())
		return kFalse;

	outDB = db;
	return kTrue;
}

// 文書の役割に応じた呼び名(ステータス行の文言用): Target=added/Source=removed/
// 未 arm・無関係な文書=総称 "added/removed"。
static const char* KESCMPageMapRoleWord(IDataBase* db)
{
	if (db != nil && db == KESCMArmedTargetDB())
		return "added";
	if (db != nil && db == KESCMArmedSourceDB())
		return "removed";
	return "added/removed";
}

//========================================================================================
// KESCMPageMapToggleSelectedPages(KESCMPageMap.h で宣言)
//   右クリックトグルの実行。選択ページに1つでも未登録があれば「全登録」、全部登録済みなら
//   「全解除」(チェック表示と対になる標準的なトグル動作)。結果と、その文書の登録合計を
//   パネルのステータス行に出す(パネルが隠れていてもセッションに残り、再表示時に見える)。
//========================================================================================
void KESCMPageMapToggleSelectedPages()
{
	IDataBase* db = nil;
	std::vector<UID> pages;
	if (!KESCMPageMapReadSelection(db, pages))
		return;		// メニューは kCustomEnabling で無効化済みのはずだが保険

	// ★2026-07-11(ユーザー指定): 登録は「比較を Start 中(arm 済み)」かつ「選択文書が Target/Source」の
	//   ときだけ可能。メニューは KESCMPageMapGetToggleState の答えで無効化済みのはずだが、保険としてここでも弾く。
	if (!KESCMIsArmed() || (db != KESCMArmedTargetDB() && db != KESCMArmedSourceDB()))
		return;

	bool16 anyUnregistered = kFalse;
	for (size_t i = 0; i < pages.size(); ++i)
	{
		if (!sRegistered.Contains(db, pages[i]))
		{
			anyUnregistered = kTrue;
			break;
		}
	}

	// ★パネルのステータス欄は幅・行数とも小さいため(ui/KCMUI.fr:1921 の kKESCMStatusTextWidgetID は
	// Frame(8,76,216,150)＝**208×74px の4行**で、kDontEllipsize＝自動省略もされない)、
	// メッセージは短く1行に収める。⚠2026-08-17 訂正: ここは「176×52px 程度」と書いていた(2026-08-06
	// 世代の値)。同じ欄を KESCMPageCheck.cpp が「約152px×4行」(2026-07-15 世代)と書いており、
	// **同じ問いに2つの古い答えがあった**＝[[one-question-one-place]]。寸法は .fr で実測すること。
	// ⚠ この欄は UI 側にあり、ここ(model)からは通知経由でしか届かない。**それでも文面の長さは
	//   ここで決まる**＝送り手が短くするしかない(受け手には切る以外の逃げ道が無く、
	//   数字の途中で切れると別の数に見える＝[[ellipsis-in-status-line-breaks-numbers]])。
	PMString msg;
	msg.SetTranslatable(kFalse);
	if (anyUnregistered)
	{
		for (size_t i = 0; i < pages.size(); ++i)
			sRegistered.Insert(db, pages[i]);
		msg.Append("+");
		msg.AppendNumber((int32)pages.size());
		msg.Append(" ");
		msg.Append(KESCMPageMapRoleWord(db));
	}
	else
	{
		for (size_t i = 0; i < pages.size(); ++i)
			sRegistered.Erase(db, pages[i]);
		msg.Append("-");
		msg.AppendNumber((int32)pages.size());
		msg.Append(" ");
		msg.Append(KESCMPageMapRoleWord(db));
	}

	// 合計は付け外しの後に数える(解除で空になった文書のエントリは Erase が捨てているので 0 が返る)。
	msg.Append(", total ");
	msg.AppendNumber(sRegistered.CountIn(db));

	// ★既に比較実行済み(Start後)なら、除外対応表が変わった分をその場で反映するため、Start と同じ
	// 全体再比較を自動で走らせる(実機確認: 比較後に登録を変えてもリアルタイムには反映されなかった
	// ため、2026-07-05 にこの自動再比較を追加)。Start 未実行なら何もしない(次の Start で自然に反映)。
	// (旧記述の「Show Marks on Source を既定 ON に戻す副作用」は 2026-07-25 に Start 経路へ移動済み=
	// この再比較ではユーザーの OFF 選択は保たれる)。報告文字列(report)は使わず短い
	// サフィックスだけ足す(ステータス欄が小さく、report をそのまま足すと溢れるため)。
	bool16 recompared = kFalse;
	if (KESCMIsArmed() && KESCMArmedTargetDB() != nil && KESCMArmedSourceDB() != nil)
	{
		// ★差分再比較(allowIncremental=kTrue)。登録の追加/解除では文書内容は変わらず除外対応表の
		// ペアリングだけが動くので、ペアが不変のページは前回結果を再利用し(=ラスタ化しない)、ペアが
		// 新規/相手変化/消滅したページだけを再計算する。大規模文書ほど効く。全体の総入れ替えではない。
		// ★戻り値を必ず見る(2026-08-05 監査で発見した「枠ゼロの Start 中」穴の5個目): 登録の変更は
		//   後続全ページのペアをずらすので差分でもラスタ化枚数が嵩み、進捗バーの Cancel が出得る。
		//   キャンセルされるとマークは KESCMDoMarkChangesDoc 側で全破棄済み(kFailure)なのに、無視して
		//   進むと「枠が1つも無い Start 中」のまま "(recompared)" と嘘の報告をしてしまう。
		//   Load Check & Register のキャンセル(KESCMPageCheck.cpp)と同じく Stop まで戻す
		//   (今回の登録変更も Stop の全消去で捨てられる=Stop の仕様どおり)。
		PMString report;
		if (KESCMDoMarkChangesDoc(KESCMArmedTargetDB(), KESCMArmedSourceDB(), report, kTrue /*allowIncremental*/) != kSuccess)
		{
			KESCMToggleStartStop();		// arm 中なので Stop 分岐(マーク/登録/Check 破棄・disarm・パネル更新)
			PMString cmsg("Recompare cancelled");
			cmsg.SetTranslatable(kFalse);
			KESCMNotifyStatus(cmsg, kTrue /*forceRedrawNow*/);
			return;
		}
		msg.Append(" (recompared)");
		recompared = kTrue;
	}

	// ★トグルしたページのサムネイル明示 per-UID Purge。必要なのは次の2ケースだけ:
	//   ・再比較が走らなかった(未 arm 等) … 他に refresh 経路が無い
	//   ・登録解除 … 解除ページは sRegistered からも sEntries/overflow(※)からも消えるため、
	//     **現在の状態から作れるどの集合にも入らない**=ここで拾うしかない
	//     (※登録中はペアリングから除外されていたので、旧 overflow にも入っていない)
	//   登録追加で再比較済みの場合はスキップ: トグル済みページは sRegistered に入っており、再比較側の
	//   KESCMCollectChangedPageUIDs(登録ページ込み)が既に Purge+ForceRedraw している。ここでも呼ぶと
	//   同じページを二重ラスタ化+パネル二重再描画(点滅)するだけで無意味(2026-07-10 レビューで判明)。
	// ⚠★★2026-08-17(不具合再検査 B4)訂正＝上の「再比較の Purge 集合(現在の集合∪**再比較前の
	//   sEntries/overflow 退避**)」という根拠は**もう存在しない**。その退避は 2026-08-13(Task 10)に
	//   廃止され(KESCMCore.cpp の KESCMDoMarkChangesDoc 冒頭が「この退避は今は取っていない」と明記)、
	//   **全再比較の通知はページ集合を載せない**⇒受け手の UI は db の**全ページ**を Purge している
	//   (ui/KESCMModelChangeObserver.cpp の fPagesA/fPagesB が nil の分岐)。
	//   ∴ 今の解除経路は「全ページ Purge のあとに触ったページをもう一度 Purge」＝**二重**になっている
	//   (上の段落が避けよと書いているものと同じ形。害は点滅と再ラスタ化だけで、絵は正しい)。
	//   ★**それでもこの通知は残す**: KESCMDoMarkChangesDoc の宿題「Task 12 で IKESCMMarkData が入ったら
	//     退避を取り直して per-UID Purge へ戻す」が生きており、戻した瞬間に**解除ページを拾えるのは
	//     この経路だけ**になる(戻すときに一緒に消すこと)。
	// ★2026-08-13(Task 10): 直接呼びから通知へ。
	// ★★2026-08-16(API 監査 B4): **トグルしたページ集合を通知に載せる**ので、UI は per-UID Purge に
	//   戻った。⚠旧記述「**どのページかは通知では運べない**ので UI は db の全ページを作り直す」は
	//   **誤り**だった——ISubject::Change の第3引数 changedBy で運べる(2026-08-15 の監査 B2 で
	//   判明していたのに、その訂正がこの行まで配られていなかった)。
	//   ⚠**渡す集合が「絵が変わり得るページ」を漏らしていないか**は、上の2ケース分析がそのまま答える:
	//     解除ページは sRegistered からも sEntries/overflow からも消えるのでどの**現在**集合にも
	//     入らない=「トグルしたページ」を運ぶ以外に拾う道が無い。
	//   ⇒ 上の「二重ラスタ化を避ける」条件も**残す意味がある**: 通知を出さなければ UI は動かない。
	if (!recompared || !anyUnregistered)
	{
		const std::set<UID> touched(pages.begin(), pages.end());
		KESCMNotifyPages(kKESCMPageFlagsChangedMessage, db, touched);
	}

	KESCMNotifyStatus(msg);
}

//========================================================================================
// KESCMPageMapGetToggleState(KESCMPageMap.h で宣言)
//   kCustomEnabling のトグルが今どう見えるべきかを**答えるだけ**。
//   ・選択に文書ページが無い(選択なし/マスターのみ)→無効
//   ・選択が全部登録済み→All / 一部だけ登録済み→Some(中間チェック)
//   ・fRole は「アクティブ文書が Target か Source か」(呼び手がラベルを選ぶ材料)
//
//   ★★2026-08-15(API 監査 B2 の A-2): **IActionStateList を受け取るのをやめた。**
//   メニューへの書き込み(SetNthActionState / SetNthActionName)と**ラベルの文字列**は
//   ui/KESCMActionComponent.cpp へ移した ---- メニューは UI の仕事だから(理由の全文は
//   KESCMPageMap.h の KESCMPageToggleState)。ここに残るのは「数える」ことだけ。
//========================================================================================
KESCMPageToggleState KESCMPageMapGetToggleState()
{
	KESCMPageToggleState st;	// 既定は「無効」

	IDataBase* db = nil;
	std::vector<UID> pages;
	if (!KESCMPageMapReadSelection(db, pages))
		return st;

	// ★2026-07-11(ユーザー指定): 登録は「比較を Start 中(arm 済み)」かつ「選択文書が Target/Source」の
	//   ときだけ可能=それ以外はグレーアウト。未 Start / 第3文書のページパネルではメニューを無効表示にする。
	if (!KESCMIsArmed() || (db != KESCMArmedTargetDB() && db != KESCMArmedSourceDB()))
		return st;

	int32 regCount = 0;
	for (size_t i = 0; i < pages.size(); ++i)
	{
		if (sRegistered.Contains(db, pages[i]))
			++regCount;
	}

	st.fEnabled = kTrue;
	if (regCount == (int32)pages.size())
		st.fTick = kKESCMPageTickAll;		// 全部登録済み=チェック
	else if (regCount > 0)
		st.fTick = kKESCMPageTickSome;		// 一部だけ登録済み=中間チェック

	// ラベルを選ぶ材料。⚠上のガードを抜けている以上、db は Target か Source のどちらかしかない
	//   ---- 旧実装の3つ目のラベル("Added/Removed" の総称)は**到達しない分岐だった**ので
	//   持ち越していない(kKESCMPageRoleNone は fEnabled=kFalse の場合の既定値としてのみ残る)。
	st.fRole = (db == KESCMArmedTargetDB()) ? kKESCMPageRoleTarget : kKESCMPageRoleSource;
	return st;
}

//========================================================================================
// KESCMPageMapSweepClosedDocs(KESCMPageMap.h で宣言)
//   ドキュメントクローズ直後の生存スイープ(呼び所=KESCMHandleDocsClosed)。閉じた文書の登録を
//   状態だけ捨てる。★閉じた db は FindDocByDataBase へのポインタ比較のみで、絶対に deref しない
//   (KESCM の他のクローズ後片付けと同じ流儀)。こまめに捨てることで、閉じた文書とアドレス再利用の
//   新文書を取り違える余地も最小化する。
//========================================================================================
void KESCMPageMapSweepClosedDocs()
{
	sRegistered.SweepClosedDocs();	// 終了中の nil ガードも deref 回避も入れ物側の責務(KESCMDocUidSet.cpp)
}

// (KESCMPageMapClearAll は 2026-08-17 の不具合再検査 B4 で削除: 呼び手ゼロで、しかもヘッダーが
//  「Stop から呼ぶ」と実在しない呼び手を宣言していた。Stop が呼ぶのは下の KESCMPageMapClearAllDocs。)

//========================================================================================
// KESCMPageMapClearAllDocs(KESCMPageMap.h で宣言)
//   全文書の登録を丸ごと忘れる。Stop(KESCMDoClearMarks)で呼び、比較を解除したら Add/Remove の
//   登録も残さない(ユーザー指定 2026-07-11)。ポインタは触らず map を空にするだけ(deref なし=安全)。
//========================================================================================
void KESCMPageMapClearAllDocs()
{
	sRegistered.ClearAllDocs();
}

//========================================================================================
// KESCMPageMapIsRegistered(KESCMPageMap.h で宣言)
//========================================================================================
bool16 KESCMPageMapIsRegistered(IDataBase* db, UID pageUID)
{
	return sRegistered.Contains(db, pageUID);
}

//========================================================================================
// KESCMPageMapHasAnyRegistered(KESCMPageMap.h で宣言)
//========================================================================================
bool16 KESCMPageMapHasAnyRegistered(IDataBase* db)
{
	return sRegistered.HasAny(db);
}

//========================================================================================
// KESCMPageMapCollectRegistered(KESCMPageMap.h で宣言)
//   db の登録済み(Added/Removed=緑「/」)ページ UID をすべて out に追加する(out はクリアしない=
//   既存の変更/overflow 集合に足し込む使い方)。登録ページは sEntries/overflow とは別管理なので、
//   サムネイル per-UID Purge の対象集合にこれを含めないと緑「/」が即時反映されない。
//========================================================================================
void KESCMPageMapCollectRegistered(IDataBase* db, std::set<UID>& out)
{
	sRegistered.CollectInto(db, out);	// out はクリアしない(入れ物側の契約)
}

//========================================================================================
// KESCMPageMapReplaceRegistered(KESCMPageMap.h で宣言)
//   db の登録集合を pages で丸ごと置き換える(LOAD 用の setter。「Load Check & Register」から呼ぶ)。
//   ここでは sRegistered を書き換えるだけで、再比較やサムネイル更新は行わない(呼び出し側が両文書を
//   set し終えてから一度だけ再比較する)。pages が空ならエントリごと消す。
//========================================================================================
void KESCMPageMapReplaceRegistered(IDataBase* db, const std::vector<UID>& pages)
{
	sRegistered.Replace(db, pages);		// 空ならエントリごと消える(入れ物側の契約)
}

//========================================================================================
// KESCMBuildPairing(KESCMPageMap.h で宣言)
//   targetDB/sourceDB の平坦ページ列(KESCMCollectPageUIDs)から、それぞれ登録済み(比較相手なし)
//   ページを除き、残り同士を順番に対応させる。従来(ステップ1以前)は素の平坦列を直接 zip していたが、
//   追加/削除ページが登録されていれば、そのページを飛ばして残りを詰めて対応させる。
//   ★文書間のページ数差で対応表からあふれたページ(登録されていないのに対応相手が無いページ)は
//   outOverflowTargetPages/outOverflowSourcePages(任意)に入れる。
//========================================================================================
void KESCMBuildPairing(IDataBase* targetDB, IDataBase* sourceDB,
	std::vector<UID>& outTargetPages, std::vector<UID>& outSourcePages,
	std::vector<UID>* outOverflowTargetPages, std::vector<UID>* outOverflowSourcePages)
{
	outTargetPages.clear();
	outSourcePages.clear();
	if (outOverflowTargetPages) outOverflowTargetPages->clear();
	if (outOverflowSourcePages) outOverflowSourcePages->clear();
	if (targetDB == nil || sourceDB == nil)
		return;

	std::vector<UID> tFlat, sFlat;
	KESCMCollectPageUIDs(targetDB, tFlat);
	KESCMCollectPageUIDs(sourceDB, sFlat);

	std::vector<UID> tFiltered, sFiltered;
	tFiltered.reserve(tFlat.size());
	sFiltered.reserve(sFlat.size());
	for (size_t i = 0; i < tFlat.size(); ++i)
		if (!KESCMPageMapIsRegistered(targetDB, tFlat[i]))
			tFiltered.push_back(tFlat[i]);
	for (size_t i = 0; i < sFlat.size(); ++i)
		if (!KESCMPageMapIsRegistered(sourceDB, sFlat[i]))
			sFiltered.push_back(sFlat[i]);

	const size_t n = (tFiltered.size() < sFiltered.size()) ? tFiltered.size() : sFiltered.size();
	outTargetPages.assign(tFiltered.begin(), tFiltered.begin() + n);
	outSourcePages.assign(sFiltered.begin(), sFiltered.begin() + n);
	if (outOverflowTargetPages && tFiltered.size() > n)
		outOverflowTargetPages->assign(tFiltered.begin() + n, tFiltered.end());
	if (outOverflowSourcePages && sFiltered.size() > n)
		outOverflowSourcePages->assign(sFiltered.begin() + n, sFiltered.end());
}

//========================================================================================
// KESCMBuildMasterPairing(KESCMPageMap.h で宣言)
//   マスタースプレッドどうしを名前で対応付け、一致した組のページを順に並べる(2026-08-11)。
//   ★上の KESCMBuildPairing とは対応の規則が違う(あちらは順番、こちらは名前)ので別関数。理由は
//   ヘッダーのコメント参照。★登録済み(比較相手なし)ページの除外はしない: 登録はページパネルの
//   選択から作られ、その読み口(KESCMPageMapReadSelection)を Register だけは includeMasters=kFalse で
//   呼ぶので、マスターページが登録集合に入ることはない。
//   ⚠**この前提はこの関数の正しさの土台**: ここは登録集合を一度も見ないので、もし Register が
//   マスターを受け付けるようになったら「登録したのに比較から外れない」という嘘になる
//   (2026-08-13 に Check/Refresh だけマスターを通す形にしたときも、Register は kFalse のまま据え置いた)。
//
//   ★Source 側の名前引きは公式 API IMasterSpreadList::FindMasterByName(prefix, basename)
//   (IMasterSpreadList.h:138)。自前で全マスターを回して名前を突き合わせる形も書けるが、
//   「名前で master を引く」ものが公式にある以上そちらが正道。
//   ⚠この API は SDK にも source/open にも呼び手がゼロで、ヘッダーは「見つからなかったとき何を
//   返すか」を書いていない。ここでは kInvalidUID を想定しつつ、返った UID は必ず ISpread として
//   開いて nil を弾く(壊れた値が来ても落ちない形)。相手のいないマスターでの実挙動は実機で確認する。
//========================================================================================
void KESCMBuildMasterPairing(IDataBase* targetDB, IDataBase* sourceDB,
	std::vector<UID>& outTargetPages, std::vector<UID>& outSourcePages)
{
	outTargetPages.clear();
	outSourcePages.clear();
	if (targetDB == nil || sourceDB == nil)
		return;

	InterfacePtr<IMasterSpreadList> tList(targetDB, targetDB->GetRootUID(), UseDefaultIID());
	InterfacePtr<IMasterSpreadList> sList(sourceDB, sourceDB->GetRootUID(), UseDefaultIID());
	if (tList == nil || sList == nil)
		return;

	// Target 側をマスタースプレッド順に回し、同名の Source があれば組む。
	const int32 tn = tList->GetMasterSpreadCount();
	for (int32 i = 0; i < tn; ++i)
	{
		const UID tu = tList->GetNthMasterSpreadUID(i);
		InterfacePtr<IMasterSpread> tms(targetDB, tu, UseDefaultIID());
		if (tms == nil)
			continue;

		// ★名前は prefix("A")と basename("親ページ"/"Master")に分けて聞く。GetName() が返す
		//   "A-親ページ" を自分で割る必要はない(公式 API がこの2つを受け取る形になっている)。
		PMString prefix, basename;
		tms->GetPrefix(&prefix);
		tms->GetBasename(&basename);

		const UID su = sList->FindMasterByName(prefix, basename);
		if (su == kInvalidUID)
			continue;			// 相手なし: このマスターは比較しない

		InterfacePtr<ISpread> tsp(targetDB, tu, UseDefaultIID());
		InterfacePtr<ISpread> ssp(sourceDB, su, UseDefaultIID());
		if (tsp == nil || ssp == nil)
			continue;
		const int32 tp = tsp->GetNumPages();
		const int32 sp = ssp->GetNumPages();
		const int32 np = (tp < sp) ? tp : sp;	// ページ数が違う組は短い方に切り詰める
		for (int32 p = 0; p < np; ++p)
		{
			outTargetPages.push_back(tsp->GetNthPageUID(p));
			outSourcePages.push_back(ssp->GetNthPageUID(p));
		}
	}
}

// (KESCMPageMapHasOverflow は 2026-07-25 監査で削除: 描画側が sOverflowT/sOverflowS キャッシュ方式へ
//  移行して以来どこからも呼ばれておらず、中身が全ページ走査なので誤ってホットパスから呼ばれる前に撤去)

//========================================================================================
// KESCMMapTargetToSource / KESCMMapSourceToTarget(KESCMPageMap.h で宣言)
//   1ページ単位の対応変換。内部で KESCMBuildPairing を呼んで対応表を作り、探しているページを
//   線形探索で引く(ページ数は高々数百なので毎回作り直しても軽い。呼び出し側は既に1スプレッド分
//   =数ページの粒度でしか呼ばないため実測コストも小さい)。
//
// ★★2026-08-16: **マスタースプレッドのページも引けるようにした**(ユーザー報告＝マスターページで
//   CMYK が出ない)。通常ページの対応表で見つからなかったときだけ、マスターの対応表
//   (KESCMBuildMasterPairing＝名前対応)も引く。
//   ⚠**2つの表を1つに混ぜてよい**理由＝ページ UID は文書内で一意なので、同じ UID が両方の表に
//     現れることはない。**比較の対応表(KESCMCore.cpp)と部分再比較(KESCMPeek.cpp)が先に同じ形を
//     取っている**(1つの std::map に通常とマスターを同居させる)ので、流儀も揃う。
//   ⚠**順番は通常が先**＝通常ページで引けるならマスターの表を作らずに済む(こちらが常用経路)。
//========================================================================================
bool16 KESCMMapTargetToSource(IDataBase* targetDB, IDataBase* sourceDB,
	UID targetPageUID, UID& outSourcePageUID)
{
	outSourcePageUID = kInvalidUID;
	std::vector<UID> tPages, sPages;
	KESCMBuildPairing(targetDB, sourceDB, tPages, sPages);
	for (size_t i = 0; i < tPages.size(); ++i)
	{
		if (tPages[i] == targetPageUID)
		{
			outSourcePageUID = sPages[i];
			return kTrue;
		}
	}
	// ★マスタースプレッドのページ(名前対応)。通常で引けなかったときだけ作る。
	std::vector<UID> mT, mS;
	KESCMBuildMasterPairing(targetDB, sourceDB, mT, mS);
	for (size_t k = 0; k < mT.size(); ++k)
	{
		if (mT[k] == targetPageUID)
		{
			outSourcePageUID = mS[k];
			return kTrue;
		}
	}
	return kFalse;
}

bool16 KESCMMapSourceToTarget(IDataBase* targetDB, IDataBase* sourceDB,
	UID sourcePageUID, UID& outTargetPageUID)
{
	outTargetPageUID = kInvalidUID;
	std::vector<UID> tPages, sPages;
	KESCMBuildPairing(targetDB, sourceDB, tPages, sPages);
	for (size_t i = 0; i < sPages.size(); ++i)
	{
		if (sPages[i] == sourcePageUID)
		{
			outTargetPageUID = tPages[i];
			return kTrue;
		}
	}
	// ★マスタースプレッドのページ(上と対称)。
	std::vector<UID> mT, mS;
	KESCMBuildMasterPairing(targetDB, sourceDB, mT, mS);
	for (size_t k = 0; k < mS.size(); ++k)
	{
		if (mS[k] == sourcePageUID)
		{
			outTargetPageUID = mT[k];
			return kTrue;
		}
	}
	return kFalse;
}

// KESCMPageMap.cpp 終わり。
