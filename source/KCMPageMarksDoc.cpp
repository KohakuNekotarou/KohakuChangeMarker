//========================================================================================
//
//  KCMPageMarksDoc.cpp
//
//  Ticks and paws to and from the document's own script labels. The reasoning is in the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IScript.h"					// a page's scripting facet -- it IS an IScriptLabel
#include "IScriptLabel.h"				// ScriptLabelKey / ScriptLabelValue / the key-value list
#include "IScriptUtils.h"				// SetScriptingTags -- the official door (see below)

// General includes:
#include "PMReal.h"
#include "PMString.h"
#include "RequestContext.h"				// EngineContext -- default-constructed: no engine is asking
#include "ScriptData.h"					// ScriptList = adobe::vector<InterfacePtr<IScript> >
#include "Utils.h"

#include <map>
#include <set>
#include <string>
#include <vector>

// Project includes:
#include "KCMCore.h"					// KCMCollectPageUIDs / KCMCollectMasterPageUIDs / KCMInvalidateDB
#include "KCMJsonText.h"				// KCMJsonEscape / KCMJsonReadString -- shared with KCMPageCheck.cpp
#include "KCMModelNotify.h"				// KCMNotifyPages
#include "KCMPageCheck.h"				// the tick's store (Collect / ReplaceAll)
#include "KCMPageMarksCmd.h"			// KCMPageMarks / KCMMarksWrite -- Clear goes through the command
#include "KCMPageMarksDoc.h"
#include "KCMPawStamp.h"				// the paws' store

//========================================================================================
// The keys.
//
//  PREFIXED WITH THE PRODUCT so that a reader who opens the Script Label panel can see where the
//  text came from. Kohaku InDesign MCP labels its own the same way.
//========================================================================================
static const char* const kKCMCheckLabelKey   = "KohakuChangeMarker.check";
static const char* const kKCMPawsLabelKey    = "KohakuChangeMarker.paws";
static const char* const kKCMCheckLabelValue = "1";	// the key's presence IS the tick

//========================================================================================
// Numbers inside a label.
//
//  ⚠★★**THOUSANDTHS OF A POINT, AS INTEGERS, AND THAT IS NOT A DETAIL.** Writing 12.5 means
//    writing a decimal separator, and the separator FOLLOWS THE LOCALE -- a French InDesign writes
//    "12,5", which a reader of this file would take for two numbers. KCM met this once already
//    (the change ratio on the status line) and answered it the same way: keep the arithmetic in
//    integers and let the unit carry the scale.
//
//  ⚠A round trip therefore rounds to a thousandth of a point. That is deliberate and harmless: a
//    paw is drawn at a sixth of the page's short side, so a thousandth of a point cannot be seen
//    and cannot move a paw out of its own hit box.
//========================================================================================
static const double kKCMLabelScale = 1000.0;	// 1 = a thousandth of a point

static void KCMAppendInt(std::string& out, int32 v)
{
	char digits[24];
	int32 n = 0;
	const bool negative = (v < 0);
	uint32 u = (uint32)(negative ? -(int64)v : (int64)v);
	do { digits[n++] = (char)('0' + (u % 10)); u /= 10; } while (u != 0 && n < 20);
	if (negative)
		out += '-';
	while (n > 0)
		out += digits[--n];
}

/** Read an integer that starts AT `pos` (after any spaces), leaving `pos` just past it.
	@return kFalse when there is no integer there.

	⚠★★**IT DOES NOT SEARCH FORWARD, AND THAT IS THE POINT** (changed 2026-09-07). Until the paws
	  carried words this scanned ahead for the next digit, which was safe while every entry was
	  three numbers and nothing else. It stopped being safe the moment a paw could carry
	  `"t":"page 12"`: a scanning reader would have taken the 12 out of the reader's own words and
	  used it as the next paw's coordinate. Nothing would have reported it -- the paw would simply
	  have appeared somewhere else. */
static bool16 KCMReadIntAt(const std::string& text, size_t& pos, int32& out)
{
	while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
		++pos;

	const bool negative = (pos < text.size() && text[pos] == '-');
	if (negative)
		++pos;
	if (pos >= text.size() || text[pos] < '0' || text[pos] > '9')
		return kFalse;

	int64 v = 0;
	while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
	{
		v = v * 10 + (text[pos] - '0');
		if (v > 2000000000)
			v = 2000000000;
		++pos;
	}
	out = (int32)(negative ? -v : v);
	return kTrue;
}

static int32 KCMToThousandths(const PMReal& r)
{
	const double d = ToDouble(r) * kKCMLabelScale;
	return (int32)(d < 0.0 ? d - 0.5 : d + 0.5);
}

//========================================================================================
// One page's labels.
//========================================================================================

/** Every page of the document, ordinary ones first and then the master pages -- the set a tick or
	a paw can sit on. */
static void KCMCollectAllPages(IDataBase* db, std::vector<UID>& out)
{
	out.clear();
	KCMCollectPageUIDs(db, out);
	KCMCollectMasterPageUIDs(db, out);	// appends; a tick may sit on a master (2026-08-24)
}

/** This page's labels, ours included. kFalse when the page has no scripting facet at all. */
static bool16 KCMReadPageLabels(IDataBase* db, UID pageUID,
                                IScriptLabel::ScriptLabelKeyValueList& out)
{
	if (db == nil || pageUID == kInvalidUID)
		return kFalse;
	InterfacePtr<IScript> script(db, pageUID, UseDefaultIID());
	if (script == nil)
		return kFalse;
	out = script->GetTags();
	return kTrue;
}

/** Write exactly `labels` onto the page -- everything it had that is not in the list goes.

	★**Through IScriptUtils, which is the door Adobe leaves open.** This used to build
	 kSetScriptingTagCmdBoss by hand and fill in IScriptTagCmdData, which that header forbids in as
	 many words: "This interface is included in the SDK for reference purposes only and plug-in
	 developers should not invoke any of the 'Set' APIs in this interface" (IScriptTagCmdData.h).
	 IScriptUtils::SetScriptingTags processes the same command and is the supported way to reach it.

	The EngineContext is default-constructed: no script engine is asking, KCM is. Measured
	2026-09-07 -- the command accepts it and the step lands on the undo stack. */
static ErrorCode KCMWritePageLabels(IDataBase* db, UID pageUID,
                                    const IScriptLabel::ScriptLabelKeyValueList& labels)
{
	InterfacePtr<IScript> script(db, pageUID, UseDefaultIID());
	if (script == nil)
		return kFailure;

	ScriptList list;
	list.push_back(script);
	return Utils<IScriptUtils>()->SetScriptingTags(list, EngineContext(), labels,
	                                               kTrue /*replaceExistingLabels*/);
}

/** Is this one of ours? */
static bool16 KCMIsOurKey(const IScriptLabel::ScriptLabelKey& key)
{
	return (key == PMString(kKCMCheckLabelKey) || key == PMString(kKCMPawsLabelKey)) ? kTrue : kFalse;
}

//========================================================================================
// The paws of one page, as text and back.
//========================================================================================

/* ★"t" IS WRITTEN ONLY WHEN THERE IS ONE. Most paws carry no word, and an empty `"t":""` on every
   one of them would make the label longer for nothing -- a reader who opens the Script Label panel
   should see the short form when the short form is the truth. */
static void KCMPawsToText(const std::vector<KCMPawStamp>& paws, std::string& out)
{
	out = "[";
	for (size_t i = 0; i < paws.size(); ++i)
	{
		if (i != 0)
			out += ',';
		out += "{\"x\":";  KCMAppendInt(out, KCMToThousandths(paws[i].fX));
		out += ",\"y\":";  KCMAppendInt(out, KCMToThousandths(paws[i].fY));
		out += ",\"c\":";  KCMAppendInt(out, paws[i].fColour);
		if (!paws[i].fText.IsEmpty())
		{
			std::string escaped;
			KCMJsonEscape(paws[i].fText.GetUTF8String(), escaped);
			out += ",\"t\":\"";
			out += escaped;
			out += '\"';
		}
		out += '}';
	}
	out += ']';
}

/** Read one page's paws back out of the label.

	★**BY NAME, ENTRY BY ENTRY** (changed 2026-09-07, when paws gained words). It used to read three
	 numbers in a row and never look at the keys, which was fine while an entry could only be three
	 numbers. It is not fine now: "t" is optional and holds text the reader chose, so an entry has
	 to be walked as an object and its members recognised. See KCMReadIntAt for what the old reader
	 would have done to a word with a digit in it.
	⚠A broken entry ENDS the read rather than being skipped: the entries are ours and in order, so
	 the first thing that does not parse means the rest cannot be trusted either. What is already
	 in `out` is kept -- losing the paws before the damage as well would help nobody. */
static void KCMTextToPaws(const std::string& text, UID pageUID, std::vector<KCMPawStamp>& out)
{
	size_t pos = 0;
	for (;;)
	{
		const size_t open = text.find('{', pos);
		if (open == std::string::npos)
			return;						// no entry left: the ordinary end
		pos = open + 1;

		int32  x = 0, y = 0, c = 0;
		bool16 haveX = kFalse, haveY = kFalse, haveC = kFalse, broken = kFalse;
		std::string word;

		while (pos < text.size() && text[pos] != '}')
		{
			if (text[pos] != '\"')		// a comma, a space: step over it
			{
				++pos;
				continue;
			}

			std::string key;
			if (!KCMJsonReadString(text, pos, key))
			{
				broken = kTrue;
				break;
			}
			while (pos < text.size() && (text[pos] == ':' || text[pos] == ' '))
				++pos;

			if (key == "t")
			{
				if (!KCMJsonReadString(text, pos, word))
				{
					broken = kTrue;
					break;
				}
			}
			else
			{
				int32 v = 0;
				if (!KCMReadIntAt(text, pos, v))
				{
					broken = kTrue;
					break;
				}
				if      (key == "x") { x = v; haveX = kTrue; }
				else if (key == "y") { y = v; haveY = kTrue; }
				else if (key == "c") { c = v; haveC = kTrue; }
				// an unknown key with a number is read and dropped, which is how a newer KCM's
				// extra member would pass through an older one without breaking the entry
			}
		}

		if (broken)
			return;
		if (pos < text.size() && text[pos] == '}')
			++pos;

		// A position is the one thing a paw cannot do without. A colour can be missing (it defaults
		// to the current one) and a word usually is.
		if (!haveX || !haveY)
			return;

		PMString word16;
		word16.SetTranslatable(kFalse);
		if (!word.empty())
			word16.SetUTF8String(word);

		out.push_back(KCMPawStamp(pageUID,
		                          PMReal(x / kKCMLabelScale),
		                          PMReal(y / kKCMLabelScale),
		                          haveC ? KCMPawColourFromStored(c) : (int32)kKCMPawColourRed,
		                          word16));
	}
}

//========================================================================================
// Writing one page (the command's hands).
//========================================================================================

ErrorCode KCMMarksWriteOnePage(IDataBase* db, UID page, bool16 check,
                               const std::vector<KCMPawStamp>& paws)
{
	if (db == nil || page == kInvalidUID)
		return kFailure;

	IScriptLabel::ScriptLabelKeyValueList had;
	if (!KCMReadPageLabels(db, page, had))
		return kFailure;

	// Everything that is not ours, kept exactly as it was, and then ours as they should be.
	IScriptLabel::ScriptLabelKeyValueList want;
	bool16 hadOurs = kFalse;
	for (int32 k = 0; k < (int32)had.size(); ++k)
	{
		if (KCMIsOurKey(had[k].Key()))
			hadOurs = kTrue;
		else
			want.push_back(had[k]);
	}

	if (check)
	{
		PMString v(kKCMCheckLabelValue);
		v.SetTranslatable(kFalse);
		want.push_back(IScriptLabel::ScriptLabelKeyValuePair(PMString(kKCMCheckLabelKey), v));
	}
	if (!paws.empty())
	{
		std::string text;
		KCMPawsToText(paws, text);
		PMString v;
		v.SetTranslatable(kFalse);
		v.SetUTF8String(text);
		want.push_back(IScriptLabel::ScriptLabelKeyValuePair(PMString(kKCMPawsLabelKey), v));
	}

	// Nothing to say about this page, and nothing of ours on it: leave it completely alone. Writing
	// an identical list would still be a change to the document as far as undo is concerned.
	if (!hadOurs && !check && paws.empty())
		return kSuccess;

	return KCMWritePageLabels(db, page, want);
}

//========================================================================================
// Reading the whole document back into the session store.
//========================================================================================

/** The paws of a document, grouped by the page they sit on -- the shape a comparison needs. */
static void KCMGroupPawsByPage(const std::vector<KCMPawStamp>& paws,
                               std::map<UID, std::vector<KCMPawStamp> >& out)
{
	out.clear();
	for (size_t i = 0; i < paws.size(); ++i)
		out[paws[i].fPageUID].push_back(paws[i]);
}

static bool16 KCMSamePaws(const std::vector<KCMPawStamp>& a, const std::vector<KCMPawStamp>& b)
{
	if (a.size() != b.size())
		return kFalse;
	for (size_t i = 0; i < a.size(); ++i)
	{
		if (a[i].fColour != b[i].fColour || a[i].fX != b[i].fX || a[i].fY != b[i].fY)
			return kFalse;
	}
	return kTrue;
}

int32 KCMMarksSyncFromDocument(IDataBase* db, int32* outChecks, int32* outPaws)
{
	if (outChecks != nil) *outChecks = 0;
	if (outPaws   != nil) *outPaws   = 0;
	if (db == nil)
		return -1;

	std::vector<UID> pages;
	KCMCollectAllPages(db, pages);

	// ⚠★★**A DOCUMENT WE CANNOT READ MUST NOT EMPTY THE STORE.** Everything below ends in
	//   ReplaceAll, and Replace with nothing is Erase -- so a document that answered with no pages
	//   would silently throw away every tick and paw the reader had made. That is the worst thing
	//   this function could do, and it would look exactly like "the marks just vanished".
	//   ⇒ Nothing read, nothing replaced. Note the care needed to keep this from swallowing the
	//   legitimate case: "every page read fine and NONE of them carries a label" is a real answer
	//   (it is what Clear Marks leaves behind) and MUST still empty the store. So the test is not
	//   "did we find anything" but "could we read anything" -- two different questions.
	if (pages.empty())
		return -1;

	// What the store says NOW, taken before anything replaces it. Once it is replaced nothing can
	// name the pages that lost a mark, and those are exactly the ones whose picture changed.
	std::set<UID>            wasChecked;
	std::vector<KCMPawStamp> wasPawed;
	KCMPageCheckCollect(db, wasChecked);
	KCMPawStampGetForSave(db, wasPawed);

	std::set<UID>            checks;
	std::vector<KCMPawStamp> paws;
	int32                    carrying = 0;
	int32                    readable = 0;		// pages whose labels we could actually read

	for (size_t i = 0; i < pages.size(); ++i)
	{
		IScriptLabel::ScriptLabelKeyValueList had;
		if (!KCMReadPageLabels(db, pages[i], had))
			continue;
		++readable;

		bool16 hadOurs = kFalse;
		for (int32 k = 0; k < (int32)had.size(); ++k)
		{
			if (had[k].Key() == PMString(kKCMCheckLabelKey))
			{
				checks.insert(pages[i]);
				hadOurs = kTrue;
			}
			else if (had[k].Key() == PMString(kKCMPawsLabelKey))
			{
				KCMTextToPaws(had[k].Value().GetUTF8String(), pages[i], paws);
				hadOurs = kTrue;
			}
		}
		if (hadOurs)
			++carrying;
	}

	// The other half of the guard above: pages were listed but not one of them would answer, so we
	// have learnt nothing about this document and must change nothing.
	if (readable == 0)
		return -1;

	// REPLACE, NOT MERGE: the document is the answer, and this is putting the cache back in step
	// with it. The stores' own contracts say the same about their file-based Load.
	KCMPageCheckReplaceAll(db, checks);
	KCMPawStampReplaceAll(db, paws);

	// Which pages' pictures changed. A tick that went and a tick that came are both changes, so it
	// is the symmetric difference; the paws are compared page by page.
	std::set<UID> touched;
	for (std::set<UID>::const_iterator it = wasChecked.begin(); it != wasChecked.end(); ++it)
		if (checks.count(*it) == 0)
			touched.insert(*it);
	for (std::set<UID>::const_iterator it = checks.begin(); it != checks.end(); ++it)
		if (wasChecked.count(*it) == 0)
			touched.insert(*it);

	std::map<UID, std::vector<KCMPawStamp> > wasByPage, nowByPage;
	KCMGroupPawsByPage(wasPawed, wasByPage);
	KCMGroupPawsByPage(paws, nowByPage);
	{
		std::map<UID, std::vector<KCMPawStamp> >::const_iterator it;
		for (it = wasByPage.begin(); it != wasByPage.end(); ++it)
		{
			std::map<UID, std::vector<KCMPawStamp> >::const_iterator n = nowByPage.find(it->first);
			if (n == nowByPage.end() || !KCMSamePaws(it->second, n->second))
				touched.insert(it->first);
		}
		for (it = nowByPage.begin(); it != nowByPage.end(); ++it)
			if (wasByPage.find(it->first) == wasByPage.end())
				touched.insert(it->first);
	}

	if (!touched.empty())
	{
		KCMNotifyPages(kKCMPageFlagsChangedMessage, db, touched);	// the Pages panel thumbnails
		KCMInvalidateDB(db);										// and the layout view, which the notification does not cover
	}

	if (outChecks != nil) *outChecks = (int32)checks.size();
	if (outPaws   != nil) *outPaws   = (int32)paws.size();
	return carrying;
}

//========================================================================================
// Taking every mark off.
//========================================================================================

int32 KCMMarksClearFromDocument(IDataBase* db)
{
	if (db == nil)
		return -1;

	std::vector<UID> pages;
	KCMCollectAllPages(db, pages);

	// Only the pages that actually carry something of ours go into the step. Handing the command
	// every page of the document would make an undo step out of a document that had no marks at
	// all, and KCMMarksWriteOnePage would have to decide page by page not to write -- which is the
	// same decision, made twice ([[one-question-one-place]]).
	std::vector<KCMPageMarks> wanted;
	for (size_t i = 0; i < pages.size(); ++i)
	{
		IScriptLabel::ScriptLabelKeyValueList had;
		if (!KCMReadPageLabels(db, pages[i], had))
			continue;

		for (int32 k = 0; k < (int32)had.size(); ++k)
		{
			if (KCMIsOurKey(had[k].Key()))
			{
				wanted.push_back(KCMPageMarks(pages[i], kFalse));	// no tick, no paws
				break;
			}
		}
	}

	if (wanted.empty())
		return 0;

	if (KCMMarksWrite(db, wanted, "Clear Marks") != kSuccess)
		return -1;

	return (int32)wanted.size();
}

// End, KCMPageMarksDoc.cpp.
