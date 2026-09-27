//========================================================================================
//
//  KCMRedlineSpike.cpp -- A SPIKE, not a feature (2026-09-28). Remove before shipping.
//  See KCMRedlineSpike.h for the question.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "ICommand.h"
#include "IDataBase.h"
#include "IIntData.h"
#include "IRedlineChangeData.h"
#include "IRedlineDataStrand.h"
#include "IStoryList.h"
#include "ITextModel.h"

#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "InCopySharedID.h"		// kRedlineStrandBoss, kReplaceDeleteChangeDataCmdBoss
#include "UIDList.h"
#include "redlineiterator.h"
#include "VOSRedline.h"

#include <string>
#include <vector>

#include "KCMCore.h"			// KCMActiveDocDB
#include "KCMRedlineSpike.h"

namespace
{
// The fields only: VOSRedlineChange's copy constructor is declared but not exported to plug-ins
// (LNK2019), so a record is never copied here - it is read, and the one rewritten is the copy the
// iterator hands over (the caller owns it - redlineiterator.h:137-138).
struct SpikeRecord
{
	TextIndex	at;
	int32		len;
	bool		isDelete;
	bool		isInsert;
	PMString	user;
	uint64		time;
};

IRedlineDataStrand* QueryStrand(const UIDRef& story)
{
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	if (model == nil)
		return nil;
	return static_cast<IRedlineDataStrand*>(model->QueryStrand(kRedlineStrandBoss, IRedlineDataStrand::kDefaultIID));
}

void Collect(const UIDRef& story, std::vector<SpikeRecord>& out)
{
	out.clear();
	InterfacePtr<IRedlineDataStrand> strand(QueryStrand(story));
	if (strand == nil || !strand->StoryHasChanges())
		return;
	RedlineIterator* it = strand->NewRedlineIterator(0);
	if (it == nil)
		return;
	for (bool16 more = kTrue; more; more = it->Increment(kFalse))
	{
		TextIndex at = 0;
		int32 len = 0;
		const VOSRedlineChange* record = it->GetCurrentChangeRecord(&at, &len);
		if (record == nil)
			continue;
		SpikeRecord r;
		r.at = at;
		r.len = len;
		r.isDelete = (record->GetChangeType() == VOSRedlineChange::kDelete);
		r.isInsert = (record->GetChangeType() == VOSRedlineChange::kInsert);
		r.user = record->GetUserName();
		r.time = record->GetTimeStamp();
		delete record;
		out.push_back(r);
	}
	delete it;
}

void Describe(const char* heading, const std::vector<SpikeRecord>& records, std::string& out)
{
	out += heading;
	out += " (";
	out += std::to_string(records.size());
	out += ")\n";
	for (size_t i = 0; i < records.size(); ++i)
	{
		const SpikeRecord& c = records[i];
		out += "  [" + std::to_string(i) + "] ";
		out += c.isDelete ? "DEL" : (c.isInsert ? "INS" : "?");
		out += " at=" + std::to_string(c.at) + " len=" + std::to_string(c.len);
		out += " user=" + c.user.GetUTF8String();
		out += " time=" + std::to_string(c.time) + "\n";
	}
}

// The record at `index` in iterator order, as the iterator hands it over (the caller deletes it).
VOSRedlineChange* ReadRecordAt(const UIDRef& story, size_t index)
{
	InterfacePtr<IRedlineDataStrand> strand(QueryStrand(story));
	RedlineIterator* it = (strand != nil) ? strand->NewRedlineIterator(0) : nil;
	if (it == nil)
		return nil;
	VOSRedlineChange* found = nil;
	size_t i = 0;
	for (bool16 more = kTrue; more && found == nil; more = it->Increment(kFalse))
	{
		const VOSRedlineChange* record = it->GetCurrentChangeRecord();
		if (record == nil)
			continue;
		if (i == index)
			found = const_cast<VOSRedlineChange*>(record);
		else
			delete record;
		++i;
	}
	delete it;
	return found;
}
}	// namespace

void KCMProbeRedlineAuthor(PMString& out)
{
	std::string text;
	IDataBase* db = KCMActiveDocDB();
	if (db == nil)
	{
		out.SetUTF8String("no active document");
		return;
	}
	InterfacePtr<IStoryList> stories(db, db->GetRootUID(), UseDefaultIID());
	if (stories == nil)
	{
		out.SetUTF8String("no story list");
		return;
	}
	UIDRef story;
	for (int32 i = 0; i < stories->GetAllTextModelCount() && story == UIDRef::gNull; ++i)
	{
		const UIDRef candidate = stories->GetNthTextModelUID(i);
		InterfacePtr<IRedlineDataStrand> strand(QueryStrand(candidate));
		if (strand != nil && strand->StoryHasChanges())
			story = candidate;
	}
	if (story == UIDRef::gNull)
	{
		out.SetUTF8String("no story with tracked changes");
		return;
	}
	text += "story UID=" + std::to_string(story.GetUID().Get()) + "\n";

	std::vector<SpikeRecord> before;
	Collect(story, before);
	Describe("BEFORE", before, text);

	size_t target = before.size();
	for (size_t i = 0; i < before.size() && target == before.size(); ++i)
		if (before[i].isDelete)
			target = i;
	if (target == before.size())
	{
		text += "no deletion record - nothing tried\n";
		out.SetUTF8String(text);
		return;
	}

	// Shape 1: the story as the item list, the deletion's position as the IIntData, and the record
	// itself with a new author and a time one day earlier as the IRedlineChangeData.
	VOSRedlineChange* changed = ReadRecordAt(story, target);
	if (changed == nil)
	{
		text += "the target record could not be read again - nothing tried\n";
		out.SetUTF8String(text);
		return;
	}
	changed->SetUserName(PMString("KohakuSpikeAuthor"));
	const uint64 oneDay = 864000000000ULL;	// 100 ns units (the stamps are FILETIME)
	if (changed->GetTimeStamp() > oneDay)
		changed->SetTimeStamp(changed->GetTimeStamp() - oneDay);
	text += "TRY shape 1 on [" + std::to_string(target) + "]: item list = story, IIntData = at ("
		+ std::to_string(before[target].at) + "), user -> KohakuSpikeAuthor, time -> one day earlier\n";

	ICommandSequence* seq = CmdUtils::BeginCommandSequence();
	if (seq != nil)
	{
		PMString name("KCM Spike: Redline Author");
		name.SetTranslatable(kFalse);
		seq->SetName(name);
	}
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kReplaceDeleteChangeDataCmdBoss));
	InterfacePtr<IRedlineChangeData> changeData(cmd, IID_IREDLINECHANGEDATA);
	InterfacePtr<IIntData> intData(cmd, IID_IINTDATA);
	if (cmd == nil || changeData == nil || intData == nil)
	{
		text += std::string("  command or its data missing: cmd=") + (cmd != nil ? "yes" : "no")
			+ " changeData=" + (changeData != nil ? "yes" : "no") + " intData=" + (intData != nil ? "yes" : "no") + "\n";
	}
	else
	{
		changeData->Set(*changed);
		intData->Set(before[target].at);
		cmd->SetItemList(UIDList(story));
		const ErrorCode err = CmdUtils::ProcessCommand(cmd);
		const PMString errText = ErrorUtils::PMGetGlobalErrorString();
		text += "  ProcessCommand -> " + std::to_string(err) + " (" + errText.GetUTF8String() + ")\n";
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	}
	if (seq != nil)
		CmdUtils::EndCommandSequence(seq);
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	delete changed;

	std::vector<SpikeRecord> after;
	Collect(story, after);
	Describe("AFTER", after, text);
	out.SetUTF8String(text);
}
