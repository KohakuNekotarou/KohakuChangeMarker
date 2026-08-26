//========================================================================================
//
//  KCMModelNotify.cpp
//
//  The only route from the model to the UI. The reasoning is at the head of KCMModelNotify.h.
//
//  This is the **model side**, and it includes not one UI header -- which is the whole reason the
//  file exists.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "ISession.h"			// GetExecutionContextSession -- can be nil during shutdown
#include "IApplication.h"
#include "ISubject.h"

#include "KCMID.h"
#include "KCMModelNotify.h"

//----------------------------------------------------------------------------------------
// The last status string raised this session.
//
// **HELD ON THE MODEL SIDE**, for two reasons:
//   1. `app.kcmStatus` (KCMScriptProvider.cpp, model side) must answer **while the panel is
//      closed**, which is existing behaviour. Read out of a widget it would be empty then.
//   2. The panel rebuilds its widgets on every re-show, so a value kept in a widget would not
//      survive. (The widget's own contents do persist, in the workspace, which is the opposite
//      problem -- last session's string coming back. AutoAttach overwrites it from here, so the
//      rule holds either way.)
//
// **THE ONLY STATICS THAT BELONG HERE ARE SESSION STATE.** These five are one value in five
// pieces: the sentence standing in the message area. Everything that belongs to a single
// NOTIFICATION -- which documents, whether to drop the Prev/Next cursor, whether to repaint at
// once -- travels on Change()'s changedBy instead (KCMNotifyPayload). What keeps this string here
// is that it is **not part of a notification: it is answered at any time, notification or not.**
//
// The five pieces exist because the panel's message area is drawn by hand: clicking a change row
// produces a message split into heading / leading context / the changed characters / trailing
// context (KCMStatusTextView.cpp), plus the ruby READING drawn above the changed characters.
// **Whatever holds the split must be whatever holds the string** -- held apart, re-opening the
// panel can bring back the sentence with the colouring or the reading missing.
// An ordinary message fills sStatusMid only: **one string is the special case of this shape.**
// @warning every one of the five must appear in KCMClearSessionStatus() at the bottom.
static PMString sStatusLabel;
static PMString sStatusPre;
static PMString sStatusMid;
static PMString sStatusPost;
static PMString sStatusRuby;

// Get at the application's subject. During shutdown the session and the application cannot be
// resolved, so nil comes back and the caller gives up quietly (KCM's rule everywhere: do not touch
// what has closed or gone).
static ISubject* KCMQueryAppSubject()
{
	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return nil;
	InterfacePtr<IApplication> app(session->QueryApplication());
	if (app == nil)
		return nil;
	return (ISubject*)app->QueryInterface(IID_ISUBJECT);
}

// KCMNotify (declared in KCMModelNotify.h)
void KCMNotify(ClassID theChange, const KCMNotifyPayload* payload)
{
	InterfacePtr<ISubject> subject(KCMQueryAppSubject());
	if (subject == nil)
		return;		// shutting down, say. The same as nobody listening: doing nothing is correct.

	// The protocol is our own IID_IKCMMODELCHANGEOBSERVER, which is what marks this notification as
	// addressed to KCM's UI. The three older observers in this plug-in listen for IID_IAPPLICATION
	// notifications; sending under our own protocol keeps ours out of that traffic.
	//
	// The third argument carries the ADDRESS of the payload (ISubject.h:150) and reaches the
	// listener as IObserver::Update's fourth. Change is synchronous, so a struct on the caller's
	// stack stays alive until delivery is finished -- there is no lifetime to manage.
	// @warning payload can be nil (a notification with nothing attached). Listeners must test it.
	subject->Change(theChange, IID_IKCMMODELCHANGEOBSERVER, (void*)payload);
}

//----------------------------------------------------------------------------------------
// What a notification carries.
//
// **NO STATICS.** "Which documents" and "drop the Prev/Next cursor" go into one struct on the
// caller's stack, whose address rides on Change's changedBy. Change is synchronous, so the struct
// outlives the delivery -- which means **there is no clean-up at all** (an implementation using
// statics has to nil them afterwards precisely because they are statics).
// @warning a listener must not keep the payload. Touching a closed document's database later
// crashes (KCM's rule everywhere).
//----------------------------------------------------------------------------------------

// KCMNotifyDocs (declared in KCMModelNotify.h) -- two-document form.
void KCMNotifyDocs(ClassID theChange, IDataBase* docA, IDataBase* docB, bool16 navReset)
{
	KCMNotifyPayload payload;
	payload.fDocA     = docA;
	payload.fDocB     = docB;
	payload.fNavReset = navReset;

	KCMNotify(theChange, &payload);
}

// KCMNotifyDocs (declared in KCMModelNotify.h) -- three-document form.
// @warning it does NOT chain to the two-document form: there would be no way to pass docC. The
// caller builds the whole payload, which is what having a payload instead of statics means.
void KCMNotifyDocs(ClassID theChange, IDataBase* docA, IDataBase* docB, IDataBase* docC, bool16 navReset)
{
	KCMNotifyPayload payload;
	payload.fDocA     = docA;
	payload.fDocB     = docB;
	payload.fDocC     = docC;
	payload.fNavReset = navReset;

	KCMNotify(theChange, &payload);
}

// KCMNotifyPages (declared in KCMModelNotify.h) -- one document plus the pages whose picture
// changed. **The set is not copied; only its address travels** -- Change is synchronous, so the
// caller's own variable stays alive until delivery is over. It is exactly the mechanism that
// carries the two document pointers, and needed nothing new.
void KCMNotifyPages(ClassID theChange, IDataBase* doc, const std::set<UID>& pages)
{
	KCMNotifyPayload payload;
	payload.fDocA   = doc;
	payload.fPagesA = &pages;

	KCMNotify(theChange, &payload);
}

// KCMNotifyDocsPages (declared in KCMModelNotify.h) -- two documents plus each one's set of pages
// whose picture changed. The same mechanism as the one-document form above; the only difference is
// that two sets ride along.
// @warning it is a separate function so that no caller can supply half the sets. A listener reads
// "there is a set" as "these are the only pages to look at", so **a set that is only half right is
// a missed page**.
void KCMNotifyDocsPages(ClassID theChange,
                          IDataBase* docA, const std::set<UID>& pagesA,
                          IDataBase* docB, const std::set<UID>& pagesB,
                          bool16 navReset)
{
	KCMNotifyPayload payload;
	payload.fDocA     = docA;
	payload.fDocB     = docB;
	payload.fPagesA   = &pagesA;
	payload.fPagesB   = &pagesB;
	payload.fNavReset = navReset;

	KCMNotify(theChange, &payload);
}

// KCMNotifyStatus (declared in KCMModelNotify.h)
// The text stays in the statics (session state, which app.kcmStatus answers from at any time);
// only "repaint right now" rides on the payload, being particular to this one notification.
void KCMNotifyStatus(const PMString& s, bool16 forceRedrawNow)
{
	// Storing goes through the function whose job it is, rather than being written out a second
	// time: the five pieces are ONE value, and a sixth piece added to only one of two identical
	// assignments is a difference nothing would report.
	KCMStoreSessionStatus(s);

	KCMNotifyPayload payload;
	payload.fStatusForceRedraw = forceRedrawNow;

	KCMNotify(kKCMStatusTextMessage, &payload);
}

// KCMStoreSessionStatus (declared in KCMModelNotify.h) -- remember, do not notify.
// Two callers: the UI's KCMSetStatus, and KCMNotifyStatus above, which stores through here and
// then notifies. A message raised by a UI action is painted by the UI itself and needs no
// notification, but **there must still be exactly one place that remembers it** (app.kcmStatus
// answers from it, and the panel restores from it on re-show).
// @warning notifying from here would loop: observer -> KCMSetStatus -> here. That is also why
// KCMNotifyStatus calls THIS one and not the other way round.
void KCMStoreSessionStatus(const PMString& s)
{
	// A message that arrives as one string has no split, so the other four pieces are CLEARED
	// rather than left: a heading or a context from the previous message would otherwise stand
	// around the new sentence. An unsplit message is the special case of the five-piece shape.
	sStatusLabel.Clear();
	sStatusPre.Clear();
	sStatusMid = s;
	sStatusPost.Clear();
	sStatusRuby.Clear();
}

// KCMStoreSessionStatusSegments (declared in KCMModelNotify.h) -- remember the split, do not notify.
// The one caller is the UI's KCMSetStatusSegments, on the route where a change row's jump shows
// the other side of the edit.
void KCMStoreSessionStatusSegments(const PMString& label, const PMString& pre,
									 const PMString& mid, const PMString& post,
									 const PMString& ruby)
{
	sStatusLabel = label;
	sStatusPre   = pre;
	sStatusMid   = mid;
	sStatusPost  = post;
	sStatusRuby  = ruby;
}

// KCMGetSessionStatus (declared in KCMModelNotify.h)
void KCMGetSessionStatus(PMString& out)
{
	// Join the four pieces **as they appear on screen**: a line break after the heading, then the
	// leading context, the changed characters and the trailing context. For an unsplit message
	// three of them are empty, so the answer is that string itself -- app.kcmStatus reads exactly
	// as it did before the split existed. The ruby is left out on purpose (see the header).
	out.Clear();
	if (!sStatusLabel.IsEmpty())
	{
		out.Append(sStatusLabel);
		out.Append("\n");
	}
	out.Append(sStatusPre);
	out.Append(sStatusMid);
	out.Append(sStatusPost);
	out.SetTranslatable(kFalse);	// an assembled status line, not a translation key
}

// KCMGetSessionStatusSegments (declared in KCMModelNotify.h)
void KCMGetSessionStatusSegments(PMString& outLabel, PMString& outPre,
								   PMString& outMid, PMString& outPost, PMString& outRuby)
{
	outLabel = sStatusLabel;	outLabel.SetTranslatable(kFalse);
	outPre   = sStatusPre;		outPre.SetTranslatable(kFalse);
	outMid   = sStatusMid;		outMid.SetTranslatable(kFalse);
	outPost  = sStatusPost;		outPost.SetTranslatable(kFalse);
	outRuby  = sStatusRuby;		outRuby.SetTranslatable(kFalse);
}

// KCMClearSessionStatus (declared in KCMModelNotify.h)
void KCMClearSessionStatus()
{
	// @warning **all five**. Leave one out and its heap stays allocated at shutdown -- a defect
	// already found twice in this plug-in, both times a static added beside its fellows and not
	// added here.
	sStatusLabel.Clear();
	sStatusPre.Clear();
	sStatusMid.Clear();
	sStatusPost.Clear();
	sStatusRuby.Clear();
}

// End of KCMModelNotify.cpp.
