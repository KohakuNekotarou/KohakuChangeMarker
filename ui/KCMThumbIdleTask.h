//========================================================================================
//
//  KCMThumbIdleTask.h
//
//  Defers the rebuild of the Pages panel thumbnails to the next idle (KCMThumbIdleTask.cpp).
//
//  WHY THE DELAY IS NEEDED (isolated on a live build). When the Target document is closed and
//    the Source becomes the newly active one, calling KCMTryRefreshPagesPanelThumbnails from
//    inside the close responder (kAfterCloseDoc) is too early: the Pages panel is still switching
//    over to the Source, the ForceRedraw does not get the thumbnails rebuilt, and the frames stay
//    on the Source's thumbnails. Purging and forcing the redraw once the switch has settled -- on
//    the next idle -- clears them every time.
//
//  Usage: hand a live db to KCMScheduleThumbRefresh. Every caller is on the UI side (the model
//    side cannot reach this function at all, it is in the other .pln):
//      - KCMModelChangeObserver.cpp ... takes the close notification from the model and passes
//        whichever documents survived
//      - KCMPeekGesture.cpp ... flushes the UI work that was held back until a batch close
//        finished (kPendingDocumentsClosedMsg); that path re-enumerates the open documents itself
//    The task then fires once, about 150 ms later, and takes itself off the queue (UninstallTask
//    at the end of RunTask -- the .cpp says why returning kEndOfTime instead breaks the contract).
//    Only documents whose db is still open get the refresh. KCMShutdownThumbIdleTask releases it
//    at shutdown.
//
//========================================================================================

#ifndef __KCMThumbIdleTask_h__
#define __KCMThumbIdleTask_h__

class IDataBase;

// Schedule a rebuild of this db's Pages panel thumbnails for the next idle (about 150 ms).
// Running after the frontmost document has settled is what makes it work even when closing one
// document promotes another one to active. Safe to call any number of times: the databases are
// collected into one set and a single idle task is reused (never added to the queue twice).
void KCMScheduleThumbRefresh(IDataBase* db);

// Release the shared idle task instance (call at application shutdown). If one is scheduled it
// is removed from the queue first.
void KCMShutdownThumbIdleTask();

#endif // __KCMThumbIdleTask_h__
