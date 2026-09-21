//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM)
//
//  A document this plug-in made, closed cleanly.
//
//  ⛔**THE REHYDRATION WENT ON 2026-09-21** - KCMRehydrate, its three modes, and the whole of
//  "INX -> a windowless document". It existed for Task Start, which saves a copy of the document
//  to a FILE now; Start opens that file, so nothing is rebuilt out of held bytes.
//  ★★**THE MEASUREMENTS IT CARRIED ARE KEPT** - how IINXManager::ImportINX has to be called, and
//  that the SDK contains not one caller of it; why the document under it cannot be
//  IDocumentCommands::New; that the import drops a whole text insertion from the second story in
//  file order, and a nested table's skeleton with it; that only <Page> loses its label while
//  spreads and stories keep theirs - in
//  **docs/ai-notes/kcm-rehydration-retired-2026-09-21.md**, and the code is one `git revert` away.
//  ⇒ **Read that note before writing an import again.**
//
//  WHAT IS LEFT HERE, AND WHY IT OUTLIVED THE ORIGIN:
//
//  Closing: IDocFileHandler::Close with kProcess, as KCMBookCompare closes its windowless chapters.
//  ⚠★★★A CLOSE UNDER AN OUTSTANDING REFERENCE IS A PROTECTIVE SHUTDOWN, NOT A CRASH: InDesign ends
//  the process itself, without an exception, and writes "CloseDocCmd - document is still
//  referenced / Document has N extra references" to InDesign Recovery/ProtectiveShutdownLog -
//  the only record there is (no crash watch sees it). Measured twice on 2026-09-12, on the failure
//  paths of the rehydration, which closed the document while `parent`, `importedHolder` and the
//  policy were still in scope. That is what the throwaway probe had hit too ("crashed from inside
//  a script property call" was the wrong reading - the context was innocent). The rule that came
//  out of it: **KCMCloseRehydrated is called only after every InterfacePtr on the document has
//  gone.**
//  ★ONE CALLER IS A SPECIAL CASE: the close sweep (kAfterCloseDoc), where a document has just
//  closed and one of ours goes with it. Closing a document from inside another document's close
//  responder is not a place anything in the SDK does, so that caller asks for `deferred` =
//  IDocFileHandler::kSchedule, the handler's default mode, and the close runs after the responder
//  has returned.
//
//  THE DOCUMENT IS LEFT CLEAN. A document this plug-in makes and fills is dirty, and it is marked
//  unmodified on purpose: it is ours, there is nothing in it to save, and an untitled dirty
//  document with no window is exactly what a Quit's close-all would stop at with "Save changes?" -
//  about a document the reader never made. Unmodified, the close-all takes it silently
//  (⚠unmeasured on a real Quit; reasoned from how a fresh untitled document closes).
//
//========================================================================================
#ifndef __KCMRehydrate_h__
#define __KCMRehydrate_h__

#include "BaseType.h"
#include "PMString.h"
#include "UIDRef.h"

class IDataBase;

/** Close a document this file made. Nothing else may be handed to it. A document that is already
    gone, or UIDRef::gNull, is ignored.
    @param deferred kTrue schedules the close (IDocFileHandler::kSchedule) instead of running it
           now - for the one caller that is inside a close responder (see the head of this file). */
void KCMCloseRehydrated(const UIDRef& doc, bool16 deferred = kFalse);

/** Mark a document this plug-in made unmodified (see the head of this file). Nil-safe. */
void KCMMarkRehydratedClean(IDataBase* db);

/** The KcmOriginUid label of `uid` in `db` (a story or a spread that carries one), as the ORIGINAL
    uid. kFalse when there is no such label or it does not parse.
    ⚠**The label itself is alive and well**: KCMXmlInject.h still writes one into every story and
     spread of an injected copy, and the Story import pairs its rows by it. Only the rehydration
     that used to consume them here has gone. */
bool16 KCMReadOriginUidLabel(IDataBase* db, UID uid, UID& outOriginal);

#endif // __KCMRehydrate_h__

// End, KCMRehydrate.h.
