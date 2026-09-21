//========================================================================================
//
//  KCMStoryRestore.cpp -- see the header.
//
//  ⛔**THE RESTORE ITSELF WENT ON 2026-09-21** - "Restore Source Text", "Undo the Restore" and
//  "Restore All in This Story", with every helper only they used. The user's reason: "the Source
//  document is in front of you, so if you want it back, take it from there". A Task Start had become
//  a copy saved to a file and opened by Start, so EVERY comparison now has its older side open in a
//  window - and KCM writes nothing into the reader's text from a menu.
//
//  ★**WHAT IS LEFT HERE IS THE WRITERS**, which were always shared and now have no other home:
//   - words ........ ReplaceCmd / InsertCmd / DeleteCmd (KCMCreateWordsWriteCmd), for the import's
//                    pour into the document (KCMStoryTextImport.cpp);
//   - ruby ......... the three attributes that ARE a reading, or all thirty overrides taken off.
//                    The shape is Kohaku InDesign MCP's ruby tool (KIDMCPRuby.cpp: ApplyOneRun /
//                    ClearRubyOn), carried over rather than reinvented;
//   - kenten ....... the KIND written into kTAKentenKindBoss (Kenten_None = off), the look left
//                    alone - and the table that turns a comparison's name into that kind;
//   - tate-chu-yoko and warichu ... ON or OFF, their settings left alone.
//  The callers are the import's pour (KCMStoryAttrPour.cpp) and the PDF report's Story table
//  (KCMReportTable.cpp / KCMReport.cpp), which sets real ruby and real kenten in its cells.
//
//  ⚠**THE FILE KEEPS ITS NAME.** Renaming it would touch both vcxproj copies - the one the build
//  reads is outside the repo ([[vcxproj-registration-not-build-dependency]]) - and every include,
//  for nothing. The name is a grave marker, and this paragraph is what it marks.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IAttrReport.h"			// what an attribute is applied as
#include "IClassIDData.h"			// which strand PrivateCreateStrandCmd is to make
#include "ICommand.h"
#include "IKentenStyle.h"			// IKentenStyle::KentenKind
#include "IRubyStrand.h"			// IRubyAttrStrand (the file is IRubyStrand.h, the class is not)
#include "ITextAttrBoolean.h"		// kTARubyAttrBoss / kTAMojiRubyBoss / kTATatechuyokoAttrBoss
#include "ITextAttrInt16.h"		// kTAKentenKindBoss
#include "ITextAttrWideString.h"	// kTARubyStringBoss
#include "ITextModel.h"
#include "ITextModelCmds.h"
#include "IUIDData.h"				// which model PrivateCreateStrandCmd is to make it on
#include "AttributeBossList.h"
#include "CJKID.h"					// kRubyAttrStrandBoss, kTARuby*, kTAKentenKindBoss
#include "CmdUtils.h"
#include "CreateObject.h"			// CreateObject2 - how an attribute boss is made
#include "TextID.h"				// kCharAttrStrandBoss, kPrivateCreateStrandCmdBoss
#include "WideString.h"
#include <string>					// the kenten name table

#include "KCMStoryRestore.h"

// (⛔Sixteen more includes stood here and went with the restore on 2026-09-21 - the command
//  sequence, the armed databases and the Source cache, the row list, the paragraph styles, the
//  re-diff, the panel's notification, KCMTableRestore. What is left is what a writer needs.)
//
// (⛔So did eight helpers in an anonymous namespace: Ascii and Refused, which made the refusals
//  non-translatable; SourceWordsFor and TargetWordsAt, which read the two sides raw; the rule for
//  which side of a table an insertion goes back on; and the three wordings for a write that was
//  blocked. The measurements they carried are in docs/ai-notes/kcm-restore-retired-2026-09-21.md.)

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

/** One reading onto one range: the attributes that ARE a reading (on, the string, and for a GROUP
    reading its setting), and none of the twenty-seven that are its look. ⚠kTAMojiRubyBoss kTrue IS MONO.

    ★★**A MONO READING WRITES NO SETTING; A GROUP READING WRITES "GROUP"** (2026-09-17 afternoon, the
      user's rule). A paragraph style says which ruby its text gets, and a setting written on top of it
      that disagrees is an override. The markup tells the two apart by its shape - one <rt> over several
      characters is a group, readings split character by character (or over one character) are mono -
      so a group is said out loud and mono is left to the style. Until then the setting was written
      both ways.
    ⚠A reading that was an explicit MONO override on a GROUP style comes back from a restore as the
     style's group, for the same reason - the rule is the user's, and it is kept in this one place. */
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
	if (group)
	{
		InterfacePtr<ITextAttrBoolean> moji(::CreateObject2<ITextAttrBoolean>(kTAMojiRubyBoss));
		if (moji == nil) return kFailure;
		moji->SetFlag(kFalse);				// kFalse = group
		InterfacePtr<IAttrReport> report(moji, UseDefaultIID());
		attrs->ApplyAttribute(report);
	}
	InterfacePtr<ITextModelCmds> cmds(model, UseDefaultIID());
	if (cmds == nil)
		return kFailure;
	if (!group)
	{
		// ⚠**MONO TAKES A GROUP SETTING OFF, rather than merely not writing one**: a range that carried
		//   "group" as an override (a reading being rewritten in place, which the restore does without
		//   clearing first) would otherwise stay group under a mono reading.
		boost::shared_ptr<AttributeBossList> setting(new AttributeBossList);
		InterfacePtr<IAttrReport> report(::CreateObject2<IAttrReport>(kTAMojiRubyBoss));
		if (report == nil)
			return kFailure;
		setting->ApplyAttribute(report);
		InterfacePtr<ICommand> clear(cmds->ClearOverridesCmd(at, len, setting, kCharAttrStrandBoss));
		if (clear == nil || CmdUtils::ProcessCommand(clear) != kSuccess)
			return kFailure;
	}
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

// ---- tate-chu-yoko -------------------------------------------------------------------------

/** Tate-chu-yoko ON or OFF over [at, at+len) (2026-09-17, the user's request - the Import mode takes
    it in). ⚠**OFF IS A VALUE, kFalse**, not a cleared override: that is how the official snippet
    takes it off (SnpPerformTextAttrTateChuYoko.cpp's `SetTextBool16Attribute(..., kFalse)`), and it
    stays off under a character style that turns it on. The X/Y offsets are its look and are left
    alone, the way a kenten's look is. */
namespace
{

/** One boolean character attribute, ON or OFF, over [at, at+len) - the shape tate-chu-yoko and warichu
	share (each is one kTA*AttrBoss holding an ITextAttrBoolean on the character strand). */
ErrorCode ApplyBooleanCharAttr(ITextModel* model, TextIndex at, int32 len, ClassID boss, bool16 on)
{
	boost::shared_ptr<AttributeBossList> attrs(new AttributeBossList);
	InterfacePtr<ITextAttrBoolean> attr(::CreateObject2<ITextAttrBoolean>(boss));
	if (attr == nil)
		return kFailure;
	attr->SetFlag(on ? kTrue : kFalse);
	InterfacePtr<IAttrReport> report(attr, UseDefaultIID());
	attrs->ApplyAttribute(report);
	InterfacePtr<ITextModelCmds> cmds(model, UseDefaultIID());
	if (cmds == nil)
		return kFailure;
	InterfacePtr<ICommand> apply(cmds->ApplyCmd(RangeData(at, at + len), attrs, kCharAttrStrandBoss));
	return (apply != nil) ? CmdUtils::ProcessCommand(apply) : kFailure;
}

}	// namespace

ErrorCode KCMApplyTcy(ITextModel* model, TextIndex at, int32 len, bool16 on)
{
	return ApplyBooleanCharAttr(model, at, len, kTATatechuyokoAttrBoss, on);
}

// ---- warichu -------------------------------------------------------------------------------

/** Warichu ON or OFF over [at, at+len) (2026-09-17, the user's request - the Import mode takes it in,
    the way it took tate-chu-yoko in earlier the same day). ⚠**OFF IS A VALUE, kFalse**, the way the official
    snippet takes it off (SnpPerformTextAttrWarichu.cpp's RemoveWarichu: `SetTextBool16Attribute(...,
    kTAWarichuAttrBoss, kFalse)`). ★**ONLY THE ON/OFF.** The snippet's ApplyWarichu writes six settings
    beside it (lines, relative size, line spacing, alignment, the two break minimums); those are the
    warichu's look - the file does not carry them - and are left as each character has them. */
ErrorCode KCMApplyWarichu(ITextModel* model, TextIndex at, int32 len, bool16 on)
{
	return ApplyBooleanCharAttr(model, at, len, kTAWarichuAttrBoss, on);
}

// ---- words ---------------------------------------------------------------------------------

ICommand* KCMCreateWordsWriteCmd(ITextModel* model, TextIndex at, int32 count, const WideString& words)
{
	InterfacePtr<ITextModelCmds> cmds(model, UseDefaultIID());
	if (cmds == nil)
		return nil;

	// ★The official shape: a deletion is a DeleteCmd, never a ReplaceCmd with nothing to put in
	//   (the header says why, and where the crash that turned this into a rule is kept).
	if (words.Length() == 0)
		return (count > 0) ? cmds->DeleteCmd(at, count) : nil;

	boost::shared_ptr<WideString> in(new WideString(words));
	return (count > 0) ? cmds->ReplaceCmd(at, count, in) : cmds->InsertCmd(at, in);
}

// (⛔**EVERYTHING BELOW THIS POINT WAS THE RESTORE** - some 1,300 lines - and went on 2026-09-21:
//  the one-undo-step guard; RefindAfterEdit, which looked a change up afresh by its SOURCE range
//  when the reader had typed since the comparison; RestoreOne; BulkRun, whose walk went BACKWARDS
//  so that each write only disturbed text already passed; the two report helpers; and the four
//  exported entry points KCMRestoreChange / KCMRestoreAllInStory / KCMUndoRestoreChange /
//  KCMStoryWritesAllowed.
//  ★The measurements that shaped them - why the walk goes backwards, why a deletion is a DeleteCmd,
//  what a write block is - are kept in docs/ai-notes/kcm-restore-retired-2026-09-21.md, so that
//  removing the code did not take the findings with it. `git revert` of that commit brings it back.)

// End, KCMStoryRestore.cpp.
