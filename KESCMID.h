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


#ifndef __KESCMID_h__
#define __KESCMID_h__

#include "SDKDef.h"

// Company:
#define kKESCMCompanyKey	"KohakuNekotarou"	// Company name used internally for menu paths and the like. Must be globally unique, only A-Z, 0-9, space and "_".
#define kKESCMCompanyValue	"KohakuNekotarou"	// Company name displayed externally.

// Plug-in:
#define kKESCMPluginName	"KohakuExtendScriptChangeMarker"			// Name of this plug-in.
#define kKESCMPrefixNumber	0x205515 		// Unique prefix number for this plug-in(*Must* be obtained from Adobe Developer Support).
#define kKESCMVersion		kSDKDefPluginVersionString						// Version of this plug-in (for the About Box).
#define kKESCMAuthor		""					// Author of this plug-in (for the About Box).

// Plug-in Prefix: (please change kKESCMPrefixNumber above to modify the prefix.)
#define kKESCMPrefix		RezLong(kKESCMPrefixNumber)				// The unique numeric prefix for all object model IDs for this plug-in.
#define kKESCMStringPrefix	SDK_DEF_STRINGIZE(kKESCMPrefixNumber)	// The string equivalent of the unique prefix number for  this plug-in.

// Missing plug-in: (see ExtraPluginInfo resource)
#define kKESCMMissingPluginURLValue		kSDKDefPartnersStandardValue_enUS // URL displayed in Missing Plug-in dialog
#define kKESCMMissingPluginAlertValue	kSDKDefMissingPluginAlertValue // Message displayed in Missing Plug-in dialog - provide a string that instructs user how to solve their missing plug-in problem

// PluginID:
DECLARE_PMID(kPlugInIDSpace, kKESCMPluginID, kKESCMPrefix + 0)

// ClassIDs:
// kClassIDSpace +3 は現在空き(旧 kKESCMScriptProviderBoss; スクリプトAPI(kescmToast)は撤去)
DECLARE_PMID(kClassIDSpace, kKESCMDrawEventServiceBoss, kKESCMPrefix + 4)
DECLARE_PMID(kClassIDSpace, kKESCMPeekWatcherBoss, kKESCMPrefix + 5)	// IEventWatcher: ミドルボタン peek(kMButtonDn/Up をスヌープ)
DECLARE_PMID(kClassIDSpace, kKESCMPeekStartupBoss, kKESCMPrefix + 6)	// IStartupShutdown: アプリ起動時に peek ウォッチャを開始
DECLARE_PMID(kClassIDSpace, kKESCMThumbIdleTaskBoss, kKESCMPrefix + 7)	// IIdleTask: クローズ後の Pages サムネイル再生成を次のidleに遅延(旧 kKESCMToastIdleTaskBoss のスロット転用)
DECLARE_PMID(kClassIDSpace, kKESCMPanelWidgetBoss, kKESCMPrefix + 8)	// ChangeMarker 操作パネル(パレット)
DECLARE_PMID(kClassIDSpace, kKESCMActionComponentBoss, kKESCMPrefix + 9)	// About メニューのアクションコンポーネント
DECLARE_PMID(kClassIDSpace, kKESCMDocResponderServiceBoss, kKESCMPrefix + 10)	// IK2ServiceProvider+IResponder: ドキュメントクローズ監視(閉じた文書の追跡状態を確定クリーンアップ)
DECLARE_PMID(kClassIDSpace, kKESCMIconWidgetBoss, kKESCMPrefix + 11)	// kRollOverIconButtonBoss を継承し IID_ITIP を追加(パネルイラストのツールチップ)
DECLARE_PMID(kClassIDSpace, kKESCMScrollMapWidgetBoss, kKESCMPrefix + 12)	// kGenericPanelWidgetBoss+自前IControlView: 縦スクロールバー脇の枠ページ地図strip(旧 kKESCMLayoutSyncObserverBoss のスロット転用)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 6)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 8)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 9)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 10)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 11)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 12)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 13)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 14)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 15)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 16)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 17)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 18)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 19)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 20)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 21)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 22)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 23)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 24)
//DECLARE_PMID(kClassIDSpace, kKESCMBoss, kKESCMPrefix + 25)


// InterfaceIDs:
DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMLAYOUTSYNCOBSERVER, kKESCMPrefix + 0)	// レイアウトビュー同期オブザーバのアタッチ識別ID(AttachObserver の observerIID)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 1)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 2)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 3)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 4)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 5)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 6)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 7)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 8)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 9)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 10)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 11)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 12)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 13)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 14)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 15)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 16)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 17)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 18)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 19)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 20)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 21)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 22)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 23)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 24)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 25)


// ImplementationIDs:
// kImplementationIDSpace +0 は現在空き(旧 kKESCMScriptProviderImpl; スクリプトAPI(kescmToast)は撤去)
DECLARE_PMID(kImplementationIDSpace, kKESCMDrawEventSrvcImpl, kKESCMPrefix + 1)
DECLARE_PMID(kImplementationIDSpace, kKESCMDrawEventHandlerImpl, kKESCMPrefix + 2)
DECLARE_PMID(kImplementationIDSpace, kKESCMPeekWatcherImpl, kKESCMPrefix + 3)	// IEventWatcher 実装(ミドルボタン peek)
DECLARE_PMID(kImplementationIDSpace, kKESCMPeekStartupImpl, kKESCMPrefix + 4)	// IStartupShutdown 実装(peek ウォッチャを開始)
DECLARE_PMID(kImplementationIDSpace, kKESCMThumbIdleTaskImpl, kKESCMPrefix + 5)	// IIdleTask 実装(クローズ後の Pages サムネイル再生成を遅延実行)
DECLARE_PMID(kImplementationIDSpace, kKESCMPanelObserverImpl, kKESCMPrefix + 6)	// IObserver 実装(パネルのウィジェットオブザーバ)
DECLARE_PMID(kImplementationIDSpace, kKESCMActionComponentImpl, kKESCMPrefix + 7)	// IActionComponent 実装(About)
DECLARE_PMID(kImplementationIDSpace, kKESCMDocServiceProviderImpl, kKESCMPrefix + 8)	// IK2ServiceProvider 実装(クローズ監視のサービス登録)
DECLARE_PMID(kImplementationIDSpace, kKESCMDocResponderImpl, kKESCMPrefix + 9)	// IResponder 実装(クローズ確定時の追跡状態クリーンアップ)
DECLARE_PMID(kImplementationIDSpace, kKESCMIconTipImpl, kKESCMPrefix + 10)	// ITip 実装(パネルイラストにURLをツールチップ表示)
DECLARE_PMID(kImplementationIDSpace, kKESCMLayoutSyncObserverImpl, kKESCMPrefix + 11)	// IObserver 実装(レイアウトビュー同期)
DECLARE_PMID(kImplementationIDSpace, kKESCMScrollMapViewImpl, kKESCMPrefix + 12)	// IControlView 実装(スクロールバー地図stripの自前描画; KESCMScrollMap.cpp)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 6)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 7)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 8)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 9)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 10)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 11)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 12)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 13)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 14)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 15)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 16)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 17)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 18)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 19)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 20)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 21)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 22)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 23)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 24)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 25)


// ActionIDs:
DECLARE_PMID(kActionIDSpace, kKESCMAboutActionID, kKESCMPrefix + 0)
DECLARE_PMID(kActionIDSpace, kKESCMPanelWidgetActionID, kKESCMPrefix + 1)	// パネルの表示/非表示(ウィンドウメニュー)
DECLARE_PMID(kActionIDSpace, kKESCMPopupAboutThisActionID, kKESCMPrefix + 2)	// パネルのフライアウトの「このプラグインについて」
DECLARE_PMID(kActionIDSpace, kKESCMPopupAboutScriptActionID, kKESCMPrefix + 3)	// パネルのフライアウトの「スクリプトについて」
DECLARE_PMID(kActionIDSpace, kKESCMPopupUsageActionID, kKESCMPrefix + 4)	// パネルのフライアウトの「使い方」
// kActionIDSpace +5 は現在空き(旧 kKESCMPopupTestSplitActionID; Split Test 検証メニューは撤去済み)
// kActionIDSpace +6 は現在空き(旧 kKESCMPopupSplitTargetActionID; Split Target on Start は 2026-07-04 撤去。
//   仕組みは docs/ai-notes/kescm-split-target-mechanism.md に保存)
DECLARE_PMID(kActionIDSpace, kKESCMPopupHideUnchangedActionID, kKESCMPrefix + 7)	// パネルのフライアウトの「Hide Unchanged Spreads」チェック式トグル(ON=変更なしスプレッドを隠す)
DECLARE_PMID(kActionIDSpace, kKESCMPopupShowOldNumsActionID, kKESCMPrefix + 8)	// パネルのフライアウトの「Show Original Page Numbers」チェック式トグル(枠表示中/印刷ON時に隠す前の元番号バッジ)
DECLARE_PMID(kActionIDSpace, kKESCMPopupSyncViewsActionID, kKESCMPrefix + 9)	// パネルのフライアウトの「Sync Layout Views」チェック式トグル(他文書のビューへ座標+拡大率を自動同期)
DECLARE_PMID(kActionIDSpace, kKESCMPopupShowSrcMarksActionID, kKESCMPrefix + 10)	// パネルのフライアウトの「Show Marks on Source」チェック式トグル(Source側にも枠を常時表示。OPPでも表示・印刷にも出す。Startで既定ON)
DECLARE_PMID(kActionIDSpace, kKESCMPageMapToggleActionID, kKESCMPrefix + 11)	// ページパネルのページ右クリック(RtMenuPagesPanel)のトグル「KESCM: Register as Added/Removed Pages」(選択ページを「比較相手なし」として登録/解除。チェック/動的ラベルは kCustomEnabling。KESCMPageMap.cpp)
DECLARE_PMID(kActionIDSpace, kKESCMPopupIgnorePageNumActionID, kKESCMPrefix + 12)	// パネルのフライアウトの「Ignore Page Number Marker」チェック式トグル(ON=ノンブル(自動ページ番号)マーカーを含むフレームを比較から除外。既定ON。KESCMPageNumberMarker.cpp)
DECLARE_PMID(kActionIDSpace, kKESCMPopupStartStopActionID, kKESCMPrefix + 13)	// パネルのフライアウト先頭の「Start / Stop」(比較の開始/解除。旧トグルボタンをメニュー化。arm 状態で名前が Start↔Stop に動的変化=kCustomEnabling+SetNthActionName。KESCMPanelObserver.cpp の KESCMToggleStartStop)
DECLARE_PMID(kActionIDSpace, kKESCMPopupPrintMarksActionID, kKESCMPrefix + 14)	// パネルのフライアウトの「Print comparison marks」チェック式トグル(旧パネルのチェックボックスをメニュー化。ON=マークを印刷し画面にも常時表示。KESCMPanelObserver.cpp の KESCMTogglePrintMarks)
DECLARE_PMID(kActionIDSpace, kKESCMPopupOpacity25ActionID, kKESCMPrefix + 15)	// パネルのフライアウトの「Marks opacity 25%」(旧パネルの opacity ラジオをメニュー化。75% と相互排他=選択中の方に✓。KESCMPanelObserver.cpp の KESCMSetMarkOpacity25)
DECLARE_PMID(kActionIDSpace, kKESCMPopupOpacity75ActionID, kKESCMPrefix + 16)	// パネルのフライアウトの「Marks opacity 75%」(25% と相互排他)
DECLARE_PMID(kActionIDSpace, kKESCMPopupSep1ActionID, kKESCMPrefix + 17)	// フライアウト: Start の下の区切り線(MenuDef のパス末尾 ":-"。ActionDef 不要・DoAction 不要=一意なIDだけ要る)
DECLARE_PMID(kActionIDSpace, kKESCMPopupSep2ActionID, kKESCMPrefix + 18)	// フライアウト: How to Use の上の区切り線
DECLARE_PMID(kActionIDSpace, kKESCMPopupHoldToHideMarksActionID, kKESCMPrefix + 19)	// パネルのフライアウトの「Hold to Hide Marks」チェック式トグル(ON=枠を画面に常時表示し、ミドル押下中だけ隠す=極性反転。画面のみ・印刷は Print comparison marks が別管理。KESCMActionComponent.cpp)
DECLARE_PMID(kActionIDSpace, kKESCMPopupPanelShortcutActionID, kKESCMPrefix + 20)	// パネルのフライアウトの「Invoke Panel Shortcut」チェック式トグル(ON=Shift+Ctrl+ミドルでパネル表示/非表示を切替えるショートカットを有効化。既定ON。実体 KESCMPeek.cpp の sPanelShortcutOn)
DECLARE_PMID(kActionIDSpace, kKESCMPopupScrollMapActionID, kKESCMPrefix + 21)	// パネルのフライアウトの「Show Scrollbar Map」チェック式トグル(ON=文書窓の縦スクロールバー脇に変更位置地図stripを表示。既定ON。実体 KESCMScrollMap.cpp の sScrollMapOn)
DECLARE_PMID(kActionIDSpace, kKESCMPopupSavePanelStateActionID, kKESCMPrefix + 22)	// パネルのフライアウトの「Save Panel Settings」(チェックではなく実行アクション)。現在の設定系トグルを独自JSONでローカルへ保存し保存先パスを表示。パネル初回オープン時に読込。実体 KESCMPanelState.cpp
DECLARE_PMID(kActionIDSpace, kKESCMPopupSep3ActionID, kKESCMPrefix + 23)	// フライアウト: Ignore Page Number Marker の下の区切り線(MenuDef のパス末尾 ":-"。ActionDef 不要)。この下に Hide Unchanged Spreads を置く
DECLARE_PMID(kActionIDSpace, kKESCMPageCheckToggleActionID, kKESCMPrefix + 24)	// ページパネルのページ右クリック(RtMenuPagesPanel)のトグル「KESCM: Check」(選択ページに✓印を付け外し。Start中限定・Stopで消去。チェック/有効無効は kCustomEnabling。実体 KESCMPageCheck.cpp、✓描画は KESCMDrawEventHandler の isThumb 分岐)
DECLARE_PMID(kActionIDSpace, kKESCMPopupSaveChecksActionID, kKESCMPrefix + 25)	// パネルのフライアウトの「Save Check & Register」(実行アクション)。Start中の Target/Source の現在の Check(✓)+ Register(Added/Removed)を独自JSON(KESCM\KESCMPageChecks.json, v2)へマージ保存し保存先パスを表示。実体 KESCMPageCheck.cpp
DECLARE_PMID(kActionIDSpace, kKESCMPopupLoadChecksActionID, kKESCMPrefix + 26)	// パネルのフライアウトの「Load Check & Register」(実行アクション)。Start中だけ有効。上記JSONから Register を両文書へ適用→再比較→Check(今もマーク付きのみ)を復元。実体 KESCMPageCheck.cpp
DECLARE_PMID(kActionIDSpace, kKESCMPopupPagesPanelShortcutActionID, kKESCMPrefix + 27)	// パネルのフライアウトの「Pages Panel Shortcut」チェック式トグル(ON=Ctrl+Alt+ミドルで InDesign 標準「ページ」パネルの表示/非表示を切替。既定ON。実体 KESCMPeek.cpp の sPagesPanelShortcutOn / トグル本体 KESCMPanelObserver.cpp の KESCMTogglePagesPanel)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 15)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 15)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 16)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 17)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 18)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 19)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 20)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 21)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 22)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 23)
// kKESCMPrefix + 24/25/26/27 は使用中(KESCM: Check / Save Check & Register / Load Check & Register / Pages Panel Shortcut)


// WidgetIDs:
DECLARE_PMID(kWidgetIDSpace, kKESCMPanelWidgetID, kKESCMPrefix + 0)
DECLARE_PMID(kWidgetIDSpace, kKESCMTargetTextWidgetID, kKESCMPrefix + 1)
DECLARE_PMID(kWidgetIDSpace, kKESCMSourceTextWidgetID, kKESCMPrefix + 26)
// kWidgetIDSpace +27 は現在空き(旧 kKESCMStartButtonWidgetID; 開始/解除を kKESCMToggleButtonWidgetID に統合)
// kWidgetIDSpace +28 は現在空き(旧 kKESCMClearButtonWidgetID; 同上)
// kWidgetIDSpace +29 は現在未使用(旧 kKESCMPrintCheckWidgetID; 印刷ON/OFF チェックボックスは 2026-07-10 に
//   フライアウト「Print comparison marks」メニュー項目=kKESCMPopupPrintMarksActionID へ移行しパネルから撤去)
// kWidgetIDSpace +30〜+32 は現在未使用(旧 kKESCMOpacityClusterWidgetID / kKESCMOpacity25RadioWidgetID /
//   kKESCMOpacity75RadioWidgetID; 不透明度 25%/75% ラジオは 2026-07-10 にフライアウト
//   kKESCMPopupOpacity25ActionID / kKESCMPopupOpacity75ActionID へ移行しパネルから撤去)
// kWidgetIDSpace +33 は現在空き(旧 kKESCMHintTextWidgetID; 説明文はパネルから撤去しフライアウト「使い方」へ移動)
DECLARE_PMID(kWidgetIDSpace, kKESCMIconOnWidgetID, kKESCMPrefix + 34)
DECLARE_PMID(kWidgetIDSpace, kKESCMIconOffWidgetID, kKESCMPrefix + 35)
DECLARE_PMID(kWidgetIDSpace, kKESCMStatusTextWidgetID, kKESCMPrefix + 36)
// kWidgetIDSpace +37 は現在未使用(旧 kKESCMToggleButtonWidgetID; 開始/解除ボタンは 2026-07-10 に
//   フライアウト「Start / Stop」メニュー項目=kKESCMPopupStartStopActionID へ移行しパネルから撤去)
DECLARE_PMID(kWidgetIDSpace, kKESCMPrevChangeButtonWidgetID, kKESCMPrefix + 38)	// 「◀ Prev」= 前の見るべきページへスクロール(KESCMChangeNav.cpp)
DECLARE_PMID(kWidgetIDSpace, kKESCMNextChangeButtonWidgetID, kKESCMPrefix + 39)	// 「Next ▶」= 次の見るべきページへスクロール(KESCMChangeNav.cpp)
DECLARE_PMID(kWidgetIDSpace, kKESCMScrollMapWidgetID, kKESCMPrefix + 40)	// スクロールバー地図strip(文書窓の縦スクロールバー左隣に実行時注入; KESCMScrollMap.cpp)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 2)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 3)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 4)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 5)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 6)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 7)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 8)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 9)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 10)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 11)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 12)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 13)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 14)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 15)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 16)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 17)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 18)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 19)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 20)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 21)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 22)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 23)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 24)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 25)

// スクリプト要素 ID(スクリプトAPIは全撤去済み=+1〜+12 はすべて空き。再利用時は旧用途との衝突に注意)
// kScriptInfoIDSpace +1 は現在空き(ページ単位 kescmMarkChanges は廃止; kescmMarkChangesDoc を使う)
// kScriptInfoIDSpace +2 は現在空き(旧 kKESCMClearMarksMethodScriptElement; スクリプトAPI撤去)
// kScriptInfoIDSpace +3 は現在空き(旧 kKESCMMarkChangesDocMethodScriptElement; 同上)
// kScriptInfoIDSpace +4 は現在空き(kescmShowPageX 廃止; 対角線のページ × は撤去)
// kScriptInfoIDSpace +5 は現在空き(kescmShowOverset 廃止); 再利用時は衝突に注意
// kScriptInfoIDSpace +6 は現在空き(kescmShowOriginal 廃止; ミドルボタン peek に統合)
// kScriptInfoIDSpace +7 は現在空き(kescmHideOriginal 廃止; kescmShowOriginal と対)
// kScriptInfoIDSpace +8 は現在空き(kescmShowOriginalUnderMouse 廃止; ミドルボタン peek を使う)
// kScriptInfoIDSpace +9 は現在空き(旧 kKESCMArmMousePeekMethodScriptElement; スクリプトAPI撤去)
// kScriptInfoIDSpace +10 は現在空き(旧 kKESCMDisarmMousePeekMethodScriptElement; 同上)
// kScriptInfoIDSpace +11 は現在空き(旧 kKESCMToastMethodScriptElement; kescmToast はスクリプトAPIごと撤去)
// kScriptInfoIDSpace +12 は現在空き(旧 kKESCMSetPrintMarksMethodScriptElement; スクリプトAPI撤去)

// "About Plug-ins" sub-menu:
#define kKESCMAboutMenuKey			kKESCMStringPrefix "kKESCMAboutMenuKey"
#define kKESCMAboutMenuPath		kSDKDefStandardAboutMenuPath kKESCMCompanyKey

// (旧 "Plug-ins" sub-menu 用の kKESCMPluginsMenuKey/Path は未使用のため撤去。パネルのメニュー配置は
//  下の kKESCMPanelPluginsMenuPath が担う)

// パネルを Plug-Ins メニューへ出すためのパスと位置。
// Plug-Ins ▸ KohakuNekotarou ▸ KohakuChangeMarker（リーフはパネル名キー）。
#define kKESCMPanelPluginsMenuPath		kSDKDefPlugInsStandardMenuPath kKESCMCompanyKey kSDKDefDelimitMenuPath kKESCMPanelTitleKey
#define kKESCMPanelPluginsMenuPosition	100.0	// 大きいほど下に並ぶ。

// Menu item keys:

// Other StringKeys:
#define kKESCMAboutBoxStringKey	kKESCMStringPrefix "kKESCMAboutBoxStringKey"
#define kKESCMRepoURL			"https://github.com/KohakuNekotarou/KohakuExtendScriptChangeMarker"	// 配布元URL。「このプラグインについて」本文とパネルのイラストクリックの飛び先で共通
#define kKESCMAboutScriptMenuKey	kKESCMStringPrefix "kKESCMAboutScriptMenuKey"	// パネルのフライアウト「スクリプトについて」のメニュー名
#define kKESCMScriptHelpStringKey	kKESCMStringPrefix "kKESCMScriptHelpStringKey"	// その本文(スクリプトAPIは撤去済み。現在は「利用可能なスクリプトはありません」の旨を表示)
#define kKESCMUsageMenuKey		kKESCMStringPrefix "kKESCMUsageMenuKey"	// パネルのフライアウト「使い方」のメニュー名(本文は kKESCMHintKey を再利用)
#define kKESCMHideUnchangedMenuKey	kKESCMStringPrefix "kKESCMHideUnchangedMenuKey"	// パネルのフライアウト「Hide Unchanged Spreads」トグルのメニュー名
#define kKESCMHideConfirmKey		kKESCMStringPrefix "kKESCMHideConfirmKey"	// その確認ダイアログ本文(ダイアログのみロケール連動: enUS=英語/jaJP=日本語)
#define kKESCMShowOldNumsMenuKey	kKESCMStringPrefix "kKESCMShowOldNumsMenuKey"	// パネルのフライアウト「Show Original Page Numbers」トグルのメニュー名
#define kKESCMSyncViewsMenuKey		kKESCMStringPrefix "kKESCMSyncViewsMenuKey"	// パネルのフライアウト「Sync Layout Views」トグルのメニュー名
#define kKESCMShowSrcMarksMenuKey	kKESCMStringPrefix "kKESCMShowSrcMarksMenuKey"	// パネルのフライアウト「Show Marks on Source」トグルのメニュー名
#define kKESCMPageMapToggleMenuKey	kKESCMStringPrefix "kKESCMPageMapToggleMenuKey"	// ページパネル右クリックのトグル「KESCM: Register as Added/Removed Pages」の既定メニュー名(表示時は UpdateActionStates が Target=Added/Source=Removed に動的差し替え)
#define kKESCMPageCheckMenuKey		kKESCMStringPrefix "kKESCMPageCheckMenuKey"	// ページパネル右クリックのトグル「KESCM: Check」のメニュー名
#define kKESCMIgnorePageNumMenuKey	kKESCMStringPrefix "kKESCMIgnorePageNumMenuKey"	// パネルのフライアウト「Ignore Page Number Marker」トグルのメニュー名
#define kKESCMHoldToHideMarksMenuKey	kKESCMStringPrefix "kKESCMHoldToHideMarksMenuKey"	// パネルのフライアウト「Hold to Hide Marks」トグルのメニュー名
#define kKESCMPanelShortcutMenuKey	kKESCMStringPrefix "kKESCMPanelShortcutMenuKey"	// パネルのフライアウト「Invoke Panel Shortcut」トグルのメニュー名
#define kKESCMPagesPanelShortcutMenuKey	kKESCMStringPrefix "kKESCMPagesPanelShortcutMenuKey"	// パネルのフライアウト「Pages Panel Shortcut」トグルのメニュー名
#define kKESCMScrollMapMenuKey		kKESCMStringPrefix "kKESCMScrollMapMenuKey"	// パネルのフライアウト「Show Scrollbar Map」トグルのメニュー名
#define kKESCMSavePanelStateMenuKey	kKESCMStringPrefix "kKESCMSavePanelStateMenuKey"	// パネルのフライアウト「Save Panel Settings」項目のメニュー名
#define kKESCMSaveChecksMenuKey		kKESCMStringPrefix "kKESCMSaveChecksMenuKey"	// パネルのフライアウト「Save Check & Register」項目のメニュー名
#define kKESCMLoadChecksMenuKey		kKESCMStringPrefix "kKESCMLoadChecksMenuKey"	// パネルのフライアウト「Load Check & Register」項目のメニュー名

// パネル: 内部フライアウト(ポップアップ)メニュー名＋そのメニューパス。
#define kKESCMInternalPopupMenuNameKey	kKESCMStringPrefix "kKESCMInternalPopupMenuNameKey"
#define kKESCMPopupMenuPath				kKESCMInternalPopupMenuNameKey

// フライアウトの「Marks opacity」サブメニュー(中に 25% / 75%)。名前は英語リテラル(KESCM のメニュー名は
// 全ロケール英語で統一)。子項目(25%/75%)はこの kKESCMOpacitySubmenuPath を親メニューパスとして指す。
// 親ノードは MenuDef で actionID 0・パス末尾に区切り(kSDKDefDelimitMenuPath)を付けて宣言する
// (Adobe 実例 open/components/buttonui FormFieldUIMenu.fr / incopyexportui と同じ流儀)。
#define kKESCMOpacitySubmenuName		"Marks opacity"
#define kKESCMOpacitySubmenuPath		kKESCMPopupMenuPath kSDKDefDelimitMenuPath kKESCMOpacitySubmenuName

// パネルの文字列キー(KESCM_enUS.fr / KESCM_jaJP.fr でローカライズ)。
#define kKESCMPanelTitleKey		kKESCMStringPrefix "kKESCMPanelTitleKey"
#define kKESCMTargetLabelKey	kKESCMStringPrefix "kKESCMTargetLabelKey"	// パネルの "Target:" ラベル。リテラルだとシステム訳と衝突するため自前キーで持つ
#define kKESCMSourceLabelKey	kKESCMStringPrefix "kKESCMSourceLabelKey"	// パネルの "Source:" ラベル。リテラル "Source:" は日本語ロケールで「スタイルソース :」に化けるため自前キーで持つ
#define kKESCMStartButtonKey	kKESCMStringPrefix "kKESCMStartButtonKey"	// フライアウト「Start / Stop」項目の既定メニュー名(未開始=Start)。表示時は UpdateActionStates が arm 状態で Start↔Stop に動的差し替え(旧トグルボタンのキャプションキーを流用)
#define kKESCMPrintCheckKey		kKESCMStringPrefix "kKESCMPrintCheckKey"	// フライアウト「Print comparison marks」トグルのメニュー名(旧パネルチェックボックスのキャプションキーを流用)
#define kKESCMOpacity25Key		kKESCMStringPrefix "kKESCMOpacity25Key"	// サブメニュー「Marks opacity」内の子項目名(="25%")
#define kKESCMOpacity75Key		kKESCMStringPrefix "kKESCMOpacity75Key"	// サブメニュー「Marks opacity」内の子項目名(="75%")
#define kKESCMPrevChangeKey		kKESCMStringPrefix "kKESCMPrevChangeKey"	// パネルの「◀ Prev」ボタンのキャプション(英語固定)
#define kKESCMNextChangeKey		kKESCMStringPrefix "kKESCMNextChangeKey"	// パネルの「Next ▶」ボタンのキャプション(英語固定)
#define kKESCMHintKey			kKESCMStringPrefix "kKESCMHintKey"

// PNG アイコンリソース(プラグインに埋め込み; .pln とは別ファイルでは出荷しない)。
#define kKESCMIconOnResID	1001
#define kKESCMIconOffResID	1002
#define kKESCMPaletteIconResID	1003	// パネルが折りたたまれた時に出る小さいドックタブアイコン

// スクロールバー地図stripのビューリソースID(kViewRsrcType; ::CreateObject で実行時生成する。KESCMScrollMap.cpp)
#define kKESCMScrollMapRsrcID	1010

// Menu item positions (flyout order): … Show Marks on Source(9.4) → Show Original Page Numbers(9.7) →
// Sync Layout Views(9.8) → Show Scrollbar Map(9.85) → Ignore Page Number Marker(9.9) →
// Invoke Panel Shortcut(9.901) → ─線 Sep3(9.905) → Hide Unchanged Spreads(9.91) → Save Panel Settings(9.93) →
// Save Check & Register(9.935) → Load Check & Register(9.94) → ─線 Sep2(9.95) → How to Use(10) → About Scripting(11) → About this plug-in(12)。
// ※メニュー名は日本語ロケールでも英語で統一(2026-07-04)。Split Target on Start(旧9.0)は撤去済み。
// ※2026-07-11: Sep3 を足し Hide Unchanged Spreads(旧9.5)をその下へ移動。Invoke Panel Shortcut を Ignore のすぐ下(線の上)へ。
#define kKESCMStartStopMenuItemPosition		9.0	// 「Start / Stop」(比較開始/解除)をフライアウト先頭に。名前は arm 状態で動的に Start↔Stop
#define kKESCMSep1MenuItemPosition			9.1	// Start の下の区切り線(パス末尾 ":-")
#define kKESCMPrintMarksMenuItemPosition	9.2	// チェック式トグル「Print comparison marks」を Start/Stop の直後に(旧パネルチェックボックスのメニュー化)
#define kKESCMHoldToHideMarksMenuItemPosition	9.35	// チェック式トグル「Hold to Hide Marks」を「Marks opacity」サブメニューの直後に(枠表示の極性反転)
#define kKESCMOpacitySubmenuMenuItemPosition	9.3	// 「Marks opacity」サブメニュー(中に 25% / 75%)
#define kKESCMOpacity25SubMenuItemPosition	1.0	// サブメニュー「Marks opacity」内: 25%(選択中に✓)
#define kKESCMOpacity75SubMenuItemPosition	2.0	// サブメニュー「Marks opacity」内: 75%(25% と相互排他)
#define kKESCMShowSrcMarksMenuItemPosition	9.4	// チェック式トグル「Show Marks on Source」を先頭に
#define kKESCMShowOldNumsMenuItemPosition	9.7	// チェック式トグル「Show Original Page Numbers」をその直後に
#define kKESCMSyncViewsMenuItemPosition		9.8	// チェック式トグル「Sync Layout Views」をさらにその直後に
#define kKESCMScrollMapMenuItemPosition		9.85	// チェック式トグル「Show Scrollbar Map」を Sync Layout Views の直後・Ignore Page Number Marker の前に
#define kKESCMIgnorePageNumMenuItemPosition	9.9	// チェック式トグル「Ignore Page Number Marker」をさらにその直後に
#define kKESCMPanelShortcutMenuItemPosition	9.901	// チェック式トグル「Invoke Panel Shortcut」(Shift+Ctrl+ミドルでパネル呼び出し)を Ignore Page Number Marker のすぐ下(Sep3 の線より上)に(2026-07-11)
#define kKESCMPagesPanelShortcutMenuItemPosition	9.902	// チェック式トグル「Pages Panel Shortcut」(Ctrl+Alt+ミドルでページパネル表示/非表示)を Invoke Panel Shortcut(9.901)の直下に(2026-07-12)
#define kKESCMSep3MenuItemPosition			9.905	// Invoke Panel Shortcut の下の区切り線(パス末尾 ":-")。この下に Hide Unchanged Spreads を置く
#define kKESCMHideUnchangedMenuItemPosition	9.91	// チェック式トグル「Hide Unchanged Spreads」を Sep3(線)の直後へ移動(2026-07-11)
#define kKESCMSavePanelStateMenuItemPosition	9.93	// 実行アクション「Save Panel Settings」を設定系トグル群の末尾(Invoke Panel Shortcut の直後・Sep2 の前)に
#define kKESCMSaveChecksMenuItemPosition	9.935	// 実行アクション「Save Check & Register」を Save Panel Settings の直下に(2026-07-11)
#define kKESCMLoadChecksMenuItemPosition	9.94	// 実行アクション「Load Check & Register」を Save Check & Register の直下に(2026-07-11)
#define kKESCMSep2MenuItemPosition			9.95	// How to Use の上の区切り線(パス末尾 ":-")
#define kKESCMUsageMenuItemPosition			10.0	// 「使い方」
// ページパネルのページ右クリックメニュー(内部名 RtMenuPagesPanel、2026-07-05 実機確定)内の位置。
// 本家項目の後ろ(末尾)に付ける。内部名は非翻訳キーなので全ロケール共通で効く。
#define kKESCMPageMapToggleMenuItemPosition	3000.0
#define kKESCMPageCheckMenuItemPosition		3001.0	// 「KESCM: Check」を Register の直後(ページパネル右クリック末尾)に
#define kKESCMAboutScriptMenuItemPosition	11.0	// その下に「スクリプトについて」
#define kKESCMAboutThisMenuItemPosition		12.0	// 末尾に「このプラグインについて」


// Initial data format version numbers
#define kKESCMFirstMajorFormatNumber  RezLong(1)
#define kKESCMFirstMinorFormatNumber  RezLong(0)

// Data format version numbers for the PluginVersion resource 
#define kKESCMCurrentMajorFormatNumber kKESCMFirstMajorFormatNumber
#define kKESCMCurrentMinorFormatNumber kKESCMFirstMinorFormatNumber

#endif // __KESCMID_h__
