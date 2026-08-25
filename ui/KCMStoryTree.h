//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  The one call the rest of the plug-in makes to the Story Edits list on screen.
//
//  Everything else about the tree - the hierarchy adapter and the row widget manager - is reached
//  only by the tree framework, through the interfaces KCMUI.fr puts on kKCMStoryTreeWidgetBoss.
//  What the model side needs is a way to say "the list changed, draw it again", and that is this.
//
//========================================================================================

#ifndef __KCMStoryTree_h__
#define __KCMStoryTree_h__

/** Redraw the Story Edits list from whatever KCMStoryList holds right now.

	Safe to call at any time: with the panel closed, the section never built, or the list empty, it
	finds the step that is missing and returns without doing anything. Callers therefore do not have
	to know whether the panel is open - which is the point, since the comparison runs the same way
	either way.
*/
void KCMStoryTreeRebuild();

#endif // __KCMStoryTree_h__

// End, KCMStoryTree.h.
