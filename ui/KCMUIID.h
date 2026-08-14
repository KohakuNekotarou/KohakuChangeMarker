//========================================================================================
//  
//  $File: $
//  
//  Owner: 
//  
//  $Author: $
//  
//  $DateTime: $
//  
//  $Revision: $
//  
//  $Change: $
//  
//  Copyright 1997-2012 Adobe Systems Incorporated. All rights reserved.
//  
//  NOTICE:  Adobe permits you to use, modify, and distribute this file in accordance 
//  with the terms of the Adobe license agreement accompanying it.  If you have received
//  this file from a source other than Adobe, then your use, modification, or 
//  distribution of it requires the prior written permission of Adobe.
//  
//========================================================================================


#ifndef __KCMUIID_h__
#define __KCMUIID_h__

#include "SDKDef.h"

// Company:
#define kKCMUICompanyKey	kSDKDefPlugInCompanyKey		// Company name used internally for menu paths and the like. Must be globally unique, only A-Z, 0-9, space and "_".
#define kKCMUICompanyValue	kSDKDefPlugInCompanyValue	// Company name displayed externally.

// Plug-in:
#define kKCMUIPluginName	"KohakuChangeMarkerUI"			// Name of this plug-in.
// ★★★**Adobe から受け取った原文（2026-08-13）**:
//
//     "Following Prefix ID has been registered as per your request below : 0x1EA500 - 0x1EA5FF ."
//
// ★★Adobe が 2026-08-13 に登録した帯 **0x1EA500 - 0x1EA5FF**(256枠)の **後半**。前半 0x1EA500 は
//   model 側(KohakuExtendScriptChangeMarker)が使う。1本の帯を model と UI で分け合うのは Adobe 自身の
//   やり方で、customdatalink(0xb3300) / customdatalinkui(0xb3380) がまさにこの形(実測: それぞれ +0..37 と
//   +0..17 ＝ 両方とも 0xb33xx に収まっている)。ほかに xdocbookworkflow 対は 16 刻み、0x572xx は4本が共有。
//   ⇒ ID の一意性はプラグイン単位ではなく**値**で決まるので、重ならなければ分け方は自由。
// ⚠ 旧値 0x205792(Adobe Developer Console のプラグイン ID を prefix に流用した暫定値)は**破棄**。
#define kKCMUIPrefixNumber	0x1EA580 		// Unique prefix number for this plug-in(registered with Adobe: 0x1EA500-0x1EA5FF).
#define kKCMUIVersion		kSDKDefPluginVersionString						// Version of this plug-in (for the About Box).
#define kKCMUIAuthor		""					// Author of this plug-in (for the About Box).

// Plug-in Prefix: (please change kKCMUIPrefixNumber above to modify the prefix.)
#define kKCMUIPrefix		RezLong(kKCMUIPrefixNumber)				// The unique numeric prefix for all object model IDs for this plug-in.
#define kKCMUIStringPrefix	SDK_DEF_STRINGIZE(kKCMUIPrefixNumber)	// The string equivalent of the unique prefix number for  this plug-in.

// Missing plug-in: (see ExtraPluginInfo resource)
#define kKCMUIMissingPluginURLValue		kSDKDefPartnersStandardValue_enUS // URL displayed in Missing Plug-in dialog
#define kKCMUIMissingPluginAlertValue	kSDKDefMissingPluginAlertValue // Message displayed in Missing Plug-in dialog - provide a string that instructs user how to solve their missing plug-in problem

// PluginID:
DECLARE_PMID(kPlugInIDSpace, kKCMUIPluginID, kKCMUIPrefix + 0)

// ClassIDs:
DECLARE_PMID(kClassIDSpace, kKCMUIActionComponentBoss, kKCMUIPrefix + 0)
//DECLARE_PMID(kClassIDSpace, kKCMUIBoss, kKCMUIPrefix + 3)
//DECLARE_PMID(kClassIDSpace, kKCMUIBoss, kKCMUIPrefix + 4)
//DECLARE_PMID(kClassIDSpace, kKCMUIBoss, kKCMUIPrefix + 5)
//DECLARE_PMID(kClassIDSpace, kKCMUIBoss, kKCMUIPrefix + 6)
//DECLARE_PMID(kClassIDSpace, kKCMUIBoss, kKCMUIPrefix + 7)
//DECLARE_PMID(kClassIDSpace, kKCMUIBoss, kKCMUIPrefix + 8)
//DECLARE_PMID(kClassIDSpace, kKCMUIBoss, kKCMUIPrefix + 9)
//DECLARE_PMID(kClassIDSpace, kKCMUIBoss, kKCMUIPrefix + 10)
//DECLARE_PMID(kClassIDSpace, kKCMUIBoss, kKCMUIPrefix + 11)
//DECLARE_PMID(kClassIDSpace, kKCMUIBoss, kKCMUIPrefix + 12)
//DECLARE_PMID(kClassIDSpace, kKCMUIBoss, kKCMUIPrefix + 13)
//DECLARE_PMID(kClassIDSpace, kKCMUIBoss, kKCMUIPrefix + 14)
//DECLARE_PMID(kClassIDSpace, kKCMUIBoss, kKCMUIPrefix + 15)
//DECLARE_PMID(kClassIDSpace, kKCMUIBoss, kKCMUIPrefix + 16)
//DECLARE_PMID(kClassIDSpace, kKCMUIBoss, kKCMUIPrefix + 17)
//DECLARE_PMID(kClassIDSpace, kKCMUIBoss, kKCMUIPrefix + 18)
//DECLARE_PMID(kClassIDSpace, kKCMUIBoss, kKCMUIPrefix + 19)
//DECLARE_PMID(kClassIDSpace, kKCMUIBoss, kKCMUIPrefix + 20)
//DECLARE_PMID(kClassIDSpace, kKCMUIBoss, kKCMUIPrefix + 21)
//DECLARE_PMID(kClassIDSpace, kKCMUIBoss, kKCMUIPrefix + 22)
//DECLARE_PMID(kClassIDSpace, kKCMUIBoss, kKCMUIPrefix + 23)
//DECLARE_PMID(kClassIDSpace, kKCMUIBoss, kKCMUIPrefix + 24)
//DECLARE_PMID(kClassIDSpace, kKCMUIBoss, kKCMUIPrefix + 25)


// InterfaceIDs:
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 0)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 1)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 2)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 3)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 4)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 5)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 6)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 7)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 8)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 9)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 10)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 11)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 12)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 13)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 14)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 15)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 16)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 17)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 18)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 19)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 20)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 21)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 22)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 23)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 24)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKCMUIINTERFACE, kKCMUIPrefix + 25)


// ImplementationIDs:
DECLARE_PMID(kImplementationIDSpace, kKCMUIActionComponentImpl, kKCMUIPrefix + 0 )
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 1)
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 2)
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 3)
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 4)
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 5)
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 6)
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 7)
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 8)
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 9)
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 10)
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 11)
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 12)
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 13)
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 14)
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 15)
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 16)
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 17)
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 18)
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 19)
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 20)
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 21)
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 22)
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 23)
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 24)
//DECLARE_PMID(kImplementationIDSpace, kKCMUIImpl, kKCMUIPrefix + 25)


// ActionIDs:
DECLARE_PMID(kActionIDSpace, kKCMUIAboutActionID, kKCMUIPrefix + 0)
//DECLARE_PMID(kActionIDSpace, kKCMUIActionID, kKCMUIPrefix + 5)
//DECLARE_PMID(kActionIDSpace, kKCMUIActionID, kKCMUIPrefix + 6)
//DECLARE_PMID(kActionIDSpace, kKCMUIActionID, kKCMUIPrefix + 7)
//DECLARE_PMID(kActionIDSpace, kKCMUIActionID, kKCMUIPrefix + 8)
//DECLARE_PMID(kActionIDSpace, kKCMUIActionID, kKCMUIPrefix + 9)
//DECLARE_PMID(kActionIDSpace, kKCMUIActionID, kKCMUIPrefix + 10)
//DECLARE_PMID(kActionIDSpace, kKCMUIActionID, kKCMUIPrefix + 11)
//DECLARE_PMID(kActionIDSpace, kKCMUIActionID, kKCMUIPrefix + 12)
//DECLARE_PMID(kActionIDSpace, kKCMUIActionID, kKCMUIPrefix + 13)
//DECLARE_PMID(kActionIDSpace, kKCMUIActionID, kKCMUIPrefix + 14)
//DECLARE_PMID(kActionIDSpace, kKCMUIActionID, kKCMUIPrefix + 15)
//DECLARE_PMID(kActionIDSpace, kKCMUIActionID, kKCMUIPrefix + 16)
//DECLARE_PMID(kActionIDSpace, kKCMUIActionID, kKCMUIPrefix + 17)
//DECLARE_PMID(kActionIDSpace, kKCMUIActionID, kKCMUIPrefix + 18)
//DECLARE_PMID(kActionIDSpace, kKCMUIActionID, kKCMUIPrefix + 19)
//DECLARE_PMID(kActionIDSpace, kKCMUIActionID, kKCMUIPrefix + 20)
//DECLARE_PMID(kActionIDSpace, kKCMUIActionID, kKCMUIPrefix + 21)
//DECLARE_PMID(kActionIDSpace, kKCMUIActionID, kKCMUIPrefix + 22)
//DECLARE_PMID(kActionIDSpace, kKCMUIActionID, kKCMUIPrefix + 23)
//DECLARE_PMID(kActionIDSpace, kKCMUIActionID, kKCMUIPrefix + 24)
//DECLARE_PMID(kActionIDSpace, kKCMUIActionID, kKCMUIPrefix + 25)


// WidgetIDs:
//DECLARE_PMID(kWidgetIDSpace, kKCMUIWidgetID, kKCMUIPrefix + 2)
//DECLARE_PMID(kWidgetIDSpace, kKCMUIWidgetID, kKCMUIPrefix + 3)
//DECLARE_PMID(kWidgetIDSpace, kKCMUIWidgetID, kKCMUIPrefix + 4)
//DECLARE_PMID(kWidgetIDSpace, kKCMUIWidgetID, kKCMUIPrefix + 5)
//DECLARE_PMID(kWidgetIDSpace, kKCMUIWidgetID, kKCMUIPrefix + 6)
//DECLARE_PMID(kWidgetIDSpace, kKCMUIWidgetID, kKCMUIPrefix + 7)
//DECLARE_PMID(kWidgetIDSpace, kKCMUIWidgetID, kKCMUIPrefix + 8)
//DECLARE_PMID(kWidgetIDSpace, kKCMUIWidgetID, kKCMUIPrefix + 9)
//DECLARE_PMID(kWidgetIDSpace, kKCMUIWidgetID, kKCMUIPrefix + 10)
//DECLARE_PMID(kWidgetIDSpace, kKCMUIWidgetID, kKCMUIPrefix + 11)
//DECLARE_PMID(kWidgetIDSpace, kKCMUIWidgetID, kKCMUIPrefix + 12)
//DECLARE_PMID(kWidgetIDSpace, kKCMUIWidgetID, kKCMUIPrefix + 13)
//DECLARE_PMID(kWidgetIDSpace, kKCMUIWidgetID, kKCMUIPrefix + 14)
//DECLARE_PMID(kWidgetIDSpace, kKCMUIWidgetID, kKCMUIPrefix + 15)
//DECLARE_PMID(kWidgetIDSpace, kKCMUIWidgetID, kKCMUIPrefix + 16)
//DECLARE_PMID(kWidgetIDSpace, kKCMUIWidgetID, kKCMUIPrefix + 17)
//DECLARE_PMID(kWidgetIDSpace, kKCMUIWidgetID, kKCMUIPrefix + 18)
//DECLARE_PMID(kWidgetIDSpace, kKCMUIWidgetID, kKCMUIPrefix + 19)
//DECLARE_PMID(kWidgetIDSpace, kKCMUIWidgetID, kKCMUIPrefix + 20)
//DECLARE_PMID(kWidgetIDSpace, kKCMUIWidgetID, kKCMUIPrefix + 21)
//DECLARE_PMID(kWidgetIDSpace, kKCMUIWidgetID, kKCMUIPrefix + 22)
//DECLARE_PMID(kWidgetIDSpace, kKCMUIWidgetID, kKCMUIPrefix + 23)
//DECLARE_PMID(kWidgetIDSpace, kKCMUIWidgetID, kKCMUIPrefix + 24)
//DECLARE_PMID(kWidgetIDSpace, kKCMUIWidgetID, kKCMUIPrefix + 25)


// "About Plug-ins" sub-menu:
#define kKCMUIAboutMenuKey			kKCMUIStringPrefix "kKCMUIAboutMenuKey"
#define kKCMUIAboutMenuPath		kSDKDefStandardAboutMenuPath kKCMUICompanyKey

// "Plug-ins" sub-menu:
#define kKCMUIPluginsMenuKey 		kKCMUIStringPrefix "kKCMUIPluginsMenuKey"
#define kKCMUIPluginsMenuPath		kSDKDefPlugInsStandardMenuPath kKCMUICompanyKey kSDKDefDelimitMenuPath kKCMUIPluginsMenuKey

// Menu item keys:

// Other StringKeys:
#define kKCMUIAboutBoxStringKey	kKCMUIStringPrefix "kKCMUIAboutBoxStringKey"
#define kKCMUITargetMenuPath kKCMUIPluginsMenuPath

// Menu item positions:


// Initial data format version numbers
#define kKCMUIFirstMajorFormatNumber  RezLong(1)
#define kKCMUIFirstMinorFormatNumber  RezLong(0)

// Data format version numbers for the PluginVersion resource 
#define kKCMUICurrentMajorFormatNumber kKCMUIFirstMajorFormatNumber
#define kKCMUICurrentMinorFormatNumber kKCMUIFirstMinorFormatNumber

#endif // __KCMUIID_h__
