//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Resources comparison mode
//
//  What happened to one definition, as a type and nothing else.
//
//  ★TYPES ONLY, AND THAT IS THE WHOLE POINT OF THE FILE. The UI half includes it through
//  IKCMResourcesFacade.h, and a header the UI includes must declare no model-side free function:
//  the UI would then be able to SEE it and unable to LINK to it, which shows up as a linker error
//  in a file that looks correct. KCMStoryKinds.h exists for the same reason and says so - the
//  Story Edits facade used to reach its enum through KCMStoryStamp.h and dragged three model-side
//  functions across the boundary with it.
//
//========================================================================================
#ifndef __KCMResourceKinds_h__
#define __KCMResourceKinds_h__

/** What happened to one definition between the Source (older) and the Target (newer). */
enum KCMResourceChangeKind
{
	kKCMResourceAdded = 0,	// in the Target only
	kKCMResourceRemoved,	// in the Source only
	kKCMResourceChanged		// in both, with different contents
};

#endif // __KCMResourceKinds_h__

// End, KCMResourceKinds.h.
