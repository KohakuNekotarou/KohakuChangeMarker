//========================================================================================
//
//  KCMReportPlace.cpp -- see the header.
//
//  THE SHAPE OF THE FILE: four small steps, in the order the picture travels - build the list,
//  export it to memory, import it into the report, put it where the report wants it. Each one
//  says why it is there rather than what it does; the WHAT is three lines of SDK calls.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <cmath>					// fabs - the tolerances in FitIntoBox
#include <vector>

#include "IBoolData.h"
#include "ICommand.h"
#include "IDataBase.h"
#include "IGeometry.h"
#include "IHierarchy.h"
#include "IHierarchyUtils.h"		// ★AddToHierarchy - the official door to kAddToHierarchyCmdBoss
#include "IImportProvider.h"
#include "IImportProviderUtils.h"
#include "IMasterSpreadUtils.h"		// AppendMasterPageItems - the furniture GetItemsOnPage leaves out
#include "IPDFExportPrefs.h"
#include "IPDFSecurityPrefs.h"
#include "IPMStream.h"
#include "IPMUnknownData.h"
#include "ISpread.h"
#include "ITransformFacade.h"
#include "IUIFlagData.h"
#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "PDFID.h"					// kPDFExportItemsCmdBoss / IID_IPDFCLIPBOARDEXPORTPREFS / IID_IUSEPROGRESSINDICATOR
#include "PMFlavorTypes.h"			// kPDFExternalFlavor / kPageItemFlavor
#include "PreferenceUtils.h"		// ::QuerySessionPreferences
#include "StreamUtil.h"
#include "TransformTypes.h"
#include "TransformUtils.h"			// ::InnerToSpreadMatrix / ::InnerToPasteboardMatrix / ::TransformParentPointToPasteboard
#include "UIDList.h"
#include "Utils.h"

#include "KCMReportPlace.h"
#include "KCMDrawEventHandler.h"	// sPrintMarks / sMarksOnPage - what puts the marks into the picture
#include "KCMRingAdornment.h"		// KCMBeginExportOn / KCMEndExportOnThisThread - the flattener has to run
#include "KCMMemXferBytes.h"		// where the PDF lands instead of a file

namespace
{

PMString Ascii(const char* ascii)
{
	PMString s(ascii);
	s.SetTranslatable(kFalse);
	return s;
}

/** Raises the two flags that put the comparison marks into an export, and puts them back on
	the way out - whichever way this function leaves.

	⚠**BOTH are needed, and neither is enough on its own** (measured 2026-09-14):
	  sPrintMarks    - "the marks go into print and PDF at all". Without it nothing is drawn.
	  sMarksOnPage   - "draw them when handed a PAGE". Without it the adornment turns back,
	                   because the marks are drawn once per SPREAD and this export never draws
	                   one. The PDF then comes out byte-identical with the marks on and off. */
class ScopedMarkFlags
{
public:
	ScopedMarkFlags(bool16 wanted, IDataBase* db)
		: fPrint(KCMDrawEventHandler::sPrintMarks)
		, fOnPage(KCMDrawEventHandler::sMarksOnPage)
		, fOpacity(KCMDrawEventHandler::sMarkScreenOpacity)
		, fAnnounced(kFalse)
	{
		KCMDrawEventHandler::sPrintMarks = wanted;
		KCMDrawEventHandler::sMarksOnPage = wanted;
		// ★★AND THE OPACITY, because sMarksOnPage takes the SCREEN route (KCMDrawEventHandler
		//   says why: an item export never flattens, so the print route's alpha server would come
		//   out solid). The screen route blits at sMarkScreenOpacity - which holds the EFFECTIVE
		//   on-screen value, 1.0 while nothing is being shown - so without this line the marks
		//   reach the PDF fully opaque, whatever the panel says.
		if (wanted)
			KCMDrawEventHandler::sMarkScreenOpacity = KCMDrawEventHandler::SelectedMarkOpacity();

		// ⬜**ANNOUNCING THE EXPORT - kept, and honestly not yet proven necessary.** It exists
		//   because kPDFExportItemsCmdBoss raises no kPDFExportSetupService event, so nothing
		//   joins the transparency list and the flattener never runs. That mattered while the
		//   marks went out through the PRINT route, whose alpha server needs the flattener to
		//   resolve it (KCMDrawRingForPrint's own warning: "a solid block"). Since sMarksOnPage
		//   takes the SCREEN route instead - a blit, which needs no flattener - this may now be
		//   doing nothing at all.
		//   ⇒ **Measure it before removing it**: the two routes were changed in the same sitting
		//     and only the pair has been seen working. Taking it out untested would be trading a
		//     known-good state for a guess. (Re-checked 2026-09-14.)
		if (wanted && db != nil)
		{
			KCMBeginExportOn(db);
			fAnnounced = kTrue;
		}
	}
	~ScopedMarkFlags()
	{
		// ⚠**Undone in the reverse order, and the announcement first**: it reads the flags.
		if (fAnnounced)
			KCMEndExportOnThisThread();
		KCMDrawEventHandler::sPrintMarks = fPrint;
		KCMDrawEventHandler::sMarksOnPage = fOnPage;
		KCMDrawEventHandler::sMarkScreenOpacity = fOpacity;
	}
private:
	const bool16 fPrint;
	const bool16 fOnPage;
	const PMReal fOpacity;
	bool16 fAnnounced;			// ★so that an early return cannot leave the document on the list
	ScopedMarkFlags(const ScopedMarkFlags&);
	ScopedMarkFlags& operator=(const ScopedMarkFlags&);
};

/** Everything that has to be drawn for one page to look like itself:

	  the PAGE's own UID   - ★without it the picture is the items' bounding box instead of the
	                         page (measured: 530x737 against 595x841). This is what the old
	                         route bought with SetCropTo(kCropToMedia).
	  the items on it      - the content. ⚠GetItemsOnPage does NOT include master items
	                         (ISpread.h:124), which is why the next line exists.
	  the master's items   - asked for exactly the way KCMPageNumberMarker.cpp:385-399 asks. */
void BuildPageItemList(IDataBase* db, UID pageUID, UIDList& list)
{
	list.Append(pageUID);

	InterfacePtr<IHierarchy> pageHier(db, pageUID, UseDefaultIID());
	InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
	if (pageHier == nil || pageGeo == nil)
		return;
	const UID spreadUID = pageHier->GetSpreadUID();
	InterfacePtr<ISpread> spread(db, spreadUID, UseDefaultIID());
	if (spread == nil)
		return;

	int32 pgPos = -1;
	const int32 count = spread->GetNumPages();
	for (int32 p = 0; p < count; ++p)
		if (spread->GetNthPageUID(p) == pageUID) { pgPos = p; break; }
	if (pgPos < 0)
		return;

	spread->GetItemsOnPage(pgPos, &list, kFalse /*the page is already in*/, kFalse /*no pasteboard*/, kTrue);

	Utils<IMasterSpreadUtils> masters;
	if (!masters)
		return;
	PMRect boundsInSpread = pageGeo->GetPathBoundingBox();
	::InnerToSpreadMatrix(pageGeo).Transform(&boundsInSpread);
	UIDList onThesePages(db);
	onThesePages.Append(pageUID);
	PMRectCollection boundsList;
	boundsList.push_back(boundsInSpread);
	UIDList masterItems(db);
	UIDList itemPages(db);
	PMMatrixCollection offsets;
	masters->AppendMasterPageItems(db, spreadUID, onThesePages, boundsList, masterItems, itemPages, offsets);
	for (int32 i = 0; i < masterItems.Length(); ++i)
		list.Append(masterItems[i]);
}

/** The list, as a PDF held in memory. Nothing touches disk.

	⚠**IID_IPMUNKNOWNDATA, not the IID_IINTDATA of the guide's example** (vol2-05:351): that
	  interface is not on this boss, and an int32 would halve a 64-bit pointer if it were. The
	  data implementation is a SOFT reference, so the stream has to outlive the command - both
	  live in this function. */
bool16 ExportToMemory(IDataBase* db, const UIDList& items, KCMMemXferBytes& bytes, PMString& why)
{
	if (db == nil || items.Length() == 0)
	{
		why = Ascii("there was nothing to export");
		return kFalse;
	}
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kPDFExportItemsCmdBoss));
	InterfacePtr<IPMUnknownData> streamData(cmd, IID_IPMUNKNOWNDATA);
	if (cmd == nil || streamData == nil)
	{
		why = Ascii("the PDF export command could not be assembled");
		return kFalse;
	}
	cmd->SetItemList(items);

	// The CLIPBOARD PDF preferences: this boss exists to put PDF on the clipboard, and that is
	// the pair the guide's example copies for it.
	InterfacePtr<IPDFExportPrefs> appPrefs((IPDFExportPrefs*)::QuerySessionPreferences(IID_IPDFCLIPBOARDEXPORTPREFS));
	InterfacePtr<IPDFExportPrefs> prefs(cmd, IID_IPDFEXPORTPREFS);
	if (prefs != nil && appPrefs != nil)
		prefs->CopyPrefs(appPrefs);
	InterfacePtr<IPDFSecurityPrefs> security(cmd, IID_IPDFSECURITYPREFS);
	if (security != nil)
		security->SetUseSecurity(kFalse);		// nothing may ask for a password on the way back in
	InterfacePtr<IBoolData> progress(cmd, IID_IUSEPROGRESSINDICATOR);
	if (progress != nil)
		progress->Set(kFalse);					// the report has a bar of its own
	InterfacePtr<IUIFlagData> ui(cmd, IID_IUIFLAGDATA);
	if (ui != nil)
		ui->Set(kSuppressUI);

	InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&bytes, kFalse, kFalse));
	if (stream == nil)
	{
		why = Ascii("the memory stream could not be created");
		return kFalse;
	}
	streamData->SetPMUnknown(stream);

	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	const ErrorCode err = CmdUtils::ProcessCommand(cmd);
	stream->Flush();
	const ErrorCode global = ErrorUtils::PMGetGlobalErrorCode();
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	if (err != kSuccess || global != kSuccess || bytes.GetSize() == 0)
	{
		why = Ascii("the PDF export wrote nothing");
		return kFalse;
	}
	return kTrue;
}

/** The provider that takes PDF bytes back in. Measured: three ways of asking all find the same
	one, and of the 18 registered import providers it is the only one that says kFullImport. */
IImportProvider* QueryPdfImporter()
{
	Utils<IImportProviderUtils> utils;		// asked for before it is used: a nil Utils<> cannot be checked after
	if (!utils)
		return nil;
	IImportProvider* provider = utils->QueryImportProviderFor(kPDFExternalFlavor, kPageItemFlavor);
	if (provider == nil)
		provider = utils->QueryImportProviderForFileType(static_cast<SysOSType>('PDF '));
	return provider;
}

/** What ImportThis makes has NO PARENT - it is a page item in the database and nothing more.
	This is what puts it on the report's layer.

	⚠★★★**THE HAND-BUILT COMMAND THAT STOOD HERE FIRST CRASHED INDESIGN** (2026-09-14,
	  EXCEPTION_ACCESS_VIOLATION inside Command::DoImmediate; the crash report named AddToLayer
	  and KCMPlacePageIntoReport on the stack). It created kAddToHierarchyCmdBoss and set only
	  IHierarchyCmdData::SetParent - but that interface has a SECOND list, SetIndexInParent, and
	  the header says it "must match up with the itemlist passed to the command"
	  (IHierarchyCmdData.h:57). One list set, one not.
	★**The lesson is not "set the other list" - it is that this had an official door all along**:
	  IHierarchyUtils::AddToHierarchy processes exactly that command with everything filled in
	  (IHierarchyUtils.h:41-48). The command was hand-built because the dictionary's "example"
	  column pointed at customdatalink/CusDtLnkDocObserver.cpp:163, which is an OBSERVER watching
	  for that command - commented out at that - and not one line of how to send it.
	  ⇒ **An "example" that only NAMES a command is not an example of using it.** */
bool16 AddToLayer(const UIDRef& item, const UIDRef& layer, PMString& why)
{
	Utils<IHierarchyUtils> hierarchy;		// asked for before it is used, like every other Utils<> here
	if (!hierarchy)
	{
		why = Ascii("the hierarchy utilities are not available");
		return kFalse;
	}
	if (hierarchy->AddToHierarchy(item, layer.GetUID()) != kSuccess)
	{
		why = Ascii("the imported picture could not be put on the report's layer");
		return kFalse;
	}
	return kTrue;
}

/** Move (and, if it ever comes to it, scale) the imported picture into the box the report
	gives. The box is in the layer's parent coordinates - the same rectangle the old route
	handed PlaceFileInFrame - so it is taken to the pasteboard first, which is the space the
	transform works in.

	★In practice the scale is 1: the report's slot is a page wide and the picture is a page
	  wide, which is the whole reason the page UID is in the export list. The scaling is here
	  because "in practice" is not "always" - a report built at another size would otherwise
	  place a picture that overflows its slot in silence. */
bool16 FitIntoBox(const UIDRef& item, const UIDRef& layer, const PMRect& box, PMString& why)
{
	InterfacePtr<IGeometry> geo(item, UseDefaultIID());
	if (geo == nil)
	{
		why = Ascii("the imported picture has no geometry");
		return kFalse;
	}
	PMRect now = geo->GetStrokeBoundingBox();
	::InnerToPasteboardMatrix(geo).Transform(&now);

	InterfacePtr<ITransform> layerTransform(layer, UseDefaultIID());
	if (layerTransform == nil)
	{
		why = Ascii("the report layer has no transform");
		return kFalse;
	}
	PMPoint wantTopLeft = box.LeftTop();
	PMPoint wantBottomRight = box.RightBottom();
	::TransformParentPointToPasteboard(layerTransform, &wantTopLeft);
	::TransformParentPointToPasteboard(layerTransform, &wantBottomRight);

	const UIDList items(item);
	Utils<Facade::ITransformFacade> transform;
	if (!transform)
	{
		why = Ascii("the transform facade is not available");
		return kFalse;
	}

	// Scale first, about the picture's own top left, so the move that follows only has to put
	// that one point where it belongs.
	if (now.Width() > 0 && now.Height() > 0)
	{
		const PMReal wantW = wantBottomRight.X() - wantTopLeft.X();
		const PMReal wantH = wantBottomRight.Y() - wantTopLeft.Y();
		const PMReal sx = wantW / now.Width();
		const PMReal sy = wantH / now.Height();
		// ⚠**A tolerance, not equality.** These are computed reals: the slot and the picture are
		//   both a page wide, so the ratio is 1 - but 1.0000000001 is not 1, and testing for
		//   equality would send a whole command through on every picture for no visible change.
		const double kSlack = 0.0002;
		if (std::fabs(::ToDouble(sx) - 1.0) > kSlack || std::fabs(::ToDouble(sy) - 1.0) > kSlack)
		{
			transform->TransformItems(items, Transform::PasteboardCoordinates(),
									  Transform::PasteboardLocation(now.LeftTop()),
									  Transform::ScaleBy(sx, sy));
			now = geo->GetStrokeBoundingBox();
			::InnerToPasteboardMatrix(geo).Transform(&now);
		}
	}

	const PMReal dx = wantTopLeft.X() - now.Left();
	const PMReal dy = wantTopLeft.Y() - now.Top();
	// Same reasoning as the scale above; a twentieth of a point is below anything printable.
	if (std::fabs(::ToDouble(dx)) > 0.05 || std::fabs(::ToDouble(dy)) > 0.05)
	{
		transform->TransformItems(items, Transform::PasteboardCoordinates(),
								  Transform::CurrentOrigin(),
								  Transform::TranslateBy(dx, dy));
	}
	return kTrue;
}

}	// namespace

bool16 KCMPlacePageIntoReport(IDataBase* sourceDB, UID pageUID, const UIDRef& layer,
							  const PMRect& box, bool16 withMarks, PMString& why)
{
	if (sourceDB == nil || pageUID == kInvalidUID || layer == UIDRef::gNull)
	{
		why = Ascii("nothing to place");
		return kFalse;
	}

	KCMMemXferBytes bytes;
	{
		UIDList list(sourceDB);
		BuildPageItemList(sourceDB, pageUID, list);
		// ⚠**Everything the export needs goes up here and comes down in the destructor** - the two
		//   mark flags, the opacity the screen route blits at, and the export announcement. It is
		//   one object rather than three pairs of lines because an early return between them would
		//   otherwise leave the document announced and the flags raised.
		ScopedMarkFlags marks(withMarks, sourceDB);
		if (!ExportToMemory(sourceDB, list, bytes, why))
			return kFalse;
	}

	InterfacePtr<IImportProvider> importer(QueryPdfImporter());
	if (importer == nil)
	{
		why = Ascii("no import provider will take PDF");
		return kFalse;
	}

	UIDRef imported = UIDRef::gNull;
	{
		InterfacePtr<IPMStream> read(StreamUtil::CreateMemoryStreamRead(&bytes, kFalse, kFalse));
		if (read == nil)
		{
			why = Ascii("the PDF could not be read back");
			return kFalse;
		}
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		importer->ImportThis(layer.GetDataBase(), read, kSuppressUI, &imported);
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		read->Close();
	}
	if (imported == UIDRef::gNull)
	{
		why = Ascii("the PDF did not come back as a page item");
		return kFalse;
	}

	if (!AddToLayer(imported, layer, why))
		return kFalse;
	return FitIntoBox(imported, layer, box, why);
}

// End, KCMReportPlace.cpp.
