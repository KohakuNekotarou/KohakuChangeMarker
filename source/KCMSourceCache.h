//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - the Source side of a story, read once and kept
//
//  WHAT THIS WAS FOR. Comparing against a TASK START used to mean the Source was not an open
//  document at all: it was a byte string, and every press of "Restore Source Text" rebuilt a whole
//  document out of it - kNewDocumentCmdBoss, ImportINX, the page names - used it for a moment, and
//  closed it again. The reader felt that as the panel going away for a second on every single
//  change they took in (the user, 2026-09-16: "it is too heavy to work with").
//  ⛔**BOTH ENDS OF THAT ARE GONE NOW**: a Task Start became a saved document on 2026-09-21, so
//  nothing is rehydrated, and the restore went the same day, so nothing presses. What is left is a
//  cache that keeps nothing (KCMSourceCacheMayKeep answers kFalse) and the readers that ask it.
//
//  ★★★**THE CASE THAT IS SLOW IS THE CASE THAT IS SAFE TO KEEP.** When two documents are open and
//  armed, nothing is rehydrated - KCMArmedSourceDB hands back the real document and the press is
//  already cheap - but that Source can be EDITED by the reader, so nothing about it may be
//  remembered. The origin is the opposite on both counts: it is what costs a document to read,
//  and it cannot change while it is held, because it IS a fixed byte string. So this cache is
//  filled only from a rehydrated origin, and the path that cannot use it never needed it.
//
//  *** WHAT IS KEPT IS WHAT KCMTextRead ANSWERS, AND NOTHING ELSE. *** Three parallel arrays per
//  story: the paragraphs' text, their KCMParaAttrs (the ruby and kenten spans, the note markers,
//  and WHICH CELL OR FOOTNOTE each paragraph is) and each paragraph's TextIndex. No document, no
//  page, no UID of the copy's own - the copy's UIDs are new ones every time it is rehydrated,
//  which is exactly why the key here is the TARGET story's UID instead.
//
//  ⚠**THE POSITIONS ARE NOT OPTIONAL.** A caller that kept only the text would have to add the
//   paragraphs up to find one, and a table's cells are not between the paragraphs at all - they
//   live past the whole body (KCMParaText.h, IndexInStory says what that cost when it was got
//   wrong). The starts are read from the walk and kept as they came.
//
//  ***** WHEN IT IS DROPPED *****
//
//  ★**ONE PLACE, AND IT IS THE ORIGIN'S OWN LIFE**: whenever the held origin changes - taken,
//  parked, unparked, released - what was read from it is no longer about anything. Everything
//  else (a refresh, a write, taking a change in) leaves it alone on purpose: the origin has not
//  moved, so neither has its text, and re-reading it would be the very cost this file exists to
//  remove.
//
//========================================================================================
#ifndef __KCMSourceCache_h__
#define __KCMSourceCache_h__

#include "BaseType.h"
#include "UIDRef.h"
#include "WideString.h"		// the story's RAW text - see KCMSourceCacheGetRaw

#include <string>
#include <vector>

#include "KCMParaText.h"	// KCMParaAttrs - the shape KCMTextRead answers in
#include "KCMTableShape.h"	// KCMTableShape - the Source story's tables, kept with its text (2026-09-20)

/** What was read for one story, or kFalse when nothing has been kept for it.

	@param targetStoryUID the story in the READER'S document that this Source story is paired with
		- a uid that outlives any number of rehydrations, which the copy's own does not.
*/
bool16 KCMSourceCacheGet(UID targetStoryUID,
						 std::vector<std::string>& outParas,
						 std::vector<KCMParaAttrs>& outAttrs,
						 std::vector<int32>& outStarts);

/** Keep what was just read. ⚠**ONLY EVER CALLED FOR A REHYDRATED ORIGIN** - see the file header
	for why an armed Source document must not be kept.

	@param raw the whole story as the text model reads it, 0..TotalLength. ★**THE WRITE'S OWN
		COPY**: see KCMSourceCacheGetRaw for why the paragraphs above cannot serve that purpose. */
void KCMSourceCachePut(UID targetStoryUID,
					   const std::vector<std::string>& paras,
					   const std::vector<KCMParaAttrs>& attrs,
					   const std::vector<int32>& starts,
					   const WideString& raw);

/** The whole Source story as the text model reads it - every character, in TextIndex order.

	★★★**THIS EXISTS BECAUSE THE PARAGRAPHS CANNOT BE PUT BACK TOGETHER SAFELY.** "Restore Source
	  Text" writes the older words over a range named in the SOURCE's TextIndex space, and until
	  now it read them straight off the Source's text model. The paragraphs kept above are not the
	  same string: JoinParagraphs puts ONE character between two of them and the document's own is
	  a CR, the reader takes a table's anchor and continuation characters OUT, and a note's
	  reference is not in them at all. Re-assembling that would be a second answer to a question
	  the document has already answered - and the kind of second answer that writes LF where a
	  paragraph break belongs, silently.
	  ⇒ So the raw text is read ONCE, from the same API the write used to call (TextIterator over
	    the whole story), and the write takes its slice out of it by the very same indices.
	@return kFalse when nothing has been kept for that story. */
bool16 KCMSourceCacheGetRaw(UID targetStoryUID, WideString& outRaw);

/** Whether anything at all has been kept for that story - asked by the callers that decide
	whether a copy of the origin has to be rehydrated at all. */
bool16 KCMSourceCacheHas(UID targetStoryUID);

/** ★The Source story's TABLES, read in the same breath as its text (2026-09-20): the shape of each,
	which the Table row compares against the live one on every re-diff - without the copy. */
void KCMSourceCachePutTableShapes(UID targetStoryUID, const std::vector<KCMTableShape>& shapes);
bool16 KCMSourceCacheGetTableShapes(UID targetStoryUID, std::vector<KCMTableShape>& outShapes);

/** Whether anything may be kept at all right now: kTrue while a rehydrated origin is standing.
	Asked by the one place that fills the cache, so that the rule lives beside the cache rather
	than at each call site. */
bool16 KCMSourceCacheMayKeep();

/** Forget everything. Called from the origin's own life and from the model's shutdown - this
	static holds strings, which KCMStoryList.h states the rule for. */
void KCMSourceCacheClear();

#endif // __KCMSourceCache_h__

// End, KCMSourceCache.h.
