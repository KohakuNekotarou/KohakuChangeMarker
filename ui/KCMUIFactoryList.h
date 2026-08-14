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
REGISTER_PMINTERFACE(KCMUIActionComponent, kKCMUIActionComponentImpl)

// ---- 2026-08-15: model/UI 分割 第2段 Task 5（葉の UI 部品）で KESCM から移ってきた4本 ----
//
// ★**Impl ID は `kKESCM*Impl` のまま**（`kKCMUI*Impl` へ振り替えていない）。これらを名指ししている
//   `.fr` の Class / AddIn ブロックは、まだ KESCM.fr 側にある——boss（パネル・アイコン widget・
//   分割バー・行セル）が移るのは Task 6/7 だから。ここで番号だけ KCMUI へ振り替えると、
//   **KESCM.fr が KCMUIID.h を include する**ことになり、model→UI という禁じられた向きになる。
//   ⇒ **番号の振り替えは boss と一緒に**（Task 6/7）。それまで ID の定義は KESCMID.h に残る。
//
// ⚠ そのため **KCMUI.fr は KESCMID.h を include している**（UI→model なので向きは正しい）。
REGISTER_PMINTERFACE(KESCMIconTip, kKESCMIconTipImpl)		// パネルのイラスト/ツール切替ボタンのツールチップ(KESCMIconTip.cpp)
REGISTER_PMINTERFACE(KESCMNoTip, kKESCMNoTipImpl)			// ツールチップを出さない ITip(KESCMNoTip.cpp)
REGISTER_PMINTERFACE(KESCMSplitterEH, kKESCMSplitterEHImpl)	// 分割バーを掴めなくする IEventHandler(KESCMSplitterEH.cpp)
REGISTER_PMINTERFACE(KESCMPanelView, kKESCMPanelViewImpl)	// パネルの最小サイズを守る(PalettePanelView派生。KESCMPanelView.cpp)
