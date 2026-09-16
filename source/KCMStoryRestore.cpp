//========================================================================================
//
//  KCMStoryRestore.cpp -- see the header.
//
//  THREE KINDS OF CHANGE, THREE WAYS BACK, ONE UNDO STEP EACH:
//   - words ........ ReplaceCmd / InsertCmd with the older words read raw from the Source;
//   - ruby ......... the older reading written as InDesign's three ruby attributes (on, the
//                    reading, mono/group) over the base characters - or, where the older side
//                    had none, the ruby attributes' overrides taken off. The shape is Kohaku
//                    InDesign MCP's ruby tool (KIDMCPRuby.cpp: ApplyOneRun / ClearRubyOn),
//                    carried over rather than reinvented;
//   - kenten ....... the older KIND written into kTAKentenKindBoss (Kenten_None where the older
//                    side had none) - the look (position, size, colour) is left as it is, the
//                    same way the ruby path leaves the ruby's look alone.
//  Every write is wrapped in one command sequence named "Restore Source Text", so a clear
//  followed by an apply is still one Ctrl+Z.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IAttrReport.h"			// what an attribute is applied as
#include "IClassIDData.h"			// which strand PrivateCreateStrandCmd is to make
#include "ICommand.h"
#include "ICommandSequence.h"		// one undo step for a clear followed by an apply
#include "IDataBase.h"
#include "IKentenStyle.h"			// IKentenStyle::KentenKind
#include "IRubyStrand.h"			// IRubyAttrStrand (the file is IRubyStrand.h, the class is not)
#include "ITextAttrBoolean.h"		// kTARubyAttrBoss / kTAMojiRubyBoss
#include "ITextAttrInt16.h"		// kTAKentenKindBoss
#include "ITextAttrWideString.h"	// kTARubyStringBoss
#include "ITextModel.h"
#include "ITextModelCmds.h"
#include "IUIDData.h"				// which model PrivateCreateStrandCmd is to make it on
#include "AttributeBossList.h"
#include "CJKID.h"					// kRubyAttrStrandBoss, kTARuby*, kTAKentenKindBoss
#include "CmdUtils.h"
#include "CreateObject.h"			// CreateObject2 - how an attribute boss is made
#include "ErrorUtils.h"
#include "TextID.h"				// kCharAttrStrandBoss, kPrivateCreateStrandCmdBoss
#include "TextIterator.h"			// AppendToStringAndIncrement - the older words, raw
#include "WideString.h"
#include <string>
#include <vector>					// the replaced rows a bulk run holds until its one re-diff is done

#include "KCMStoryRestore.h"
#include "KCMCore.h"				// KCMArmedTargetDB / KCMArmedSourceDB / KCMIsDocDBOpen
#include "KCMOriginCompare.h"		// KCMOriginArmed / KCMOriginScopedCopy / KCMOriginToSourceUID
#include "KCMStoryList.h"			// the row and its changes
#include "KCMStoryKinds.h"			// kKCMStoryAttrRuby / kKCMStoryAttrKenten
#include "KCMStoryDiffRun.h"		// RunOne - the row diffed again after the write
#include "KCMModelNotify.h"		// KCMNotify - the panel rebuilds its tree
#include "KCMID.h"					// kKCMStoryEditsRebuiltMessage

namespace
{

PMString Ascii(const char* text)
{
	PMString s(text);
	s.SetTranslatable(kFalse);
	return s;
}

/** A refusal, named after the act the reader pressed.

	★**THE SAME COMMAND WEARS TWO NAMES** (2026-09-15). In the Import mode the Source is the copy
	the reader's own edited words were poured into, so taking a change in is a REPLACEMENT - the
	item there is called "Change to Imported Text". A message beginning "restore:" under that item
	is the plug-in disagreeing with itself in the one line the reader reads after pressing.
*/
PMString Refused(const char* what)
{
	PMString s;
	s.SetTranslatable(kFalse);
	s.Append((KCMGetCompareMode() == kKCMModeImport) ? "import: " : "restore: ");
	s.Append(what);
	return s;
}

}	// namespace

// ---- ruby ----------------------------------------------------------------------------------
// ★The four writers below (ruby strand, ruby, kenten kind, the kenten name table) are EXPORTED
//   from this file since 2026-09-13: the PDF report's Story section (KCMReportTable.cpp) sets
//   real ruby and real kenten over the changed characters of its table cells, and writing them a
//   second time there would be the same recipe in two places. Declared in KCMStoryRestore.h.

/** The ruby strand exists on a story only once something put ruby on it; a story that never had
    any needs it made first (kPrivateCreateStrandCmdBoss - KIDMCPRuby.cpp measured the shape). */
ErrorCode KCMCreateRubyStrandIfNeeded(ITextModel* model)
{
	InterfacePtr<IRubyAttrStrand> existing(
		(IRubyAttrStrand*)model->QueryStrand(kRubyAttrStrandBoss, IRubyAttrStrand::kDefaultIID));
	if (existing != nil)
		return kSuccess;
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kPrivateCreateStrandCmdBoss));
	InterfacePtr<IUIDData> modelData(cmd, UseDefaultIID());
	InterfacePtr<IClassIDData> strandData(cmd, UseDefaultIID());
	if (cmd == nil || modelData == nil || strandData == nil)
		return kFailure;
	modelData->Set(model);
	strandData->Set(kRubyAttrStrandBoss);
	return CmdUtils::ProcessCommand(cmd);
}

/** One reading onto one range: the three attributes that ARE a reading (on, the string,
    mono/group), and none of the twenty-seven that are its look. ⚠kTAMojiRubyBoss kTrue IS MONO. */
ErrorCode KCMApplyRuby(ITextModel* model, TextIndex at, int32 len, const PMString& reading, bool16 group)
{
	boost::shared_ptr<AttributeBossList> attrs(new AttributeBossList);
	{
		InterfacePtr<ITextAttrBoolean> on(::CreateObject2<ITextAttrBoolean>(kTARubyAttrBoss));
		if (on == nil) return kFailure;
		on->SetFlag(kTrue);
		InterfacePtr<IAttrReport> report(on, UseDefaultIID());
		attrs->ApplyAttribute(report);
	}
	{
		InterfacePtr<ITextAttrWideString> text(::CreateObject2<ITextAttrWideString>(kTARubyStringBoss));
		if (text == nil) return kFailure;
		text->SetString(WideString(reading));
		InterfacePtr<IAttrReport> report(text, UseDefaultIID());
		attrs->ApplyAttribute(report);
	}
	{
		InterfacePtr<ITextAttrBoolean> moji(::CreateObject2<ITextAttrBoolean>(kTAMojiRubyBoss));
		if (moji == nil) return kFailure;
		moji->SetFlag(group ? kFalse : kTrue);
		InterfacePtr<IAttrReport> report(moji, UseDefaultIID());
		attrs->ApplyAttribute(report);
	}
	InterfacePtr<ITextModelCmds> cmds(model, UseDefaultIID());
	if (cmds == nil)
		return kFailure;
	InterfacePtr<ICommand> apply(cmds->ApplyCmd(RangeData(at, at + len), attrs, kCharAttrStrandBoss));
	return (apply != nil) ? CmdUtils::ProcessCommand(apply) : kFailure;
}

/** Ruby off a range by REMOVING the ruby attributes' overrides - all thirty, so that nothing is
    left for the diff to report as a residue (KIDMCPRuby.cpp's list and reasoning).

    ★EXPORTED SINCE 2026-09-16, when the import gained a third caller (KCMStoryAttrPour.cpp): a
      reading the reader deleted from their file has to come off the copy, and taking it off is
      this list of thirty or it is a residue nobody asked for. */
ErrorCode KCMClearRuby(ITextModel* model, TextIndex at, int32 len)
{
	static const ClassID kRubyAttrs[] =
	{
		kTARubyAttrBoss,			kTARubyStringBoss,			kTAMojiRubyBoss,
		kTARubyOTProBoss,			kTARubyPointSizeBoss,		kTARubyRelativeSizeBoss,
		kTARubyAlignmentBoss,		kTARubyFontUIDBoss,			kTARubyFontStyleBoss,
		kTARubyAdjustParentBoss,	kTARubyXScaleBoss,			kTARubyYScaleBoss,
		kTARubyXOffsetBoss,			kTARubyYOffsetBoss,			kTARubyPositionBoss,
		kTARubyEdgeSpaceBoss,		kTARubyOverhangBoss,		kTARubyOverhangFlagBoss,
		kTARubyAutoScalingBoss,		kTARubyAutoScaleMinBoss,	kTARubyColorBoss,
		kTARubyTintBoss,			kTARubyOverprintBoss,		kTARubyStrokeColorBoss,
		kTARubyStrokeTintBoss,		kTARubyStrokeOverprintBoss,	kTARubyOutlineBoss,
		kTARubyAutoTCYNumDigitsBoss,kTARubyAutoTCYIncludeRomanBoss,
		kTARubyAutoTCYAutoScaleBoss
	};
	boost::shared_ptr<AttributeBossList> attrs(new AttributeBossList);
	for (size_t i = 0; i < sizeof(kRubyAttrs) / sizeof(kRubyAttrs[0]); ++i)
	{
		InterfacePtr<IAttrReport> report(::CreateObject2<IAttrReport>(kRubyAttrs[i]));
		if (report == nil)
			return kFailure;
		attrs->ApplyAttribute(report);
	}
	InterfacePtr<ITextModelCmds> cmds(model, UseDefaultIID());
	if (cmds == nil)
		return kFailure;
	InterfacePtr<ICommand> clear(cmds->ClearOverridesCmd(at, len, attrs, kCharAttrStrandBoss));
	return (clear != nil) ? CmdUtils::ProcessCommand(clear) : kFailure;
}

// ---- kenten --------------------------------------------------------------------------------

/** The inverse of KCMTextRead's KentenKindName (the official spelling from
    SnpPerformTextAttrKenten). kFalse for a name this build cannot write - "Custom" among them:
    a custom mark needs its character too, which the row does not carry. */
bool16 KCMKentenKindOf(const PMString& name, int16& outKind)
{
	const std::string n = name.GetUTF8String();
	struct Entry { const char* fName; int16 fKind; };
	static const Entry kTable[] =
	{
		{ "BlackSesameDot",   IKentenStyle::Kenten_BlackSesameDot },
		{ "WhiteSesameDot",   IKentenStyle::Kenten_WhiteSesameDot },
		{ "Fisheye",          IKentenStyle::Kenten_Fisheye },
		{ "BlackCircle",      IKentenStyle::Kenten_BlackCircle },
		{ "SmallBlackCircle", IKentenStyle::Kenten_SmallBlackCircle },
		{ "Bullseye",         IKentenStyle::Kenten_Bullseye },
		{ "BlackTriangle",    IKentenStyle::Kenten_BlackTriangle },
		{ "WhiteTriangle",    IKentenStyle::Kenten_WhiteTriangle },
		{ "WhiteCircle",      IKentenStyle::Kenten_WhiteCircle },
		{ "SmallWhiteCircle", IKentenStyle::Kenten_SmallWhiteCircle },
	};
	for (size_t i = 0; i < sizeof(kTable) / sizeof(kTable[0]); ++i)
		if (n == kTable[i].fName)
		{
			outKind = kTable[i].fKind;
			return kTrue;
		}
	return kFalse;
}

/** The kenten KIND onto a range (Kenten_None = off; the look is left alone). */
ErrorCode KCMApplyKentenKind(ITextModel* model, TextIndex at, int32 len, int16 kind)
{
	boost::shared_ptr<AttributeBossList> attrs(new AttributeBossList);
	InterfacePtr<ITextAttrInt16> attr(::CreateObject2<ITextAttrInt16>(kTAKentenKindBoss));
	if (attr == nil)
		return kFailure;
	attr->Set(kind);
	InterfacePtr<IAttrReport> report(attr, UseDefaultIID());
	attrs->ApplyAttribute(report);
	InterfacePtr<ITextModelCmds> cmds(model, UseDefaultIID());
	if (cmds == nil)
		return kFailure;
	InterfacePtr<ICommand> apply(cmds->ApplyCmd(RangeData(at, at + len), attrs, kCharAttrStrandBoss));
	return (apply != nil) ? CmdUtils::ProcessCommand(apply) : kFailure;
}

namespace
{

/** One undo step around whatever the restore writes, named for the Edit menu.

	@param own kFalse builds nothing: a bulk restore owns ONE step around the whole run, and a
		sequence per change inside it would put a dozen entries on the Edit menu for one press.
		★The name is then the bulk caller's, which is why this class does not know about it. */
class RestoreSequence
{
public:
	explicit RestoreSequence(bool16 own = kTrue)
		: fSequence(own ? CmdUtils::BeginCommandSequence("KCMRestoreChange") : nil)
	{
		// ★THE NAME THE READER PRESSED, because this one goes on the Edit menu beside Undo. In the
		//   Import mode the source is the copy their own edited words were poured into, so the
		//   item there is called "Change to Imported Text" (the user, 2026-09-15: "taking it in and
		//   swapping it over is a replacement") - and an undo step calling itself something the
		//   panel never offered would be the plug-in disagreeing with itself.
		if (fSequence != nil)
			fSequence->SetName(KCMGetCompareMode() == kKCMModeImport
							   ? Ascii("Change to Imported Text")
							   : Ascii("Restore Source Text"));
	}
	~RestoreSequence()
	{
		if (fSequence != nil)
			CmdUtils::EndCommandSequence(fSequence);
	}
private:
	ICommandSequence* fSequence;
	RestoreSequence(const RestoreSequence&);
	RestoreSequence& operator=(const RestoreSequence&);
};

/*	RefindAfterEdit
	The story was edited since the comparison named the positions. Compare that ONE story again and
	find the same change in the new list, instead of refusing the write.

	★★★**THE SOURCE IS THE KEY, BECAUSE THE SOURCE CANNOT MOVE.** The task-start copy is rebuilt
	from a fixed byte string every time, so a change's fSourceStart/fSourceEnd name the same place
	before and after the reader types in the Target - while fTargetStart/fTargetEnd are exactly what
	an edit invalidates. So the side that cannot move is what a change is looked up by, and the side
	that moved is read back off the fresh comparison.
	⚠**fSourceEnd == fSourceStart IS A REAL PLACE** (an insertion's caret in the older version), so
	  the pair identifies those rows too - the rule is KCMStoryList.h's, not invented here.

	★★**THIS IS NOT "TRUSTING THE COUNTER" - IT IS ITS OPPOSITE.** KBS once held the same counter in
	order to SKIP the position test, and rewrote occurrences its reader had never seen (the note in
	KBSResultModel.h, removed 2026-08-03 with the fast path it fed). Here the counter does not stand
	in for the test: it is what makes the test run AGAIN.

	@param change IN as the row held it; OUT the same change as the re-diff names it now.
	@param outMessage filled on every kFalse.
	@return kFalse when nothing should be written - the story cannot be compared again, the change
		is gone, or the row now reads differently and the reader has not seen that yet.
*/
bool16 RefindAfterEdit(int32 nth, IDataBase* targetDB, IDataBase* sourceDB,
					   KCMStoryChange& change, PMString& outMessage)
{
	// The row's story, diffed again - the same work "Refresh Story Comparison" does on one row, and
	// it brings the row's counter up to date, which is what lets the SECOND press go straight
	// through (KCMStoryDiffRun.h, RunOne).
	const int32 left = KCMStoryDiffRun::RunOne(targetDB, sourceDB, nth);

	bool16 ok = kFalse;
	if (left < 0)
	{
		outMessage = Refused("this story was edited and cannot be compared again, so nothing was changed.");
	}
	else
	{
		// ⚠**AN ALREADY-REPLACED ROW IS NOT A CANDIDATE**: asking the merged list rather than the
		//   live one keeps every caller counting in the same index space (KCMStoryList.h), and
		//   GetMergedChange is the one place that knows which list an index fell in.
		const int32 count = KCMStoryList::GetMergedChangeCount(nth);
		const KCMStoryChange* found = nil;
		for (int32 i = 0; i < count && found == nil; ++i)
		{
			bool16 isReplaced = kFalse;
			const KCMStoryChange* const c = KCMStoryList::GetMergedChange(nth, i, isReplaced);
			if (c == nil || isReplaced)
				continue;
			// ⚠**fKind IS PART OF THE IDENTITY, NOT A DETAIL.** Delete the paragraph this change
			//   sits in and the re-diff names a DELETION over the same source range - which would
			//   otherwise answer to a replacement's lookup and write the older words into whatever
			//   now stands at that spot. A change that has become a different KIND of change is
			//   not the same change; it is not found, and nothing is written.
			if (c->fSourceStart == change.fSourceStart && c->fSourceEnd == change.fSourceEnd
				&& c->fKind == change.fKind
				&& c->fWhat == change.fWhat && c->fAttrKind == change.fAttrKind)
				found = c;
		}

		if (found == nil)
		{
			outMessage = Refused("this change is not there any more - the words were edited until the "
							   "two sides agreed. Nothing was changed.");
		}
		// ***** THE PARAGRAPH NOW READS DIFFERENTLY: STOP ONCE AND LET THEM LOOK. *****
		// (the user's call, 2026-09-15: "this paragraph has been changed - check it and confirm the
		// replacement once more"). The reader edited the very words they are replacing, so what
		// would go in is no longer what they had in front of them when they opened the menu.
		// ★The press after this one goes through without stopping: the panel is redrawn below, and
		//   the row's counter now matches the story, so this function is not even reached.
		//
		// ⚠★★★**WHAT IS COMPARED IS WHAT WOULD BE OVERWRITTEN - NOT WHAT THE ROW LOOKS LIKE.**
		//   Measured twice on 2026-09-15, each time by stopping on a change NOBODY had touched:
		//     1. fTargetStart was in the test. It moves whenever anything EARLIER in the story is
		//        edited, and fetching that moved position is the whole purpose of the re-diff above.
		//     2. fTextPre / fTextPost were in the test. They are cut `kContextCodePoints` either
		//        side of the change out of a JOINED RUN (KCMStoryDiffRun's Slice), so they reach
		//        across the paragraph boundary and carry the neighbour's edits into this comparison.
		//   Both mistakes were the same one: confusing "the row is drawn differently" with "the
		//   words this would overwrite are different". Only the second is the reader's risk - the
		//   context is never written, and what goes IN is read from the Source, which cannot move.
		//   ⇒ the test is the CHANGED PART alone: how many characters would be taken out, and what
		//     they read. A neighbour's typing is invisible to both.
		else if ((found->fTargetEnd - found->fTargetStart) != (change.fTargetEnd - change.fTargetStart)
				 || found->fText.Compare(kTrue, change.fText) != 0)
		{
			outMessage = Refused("this paragraph has changed - check what it shows now, then press "
							   "again to replace.");
		}
		else
		{
			change = *found;	// copied before the panel is told: the pointer is the list's own
			ok = kTrue;
		}
	}

	// ★**TOLD WHICHEVER WAY THIS WENT.** The row is quoting a different moment than the one that was
	//   right-clicked, and a row saying something untrue about the document in front of the reader
	//   is worse than a refusal - the same reason RunOne re-reads the row at all.
	KCMNotify(kKCMStoryEditsRebuiltMessage);
	return ok;
}

/*	RestoreOne
	One change written back into the newer document.

	@param standalone kTrue when this call IS the act - the reader pressed the item on one change -
		so it owns the undo step, the counter test, the re-diff afterwards and the panel's redraw.
		kFalse when a bulk restore is walking the row: that caller has already tested the counter,
		owns ONE undo step around the whole run, and re-diffs and redraws once at the end.
	⚠★★★**A BULK CALLER MUST WALK BACKWARDS.** With no re-diff between writes, the positions this
	  function was handed stay true only for the changes BEFORE the one being written - a
	  replacement makes the text longer or shorter and slides everything after it. Walking from the
	  end is what makes "no re-diff between writes" safe, and it is the same rule the import's own
	  pour follows (KCMStoryTextImport's ApplyParagraph, "back to front").
*/
bool16 RestoreOne(int32 nth, int32 which, bool16 standalone, PMString& outMessage,
				  KCMStoryChange* outDone, IDataBase* sourceDBIn)
{
	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);

	// ★★★**THE SAME INDEX SPACE THE PANEL COUNTS IN** (KCMStoryList::GetMergedChange). The menu
	//   hands over the child's position in the tree, and in the Import mode that tree holds the
	//   live changes AND the ones already replaced. Reading fChanges[which] here would name a
	//   DIFFERENT change as soon as one replaced row sat above it - and it would not fail loudly,
	//   it would write the wrong words into the document.
	bool16 alreadyReplaced = kFalse;
	const KCMStoryChange* const found = KCMStoryList::GetMergedChange(nth, which, alreadyReplaced);
	const KCMStoryRow* row = KCMStoryList::GetRow(nth);
	if (found == nil || row == nil)
	{
		outMessage = Refused("no such change (the list was rebuilt - right-click the row again).");
		return kFalse;
	}
	if (alreadyReplaced)
	{
		// The row is kept in the list precisely so the reader can see what they took in; taking
		// it in twice would write the same words over words that already match them. ⚠Refused
		// even when an undo has put the older text back: the positions on every OTHER row were
		// named against the text as it stood after the write, so the list as a whole needs
		// comparing again before anything more is written into this story.
		outMessage = Refused("this change has already been taken in - "
						   "run Refresh Story Comparison on its row to start over.");
		return kFalse;
	}
	// A copy, and deliberately NOT const: when the story has been edited since the comparison, this
	// is replaced by the same change as the re-diff now names it (RefindAfterEdit).
	// ⚠The reference above points into freed memory once the row is rebuilt, either way.
	KCMStoryChange change = *found;
	const UID storyUID = row->fStoryUID;
	const uint32 countThen = row->fTargetTextCount;
	row = nil;

	IDataBase* const targetDB = KCMArmedTargetDB();
	if (targetDB == nil || !KCMIsDocDBOpen(targetDB))
	{
		outMessage = Refused("the Target document is not open.");
		return kFalse;
	}

	// The Source: the armed one, or the task-start copy rehydrated for this call (and closed on
	// the way out - the scope is the whole function, so RunOne below still sees it).
	// ⚠★★**A BULK RUN HANDS ITS OWN COPY DOWN, AND THAT IS NOT AN OPTIMISATION.** There is one
	//   rehydrated task-start document at a time, so a second Open fails outright - measured on the
	//   first live run of the bulk item, which answered "0 changes taken in, 2 skipped (a
	//   task-start copy is already open)" and left the document untouched.
	KCMOriginScopedCopy originCopy;
	IDataBase* sourceDB = sourceDBIn;
	if (sourceDB == nil)
	{
		sourceDB = KCMArmedSourceDB();
		if (sourceDB == nil && KCMOriginArmed())
		{
			PMString whyNot;
			if (!originCopy.Open(whyNot))
			{
				outMessage = Refused("could not rebuild the task-start copy: ");
				outMessage.Append(whyNot);
				return kFalse;
			}
			sourceDB = originCopy.DB();
		}
	}
	if (sourceDB == nil || !KCMIsDocDBOpen(sourceDB))
	{
		outMessage = Refused("the Source document is not open.");
		return kFalse;
	}

	// The Target story, as it is NOW - which must be as it was when the diff named the positions.
	InterfacePtr<ITextModel> target(UIDRef(targetDB, storyUID), UseDefaultIID());
	if (target == nil)
	{
		outMessage = Refused("the story is no longer in the Target document.");
		return kFalse;
	}
	// ***** EDITED SINCE THE COMPARISON? COMPARE IT AGAIN RATHER THAN REFUSE. *****
	// (the user's decision, 2026-09-15). Until then this was a dead end: the reader had touched the
	// document - by accident as often as not - and every remaining change in that story could only
	// be taken in after running "Refresh Story Comparison" by hand. The positions are what an edit
	// invalidates, so the positions are what is fetched again; nothing is taken on trust.
	// ⚠**ONLY WHEN THIS CALL IS THE ACT.** A bulk run tested the counter once before it started, and
	//   walks backwards so that its own writes cannot invalidate what it has not reached yet.
	//   Re-diffing here would throw that away and cost one comparison per change.
	if (standalone && target->GetTextChangeCount() != countThen
		&& !RefindAfterEdit(nth, targetDB, sourceDB, change, outMessage))
		return kFalse;

	// ***** THE BEFORE-STATE, TAKEN ONCE `change` IS FINAL AND BEFORE ANYTHING IS WRITTEN. *****
	// In the Import mode this change stays in the list after the write, and its row has to be
	// drawable both ways: as it stands now, and as it stood before - because Ctrl+Z puts the text
	// back and the row follows it there. This is the only moment the before-state can be read;
	// afterwards the words it names are no longer in the story.
	// ⚠**AFTER RefindAfterEdit, NOT BEFORE IT**: when the story was edited, `change` above is the
	//   re-diff's version of it, and a before-state cut from the stale one would draw the replaced
	//   row with words that are no longer anywhere in the document.
	// ★**THE AFTER-PIECES ARE ALREADY IN HAND** and are not cut again: a replacement changes the
	//   CHANGED PART and leaves the context on either side untouched, so "after" is the row's own
	//   pre + the SOURCE's middle + the row's own post. fOtherText is exactly the words about to
	//   go in, cut and marked up by the same pass that made fText (KCMStoryDiffRun's Slice), so
	//   the two states are drawn by one rule rather than by two that could drift.
	KCMStoryChange done  = change;
	done.fBeforeStart    = change.fTargetStart;
	done.fBeforeEnd      = change.fTargetEnd;
	done.fBeforeTextPre  = change.fTextPre;
	done.fBeforeText     = change.fText;
	done.fBeforeTextPost = change.fTextPost;
	done.fReplacedTextPre  = change.fTextPre;
	done.fReplacedText     = change.fOtherText;
	done.fReplacedTextPost = change.fTextPost;

	const TextIndex targetLength = target->TotalLength();
	if (change.fTargetStart < 0 || change.fTargetEnd < change.fTargetStart || change.fTargetEnd > targetLength)
	{
		outMessage = Refused("the change's range is outside the story.");
		return kFalse;
	}
	const int32 targetCount = change.fTargetEnd - change.fTargetStart;
	const int32 sourceCount = change.fSourceEnd - change.fSourceStart;

	// ===== the words =============================================================================
	if (change.fWhat == KCMStoryChange::kText)
	{
		InterfacePtr<ITextModel> source(UIDRef(sourceDB, KCMOriginToSourceUID(sourceDB, storyUID)), UseDefaultIID());
		if (source == nil)
		{
			outMessage = Refused("the story is not in the Source.");
			return kFalse;
		}
		boost::shared_ptr<WideString> words(new WideString());
		if (sourceCount > 0)
		{
			if (change.fSourceStart < 0 || change.fSourceEnd > source->TotalLength())
			{
				outMessage = Refused("the change's range is outside the Source story.");
				return kFalse;
			}
			TextIterator iter(source, change.fSourceStart);
			iter.AppendToStringAndIncrement(words.get(), sourceCount);
		}

		InterfacePtr<ITextModelCmds> cmds(target, UseDefaultIID());
		if (cmds == nil)
		{
			outMessage = Refused("the story cannot be edited.");
			return kFalse;
		}
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		{
			RestoreSequence undo(standalone);
			InterfacePtr<ICommand> write(targetCount > 0
				? cmds->ReplaceCmd(change.fTargetStart, targetCount, words)
				: cmds->InsertCmd(change.fTargetStart, words));
			if (write == nil || CmdUtils::ProcessCommand(write) != kSuccess)
			{
				ErrorUtils::PMSetGlobalErrorCode(kSuccess);
				outMessage = Refused("the write failed (a locked story or layer?).");
				return kFalse;
			}
		}
		// Where the replacement now stands. ⚠**THE START DID NOT MOVE, THE END DID**: what went in
		//   is as long as the source's side of the change, which is not the length that came out.
		//   WideString counts code points, the same unit TextIndex counts in
		//   ([[textindex-counts-code-points]]), so no conversion belongs here.
		done.fReplacedStart = change.fTargetStart;
		done.fReplacedEnd   = change.fTargetStart + static_cast<int32>(words->Length());

		// ★★★**AND EVERYTHING ALREADY REPLACED FURTHER DOWN THE STORY SLIDES.** The live changes
		//   are about to be named afresh by RunOne, but a change that has already been replaced
		//   is not in that comparison any more - nothing else would move it. Replacing three
		//   words with five pushes every later replaced row along by two, and a row whose
		//   position quietly rots is a row whose jump lands in the wrong place.
		//   ⚠BEFORE the new one is added, so that it is not shifted by its own write.
		KCMStoryList::ShiftReplacedChanges(nth, change.fTargetStart,
										   static_cast<int32>(words->Length()) - targetCount);

		if (words->Length() > 0)
		{
			outMessage = Ascii("Restored ");
			outMessage.AppendNumber(static_cast<int32>(words->Length()));
			outMessage.Append(" character(s) from the Source");
		}
		else
		{
			outMessage = Ascii("Took out ");
			outMessage.AppendNumber(targetCount);
			outMessage.Append(" inserted character(s)");
		}
	}
	// ===== ruby / kenten ==========================================================================
	else
	{
		// The base characters the attribute sits on, in the Target. When the paragraph's TEXT
		// changed too, a REMOVED attribute has no place named on this side (the diff puts 0 there)
		// - the words have to come back first.
		if (targetCount <= 0)
		{
			outMessage = Refused("the words of this paragraph changed as well - restore the words first, then this.");
			return kFalse;
		}
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		ErrorCode err = kFailure;
		if (change.fAttrKind == kKCMStoryAttrRuby)
		{
			RestoreSequence undo(standalone);
			if (change.fOtherRuby.IsEmpty())
			{
				// ruby ADDED since the older version: take it off
				err = KCMClearRuby(target, change.fTargetStart, targetCount);
				outMessage = Ascii("Took the ruby off ");
				outMessage.AppendNumber(targetCount);
				outMessage.Append(" character(s)");
			}
			else
			{
				// ruby REMOVED or CHANGED: the older reading back, over the older span. A span that
				// grew or shrank is cleared first, then written at the older length from its start.
				err = KCMCreateRubyStrandIfNeeded(target);
				int32 len = targetCount;
				if (err == kSuccess && sourceCount > 0 && sourceCount != targetCount)
				{
					err = KCMClearRuby(target, change.fTargetStart, targetCount);
					len = sourceCount;
					if (change.fTargetStart + len > targetLength)
						len = targetLength - change.fTargetStart;
				}
				if (err == kSuccess)
					err = KCMApplyRuby(target, change.fTargetStart, len, change.fOtherRuby, change.fOtherRubyGroup);
				outMessage = Ascii("Restored the ");
				outMessage.Append(change.fOtherRubyGroup ? "group" : "mono");
				outMessage.Append(" ruby \"");
				outMessage.Append(change.fOtherRuby);
				outMessage.Append("\" over ");
				outMessage.AppendNumber(len);
				outMessage.Append(" character(s)");
			}
		}
		else if (change.fAttrKind == kKCMStoryAttrKenten)
		{
			int16 kind = IKentenStyle::Kenten_None;
			if (!change.fOtherRuby.IsEmpty() && !KCMKentenKindOf(change.fOtherRuby, kind))
			{
				outMessage = Refused("this kenten kind cannot be written back (a custom mark carries a character the row does not hold).");
				return kFalse;
			}
			RestoreSequence undo(standalone);
			err = KCMApplyKentenKind(target, change.fTargetStart, targetCount, kind);
			outMessage = (kind == IKentenStyle::Kenten_None) ? Ascii("Took the kenten off ") : Ascii("Restored the kenten \"");
			if (kind != IKentenStyle::Kenten_None) { outMessage.Append(change.fOtherRuby); outMessage.Append("\" over "); }
			outMessage.AppendNumber(targetCount);
			outMessage.Append(" character(s)");
		}
		else
		{
			outMessage = Refused("this kind of change is not restorable yet.");
			return kFalse;
		}
		if (err != kSuccess)
		{
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			outMessage = Refused("the attribute could not be written.");
			return kFalse;
		}

		// An attribute does not change how many characters there are, so the range is the one the
		// diff named. ⚠A ruby whose span shrank writes its reading over fewer characters than the
		// row covers; the row still points at where the change happened, which is what a jump and
		// the cell's highlight are for.
		done.fReplacedStart = change.fTargetStart;
		done.fReplacedEnd   = change.fTargetEnd;
	}

	// ***** THE BOOKKEEPING, WHICH A BULK CALLER OWNS INSTEAD. *****
	// Handing `done` back is what lets it: the replaced row cannot be added here, because the
	// counter that decides whether it is DRAWN as replaced is the one the re-diff records, and the
	// bulk run re-diffs once, at the end, for all of them at once.
	if (!standalone)
	{
		if (outDone != nil)
			*outDone = done;
		return kTrue;
	}

	// The row's story, diffed again: the restored change leaves the list, and every other
	// change's positions are named afresh against the text as it now stands.
	// ★★AND THE ROW'S COUNTER IS BROUGHT UP TO DATE WITH IT, which is what lets a SECOND
	//   replacement in the same story go through: the check at the top of this function refuses a
	//   story whose counter has moved since the comparison, and this write moved it.
	const int32 left = KCMStoryDiffRun::RunOne(targetDB, sourceDB, nth);

	// ***** THE REPLACED CHANGE GOES BACK INTO THE LIST - IN BOTH MODES. *****
	// ★★**BOTH SINCE 2026-09-15, and the undo observer is why** (the user's decision, changing
	//   their own of the same day). It was the Import mode's alone at first: there the reader works
	//   through a list of edits they made outside InDesign, and a row that vanishes on being taken
	//   in leaves them nothing to read afterwards ("I want the child row to stay, the way KBS keeps
	//   a replaced hit"). What made it both was measuring what a Ctrl+Z can and cannot put back:
	//   a row the Story mode had DELETED needs the story diffed again to come back, and that needs
	//   the task-start copy rehydrated - which runs commands (KCMRehydrate: three ProcessCommand
	//   calls, a new document, ImportINX) and so cannot be done from inside a lazy notification.
	//   A row that STAYS needs none of it: the sign is DERIVED from the story's change counter,
	//   which the undo takes back by itself, so redrawing is the whole of the work.
	// ⚠**AFTER RunOne, NEVER BEFORE IT**: RunOne rebuilds the row, and a replaced change added
	//   ahead of it would be added to the row that is about to be replaced.
	{
		const KCMStoryRow* const after = KCMStoryList::GetRow(nth);
		if (after != nil)
		{
			// The counter as the re-diff has just recorded it. ★This number IS "replaced": the row
			// is drawn that way while it still matches the story's counter, and an undo - which
			// takes the counter back - undraws it without a line of undo-specific code.
			done.fReplacedCount = after->fTargetTextCount;
			KCMStoryList::AddReplacedChange(nth, done);
		}
	}

	KCMNotify(kKCMStoryEditsRebuiltMessage);
	if (left >= 0)
	{
		outMessage.Append(" - ");
		outMessage.AppendNumber(left);
		outMessage.Append(" change(s) left in this story");
	}
	outMessage.Append(". Ctrl+Z undoes it.");
	return kTrue;
}

/*	BulkRun
	Everything both bulk items need: the two documents, the one counter test, the backwards walk,
	the one undo step, and the replaced rows put back afterwards.

	★★★**BACKWARDS, AND THAT IS THE WHOLE TRICK.** Writing a change makes the story longer or
	shorter, so every position AFTER it moves. Walking from the end means each write only disturbs
	text the walk has already passed, and no re-diff is needed between writes - one at the start if
	the reader had typed since the comparison, one at the end to rebuild the row. The alternative
	measured out as one full story comparison per change.

	@param nth which row.
	@param seq the undo step, owned by the caller (one step per PRESS, not per story).
	@param outWritten / outSkipped counted, never reset - the all-stories item adds across rows.
	@param outFirstWhyNot the first refusal's reason, for the status line.
	@return kFalse only when the row could not be started on at all.
*/
bool16 BulkRun(int32 nth, IDataBase* sourceDBIn, int32& outWritten, int32& outSkipped,
			   PMString& outFirstWhyNot)
{
	const KCMStoryRow* row = KCMStoryList::GetRow(nth);
	if (row == nil)
		return kFalse;
	const UID storyUID = row->fStoryUID;
	const uint32 countThen = row->fTargetTextCount;
	row = nil;

	IDataBase* const targetDB = KCMArmedTargetDB();
	if (targetDB == nil || !KCMIsDocDBOpen(targetDB))
		return kFalse;

	// The Source: the armed one, the one handed down, or the task-start copy rehydrated for this
	// run. ⚠The scope is the whole function, so every RestoreOne below still sees it.
	// ⚠★★**ONE COPY PER PRESS, NOT PER ROW.** The all-stories item opens it once and hands it to
	//   every row: rehydrating the task-start document once per story would cost that work N times,
	//   and two of them open at once is refused outright (measured - see RestoreOne).
	KCMOriginScopedCopy originCopy;
	IDataBase* sourceDB = sourceDBIn;
	if (sourceDB == nil)
	{
		sourceDB = KCMArmedSourceDB();
		if (sourceDB == nil && KCMOriginArmed())
		{
			PMString whyNot;
			if (!originCopy.Open(whyNot))
				return kFalse;
			sourceDB = originCopy.DB();
		}
	}
	if (sourceDB == nil || !KCMIsDocDBOpen(sourceDB))
		return kFalse;

	InterfacePtr<ITextModel> target(UIDRef(targetDB, storyUID), UseDefaultIID());
	if (target == nil)
		return kFalse;

	// ***** THE COUNTER, TESTED ONCE FOR THE WHOLE RUN. *****
	// The reader may have typed since the comparison - the same case the single item now handles by
	// looking the change up again. Here one re-diff at the start makes every position in the row
	// true at once, and the backwards walk keeps them true.
	if (target->GetTextChangeCount() != countThen
		&& KCMStoryDiffRun::RunOne(targetDB, sourceDB, nth) < 0)
		return kFalse;

	std::vector<KCMStoryChange> dones;
	const int32 count = KCMStoryList::GetMergedChangeCount(nth);
	for (int32 i = count - 1; i >= 0; --i)
	{
		bool16 isReplaced = kFalse;
		const KCMStoryChange* const c = KCMStoryList::GetMergedChange(nth, i, isReplaced);
		if (c == nil || isReplaced)
			continue;				// already taken in; not a candidate to take in twice

		PMString whyNot;
		KCMStoryChange done;
		if (RestoreOne(nth, i, kFalse, whyNot, &done, sourceDB))
		{
			// ★**WHAT THIS WRITE DID TO THE ONES ALREADY DONE.** They all lie AFTER this change
			//   (the walk is backwards), so a write that changed the length slides every one of
			//   them. They are not in the list yet - that is what makes them invisible to
			//   ShiftReplacedChanges, which moves the rows the list already holds.
			const int32 delta = (done.fReplacedEnd - done.fReplacedStart)
							  - (done.fBeforeEnd - done.fBeforeStart);
			if (delta != 0)
				for (size_t k = 0; k < dones.size(); ++k)
					if (dones[k].fReplacedStart >= done.fBeforeStart)
					{
						dones[k].fReplacedStart += delta;
						dones[k].fReplacedEnd   += delta;
					}
			dones.push_back(done);
			++outWritten;
		}
		else
		{
			++outSkipped;
			if (outFirstWhyNot.IsEmpty())
				outFirstWhyNot = whyNot;
		}
	}

	// ***** ONE RE-DIFF, AT THE END, FOR ALL OF THEM. *****
	KCMStoryDiffRun::RunOne(targetDB, sourceDB, nth);

	// And the replaced rows, with the counter the re-diff has just recorded - the number that makes
	// them draw as replaced, and that an undo takes back (see RestoreOne's own tail).
	// (Both modes, since 2026-09-15 - see RestoreOne's tail for why the Story mode joined.)
	if (!dones.empty())
	{
		const KCMStoryRow* const after = KCMStoryList::GetRow(nth);
		if (after != nil)
			for (size_t k = 0; k < dones.size(); ++k)
			{
				dones[k].fReplacedCount = after->fTargetTextCount;
				KCMStoryList::AddReplacedChange(nth, dones[k]);
			}
	}
	return kTrue;
}

/*	BulkReport
	The one sentence both items end on, so that they cannot describe the same run differently.
*/
void BulkReport(PMString& outMessage, int32 written, int32 skipped, int32 stories,
				const PMString& firstWhyNot)
{
	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);
	if (written == 0 && skipped == 0)
	{
		outMessage.Append("nothing to take in.");
		return;
	}
	outMessage.AppendNumber(written);
	outMessage.Append(written == 1 ? " change taken in" : " changes taken in");
	if (stories > 1)
	{
		outMessage.Append(" across ");
		outMessage.AppendNumber(stories);
		outMessage.Append(" stories");
	}
	if (skipped > 0)
	{
		outMessage.Append(", ");
		outMessage.AppendNumber(skipped);
		outMessage.Append(" skipped");		// the word is the same either way; no plural here
		if (!firstWhyNot.IsEmpty())
		{
			outMessage.Append(" (");
			outMessage.Append(firstWhyNot);
			outMessage.Append(")");
		}
	}
	if (written > 0)
		outMessage.Append(". Ctrl+Z undoes it.");
}

/*	BulkSequenceName
	★The undo step says what the reader pressed, in the words the menu used (the same rule the
	single item's RestoreSequence follows).
*/
PMString BulkSequenceName(bool16 wholeList)
{
	const bool16 importing = (KCMGetCompareMode() == kKCMModeImport);
	if (wholeList)
		return importing ? Ascii("Change All Stories to Imported Text") : Ascii("Restore All Stories");
	return importing ? Ascii("Change All in This Story") : Ascii("Restore All in This Story");
}

}	// namespace

bool16 KCMRestoreChange(int32 nth, int32 which, PMString& outMessage)
{
	return RestoreOne(nth, which, kTrue, outMessage, nil, nil);
}

bool16 KCMRestoreAllInStory(int32 nth, PMString& outMessage)
{
	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);

	ICommandSequence* const seq = CmdUtils::BeginCommandSequence("KCMRestoreAllInStory");
	if (seq != nil)
		seq->SetName(BulkSequenceName(kFalse));

	int32 written = 0, skipped = 0;
	PMString firstWhyNot;
	firstWhyNot.SetTranslatable(kFalse);
	const bool16 ok = BulkRun(nth, nil, written, skipped, firstWhyNot);

	if (seq != nil)
		CmdUtils::EndCommandSequence(seq);

	KCMNotify(kKCMStoryEditsRebuiltMessage);

	if (!ok && written == 0)
	{
		outMessage = Refused("this story could not be taken in (is the comparison still running?).");
		return kFalse;
	}
	BulkReport(outMessage, written, skipped, 1, firstWhyNot);
	return (written > 0) ? kTrue : kFalse;
}

bool16 KCMRestoreAllStories(PMString& outMessage)
{
	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);

	// ⚠**THE ROW COUNT IS READ ONCE, BEFORE ANYTHING IS WRITTEN.** A row never disappears from the
	//   list while this runs (a re-diff empties a row's children, it does not drop the row), so the
	//   indexes stay meaningful - but reading the count inside the loop would invite the opposite
	//   assumption from the next person to touch this.
	const int32 rows = KCMStoryList::GetRowCount();

	// ***** ONE TASK-START COPY FOR THE WHOLE PRESS. *****
	// ⚠Opened BEFORE the undo step, and outside it: rehydrating builds a document of its own, and
	//   that work has no business inside the step the reader will undo.
	KCMOriginScopedCopy originCopy;
	IDataBase* sourceDB = KCMArmedSourceDB();
	if (sourceDB == nil && KCMOriginArmed())
	{
		PMString whyNot;
		if (originCopy.Open(whyNot))
			sourceDB = originCopy.DB();
		// A failure is not fatal here: each row falls back to opening its own, and reports for
		// itself if that fails too.
	}

	ICommandSequence* const seq = CmdUtils::BeginCommandSequence("KCMRestoreAllStories");
	if (seq != nil)
		seq->SetName(BulkSequenceName(kTrue));

	int32 written = 0, skipped = 0, storiesTouched = 0;
	PMString firstWhyNot;
	firstWhyNot.SetTranslatable(kFalse);
	for (int32 nth = 0; nth < rows; ++nth)
	{
		const int32 before = written;
		BulkRun(nth, sourceDB, written, skipped, firstWhyNot);
		if (written > before)
			++storiesTouched;
	}

	if (seq != nil)
		CmdUtils::EndCommandSequence(seq);

	KCMNotify(kKCMStoryEditsRebuiltMessage);

	BulkReport(outMessage, written, skipped, storiesTouched, firstWhyNot);
	return (written > 0) ? kTrue : kFalse;
}

// End, KCMStoryRestore.cpp.
