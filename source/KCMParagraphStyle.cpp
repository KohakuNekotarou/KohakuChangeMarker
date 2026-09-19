//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMParagraphStyle.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IAttributeStrand.h"
#include "ICommand.h"
#include "IDocument.h"
#include "IStyleGroupHierarchy.h"
#include "IStyleGroupManager.h"
#include "IStyleInfo.h"
#include "ITextModel.h"
#include "ITextModelCmds.h"
#include "ITextStoryThread.h"

#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "TextChar.h"
#include "TextID.h"
#include "TextIterator.h"

#include "KCMParagraphStyle.h"

//----------------------------------------------------------------------------------------
UID KCMParagraphStyleAt(ITextModel* model, TextIndex at)
{
	if (model == nil || at < 0)
		return kInvalidUID;
	// The paragraph strand keeps the applied style per paragraph run - the road SnpManipulateTextStyle
	// takes (QueryStrand(kParaAttrStrandBoss, IID_IATTRIBUTESTRAND)).
	InterfacePtr<IAttributeStrand> strand(static_cast<IAttributeStrand*>(
		model->QueryStrand(kParaAttrStrandBoss, IID_IATTRIBUTESTRAND)));
	if (strand == nil)
		return kInvalidUID;
	int32 count = 0;
	return strand->GetStyleUID(at, &count);
}

//----------------------------------------------------------------------------------------
UID KCMNextParagraphStyle(IDataBase* db, UID style)
{
	if (db == nil || style == kInvalidUID)
		return style;
	InterfacePtr<IStyleInfo> info(db, style, UseDefaultIID());
	if (info == nil)
		return style;
	const UID next = info->GetNextStyle();
	return (next == kInvalidUID) ? style : next;
}

//----------------------------------------------------------------------------------------
ErrorCode KCMApplyNextStyleAfter(ITextModel* model, TextIndex prevAt, TextIndex newStart, int32 newLength)
{
	if (model == nil || newLength <= 0)
		return kSuccess;
	IDataBase* db = ::GetDataBase(model);
	const UID previous = KCMParagraphStyleAt(model, prevAt);
	if (previous == kInvalidUID)
		return kSuccess;					// nothing to go by: the new paragraphs keep what they inherited
	const UID first = KCMNextParagraphStyle(db, previous);
	if (first == previous)
		return kSuccess;					// [Same Style] all the way down: inherited, overrides and all

	InterfacePtr<ITextModelCmds> cmds(model, UseDefaultIID());
	if (cmds == nil)
		return kFailure;
	// ★autoNextStyle: the first paragraph gets `first`, the second first's next style, and so on - what
	//   pressing Return again and again does. replaceOverrides: the previous paragraph's hand adjustments
	//   are not carried into a paragraph of another style.
	InterfacePtr<ICommand> apply(cmds->ApplyStyleCmd(newStart, newLength, first, kParaAttrStrandBoss,
													 kTrue /*replaceOverrides*/, kTrue /*autoNextStyle*/));
	if (apply == nil)
		return kFailure;
	const ErrorCode err = CmdUtils::ProcessCommand(apply);
	if (err != kSuccess)
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	return err;
}

//----------------------------------------------------------------------------------------
void KCMSnapshotChainAfter(ITextModel* model, TextIndex removedFrom, TextIndex removedTo, KCMChainAfter& out)
{
	out.fStarts.clear();
	out.fChained.clear();
	if (model == nil || removedTo <= removedFrom)
		return;

	TextIndex threadStart = 0;
	int32 threadSpan = 0;
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(removedFrom, &threadStart, &threadSpan));
	if (thread == nil)
		return;
	const TextIndex threadEnd = threadStart + threadSpan;		// past the thread's own last return
	if (removedTo >= threadEnd)
		return;

	IDataBase* db = ::GetDataBase(model);
	// The "before" of the first following paragraph is the one being taken out: its style is read off
	// its last character, which both removal shapes hold inside [removedFrom, removedTo).
	UID previous = KCMParagraphStyleAt(model, removedTo - 1);

	// Every paragraph after the removal: the first starts at removedTo - or one further on when the
	// character at removedTo is the removed paragraph's OWN return ("\rTEXT" leaves that return behind
	// as the paragraph before's; measured 2026-09-19: read as a paragraph of its own it was "not chained"
	// and stopped the walk at once, so nothing was ever re-styled) - and each character after a return
	// from there, except the thread's final return, which ends the last paragraph.
	TextIndex start = removedTo;
	{
		TextIterator at(model, removedTo);
		if (static_cast<int32>((*at).GetValue()) == kTextChar_CR)
			start = removedTo + 1;
	}
	if (start >= threadEnd)
		return;
	TextIterator iter(model, start);
	for (TextIndex i = start; i < threadEnd; ++i, ++iter)
	{
		const int32 cp = static_cast<int32>((*iter).GetValue());
		if (cp != kTextChar_CR)
			continue;
		const UID own = KCMParagraphStyleAt(model, start);
		out.fStarts.push_back(start);
		out.fChained.push_back((own != kInvalidUID && own == KCMNextParagraphStyle(db, previous)) ? kTrue : kFalse);
		previous = own;
		start = i + 1;
	}
}

//----------------------------------------------------------------------------------------
ErrorCode KCMRechainAfterRemoval(ITextModel* model, TextIndex removedFrom, int32 removedCount,
								 const KCMChainAfter& chain, bool16 returnBefore)
{
	if (model == nil || chain.fStarts.empty())
		return kSuccess;
	TextIndex threadStart = 0;
	int32 threadSpan = 0;
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(removedFrom, &threadStart, &threadSpan));
	if (thread == nil)
		return kSuccess;

	// Where the paragraph now standing before the chain is read. "\rTEXT" took the return of that
	// paragraph and left TEXT's return in its place, AT removedFrom - and a paragraph is its return,
	// so that is where an empty one is found too (the header says what reading removedFrom - 1
	// missed). "TEXT\r" at the start of its place has nothing before the chain at all.
	TextIndex prevAt = 0;
	if (returnBefore)
	{
		if (removedFrom < threadStart)
			return kSuccess;
		prevAt = removedFrom;
	}
	else
	{
		if (removedFrom <= threadStart)
			return kSuccess;			// the first paragraph of its place went: nothing stands before the chain
		prevAt = removedFrom - 1;
	}

	IDataBase* db = ::GetDataBase(model);
	ErrorCode err = kSuccess;
	for (size_t i = 0; i < chain.fStarts.size(); ++i)
	{
		if (!chain.fChained[i])
			break;						// chosen by hand: the chain ends here, and so does the walk
		const TextIndex at = chain.fStarts[i] - removedCount;
		const UID previous = KCMParagraphStyleAt(model, prevAt);
		const UID wanted = KCMNextParagraphStyle(db, previous);
		const UID own = KCMParagraphStyleAt(model, at);
		if (wanted != kInvalidUID && own != wanted)
		{
			const ErrorCode e = KCMApplyParagraphStyle(model, at, 1, wanted, kFalse /*keep its overrides*/);
			if (e != kSuccess)
				err = e;
		}
		prevAt = at;
	}
	return err;
}

//----------------------------------------------------------------------------------------
PMString KCMParagraphStylePath(IDataBase* db, UID style)
{
	PMString path;
	path.SetTranslatable(kFalse);
	if (db == nil || style == kInvalidUID)
		return path;
	InterfacePtr<IStyleGroupHierarchy> node(db, style, IID_ISTYLEGROUPHIERARCHY);
	if (node == nil)
		return path;
	path = node->GetFullPath();
	path.SetTranslatable(kFalse);
	return path;
}

//----------------------------------------------------------------------------------------
UID KCMFindParagraphStyle(IDataBase* db, const PMString& path)
{
	if (db == nil || path.IsEmpty())
		return kInvalidUID;
	InterfacePtr<IDocument> document(db, db->GetRootUID(), UseDefaultIID());
	if (document == nil)
		return kInvalidUID;
	// The workspace's paragraph style manager - as hiddentext's HidTxtCommands asks it.
	InterfacePtr<IStyleGroupManager> styles(document->GetDocWorkSpace(), IID_IPARASTYLEGROUPMANAGER);
	if (styles == nil)
		return kInvalidUID;
	return styles->FindByName(path);
}

//----------------------------------------------------------------------------------------
ErrorCode KCMApplyParagraphStyle(ITextModel* model, TextIndex start, int32 length, UID style,
								 bool16 replaceOverrides)
{
	if (model == nil || style == kInvalidUID)
		return kFailure;
	InterfacePtr<ITextModelCmds> cmds(model, UseDefaultIID());
	if (cmds == nil)
		return kFailure;
	InterfacePtr<ICommand> apply(cmds->ApplyStyleCmd(start, length, style, kParaAttrStrandBoss,
													 replaceOverrides, kFalse /*autoNextStyle*/));
	if (apply == nil)
		return kFailure;
	const ErrorCode err = CmdUtils::ProcessCommand(apply);
	if (err != kSuccess)
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	return err;
}

//----------------------------------------------------------------------------------------
namespace
{

/* ParagraphPositions
   One position inside each of up to `count` consecutive paragraphs: `anchor` itself for the paragraph
   that holds it, then the character after each return, stopping at the thread's end (its final return
   ends the last paragraph and starts none). outThreadStart is where the thread begins, for a caller that
   wants to look at the paragraph BEFORE the first one.
*/
void ParagraphPositions(ITextModel* model, TextIndex anchor, int32 count,
						std::vector<TextIndex>& out, TextIndex& outThreadStart)
{
	out.clear();
	outThreadStart = 0;
	if (model == nil || count <= 0 || anchor < 0)
		return;
	TextIndex threadStart = 0;
	int32 threadSpan = 0;
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(anchor, &threadStart, &threadSpan));
	if (thread == nil)
		return;
	outThreadStart = threadStart;
	const TextIndex threadEnd = threadStart + threadSpan;
	if (anchor >= threadEnd)
		return;
	out.push_back(anchor);
	TextIterator iter(model, anchor);
	for (TextIndex i = anchor; i < threadEnd && static_cast<int32>(out.size()) < count; ++i, ++iter)
	{
		if (static_cast<int32>((*iter).GetValue()) != kTextChar_CR)
			continue;
		if (i + 1 >= threadEnd)
			break;						// the thread's final return: no paragraph follows it
		out.push_back(i + 1);
	}
}

}	// namespace

//----------------------------------------------------------------------------------------
void KCMReadParagraphStyles(ITextModel* model, TextIndex anchor, int32 count, std::vector<UID>& out)
{
	out.clear();
	std::vector<TextIndex> at;
	TextIndex threadStart = 0;
	ParagraphPositions(model, anchor, count, at, threadStart);
	for (size_t i = 0; i < at.size(); ++i)
		out.push_back(KCMParagraphStyleAt(model, at[i]));
}

//----------------------------------------------------------------------------------------
void KCMSnapshotChainFrom(ITextModel* model, TextIndex firstAt, TextIndex prevAt, KCMChainAfter& out)
{
	out.fStarts.clear();
	out.fChained.clear();
	if (model == nil || firstAt < 0)
		return;
	TextIndex threadStart = 0;
	int32 threadSpan = 0;
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(firstAt, &threadStart, &threadSpan));
	if (thread == nil)
		return;
	const TextIndex threadEnd = threadStart + threadSpan;
	if (firstAt >= threadEnd)
		return;
	IDataBase* db = ::GetDataBase(model);
	UID previous = (prevAt >= threadStart && prevAt < firstAt) ? KCMParagraphStyleAt(model, prevAt) : kInvalidUID;

	TextIndex start = firstAt;
	TextIterator iter(model, start);
	for (TextIndex i = start; i < threadEnd; ++i, ++iter)
	{
		if (static_cast<int32>((*iter).GetValue()) != kTextChar_CR)
			continue;
		const UID own = KCMParagraphStyleAt(model, start);
		out.fStarts.push_back(start);
		out.fChained.push_back((previous != kInvalidUID && own != kInvalidUID
								&& own == KCMNextParagraphStyle(db, previous)) ? kTrue : kFalse);
		previous = own;
		start = i + 1;
	}
}

//----------------------------------------------------------------------------------------
ErrorCode KCMRestoreParagraphStyles(ITextModel* model, TextIndex anchor, const std::vector<UID>& remembered,
									const std::vector<UID>& asLeft, int32 firstDerived,
									const KCMChainAfter& followersBefore)
{
	if (model == nil)
		return kSuccess;
	IDataBase* db = ::GetDataBase(model);

	// The paragraphs to look at: the one before (when firstDerived says there is one), the one put back,
	// and every follower either the record or the chain snapshot knows about.
	const int32 xIndex = (firstDerived > 0) ? 1 : 0;
	const int32 byChain = xIndex + 1 + static_cast<int32>(followersBefore.fChained.size());
	const int32 byRecord = static_cast<int32>(remembered.size());
	std::vector<TextIndex> at;
	TextIndex threadStart = 0;
	ParagraphPositions(model, anchor, (byChain > byRecord) ? byChain : byRecord, at, threadStart);

	ErrorCode err = kSuccess;
	bool16 recordStands = kTrue;		// goes kFalse at the first follower the record no longer describes
	for (size_t i = 0; i < at.size(); ++i)
	{
		const UID current = KCMParagraphStyleAt(model, at[i]);
		const bool16 recorded = (recordStands && i < remembered.size()) ? kTrue : kFalse;
		// ★IS THIS STILL THE PARAGRAPH THE RECORD WAS MADE FOR? Only if it wears what the take-in left it
		//   wearing (the header says what went wrong without this test). The paragraph put back is not
		//   tested - asLeft holds kInvalidUID for it - because the write just gave it whatever it inherited.
		const bool16 agrees = (recorded && (i >= asLeft.size() || asLeft[i] == kInvalidUID || current == asLeft[i]))
							  ? kTrue : kFalse;

		// The paragraph above, as it stands NOW - restored a moment ago when it is one of ours. For the
		// first paragraph of the list `anchor` is its start (the take-in cut it there), so the one above
		// ends at anchor - 1, unless the thread begins here.
		UID previous = kInvalidUID;
		if (i > 0)
			previous = KCMParagraphStyleAt(model, at[i - 1]);
		else if (anchor - 1 >= threadStart)
			previous = KCMParagraphStyleAt(model, anchor - 1);
		UID next = kInvalidUID;			// what the paragraph above hands down, when it names one
		if (previous != kInvalidUID)
		{
			const UID n = KCMNextParagraphStyle(db, previous);
			if (n != previous)
				next = n;
		}

		UID wanted = kInvalidUID;
		if (static_cast<int32>(i) < xIndex)
		{
			// The paragraph before: what it wore, and only while it still looks the way it was left
			// (restyled by hand since, it is theirs to keep).
			if (recorded && agrees)
				wanted = remembered[i];
		}
		else if (static_cast<int32>(i) == xIndex)
		{
			// The paragraph put back: the first layer, then the second.
			wanted = (next != kInvalidUID) ? next : (recorded ? remembered[i] : kInvalidUID);
		}
		else
		{
			// A follower. The record, while it still describes what stands there; the chain from there on.
			if (recorded && !agrees)
				recordStands = kFalse;
			if (recorded && agrees)
			{
				wanted = (next != kInvalidUID) ? next : remembered[i];
			}
			else
			{
				const size_t k = i - static_cast<size_t>(xIndex) - 1;
				if (k >= followersBefore.fChained.size() || !followersBefore.fChained[k])
					break;				// chosen by hand (or unknown): the chain ends here, and so does the walk
				if (next == kInvalidUID)
					continue;			// chained, but the paragraph now above names no next style: left as it is
				wanted = next;
			}
		}

		if (wanted == kInvalidUID || current == wanted)
			continue;
		const ErrorCode e = KCMApplyParagraphStyle(model, at[i], 1, wanted, kFalse /*keep its overrides*/);
		if (e != kSuccess)
			err = e;					// a style that cannot be applied leaves the one it had
	}
	return err;
}

// End, KCMParagraphStyle.cpp.
