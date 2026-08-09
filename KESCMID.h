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
#define kKESCMPluginName	"KohakuExtendScriptChangeMarker"			// Name of this plug-in. 内部名(ID系・.rc の InternalName)。互換のため据え置き。
#define kKESCMDisplayName	"Kohaku Change Marker"			// 表示名(About メニュー項目・About ボックス本文・パネル/ツール名)。KBS の "Kohaku Search Panel" に合わせ、単語間をスペースで区切る(2026-07-25)。
#define kKESCMFileName		"KohakuChangeMarker"			// 出力ファイル名の基底(.rc の OriginalFilename)。vcxproj の TargetName と一致させること。表示名と違いスペースは入れない。
#define kKESCMPrefixNumber	0x205515 		// Unique prefix number for this plug-in(*Must* be obtained from Adobe Developer Support).
#define kKESCMVersion		"1.3.1"						// Version of this plug-in。About ボックス本文・.rc の FileVersion・PluginVersion リソースの3か所に出る。1.0.1 → 1.1.0(2026-07-25) → 1.1.1(2026-07-26) → 1.2.0(2026-07-30) → 1.2.1(2026-08-06) → 1.3.0(2026-08-07) → 1.3.1(2026-08-07)。
														// ★Adobe Exchange の公開版は **1.3.0**(2026-08-07 承認・公開)。1.2.1 は提出しないまま 1.3.0 へ繰り上げた(機能追加が入ったので patch では足りない)。
														// ★★**「版数が◯◯だった時期にコードへ入れた」ことと「提出した◯◯のビルドに入っている」ことは別物**。取り違えると提出説明を誤る(2026-08-07 に実際に踏んだ)ので、増分は**提出したビルドを境に**2段へ分けてある。★提出文を起こすときは【次に提出する分】だけを読むこと。
														//
														// ■■【1.3.1 = 次に提出する分】★公開版 1.3.0 から見た増分。**提出説明はここだけを使う**。
														//   ①パネルにツール切替ボタン(kKESCMToolButtonWidgetID) = 押すとツールボックスの琥珀のツールがアクティブになる。絵はツールボックスと同じリソースを参照。★押下表示はツールボックスと双方向に同期する(状態を書くのは ITool::Select/Deselect の1か所だけなので、どちらから選んでも食い違わない)。How to Use の冒頭も「ツールボックス、またはパネルのツールボタン」へ追随済み。
														//   ②半透明パネルの「不透明に戻す」判定を変更 = カーソルがパネルの矩形の中にある限り、その上にフライアウト・子メニュー・ツールチップが出ていても不透明のまま。公開版 1.3.0 は自分の窓が上に出ると薄くなった(KBS と同じ判定へ揃えたもの)。
														//   ■1.3.1 で撤去したもの: 「Translucent Toolbox」トグル(フローティング中の**ツールボックス**を半透明にする)。★★**提出説明に「機能を削除した」と書かないこと** ＝ **提出した 1.3.0 のビルドに最初から入っていない**(2026-08-07 ユーザー明言)ので、公開版から見れば存在しなかった機能。ActionID +38 は欠番のまま再利用しない。
														//   ⚠**①②とも「版数が 1.3.0 だった時期にコードへ入れた」もの**だが、提出した 1.3.0 のビルド(commit 5ff22c5 時点)には入っていない。**版数コメントが載っている位置で「提出済みか」を判断しない**。
														//
														// ■■【1.3.0 = 公開済み】公開版 1.2.0 から見た増分＝**1.3.0 の提出説明に使用済み。次の提出では繰り返さない**。
														// ■機能追加(minor を上げた理由):
														//   ①app.kcmStatus = パネルのステータス行の最後の1行をスクリプト/COM から読む読み取り専用プロパティ(KESCMScriptProvider.cpp)。パネルを閉じていても答える。実機検証の自動化用。
														//   ②Translucent Pages Panel = **本体のページパネル**もホバーで不透明に戻る半透明にできる(フライアウト。従来は自分のパネルだけ)。窓の特定を WidgetID 狙い撃ちへ全面入れ替えたことで可能になった(本体パネルの窓タイトルは UI 言語で変わるため)。
														//   ③ツールに ScriptID を与えた = app.toolBoxTools.currentTool から KESCM ツールを読み書きできる(UITools.KOHAKU_CHANGE_MARKER_TOOL)。従来は en_None で「何も選ばれていない」と区別できなかった。
														// ■機能変更(1.3.0 の提出説明で記載済み): 押下中 HUD を作り直した。公開版 1.2.0 は sprite 層で**相手の文書名**を出す作りで、押下を抜けた後に one-shot タイマーで描くため枠より遅れて出ていた。新版は比較マークの枠と同じ Draw Event で描くので**枠と同時に出る**。出す内容も文書名ではなく**押した窓の役割**(Target / Source / Not in comparison / Not comparing)に変えた(2026-07-27 ユーザー指示「相手の文書名は出さない」)。位置・見た目は旧版と同じ(ビュー左上・文字20px・不透明度0.6)。フライアウトの「Show HUD」トグルと設定キー hudOn は廃止＝**常に出る**。★2026-08-06 に一度全廃し、2026-08-07 に作り直した経緯があるので、「削除した機能」として説明しないこと。
														// ■表示・操作の整理: About を「名前 版数」1行に(英語のみ)／日本語で出すのは How to Use と Hide Unchanged の確認アラートの2箇所だけに整理(メニュー・パネル・ステータス行は全ロケール英語)／パネル幅 +10px／Hide Unchanged と Start(文書2つ未満)と Find Overset(走査対象なし)を条件付きで灰色化／ページパネル右クリックのメニュー接頭辞を KESCM:→KCM: に短縮／Prev/Next のラベル整理。
														// ■不具合修正・内部改善: ジャンプ前にスプレッド切替(マスターページへ飛べるように)＋マスターページのあふれを巡回一覧に載せる／Find Overset で押し出された表のセルを報告しない／あふれを聞く前にリコンポーズ／表の列挙を ITextStoryThreadDictHier へ(入れ子の表が入る)／描画エンジンの見直し(greek 無効・除外領域の緑は画面限定・除外矩形のキャッシュ化)／LocaleIndex に k_Wild 追補(列挙外 UI 言語での生キー表示を防ぐ)／比較失敗ページの可視化(failed=N・Refresh で既存枠を消さない)／半透明の細部(はみ出しメニュー・AutoAttach の OFF ガード・Shutdown 後の再武装禁止)。
														// ■全14ブロックの API 監査(2026-08-05〜08-07)とバグ特化の全コード再点検(08-06)を実施済み。全文=docs/ai-notes/kescm-file-map.md の各ブロックノート／kescm-bug-recheck-2026-08-06.md。
// (kKESCMAuthor はテンプレート残骸(どこからも未参照)のため削除 2026-07-25)

// Plug-in Prefix: (please change kKESCMPrefixNumber above to modify the prefix.)
#define kKESCMPrefix		RezLong(kKESCMPrefixNumber)				// The unique numeric prefix for all object model IDs for this plug-in.
#define kKESCMStringPrefix	SDK_DEF_STRINGIZE(kKESCMPrefixNumber)	// The string equivalent of the unique prefix number for  this plug-in.

// Missing plug-in: (see ExtraPluginInfo resource)
#define kKESCMMissingPluginURLValue		kSDKDefPartnersStandardValue_enUS // URL displayed in Missing Plug-in dialog
#define kKESCMMissingPluginAlertValue	kSDKDefMissingPluginAlertValue // Message displayed in Missing Plug-in dialog - provide a string that instructs user how to solve their missing plug-in problem

// PluginID:
DECLARE_PMID(kPlugInIDSpace, kKESCMPluginID, kKESCMPrefix + 0)

// ClassIDs:
DECLARE_PMID(kClassIDSpace, kKESCMScriptProviderBoss, kKESCMPrefix + 3)	// app.kcmStatus を返す ScriptProvider(2026-08-06 復活。旧スクリプトAPI(kescmToast 等)は撤去済みで、公開するのは読み取り専用の1プロパティだけ)
DECLARE_PMID(kClassIDSpace, kKESCMDrawEventServiceBoss, kKESCMPrefix + 4)
// kKESCMPeekWatcherBoss (kKESCMPrefix + 5) は中ボタンウォッチャ撤去(2026-07-13)により廃止。スロットは予約のまま。
DECLARE_PMID(kClassIDSpace, kKESCMPeekStartupBoss, kKESCMPrefix + 6)	// IStartupShutdown: アプリ起動時に peek ウォッチャを開始
DECLARE_PMID(kClassIDSpace, kKESCMThumbIdleTaskBoss, kKESCMPrefix + 7)	// IIdleTask: クローズ後の Pages サムネイル再生成を次のidleに遅延(旧 kKESCMToastIdleTaskBoss のスロット転用)
DECLARE_PMID(kClassIDSpace, kKESCMPanelWidgetBoss, kKESCMPrefix + 8)	// ChangeMarker 操作パネル(パレット)
DECLARE_PMID(kClassIDSpace, kKESCMActionComponentBoss, kKESCMPrefix + 9)	// About メニューのアクションコンポーネント
DECLARE_PMID(kClassIDSpace, kKESCMDocResponderServiceBoss, kKESCMPrefix + 10)	// IK2ServiceProvider+IResponder: ドキュメントクローズ監視(閉じた文書の追跡状態を確定クリーンアップ)
DECLARE_PMID(kClassIDSpace, kKESCMIconWidgetBoss, kKESCMPrefix + 11)	// kRollOverIconButtonBoss を継承し IID_ITIP を追加(パネルイラストのツールチップ)
DECLARE_PMID(kClassIDSpace, kKESCMScrollMapWidgetBoss, kKESCMPrefix + 12)	// kGenericPanelWidgetBoss+自前IControlView: 縦スクロールバー脇の枠ページ地図strip(旧 kKESCMLayoutSyncObserverBoss のスロット転用)
DECLARE_PMID(kClassIDSpace, kKESCMToolBoss, kKESCMPrefix + 13)	// kGenericToolBoss継承: ツールボックスの peek 専用ツール(KESCMTool.cpp)
DECLARE_PMID(kClassIDSpace, kKESCMTrackerBoss, kKESCMPrefix + 14)	// ツールのキャプチャ型トラッカー(IID_ITRACKER+IID_IEVENTHANDLER)。左ボタン hold 中だけ reveal。KESCMTracker.cpp
DECLARE_PMID(kClassIDSpace, kKESCMTrackerRegisterBoss, kKESCMPrefix + 15)	// トラッカー登録(kLayoutWidgetBoss×ツール→トラッカー)。KESCMTrackerRegister.cpp
// (unused-slot placeholders below start at +16; +6..+15 are declared above. 2026-08-05 audit)
DECLARE_PMID(kClassIDSpace, kKESCMStorySectionToggleBoss, kKESCMPrefix + 16)	// kRollOverIconButtonBoss継承+IID_IOBSERVER: パネル下部「Story Edits」セクションの開閉ボタン(三角)。絵は本体の kTreeBranchCollapsed/Expanded を借りる
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
DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMDOCSCLOSEDOBSERVER, kKESCMPrefix + 1)	// 一括クローズ完了(kPendingDocumentsClosedMsg)を受けるオブザーバのアタッチ識別ID
DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMPANELVISIBILITYOBSERVER, kKESCMPrefix + 2)	// パネルの表示状態変化(kPaletteVisibilityChangedMessage)を受けるオブザーバのアタッチ識別ID。半透明トグルをドッキング切り替え/開き直しに追随させるために使う
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
DECLARE_PMID(kImplementationIDSpace, kKESCMScriptProviderImpl, kKESCMPrefix + 0)	// CScriptProvider 実装(app.kcmStatus を返す。KESCMScriptProvider.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMDrawEventSrvcImpl, kKESCMPrefix + 1)
DECLARE_PMID(kImplementationIDSpace, kKESCMDrawEventHandlerImpl, kKESCMPrefix + 2)
// kKESCMPeekWatcherImpl (kKESCMPrefix + 3) は中ボタンウォッチャ撤去(2026-07-13)により廃止。スロットは予約のまま。
DECLARE_PMID(kImplementationIDSpace, kKESCMPeekStartupImpl, kKESCMPrefix + 4)	// IStartupShutdown 実装(peek ウォッチャを開始)
DECLARE_PMID(kImplementationIDSpace, kKESCMThumbIdleTaskImpl, kKESCMPrefix + 5)	// IIdleTask 実装(クローズ後の Pages サムネイル再生成を遅延実行)
DECLARE_PMID(kImplementationIDSpace, kKESCMPanelObserverImpl, kKESCMPrefix + 6)	// IObserver 実装(パネルのウィジェットオブザーバ)
DECLARE_PMID(kImplementationIDSpace, kKESCMActionComponentImpl, kKESCMPrefix + 7)	// IActionComponent 実装(About)
// kKESCMDocServiceProviderImpl (kKESCMPrefix + 8) は自前 ServiceProvider の撤去(2026-08-06)により廃止。
// 1シグナルだけの responder は API 提供の kAfterCloseDocSignalRespServiceImpl(DocumentID.h)を .fr で
// 名指しすれば登録される(KESCM.fr の kKESCMDocResponderServiceBoss 参照)。スロットは予約のまま。
DECLARE_PMID(kImplementationIDSpace, kKESCMDocResponderImpl, kKESCMPrefix + 9)	// IResponder 実装(クローズ確定時の追跡状態クリーンアップ)
DECLARE_PMID(kImplementationIDSpace, kKESCMIconTipImpl, kKESCMPrefix + 10)	// ITip 実装(パネルイラストにURLをツールチップ表示)
DECLARE_PMID(kImplementationIDSpace, kKESCMLayoutSyncObserverImpl, kKESCMPrefix + 11)	// IObserver 実装(レイアウトビュー同期)
DECLARE_PMID(kImplementationIDSpace, kKESCMScrollMapViewImpl, kKESCMPrefix + 12)	// IControlView 実装(スクロールバー地図stripの自前描画; KESCMScrollMap.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMToolImpl, kKESCMPrefix + 13)	// ITool 実装(KESCMTool.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMTrackerImpl, kKESCMPrefix + 14)	// ITracker 実装(CTracker派生; KESCMTracker.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMTrackerRegisterImpl, kKESCMPrefix + 15)	// ITrackerRegister 実装(KESCMTrackerRegister.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMTrackerEHImpl, kKESCMPrefix + 16)	// IEventHandler 実装(CTrackerEventHandler派生; 押下中のボタン解放を EndTracking へ転送。KESCMTracker.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMCursorProviderImpl, kKESCMPrefix + 17)	// ICursorProvider 実装(CToolCursorProvider派生; ツール選択中は常時✓カーソル。KESCMCursorProvider.cpp)
// (+6..+17 are all declared above - stale placeholders for them removed 2026-08-05 audit)
// (+18 = kKESCMSpriteImpl は 2026-08-06 に押下中 HUD ごと撤去。★**この番号は再利用しない**。
//  トラッカー boss の IID_ISPRITE は SDK 標準実装 kNoHandleSpriteImpl に戻してある=公式サンプル
//  wavetool の boss 構成(WavTl.fr:151,155)と同じ形)
DECLARE_PMID(kImplementationIDSpace, kKESCMDocsClosedObserverImpl, kKESCMPrefix + 19)	// IObserver 実装(一括クローズ完了で、保留した後片付けを1回だけ流す。KESCMPeek.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMPanelVisibilityObserverImpl, kKESCMPrefix + 20)	// IObserver 実装(パネルの表示状態が変わったら半透明を貼り直す。KESCMPanelAlpha.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMPanelRollOverImpl, kKESCMPrefix + 21)	// IMouseRollOver 実装(パネルにカーソルが乗っている間だけ半透明を解除。KESCMPanelAlpha.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMStorySectionToggleObserverImpl, kKESCMPrefix + 22)	// IObserver 実装(開閉ボタンの押下を受けて Story Edits セクションを開閉。KESCMStorySectionObserver.cpp)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 23)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 24)
//DECLARE_PMID(kImplementationIDSpace, kKESCMImpl, kKESCMPrefix + 25)


// ActionIDs:
DECLARE_PMID(kActionIDSpace, kKESCMAboutActionID, kKESCMPrefix + 0)
DECLARE_PMID(kActionIDSpace, kKESCMPanelWidgetActionID, kKESCMPrefix + 1)	// パネルの表示/非表示(ウィンドウメニュー)
DECLARE_PMID(kActionIDSpace, kKESCMPopupAboutThisActionID, kKESCMPrefix + 2)	// パネルのフライアウトの「このプラグインについて」
DECLARE_PMID(kActionIDSpace, kKESCMPopupAboutScriptActionID, kKESCMPrefix + 3)	// (撤去・予約)旧「About Scripting」フライアウト項目。2026-07-25 削除(ユーザー指定)。ID スロットは予約のまま
DECLARE_PMID(kActionIDSpace, kKESCMPopupUsageActionID, kKESCMPrefix + 4)	// パネルのフライアウトの「使い方」
// kActionIDSpace +5 は現在空き(旧 kKESCMPopupTestSplitActionID; Split Test 検証メニューは撤去済み)
// kActionIDSpace +6 は現在空き(旧 kKESCMPopupSplitTargetActionID; Split Target on Start は 2026-07-04 撤去。
//   仕組みは docs/ai-notes/kescm-split-target-mechanism.md に保存)
DECLARE_PMID(kActionIDSpace, kKESCMPopupHideUnchangedActionID, kKESCMPrefix + 7)	// パネルのフライアウトの「Hide Unchanged Spreads」チェック式トグル(ON=変更なしスプレッドを隠す)
DECLARE_PMID(kActionIDSpace, kKESCMPopupShowOldNumsActionID, kKESCMPrefix + 8)	// パネルのフライアウトの「Show Original Page Numbers」チェック式トグル(枠表示中/印刷ON時に隠す前の元番号バッジ)
DECLARE_PMID(kActionIDSpace, kKESCMPopupSyncViewsActionID, kKESCMPrefix + 9)	// パネルのフライアウトの「Sync Layout Views」チェック式トグル(他文書のビューへ座標+拡大率を自動同期)
DECLARE_PMID(kActionIDSpace, kKESCMPopupShowSrcMarksActionID, kKESCMPrefix + 10)	// パネルのフライアウトの「Show Marks on Source」チェック式トグル(Source側にも枠を常時表示。OPPでも表示・印刷にも出す。Startで既定ON)
DECLARE_PMID(kActionIDSpace, kKESCMPageMapToggleActionID, kKESCMPrefix + 11)	// ページパネルのページ右クリック(RtMenuPagesPanel)のトグル「KCM: Register as Added/Removed Pages」(選択ページを「比較相手なし」として登録/解除。チェック/動的ラベルは kCustomEnabling。KESCMPageMap.cpp)
DECLARE_PMID(kActionIDSpace, kKESCMPopupIgnorePageNumActionID, kKESCMPrefix + 12)	// パネルのフライアウトの「Ignore Page Number Marker」チェック式トグル(ON=ノンブル(自動ページ番号)マーカーを含むフレームを比較から除外。★既定OFF=sIgnorePageNumberMarker の初期値。KESCMPageNumberMarker.cpp)
DECLARE_PMID(kActionIDSpace, kKESCMPopupStartStopActionID, kKESCMPrefix + 13)	// パネルのフライアウト先頭の「Start / Stop」(比較の開始/解除。旧トグルボタンをメニュー化。arm 状態で名前が Start↔Stop に動的変化=kCustomEnabling+SetNthActionName。KESCMPanelObserver.cpp の KESCMToggleStartStop)
DECLARE_PMID(kActionIDSpace, kKESCMPopupPrintMarksActionID, kKESCMPrefix + 14)	// パネルのフライアウトの「Print comparison marks」チェック式トグル(旧パネルのチェックボックスをメニュー化。ON=マークを印刷し画面にも常時表示。KESCMPanelObserver.cpp の KESCMTogglePrintMarks)
DECLARE_PMID(kActionIDSpace, kKESCMPopupOpacity25ActionID, kKESCMPrefix + 15)	// パネルのフライアウトの「Marks opacity 25%」(旧パネルの opacity ラジオをメニュー化。75% と相互排他=選択中の方に✓。KESCMPanelObserver.cpp の KESCMSetMarkOpacity25)
DECLARE_PMID(kActionIDSpace, kKESCMPopupOpacity75ActionID, kKESCMPrefix + 16)	// パネルのフライアウトの「Marks opacity 75%」(25% と相互排他)
DECLARE_PMID(kActionIDSpace, kKESCMPopupSep1ActionID, kKESCMPrefix + 17)	// フライアウト: Start の下の区切り線(MenuDef のパス末尾 ":-"。ActionDef 不要・DoAction 不要=一意なIDだけ要る)
DECLARE_PMID(kActionIDSpace, kKESCMPopupSep2ActionID, kKESCMPrefix + 18)	// フライアウト: How to Use の上の区切り線
DECLARE_PMID(kActionIDSpace, kKESCMPopupHoldToHideMarksActionID, kKESCMPrefix + 19)	// パネルのフライアウトの「Hold to Hide Marks」チェック式トグル(ON=枠を画面に常時表示し、ツール左hold中だけ隠す=極性反転。画面のみ・印刷は Print comparison marks が別管理。KESCMActionComponent.cpp)
// kKESCMPopupPanelShortcutActionID (kKESCMPrefix + 20) は中ボタン撤去(2026-07-13)に伴い廃止。スロットを 2026-07-24 に再利用:
DECLARE_PMID(kActionIDSpace, kKESCMPopupAlignViewsActionID, kKESCMPrefix + 20)	// パネルのフライアウトの「Align Other Views to Active」(実行アクション)。アクティブ(最前面)文書のビューの位置+拡大率を他文書のビューへ1回そろえる。Start中はページのAdd/Remove補正あり。ショートカット割当可(kKESCMPanelMenuActionArea+VisibleInKBSC)。実体 KESCMPeek.cpp の KESCMAlignOtherViewsToActiveNow
DECLARE_PMID(kActionIDSpace, kKESCMPopupScrollMapActionID, kKESCMPrefix + 21)	// パネルのフライアウトの「Show Scrollbar Map」チェック式トグル(ON=文書窓の縦スクロールバー脇に変更位置地図stripを表示。既定ON。実体 KESCMScrollMap.cpp の sScrollMapOn)
DECLARE_PMID(kActionIDSpace, kKESCMPopupSavePanelStateActionID, kKESCMPrefix + 22)	// パネルのフライアウトの「Save Panel Settings」(チェックではなく実行アクション)。現在の設定系トグルを独自JSONでローカルへ保存し保存先パスを表示。読込は起動時(KESCMPeekStartup::Startup。2026-07-15 前倒し)。実体 KESCMPanelState.cpp
DECLARE_PMID(kActionIDSpace, kKESCMPopupSep3ActionID, kKESCMPrefix + 23)	// フライアウト: Refresh Overset の下(9.50)の区切り線(MenuDef のパス末尾 ":-"。ActionDef 不要)。現配置は下の位置一覧(9.50)が正(2026-07-25 コメント現行化)
DECLARE_PMID(kActionIDSpace, kKESCMPageCheckToggleActionID, kKESCMPrefix + 24)	// ページパネルのページ右クリック(RtMenuPagesPanel)のトグル「KCM: Check」(選択ページに✓印を付け外し。Start中限定・Stopで消去。チェック/有効無効は kCustomEnabling。実体 KESCMPageCheck.cpp、✓描画は KESCMDrawEventHandler の isThumb 分岐)
DECLARE_PMID(kActionIDSpace, kKESCMPopupSaveChecksActionID, kKESCMPrefix + 25)	// パネルのフライアウトの「Save Check & Register」(実行アクション)。Start中の Target/Source の現在の Check(✓)+ Register(Added/Removed)を独自JSON(KESCM\KESCMPageChecks.json, v2)へマージ保存し保存先パスを表示。実体 KESCMPageCheck.cpp
DECLARE_PMID(kActionIDSpace, kKESCMPopupLoadChecksActionID, kKESCMPrefix + 26)	// パネルのフライアウトの「Load Check & Register」(実行アクション)。Start中だけ有効。上記JSONから Register を両文書へ適用→再比較→Check(今もマーク付きのみ)を復元。実体 KESCMPageCheck.cpp
// kKESCMPopupPagesPanelShortcutActionID (kKESCMPrefix + 27) は中ボタン撤去(2026-07-13)に伴い「Invoke Pages Panel Shortcut」トグルごと廃止。スロットは予約のまま。
DECLARE_PMID(kActionIDSpace, kKESCMPageMapSepActionID, kKESCMPrefix + 28)	// ページパネルのページ右クリック(RtMenuPagesPanel): KESCM 追加項目(Register / Check)の上の区切り線(MenuDef のパス末尾 ":-"。ActionDef 不要・DoAction 不要=一意なIDだけ要る)。本家メニューと視覚的に分けるため
DECLARE_PMID(kActionIDSpace, kKESCMToolActionID, kKESCMPrefix + 29)	// ツールボックスのツール選択ショートカット用の ActionID(ToolDef が参照。ActionDef 不要=ツール枠が自動生成)
DECLARE_PMID(kActionIDSpace, kKESCMPageRefreshCompareActionID, kKESCMPrefix + 30)	// ページパネルのページ右クリック(RtMenuPagesPanel)の実行アクション「KCM: Refresh Page Comparison」(選択ページの比較を再検出して枠/サムネイルを更新。旧 Ctrl+ミドルのスプレッド再比較を移設。Start中限定・kCustomEnabling。実体 KESCMPeek.cpp の KESCMRefreshComparisonForSelectedPages)
DECLARE_PMID(kActionIDSpace, kKESCMPopupFindOversetActionID, kKESCMPrefix + 31)	// パネルのフライアウトの「Find Overset」チェック式トグル(ON=アクティブ文書を走査し overset のあるページに大きな十字を表示。比較と独立・単独点検。kCustomEnabling。実体 KESCMActionComponent.cpp/KESCMOversetScan.cpp)
DECLARE_PMID(kActionIDSpace, kKESCMPopupRefreshOversetActionID, kKESCMPrefix + 32)	// パネルのフライアウトの「Refresh Overset」(実行アクション)。Find Overset が ON のときだけ有効(OFF時は灰色)=アクティブ文書を再走査して十字を貼り直す。kCustomEnabling
DECLARE_PMID(kActionIDSpace, kKESCMPopupOversetSepActionID, kKESCMPrefix + 33)	// フライアウト: Find Overset 群の上の区切り線(MenuDef のパス末尾 ":-"。ActionDef 不要・DoAction 不要=一意なIDだけ要る)
DECLARE_PMID(kActionIDSpace, kKESCMPopupExportChangedPagesActionID, kKESCMPrefix + 34)	// パネルのフライアウトの「Export Changed Pages...」(実行アクション)。比較中(sDB≠nil)のみ有効=現在の比較の変更ページ一覧をTSV(新/旧/種別)で保存。実体 KESCMChangedPagesTSV.cpp
// (+35 = kKESCMPopupHudActionID「Show HUD」は 2026-08-06 に機能ごと撤去。★**この番号は再利用しない**
//  ＝ショートカット割当は .indk に ActionID の数値で保存されるので、別機能に振り直すと古い割当が
//  その機能を叩いてしまう)
DECLARE_PMID(kActionIDSpace, kKESCMPopupTranslucentPanelActionID, kKESCMPrefix + 36)	// パネルのフライアウトの「Translucent Panel」チェック式トグル(ON=フローティング中のこのパネルを半透明にする。★Windows 専用・★ドッキング中は選べるが効かない(フラグだけ立ちフローティングに戻すと効く)。既定 OFF。実体 KESCMPanelAlpha.cpp)
DECLARE_PMID(kActionIDSpace, kKESCMPopupTranslucentPagesActionID, kKESCMPrefix + 37)	// パネルのフライアウトの「Translucent Pages Panel」チェック式トグル(ON=フローティング中の**本体のページパネル**を半透明にする。上の +36 と同じ仕組みで対象だけが違う=WidgetID(kPagesPanelWidgetID)で狙い撃ちする。★Windows 専用・★ドッキング中は選べるが効かない。既定 OFF。実体 KESCMPanelAlpha.cpp)
// ★kKESCMPrefix + 38 は「Translucent Toolbox」トグルの跡地(2026-08-07 に追加し、同日ユーザー判断で撤去)。
//   **番号は再利用しない** = ショートカット設定(.indk)はアクションを数値の ActionID で保存するので、
//   割り当て済みの番号を別機能へ回すと、そのショートカットが無関係な機能を叩く。押下中 HUD を撤去した
//   ときの +35 とまったく同じ扱い。
// (+15..+23 are all declared above - stale placeholders for them removed 2026-08-05 audit. Next free: +39)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 39)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 40)
// kKESCMPrefix + 24/25/26/28 は使用中(KCM: Check / Save Check & Register / Load Check & Register / RtMenuPagesPanel の区切り線)。+27 は廃止・予約(上記)


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
// kWidgetIDSpace +37 は 2026-07-15 に kKESCMNavPosTextWidgetID として再利用(旧 kKESCMToggleButtonWidgetID;
//   開始/解除ボタンは 2026-07-10 にフライアウト「Start / Stop」項目=kKESCMPopupStartStopActionID へ移行済み)
DECLARE_PMID(kWidgetIDSpace, kKESCMNavPosTextWidgetID, kKESCMPrefix + 37)	// Prev/Next の間に出す現在位置「3/12」(中央揃え StaticText。KESCMChangeNav.cpp が KESCMSetNavPosition で更新)
DECLARE_PMID(kWidgetIDSpace, kKESCMPrevChangeButtonWidgetID, kKESCMPrefix + 38)	// 「◀ Prev」= 前の見るべきページへスクロール(KESCMChangeNav.cpp)
DECLARE_PMID(kWidgetIDSpace, kKESCMNextChangeButtonWidgetID, kKESCMPrefix + 39)	// 「Next ▶」= 次の見るべきページへスクロール(KESCMChangeNav.cpp)
DECLARE_PMID(kWidgetIDSpace, kKESCMScrollMapWidgetID, kKESCMPrefix + 40)	// スクロールバー地図strip(文書窓の縦スクロールバー左隣に実行時注入; KESCMScrollMap.cpp)
DECLARE_PMID(kWidgetIDSpace, kKESCMToolWidgetID, kKESCMPrefix + 41)	// ツールボックスのツールボタンのウィジェットID(KESCMTool::InitWidget)
DECLARE_PMID(kWidgetIDSpace, kKESCMToolButtonWidgetID, kKESCMPrefix + 42)	// ★パネル内のツール切替ボタン(2026-08-07 追加。Prev の左・32x22)。押すと kKESCMToolBoss をアクティブツールにする(KESCMActivateOwnTool)。上の +41 とは別物＝あちらはツールボックス側のツール枠
// ★「Story Edits」セクション(2026-08-09 追加)。パネルを SplitterPanelWidget で上下に割り、下ペインに
//   「テキストが編集されたストーリー」の一覧を出す(段階3)。手本は製品 linksui の「リンク情報」セクション。
DECLARE_PMID(kWidgetIDSpace, kKESCMSplitterWidgetID, kKESCMPrefix + 43)			// パネルを上下に割る SplitterPanelWidget(Widgets.fh:462 / kSplitterPanelWidgetBoss)
DECLARE_PMID(kWidgetIDSpace, kKESCMTopPaneWidgetID, kKESCMPrefix + 44)			// 上ペイン=従来のパネル内容一式を丸ごと収めた GenericPanelWidget。★splitter の「伸縮させない方」に指定する
DECLARE_PMID(kWidgetIDSpace, kKESCMStorySectionWidgetID, kKESCMPrefix + 45)		// 下ペイン=Story Edits 本体(初期は非表示。段階3でツリーが入る)
DECLARE_PMID(kWidgetIDSpace, kKESCMStorySectionToggleWidgetID, kKESCMPrefix + 46)	// 開閉ボタン(三角)。★上ペインの中に置く=下ペインに置くと閉じたときボタンごと消えて開けなくなる
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
// kScriptInfoIDSpace +6 は現在空き(kescmShowOriginal 廃止; peek に統合)
// kScriptInfoIDSpace +7 は現在空き(kescmHideOriginal 廃止; kescmShowOriginal と対)
// kScriptInfoIDSpace +8 は現在空き(kescmShowOriginalUnderMouse 廃止; peek を使う)
// kScriptInfoIDSpace +9 は現在空き(旧 kKESCMArmMousePeekMethodScriptElement; スクリプトAPI撤去)
// kScriptInfoIDSpace +10 は現在空き(旧 kKESCMDisarmMousePeekMethodScriptElement; 同上)
// kScriptInfoIDSpace +11 は現在空き(旧 kKESCMToastMethodScriptElement; kescmToast はスクリプトAPIごと撤去)
// kScriptInfoIDSpace +12 は現在空き(旧 kKESCMSetPrintMarksMethodScriptElement; スクリプトAPI撤去)
// ★+1〜+12 は「メソッド」の跡地なので再利用せず、新しいプロパティは +13 から採る(旧用途と混同しないため)。
DECLARE_PMID(kScriptInfoIDSpace, kKESCMStatusPropertyScriptElement, kKESCMPrefix + 13)	// app.kcmStatus(読み取り専用。パネルのステータス行の最後の1行)
// (ツールの列挙子は本体の kToolBoxEnumScriptElement に載せるので、こちら側の ID は要らない。)

// "About Plug-ins" sub-menu:
#define kKESCMAboutMenuKey			kKESCMStringPrefix "kKESCMAboutMenuKey"
#define kKESCMAboutMenuPath		kSDKDefStandardAboutMenuPath kKESCMCompanyKey

// (旧 "Plug-ins" sub-menu 用の kKESCMPluginsMenuKey/Path は未使用のため撤去。パネルのメニュー配置は
//  下の kKESCMPanelPluginsMenuPath が担う)

// パネルを Plug-Ins メニューへ出すためのパスと位置。
// Plug-Ins ▸ KohakuNekotarou ▸ Kohaku Change Marker（リーフはパネル名キー）。
#define kKESCMPanelPluginsMenuPath		kSDKDefPlugInsStandardMenuPath kKESCMCompanyKey kSDKDefDelimitMenuPath kKESCMPanelTitleKey
#define kKESCMPanelPluginsMenuPosition	100.0	// 大きいほど下に並ぶ。

// Menu item keys:

// ★プラットフォーム別の修飾キー表記(2026-07-25 追補 Mac 対応)。
//   実装側は SDK の IEvent が差を吸収する(OptionAltKeyDown = Win の Alt / Mac の Option、
//   CmdKeyDown = Win の Ctrl / Mac の Command)ので、切り替えるのは「ユーザーに見せる名前」だけ。
//   この定数は文字列リテラルなので、.fr の StringTable でも C++ でも隣接連結でそのまま埋め込める
//   (例: "Hold Left + " kKESCMAltKeyName "=")。MACINTOSH は Mac ビルドの xcconfig
//   (GCC_PREPROCESSOR_DEFINITIONS)と odfrc の双方で定義される(SDK サンプル snapshot/linksui に
//   .fr 内 #ifdef MACINTOSH の実例あり)。
//   ※現在この表記が要るのは How to Use 本文(kKESCMHintKey)だけ。KESCM のジェスチャは Ctrl/Command を
//     使わないので Cmd 側の名前定数は用意していない(必要になったら同じ形で足す)。
#ifdef MACINTOSH
#define kKESCMAltKeyName	"Option"
#else
#define kKESCMAltKeyName	"Alt"
#endif

// Other StringKeys:
#define kKESCMAboutBoxStringKey	kKESCMStringPrefix "kKESCMAboutBoxStringKey"
#define kKESCMRepoURL			"https://github.com/KohakuNekotarou/KohakuChangeMarker"// 配布元URL。「このプラグインについて」本文とパネルのイラストクリックの飛び先で共通
// (kKESCMAboutScriptMenuKey / kKESCMScriptHelpStringKey は「About Scripting」撤去(2026-07-25)に伴い削除)
#define kKESCMUsageMenuKey		kKESCMStringPrefix "kKESCMUsageMenuKey"	// パネルのフライアウト「使い方」のメニュー名(本文は kKESCMHintKey を再利用)
#define kKESCMHideUnchangedMenuKey	kKESCMStringPrefix "kKESCMHideUnchangedMenuKey"	// パネルのフライアウト「Hide Unchanged Spreads」トグルのメニュー名
#define kKESCMHideConfirmKey		kKESCMStringPrefix "kKESCMHideConfirmKey"	// その確認ダイアログ本文(enUS=英語。日本語UIは KESCMLoc.h の実行時切替 2026-08-05)
#define kKESCMShowOldNumsMenuKey	kKESCMStringPrefix "kKESCMShowOldNumsMenuKey"	// パネルのフライアウト「Show Original Page Numbers」トグルのメニュー名
#define kKESCMSyncViewsMenuKey		kKESCMStringPrefix "kKESCMSyncViewsMenuKey"	// パネルのフライアウト「Sync Layout Views」トグルのメニュー名
#define kKESCMAlignViewsMenuKey		kKESCMStringPrefix "kKESCMAlignViewsMenuKey"	// パネルのフライアウト「Align Other Views to Active」(実行アクション)のメニュー名
#define kKESCMShowSrcMarksMenuKey	kKESCMStringPrefix "kKESCMShowSrcMarksMenuKey"	// パネルのフライアウト「Show Marks on Source」トグルのメニュー名
#define kKESCMPageMapToggleMenuKey	kKESCMStringPrefix "kKESCMPageMapToggleMenuKey"	// ページパネル右クリックのトグル「KCM: Register as Added/Removed Pages」の既定メニュー名(表示時は UpdateActionStates が Target=Added/Source=Removed に動的差し替え)
#define kKESCMPageCheckMenuKey		kKESCMStringPrefix "kKESCMPageCheckMenuKey"	// ページパネル右クリックのトグル「KCM: Check」のメニュー名
#define kKESCMPageRefreshCompareMenuKey	kKESCMStringPrefix "kKESCMPageRefreshCompareMenuKey"	// ページパネル右クリックの「KCM: Refresh Page Comparison」のメニュー名(選択ページの比較を再検出して更新)
#define kKESCMIgnorePageNumMenuKey	kKESCMStringPrefix "kKESCMIgnorePageNumMenuKey"	// パネルのフライアウト「Ignore Page Number Marker」トグルのメニュー名
#define kKESCMHoldToHideMarksMenuKey	kKESCMStringPrefix "kKESCMHoldToHideMarksMenuKey"	// パネルのフライアウト「Hold to Hide Marks」トグルのメニュー名
#define kKESCMScrollMapMenuKey		kKESCMStringPrefix "kKESCMScrollMapMenuKey"	// パネルのフライアウト「Show Scrollbar Map」トグルのメニュー名
#define kKESCMSavePanelStateMenuKey	kKESCMStringPrefix "kKESCMSavePanelStateMenuKey"	// パネルのフライアウト「Save Panel Settings」項目のメニュー名
#define kKESCMSaveChecksMenuKey		kKESCMStringPrefix "kKESCMSaveChecksMenuKey"	// パネルのフライアウト「Save Check & Register」項目のメニュー名
#define kKESCMLoadChecksMenuKey		kKESCMStringPrefix "kKESCMLoadChecksMenuKey"	// パネルのフライアウト「Load Check & Register」項目のメニュー名
#define kKESCMFindOversetMenuKey	kKESCMStringPrefix "kKESCMFindOversetMenuKey"	// パネルのフライアウト「Find Overset」トグルのメニュー名
#define kKESCMRefreshOversetMenuKey	kKESCMStringPrefix "kKESCMRefreshOversetMenuKey"	// パネルのフライアウト「Refresh Overset」項目のメニュー名
#define kKESCMExportChangedPagesMenuKey	kKESCMStringPrefix "kKESCMExportChangedPagesMenuKey"	// パネルのフライアウト「Export Changed Pages...」項目のメニュー名
#define kKESCMTranslucentPanelMenuKey	kKESCMStringPrefix "kKESCMTranslucentPanelMenuKey"	// パネルのフライアウト「Translucent Panel」トグルのメニュー名
#define kKESCMTranslucentPagesPanelMenuKey	kKESCMStringPrefix "kKESCMTranslucentPagesPanelMenuKey"	// パネルのフライアウト「Translucent Pages Panel」トグルのメニュー名(対象は本体のページパネル)
// (kKESCMTranslucentToolboxMenuKey は 2026-08-07 に機能ごと撤去。文字列キーは ActionID と違って
//  外部に保存されないので、跡地を残す必要は無い＝行ごと削除してある。)

// ショートカット割当可アクション用のアクションエリア(KESCL と同型。2026-07-24)。ActionDef の area 欄に
// kKESCMPanelMenuActionArea を渡し、StringTable でその値 kKESCMPanelMenuActionAreaValue に解決させると、
// Edit > キーボードショートカット の Product Area「Palette Menus」に「Kohaku Change Marker: <名前>」として並ぶ。
// 先頭の "KBSCE " は KBSC エディタ用エリアキーの規約プレフィックス。既定ショートカットは同梱しない(ユーザーが割当)。
// ※この表記は表示ラベルにすぎない。ショートカット自体は IShortcutManager が ActionID + コンテキスト文字列で
//   保持する(IShortcutManager.h の AddShortcut/GetActionIDOfShortcut)ので、ここを改名しても既存の割当は外れない。
#define kKESCMPanelMenuActionArea		"KBSCE Palette Menus: Kohaku Change Marker: "
#define kKESCMPanelMenuActionAreaValue	"Palette Menus:Kohaku Change Marker"

// パネル: 内部フライアウト(ポップアップ)メニュー名＋そのメニューパス。
#define kKESCMInternalPopupMenuNameKey	kKESCMStringPrefix "kKESCMInternalPopupMenuNameKey"
#define kKESCMPopupMenuPath				kKESCMInternalPopupMenuNameKey

// フライアウトの「Marks opacity」サブメニュー(中に 25% / 75%)。名前は英語リテラル(KESCM のメニュー名は
// 全ロケール英語で統一)。子項目(25%/75%)はこの kKESCMOpacitySubmenuPath を親メニューパスとして指す。
// 親ノードは MenuDef で actionID 0・パス末尾に区切り(kSDKDefDelimitMenuPath)を付けて宣言する
// (Adobe 実例 open/components/buttonui FormFieldUIMenu.fr / incopyexportui と同じ流儀)。
#define kKESCMOpacitySubmenuName		"Marks opacity"
#define kKESCMOpacitySubmenuPath		kKESCMPopupMenuPath kSDKDefDelimitMenuPath kKESCMOpacitySubmenuName

// パネルの文字列キー(値は KESCM_enUS.fr の StringTable。全ロケールがこの1枚を引く=
// LocaleIndex の全行が index_enUS を指す。KESCM_jaJP.fr は 2026-08-05 に撤去し、日本語で出す
// 2箇所だけ KESCMLoc.h の実行時切替に移した)。
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
#define kKESCMToolStringKey		kKESCMStringPrefix "kKESCMToolStringKey"	// ツールボックスのツール名(ツールチップ)。全ロケール英語で統一

// PNG アイコンリソース(プラグインに埋め込み; .pln とは別ファイルでは出荷しない)。
#define kKESCMIconOnResID	1001
#define kKESCMIconOffResID	1002
#define kKESCMPaletteIconResID	1003	// パネルが折りたたまれた時に出る小さいドックタブアイコン

// スクロールバー地図stripのビューリソースID(kViewRsrcType; ::CreateObject で実行時生成する。KESCMScrollMap.cpp)
#define kKESCMScrollMapRsrcID	1010

// ✓チェックマークカーソルのリソースID。CursorSpec の CursorID として使い、HOTC(このID)でホットスポット
// (✓の折れ点=座標取得点)を指定する。★2026-07-25: 画像はコールバック描画から PNGC リソースへ変更
// (KESCM_Check_10_18.png ＋ @2x=+kHIDPICrsrOffset / @3to2x=+kHIDPI150CrsrOffset)。押下時のゴミの発生源
// =基底のモーダルカーソル取得によるコールバック再実行 を断つため。KESCMCursorProvider.cpp / KESCM.fr。
#define kKESCMCheckCursorResID	1020

// Alt+左「色比較」の CMYK 情報カーソルのリソースID。✓カーソルと HOTC は同じ(10,18)だが CursorID は
// 分ける。同一 CursorID を使い回すとカーソルキャッシュが ✓(24×24)と CMYK情報(150×60)を取り違え、
// 色比較の初回フレームに「ゴミ」が一瞬見えた(ユーザー報告 2026-07-13)。別IDにして解消。KESCMPeek.cpp。
#define kKESCMCmykCursorResID	1021

// CMYK 情報カーソルの交互切替用の第2リソースID。ドラッグ中の数値更新は kFalse スペックの「入れ直し」で
// 行うが(動的 kTrue スペックは設定の瞬間に未初期化バッファが見える=初回ゴミの真因のため全廃。2026-07-14)、
// 同一スペックの再設定が no-op 扱いされても確実に描き直しが起きるよう 1021 と 1022 を交互に使う
// (KESCMTracker.cpp の InstallCmykCursor)。HOTC は 1021 と同じ (10,18)=切替でカーソル位置は動かない。
#define kKESCMCmykCursor2ResID	1022

// ✓カーソルの非アクティブ版(白抜き=黒フチ+白本体)のリソースID。ツール選択中、「Start 中かつマウス下が
// Target 文書」のときだけ黒✓、それ以外(Source・第3の文書・未 Start)は白抜き✓=「ここではツールは
// 効かない」の明示(ユーザー指定 2026-07-15。灰色本体は判別しづらく反転式に変更)。CursorID を分けるのは
// キャッシュの取り違え防止(1021/1022 と同じ理由)+ClearCache 不要で切り替えるため。
// HOTC は ✓ と同じ (10,18)。画像は黒✓と同様 PNGC リソース(KESCM_CheckOff_10_18.png ＋ @2x / @3to2x。
// 2026-07-25)。KESCMCursorProvider.cpp / KESCM.fr。
#define kKESCMCheckCursorInactiveResID	1023

// ツールボックスの KESCM ツール専用アイコン(32×32 通常 / 64×64 = +kHIDPIIconOffset)。従来はパネル用
// アイコン(kKESCMIconOnResID)を流用していたが、専用画像(KESCM_Tool_32.png/_64.png)を用意したため差し替え
// (ユーザー提供 2026-07-14)。ダーク版は用意していないため PNGAD もライト版と同じ画像を指す(=流用)。
#define kKESCMToolIconResID	1030

// Menu item positions (flyout order, 2026-07-24 に大幅入れ替え):
//   Start/Stop(9.0) → ─線Sep1(9.1) →
//   [表示系トグル群] Hold to Hide Marks(9.20) → Ignore Page Number Marker(9.22) → Marks opacity ▸(9.24) →
//     Print comparison marks(9.26) → Show Original Page Numbers(9.28) →
//     Show Marks on Source(9.30) → Show Scrollbar Map(9.32) → Sync Layout Views(9.34) →
//     Translucent Panel(9.36) →
//   ─線OversetSep(9.40) → Find Overset(9.42) → Refresh Overset(9.44) →
//   ─線Sep3(9.50) → [実行アクション群] Align Other Views to Active(9.52) → Export Changed Pages...(9.53) →
//     Hide Unchanged Spreads(9.54) → Save Panel Settings(9.56) → Save Check & Register(9.58) →
//     Load Check & Register(9.60) →
//   ─線Sep2(9.95) → How to Use(10) → About this plug-in(12)。 ※About Scripting(11)は 2026-07-25 撤去
// ※メニュー名は日本語ロケールでも英語で統一(2026-07-04)。区切り線は Sep1/OversetSep/Sep3/Sep2 の4本を再利用。
#define kKESCMStartStopMenuItemPosition		9.0	// 「Start / Stop」(比較開始/解除)をフライアウト先頭に。名前は arm 状態で動的に Start↔Stop
#define kKESCMSep1MenuItemPosition			9.1	// Start の下の区切り線(パス末尾 ":-")
// ── 表示系トグル群 ──
#define kKESCMHoldToHideMarksMenuItemPosition	9.20	// チェック式トグル「Hold to Hide Marks」(枠表示の極性反転)。Sep1 の直後(群の先頭)
#define kKESCMIgnorePageNumMenuItemPosition	9.22	// チェック式トグル「Ignore Page Number Marker」
#define kKESCMOpacitySubmenuMenuItemPosition	9.24	// 「Marks opacity」サブメニュー(中に 25% / 75%)。Print の上へ入れ替え(2026-07-24)
#define kKESCMPrintMarksMenuItemPosition	9.26	// チェック式トグル「Print comparison marks」。Marks opacity の下へ入れ替え(2026-07-24)
#define kKESCMOpacity25SubMenuItemPosition	1.0	// サブメニュー「Marks opacity」内: 25%(選択中に✓)
#define kKESCMOpacity75SubMenuItemPosition	2.0	// サブメニュー「Marks opacity」内: 75%(25% と相互排他)
// (9.27 = 「Show HUD」は 2026-08-06 に機能ごと撤去。位置番号は空き=別項目に使ってよい)
#define kKESCMShowOldNumsMenuItemPosition	9.28	// チェック式トグル「Show Original Page Numbers」
#define kKESCMShowSrcMarksMenuItemPosition	9.30	// チェック式トグル「Show Marks on Source」
#define kKESCMScrollMapMenuItemPosition		9.32	// チェック式トグル「Show Scrollbar Map」
#define kKESCMSyncViewsMenuItemPosition		9.34	// チェック式トグル「Sync Layout Views」
#define kKESCMTranslucentPagesPanelMenuItemPosition	9.36	// チェック式トグル「Translucent Pages Panel」(★Windows 専用=フローティング中の**本体のページパネル**を半透明に。2026-08-06 追加。★同日ユーザー指定で Translucent Panel より上へ)
// (9.37 は「Translucent Toolbox」の跡地。2026-08-07 に機能ごと撤去したので空き番。)
#define kKESCMTranslucentPanelMenuItemPosition	9.38	// チェック式トグル「Translucent Panel」(表示系トグル群の末尾。★Windows 専用=フローティング中のパネル自身を半透明に。2026-08-06 に Pages 側と上下を入れ替え)
// ── Overset 群 ──
#define kKESCMOversetSepMenuItemPosition	9.40	// Find Overset 群の上の区切り線(パス末尾 ":-")
#define kKESCMFindOversetMenuItemPosition	9.42	// チェック式トグル「Find Overset」(アクティブ文書の overset ページに十字)
#define kKESCMRefreshOversetMenuItemPosition	9.44	// 実行アクション「Refresh Overset」(ON時のみ有効=再走査)
// ── 実行アクション群 ──
#define kKESCMSep3MenuItemPosition			9.50	// Refresh Overset の下の区切り線(パス末尾 ":-")。この下に実行アクション群を置く
#define kKESCMAlignViewsMenuItemPosition	9.52	// 実行アクション「Align Other Views to Active」を実行アクション群の先頭に(2026-07-24)
#define kKESCMHideUnchangedMenuItemPosition	9.54	// チェック式トグル「Hide Unchanged Spreads」
#define kKESCMSavePanelStateMenuItemPosition	9.56	// 実行アクション「Save Panel Settings」
#define kKESCMSaveChecksMenuItemPosition	9.58	// 実行アクション「Save Check & Register」
#define kKESCMLoadChecksMenuItemPosition	9.60	// 実行アクション「Load Check & Register」
#define kKESCMExportChangedPagesMenuItemPosition	9.53	// 実行アクション「Export Changed Pages...」(変更ページ一覧をTSVで保存)。Align の直下(2026-07-25 ユーザー指定)
// ── 情報系(末尾) ──
#define kKESCMSep2MenuItemPosition			9.95	// How to Use の上の区切り線(パス末尾 ":-")
#define kKESCMUsageMenuItemPosition			10.0	// 「使い方」
// ページパネルのページ右クリックメニュー(内部名 RtMenuPagesPanel、2026-07-05 実機確定)内の位置。
// 本家項目の後ろ(末尾)に付ける。内部名は非翻訳キーなので全ロケール共通で効く。
#define kKESCMPageMapSepMenuItemPosition	2999.0	// KESCM 追加項目(Register 3000.0 / Check 3001.0)の直上の区切り線。本家メニュー項目と KESCM 項目を視覚的に分ける
#define kKESCMPageMapToggleMenuItemPosition	3000.0
#define kKESCMPageCheckMenuItemPosition		3001.0	// 「KCM: Check」を Register の直後(ページパネル右クリック末尾)に
#define kKESCMPageRefreshCompareMenuItemPosition	3002.0	// 「KCM: Refresh Page Comparison」を Check の直後(ページパネル右クリック末尾)に
#define kKESCMAboutThisMenuItemPosition		12.0	// 末尾に「このプラグインについて」(11.0=旧 About Scripting は撤去済み 2026-07-25)


// Initial data format version numbers
#define kKESCMFirstMajorFormatNumber  RezLong(1)
#define kKESCMFirstMinorFormatNumber  RezLong(0)

// Data format version numbers for the PluginVersion resource 
#define kKESCMCurrentMajorFormatNumber kKESCMFirstMajorFormatNumber
#define kKESCMCurrentMinorFormatNumber kKESCMFirstMinorFormatNumber

#endif // __KESCMID_h__
