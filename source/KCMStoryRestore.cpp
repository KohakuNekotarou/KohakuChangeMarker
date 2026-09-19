//========================================================================================
//
//  KCMStoryRestore.cpp -- see the header.
//
//  THREE KINDS OF CHANGE, THREE WAYS BACK, ONE UNDO STEP EACH:
//   - words ........ ReplaceCmd / InsertCmd / DeleteCmd (KCMCreateWordsWriteCmd) with the older words read raw from the Source;
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
#include "ErrorUtils.h"
#include "TextID.h"				// kCharAttrStrandBoss, kPrivateCreateStrandCmdBoss
#include "TextIterator.h"			// AppendToStringAndIncrement - the older words, raw
#include "TextChar.h"				// kTextChar_Table - which side of a table an insertion goes
#include "WideString.h"
#include <string>
#include <vector>					// the replaced rows a bulk run holds until its one re-diff is done

#include "K2SmartPtr.h"			// K2::scoped_ptr - WideString::Substring hands back a new one
#include "KCMStoryRestore.h"
#include "KCMSourceCache.h"		// the Source story kept from the origin: no copy per press
#include "KCMCore.h"				// KCMArmedTargetDB / KCMArmedSourceDB / KCMIsDocDBOpen
#include "KCMOriginCompare.h"		// KCMOriginArmed / KCMOriginScopedCopy / KCMOriginToSourceUID
#include "KCMStoryList.h"			// the row and its changes
#include "KCMStoryKinds.h"			// kKCMStoryAttrRuby / kKCMStoryAttrKenten / KCMStoryWriteBlock
#include "KCMParaText.h"			// IsObjectCharacter - what text cannot bring back
#include "KCMParagraphStyle.h"		// the next style for a whole paragraph taken in after another
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

/*	SourceWordsFor
	The OLDER side's characters for one range of one story: from the text kept for the origin when
	there is any, and from the Source story otherwise.

	★**ONE PLACE, BECAUSE TWO CALLERS NEED THE SAME SLICE** (2026-09-16). "Restore Source Text"
	  writes these characters; "Undo the Restore" compares them against what is in the document, to
	  find out whether what it is about to overwrite is still what the take-in put there. The two
	  must agree about what "the older words" are, or the check would pass on a slice the write
	  would not have produced.
	⚠**NEVER RE-ASSEMBLED FROM THE PARAGRAPHS** - KCMSourceCache.h says what that gets wrong, and
	 it is the paragraph break itself.

	@param whyNot filled, and already worded for the reader, on every kFalse.
*/
bool16 SourceWordsFor(UID storyUID, IDataBase* sourceDB, TextIndex from, int32 count,
					  WideString& out, PMString& whyNot)
{
	out = WideString();
	if (count <= 0)
		return kTrue;				// an insertion: nothing stood there, and that is an answer

	WideString keptRaw;
	if (KCMSourceCacheGetRaw(storyUID, keptRaw))
	{
		if (from < 0 || from + count > keptRaw.Length())
		{
			whyNot = Refused("the change's range is outside the Source story.");
			return kFalse;
		}
		K2::scoped_ptr<WideString> slice(keptRaw.Substring(from, count));
		if (slice.get() != nil)
			out = *slice;
		return kTrue;
	}

	InterfacePtr<ITextModel> source(sourceDB != nil
		? UIDRef(sourceDB, KCMOriginToSourceUID(sourceDB, storyUID))
		: UIDRef(nil, kInvalidUID), UseDefaultIID());
	if (source == nil)
	{
		whyNot = Refused("the story is not in the Source.");
		return kFalse;
	}
	if (from < 0 || from + count > source->TotalLength())
	{
		whyNot = Refused("the change's range is outside the Source story.");
		return kFalse;
	}
	TextIterator iter(source, from);
	iter.AppendToStringAndIncrement(&out, count);
	return kTrue;
}

/*	TargetWordsAt
	What the TARGET story reads right now over one range. Used to ask the one question a write that
	puts something back has to ask: is what I am about to overwrite still what went in?
*/
void TargetWordsAt(ITextModel* target, TextIndex at, int32 count, WideString& out)
{
	out = WideString();
	if (target == nil || count <= 0)
		return;
	if (at < 0 || at + count > target->TotalLength())
		return;
	TextIterator iter(target, at);
	iter.AppendToStringAndIncrement(&out, count);
}

/*	InsertionSideInTarget
	Where a pure INSERTION goes in the Target when tables stand right before the position the diff
	named (2026-09-17, the import matrix's G1).

	★★★**THE TEXT CANNOT TELL "BEFORE THE TABLE" FROM "AFTER IT".** The diff counts in the paragraph's
	  text, where a table's own characters are left out, so words typed right before a table and right
	  after it come out as the same text position - and the Target's model position for it is the one
	  AFTER the table. Measured: `表の前の文追加` taken in came back as `表の前の文` [table] `追加表の後の文`.
	★**THE SOURCE KNOWS**: there the words stand where they belong. When a table's anchor follows them
	  in the Source, they go in front of the matching table in the Target - matched by counting the tables
	  that stand directly before the words on each side.
	★★**A BACKSTOP SINCE THE SAME AFTERNOON (G2)**: the diff now cuts every change where the tables and
	  note references stand and names the right side itself (KCMParaText::CutChangeAtObjects), so this
	  finds no table right before `at` and answers `at`. It stays for a run whose objects could not be
	  paired - measured nowhere yet, and cheap: it reads a character or two.
	@return `at` whenever anything is unclear: no table right before it, a Source that cannot be read. */
TextIndex InsertionSideInTarget(ITextModel* target, TextIndex at, UID storyUID, IDataBase* sourceDB,
								TextIndex sourceStart, int32 sourceCount)
{
	// The anchors of the tables standing right before `at` in the Target, in order.
	std::vector<TextIndex> anchors;
	for (TextIndex s = at; s > 0; --s)
	{
		WideString one;
		TargetWordsAt(target, s - 1, 1, one);
		const int32 cp = (one.CharCount() == 1) ? static_cast<int32>(one.GetChar(0).GetValue()) : -1;
		if (cp != kTextChar_Table && cp != kTextChar_TableContinued)
			break;
		if (cp == kTextChar_Table)
			anchors.insert(anchors.begin(), s - 1);
	}
	if (anchors.empty())
		return at;

	// In the Source: does a table's anchor follow the words, and how many tables stand right before them?
	PMString ignored;
	WideString next;
	if (!SourceWordsFor(storyUID, sourceDB, sourceStart + sourceCount, 1, next, ignored)
		|| next.CharCount() != 1 || static_cast<int32>(next.GetChar(0).GetValue()) != kTextChar_Table)
		return at;

	size_t before = 0;
	for (TextIndex q = sourceStart; q > 0; --q)
	{
		WideString one;
		if (!SourceWordsFor(storyUID, sourceDB, q - 1, 1, one, ignored) || one.CharCount() != 1)
			break;
		const int32 cp = static_cast<int32>(one.GetChar(0).GetValue());
		if (cp != kTextChar_Table && cp != kTextChar_TableContinued)
			break;
		if (cp == kTextChar_Table)
			++before;
	}
	return (before < anchors.size()) ? anchors[before] : at;
}

/*	HoldsObjectCharacter
	Whether any character of `words` is one InDesign hangs an object on (KCMParaText::IsObjectCharacter).

	★★★**ASKED OF EVERY WRITE, BOTH WAYS** (2026-09-16, measured the same night): a text command puts
	  back the CHARACTER and not the object - an anchored rectangle came back as U+FFFC alone - and
	  removing one takes the object with it. The diff has already hidden the menu item for such a
	  change (KCMStoryChange::fWriteBlock); this is the write asking again of the characters as
	  they stand now, because the reader can type between the menu and the press.
*/
bool16 HoldsObjectCharacter(const WideString& words)
{
	for (int32 i = 0; i < words.CharCount(); ++i)
	{
		if (KCMParaText::IsObjectCharacter(static_cast<int32>(words.GetChar(i).GetValue())))
			return kTrue;
	}
	return kFalse;
}

/*	WriteBlockedMessage
	The refusal for a change the diff marked as not writable - one sentence per reason, so a bulk run
	that skips it can quote the reason on the status line.
*/
PMString WriteBlockedMessage(int32 writeBlock)
{
	if (writeBlock == kKCMWriteBlockedKind)
		return Refused("a warichu or tate-chu-yoko change outside the Import mode is shown for reading - "
					   "it is not written back.");
	return (writeBlock == kKCMWriteBlockedPlaces)
		? Refused("this change is in a table cell or a footnote that the other version does not have - "
				  "its words cannot be put back as text.")
		: Refused("this change holds a table, a note, an anchored object or another special character - "
				  "text cannot bring it back, and removing it would delete the object.");
}

/*	NotAgainstTwoDocumentsMessage
	The refusal every write gives when KCMStoryWritesAllowed says no - one wording, whichever item
	was pressed (the menu hides them all, so this is what a script or a stale menu reaches).
*/
PMString NotAgainstTwoDocumentsMessage()
{
	return Refused("the Source is a document of its own, so nothing is written back from here - "
				   "take what you need from the Source. (Restore works against a Task Start.)");
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

	// ★★**NOT AGAINST TWO DOCUMENTS** (2026-09-16, the user's rule - KCMStoryRestore.h says why).
	if (!KCMStoryWritesAllowed())
	{
		outMessage = NotAgainstTwoDocumentsMessage();
		return kFalse;
	}

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
	// ***** ALREADY TAKEN IN - OR TAKEN IN AND THEN UNDONE, WHICH IS NOT THE SAME THING. *****
	// ★★★**THE UNDONE ONE IS TAKEN IN AGAIN, AFTER COMPARING THE STORY AFRESH** (2026-09-16, the
	//   user's ask: "it says refresh - refreshing worked, but maybe it should refresh by itself").
	//   It refused both cases until then, with the same words, and the reasoning for refusing the
	//   undone one was sound as far as it went: a write moves every position after it, so the
	//   OTHER rows were named against text that the undo has since taken back. What was wrong was
	//   the remedy - making the reader run "Refresh Story Comparison" by hand, which is a dead end
	//   the moment they do not know that is what the message means. So the refresh is done here,
	//   where commands MAY run, and the change is looked up again on the far side of it.
	// ⚠**THE UNDO ITSELF CANNOT DO THIS** - a re-diff can need the task-start copy rehydrated,
	//   which runs commands, and commands cannot run inside a lazy notification. That is why the
	//   rows are kept rather than deleted (the tail of this function says so at length), and it is
	//   why "automatically" means "at the next press" rather than "at the undo".
	// ★**SAME SHAPE AS THE COUNTER CASE BELOW**, which the user turned from a refusal into a
	//   re-diff on 2026-09-15 for the same reason.
	bool16 takingInAnUndoneOne = kFalse;
	if (alreadyReplaced)
	{
		// ⚠**THE RE-DIFF BELONGS TO THE SINGLE PRESS** (`standalone`), the same way the counter
		//   case does: a bulk run refreshes ONCE at its start and walks backwards so that its own
		//   writes cannot invalidate what it has not reached - and it therefore never hands an
		//   already-replaced index down here. Refusing rather than dropping records for a caller
		//   that is not going to re-diff is what keeps that true whoever calls next.
		if (!standalone || KCMStoryDiffRun::StillReplaced(*row, *found))
		{
			// Still standing as replaced: the row is kept in the list precisely so the reader can
			// see what they took in, and taking it in twice would write the same words over words
			// that already match them.
			outMessage = Refused("this change has already been taken in - "
							   "run Refresh Story Comparison on its row to start over.");
			return kFalse;
		}
		takingInAnUndoneOne = kTrue;
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
	// ★★★**AND MOST PRESSES NOW NEED NO SOURCE DOCUMENT AT ALL** (2026-09-16, the user: "it is too
	//   heavy to work with"). Against a Task Start the Source was a byte string rebuilt into a
	//   whole document - kNewDocumentCmdBoss, ImportINX, the page names - for every single change
	//   taken in. What that document was asked for is kept instead, read once when the comparison
	//   was set up: the older words, and the paragraphs the re-diff needs (KCMSourceCache.h).
	//   ⚠**IT CHANGES NOTHING FOR TWO OPEN DOCUMENTS.** An armed Source is handed back by
	//    KCMArmedSourceDB without rehydrating anything - that path was never the slow one - and it
	//    must not be cached either, because the reader can edit it.
	const bool16 sourceIsKept = KCMSourceCacheHas(storyUID);

	KCMOriginScopedCopy originCopy;
	IDataBase* sourceDB = sourceDBIn;
	if (sourceDB == nil)
	{
		sourceDB = KCMArmedSourceDB();
		if (sourceDB == nil && KCMOriginArmed() && !sourceIsKept)
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
	if (sourceDB != nil && !KCMIsDocDBOpen(sourceDB))
		sourceDB = nil;					// closed under us; the kept text may still answer

	if (sourceDB == nil && !sourceIsKept)
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
	// ⚠**THE UNDONE RECORDS GO FIRST, AND ONLY THEN THE RE-DIFF.** RefindAfterEdit looks through
	//   the MERGED list and skips anything marked replaced, so the stale record of this very
	//   change would hide the live one the re-diff is about to produce - and the change would come
	//   back as "not there any more". ★Only the records an undo has taken back are dropped: the
	//   ones still standing are the reader's own record of what IS in the document (DropUndoneReplaced).
	// ⚠**`found` AND `row` POINT INTO THE LIST BEING REWRITTEN** - `change` and `storyUID` above
	//   are copies, taken before this, and nothing below reads either pointer again.
	if (takingInAnUndoneOne)
		KCMStoryDiffRun::DropUndoneReplaced(nth);

	if (standalone && (takingInAnUndoneOne || target->GetTextChangeCount() != countThen)
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

	// ★★**NOT A CHANGE THE DIFF SAID CANNOT GO BACK** (2026-09-16, the user's rule). The menu hides
	//   the item for these; a bulk run reaches them anyway, and skips them with this reason.
	// ⚠**ASKED BEFORE THE WORDS/ATTRIBUTE SPLIT** since the same day: it sat inside the words branch
	//   while only text changes could carry a reason, and a warichu or tate-chu-yoko (ATTRIBUTE
	//   changes marked kKCMWriteBlockedKind) would otherwise fall through to "not restorable yet".
	if (change.fWriteBlock != kKCMWriteAllowed)
	{
		outMessage = WriteBlockedMessage(change.fWriteBlock);
		return kFalse;
	}

	// ===== the words =============================================================================
	if (change.fWhat == KCMStoryChange::kText)
	{
		// ★★★**THE OLDER WORDS COME FROM WHAT WAS KEPT, WHEN ANYTHING WAS** (2026-09-16). The slice
		//   is taken by the very same indices out of the very same characters - the whole story as
		//   TextIterator read it, once, when the comparison was set up. ⚠**NOT re-assembled from
		//   the paragraphs**: those have their break characters and a table's own characters taken
		//   out, and putting them back is a second answer to a question the document has already
		//   answered (KCMSourceCache.h).
		boost::shared_ptr<WideString> words(new WideString());
		if (!SourceWordsFor(storyUID, sourceDB, change.fSourceStart, sourceCount, *words, outMessage))
			return kFalse;

		// ★★★**WHAT IS ABOUT TO COME OUT, AS THE CHARACTERS THEMSELVES** - kept on the record for
		//   "Undo the Restore", and asked the same question as the words going in.
		//   ⚠**NEVER change.fText**: that is the row's quote, cut to kExcerptCodePoints with its
		//    paragraph breaks drawn as pilcrows (measured: 80 characters came back as 60, and a
		//    break came back as U+00B6).
		WideString goingOut;
		TargetWordsAt(target, change.fTargetStart, targetCount, goingOut);
		if (HoldsObjectCharacter(*words) || HoldsObjectCharacter(goingOut))
		{
			outMessage = WriteBlockedMessage(kKCMWriteBlockedObjects);
			return kFalse;
		}
		done.fBeforeRaw = goingOut;

		InterfacePtr<ITextModelCmds> cmds(target, UseDefaultIID());
		if (cmds == nil)
		{
			outMessage = Refused("the story cannot be edited.");
			return kFalse;
		}

		// ★★WHICH SIDE OF A TABLE AN INSERTION GOES (2026-09-17, the import matrix's G1). The diff names
		//   a position in the TEXT, and words typed right before a table and right after it are the same
		//   position there. The diff names the side itself since G2 (CutChangeAtObjects); asking the Source
		//   as well is the backstop (InsertionSideInTarget says when it still matters).
		const TextIndex writeAt = (targetCount == 0 && words->Length() > 0 && !change.fWholeParagraph)
								  ? InsertionSideInTarget(target, change.fTargetStart, storyUID, sourceDB,
														  change.fSourceStart, sourceCount)
								  : change.fTargetStart;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		{
			RestoreSequence undo(standalone);

			// ★★**A WHOLE PARAGRAPH TAKEN OUT MOVES THE STYLES AFTER IT ALONG THEIR CHAIN** (2026-09-19, the
			//   user: "1 2 3 4 and 2 goes - 3 and 4 have to get the next styles, or the styles are wrong
			//   even though the words did not change"). The chain is read BEFORE the write, while the
			//   paragraph going out is still there to be the first link's "before"; re-chained after, in
			//   the same undo step, and only as far as the chain went (KCMParagraphStyle.h).
			const bool16 removesParagraph = (change.fWholeParagraph && words->Length() == 0 && targetCount > 0) ? kTrue : kFalse;
			KCMChainAfter chain;
			if (removesParagraph)
				KCMSnapshotChainAfter(target, writeAt, writeAt + targetCount, chain);

			InterfacePtr<ICommand> write(KCMCreateWordsWriteCmd(target, writeAt, targetCount, *words));
			if (write == nil || CmdUtils::ProcessCommand(write) != kSuccess)
			{
				ErrorUtils::PMSetGlobalErrorCode(kSuccess);
				outMessage = Refused("the write failed (a locked story or layer?).");
				return kFalse;
			}
			if (removesParagraph)
				KCMRechainAfterRemoval(target, writeAt, targetCount, chain);		// a style that cannot be applied leaves the one it had
			// ★★**A WHOLE PARAGRAPH TAKEN IN AFTER ANOTHER GETS THAT PARAGRAPH'S NEXT STYLE** (2026-09-17
			//   afternoon, the user's rule - both the Import and the Task Start, and taken from the document
			//   AS IT STANDS: "\rNEW" went in right before the return of the paragraph it follows, so it
			//   starts in that paragraph's style, the way Return starts it). A bulk run chains its runs of
			//   new paragraphs again once they are all in (BulkRun).
			//   ⚠In the same undo step as the words.
			if (change.fWholeParagraph && targetCount == 0 && words->Length() > 1
				&& static_cast<int32>(words->GetChar(0).GetValue()) == kTextChar_CR)
				KCMApplyNextStyleAfter(target, writeAt, writeAt + 1, static_cast<int32>(words->Length()) - 1);
		}
		// Where the replacement now stands. ⚠**THE START DID NOT MOVE, THE END DID**: what went in
		//   is as long as the source's side of the change, which is not the length that came out.
		//   WideString counts code points, the same unit TextIndex counts in
		//   ([[textindex-counts-code-points]]), so no conversion belongs here.
		done.fReplacedStart = writeAt;		// where it really went in (a table may stand after it - G1)
		done.fReplacedEnd   = writeAt + static_cast<int32>(words->Length());

		// ★★★**AND EVERYTHING ALREADY REPLACED FURTHER DOWN THE STORY SLIDES.** The live changes
		//   are about to be named afresh by RunOne, but a change that has already been replaced
		//   is not in that comparison any more - nothing else would move it. Replacing three
		//   words with five pushes every later replaced row along by two, and a row whose
		//   position quietly rots is a row whose jump lands in the wrong place.
		//   ⚠BEFORE the new one is added, so that it is not shifted by its own write.
		KCMStoryList::ShiftReplacedChanges(nth, writeAt,
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
		else if (change.fAttrKind == kKCMStoryAttrTcy || change.fAttrKind == kKCMStoryAttrWarichu)
		{
			// ★★TATE-CHU-YOKO AND WARICHU (2026-09-17) - the user's calls: the Import mode
			//   takes them in; the diff still blocks them everywhere else, so only an Import row gets
			//   this far. Each is one ON/OFF whose value IS its characters, so the Source's value says
			//   only one thing: where it is ON. Off over the Target's stretch, then on over the Source's
			//   reach from the same start - which is one write when the stretch kept its length, and
			//   right either way when it grew or shrank.
			const bool16 isWarichu = (change.fAttrKind == kKCMStoryAttrWarichu) ? kTrue : kFalse;
			ErrorCode (*const apply)(ITextModel*, TextIndex, int32, bool16) = isWarichu ? KCMApplyWarichu : KCMApplyTcy;
			const bool16 sourceHasIt = change.fOtherRuby.IsEmpty() ? kFalse : kTrue;
			int32 len = targetCount;
			if (sourceHasIt && sourceCount > 0)
			{
				len = sourceCount;
				if (change.fTargetStart + len > targetLength)
					len = targetLength - change.fTargetStart;
			}

			// ★★★**THE CHARACTERS ABOUT TO BE SET HAVE TO BE THE SOURCE'S.** The value is the characters,
			//   so this one test says whether the two sides' positions still name the same words. When
			//   the words inside it changed as well, the Source's stretch would land on whatever the
			//   Target has there - silently, on the wrong characters. So the words go back first, the
			//   same order a ruby keeps for the same reason.
			if (sourceHasIt)
			{
				WideString standing;
				TargetWordsAt(target, change.fTargetStart, len, standing);
				if (standing != WideString(change.fOtherRuby))
				{
					outMessage = isWarichu
						? Refused("the characters under this warichu are not the Source's - "
								  "restore the words first, then this.")
						: Refused("the characters under this tate-chu-yoko are not the Source's - "
								  "restore the words first, then this.");
					return kFalse;
				}
			}

			RestoreSequence undo(standalone);
			err = kSuccess;
			if (!sourceHasIt || len != targetCount)
				err = apply(target, change.fTargetStart, targetCount, kFalse);
			if (sourceHasIt && err == kSuccess)
				err = apply(target, change.fTargetStart, len, kTrue);
			if (isWarichu)
				outMessage = sourceHasIt ? Ascii("Set warichu over ") : Ascii("Took the warichu off ");
			else
				outMessage = sourceHasIt ? Ascii("Set tate-chu-yoko over ") : Ascii("Took the tate-chu-yoko off ");
			outMessage.AppendNumber(sourceHasIt ? len : targetCount);
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
			// The counter this change is measured by. ★This number IS "replaced": the row is drawn
			// that way while the story has got at least as far as it, and an undo - which takes
			// the counter back - undraws it without a line of undo-specific code.
			// ⚠**ASKED THROUGH CountForKind, NOT TAKEN FROM THE ROW** (2026-09-16): for a change
			//   to the WORDS it is the same number the re-diff has just recorded on the row, but
			//   a ruby or a kenten has to be measured by the aggregate counter, because the text
			//   counter does not move for either - and then neither the write nor its undo would
			//   be visible here. The choosing lives in ONE place, and CountForKind is it.
			done.fReplacedCount = KCMStoryDiffRun::CountForKind(
										UIDRef(targetDB, after->fStoryUID), done.fAttrKind);
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
	// ★★**AND THE RECORDS AN UNDO HAS TAKEN BACK GO WITH IT** (2026-09-16): a change taken in and
	//   then undone is a candidate again, and it only returns to the LIVE list when the story is
	//   compared afresh. Dropping first, then re-diffing, is the same order the single press uses
	//   and for the same reason - a stale record hides the live change behind it.
	// ⚠**DropUndoneReplaced IS ASKED FIRST AND THE `||` IS DELIBERATE**: the text counter does not
	//   move for a ruby, so an undone ruby would not be caught by the counter test beside it.
	const bool16 someWereUndone = KCMStoryDiffRun::DropUndoneReplaced(nth);
	if ((someWereUndone || target->GetTextChangeCount() != countThen)
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

	// ★★**RUNS OF NEW PARAGRAPHS GET THEIR NEXT STYLES CHAINED, ONCE THEY ARE ALL IN** (2026-09-17 afternoon,
	//   the user: "only the bulk take-in needs to care"). Walking backwards puts the paragraphs of one run
	//   in right before the same return, each in front of the last - the right order, but each styled after
	//   the paragraph before the run, not after the new one before it. Chained over the whole run from the
	//   paragraph before it, they come out as pressing Return in order gives them.
	//   The run is found by the caret the diff named for all of its paragraphs (fBeforeStart), and where it
	//   stands now by the replaced ranges, which have followed every write since.
	{
		std::vector<bool16> chained(dones.size(), kFalse);
		for (size_t k = 0; k < dones.size(); ++k)
		{
			if (chained[k] || !dones[k].fWholeParagraph || dones[k].fBeforeEnd != dones[k].fBeforeStart)
				continue;
			TextIndex from = dones[k].fReplacedStart;
			TextIndex to = dones[k].fReplacedEnd;
			int32 members = 1;
			for (size_t m = k + 1; m < dones.size(); ++m)
			{
				if (!chained[m] && dones[m].fWholeParagraph && dones[m].fBeforeEnd == dones[m].fBeforeStart
					&& dones[m].fBeforeStart == dones[k].fBeforeStart)
				{
					chained[m] = kTrue;
					from = (dones[m].fReplacedStart < from) ? dones[m].fReplacedStart : from;
					to = (dones[m].fReplacedEnd > to) ? dones[m].fReplacedEnd : to;
					++members;
				}
			}
			chained[k] = kTrue;
			WideString first;
			TargetWordsAt(target, from, 1, first);
			if (members > 1 && to - from > 1 && first.CharCount() == 1
				&& static_cast<int32>(first.GetChar(0).GetValue()) == kTextChar_CR)
				KCMApplyNextStyleAfter(target, from, from + 1, to - from - 1);
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
				// ⚠Each change by ITS OWN instrument - a bulk run can hold words and rubies
				//   together, and the two are not measured by the same counter (CountForKind).
				dones[k].fReplacedCount = KCMStoryDiffRun::CountForKind(
												UIDRef(targetDB, after->fStoryUID), dones[k].fAttrKind);
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

bool16 KCMStoryWritesAllowed()
{
	// KCMOriginArmed is true exactly while the armed Source is a rehydrated origin: a Task Start, or
	// the Import mode's snapshot (it takes the origin slot too). Two open documents, and the lent
	// database, arm a real Source database instead - KCMArmedSourceDB is then non-nil.
	return KCMOriginArmed();
}

/*	KCMUndoRestoreChange
	"Undo the Restore" / "Change Back to the Original" (2026-09-16, the user's ask: "Ctrl+Z puts it
	back, but I want it on the right-click menu too").

	★★**IT IS A COMMAND, NOT Edit > Undo, AND THAT IS THE WHOLE VALUE OF IT.** Ctrl+Z can only take
	  back the LAST thing done; this takes back the one change the reader points at, whatever they
	  have done since, and is itself one undo step.
	★**THE ROW ALREADY HOLDS BOTH SIDES OF ITSELF** - that is why it is kept in the list after a
	  take-in (KCMStoryList.h, fBefore*), so nothing has to be worked out again here. Words come
	  from fBeforeRaw (⚠never fBeforeText, the row's quote - measured 2026-09-16 turning 80
	  characters into 60); a ruby or a kenten from fRuby, which is the TARGET's own value, the one the
	  take-in wrote over (RestoreOne writes fOtherRuby, the Source's - this is its mirror).
	⚠**THE RECORD IS TAKEN OUT BY HAND, not left to the counter.** StillReplaced answers by asking
	  whether the story has got as far as the write, and this write moves it FURTHER - so the
	  counter alone would go on saying "replaced" over text that has just been put back. The record
	  goes, the story is compared again, and the change returns to the list as a live difference.
*/
bool16 KCMUndoRestoreChange(int32 nth, int32 which, PMString& outMessage)
{
	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);

	if (!KCMStoryWritesAllowed())
	{
		outMessage = NotAgainstTwoDocumentsMessage();
		return kFalse;
	}

	bool16 isReplaced = kFalse;
	const KCMStoryChange* const found = KCMStoryList::GetMergedChange(nth, which, isReplaced);
	const KCMStoryRow* const row = KCMStoryList::GetRow(nth);
	if (found == nil || row == nil)
	{
		outMessage = Refused("no such change (the list was rebuilt - right-click the row again).");
		return kFalse;
	}
	if (!isReplaced)
	{
		outMessage = Refused("this change has not been taken in, so there is nothing to put back.");
		return kFalse;
	}
	if (!KCMStoryDiffRun::StillReplaced(*row, *found))
	{
		// An undo has already taken it back; saying so beats writing the same words twice.
		outMessage = Refused("this change is already back the way it was.");
		return kFalse;
	}

	// ⚠**COPIES FIRST**: the list is rewritten below and both pointers go stale with it.
	const KCMStoryChange change = *found;
	const UID storyUID = row->fStoryUID;

	IDataBase* const targetDB = KCMArmedTargetDB();
	if (targetDB == nil || !KCMIsDocDBOpen(targetDB))
	{
		outMessage = Refused("the Target document is not open.");
		return kFalse;
	}
	InterfacePtr<ITextModel> target(UIDRef(targetDB, storyUID), UseDefaultIID());
	if (target == nil)
	{
		outMessage = Refused("the story is no longer in the Target document.");
		return kFalse;
	}

	const TextIndex at = change.fReplacedStart;
	const int32 len = change.fReplacedEnd - change.fReplacedStart;
	if (at < 0 || len < 0 || at + len > target->TotalLength())
	{
		outMessage = Refused("what went in is no longer where it was - "
						   "run Refresh Story Comparison on its row.");
		return kFalse;
	}

	// ***** IS WHAT I AM ABOUT TO OVERWRITE STILL WHAT WENT IN? *****
	// ⚠★★★**NOTHING ELSE ASKS THIS, AND WITHOUT IT THE ITEM CAN OVERWRITE THE READER'S OWN WORDS**
	//   (found by re-reading this on 2026-09-16, the day it was written). A later TAKE-IN is
	//   accounted for - ShiftReplacedChanges slides the records that stand after it - but nothing
	//   moves them when the reader TYPES, and StillReplaced above cannot tell: it asks whether the
	//   story has got at least as far as the write, and an ordinary edit lifts that counter too.
	//   So the range could name text the reader wrote, and the older words would go silently on
	//   top of it.
	// ★**THE TEST IS THE CHARACTERS THEMSELVES**, which is the one answer that cannot be
	//   wrong-footed: what the take-in put there is the SOURCE's words for this change, and
	//   SourceWordsFor is the function the write itself uses to produce them.
	// ⚠**WORDS ONLY, AND THAT IS NOT LAZINESS.** A ruby's two sides may cover DIFFERENT NUMBERS OF
	//   CHARACTERS - RestoreOne clears and rewrites at the older length when they do - so the two
	//   slices would differ in length whenever a reading's span had grown or shrunk, and the test
	//   would refuse a perfectly good change. What it is guarding against is text loss, and only
	//   the words branch writes text; an attribute landing on the wrong characters is wrong, but it
	//   is the same exposure the take-in itself has had all along, and it takes no words away.
	if (change.fAttrKind == kKCMStoryAttrNone)
	{
		IDataBase* checkSourceDB = KCMArmedSourceDB();
		KCMOriginScopedCopy checkCopy;
		if (checkSourceDB == nil && KCMOriginArmed() && !KCMSourceCacheHas(storyUID))
		{
			PMString whyNot;
			if (checkCopy.Open(whyNot))
				checkSourceDB = checkCopy.DB();
		}

		WideString wentIn;
		const int32 sourceCount = change.fSourceEnd - change.fSourceStart;
		if (!SourceWordsFor(storyUID, checkSourceDB, change.fSourceStart, sourceCount,
							wentIn, outMessage))
			return kFalse;

		WideString standingThere;
		TargetWordsAt(target, at, len, standingThere);
		if (standingThere != wentIn)
		{
			outMessage = Refused("what went in is not there any more - the words have been edited "
							   "since. Run Refresh Story Comparison on its row.");
			return kFalse;
		}

		// ***** AND WHAT GOES BACK IS THE CHARACTERS THEMSELVES *****
		// ⚠★★★**fBeforeRaw, NEVER fBeforeText** (2026-09-16, measured before the fix): fBeforeText
		//   is the row's quote - cut to kExcerptCodePoints, paragraph breaks drawn as pilcrows - and
		//   written back it turned eighty characters into sixty and a paragraph break into U+00B6.
		//   A record without the raw characters is refused rather than guessed at.
		if (change.fBeforeRaw.CharCount() != (change.fBeforeEnd - change.fBeforeStart))
		{
			outMessage = Refused("the words that stood here before were not kept, so they cannot be put "
							   "back - Ctrl+Z still can.");
			return kFalse;
		}
		if (HoldsObjectCharacter(change.fBeforeRaw))
		{
			outMessage = WriteBlockedMessage(kKCMWriteBlockedObjects);
			return kFalse;
		}
	}

	// A kenten this build cannot write is judged BEFORE anything is written, the same way the
	// take-in judges it: a refusal then costs nothing.
	int16 kentenKind = IKentenStyle::Kenten_None;
	if (change.fAttrKind == kKCMStoryAttrKenten && !change.fRuby.IsEmpty()
		&& !KCMKentenKindOf(change.fRuby, kentenKind))
	{
		outMessage = Refused("this kenten kind cannot be written back (a custom mark carries a "
						   "character the row does not hold).");
		return kFalse;
	}

	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	ErrorCode err = kFailure;
	{
		ICommandSequence* const seq = CmdUtils::BeginCommandSequence("KCMUndoRestoreChange");
		if (seq != nil)
			seq->SetName(KCMGetCompareMode() == kKCMModeImport
						 ? Ascii("Change Back to the Original")
						 : Ascii("Undo the Restore"));

		if (change.fAttrKind == kKCMStoryAttrRuby)
		{
			// ★**WHAT THE TAKE-IN WROTE OVER, WHICH IS NOT ALWAYS `len`** (2026-09-16, measured before
			//   the fix). When the two sides' spans differ, RestoreOne takes the Target's reading off
			//   and writes the Source's at the SOURCE's length. Putting the Target's reading back over
			//   `len` alone left the rest of that span holding the Source's reading string and its
			//   mono/group setting (flag off, so nothing was drawn - but not the state it was taken
			//   from). So the whole written reach comes off first, the same thirty attributes the
			//   take-in clears, and only then does the older reading go on.
			const int32 sourceCount = change.fSourceEnd - change.fSourceStart;
			int32 written = len;
			if (!change.fOtherRuby.IsEmpty() && sourceCount > 0 && sourceCount != len)
			{
				written = sourceCount;
				if (at + written > target->TotalLength())
					written = target->TotalLength() - at;
			}
			const int32 reach = (written > len) ? written : len;

			if (change.fRuby.IsEmpty())
			{
				err = KCMClearRuby(target, at, reach);		// there was no ruby before it
				outMessage = Ascii("Took the ruby off again over ");
			}
			else
			{
				err = KCMCreateRubyStrandIfNeeded(target);
				if (err == kSuccess && written != len)
					err = KCMClearRuby(target, at, reach);
				if (err == kSuccess)
					err = KCMApplyRuby(target, at, len, change.fRuby, change.fRubyGroup);
				outMessage = Ascii("Put the ruby \"");
				outMessage.Append(change.fRuby);
				outMessage.Append("\" back over ");
			}
		}
		else if (change.fAttrKind == kKCMStoryAttrTcy || change.fAttrKind == kKCMStoryAttrWarichu)
		{
			// ★THE MIRROR OF THE TAKE-IN (2026-09-17, a warichu the same way). What it
			//   set ON reached the SOURCE's length from `at`, which can be further than `len` - so that
			//   whole reach goes off first, and then the Target's own stretch goes back on if it had one.
			//   When the reach is `len` and there was one, turning it on is the whole of it.
			const bool16 isWarichu = (change.fAttrKind == kKCMStoryAttrWarichu) ? kTrue : kFalse;
			ErrorCode (*const apply)(ITextModel*, TextIndex, int32, bool16) = isWarichu ? KCMApplyWarichu : KCMApplyTcy;
			const int32 sourceCount = change.fSourceEnd - change.fSourceStart;
			int32 reach = len;
			if (!change.fOtherRuby.IsEmpty() && sourceCount > len)
			{
				reach = sourceCount;
				if (at + reach > target->TotalLength())
					reach = target->TotalLength() - at;
			}
			err = kSuccess;
			if (change.fRuby.IsEmpty() || reach != len)
				err = apply(target, at, reach, kFalse);
			if (err == kSuccess && !change.fRuby.IsEmpty())
				err = apply(target, at, len, kTrue);
			if (isWarichu)
				outMessage = change.fRuby.IsEmpty()
							 ? Ascii("Took the warichu off again over ")
							 : Ascii("Put the warichu back over ");
			else
				outMessage = change.fRuby.IsEmpty()
							 ? Ascii("Took the tate-chu-yoko off again over ")
							 : Ascii("Put the tate-chu-yoko back over ");
		}
		else if (change.fAttrKind == kKCMStoryAttrKenten)
		{
			err = KCMApplyKentenKind(target, at, len, kentenKind);
			outMessage = (kentenKind == IKentenStyle::Kenten_None)
						 ? Ascii("Took the kenten off again over ")
						 : Ascii("Put the kenten back over ");
		}
		else
		{
			// The words: the Target's own characters as the take-in found them (fBeforeRaw, checked
			//   above). An insertion taken in is put back by deleting what went in; a deletion taken
			//   in (nothing went in, len 0) by inserting them (KCMCreateWordsWriteCmd picks which).
			InterfacePtr<ICommand> write(KCMCreateWordsWriteCmd(target, at, len, change.fBeforeRaw));
			err = (write != nil) ? CmdUtils::ProcessCommand(write) : kFailure;
			outMessage = Ascii("Put back ");
		}

		if (seq != nil)
			CmdUtils::EndCommandSequence(seq);
	}

	if (err != kSuccess)
	{
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		outMessage = Refused("it could not be written back (a locked story or layer?).");
		return kFalse;
	}

	outMessage.AppendNumber((change.fAttrKind == kKCMStoryAttrNone)
							? change.fBeforeRaw.CharCount() : len);
	outMessage.Append(" character(s)");

	// ★★★**AND EVERYTHING ALREADY REPLACED FURTHER DOWN THE STORY SLIDES** - the same rule the
	//   take-in keeps, and for the same reason: the other replaced records are not in any live
	//   comparison, so nothing else would move them, and a record whose position quietly rots is a
	//   row whose jump lands in the wrong place. ⚠**BEFORE this change's own record is removed**,
	//   so that the walk sees the list as it stood when the write happened.
	//   ⚠An attribute write changes no lengths, so the delta is zero and the call is a no-op -
	//    stated rather than branched on, because the reason it is zero is worth reading.
	{
		const int32 wentBack = (change.fAttrKind == kKCMStoryAttrNone)
							 ? change.fBeforeRaw.CharCount() : len;
		KCMStoryList::ShiftReplacedChanges(nth, at, wentBack - len);
	}

	// ***** THE RECORD GOES, AND THE STORY IS COMPARED AGAIN. *****
	// The change is a live difference once more - which is exactly what it was before the reader
	// took it in - so the row shows it that way and can take it in again.
	KCMStoryList::RemoveMergedReplacedChange(nth, which);

	// ⚠**AND NO COPY IS BUILT FOR IT WHEN THE STORY IS KEPT** - this item would otherwise carry
	//   the very cost that was taken out of the take-in the same day (KCMSourceCache.h): a whole
	//   document rebuilt from the origin, per press, to compare one story.
	IDataBase* sourceDB = KCMArmedSourceDB();
	KCMOriginScopedCopy originCopy;
	if (sourceDB == nil && KCMOriginArmed() && !KCMSourceCacheHas(storyUID))
	{
		PMString whyNot;
		if (originCopy.Open(whyNot))
			sourceDB = originCopy.DB();
	}
	if (sourceDB != nil && !KCMIsDocDBOpen(sourceDB))
		sourceDB = nil;
	if (sourceDB != nil || KCMSourceCacheHas(storyUID))
		KCMStoryDiffRun::RunOne(targetDB, sourceDB, nth);

	KCMNotify(kKCMStoryEditsRebuiltMessage);
	return kTrue;
}

bool16 KCMRestoreAllInStory(int32 nth, PMString& outMessage)
{
	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);

	// Asked before the undo step is opened, so a refusal leaves no empty step on the Edit menu.
	if (!KCMStoryWritesAllowed())
	{
		outMessage = NotAgainstTwoDocumentsMessage();
		return kFalse;
	}

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

	if (!KCMStoryWritesAllowed())
	{
		outMessage = NotAgainstTwoDocumentsMessage();
		return kFalse;
	}

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
