//========================================================================================
//
//  KCMExternalSource.cpp
//
//  The lent Source. The reasoning is at the top of the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IDocumentList.h"
#include "PMString.h"

// Project includes:
#include "KCMExternalSource.h"

// **File-scope statics, never dereferenced.** One registration at a time: the comparison is one
// pair, so a second lent Source can only arrive through a second Start, which forgets the first.
static IDataBase*	sExternalSourceDB = nil;
static PMString		sExternalSourceLabel;

void KCMRegisterExternalSource(IDataBase* db, const PMString& label)
{
	if (db == nil)
		return;
	sExternalSourceDB = db;
	sExternalSourceLabel = label;
	sExternalSourceLabel.SetTranslatable(kFalse);
}

void KCMForgetExternalSource()
{
	sExternalSourceDB = nil;
	sExternalSourceLabel.Clear();
}

IDataBase* KCMExternalSourceDB()
{
	return sExternalSourceDB;
}

bool16 KCMIsExternalSource(IDataBase* db)
{
	return (db != nil && db == sExternalSourceDB) ? kTrue : kFalse;
}

bool16 KCMExternalSourceLabel(IDataBase* db, PMString& outLabel)
{
	if (!KCMIsExternalSource(db))
		return kFalse;
	outLabel = sExternalSourceLabel;
	outLabel.SetTranslatable(kFalse);
	return kTrue;
}

bool16 KCMIsDbAlive(IDocumentList* docList, IDataBase* db)
{
	if (db == nil)
		return kFalse;
	if (docList != nil && docList->FindDocByDataBase(db) != nil)
		return kTrue;
	return KCMIsExternalSource(db);
}

// End, KCMExternalSource.cpp.
