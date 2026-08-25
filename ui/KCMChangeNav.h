//========================================================================================
//
//  KCMChangeNav.h
//
//  「見るべき箇所」を順に巡回するナビゲーション(パネルの ◀ Prev / Next ▶ ボタンの実体)。
//  巡回対象(2026-08-24 現行化。実装は KCMChangeNav.cpp の KCMBuildStops が正):
//    ① 変更あり(赤/青リング)のページ = sEntries にキーがある(**Pixel モードで**比較 Start 中)
//    ② overset「+」箇所 = Find Overset の sOversetLocs(未 Start でも単独巡回可。1箇所=1ストップ)
//    ③ ★**Story Edits 一覧の葉**(2026-08-24 追加。**Story モードで**比較 Start 中)＝1つの編集、
//       または子を持たない行そのもの。**子のある行は含めない**(規則と理由＝KCMStoryNav.h)。
//  ※Added/Removed(登録・緑「/」)と Overflow(赤「/」)は巡回対象に含めない(2026-07-10 ユーザー指定)。
//  ※①と③は**排他**＝モードで決まる(Story モードは1枚もラスタ化しないので①は元から0件)。②はどちらの
//    モードでも Find Overset が ON なら**末尾に続く**。
//  変更ページはズームを変えずページ中心へ、overset は「+」点へスクロールするだけ(選択はしない)。
//  ③だけは飛び方が違い、**一覧の行をクリックしたときと同じ実装**を呼ぶ(ジャンプ＋一瞬のマーク＋
//  メッセージ欄。KCMStoryNav.cpp → KCMStoryJump.cpp)。
//
//========================================================================================

#ifndef __KCMChangeNav_h__
#define __KCMChangeNav_h__

#include "BaseType.h"	// bool16
#include "OMTypes.h"	// UID

class IDataBase;
class IControlView;

// view が spreadUID を映していなければ、公式コマンド kSetSpreadCmdBoss で切り替える(2026-08-11 に
// 1ビュー単位で括り出して公開)。既に映していれば何もしないので、何度呼んでも安い。
// ★スクロールだけでは別スプレッド(とくにマスタースプレッド)へは届かない＝空のペーストボードに
//   着地する。「違うスプレッドなら切り替える」は公式の作法(手本 SnapTracker.cpp:224 に特例なし)。
// ★Prev/Next(この .cpp 内)と、レイアウトビュー同期(**KCMViewSync.cpp**)の両方が呼ぶ＝同じ判断を
//   2か所に書かないため([[one-question-one-place]])。
// 戻り値: 実際に切り替えたら kTrue(既に映していた・失敗した場合は kFalse)。
// ⚠2026-08-19(不具合再検査 B-U8)訂正＝上は「KCMPeek.cpp」と書いていたが、同期エンジンは 2026-08-13 の
//   model/UI 分割で出て行っている(呼び所は KCMViewSync.cpp の KCMSyncOtherDocViewportsTo)。
//   ★**同じ主張を .cpp 側は 2026-08-17 に訂正済みで、この .h だけが残っていた**＝1本直したときに
//   同じ形の兄弟を探さなかった型([[verify-claims-in-comments]] の「近い兄弟ほど残る」)。
bool16 KCMEnsureViewShowsSpread(IControlView* view, IDataBase* db, UID spreadUID);

// 次/前の「見るべきページ」へレイアウトビューをスクロールする。未 Start(sDB==nil)や対象0件のときは
// スクロールせず、パネルのステータス行にその旨を出すだけ(安全に何度でも呼べる)。
void KCMGotoNextChange();
void KCMGotoPrevChange();

// 巡回の基準点(直近ページ)を忘れる。比較の Start(全再比較=対象文書入れ替え)と Stop で呼ぶ。
// ★UID はデータベース単位なので、別文書で再 Start したときに旧文書のページ UID が偶然一致して
// 誤った位置から巡回が始まるのを防ぐ(セッションを跨いだ基準点の持ち越しを断つ)。
void KCMResetNav();

// Prev/Next の間の現在位置表示を「今の変更ページ集合＋巡回基準点」から作り直してパネルへ送る。
// KESCL の UpdateNavWidgets と同じ発想で、Next/Prev を押さなくても状態変化に追従させるために、
// 変更ページ集合が変わり得るすべての契機から呼ぶ。
// ⚠2026-08-19(不具合再検査 B-U8)訂正＝ここには呼び手を4つ名指ししてあったが、**3つは分割で失効していた**
//   (KCMDoMarkChangesDoc / KCMRefreshComparisonForSelectedPages / KCMDoClearMarks は
//   いずれも model 側＝別 .pln のこの UI 関数を呼べない)。今の呼び手は全数 Grep で次のとおり:
//     ・KCMModelChangeObserver … 比較の再構築/消去の通知、あふれ走査の通知、**Story Edits 一覧の
//       作り直しの通知**(2026-08-24 追加＝Refresh Story Comparison で子の数が変わると N が変わる)
//     ・KCMActionComponent     … Find Overset を OFF にしたとき(巡回対象からあふれを外す)
//     ・KCMPanelObserver       … パネルの表示内容を作り直すとき(KCMApplyPanelInfo。4つ目だけ生きていた)
//     ・KCMChangeNav.cpp 自身  … 巡回の各出口(3か所)
// 表示規則:
//   ・巡回対象の文書が無い          → 空(＝未 Start **かつ** Find Overset も OFF)
//   ・対象文書あり・ストップ 0 件   → "/"
//   ・対象文書あり・N 件(未巡回)    → "1/N"(Start 直後に即表示)
//   ・k 番目を巡回中                → "k/N"
// ⚠同じ 2026-08-19 の訂正＝旧記述は「未 Start(比較なし)→空」だったが、**未 Start でも Find Overset が
//   ON なら "1/N" が出る**(巡回対象はあふれ箇所)。.cpp 側(KCMRefreshNavPosition の末尾)は
//   「未 Start かつ overset 無し」と正しく書いており、ここでも .h だけが古かった。
void KCMRefreshNavPosition();

// ★★★Story Edits の行へ「今立った」ことを巡回位置(k/N)に反映する(2026-08-24 ユーザー要望
//   「StoryEdit の行を選択した時も Prev のほうに連動しないと違和感」)。
//
// ★**呼び手は2本のジャンプ関数だけ**＝`KCMStoryJumpToRow` と `KCMStoryJumpToChange`
//   (KCMStoryJump.cpp。2026-08-25 の再検査で全数 Grep して確認＝この2つ以外に呼び手は無い)。
//   行のクリックも、矢印キーの歩きも、Prev/Next の巡回も、**行きたい先が決まったら必ずその2本のどちらかを
//   通る** ∴「今どのストップに立っているか」を決める場所は1つで済む([[one-question-one-place]])。
//   ⚠Prev/Next 側でも別に覚えさせると、同じ行について2つの答えが出る。
//
// 引数の意味は3通りで、**3つ目がこの関数の要**:
//   ・changeIndex >= 0            … その変更に立つ(そのままストップ)
//   ・changeIndex < 0 で子が無い行 … その行に立つ(行そのものがストップ)
//   ・changeIndex < 0 で子が有る行 … ★**その最初の子の「入口」に立つ**＝表示はその子の番号だが、
//     **まだそこへは行っていない**。次に Next を押すとその子へ行く(飛ばさない)／Prev を押すと
//     1つ前のストップへ行く。⇒ **比較を Start した直後に「1/N」と出るのとまったく同じ規則**
//     （表示＝次に Next で行く先。ユーザー決定 2026-08-24）。子のある親行は巡回対象では無い
//     （KCMStoryNav.h）ので、立てる場所がここしか無い。
//
// ⚠**Pixel モードでは何もしない。** あちらの巡回対象はページで、一覧の行はその列に居ない
//   ---- 触ると「行をクリックしたらページの巡回位置が飛ぶ」ことになる。
// ⚠rowIndex が今の一覧の外なら何もしない(一覧が作り直された直後のクリック)。
void KCMNoteStoryStop(int32 rowIndex, int32 changeIndex);

// Story Edits の行から呼ぶジャンプ: そのストーリーの先頭フレームを画面中央に出す。
//   ・frameUID のスプレッドを先に出すので、別スプレッドでもマスターでもペーストボードでも届く
//   ・★★Source 窓も連れて行くが、合わせるのは**ページではなく同じストーリー**(storyUID)＝2つの版で
//     そのストーリーが違う場所にあっても、両方の窓が同じストーリーを映す。Prev/Next が対応表で
//     ページを引くのとは意図して違う(2026-08-10 ユーザー指摘)。Source に無いストーリー(Added)なら
//     Target だけが動く。⚠「Sync Layout Views」が ON のときは Source を手動で動かさない
//     (Sync が Target のスクロールを運ぶので二重になる)
//   ・Pages パネルは両側とも追随する(pageUID がその解決に要る。kInvalidUID＝ページに載っていない
//     フレームなら Pages パネルは動かない)
//   ・★Prev/Next の巡回位置「k/N」には影響しない(別の動線なので基準点を動かさない)
// 戻り値: 1つでもビューをスクロールできたら kTrue。実体は KCMChangeNav.cpp。
//
// ★★focusIndex / sourceFocusIndex (2026-08-22 ユーザー要望)＝渡すと「ストーリーの書き出し」ではなく
//   **その文字**(キャレットが立つ位置)を画面中央に置く。★**両側を別々に受ける**のは、同じ編集でも
//   新旧で文字位置が違うから＝Change の fTargetStart / fSourceStart をそのまま渡す。
//   ⚠kInvalidTextIndex(既定)なら従来どおりストーリーの書き出しへ＝**親のストーリー行はこれで呼ぶ**
//     (行が指しているのがストーリーそのものなので)。
//   ⚠**呼び手は db(新側)に IDataBase::SaveRestoreModifiedState を持つこと**＝点を出すのに組版が要り、
//     組版は文書を dirty にする(IKCMStoryEditsFacade::GetStoryPointAt)。
//     ★**旧側のガードはこの関数が自分で持つ**＝旧文書に触るのはここだけなので。
bool16 KCMGotoStoryFrame(IDataBase* db, UID frameUID, UID pageUID, UID storyUID,
	TextIndex focusIndex = kInvalidTextIndex, TextIndex sourceFocusIndex = kInvalidTextIndex);

#endif // __KCMChangeNav_h__
