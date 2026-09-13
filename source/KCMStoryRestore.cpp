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

// ---- ruby ----------------------------------------------------------------------------------

/** The ruby strand exists on a story only once something put ruby on it; a story that never had
    any needs it made first (kPrivateCreateStrandCmdBoss - KIDMCPRuby.cpp measured the shape). */
ErrorCode CreateRubyStrandIfNeeded(ITextModel* model)
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
ErrorCode ApplyRuby(ITextModel* model, TextIndex at, int32 len, const PMString& reading, bool16 group)
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

// ---- kenten --------------------------------------------------------------------------------

/** The inverse of KCMTextRead's KentenKindName (the official spelling from
    SnpPerformTextAttrKenten). kFalse for a name this build cannot write - "Custom" among them:
    a custom mark needs its character too, which the row does not carry. */
bool16 KentenKindOf(const PMString& name, int16& outKind)
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
ErrorCode ApplyKentenKind(ITextModel* model, TextIndex at, int32 len, int16 kind)
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

/** One undo step around whatever the restore writes, named for the Edit menu. */
class RestoreSequence
{
public:
	RestoreSequence() : fSequence(CmdUtils::BeginCommandSequence("KCMRestoreChange"))
	{
		if (fSequence != nil)
			fSequence->SetName(Ascii("Restore Source Text"));
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

	const KCMStoryRow* row = KCMStoryList::GetRow(nth);
	if (row == nil || which < 0 || which >= static_cast<int32>(row->fChanges.size()))
	{
		outMessage = Ascii("restore: no such change (the list was rebuilt - right-click the row again).");
		return kFalse;
	}
	// A copy: the row is rebuilt below, and the reference above would then point into freed memory.
	const KCMStoryChange change = row->fChanges[which];
	const UID storyUID = row->fStoryUID;
	const uint32 countThen = row->fTargetTextCount;
	row = nil;

	IDataBase* const targetDB = KCMArmedTargetDB();
	if (targetDB == nil || !KCMIsDocDBOpen(targetDB))
	{
		outMessage = Ascii("restore: the Target document is not open.");
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
			outMessage = Ascii("restore: could not rebuild the task-start copy: ");
			outMessage.Append(whyNot);
			return kFalse;
		}
		sourceDB = originCopy.DB();
	}
	if (sourceDB == nil || !KCMIsDocDBOpen(sourceDB))
	{
		outMessage = Ascii("restore: the Source document is not open.");
		return kFalse;
	}

	// The Target story, as it is NOW - which must be as it was when the diff named the positions.
	InterfacePtr<ITextModel> target(UIDRef(targetDB, storyUID), UseDefaultIID());
	if (target == nil)
	{
		outMessage = Ascii("restore: the story is no longer in the Target document.");
		return kFalse;
	}
	if (target->GetTextChangeCount() != countThen)
	{
		outMessage = Ascii("restore: this story was edited after the comparison - run Refresh Story Comparison on its row first.");
		return kFalse;
	}
	const TextIndex targetLength = target->TotalLength();
	if (change.fTargetStart < 0 || change.fTargetEnd < change.fTargetStart || change.fTargetEnd > targetLength)
	{
		outMessage = Ascii("restore: the change's range is outside the story.");
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
			outMessage = Ascii("restore: the story is not in the Source.");
			return kFalse;
		}
		boost::shared_ptr<WideString> words(new WideString());
		if (sourceCount > 0)
		{
			if (change.fSourceStart < 0 || change.fSourceEnd > source->TotalLength())
			{
				outMessage = Ascii("restore: the change's range is outside the Source story.");
				return kFalse;
			}
			TextIterator iter(source, change.fSourceStart);
			iter.AppendToStringAndIncrement(words.get(), sourceCount);
		}

		InterfacePtr<ITextModelCmds> cmds(target, UseDefaultIID());
		if (cmds == nil)
		{
			outMessage = Ascii("restore: the story cannot be edited.");
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
				outMessage = Ascii("restore: the write failed (a locked story or layer?).");
				return kFalse;
			}
		}
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
			outMessage = Ascii("restore: the words of this paragraph changed as well - restore the words first, then this.");
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
				err = CreateRubyStrandIfNeeded(target);
				int32 len = targetCount;
				if (err == kSuccess && sourceCount > 0 && sourceCount != targetCount)
				{
					err = ClearRuby(target, change.fTargetStart, targetCount);
					len = sourceCount;
					if (change.fTargetStart + len > targetLength)
						len = targetLength - change.fTargetStart;
				}
				if (err == kSuccess)
					err = ApplyRuby(target, change.fTargetStart, len, change.fOtherRuby, change.fOtherRubyGroup);
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
			if (!change.fOtherRuby.IsEmpty() && !KentenKindOf(change.fOtherRuby, kind))
			{
				outMessage = Ascii("restore: this kenten kind cannot be written back (a custom mark carries a character the row does not hold).");
				return kFalse;
			}
			RestoreSequence undo;
			err = ApplyKentenKind(target, change.fTargetStart, targetCount, kind);
			outMessage = (kind == IKentenStyle::Kenten_None) ? Ascii("Took the kenten off ") : Ascii("Restored the kenten \"");
			if (kind != IKentenStyle::Kenten_None) { outMessage.Append(change.fOtherRuby); outMessage.Append("\" over "); }
			outMessage.AppendNumber(targetCount);
			outMessage.Append(" character(s)");
		}
		else
		{
			outMessage = Ascii("restore: this kind of change is not restorable yet.");
			return kFalse;
		}
		if (err != kSuccess)
		{
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			outMessage = Ascii("restore: the attribute could not be written.");
			return kFalse;
		}
	}

	// The row's story, diffed again: the restored change leaves the list, and every other
	// change's positions are named afresh against the text as it now stands.
	const int32 left = KCMStoryDiffRun::RunOne(targetDB, sourceDB, nth);
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
