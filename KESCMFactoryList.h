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
REGISTER_PMINTERFACE(KESCMDrawEventSrvc, kKESCMDrawEventSrvcImpl)
REGISTER_PMINTERFACE(KESCMDrawEventHandler, kKESCMDrawEventHandlerImpl)
REGISTER_PMINTERFACE(KESCMPeekStartup, kKESCMPeekStartupImpl)
REGISTER_PMINTERFACE(KESCMPanelObserver, kKESCMPanelObserverImpl)
REGISTER_PMINTERFACE(KESCMActionComponent, kKESCMActionComponentImpl)
REGISTER_PMINTERFACE(KESCMDocServiceProvider, kKESCMDocServiceProviderImpl)
REGISTER_PMINTERFACE(KESCMDocResponder, kKESCMDocResponderImpl)
REGISTER_PMINTERFACE(KESCMIconTip, kKESCMIconTipImpl)
REGISTER_PMINTERFACE(KESCMLayoutSyncObserver, kKESCMLayoutSyncObserverImpl)
REGISTER_PMINTERFACE(KESCMThumbIdleTask, kKESCMThumbIdleTaskImpl)
REGISTER_PMINTERFACE(KESCMScrollMapView, kKESCMScrollMapViewImpl)
REGISTER_PMINTERFACE(KESCMTool, kKESCMToolImpl)
REGISTER_PMINTERFACE(KESCMTracker, kKESCMTrackerImpl)
REGISTER_PMINTERFACE(KESCMTrackerEH, kKESCMTrackerEHImpl)
REGISTER_PMINTERFACE(KESCMSprite, kKESCMSpriteImpl)	// トラッカー描画層の自前 sprite(押下中 HUD の描画。KESCMTracker.cpp)
REGISTER_PMINTERFACE(KESCMTrackerRegister, kKESCMTrackerRegisterImpl)
REGISTER_PMINTERFACE(KESCMCheckCursorProvider, kKESCMCursorProviderImpl)
