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
//  ⚠The throwaway probe crashed closing such a document from INSIDE A SCRIPT PROPERTY CALL; the
//  callers here are menu actions. If it still crashes, switch to IDocFileHandler::kSchedule (the
//  spec, section 11-1).
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

/** Close a document this file made. Nothing else may be handed to it. A document that is already
    gone, or UIDRef::gNull, is ignored. */
void KCMCloseRehydrated(const UIDRef& doc);

/** The KcmOriginUid label of uid in db (a story or a spread of a rehydrated document), as the
    ORIGINAL uid. kFalse when there is no such label or it does not parse. */
bool16 KCMReadOriginUidLabel(IDataBase* db, UID uid, UID& outOriginal);

#endif // __KCMRehydrate_h__

// End, KCMRehydrate.h.
