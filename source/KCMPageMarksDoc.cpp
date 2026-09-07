//========================================================================================
//
//  KCMPageMarksDoc.cpp
//
//  Ticks and paws to and from the document's own script labels. The reasoning is in the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICommand.h"
#include "IDataBase.h"
#include "IScript.h"					// a page's scripting facet -- it IS an IScriptLabel
#include "IScriptLabel.h"				// ScriptLabelKey / ScriptLabelValue / the key-value list
#include "IScriptTagCmdData.h"			// what kSetScriptingTagCmdBoss is given
#include "ScriptingID.h"				// kSetScriptingTagCmdBoss

// General includes:
#include "CmdUtils.h"
#include "PMReal.h"
#include "PMString.h"
#include "RequestContext.h"				// EngineContext -- default-constructed: no engine is asking
#include "ScriptData.h"					// ScriptList = adobe::vector<InterfacePtr<IScript> >

#include <set>
#include <string>
#include <vector>

// Project includes:
#include "KCMCore.h"					// KCMCollectPageUIDs / KCMCollectMasterPageUIDs
#include "KCMPageCheck.h"				// the tick's store (Collect / ReplaceAll)
#include "KCMPageMarksDoc.h"
#include "KCMPawStamp.h"				// the paws' store

//========================================================================================
// The keys.
//
//  ★PREFIXED WITH THE PRODUCT so that a reader who opens the Script Label panel can see where
//    the text came from. Kohaku InDesign MCP labels its own the same way.
//========================================================================================
static const char* const kKCMCheckLabelKey   = "KohakuChangeMarker.check";
static const char* const kKCMPawsLabelKey    = "KohakuChangeMarker.paws";
static const char* const kKCMCheckLabelValue = "1";	// the key's presence IS the tick

//========================================================================================
// Numbers inside a label.
//
//  ⚠★★**THOUSANDTHS OF A POINT, AS INTEGERS, AND THAT IS NOT A DETAIL.** Writing
//    12.5 means writing a decimal separator, and the separator FOLLOWS THE LOCALE -- a French
//    InDesign writes "12,5", which a reader of this file would take for two numbers. KCM met this
//    once already (the change ratio on the status line) and answered it the same way: keep the
//    arithmetic in integers and let the unit carry the scale.
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

/** Read the next integer at or after `pos`, leaving `pos` just past it.
	@return kFalse when there is no integer left. */
static bool16 KCMReadInt(const std::string& text, size_t& pos, int32& out)
{
	while (pos < text.size() && (text[pos] < '0' || text[pos] > '9') && text[pos] != '-')
		++pos;
	if (pos >= text.size())
		return kFalse;

	const bool negative = (text[pos] == '-');
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
	★**Through the application's own command** (kSetScriptingTagCmdBoss), so the change joins the
	 undo history and dirties the document the way any edit does. */
static ErrorCode KCMWritePageLabels(IDataBase* db, UID pageUID,
                                    const IScriptLabel::ScriptLabelKeyValueList& labels)
{
	InterfacePtr<IScript> script(db, pageUID, UseDefaultIID());
	if (script == nil)
		return kFailure;

	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kSetScriptingTagCmdBoss));
	InterfacePtr<IScriptTagCmdData> data(cmd, UseDefaultIID());
	if (cmd == nil || data == nil)
		return kFailure;

	ScriptList list;
	list.push_back(script);
	data->SetScriptList(list, EngineContext());
	data->SetTags(labels, kTrue /*replaceExistingLabels*/);
	return CmdUtils::ProcessCommand(cmd);
}

/** Is this one of ours? */
static bool16 KCMIsOurKey(const IScriptLabel::ScriptLabelKey& key)
{
	return (key == PMString(kKCMCheckLabelKey) || key == PMString(kKCMPawsLabelKey)) ? kTrue : kFalse;
}

//========================================================================================
// The paws of one page, as text and back.
//========================================================================================

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
		out += '}';
	}
	out += ']';
}

/** ⚠The three numbers are read IN ORDER rather than by name: this file wrote them, and a
	parser that hunts for keys would have to answer what a missing one means. A short entry is
	dropped rather than half-read. */
static void KCMTextToPaws(const std::string& text, UID pageUID, std::vector<KCMPawStamp>& out)
{
	size_t pos = 0;
	for (;;)
	{
		int32 x = 0, y = 0, c = 0;
		if (!KCMReadInt(text, pos, x)) break;
		if (!KCMReadInt(text, pos, y)) break;
		if (!KCMReadInt(text, pos, c)) break;
		out.push_back(KCMPawStamp(pageUID,
		                          PMReal(x / kKCMLabelScale),
		                          PMReal(y / kKCMLabelScale),
		                          c));
	}
}

//========================================================================================
// The three the header declares.
//========================================================================================

int32 KCMMarksSaveToDocument(IDataBase* db)
{
	if (db == nil)
		return -1;

	std::vector<UID> pages;
	KCMCollectAllPages(db, pages);

	std::set<UID> checked;
	KCMPageCheckCollect(db, checked);

	int32 touched = 0;
	for (size_t i = 0; i < pages.size(); ++i)
	{
		const UID page = pages[i];

		std::vector<KCMPawStamp> paws;
		KCMPawStampsOnPage(db, page, paws);
		const bool16 wantCheck = (checked.count(page) > 0) ? kTrue : kFalse;

		IScriptLabel::ScriptLabelKeyValueList had;
		if (!KCMReadPageLabels(db, page, had))
			continue;

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

		if (wantCheck)
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

		// Nothing to say about this page, and nothing of ours on it: leave it completely alone.
		if (!hadOurs && !wantCheck && paws.empty())
			continue;

		if (KCMWritePageLabels(db, page, want) == kSuccess)
			++touched;
	}
	return touched;
}

int32 KCMMarksRestoreFromDocument(IDataBase* db, int32* outChecks, int32* outPaws)
{
	if (outChecks != nil) *outChecks = 0;
	if (outPaws   != nil) *outPaws   = 0;
	if (db == nil)
		return -1;

	std::vector<UID> pages;
	KCMCollectAllPages(db, pages);

	std::set<UID>            checks;
	std::vector<KCMPawStamp> paws;
	int32                    carrying = 0;

	for (size_t i = 0; i < pages.size(); ++i)
	{
		IScriptLabel::ScriptLabelKeyValueList had;
		if (!KCMReadPageLabels(db, pages[i], had))
			continue;

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

	// ★REPLACE, NOT MERGE: restoring is "put back what was saved". The stores' own contracts say
	//   the same about their file-based Load.
	KCMPageCheckReplaceAll(db, checks);
	KCMPawStampReplaceAll(db, paws);

	if (outChecks != nil) *outChecks = (int32)checks.size();
	if (outPaws   != nil) *outPaws   = (int32)paws.size();
	return carrying;
}

int32 KCMMarksClearFromDocument(IDataBase* db)
{
	if (db == nil)
		return -1;

	std::vector<UID> pages;
	KCMCollectAllPages(db, pages);

	int32 touched = 0;
	for (size_t i = 0; i < pages.size(); ++i)
	{
		IScriptLabel::ScriptLabelKeyValueList had;
		if (!KCMReadPageLabels(db, pages[i], had))
			continue;

		IScriptLabel::ScriptLabelKeyValueList keep;
		bool16 hadOurs = kFalse;
		for (int32 k = 0; k < (int32)had.size(); ++k)
		{
			if (KCMIsOurKey(had[k].Key()))
				hadOurs = kTrue;
			else
				keep.push_back(had[k]);
		}
		if (!hadOurs)
			continue;			// nothing of ours here: the page is not touched at all

		if (KCMWritePageLabels(db, pages[i], keep) == kSuccess)
			++touched;
	}
	return touched;
}

// End, KCMPageMarksDoc.cpp.
