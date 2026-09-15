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
    left for the diff to report as a residue (KIDMCPRuby.cpp's list and reasoning). */
namespace
{

ErrorCode ClearRuby(ITextModel* model, TextIndex at, int32 len)
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

}	// namespace

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

/** One undo step around whatever the restore writes, named for the Edit menu. */
class RestoreSequence
{
public:
	RestoreSequence() : fSequence(CmdUtils::BeginCommandSequence("KCMRestoreChange"))
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

}	// namespace

bool16 KCMRestoreChange(int32 nth, int32 which, PMString& outMessage)
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
	// A copy: the row is rebuilt below, and the reference above would then point into freed memory.
	const KCMStoryChange change = *found;
	const UID storyUID = row->fStoryUID;
	const uint32 countThen = row->fTargetTextCount;
	row = nil;

	// ***** THE BEFORE-STATE, TAKEN BEFORE ANYTHING IS WRITTEN. *****
	// In the Import mode this change stays in the list after the write, and its row has to be
	// drawable both ways: as it stands now, and as it stood before - because Ctrl+Z puts the text
	// back and the row follows it there. This is the only moment the before-state can be read;
	// afterwards the words it names are no longer in the story.
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

	IDataBase* const targetDB = KCMArmedTargetDB();
	if (targetDB == nil || !KCMIsDocDBOpen(targetDB))
	{
		outMessage = Refused("the Target document is not open.");
		return kFalse;
	}

	// The Source: the armed one, or the task-start copy rehydrated for this call (and closed on
	// the way out - the scope is the whole function, so RunOne below still sees it).
	KCMOriginScopedCopy originCopy;
	IDataBase* sourceDB = KCMArmedSourceDB();
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
	if (target->GetTextChangeCount() != countThen)
	{
		outMessage = Refused("this story has been edited since the comparison, so nothing was changed. "
						   "Refreshing this row compares it again.");
		return kFalse;
	}
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
			RestoreSequence undo;
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
			RestoreSequence undo;
			if (change.fOtherRuby.IsEmpty())
			{
				// ruby ADDED since the older version: take it off
				err = ClearRuby(target, change.fTargetStart, targetCount);
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
					err = ClearRuby(target, change.fTargetStart, targetCount);
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
			RestoreSequence undo;
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

	// The row's story, diffed again: the restored change leaves the list, and every other
	// change's positions are named afresh against the text as it now stands.
	// ★★AND THE ROW'S COUNTER IS BROUGHT UP TO DATE WITH IT, which is what lets a SECOND
	//   replacement in the same story go through: the check at the top of this function refuses a
	//   story whose counter has moved since the comparison, and this write moved it.
	const int32 left = KCMStoryDiffRun::RunOne(targetDB, sourceDB, nth);

	// ***** AND IN THE IMPORT MODE THE REPLACED CHANGE GOES BACK INTO THE LIST. *****
	// The Story mode keeps its old behaviour - the change is dealt with, and its row is gone.
	// Here the reader is working through a list of edits they made outside InDesign, and a row
	// that vanishes on being taken in leaves them nothing to read afterwards (the user, 2026-09-15:
	// "I want the child row to stay, the way KBS keeps a replaced hit").
	// ⚠**AFTER RunOne, NEVER BEFORE IT**: RunOne rebuilds the row, and a replaced change added
	//   ahead of it would be added to the row that is about to be replaced.
	if (KCMGetCompareMode() == kKCMModeImport)
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

// End, KCMStoryRestore.cpp.
