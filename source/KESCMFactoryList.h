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
REGISTER_PMINTERFACE(KESCMDocResponder, kKESCMDocResponderImpl)	// ServiceProvider は API 提供の実装を .fr で名指し(2026-08-06)
REGISTER_PMINTERFACE(KESCMIconTip, kKESCMIconTipImpl)
REGISTER_PMINTERFACE(KESCMLayoutSyncObserver, kKESCMLayoutSyncObserverImpl)
REGISTER_PMINTERFACE(KESCMDocsClosedObserver, kKESCMDocsClosedObserverImpl)	// 一括クローズ完了で保留した後片付けを流す(KESCMPeek.cpp)
REGISTER_PMINTERFACE(KESCMPanelVisibilityObserver, kKESCMPanelVisibilityObserverImpl)	// パネルの開閉/ドッキング切り替えで半透明を貼り直す(KESCMPanelAlpha.cpp)
REGISTER_PMINTERFACE(KESCMStorySectionToggleObserver, kKESCMStorySectionToggleObserverImpl)	// パネル下部「Story Edits」セクションの開閉ボタン(KESCMStorySectionObserver.cpp)
REGISTER_PMINTERFACE(KESCMPanelRollOver, kKESCMPanelRollOverImpl)	// カーソルが乗っている間だけ半透明を解除(IMouseRollOver。KESCMPanelAlpha.cpp)
REGISTER_PMINTERFACE(KESCMPanelView, kKESCMPanelViewImpl)	// パネルの最小サイズを守る(PalettePanelView派生。KESCMPanelView.cpp)
REGISTER_PMINTERFACE(KESCMNoTip, kKESCMNoTipImpl)	// ツールチップを出さない ITip(KESCMNoTip.cpp)
REGISTER_PMINTERFACE(KESCMSplitterEH, kKESCMSplitterEHImpl)	// 分割バーを掴めなくする IEventHandler(KESCMSplitterEH.cpp)
REGISTER_PMINTERFACE(KESCMStoryTreeAdapter, kKESCMStoryTreeAdapterImpl)	// Story Edits 一覧の中身(ListTreeViewAdapter派生。KESCMStoryTreeAdapter.cpp)
REGISTER_PMINTERFACE(KESCMStoryTreeWidgetMgr, kKESCMStoryTreeWidgetMgrImpl)	// Story Edits 一覧の行の生成と流し込み(CTreeViewWidgetMgr派生。KESCMStoryTreeWidgetMgr.cpp)
REGISTER_PMINTERFACE(KESCMStoryRowEH, kKESCMStoryRowEHImpl)	// Story Edits の行のクリック=ジャンプ/ダブルクリック=ストーリー全文を選択(TreeNodeEventHandler派生。KESCMStoryRowEH.cpp)
REGISTER_PMINTERFACE(KESCMStoryTreeEH, kKESCMStoryTreeEHImpl)	// Story Edits の一覧の↑↓=行を移動して着いた行を表示(TreeViewEventHandler派生。KESCMStoryTreeEH.cpp)
REGISTER_PMINTERFACE(KESCMBookDialogController, kKESCMBookDialogControllerImpl)	// ブック比較のモードレスダイアログ(CDialogController派生。KESCMBookDialog.cpp)
// (KESCMBookDialogObserver は 2026-08-12 に削除＝Compare ボタンごと撤去したので、監視するものが
//  無くなった。IID_IOBSERVER は kDialogBoss の stock 実装へ戻してある＝KESCM.fr の boss 定義を参照。)
REGISTER_PMINTERFACE(KESCMBookTreeAdapter, kKESCMBookTreeAdapterImpl)	// 同ダイアログの章一覧の中身(ListTreeViewAdapter派生。KESCMBookTreeAdapter.cpp)
REGISTER_PMINTERFACE(KESCMBookTreeWidgetMgr, kKESCMBookTreeWidgetMgrImpl)	// 同ダイアログの章一覧の行の生成と流し込み(CTreeViewWidgetMgr派生。KESCMBookTreeWidgetMgr.cpp)
REGISTER_PMINTERFACE(KESCMBookRowEH, kKESCMBookRowEHImpl)	// 同ダイアログの章行のダブルクリック=その章を開く/右クリック=行メニュー(TreeNodeEventHandler派生。KESCMBookRowEH.cpp)
REGISTER_PMINTERFACE(KESCMThumbIdleTask, kKESCMThumbIdleTaskImpl)
REGISTER_PMINTERFACE(KESCMScrollMapView, kKESCMScrollMapViewImpl)
REGISTER_PMINTERFACE(KESCMTool, kKESCMToolImpl)
REGISTER_PMINTERFACE(KESCMTracker, kKESCMTrackerImpl)
REGISTER_PMINTERFACE(KESCMTrackerEH, kKESCMTrackerEHImpl)
REGISTER_PMINTERFACE(KESCMTrackerRegister, kKESCMTrackerRegisterImpl)
REGISTER_PMINTERFACE(KESCMCheckCursorProvider, kKESCMCursorProviderImpl)
REGISTER_PMINTERFACE(KESCMScriptProvider, kKESCMScriptProviderImpl)	// app.kcmStatus(読み取り専用。KESCMScriptProvider.cpp)
