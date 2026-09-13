//========================================================================================
//
//  KCMReportTable.cpp -- see the header.
//
//  Recipes: codesnippets/SnpCreateTable.cpp (ITableUtils::InsertTable), SnpAccessTableContent.cpp
//  (a cell's story thread), xdocbookworkflow/XDocBkTableHelper.cpp (ITableCommands on a table
//  model), SnpManipulateTextFrame.cpp (kTextLinkCmdBoss), SnpManipulateSpreadsAndPages.cpp
//  (kNewSpreadCmdBoss), SnpEstimateTextDepth.cpp (ITextUtils::IsOverset).
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <vector>

#include "IApplyMasterCmdData.h"	// the new spread's master (none) and page size rule
#include "ICellContent.h"
#include "ICommand.h"
#include "ICompositionStyle.h"		// kTextAlignLeft
#include "IDataBase.h"
#include "IDrawingStyle.h"			// kPosSuperscript
#include "IFrameList.h"
#include "IFrameListComposer.h"
#include "IGeometry.h"
#include "IGraphicFrameData.h"		// GetTextContentUID - the frame's kMultiColumnItemBoss, what kTextLinkCmdBoss links
#include "IIntData.h"
#include "IMultiColumnTextFrame.h"
#include "INewSpreadCmdData.h"
#include "ISpread.h"
#include "ISpreadList.h"
#include "ITableModel.h"
#include "ITableModelList.h"		// the table the story holds
#include "ITableCommands.h"		// ResizeCols / MergeCells / ApplyCellOverrides
#include "ITableAttrRealNumber.h"	// the cell insets
#include "AttributeBossList.h"
#include "CJKID.h"					// kTARubyPointSizeBoss / kTAKentenSizeBoss
#include "TablesID.h"				// kCellAttr*InsetBoss
#include "ITableUtils.h"
#include "ITextAttrAlign.h"
#include "ITextAttrPositionMode.h"
#include "ITextAttrRealNumber.h"
#include "ITextAttrUtils.h"
#include "ITextModel.h"
#include "ITextModelCmds.h"
#include "ITextStoryThread.h"
#include "ITextUtils.h"			// IsOverset
#include "CmdUtils.h"
#include "CreateObject.h"
#include "PMPageSize.h"			// kPMPageSizeNeighbor
#include "SDKLayoutHelper.h"
#include "SpreadID.h"				// kNewSpreadCmdBoss / IID_IBINDINGLOCATION
#include "TableTypes.h"			// GridAddress
#include "TextAttrID.h"			// kTextAttrPointSizeBoss / kTextAttrAlignmentBoss / kTextAttrTintBoss / kTextAttrPositionModeBoss
#include "TextID.h"				// kCharAttrStrandBoss / kParaAttrStrandBoss / kTextLinkCmdBoss
#include "TransformUtils.h"		// ::InnerToParentMatrix
#include "UIDList.h"
#include "Utils.h"
#include "WideString.h"

#include "KCMReportTable.h"
#include "KCMStoryRestore.h"		// KCMCreateRubyStrandIfNeeded / KCMApplyRuby / KCMApplyKentenKind - the writers the restore uses

namespace
{

const int32  kMaxSectionPages = 200;	// a runaway stop for the overflow loop
const PMReal kContextTint = 40.0;		// the unchanged text, in percent of the text colour
const PMReal kLabelColW = 180.0;		// the first column: a story ID, a kind, an attribute's name (⚠a long name has no break in it: too narrow and the cell shows NOTHING - measured with SwatchColorGroupReference at 130)
const PMReal kLabelPt   = 12.0;			// the first column's size, a little under the body's, for the same reason
const PMReal kSignColW  = 28.0;			// the Δ column: one sign
const int32  kColumns   = 4;			// label | Δ | Before | After
const PMReal kCellInset = 4.0;			// the padding between a cell's rules and its text (the user's ask)

PMString Ascii(const char* ascii)
{
	PMString s(ascii);
	s.SetTranslatable(kFalse);
	return s;
}

/** One UTF-16 character as a PMString - the way a sign outside ASCII has to be made. */
PMString OneChar(const char16_t* one)
{
	PMString s;
	s.SetXString(reinterpret_cast<const UTF16TextChar*>(one), 1);
	s.SetTranslatable(kFalse);
	return s;
}

/** The length of `s` in the model's units (code points - what InsertCmd puts in). */
int32 Len(const PMString& s)
{
	return WideString(s).Length();
}

// ---- attributes over a range ----------------------------------------------------------------

bool16 ApplyReal(ITextModel* model, TextIndex start, int32 len, const ClassID& boss, const PMReal& value, const ClassID& strand)
{
	if (len <= 0)
		return kTrue;
	InterfacePtr<ITextAttrRealNumber> attr(::CreateObject2<ITextAttrRealNumber>(boss));
	if (attr == nil)
		return kFalse;
	attr->Set(value);
	InterfacePtr<ICommand> cmd(Utils<ITextAttrUtils>()->BuildApplyTextAttrCmd(model, start, static_cast<uint32>(len), attr, strand));
	return (cmd != nil && CmdUtils::ProcessCommand(cmd) == kSuccess) ? kTrue : kFalse;
}

/** Left-aligned, explicitly: the report document is made from the application's defaults, and on
    the machine this was written on those justify every line (measured 2026-09-13). */
bool16 ApplyAlignLeft(ITextModel* model, TextIndex start, int32 len)
{
	if (len <= 0)
		return kTrue;
	InterfacePtr<ITextAttrAlign> align(::CreateObject2<ITextAttrAlign>(kTextAttrAlignmentBoss));
	if (align == nil)
		return kFalse;
	align->SetAlignment(ICompositionStyle::kTextAlignLeft);
	InterfacePtr<ICommand> cmd(Utils<ITextAttrUtils>()->BuildApplyTextAttrCmd(model, start, static_cast<uint32>(len), align, kParaAttrStrandBoss));
	return (cmd != nil && CmdUtils::ProcessCommand(cmd) == kSuccess) ? kTrue : kFalse;
}

bool16 ApplySuperscript(ITextModel* model, TextIndex start, int32 len)
{
	if (len <= 0)
		return kTrue;
	InterfacePtr<ITextAttrPositionMode> mode(::CreateObject2<ITextAttrPositionMode>(kTextAttrPositionModeBoss));
	if (mode == nil)
		return kFalse;
	mode->SetMode(IDrawingStyle::kPosSuperscript);
	InterfacePtr<ICommand> cmd(Utils<ITextAttrUtils>()->BuildApplyTextAttrCmd(model, start, static_cast<uint32>(len), mode, kCharAttrStrandBoss));
	return (cmd != nil && CmdUtils::ProcessCommand(cmd) == kSuccess) ? kTrue : kFalse;
}

bool16 InsertText(ITextModel* model, TextIndex at, const PMString& text)
{
	if (text.IsEmpty())
		return kTrue;
	InterfacePtr<ITextModelCmds> cmds(model, UseDefaultIID());
	if (cmds == nil)
		return kFalse;
	boost::shared_ptr<WideString> data(new WideString(text));
	InterfacePtr<ICommand> cmd(cmds->InsertCmd(at, data));
	return (cmd != nil && CmdUtils::ProcessCommand(cmd) == kSuccess) ? kTrue : kFalse;
}

/** Type one cell: the pieces, then the attributes by range. The cell's thread is looked up
    fresh each time - every insertion moves the threads after it. */
bool16 FillCell(ITextModel* model, ITableModel* table, int32 row, int32 col, const KCMReportCell& cell, bool16 heading, PMString& why,
				const PMReal& pointSize = kKCMReportBodyPt)
{
	const int32 preLen  = Len(cell.fPre);
	const int32 midLen  = Len(cell.fMid);
	const int32 noteLen = Len(cell.fNote);
	const int32 postLen = Len(cell.fPost);
	const int32 total = preLen + midLen + noteLen + postLen;
	if (total == 0)
		return kTrue;

	InterfacePtr<ICellContent> content(table->QueryCellContentBoss(GridAddress(row, col)));
	InterfacePtr<ITextStoryThread> thread(content, UseDefaultIID());
	if (thread == nil)
	{
		why = Ascii("a table cell of the report has no text thread");
		return kFalse;
	}
	const TextIndex start = thread->GetTextStart();

	PMString all(cell.fPre);
	all.Append(cell.fMid);
	all.Append(cell.fNote);
	all.Append(cell.fPost);
	all.SetTranslatable(kFalse);
	if (!InsertText(model, start, all))
	{
		why = Ascii("the text of a table cell could not be typed");
		return kFalse;
	}

	// The cell as a whole: its size, left-aligned.
	ApplyReal(model, start, total, kTextAttrPointSizeBoss, pointSize, kCharAttrStrandBoss);
	ApplyAlignLeft(model, start, total);

	// The context pale, the change at full strength (the user's ask). A heading row is all one
	// piece and stays dark.
	if (!heading)
	{
		ApplyReal(model, start, preLen, kTextAttrTintBoss, kContextTint, kCharAttrStrandBoss);
		ApplyReal(model, start + preLen + midLen + noteLen, postLen, kTextAttrTintBoss, kContextTint, kCharAttrStrandBoss);
	}
	// A note number rides above the line, right after the change.
	if (noteLen > 0)
		ApplySuperscript(model, start + preLen + midLen, noteLen);
	// A reading as real ruby, a kind as real kenten - the restore's own writers. ★THE READING IS
	// SET AT THE BODY'S OWN SIZE (the user's ask, 2026-09-13: "like the panel, ruby the same size
	// as the text - easier to read; ruby and kenten take two lines then"), and the kenten mark at
	// a little over half of it; the cell's top inset (TableInsets) makes the second line's room.
	if (midLen > 0 && !cell.fRuby.IsEmpty())
	{
		if (KCMCreateRubyStrandIfNeeded(model) == kSuccess
			&& KCMApplyRuby(model, start + preLen, midLen, cell.fRuby, cell.fRubyGroup) == kSuccess)
			ApplyReal(model, start + preLen, midLen, kTARubyPointSizeBoss, kKCMReportBodyPt, kCharAttrStrandBoss);
	}
	if (midLen > 0 && cell.fKentenKind >= 0)
	{
		if (KCMApplyKentenKind(model, start + preLen, midLen, cell.fKentenKind) == kSuccess)
			ApplyReal(model, start + preLen, midLen, kTAKentenSizeBoss, kKCMReportBodyPt * 0.6, kCharAttrStrandBoss);
	}
	return kTrue;
}

/** Whether a cell carries something on a second line above its text. */
bool16 HasUpperLine(const KCMReportCell& cell)
{
	return (!cell.fRuby.IsEmpty() || cell.fKentenKind >= 0) ? kTrue : kFalse;
}

/** Cell insets over `area`: the padding between a cell's rules and its text (the user's ask,
    2026-09-13), with `top` chosen by the caller - a row with a reading or a kenten above its
    text gets a taller one. */
bool16 ApplyInsets(ITableModel* table, const GridArea& area, const PMReal& top, const PMReal& sides, const PMReal& bottom)
{
	InterfacePtr<ITableCommands> cmds(table, UseDefaultIID());
	if (cmds == nil)
		return kFalse;
	AttributeBossList attrs;
	struct One { ClassID fBoss; PMReal fValue; };
	const One insets[] =
	{
		{ kCellAttrTopInsetBoss,    top },
		{ kCellAttrBottomInsetBoss, bottom },
		{ kCellAttrLeftInsetBoss,   sides },
		{ kCellAttrRightInsetBoss,  sides },
	};
	for (size_t i = 0; i < sizeof(insets) / sizeof(insets[0]); ++i)
	{
		InterfacePtr<ITableAttrRealNumber> attr(::CreateObject2<ITableAttrRealNumber>(insets[i].fBoss));
		if (attr == nil)
			return kFalse;
		attr->Set(insets[i].fValue);
		attrs.ApplyAttribute(attr);
	}
	return (cmds->ApplyCellOverrides(area, &attrs) == kSuccess) ? kTrue : kFalse;
}

/** Type the four cells of one table row: the label, the sign, the older side, the newer side.
    The label and the sign are always full strength (a plain cell each). A HEADING row's two
    side cells are merged into one first (the user's ask, 2026-09-13: "join the parent row's two
    cells into one"), and its text goes into that one; `mergeSides` is kFalse for the header row,
    which keeps its Before / After. A row with an upper line (a reading, a kenten) gets a taller
    top inset on its side cells so that line has room. */
bool16 FillRow(ITextModel* model, ITableModel* table, int32 row, const KCMReportRow& spec, bool16 mergeSides, PMString& why)
{
	if (spec.fHeading && mergeSides)
	{
		InterfacePtr<ITableCommands> cmds(table, UseDefaultIID());
		if (cmds != nil)
			cmds->MergeCells(GridArea(row, 2, row + 1, kColumns));	// the two side cells become one, anchored at column 2
	}
	if (HasUpperLine(spec.fLeft) || HasUpperLine(spec.fRight))
		ApplyInsets(table, GridArea(row, 2, row + 1, kColumns), kKCMReportBodyPt * 1.15 + kCellInset, kCellInset, kCellInset);

	KCMReportCell label;
	label.fMid = spec.fLabel;
	KCMReportCell sign;
	sign.fMid = spec.fSign;
	// A row whose first cell was merged into the row above's has no cell of its own there.
	if (!spec.fJoinLabel && !FillCell(model, table, row, 0, label, kTrue, why, kLabelPt))
		return kFalse;
	if (!FillCell(model, table, row, 1, sign, kTrue, why)
		|| !FillCell(model, table, row, 2, spec.fLeft, spec.fHeading, why))
		return kFalse;
	if (spec.fHeading && mergeSides)
		return kTrue;		// the merged cell holds the heading; there is no column 3 on this row
	return FillCell(model, table, row, 3, spec.fRight, spec.fHeading, why);
}

/** The text frame (column) a table row was composed into - which PAGE it landed on, in effect -
    read off the row's first cell's parcel. kInvalidUID for a row that is overset or not composed. */
UID FrameOfRow(ITableModel* table, int32 row)
{
	InterfacePtr<ICellContent> content(table->QueryCellContentBoss(GridAddress(row, 0)));
	if (content == nil || content->GetParcelCount() < 1)
		return kInvalidUID;
	return content->GetParcelFrameUID(content->GetNthParcelKey(0));
}

/** One first cell for a row and the rows that join it (a story's ID over its edits), PER PAGE:
    a run is merged as far as it stays on one page, and starts a new merged cell - with the label
    typed again - where the page breaks (the user's ask, 2026-09-13: "one cell for the same ID,
    except across a page").

    ⚠**A MERGED CELL CANNOT BREAK ACROSS PAGES** (measured 2026-09-13: one story with 80 edits
      merged into one cell never fitted, the overflow loop added 200 pages and gave up), which is
      why this runs AFTER the table has been flowed through its pages and composed: only then
      does a row know its page. Merging afterwards moves nothing - the first column holds a short
      ID, and a row's height comes from its side cells - so the pages stay as they were.
    The joining rows' first cells are empty (FillRow typed nothing there), so a merge concatenates
    nothing; the label is typed into the anchor of every piece after the first. */
bool16 MergeLabelRunsByPage(const UIDRef& story, const std::vector<KCMReportRow>& rows, PMString& why)
{
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	InterfacePtr<ITableModelList> tables(model, UseDefaultIID());
	InterfacePtr<ITableModel> table(tables != nil && tables->GetModelCount() > 0 ? tables->QueryNthModel(0) : nil);
	InterfacePtr<ITableCommands> cmds(table, UseDefaultIID());
	if (model == nil || table == nil || cmds == nil)
	{
		why = Ascii("the table could not be reopened to merge its first column");
		return kFalse;
	}
	const int32 bodyRows = static_cast<int32>(rows.size());
	for (int32 r = 0; r < bodyRows; )
	{
		int32 end = r + 1;
		while (end < bodyRows && rows[end].fJoinLabel)
			++end;
		for (int32 piece = r; piece < end; )
		{
			const UID frame = FrameOfRow(table, piece + 1);		// body row r is table row r + 1 (the header row is 0)
			int32 pieceEnd = piece + 1;
			while (pieceEnd < end && frame != kInvalidUID && FrameOfRow(table, pieceEnd + 1) == frame)
				++pieceEnd;
			if (pieceEnd - piece > 1)
				cmds->MergeCells(GridArea(piece + 1, 0, pieceEnd + 1, 1));
			if (piece != r)
			{
				KCMReportCell label;
				label.fMid = rows[r].fLabel;
				if (!FillCell(model, table, piece + 1, 0, label, kTrue, why, kLabelPt))
					return kFalse;
			}
			piece = pieceEnd;
		}
		r = end;
	}
	return kTrue;
}

/** The frame list behind a text frame (its kMultiColumnItemBoss), composed up to date. */
IFrameList* QueryComposedFrameList(IDataBase* db, const UIDRef& frame)
{
	InterfacePtr<IGraphicFrameData> frameData(frame, UseDefaultIID());
	if (frameData == nil)
		return nil;
	InterfacePtr<IMultiColumnTextFrame> mcf(db, frameData->GetTextContentUID(), UseDefaultIID());
	if (mcf == nil)
		return nil;
	IFrameList* frameList = mcf->QueryFrameList();
	if (frameList != nil && frameList->GetFirstDamagedFrameIndex() != -1)
	{
		InterfacePtr<IFrameListComposer> composer(frameList, UseDefaultIID());
		if (composer != nil)
			composer->RecomposeThruLastFrame();
	}
	return frameList;
}

bool16 IsOverset(IDataBase* db, const UIDRef& frame)
{
	InterfacePtr<IFrameList> frameList(QueryComposedFrameList(db, frame));
	if (frameList == nil)
		return kFalse;
	Utils<ITextUtils> textUtils;
	return (textUtils && textUtils->IsOverset(frameList)) ? kTrue : kFalse;
}

/** Thread `from` into `to` (both text frames; `to` freshly made, so its story is empty). */
bool16 LinkFrames(IDataBase* db, const UIDRef& from, const UIDRef& to)
{
	InterfacePtr<IGraphicFrameData> fromData(from, UseDefaultIID());
	InterfacePtr<IGraphicFrameData> toData(to, UseDefaultIID());
	if (fromData == nil || toData == nil)
		return kFalse;
	InterfacePtr<ICommand> link(CmdUtils::CreateCommand(kTextLinkCmdBoss));
	if (link == nil)
		return kFalse;
	UIDList items(db);
	items.Append(fromData->GetTextContentUID());
	items.Append(toData->GetTextContentUID());
	link->SetItemList(items);
	return (CmdUtils::ProcessCommand(link) == kSuccess) ? kTrue : kFalse;
}

/** The table's frame on a page: the full width under the heading band. */
PMRect TableBounds(const PMRect& page)
{
	return PMRect(page.Left() + kKCMReportGutter, page.Top() + kKCMReportHeaderBand,
				  page.Right() - kKCMReportGutter, page.Bottom() - kKCMReportGutter);
}

}	// namespace

// ---- the signs ---------------------------------------------------------------------------------

PMString KCMReportSign::Plus()     { return Ascii("+"); }
PMString KCMReportSign::Minus()    { return Ascii("-"); }
PMString KCMReportSign::Equal()    { return Ascii("="); }
PMString KCMReportSign::NotEqual() { static const char16_t s[] = u"≠"; return OneChar(s); }
PMString KCMReportSign::Delta()    { static const char16_t s[] = u"Δ"; return OneChar(s); }

// ---- page helpers ------------------------------------------------------------------------------

int32 KCMReportPageCount(IDataBase* reportDB)
{
	if (reportDB == nil)
		return 0;
	InterfacePtr<ISpreadList> spreads(reportDB, reportDB->GetRootUID(), UseDefaultIID());
	return (spreads != nil) ? spreads->GetSpreadCount() : 0;
}

bool16 KCMReportAddPage(IDataBase* reportDB)
{
	if (reportDB == nil)
		return kFalse;
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kNewSpreadCmdBoss));
	InterfacePtr<INewSpreadCmdData> data(cmd, UseDefaultIID());
	InterfacePtr<IApplyMasterCmdData> master(cmd, IID_IAPPLYMASTERCMDDATA);
	InterfacePtr<IIntData> binding(cmd, IID_IBINDINGLOCATION);
	if (cmd == nil || data == nil || master == nil || binding == nil)
		return kFalse;
	// One spread of one page at the end, the size of its neighbour (the document's only size).
	data->SetNewSpreadCmdData(UIDRef(reportDB, reportDB->GetRootUID()), 1, ISpreadList::kAtTheEnd,
							  INewSpreadCmdData::kPagesMayShuffleThroughNewSpread, 1, kPMPageSizeNeighbor);
	master->SetApplyMasterCmdData(kInvalidUIDRef, IApplyMasterCmdData::kKeepCurrentPageSize);
	binding->Set(ISpread::kDefaultBinding);
	return (CmdUtils::ProcessCommand(cmd) == kSuccess) ? kTrue : kFalse;
}

bool16 KCMReportPageAt(SDKLayoutHelper& helper, IDataBase* db, int32 n, PMRect& outPageRect, UIDRef& outLayer)
{
	InterfacePtr<ISpreadList> spreads(db, db->GetRootUID(), UseDefaultIID());
	if (spreads == nil || n < 0 || n >= spreads->GetSpreadCount())
		return kFalse;
	const UIDRef spreadRef(db, spreads->GetNthSpreadUID(n));
	InterfacePtr<ISpread> spread(spreadRef, UseDefaultIID());
	if (spread == nil || spread->GetNumPages() < 1)
		return kFalse;
	const UIDRef pageRef(db, spread->GetNthPageUID(0));
	InterfacePtr<IGeometry> geometry(pageRef, UseDefaultIID());
	if (geometry == nil)
		return kFalse;
	// Spread coordinates (PageToSpread is margin-based, so it is not used).
	outPageRect = geometry->GetStrokeBoundingBox(::InnerToParentMatrix(geometry));
	outLayer = helper.GetActiveSpreadLayerRef(spreadRef);
	return (outLayer != UIDRef::gNull) ? kTrue : kFalse;
}

void KCMReportTypeAt(SDKLayoutHelper& helper, const UIDRef& layer, const PMRect& bounds, const PMString& text, const PMReal& pointSize)
{
	UIDRef story;
	const UIDRef frame = helper.CreateTextFrame(layer, bounds, 1, kFalse, &story);
	if (frame == UIDRef::gNull || story == UIDRef::gNull)
		return;
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	if (model == nil)
		return;
	const int32 len = Len(text);
	if (!InsertText(model, 0, text) || len <= 0)
		return;
	// Left-aligned as a paragraph override over the whole story (the way SnpManipulateTextModel
	// does), and at pointSize as a character override.
	ApplyAlignLeft(model, 0, len);
	ApplyReal(model, 0, len, kTextAttrPointSizeBoss, pointSize, kCharAttrStrandBoss);
}

// ---- a table section ---------------------------------------------------------------------------

bool16 KCMReportWriteTable(IDataBase* reportDB, int32 firstPage, const PMString& heading, const PMString& labelHeading,
						   const std::vector<KCMReportRow>& rows, int32& outNextPage, PMString& why)
{
	outNextPage = firstPage;
	if (reportDB == nil || firstPage < 0)
	{
		why = Ascii("no report document for a table section");
		return kFalse;
	}
	SDKLayoutHelper helper;

	// The first page of the section, made if the document does not reach it yet.
	while (KCMReportPageCount(reportDB) <= firstPage)
		if (!KCMReportAddPage(reportDB))
		{
			why = Ascii("a page could not be added to the report");
			return kFalse;
		}
	PMRect page;
	UIDRef layer;
	if (!KCMReportPageAt(helper, reportDB, firstPage, page, layer))
	{
		why = Ascii("the report document has no page for a table section");
		return kFalse;
	}
	KCMReportTypeAt(helper, layer, PMRect(page.Left() + kKCMReportGutter, page.Top() + 12, page.Right() - kKCMReportGutter, page.Top() + kKCMReportHeaderBand - 4), heading, kKCMReportHeadingPt);

	if (rows.empty())
	{
		KCMReportTypeAt(helper, layer, TableBounds(page), Ascii("(none)"), kKCMReportBodyPt);
		outNextPage = firstPage + 1;
		return kTrue;
	}

	// The frame that holds the table, and the story it starts.
	UIDRef story;
	const PMRect bounds = TableBounds(page);
	UIDRef frame = helper.CreateTextFrame(layer, bounds, 1, kFalse, &story);
	if (frame == UIDRef::gNull || story == UIDRef::gNull)
	{
		why = Ascii("the table's text frame could not be made");
		return kFalse;
	}
	{
		InterfacePtr<ITextModel> model(story, UseDefaultIID());
		Utils<ITableUtils> tableUtils;
		if (model == nil || !tableUtils)
		{
			why = Ascii("the table could not be inserted (no text model or no ITableUtils)");
			return kFalse;
		}
		// Four columns (label | Δ | Before | After), a HEADER ROW first - InDesign repeats a header
		// row at the top of every frame the table flows into, so the column names stand on every
		// page. Rows grow with their content, from a MINIMUM that leaves room for a reading or a
		// kenten mark above the characters (measured 2026-09-13 at 0.0: the ruby stood on the
		// cell's top rule). The widths are set right after: InsertTable takes one width for all.
		const int32 bodyRows = static_cast<int32>(rows.size());
		tableUtils->InsertTable(model, 0, 0, bodyRows + 1, kColumns, 1 /*header rows*/, 0 /*footer rows*/,
								kKCMReportBodyPt * 1.6, bounds.Width() / kColumns,
								kTextContentType, ITableUtils::eNoSelection);
		InterfacePtr<ITableModelList> tables(model, UseDefaultIID());
		InterfacePtr<ITableModel> table(tables != nil && tables->GetModelCount() > 0 ? tables->QueryNthModel(0) : nil);
		InterfacePtr<ITableCommands> tableCmds(table, UseDefaultIID());
		if (table == nil || tableCmds == nil)
		{
			why = Ascii("the table was not inserted");
			return kFalse;
		}
		{
			const PMReal sideW = (bounds.Width() - kLabelColW - kSignColW) / 2;
			tableCmds->ResizeCols(ColRange(0, 1), kLabelColW);
			tableCmds->ResizeCols(ColRange(1, 1), kSignColW);
			tableCmds->ResizeCols(ColRange(2, 2), sideW);
		}
		// The padding, over the whole table; rows with an upper line get more at the top (FillRow).
		ApplyInsets(table, GridArea(0, 0, bodyRows + 1, kColumns), kCellInset, kCellInset, kCellInset);


		// The header row: the panel's own headings (ID / Kind, Δ) and the report's two sides.
		{
			KCMReportRow header;
			header.fHeading = kTrue;
			header.fLabel = labelHeading;
			header.fSign = KCMReportSign::Delta();
			header.fLeft.fMid = Ascii("Before");
			header.fRight.fMid = Ascii("After");
			if (!FillRow(model, table, 0, header, kFalse /*keep Before | After apart*/, why))
				return kFalse;
		}
		for (int32 r = 0; r < bodyRows; ++r)
			if (!FillRow(model, table, r + 1, rows[r], kTrue, why))
				return kFalse;
	}

	// Overflow: a page, a frame on it, threaded from the last one - until nothing is overset.
	int32 pageIndex = firstPage;
	int32 added = 0;
	while (IsOverset(reportDB, frame))
	{
		if (++added > kMaxSectionPages)
		{
			why = Ascii("the table section ran past 200 pages");
			return kFalse;
		}
		++pageIndex;
		while (KCMReportPageCount(reportDB) <= pageIndex)
			if (!KCMReportAddPage(reportDB))
			{
				why = Ascii("a page could not be added to the report");
				return kFalse;
			}
		PMRect nextPage;
		UIDRef nextLayer;
		if (!KCMReportPageAt(helper, reportDB, pageIndex, nextPage, nextLayer))
		{
			why = Ascii("the report document lost a page");
			return kFalse;
		}
		const UIDRef next = helper.CreateTextFrame(nextLayer, TableBounds(nextPage), 1, kFalse, nil);
		if (next == UIDRef::gNull || !LinkFrames(reportDB, frame, next))
		{
			why = Ascii("the table's frames could not be threaded");
			return kFalse;
		}
		frame = next;
	}

	// Now that every row has its page: one first cell per story per page.
	if (!MergeLabelRunsByPage(story, rows, why))
		return kFalse;

	outNextPage = pageIndex + 1;
	return kTrue;
}

// End, KCMReportTable.cpp.
