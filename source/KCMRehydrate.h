//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Task Start
//
//  INX -> a windowless document. ★IINXManager::ImportINX IS CALLED HERE AND NOWHERE ELSE.
//
//  Everything below was measured on 2026-09-12 (docs/ai-notes/kcm-inx-rehydration-2026-09-12.md),
//  because the SDK contains not one caller of ImportINX:
//    - the parent is a NEW windowless document's IDOMElement, and the policy is
//      kDocElementImportBoss. kActionImportPolicyBoss - the export's counterpart by name - puts
//      nothing in; kSaveBackImportPolicyBoss (IDML's) takes InDesign down; ISnippetImport refuses
//      the stream before any policy is consulted (kSnippetWrongDocType: the PI says type="action").
//    - the XML is injected first (KCMXmlInject.h): without the sacrificial range the first
//      ParagraphStyleRange of every story is lost.
//    - the result is CHECKED against the origin's shape. A short rehydration is a failure, never
//      a smaller document - the comparison must not be handed one.
//
//  Closing: IDocFileHandler::Close with kProcess, as KCMBookCompare closes its windowless chapters.
//  ⚠★★★A CLOSE UNDER AN OUTSTANDING REFERENCE IS A PROTECTIVE SHUTDOWN, NOT A CRASH: InDesign ends
//  the process itself, without an exception, and writes "CloseDocCmd - document is still
//  referenced / Document has N extra references" to InDesign Recovery/ProtectiveShutdownLog -
//  the only record there is (no crash watch sees it). Measured twice on 2026-09-12: the failure
//  paths of the rehydration closed the document while `parent`, `importedHolder` and the policy
//  were still in scope. That is what the throwaway probe had hit too ("crashed from inside a
//  script property call" was the wrong reading - the context was innocent). The rule that came
//  out of it: KCMCloseRehydrated is called only after every InterfacePtr on the document has gone
//  (KCMRehydrate.cpp, ImportAndCheck). The callers here are menu actions and the tool's press.
//  ★ONE CALLER IS NEITHER: the close sweep
//  (kAfterCloseDoc), where the origin's own document has just closed and the peek document goes
//  with it. Closing a document from inside another document's close responder is not a place
//  anything in the SDK does, so that caller asks for `deferred` = IDocFileHandler::kSchedule,
//  the handler's default mode, and the close runs after the responder has returned.
//
//  THE DOCUMENT IS LEFT CLEAN. A rehydration is a New + an import (+ deleted spreads, for the
//  peek), which dirties the document. It is marked unmodified at the end on purpose: it is ours,
//  there is nothing in it to save, and an untitled dirty document with no window is exactly what
//  a Quit's close-all would stop at with "Save changes?" - about a document the reader never
//  made. Unmodified, the close-all takes it silently (⚠unmeasured on a real Quit; reasoned from
//  how a fresh untitled document closes).
//
//========================================================================================
#ifndef __KCMRehydrate_h__
#define __KCMRehydrate_h__

#include "BaseType.h"
#include "PMString.h"
#include "UIDRef.h"

class IDataBase;
class KCMResourceBytes;
struct KCMOriginShape;

/** Rehydrate inx into a fresh windowless document.
    @param expect  the shape the origin had; the result must match it or it is closed and refused.
    @param outDoc  the document, when kTrue. Windowless, untitled, in app.documents.
    @param whyNot  when kFalse, the step that failed. */
bool16 KCMRehydrate(const KCMResourceBytes& inx, const KCMOriginShape& expect, UIDRef& outDoc, PMString& whyNot);

/** THE TEST INSTRUMENT (2026-09-12, the user's ask: "materialise the XML as it is, touching
    nothing"): a new windowless document with inx imported into it UNTOUCHED - no sacrificial
    range, no deletion, no compose, no shape check, no clean mark. It is how the import's own
    behaviour (the first range of a story going missing, KCMXmlInject.h) is looked at on the real
    application. The caller gives it a window; the document is the reader's to close.
    kFalse with a reason when the import failed; nothing is left open then. */
bool16 KCMRehydrateRaw(const KCMResourceBytes& inx, UIDRef& outDoc, PMString& whyNot);

/** Close a document this file made. Nothing else may be handed to it. A document that is already
    gone, or UIDRef::gNull, is ignored.
    @param deferred kTrue schedules the close (IDocFileHandler::kSchedule) instead of running it
           now - for the one caller that is inside a close responder (see the head of this file). */
void KCMCloseRehydrated(const UIDRef& doc, bool16 deferred = kFalse);

/** Mark a rehydrated document unmodified (see the head of this file). The peek calls it again
    after cutting the copy down; nil-safe. */
void KCMMarkRehydratedClean(IDataBase* db);

/** The KcmOriginUid label of uid in db (a story or a spread of a rehydrated document), as the
    ORIGINAL uid. kFalse when there is no such label or it does not parse. */
bool16 KCMReadOriginUidLabel(IDataBase* db, UID uid, UID& outOriginal);

#endif // __KCMRehydrate_h__

// End, KCMRehydrate.h.
