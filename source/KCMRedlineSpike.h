//========================================================================================
//
//  KCMRedlineSpike.h -- A SPIKE, not a feature (2026-09-28). Remove before shipping.
//
//  The question (the user, on KBS's Track Changes): can a tracked change's AUTHOR - and its time - be
//  rewritten without changing InDesign's user name? kReplaceDeleteChangeDataCmdBoss (InCopySharedID.h,
//  kInCopySharedPrefix + 126) takes an IRedlineChangeData (type, user, creation date) and an IIntData;
//  it has no caller anywhere in the SDK and no documentation. This measures it, one shape at a time.
//
//  THE WAY IN is the script method app.kcmProbeRedlineAuthor() (KCM.fr, KCMScriptProvider.cpp). It
//  works on the ACTIVE document's first story that holds tracked changes, rewrites the FIRST deletion
//  record only, inside one undo step ("KCM Spike: Redline Author"), and answers with the records before
//  and after, one per line.
//
//========================================================================================

#ifndef __KCMRedlineSpike_h__
#define __KCMRedlineSpike_h__

class PMString;

/** Run the experiment on the active document and describe what happened, a line per step. */
void KCMProbeRedlineAuthor(PMString& out);

#endif // __KCMRedlineSpike_h__
