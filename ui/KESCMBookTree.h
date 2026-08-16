//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KESCM)
//
//  The one call the rest of the plug-in makes to the chapter list in the book comparison dialog.
//
//  Everything else about that tree - the hierarchy adapter and the row widget manager - is reached
//  only by the tree framework, through the interfaces KCMUI.fr puts on kKESCMBookTreeWidgetBoss.
//  What the caller needs is a way to say "the rows changed, draw them again", and that is this.
//
//  Same shape as KESCMStoryTree.h, with one difference: the panel's list can be found from
//  anywhere (there is only one panel), while a dialog has to be handed in - so the caller passes
//  the dialog's IPanelControlData, which is exactly what the observer already holds.
//
//========================================================================================

#ifndef __KESCMBookTree_h__
#define __KESCMBookTree_h__

class IPanelControlData;

/** Redraw the chapter list from whatever KESCMBookDialogRows() holds right now.

	Safe to call with nil, or with a dialog that has no list in it: it finds the step that is
	missing and returns without doing anything.
*/
void KESCMBookTreeRebuild(IPanelControlData* panelData);

#endif // __KESCMBookTree_h__

// End, KESCMBookTree.h.
