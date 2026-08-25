//========================================================================================
//
//  KCMHideUnchanged.h
//
//  The "Hide Unchanged Spreads" implementation: hiding the spreads that carry no marks and
//  restoring the ones we hid.
//
//  Split out of KCMActionComponent.cpp on 2026-08-13. Behaviour unchanged.
//
//  This is MODEL side: it issues kHideSpreadCmdBoss, i.e. it changes the document. The menu
//  item that turns the feature on and off stays on the UI side in KCMActionComponent.cpp.
//
//========================================================================================

#ifndef __KCMHideUnchanged_h__
#define __KCMHideUnchanged_h__

#include "BaseType.h"

class IDataBase;

// The flyout toggle "Hide Unchanged Spreads", both directions.
//
// OFF -> ON: confirm (the command writes to the document file), then collect every spread
// that carries no mark, no registered page and no overflow page, hide them with one
// kHideSpreadCmdBoss, and remember what was hidden. The Source document is then classified
// the same way through the pairing table and hidden too. ON -> OFF: show back exactly what
// we hid, on both sides, without asking.
//
// ★Both commands (Target and Source) run inside ONE CmdUtils::SequenceContext, in both
// directions -- a hide/restore that spans two documents must be one undo step. Splitting it
// per document is the shape that leaves one document stranded when the user hits Ctrl+Z
// (measured 2026-08-16: kHideSpreadCmdBoss pushes one undo step per call). API audit B10.
//
// ★Split out of KCMActionComponent::DoHideUnchangedToggle on 2026-08-13, body unchanged.
//  It moved as a whole because the five statics it writes (the toggle, and the database +
//  spread list for each side) are the same ones KCMResetHideUnchanged clears -- leaving
//  the writer on the UI side would have split that state across the boundary.
// ***** 保存されるときは隠しを解除する。***** (ユーザー決定 2026-08-19)
//   このトグルは永続変更(`kHideSpreadCmdBoss`)なので、隠したまま保存すると `.indd` に残り、
//   **隠れたスプレッドは印刷にも書き出しにも出ない**＝気づかず入稿するとページが丸ごと落ちる。
//   ⇒ `KCMBeforeSaveDocResponder`(KCMDocResponder.cpp) が **保存の直前に両側とも戻す**。
//   ★**別名保存(Save As)だけは対象外にしてあり、それが使い分けになる**＝
//     **上書き保存/閉じるときの保存＝解除して保存 ／ Save As＝隠したまま保存**（実測で確認済み）。
//   ⚠**「閉じる前(`kBeforeCloseDoc`)」では間に合わない**＝その時点で保存は既に終わっている
//     （診断ビルドで `IDataBase::IsModified()==0` を実測。詳細は KCMDocResponder.cpp 冒頭）。
//
// ★**これは Target と Source の食い違いも同時に消す**（ユーザー指摘 2026-08-19）。
//   従来は「先に閉じた側は隠れたまま保存され、あとに残った側は `KCMHandleDocsClosed` が
//   `KCMResetHideUnchanged(kTrue)` を呼ぶので隠れずに保存される」＝**保存が Stop より
//   先か後かで結果が割れていた**。保存の直前に必ず両側を戻すので、どちらの順で閉じても揃う。
//
// ***** 控えは Undo/Redo に追随しない。これは既知で、現状維持と決まっている。*****
//   (ユーザー決定 2026-08-19。**再提案しないこと。**)
//   `kHideSpreadCmdBoss` は **undo 可能な永続コマンド**だが、下の5つの static はそうではないので、
//   Undo を挟むと文書と控えが食い違う。2026-08-19 に両経路を実測した:
//     ・Hide ON → Undo …… 文書は戻る(`[-,-,-,-]`)のにトグルは ON のまま
//       ⇒ 実害小(トグルを押すと、既に表示されているものを再表示するだけ)
//     ・Hide ON → 保存 → Undo → Redo …… **文書は隠れる**のにトグルは OFF
//       ⇒ 控えが空なので**このトグルでは戻せない**(Pages パネルで手動、または再 Start)
//   ★**保存の解除コマンドが undo スタックに積まれない**ことが後者の機序(実測)。保存が undo 履歴を
//     汚さないのは良い性質だが、その分スタックとモデルが食い違い、Redo で表に出る。
//   ⚠**`undoName` は hide も unhide も同じ `スプレッドを隠す`** なので、名前を見ても何が積まれて
//     いるか分からない ---- 実際に Undo/Redo を走らせて初めて判明した。
//   ★公式には解がある(Snapshot interface = 非永続データを Undo/Redo に追随させる)が、
//     **起こる操作が稀なので採らない**と決めた。作り直すときだけ再検討する。
void		KCMHideUnchangedToggle();

// Reset the toggle on both sides. With restoreSpreads=kTrue the spreads we remember hiding
// are shown again with kHideSpreadCmdBoss(kFalse) before the state is dropped (deleted UIDs
// are skipped). Document liveness is checked internally by pointer comparison against
// IDocumentList, never by dereferencing, so passing kTrue is safe even when one side has
// been closed -- only the surviving side is restored. With kFalse the databases are not
// touched at all and only the state is dropped.
//
// Callers -- all FOUR of them (counted 2026-08-18, bug recheck B10):
//   1. re-comparison  KCMDoMarkChangesDoc  (KCMCore.cpp)  -- kTrue
//   2. Stop           KCMDoClearMarks      (KCMCore.cpp)  -- kTrue
//   3. the close sweep KCMHandleDocsClosed (KCMPeek.cpp)  -- kTrue, or kFalse while quitting
//   4. ★the model's Shutdown                (KCMPeek.cpp)  -- kFalse
// ⚠Number 4 was missing from this list, and it is the one the kFalse sentence above is written
// for: it is the only caller that ALWAYS passes kFalse. A header that explains an argument
// nobody in its own caller list ever passes is a header that has not been re-counted.
void		KCMResetHideUnchanged(bool16 restoreSpreads);

// kTrue while the toggle is ON, i.e. while spreads hidden by this feature are being held.
// UpdateActionStates asks for the check mark next to the menu item. (Split out on
// 2026-08-13: the flag itself lives in KCMHideUnchanged.cpp with the rest of the state.)
bool16		KCMGetHideUnchangedOn();

// The documents currently hiding spreads for this feature, or nil. The close sweep uses
// these for liveness checks (comparison against FindDocByDataBase only, never a deref).
IDataBase*	KCMGetHideUnchangedDB();
IDataBase*	KCMGetHideUnchangedSrcDB();

#endif // __KCMHideUnchanged_h__
