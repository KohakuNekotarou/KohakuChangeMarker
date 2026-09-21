//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  KCMOrigin.cpp -- see the header.
//
//  ⛔This file held the Task Start origin until 2026-09-21: the document's INX in memory, the
//  park and unpark, the peek document, the IDML wrapper and the script writers. All of it went
//  when Task Start began saving a copy to a FILE. What is left never was about the origin - it
//  is the count KCMRehydrate checks an import against.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IPageList.h"
#include "ISpreadList.h"
#include "IStoryList.h"
#include "ITextModel.h"

// General includes:
#include "PersistUtils.h"
#include "K2SmartPtr.h"

// Project includes:
#include "KCMOrigin.h"

void KCMMeasureShape(IDataBase* db, KCMOriginShape& out)
{
	out = KCMOriginShape();
	if (db == nil)
		return;
	InterfacePtr<ISpreadList> spreads(db, db->GetRootUID(), UseDefaultIID());
	if (spreads != nil)
		out.fSpreads = spreads->GetSpreadCount();
	InterfacePtr<IPageList> pages(db, db->GetRootUID(), UseDefaultIID());
	if (pages != nil)
		out.fPages = pages->GetPageCount();
	InterfacePtr<IStoryList> stories(db, db->GetRootUID(), UseDefaultIID());
	if (stories != nil)
	{
		out.fStories = stories->GetUserAccessibleStoryCount();
		for (int32 i = 0; i < out.fStories; ++i)
		{
			InterfacePtr<ITextModel> model(stories->GetNthUserAccessibleStoryUID(i), UseDefaultIID());
			if (model != nil)
				out.fTextLen += model->TotalLength();
		}
	}
}

// End, KCMOrigin.cpp.