//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Task Start
//
//  The peek document: the origin rehydrated and cut down to ONE spread, kept while the reader
//  peeks at that spread of the Target. (The body arrives with the peek work; until then the two
//  functions the origin needs are here so that releasing the origin can drop the document.)
//
//========================================================================================
#ifndef __KCMOriginPeek_h__
#define __KCMOriginPeek_h__

#include "BaseType.h"
#include "PMString.h"

class IDataBase;

/** Close and forget the peek document, if any. Idempotent. */
void KCMOriginPeekDrop();

/** "-" when none is held, else the TARGET spread uid it was built for, as a number. */
void KCMOriginPeekDescribe(PMString& out);

#endif // __KCMOriginPeek_h__

// End, KCMOriginPeek.h.
