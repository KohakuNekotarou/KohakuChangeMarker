//========================================================================================
//
//  KCMDrawEventHandler.h
//
//  The drawing engine for the difference overlay. It holds page UID -> overlay
//  (KCMOverlayEntry) and, when a spread is drawn, paints the rings (change frames), the
//  registered/overflow slashes, the original-page-number badges and the peek at the older
//  version.
//  The shared state is exposed as public static members for the other modules (peek, core) to
//  read.
//  (The number of changes is never drawn. changedCells is read only by Prev/Next, for the
//  percentage it reports -- KCMChangeNav.)
//
//========================================================================================
#ifndef __KCMDrawEventHandler_h__
#define __KCMDrawEventHandler_h__

#include <map>
#include <set>
#include <vector>
#include "KCMConstants.h"
#include "KCMOversetScan.h"	// KCMOversetLoc (the element type of sOversetLocs)
#include "CPMUnknown.h"
#include "IDrwEvtHandler.h"
#include "GraphicsExternal.h"   // AGMImageRecord (a struct member below)
#include "UIDRef.h"             // UID / UIDRef
#include "PMReal.h"
#include "PMRect.h"             // PMRect - the folio rectangles the sieve below converts
#include "IDThreading.h"        // IDThreading::ThreadLocal (tl_Rasterizing below)
#include "KCMThreadSafety.h"  // KCMMarkStateMutex / KCMMarkStateLock, taken wherever sEntries is deleted

class IDataBase;
class IDrwEvtDispatcher;
class IControlView;
class IPanorama;

struct KCMOverlayEntry
{
	uint8*         buf;			// our own ARGB buffer (the ring image). Owned.
	AGMImageRecord rec;			// our own image record pointing at buf (for the blit)
	uint8*         dist;		// chessboard distance transform of the difference mask (w*h, uint8,
								//   0 = a changed pixel, clamped at 255). Owned.
								//   The ring is 0 < dist <= radius, which lets BuildRing paint it in
								//   one pass with no dilation (the mask is discarded once dist exists).
	int32          w, h;
	int32          rowBytes;	// bytes per row of buf (= rec.byteWidth)
	int32          bpp;			// bytes per pixel
	int32          lastRadius;	// the ring radius last drawn, in px. -1 = not drawn yet
	int32          changedCells;	// low-resolution cells that changed (MakeEntry's diffCount), used by
									//   Prev/Next to report how much of the page it jumped to changed.
									//   The denominator is NOT stored: w * h IS the denominator, and
									//   holding the same number twice is how the two drift apart.

	KCMOverlayEntry() : buf(nil), dist(nil), w(0), h(0), rowBytes(0), bpp(0), lastRadius(-1),
		changedCells(0)
	{
		rec.baseAddr = nil; rec.decodeArray = nil;
		rec.colorTab.numColors = 0; rec.colorTab.theColors = nil;
	}
	~KCMOverlayEntry()
	{
		if (buf)   delete[] buf;
		if (dist)  delete[] dist;
	}
};


//========================================================================================
// KCMOrigImage
//   One page's opaque picture of the OLDER version. Built on the spot when the peek asks for
//   it and kept; while the toggle is on it is blitted opaquely over the whole of the matching
//   page's rectangle.
//   The SnapshotUtilsEx accessor is NOT kept: the pixels are copied into buf and our own record
//   is built around it. Holding the accessor across draws crashes when it is destroyed.
//========================================================================================
struct KCMOrigImage
{
	uint8*         buf;			// our own image buffer (opaque). Owned.
	AGMImageRecord rec;			// our own image record pointing at buf (for the blit)
	int32          w, h;
	int32          rowBytes;
	int32          bpp;

	KCMOrigImage() : buf(nil), w(0), h(0), rowBytes(0), bpp(0)
	{
		rec.baseAddr = nil; rec.decodeArray = nil;
		rec.colorTab.numColors = 0; rec.colorTab.theColors = nil;
	}
	~KCMOrigImage()
	{
		if (buf) delete[] buf;
	}
};


//========================================================================================
// KCMDrawEventHandler
//   Holds the set of page UID -> overlay and, when a spread is drawn, blits the ring of each of
//   that spread's pages. Ring thickness follows the zoom. Nothing here is persistent: none of it
//   is written into the .indd.
//
// **THIS CODE ALSO RUNS ON A BACKGROUND THREAD** (the asynchronous PDF export). Both consequences
// come straight out of guide vol1-07 Multithreading:
//
//   (1) **A background thread sees a CLONED database**
//       ("provides a separate execution context (a cloned copy of the database) for each thread").
//       So the db handed to the drawing is ALWAYS a different pointer from sDB.
//       Handled by asking KCMIsSameDoc() (KCMThreadSafety.h), which asks the FILE rather than the
//       pointer. That also disposed of [[uidref-reuse-after-close]] -- a closed document's pointer
//       matching a different document because the address was reused.
//       @warning **sOverflowCacheDB / sOverflowCacheSrcDB are still compared as raw pointers, and
//       that is correct** -- what they compare is one static against another (sOverflowCacheDB
//       against sDB), which gives the same answer on every thread (the reasoning is at
//       EnsureOverflowCache in the .cpp). This is NOT a case of "move everything to GetSysFile".
//
//   (2) **Threads do not share object-model instances. They DO share globals and statics.**
//       So the mutable statics below are touched by the main thread and a background thread at
//       once. sEntries above all: it is a map of raw pointers and DropAll() deletes them, so a
//       Stop on the main thread while a background export is reading one is a read of freed
//       memory ("InDesign will behave inconsistently and **may randomly crash**", the guide's own
//       words).
//       Handled by taking **KCMMarkStateLock** everywhere the mark state is touched: DropAll,
//       DropOneEntry, MakeEntry's replacement, both drawing loops in DrawSpreadMarks, and
//       RebuildOverflowCache's swap. The "am I rasterising" flag is THREAD-LOCAL (tl_Rasterizing).
//       @warning the condition to protect is **"the main thread writes it and the background
//       thread reads it while drawing"**. Count that condition for every new piece of shared
//       state: the writers of sSrcPageToTarget and of the overflow cache were both missed once,
//       leaving only the discarding side protected.
//
//   The measurements are in KCMThreadSafety.h; the full record is in
//   docs/ai-notes/kescm-task12-pdf-export-marks-2026-08-15.md and
//   kescm-bg-clone-db-probe-2026-08-15.md.
//========================================================================================
/** **THIS CLASS IS NO LONGER AN IDrwEvtHandler IMPLEMENTATION.**

	Drawing the marks was consolidated into the **global page item adornment**
	(KCMRingAdornment.cpp), and the draw-event entry points (`HandleDrawEvent` / `Register` /
	`UnRegister`) together with `kKCMDrawEventServiceBoss` and `KCMDrawEventSrvc` were removed.

	They had been kept only as a fallback for the adornment failing to register, and carried no
	function of their own -- the deleted `HandleDrawEvent` was two lines: return if the adornment
	is alive, otherwise call `DrawSpreadMarks`. **Not one drawn thing was lost**: the ticks, the
	slashes, the crosses, the folio wash and Find Overset's "+" are all inside `DrawSpreadMarks`.

	What is left here is the drawing itself (`DrawSpreadMarks`) and the statics that hold the mark
	state. **No instance of this class is ever created** -- all the mutable state was static
	already, so that works unchanged.
	@warning the class name and the file name are historical. It does not inherit IDrwEvtHandler
	and it goes on no boss.
	`DrawEventData` is still used, as a convenient carrier for "the spread being drawn +
	GraphicsData + flags" (the struct is defined by `IDrwEvtHandler.h`). */
class KCMDrawEventHandler
{
public:
	// The drawing itself. **The only caller is the global page item adornment**
	// (KCMRingAdornment.cpp), which builds a DrawEventData for the spread and calls this.
	// @warning the changedBy it is handed is "the spread being drawn" -- this function reads
	// changedBy as a spread and as nothing else.
	// It is static because it uses no instance state (every mutable member of this class is).
	static bool16 DrawSpreadMarks(DrawEventData* ded);

	// Page UID -> overlay. Only pages that changed are in here.
	// @warning raw pointers, deleted by DropAll(): touched from the background thread and the
	// main thread at once, this is a read of freed memory. **Every place that touches it takes
	// KCMMarkStateLock** (see (2) at the head of this file), and so must any new caller.
	static std::map<UID, KCMOverlayEntry*> sEntries;
	// The single document all the entries belong to. Marking a different db rebuilds them, UIDs
	// being unique only within one database.
	// @warning a background thread is handed a **clone with a different pointer**, so this must
	// never be compared as a raw pointer there: the drawing side asks
	// `KCMIsSameDoc(db, sDB)` (see (1) at the head of this file).
	static IDataBase* sDB;
	// The master switch for showing the marks (the rings and everything drawn with them). The data
	// (sEntries) is not touched; only the display is. Default kFalse (hidden): it goes kTrue while
	// the tool's left button is held and back to kFalse on release, and nothing is drawn while it
	// is kFalse. The peek at the older version (sShowOriginal) is not affected by it -- that is
	// driven separately, by a double click.
	static bool16 sMarksVisible;
	// The EFFECTIVE opacity applied to the on-screen marks (the rings). Default 1.0.
	//   - while the tool's left button is held = SelectedMarkOpacity() (the panel's 25%/75%)
	//   - while shown permanently             = KCMBaseScreenOpacity() (the selected opacity when
	//                                            printing is on, 1.0 when it is off)
	static PMReal sMarkScreenOpacity;
	// Whether the change marks (rings) also go into print and PDF (KCMDoSetPrintMarks).
	// Default kFalse (screen only). While it is on they are also shown on screen at all times,
	// regardless of the tool's left button (WYSIWYG). Held independently of the mark data.
	static bool16 sPrintMarks;
	// The frame opacity chosen by the panel radio "Marks opacity 25% / 75%". kTrue = 25%,
	// kFalse = 75%. Default kTrue (25%).
	// The tool's left-hold display, the always-on display while printing is on
	// (KCMBaseScreenOpacity) and the print/PDF output (KCMDrawRingForPrint) all read this choice
	// through SelectedMarkOpacity(), so screen and print agree.
	static bool16 sMarkOpacity25;
	// The mark colour chosen by the flyout ("Mark colour: Red / Cyan"). kFalse = red (the
	// default), kTrue = cyan.
	// This replaced an automatic choice that read the comparison raster per pixel and turned the
	// mark cyan over reddish ground (kKCMRedBgDom). It went for two reasons: **the Story mode's
	// wash cannot read the ground at all**, so the two modes would have decided colour
	// differently; and an automatic change cannot be explained to the reader looking at it.
	// The reader picks instead -- if a red mark is lost against red ground, choose cyan.
	static bool16 sMarkColorCyan;
	// "Always Show Marks on Source": the Source (older) document carries the same rings at all
	// times, regardless of the tool's left button. Default kFalse.
	// **Start does not touch it.** The setting is saved in the panel state and restored at
	// start-up, so a Start that overwrote it would wipe the reader's saved choice on every
	// comparison. (KCMStartComparisonFor used to set it kTrue.)
	// The opacity follows the panel's 25%/75% choice (SelectedMarkOpacity), it is not hidden by
	// overprint preview, and it always goes into print and PDF -- independently of the Target
	// side's sPrintMarks.
	static bool16 sSrcMarksOn;
	// "Always Show Marks on Target": the Target document's marks are shown **on screen** at all
	// times, regardless of the tool's left button. The pair of the Source one above.
	// In the Story compare mode the same toggle governs the inverted characters
	// (KCMStoryMarkBuild.cpp) -- one toggle, both modes.
	// @warning **screen only**, unlike the Source one: what comes out of the Target document is
	// decided by "Print comparison marks" (sPrintMarks). If this affected the output too, that
	// toggle would stop meaning anything.
	static bool16 sTgtMarksOn;
	// The Source document's db. Set when a comparison runs (KCMDoMarkChangesDoc / MakeEntry) and
	// put back to nil by DropAll.
	static IDataBase* sSrcDB;
	// Source page UID -> Target page UID. The comparison pairs flattened page numbers, so when a
	// Source spread is drawn, going through this table and then sEntries puts the same ring image
	// over the Source page. MakeEntry records it as it registers the entry.
	static std::map<UID, UID> sSrcPageToTarget;
	// The Target -> Source pairing the LAST comparison used (the zip of the exclusion table).
	// Kept so that a re-comparison caused by the register toggle (adding or clearing a page with
	// no partner) can be differential: the old and new pairings are matched, and a page whose pair
	// is unchanged reuses its previous result instead of calling MakeEntry (KCMDoMarkChangesDoc's
	// allowIncremental path). Rebuilt whole on every full comparison and discarded by DropAll.
	// Unlike sEntries it holds **every pair the last comparison looked at**, pages with no change
	// included -- the presence of an entry alone cannot tell you a page's pair is unchanged.
	static std::map<UID, UID> sPrevPairTargetToSource;
	// The overflow sets: pages that are not registered but have no partner because the two
	// documents hold different numbers of pages (drawn as "/"). Target side = sOverflowT, Source
	// side = sOverflowS.
	// They are built once, when the comparison runs, rather than by walking both documents'
	// pages (KCMBuildPairing) on every draw. Which (sDB, sSrcDB) they were built for is kept in
	// sOverflowCacheDB / sOverflowCacheSrcDB, and EnsureOverflowCache rebuilds them when the
	// drawing finds a mismatch (the documents were switched, a re-comparison moved to another
	// document). Discarded by DropAll.
	// They do NOT follow raw page insertions and deletions made without a Start: they stay as they
	// are until the next Start or re-comparison -- the same behaviour the rings have.
	static std::set<UID> sOverflowT;
	static std::set<UID> sOverflowS;
	static IDataBase* sOverflowCacheDB;
	static IDataBase* sOverflowCacheSrcDB;
	// The original-page-number badge (the flyout's "Show Original Page Numbers"). Default kFalse.
	// While it is on, and under the same visibility rule as the rings (always-on while sPrintMarks
	// is on, or during the tool's left hold with sMarksVisible), the number a page had BEFORE the
	// hiding is drawn at the bottom centre of every page whose number has shifted (i.e. that has a
	// hidden spread before it). With sPrintMarks on it goes into print and PDF too.
	// Independent of the mark data (sEntries): it is drawn on any document's spread whose numbers
	// have shifted (with nothing hidden the old number equals the current one and nothing is drawn).
	static bool16 sShowOldNumbers;

	// The "Hold to Hide Marks" toggle (sAlwaysShowMarks) was removed: its first half -- show the
	// frames permanently -- was exactly "Always Show Marks on Target", and its own half -- hide
	// them while the button is held -- became the standard behaviour of a toggle that is ON, in
	// sMarksTempHidden below. The rule is now one sentence: **while the button is held, everything
	// is the other way round** -- off shows while held, on hides while held.
	// @warning ActionID +19 stays vacant and must not be reused.

	// While "Always Show Marks on Target" is on, kTrue only while the tool's left button is held
	// down over the Target window (the permanent frames step aside); kFalse on release.
	// Raised and lowered by the tracker in KCMPeekGesture.cpp (KCMTrackerRevealBegin/End). While
	// the toggle is off it stays kFalse and has no effect (that case is the reveal, which shows
	// the marks only while held).
	// It is raised only for a press over the Target window, so only the pressed window's frames
	// are hidden -- the state is per window.
	static bool16 sMarksTempHidden;
	// kTrue only **while** the tool's left button is held over the Source layout window.
	// @warning **this is not the Source counterpart of sMarksTempHidden.** That one remembers
	//   "hidden"; this one remembers only "pressed", and what to show is decided by the drawing
	//   side, which **XORs** it with sSrcMarksOn. So **pressing in a Source window whose toggle is
	//   off shows the frames, and pressing in one whose toggle is on hides them** -- the rule
	//   "while the button is held, everything is the other way round" costs one expression here.
	//   (A flag that is only raised while the toggle is ON means **pressing with the toggle off
	//   does nothing at all**, which contradicts the rule as three other places in this plug-in
	//   state it.)
	// The Target side keeps two flags (sMarksVisible to show, sMarksTempHidden to hide) because
	//   its sMarksVisible is also raised by other routes, the peek among them. **Only the Source
	//   side can be one flag.**
	// Printing always shows the Source frames, so this does not affect it (the drawing side gates
	// on !printing).
	static bool16 sSrcMarksPressed;

	// While kTrue, the frames are drawn into the Pages panel's thumbnails as well (which are
	// generated with no view, in kPreviewMode: normally nothing is drawn there when sPrintMarks
	// and sMarksVisible are off, but a thumbnail forces it on through isThumb, with a thicker
	// fixed-ratio radius at full opacity). It also makes a comparison try to regenerate the
	// thumbnails already on screen, through KCMTryRefreshPagesPanelThumbnails.
	// Setting this one flag back to kFalse restores the earlier behaviour completely (nothing
	// drawn into thumbnails at all), which is the escape route if the refresh misbehaves.
	// Related: memory kescm-pages-panel-thumbnails.
	static bool16 sThumbExperiment;

	// The chosen frame opacity (0.25 / 0.75). The single supplier for every route that draws a
	// frame.
	static PMReal SelectedMarkOpacity() { return sMarkOpacity25 ? kKCMMarkOpacity25 : kKCMMarkOpacity75; }

	// The chosen mark colour. Shaped like SelectedMarkOpacity, and for the same reason: **screen,
	// print, the Pixel mode's frames and the Story mode's wash all come through here** (one
	// question, one answer). It is held as RGB; converting to CMYK for print is KCMSetOutputColor's
	// job (red -> C0 M100 Y100 K0, cyan -> C100 M0 Y0 K0).
	static void SelectedMarkColor(uint8& r, uint8& g, uint8& b)
	{
		r = sMarkColorCyan ? kKCMRingAltR : kKCMRingR;
		g = sMarkColorCyan ? kKCMRingAltG : kKCMRingG;
		b = sMarkColorCyan ? kKCMRingAltB : kKCMRingB;
	}
	// kTrue only while WE are rasterising (SnapshotUtilsEx::Draw inside MakeEntry / MakeOrigImage),
	// so that a re-entrant draw paints no marks into our own raster. Relying on the kPreviewMode
	// bit instead would catch PDF export, which sets the same bit.
	// **THREAD-LOCAL, and it has to be**: the question is "is THIS THREAD in the middle of
	// rasterising", and it stops meaning anything across threads. A plain static means that while
	// the main thread rasterises for a comparison, **a background PDF export reads it, concludes
	// it is re-entrant, and silently draws no marks** -- so the marks appear in some exports and
	// not others. That failure only shows when the two happen to run at once, so a single export
	// never reproduces it.
	// Precedent: open/components/incopyfileactions/InCopyDocFileHandler.cpp:261 uses
	// `IDThreading::ThreadLocalManagedObject< K2Vector<IDataBase*> >` for the same purpose
	// (re-entrancy), and `ThreadLocal<bool16>` itself appears in
	// open/includes/architecture/bossrecycler.h:159. The `tl_` prefix follows those.
	static IDThreading::ThreadLocal<bool16> tl_Rasterizing;

	// The peek at the older version. Completely independent of the marks (sEntries).
	// The pages peeked at have their older picture kept in sOrigImages, and while sShowOriginal is
	// on they are blitted opaquely over that db's pages.
	static std::map<UID, KCMOrigImage*> sOrigImages;	// page UID (new) -> the older picture
	static IDataBase* sOrigDB;							// the single document those pictures belong to (switching db rebuilds them)
	static bool16 sShowOriginal;						// whether the peek is shown (default off)
	static PMReal sOrigScale;							// the content->window scale (zoom x device scale) the pictures were
														// rasterised at, so a later peek can tell the zoom has changed and
														// rebuild them. 0 = not set
	static PMReal sPeekOpacity;							// the peek's opacity: Shift+left = 1.0 (opaque),
														// Shift+Alt+left = 0.5. Read by the drawing block

	// The overset cross (the flyout's "Find Overset"). Completely independent of the comparison.
	// One active document is scanned and the pages holding overset are kept in sOversetPages;
	// while sOversetOn, drawing a spread of that document (sOversetDB) paints a page-sized red "+"
	// on those pages, on screen only (colour, weight and opacity follow the change frames).
	// sOversetDB only identifies which document was scanned: the drawing compares pointers and
	// never dereferences it, which is what makes it safe after a close. Switching the toggle off,
	// or closing the document, empties sOversetPages.
	static bool16 sOversetOn;			// the Find Overset toggle (default off)
	static IDataBase* sOversetDB;		// the document scanned (identity only, never dereferenced)
	static std::set<UID> sOversetPages;	// page UIDs holding overset (for the Pages panel's frame and "+", and the scrollbar map's band)
	static std::vector<KCMOversetLoc> sOversetLocs;	// where each overset "+" goes (page + pasteboard point). Prev/Next's stops

	// Clear Find Overset (the toggle going off, the scanned document closing, switching document).
	// Empties the sets and puts the toggle off.
	static void DropOverset()
	{
		sOversetOn = kFalse;
		sOversetDB = nil;
		sOversetPages.clear();
		sOversetLocs.clear();
	}

	// (The transient toast mechanism was removed; messages go to the panel's status line
	//  (KCMSetStatus). The mechanism itself may be worth reusing in another plug-in:
	//  docs/ai-notes/kescm-toast-mechanism.md and git 509e830.)

	// Paint the ring (0 < dist <= radius) into buf (ARGB) in one pass, using the distance
	// transform -- no dilation needed. Every ring pixel takes the panel's chosen colour
	// (SelectedMarkColor). Everything else is left transparent (alpha = 0).
	// dist is produced beforehand by KCMDistTransform (0 = a changed pixel).
	static void BuildRing(uint8* buf, int32 rb, int32 bpp, int32 wt, int32 ht,
		const uint8* dist, int32 radius);

	// Rasterise target and source as CMYK at high resolution (kKCMResolution x kKCMHiResMul) and
	// compare the four channels (threshold kKCMCmykThr). An entry is registered in
	// sEntries[target.UID] only when the count of changed pixels is above zero (an existing entry
	// is replaced). `changed` reports whether anything differed.
	static ErrorCode MakeEntry(const UIDRef& targetRef, const UIDRef& sourceRef, bool16& changed);

	// Rasterise sourceRef (the older side) once at `resolution` dpi and keep the opaque picture in
	// sOrigImages[target.UID] (replacing any existing one). The offscreen is destroyed immediately,
	// so only one is ever alive at a time.
	// The caller decides the dpi and there is deliberately no default: the only caller is the peek
	// route (KCMPeek.cpp), which derives it from the current zoom - 72.0 * effScale, clamped to
	// 16..300 - so the picture is always crisp. A default here would only be reached by a future
	// caller that forgot to think about resolution, and it would render blurry without saying so.
	// The cost grows with the square of the dpi: about 2MB per A4 page at 72dpi against 26-35MB at
	// 300dpi, and one image is kept alive per page peeked at.
	static ErrorCode MakeOrigImage(const UIDRef& targetRef, const UIDRef& sourceRef, const PMReal& resolution);

	// Rebuild the overflow cache (sOverflowT / sOverflowS) from the current sDB / sSrcDB, with one
	// call to KCMBuildPairing. Called from the comparison (KCMDoMarkChangesDoc), so it is up to
	// date after a Start, a register Add and an Ignore toggle.
	static void RebuildOverflowCache();
	// Rebuild only when the (sDB, sSrcDB) the cache was built for differs from the current pair --
	// the guard for a document switch, or a re-comparison having moved to another document. When
	// they match it does nothing, which is what keeps a full walk of both documents out of every
	// draw.
	static void EnsureOverflowCache();

	// Discard one page's overlay (used when a differential re-comparison applies its diff).
	// Removes targetUID's entry and cleans up the Source-side mapping that pointed at it
	// (sSrcPageToTarget[oldSourceUID]). With no entry and no mapping it does nothing, so it is
	// safe to call for an unchanged page.
	static void DropOneEntry(UID targetUID, UID oldSourceUID)
	{
		// A background thread may be reading the same entry while it draws, so the side that
		// deletes takes the lock (the reasoning is in KCMThreadSafety.h).
		KCMMarkStateLock lock(KCMMarkStateMutex());
		std::map<UID, KCMOverlayEntry*>::iterator it = sEntries.find(targetUID);
		if (it != sEntries.end()) { delete it->second; sEntries.erase(it); }
		std::map<UID, UID>::iterator sp = sSrcPageToTarget.find(oldSourceUID);
		if (sp != sSrcPageToTarget.end() && sp->second == targetUID)
			sSrcPageToTarget.erase(sp);
	}

	// Discard every entry (Clear, or switching to another document). The Source-side mapping and
	// its db go with them. The sSrcMarksOn toggle itself is KEPT, being the reader's preference --
	// with no entries nothing is drawn, so it does no harm.
	static void DropAll()
	{
		// **THE MOST DANGEROUS PLACE IN THIS FILE.** sEntries is a map of raw pointers and this is
		// what deletes them. A background PDF export drawing one of those entries while the main
		// thread Stops is a **read of freed memory** (guide vol1-07 L95, "may randomly crash").
		// Both drawing loops in DrawSpreadMarks take the same lock, which is what makes them wait
		// for each other.
		KCMMarkStateLock lock(KCMMarkStateMutex());
		for (std::map<UID, KCMOverlayEntry*>::iterator it = sEntries.begin(); it != sEntries.end(); ++it)
			delete it->second;
		sEntries.clear();
		sDB = nil;
		sSrcPageToTarget.clear();
		sSrcDB = nil;
		sPrevPairTargetToSource.clear();	// the previous pairing goes too; the next comparison rebuilds it
		sOverflowT.clear();  sOverflowS.clear();		// and so does the overflow cache
		sOverflowCacheDB = nil;  sOverflowCacheSrcDB = nil;
	}

	// Discard every picture of the older version (Clear, or switching document). The display
	// toggle goes off with them.
	//
	// @warning **unlike DropAll, this takes no lock, and that is correct.** sOrigImages is a map
	//   of raw pointers deleted here -- the same shape as DropAll -- so the asymmetry needs its
	//   reason stated: **the readers are all on the main thread.** The drawing side's entry test is
	//   `wantOrig = !suppressForPrint && !printing && sShowOriginal && !sOrigImages.empty()`, and
	//   **`!printing` comes first, so a background PDF export short-circuits and never touches the
	//   map**; the place that actually reads it sits further in, under `wantOrig && !isThumb`.
	//   The peek is a screen-only feature, so it cannot race the background thread at all.
	//   **What breaks that assumption**: putting the peek's picture into print/PDF, or into the
	//   thumbnails. Do either and this function and MakeOrigImage both need KCMMarkStateLock, the
	//   way DropAll has it.
	static void DropAllOrig()
	{
		for (std::map<UID, KCMOrigImage*>::iterator it = sOrigImages.begin(); it != sOrigImages.end(); ++it)
			delete it->second;
		sOrigImages.clear();
		sOrigDB = nil;
		sShowOriginal = kFalse;
		sOrigScale = 0.0;
	}
};

// Raises tl_Rasterizing and puts it back, exception-safe: if SnapshotUtilsEx::Draw throws (a
// bad_alloc from inside AGM, say) the flag must not stay raised, which would suppress every mark
// from then on.
// Making the flag thread-local changed not one caller -- the RAII wrapper is what kept that change
// inside this class.
// The callers are found by grepping for `KCMRasterizingGuard` (five today: three in
// KCMDrawEventHandler.cpp, one in KCMColorSampler.cpp, one in KCMBookCompare.cpp). **The count can
// be re-derived; line numbers cannot -- they go quietly wrong.**
//
// **It nests safely** (the previous value is saved and restored). A destructor that wrote kFalse
// unconditionally would mean a guard inside a guard **ends the outer protection when the inner one
// finishes** -- and rasterising after that point paints **our own marks into the comparison
// raster**, which makes the result quietly wrong with no crash and no warning.
// @warning today all five wrap a single Draw in the smallest possible scope and none of them
//   nests, but the book comparison has only recently started using this guard, and the day that
//   route calls MakeEntry it nests immediately.
// Two lines suffice because ThreadLocal::Get() is guaranteed to return the initial value (kFalse)
// when nothing has been set (IDThreading.h:107 = PublicThreadLocalStorageGet(fKey, fInitialVal)),
// so fPrev is not garbage on a background thread's first pass.
class KCMRasterizingGuard
{
public:
	KCMRasterizingGuard()
		: fPrev(KCMDrawEventHandler::tl_Rasterizing.Get())
	{
		KCMDrawEventHandler::tl_Rasterizing.Set(kTrue);
	}
	~KCMRasterizingGuard() { KCMDrawEventHandler::tl_Rasterizing.Set(fPrev); }
private:
	bool16 fPrev;
};

// (KCMQueryPanorama moved to KCMViewLookup.h: it returns an IPanorama, which is a question about a
//  window, so the UI half owns it. Callers include "KCMViewLookup.h".)

// Release the font cache used by the original-page-number badge (called from
// KCMPeekStartup::Shutdown). The implementation is in KCMDrawEventHandler.cpp, beside the cache.
void KCMReleaseOldNumFontCache();

// The folio-exclusion rectangles, in the comparison's own pixels. **Both pages go into ONE list**,
// because the two are the same page size and therefore the same (x, y) space -- a folio area on
// either side is skipped on both.
// This and the bbox below are here for the same reason as the per-row test that follows them:
// the three steps are the ONE sieve, they ran in two .cpp files, and only the third had been
// pulled out -- so two thirds of it was still a copy waiting to be edited on one side only.
// @warning **the caller decides WHETHER to exclude anything.** The document comparison asks the
//   "Ignore page numbers" toggle; the book comparison always excludes, because inserting one
//   chapter shifts every folio after it. This only converts what it is handed.
// @warning it APPENDS. Both callers hand in a vector they have just declared.
inline void KCMCollectFolioExcludeRects(const std::vector<PMRect>& tRects,
										 const std::vector<PMRect>& sRects,
										 const PMReal& hiRes,
										 std::vector<Int32Rect>& outRects)
{
	const PMReal pxScale = hiRes / PMReal(72.0);	// points -> comparison-resolution pixels
	for (int pass = 0; pass < 2; ++pass)		// 0 = target, 1 = source (both into the same space)
	{
		const std::vector<PMRect>& mrs = (pass == 0) ? tRects : sRects;
		for (size_t mi = 0; mi < mrs.size(); ++mi)
		{
			const PMRect& mr = mrs[mi];
			Int32Rect epr;
			epr.left   = ::ToInt32(::Round(mr.Left()   * pxScale));
			epr.top    = ::ToInt32(::Round(mr.Top()    * pxScale));
			epr.right  = ::ToInt32(::Round(mr.Right()  * pxScale));
			epr.bottom = ::ToInt32(::Round(mr.Bottom() * pxScale));
			outRects.push_back(epr);
		}
	}
}

// Stage 1 of the two-stage sieve: the union bbox of those rectangles. After this, a row outside
// its vertical range costs zero tests and an x outside its horizontal range costs two
// comparisons. All four come back 0 for an empty list, which is why both callers test
// excludeRects.empty() before they use them.
inline void KCMFolioExcludeBBox(const std::vector<Int32Rect>& rects,
								 int32& outTop, int32& outBottom, int32& outLeft, int32& outRight)
{
	outTop = outBottom = outLeft = outRight = 0;
	if (rects.empty())
		return;

	outTop  = rects[0].top;   outBottom = rects[0].bottom;
	outLeft = rects[0].left;  outRight  = rects[0].right;
	for (size_t mi = 1; mi < rects.size(); ++mi)
	{
		const Int32Rect& r = rects[mi];
		if (r.top    < outTop)    outTop    = r.top;
		if (r.bottom > outBottom) outBottom = r.bottom;
		if (r.left   < outLeft)   outLeft   = r.left;
		if (r.right  > outRight)  outRight  = r.right;
	}
}

// The folio-exclusion test, per row: is x inside any of the rectangles that reach this row?
// The document comparison (MakeEntry in KCMDrawEventHandler.cpp) and the book comparison
// (ComparePages in KCMBookCompare.cpp) use the same test. It is one function because the same
// four lines used to be copied into both .cpp files, with a comment asking whoever edited one to
// edit the other -- a promise that splits silently the first time it is forgotten.
// It is inline in the header because it is called from the **innermost (per pixel)** loop of the
// comparison: an extern definition would remove the duplication but leave the book comparison
// paying for a real function call.
// The argument is the list already narrowed to the rectangles that reach this row (stage 2 of the
// two-stage sieve); the narrowing is described at MakeEntry in KCMDrawEventHandler.cpp.
inline bool16 KCMXInRowRects(int32 x, const std::vector<const Int32Rect*>& rowRects)
{
	for (size_t i = 0; i < rowRects.size(); ++i)
		if (x >= rowRects[i]->left && x < rowRects[i]->right)
			return kTrue;
	return kFalse;
}

class IGraphicsPort;

// Set the mark colour on a gPort. **Screen is RGB, print and export are CMYK.**
// The full reasoning is in the implementation (KCMDrawEventHandler.cpp): KCM does its **comparison
// in CMYK**, so drawing the marks in RGB was the inconsistency this removed. The conversion is the
// standard formula.
// It is not static, and is declared here, because the Story mode's marker (KCMStoryMarker.cpp)
// reaches paper too and needs the same screen-RGB / print-CMYK decision. Written in two places it
// would drift.
void KCMSetOutputColor(IGraphicsPort* gPort, uint8 r, uint8 g, uint8 b, bool16 useCMYK);

#endif // __KCMDrawEventHandler_h__
