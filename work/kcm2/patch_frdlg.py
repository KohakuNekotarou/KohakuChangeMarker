import sys; sys.path.insert(0,'work/kcm2')
from ed import apply
T = chr(9)

CLASS = '''	Class
	{
		kKCMBookDialogBoss,
		kDialogBoss,
'''
NEWCLASS = '''	/*
	 * The paw word box (2026-09-07): one line of text, asked for when the cat-paw tool is pressed
	 * with Alt. Stock kDialogBoss plus our controller, the same shape as the book dialog below.
	 * ⚠It is opened MODAL and a millisecond AFTER the tracker lets go -- KCMPawWordDialog.h carries
	 *   the reason, which is the same one KCM's own tool-button flyout already obeys.
	 */
	Class
	{
		kKCMPawWordDialogBoss,
		kDialogBoss,
		{
			IID_IDIALOGCONTROLLER, kKCMPawWordDialogControllerImpl,
			// No IID_IOBSERVER, for the reason spelled out on the book dialog below: naming one
			// REPLACES the stock CDialogObserver that makes the close box and the buttons work.
		}
	},

''' + CLASS

TYPE = '''type KCMBookDialogWidget(kViewRsrcType) : DialogBoss(ClassID = kKCMBookDialogBoss)
{
	WidgetEveInfo;
};
'''
NEWTYPE = TYPE + '''
// The paw word box (2026-09-07). Same form, one line of text and a button row.
type KCMPawWordDialogWidget(kViewRsrcType) : DialogBoss(ClassID = kKCMPawWordDialogBoss)
{
	WidgetEveInfo;
};
'''

LOC = '''resource LocaleIndex (kKCMBookDialogRsrcID)
{
	kViewRsrcType,
	{
		kWildFS, k_Wild, kKCMBookDialogRsrcID + index_enUS
	}
};
'''
NEWLOC = LOC + '''
resource LocaleIndex (kKCMPawWordDialogRsrcID)
{
	kViewRsrcType,
	{
		kWildFS, k_Wild, kKCMPawWordDialogRsrcID + index_enUS
	}
};

/*
 * The paw word box. Laid out with EVE exactly as KESCL's Jump Offset dialog is (KESCL.fr:1538):
 * the dialog arranges its children in a column, the label and the box share a row, and the button
 * row is right-aligned. ⚠The trailing arrange flag after the children is REQUIRED - without it
 * ODFRC fails with R32697.
 * ★The width is set by the CHILD frames, never by this one (the book dialog above carries the
 *   eight measurements behind that).
 */
resource KCMPawWordDialogWidget (kKCMPawWordDialogRsrcID + index_enUS)
{
	__FILE__, __LINE__,
	kKCMPawWordDialogWidgetID,		// WidgetID
	kPMRsrcID_None,					// RsrcID
	kBindNone,						// Binding
	Frame(0, 0, 320, 100),			// Frame (l,t,r,b) - EVE follows the children, not this
	kTrue, kTrue,					// Visible, Enabled
	kKCMPawWordDialogTitleKey,		// Dialog name
	{
		// ----- the one row: label + box -----
		EVEGenericPanelWidget
		(
			kInvalidWidgetID,		// WidgetId
			0						// RsrcId
			0,						// Widget EVE Info
			kBindNone,				// Frame binding
			Frame(0, 0, 300, 24)	// Frame
			kTrue,					// Visible
			kTrue,					// Enabled
			kEVEAlignLeft | kEVERegularSpaceAfter | kEVEArrangeChildrenInRow,

			{
				EVEStaticTextWidget
				(
					kInvalidWidgetID,			// WidgetId
					kSysStaticTextPMRsrcId,		// RsrcId
					kBindNone,					// Frame binding
					Frame(0, 0, 40, 20)			// Frame (l,t,r,b)
					kTrue, kTrue, kAlignLeft,	// Visible, Enabled, Alignment
					kDontEllipsize, kTrue,		// Ellipsize style, Convert ampersands
					kKCMPawWordLabelKey,		// Text ("Note:")
					kKCMPawWordEditWidgetID	// Associated control for shortcut focus

					kEVEAlignLeft | kEVERegularSpaceAfter,
				),

				EVETextEditBoxWidget
				(
					kKCMPawWordEditWidgetID,	// WidgetId
					kSysEditBoxPMRsrcId,		// RsrcId
					kBindNone,					// Frame binding
					Frame(0, 0, 240, 20)		// Frame (l,t,r,b) - this number is the dialog's width
					kTrue, kTrue				// Visible, Enabled
					0,							// Widget id of nudge button (0 = none)
					0, 0,						// small, large nudge amount
					0,							// max num chars (0 = no limit)
					kFalse,						// is read only
					kFalse,						// should notify each key stroke
					kFalse,						// range checking enabled (there is nothing to range)
					kTrue,						// ★blank entry allowed - an empty OK still places a paw
					0,							// Upper bounds
					0,							// Lower bounds
					"",							// Initial text (the controller empties it every time)

					kEVEAlignLeft | kEVERegularSpaceAfter,
				),
			}
		),

		// ----- OK / Cancel -----
		EVEGenericPanelWidget
		(
			kInvalidWidgetID,		// WidgetId
			0						// RsrcId
			0,						// Widget EVE Info
			kBindNone,				// Frame binding
			Frame(0, 0, 160, 24)	// Frame
			kTrue,					// Visible
			kTrue,					// Enabled
			kEVEAlignRight | kEVERegularSpaceAfter | kEVEArrangeChildrenInRow,

			{
				EVEDefaultButtonWidget
				(
					kOKButtonWidgetID,		// WidgetID
					kSysButtonPMRsrcId,		// RsrcID
					kBindNone,				// Binding
					Frame(0, 0, 0, 0)		// Frame (l,t,r,b)
					kTrue, kTrue,			// Visible, Enabled
					kSDKDefOKButtonApplicationKey,	// Button text

					kEVERegularSpaceAfter,
				),

				EVECancelButtonWidget
				(
					kCancelButton_WidgetID,	// WidgetID
					kSysButtonPMRsrcId,		// RsrcID
					kBindNone,				// Binding
					Frame(0, 0, 0, 0)		// Frame (l,t,r,b)
					kTrue, kTrue,			// Visible, Enabled
					kSDKDefCancelButtonApplicationKey,	// Button name
					kTrue,					// Change to Reset on option-click.

					kEVERegularSpaceAfter,
				),
			}
		),
	},

	kEVEArrangeChildrenInColumn | kEVELargeMargin,
};
'''

apply('ui/KCMUI.fr', [(CLASS, NEWCLASS), (TYPE, NEWTYPE), (LOC, NEWLOC)])
