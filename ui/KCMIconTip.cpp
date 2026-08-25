//========================================================================================
//
//  KCMIconTip.cpp
//
//  The tooltip shown when the pointer is over a widget carrying kKCMIconWidgetBoss. ★That boss is
//  shared by **three widgets** of the panel, so the wording is chosen by WidgetID:
//    - the illustration (the ON and OFF icons) -> the distribution URL (kKCMRepoURL). Showing the
//      click target itself is what says that clicking goes there.
//    - the tool switch button                  -> the same tool name as the toolbox
//      (kKCMToolStringKey). ★KCMTool::Init passes the same string key to SetName, so the two
//      tooltips cannot disagree.
//
//  ***** WHY IT DERIVES FROM AbstractTip *****
//
//  AbstractTip (source/public/libs/widgetbin/includes/AbstractTip.h) is the base every tooltip in
//  the product code derives from, without exception (linksui's LinkInfoIconTip.cpp:35 among them).
//  The one sample outside the product code does the same
//  (customconditionaltextui/CusCondTxtUIIconTip.cpp:42), and ★**no code of Adobe’s implements ITip
//  directly under CPMUnknown**.
//    ⚠Measured 2026-08-25: the two that do are **ours** -- KESCL’s KESCLButtonTip.cpp and
//      KESCLIconTip.cpp, written before this was established. (The older wording here said "not
//      one in the whole SDK", which counted a set that includes our own plug-ins.)
//
//  The base supplies the two things this class has no opinion about, so only GetTipText is
//  written:
//    UpdateToolTipOnMouseMove - the base returns kFalse (AbstractTip.cpp:45-48), and ITip.h:44-50
//                               wraps that API in ID_DEPRECATED.
//    SetTipText               - the base is empty (AbstractTip.h:56); ITip.h:51-53 states that it
//                               is unimplemented in the general case.
//
//  The implementation lives in DV_WidgetBin.lib. ★That used to be the reason for NOT using it, but
//  KCM links that library in all four configurations for its self-drawn views (the DVControlView
//  of KCMScrollMap) -- see the "custom parts of this project file" section of
//  `buildproj/README.md` -- so the base costs nothing.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "AbstractTip.h"		// the base every product tooltip derives from
#include "IControlView.h"		// GetWidgetID (★asking oneself which widget one is on)

// General includes:
#include "PMString.h"

// Project includes:
#include "KCMUIID.h"

/** The tooltip of kKCMIconWidgetBoss: the click target (the distribution URL) on the
    illustration, the tool name on the tool switch button. */
class KCMIconTip : public AbstractTip
{
public:
	KCMIconTip(IPMUnknown* boss);
	virtual ~KCMIconTip();

	virtual PMString GetTipText(const PMPoint& mouseLocation);
};

CREATE_PMINTERFACE(KCMIconTip, kKCMIconTipImpl)

KCMIconTip::KCMIconTip(IPMUnknown* boss) : AbstractTip(boss)
{
}

KCMIconTip::~KCMIconTip()
{
}

PMString KCMIconTip::GetTipText(const PMPoint& /*mouseLocation*/)
{
	// ★The wording is chosen by which widget this is on. The ITip and the widget are **two
	//   interfaces of one boss**, so asking oneself for IControlView gives the WidgetID (no walking
	//   up to a parent ＝ one step).
	InterfacePtr<IControlView> cv(this, UseDefaultIID());
	if (cv != nil && cv->GetWidgetID() == kKCMToolButtonWidgetID)
	{
		// ★Return **the same string key** as the toolbox tool name (the one KCMTool::Init passes to
		//   SetName). The translatable flag is deliberately left on ＝ let it resolve through the
		//   string table.
		return PMString(kKCMToolStringKey);
	}

	// A URL is not a phrase to translate (which is also why it is in no string table).
	PMString tip(kKCMRepoURL);
	tip.SetTranslatable(kFalse);
	return tip;
}

// End, KCMIconTip.cpp.
