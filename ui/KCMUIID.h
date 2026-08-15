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

// ★★2026-08-15（第2段 Task 6B）: **model と UI の両方が同じ値で知っていなければならない ID**
//   （Facade 5本の IID・通知の protocol IID・MessageID 7本、および model 側の prefix）は
//   KESCMBoundaryID.h にある。**このファイルは ui/ 側のコピーを読む**（相方 = source/KESCMBoundaryID.h）。
//   ⚠ 片方だけ直すと黙ってずれる。必ず両方直すこと。
//   ★あちらが定義する kKESCMPrefix（0x1EA500）は **model 側の番号**で、下の kKCMUIPrefix（0x1EA580）
//     とは別物。UI 専用の ID はこのファイルで kKCMUIPrefix から採る。
#include "KESCMBoundaryID.h"

// Company: ★KESCMBoundaryID.h の値をそのまま名乗る。model と UI は**同じ製品の2つの .pln** なので、
//   会社名・表示名・版数が食い違ってはいけない（[[one-question-one-place]]）。
//   ⚠ 雛形は kSDKDefPlugInCompanyKey（＝SDK サンプル用の名前）を名乗っていた＝About メニューで
//     Adobe のサンプル群と同じ束ね名の下に並んでいた。2026-08-15（第2段 Task 6B-2）に揃えた。
#define kKCMUICompanyKey	kKESCMCompanyKey
#define kKCMUICompanyValue	kKESCMCompanyValue

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
// ★★2026-08-15（第2段 Task 6B-2）: 雛形の kSDKDefPluginVersionString をやめ、**製品の版数**を名乗る。
//   この値は PluginVersion リソース（＝プラグイン一覧の表示）と .rc の FileVersion に出るので、
//   放っておくと model(1.4.0) と UI(SDK 既定) が**別々の版数の別プラグイン**として並ぶ。
//   値の正本＝KESCMBoundaryID.h ／ 履歴と「次に提出する分」の増分＝source/KESCMID.h。
#define kKCMUIVersion		kKESCMVersion					// Version of this plug-in (for the About Box).
#define kKCMUIAuthor		""					// Author of this plug-in (for the About Box).

// Plug-in Prefix: (please change kKCMUIPrefixNumber above to modify the prefix.)
#define kKCMUIPrefix		RezLong(kKCMUIPrefixNumber)				// The unique numeric prefix for all object model IDs for this plug-in.
#define kKCMUIStringPrefix	SDK_DEF_STRINGIZE(kKCMUIPrefixNumber)	// The string equivalent of the unique prefix number for  this plug-in.

// Missing plug-in: (see ExtraPluginInfo resource)
#define kKCMUIMissingPluginURLValue		kSDKDefPartnersStandardValue_enUS // URL displayed in Missing Plug-in dialog
#define kKCMUIMissingPluginAlertValue	kSDKDefMissingPluginAlertValue // Message displayed in Missing Plug-in dialog - provide a string that instructs user how to solve their missing plug-in problem

// PluginID:
DECLARE_PMID(kPlugInIDSpace, kKCMUIPluginID, kKCMUIPrefix + 0)

//========================================================================================
// ★★★以下は 2026-08-15（第2段 Task 6B-2）に source/KESCMID.h から移してきた **UI 専用の ID**。
//
//  ★★機械的に安全にするために採った方針が2つある。どちらも「判断をゼロにする」ためのもの:
//
//   ① **オフセットを保存した** — `kKESCMPrefix + 8` → `kKCMUIPrefix + 8`。番号は採り直していない。
//      ⇒ 採番の判断が1つも要らず、衝突も原理的に起きない（同じ ID 空間の中で番号が重複していない）。
//      ⚠ 逆に言うと、**model 側 KESCMID.h のこれらの番号は「空き」ではない**。あちらの帯と
//        こちらの帯は別物なので実害は無いが、対応が読めなくなるので再利用しないこと。
//      ★最大オフセットは WidgetID の +62 ＝ kKCMUIPrefix(0x1EA580) に許された 128 枠に収まっている。
//
//   ② **ID の名前は `kKESCM*` のまま変えていない** — `kKCMUI*` へ改名していない。
//      ⇒ C++ 側は 53 ファイルの `#include "KCMUIID.h"` を `"KCMUIID.h"` に変えるだけで済み、
//        コードは1行も動かなかった。**コードとコメントの中の ID 名がすべてそのまま有効**。
//
//  ⚠ 文字列キーだけは別扱い＝値は `kKESCMStringPrefix`（model の prefix）のまま。
//    **文字列キーはグローバルに一意でなければならず、widget ID のように借用できない**
//    （ガイド vol2-12:71）ので、prefix を変えるとキーの値が変わってしまう。
//    ⇒ 文字列テーブルは値を1文字も変えずに丸ごと移せた。
//========================================================================================

// ClassIDs:
DECLARE_PMID(kClassIDSpace, kKESCMThumbIdleTaskBoss, kKCMUIPrefix + 7)	// IIdleTask: クローズ後の Pages サムネイル再生成を次のidleに遅延(旧 kKESCMToastIdleTaskBoss のスロット転用)
DECLARE_PMID(kClassIDSpace, kKESCMPanelWidgetBoss, kKCMUIPrefix + 8)	// ChangeMarker 操作パネル(パレット)
DECLARE_PMID(kClassIDSpace, kKESCMActionComponentBoss, kKCMUIPrefix + 9)	// About メニューのアクションコンポーネント
DECLARE_PMID(kClassIDSpace, kKESCMIconWidgetBoss, kKCMUIPrefix + 11)	// kRollOverIconButtonBoss を継承し IID_ITIP を追加(パネルイラストのツールチップ)
DECLARE_PMID(kClassIDSpace, kKESCMScrollMapWidgetBoss, kKCMUIPrefix + 12)	// kGenericPanelWidgetBoss+自前IControlView: 縦スクロールバー脇の枠ページ地図strip(旧 kKESCMLayoutSyncObserverBoss のスロット転用)
DECLARE_PMID(kClassIDSpace, kKESCMToolBoss, kKCMUIPrefix + 13)	// kGenericToolBoss継承: ツールボックスの peek 専用ツール(KESCMTool.cpp)
DECLARE_PMID(kClassIDSpace, kKESCMTrackerBoss, kKCMUIPrefix + 14)	// ツールのキャプチャ型トラッカー(IID_ITRACKER+IID_IEVENTHANDLER)。左ボタン hold 中だけ reveal。KESCMTracker.cpp
DECLARE_PMID(kClassIDSpace, kKESCMTrackerRegisterBoss, kKCMUIPrefix + 15)	// トラッカー登録(kLayoutWidgetBoss×ツール→トラッカー)。KESCMTrackerRegister.cpp
// (unused-slot placeholders below start at +16; +6..+15 are declared above. 2026-08-05 audit)
DECLARE_PMID(kClassIDSpace, kKESCMStorySectionToggleBoss, kKCMUIPrefix + 16)	// kRollOverIconButtonBoss継承+IID_IOBSERVER: パネル下部「Story Edits」セクションの開閉ボタン(三角)。絵は本体の kTreeBranchCollapsed/Expanded を借りる
DECLARE_PMID(kClassIDSpace, kKESCMStorySectionPanelBoss, kKCMUIPrefix + 17)	// kGenericPanelWidgetBoss継承+IID_IKESCMSAVEDSECTIONHEIGHT(kPersistIntDataImpl): 下ペイン本体。閉じる直前の高さをここに覚える(手本=製品 linksui の kLinkInfoPanelWidgetBoss)
DECLARE_PMID(kClassIDSpace, kKESCMStoryTreeWidgetBoss, kKCMUIPrefix + 18)	// kTreeViewWidgetBoss継承: Story Edits の一覧(平坦1階層)。載せるのは adapter と widget mgr の2つだけ＝コントローラーは kTreeViewWidgetBoss が既に持っている
DECLARE_PMID(kClassIDSpace, kKESCMStoryRowWidgetBoss, kKCMUIPrefix + 19)	// kTreeNodeWidgetBoss継承: 一覧の1行。載せるのは IID_IEVENTHANDLER(kKESCMStoryRowEHImpl)の1つだけ＝単クリックでジャンプ・ダブルクリックで全文選択。⚠2026-08-11 まで空の Class だった(段階4 で足した)
DECLARE_PMID(kClassIDSpace, kKESCMStoryRowCellBoss, kKCMUIPrefix + 20)	// kInfoStaticTextWidgetBoss継承+IID_ITIP(kKESCMNoTipImpl): 一覧の行のセル。★狙いはツールチップを**消す**こと＝素の静的テキストは省略表示すると全文をポップアップで出す(実機ダンプ: kStaticTextWidgetBoss が IID_ITIP=kTextWidgetTipImpl を持つ)。行に出るのは邪魔なので空の tip を返す実装で上書きする(2026-08-10 ユーザー指定)
// ブック比較のダイアログ(2026-08-11)。★モードレス＝開いたまま文書を触れる。だから未決の
// 「行クリックでその章を開く」を後から足せる(モーダルだとその道が閉じる)。stock の kDialogBoss に
// 自前の IDialogController を載せるだけ＝KESCL の Jump Offset ダイアログと同じ形。
DECLARE_PMID(kClassIDSpace, kKESCMBookDialogBoss, kKCMUIPrefix + 21)
// ブック比較ダイアログの中の章一覧(2026-08-11)。★Story Edits の一覧と**同じ3点セット**＝
// ツリー本体(adapter+widget mgr)／行／行のセル。あちらとの違いは住む場所だけで、
// パレットではなくダイアログに載る＝テーマが kIDDialogTheme・フォントがダイアログ用になる。
DECLARE_PMID(kClassIDSpace, kKESCMBookTreeWidgetBoss, kKCMUIPrefix + 22)	// kTreeViewWidgetBoss継承: 章の一覧(平坦1階層)。載せるのは adapter と widget mgr の2つだけ
DECLARE_PMID(kClassIDSpace, kKESCMBookRowWidgetBoss, kKCMUIPrefix + 23)	// kTreeNodeWidgetBoss継承: 一覧の1行。★今は何も足していない空の Class＝行クリック(段階4「その章を開く」)で IID_IEVENTHANDLER を載せる場所として先に採ってある。Story Edits の行 boss がたどったのと同じ順序
DECLARE_PMID(kClassIDSpace, kKESCMBookRowCellBoss, kKCMUIPrefix + 24)	// kInfoStaticTextWidgetBoss継承+IID_ITIP(kKESCMNoTipImpl): 行のセル。素の静的テキストは省略表示すると全文をポップアップで出すので、一覧の行では黙らせる(Story Edits と同じ判断=2026-08-10 ユーザー指定)
// パネルを上下に割る分割バー(2026-08-12)。★中身は素の kSplitterPanelWidgetBoss と同じで、
// IID_IEVENTHANDLER だけを「何もしない」実装に差し替えてある＝**バーをドラッグして動かせなくする**
// (ユーザー指定 2026-08-12)。継承した boss からインターフェイスを**取り除く道は無い**ので、消し方は
// 「別の答えを返す実装で上書きする」になる ---- kKESCMStoryRowCellBoss(+20)がツールチップを黙らせたのと同じ形。
DECLARE_PMID(kClassIDSpace, kKESCMSplitterPanelBoss, kKCMUIPrefix + 25)	// kSplitterPanelWidgetBoss継承+IID_IEVENTHANDLER(kKESCMSplitterEHImpl): 分割バーを掴めない SplitterPanelWidget
DECLARE_PMID(kClassIDSpace, kKESCMUIStartupBoss, kKCMUIPrefix + 27)	// IStartupShutdown: **UI 側**の起動/終了処理(2026-08-13・model/UI 分割 第1段 Task 8)。パネル設定の復元・半透明の購読/解除・HUD のフォント返却・一括クローズの購読・遅延サムネイル idle task の解放。★model 側の kKESCMPeekStartupBoss と**対**。第2段でこの Class ごと KCMUI へ移る
DECLARE_PMID(kClassIDSpace, kKESCMUIDrawEventServiceBoss, kKCMUIPrefix + 26)	// IK2ServiceProvider+IDrwEvtHandler: **UI 専用**の描画サービス(2026-08-13・model/UI 分割 第1段 Task 6)。押下中 HUD だけを持つ。★上の kKESCMDrawEventServiceBoss(比較マーク)と役割が違う＝あちらは印刷と PDF 書き出しに出なければならないので model 側、こちらは画面専用。kDrawEventService は複数プロバイダ登録が前提(本体だけで20以上)。第2段でこの Class ごと KCMUI へ移る
DECLARE_PMID(kClassIDSpace, kKESCMBookPathTextWidgetBoss, kKCMUIPrefix + 28)	// kStaticTextWidgetBoss継承+IID_IEVEINFO(kFixedSizeEVEInfoImpl): ブック比較ダイアログの Target:/Source: 行(2026-08-15)。★★EVE は **.fr の幅を「最小幅」として扱う**(公式ガイド Using EVE の Example 2「We treat the width in the .fr file as a minimum width」)ので、フルパスを入れると widget が伸び、親ごと広がる(実測 593px)。⚠kEVEAlignFill では止まらない＝Fill は「親の幅を取る」で、その親が子に押し広げられる。⇒ **EVE は widget の寸法を IID_IEVEINFO に聞く**ので、「サイズはリソースが書いたとおり」と答える実装を名乗らせて幅を確定させ、省略は widget 自身の kEllipsizeBeginning に返す＝パネルの Target:/Source: と同じ出方になる。手本=KBS.fr:289-293(グリフ枠。SDK 全体で使用例ゼロだが実機で動作確認済み)
// InterfaceIDs:
// ⚠★ここにあるのは **UI 側の boss にだけ載る IID**。境界を跨ぐ IID（Facade 5本＋通知の protocol）は
//   **KESCMBoundaryID.h** にあり、あちらは model 側の `kKESCMPrefix` のまま名乗る
//   ＝値が食い違うと**ビルドは通るのに黙って何も起きない**。
DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMLAYOUTSYNCOBSERVER, kKCMUIPrefix + 0)	// レイアウトビュー同期オブザーバのアタッチ識別ID(AttachObserver の observerIID)
DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMDOCSCLOSEDOBSERVER, kKCMUIPrefix + 1)	// 一括クローズ完了(kPendingDocumentsClosedMsg)を受けるオブザーバのアタッチ識別ID
DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMPANELVISIBILITYOBSERVER, kKCMUIPrefix + 2)	// パネルの表示状態変化(kPaletteVisibilityChangedMessage)を受けるオブザーバのアタッチ識別ID。半透明トグルをドッキング切り替え/開き直しに追随させるために使う
DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMSAVEDSECTIONHEIGHT, kKCMUIPrefix + 3)	// IIntData として扱う: Story Edits セクションを閉じた瞬間の高さ(px)。次に開くときこの高さで開く。実装は SDK 標準の kPersistIntDataImpl(手本=linksui の IID_ISAVEDINFOPANESIZE)
// ImplementationIDs:
// ⚠ ここに載せた実装は **ui/KCMUIFactoryList.h** にも 1 対 1 で登録されていること
//   （登録漏れは完全に無言で失敗する）。
DECLARE_PMID(kImplementationIDSpace, kKESCMThumbIdleTaskImpl, kKCMUIPrefix + 5)	// IIdleTask 実装(クローズ後の Pages サムネイル再生成を遅延実行)
DECLARE_PMID(kImplementationIDSpace, kKESCMPanelObserverImpl, kKCMUIPrefix + 6)	// IObserver 実装(パネルのウィジェットオブザーバ)
DECLARE_PMID(kImplementationIDSpace, kKESCMActionComponentImpl, kKCMUIPrefix + 7)	// IActionComponent 実装(About)
DECLARE_PMID(kImplementationIDSpace, kKESCMIconTipImpl, kKCMUIPrefix + 10)	// ITip 実装(パネルイラストにURLをツールチップ表示)
DECLARE_PMID(kImplementationIDSpace, kKESCMLayoutSyncObserverImpl, kKCMUIPrefix + 11)	// IObserver 実装(レイアウトビュー同期)
DECLARE_PMID(kImplementationIDSpace, kKESCMScrollMapViewImpl, kKCMUIPrefix + 12)	// IControlView 実装(スクロールバー地図stripの自前描画; KESCMScrollMap.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMToolImpl, kKCMUIPrefix + 13)	// ITool 実装(KESCMTool.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMTrackerImpl, kKCMUIPrefix + 14)	// ITracker 実装(CTracker派生; KESCMTracker.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMTrackerRegisterImpl, kKCMUIPrefix + 15)	// ITrackerRegister 実装(KESCMTrackerRegister.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMTrackerEHImpl, kKCMUIPrefix + 16)	// IEventHandler 実装(CTrackerEventHandler派生; 押下中のボタン解放を EndTracking へ転送。KESCMTracker.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMCursorProviderImpl, kKCMUIPrefix + 17)	// ICursorProvider 実装(CToolCursorProvider派生; ツール選択中は常時✓カーソル。KESCMCursorProvider.cpp)
// (+6..+17 are all declared above - stale placeholders for them removed 2026-08-05 audit)
// (+18 = kKESCMSpriteImpl は 2026-08-06 に押下中 HUD ごと撤去。★**この番号は再利用しない**。
//  トラッカー boss の IID_ISPRITE は SDK 標準実装 kNoHandleSpriteImpl に戻してある=公式サンプル
//  wavetool の boss 構成(WavTl.fr:151,155)と同じ形)
DECLARE_PMID(kImplementationIDSpace, kKESCMDocsClosedObserverImpl, kKCMUIPrefix + 19)	// IObserver 実装(一括クローズ完了で、保留した後片付けを1回だけ流す。KESCMPeek.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMPanelVisibilityObserverImpl, kKCMUIPrefix + 20)	// IObserver 実装(パネルの表示状態が変わったら半透明を貼り直す。KESCMPanelAlpha.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMPanelRollOverImpl, kKCMUIPrefix + 21)	// IMouseRollOver 実装(パネルにカーソルが乗っている間だけ半透明を解除。KESCMPanelAlpha.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMStorySectionToggleObserverImpl, kKCMUIPrefix + 22)	// IObserver 実装(開閉ボタンの押下を受けて Story Edits セクションを開閉。KESCMStorySectionObserver.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMStoryTreeAdapterImpl, kKCMUIPrefix + 23)	// ITreeViewHierarchyAdapter 実装(ListTreeViewAdapter派生。KESCMStoryTreeAdapter.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMStoryTreeWidgetMgrImpl, kKCMUIPrefix + 24)	// ITreeViewWidgetMgr 実装(CTreeViewWidgetMgr派生。KESCMStoryTreeWidgetMgr.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMNoTipImpl, kKCMUIPrefix + 25)	// ITip 実装(常に空を返す＝ツールチップを出さない。KESCMNoTip.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMPanelViewImpl, kKCMUIPrefix + 26)	// IControlView 実装(PalettePanelView派生。ConstrainDimensions でパネルの最小サイズを守る。KESCMPanelView.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMStoryRowEHImpl, kKCMUIPrefix + 27)	// IEventHandler 実装(TreeNodeEventHandler派生。Story Edits の行=単クリックでジャンプ・ダブルクリックでストーリー全文を選択。KESCMStoryRowEH.cpp)
// (kKCMUIPrefix + 28 は一度 kKESCMStoryRowViewImpl=行の間の区切り線を消す IControlView に使い、同日
//  撤去した跡地。線を残すユーザー判断なので実装ごと消えている＝経緯は KESCM.fr の行 boss のコメント。
//  ActionID と違い Impl 番号は外部保存が参照しないので、下記のとおり再利用した。)
DECLARE_PMID(kImplementationIDSpace, kKESCMStoryTreeEHImpl, kKCMUIPrefix + 28)	// IEventHandler 実装(TreeViewEventHandler派生)。★一覧**そのもの**のキー操作＝↑↓で行を移動し、着いた行へジャンプする(KESCMStoryTreeEH.cpp)。行側の kKESCMStoryRowEHImpl とは別物＝あちらはクリック
DECLARE_PMID(kImplementationIDSpace, kKESCMBookDialogControllerImpl, kKCMUIPrefix + 29)	// IDialogController 実装(CDialogController派生)。ブック比較のモードレスダイアログ＝開いたとき対象の2ブック名を埋める(KESCMBookDialog.cpp)
// (退役 2026-08-12)kKESCMBookDialogObserverImpl(kKCMUIPrefix + 30)＝ブック比較ダイアログの Compare ボタンの押下を受けていた IObserver。
//   ★ボタンごと撤去した(確認アラート→OK で比較する流れへ変更)ので実装ファイルごと削除し、IID_IOBSERVER は kDialogBoss の stock(kCDialogObserverImpl)へ戻した。スロットは予約のまま再利用しない。
DECLARE_PMID(kImplementationIDSpace, kKESCMBookTreeAdapterImpl, kKCMUIPrefix + 31)	// ITreeViewHierarchyAdapter 実装(ListTreeViewAdapter派生。KESCMBookTreeAdapter.cpp)。ブック比較ダイアログの章一覧＝行数を答えるだけ
DECLARE_PMID(kImplementationIDSpace, kKESCMBookTreeWidgetMgrImpl, kKCMUIPrefix + 32)	// ITreeViewWidgetMgr 実装(CTreeViewWidgetMgr派生。KESCMBookTreeWidgetMgr.cpp)。章一覧の行の生成と流し込み(章名 / 状態の2列)
DECLARE_PMID(kImplementationIDSpace, kKESCMBookRowEHImpl, kKCMUIPrefix + 33)	// IEventHandler 実装(TreeNodeEventHandler派生。KESCMBookRowEH.cpp)。ブック比較の章行＝**ダブルクリックでその章を開く**・**右クリックで行メニュー**(Start Change Marker)。★単クリックは何もしない(Story Edits の行と違う＝あちらは開いている文書の中を移動するだけだが、こちらは文書を開いてしまうため)。実際の動作は KESCMBookOpen.cpp
DECLARE_PMID(kImplementationIDSpace, kKESCMUIStartupImpl, kKCMUIPrefix + 38)	// IStartupShutdown 実装(UI 側の起動/終了。KESCMUIStartup.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMModelChangeObserverImpl, kKCMUIPrefix + 37)	// IObserver 実装(model の通知を受けて画面を作り直す **UI 側**。KESCMModelChangeObserver.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMUIDrawEventSrvcImpl, kKCMUIPrefix + 35)	// CServiceProvider 実装(kDrawEventService。UI 専用の描画サービス。KESCMUIDrawEvent.cpp)。★GetThreadingPolicy は手書きしない＝CServiceProvider がプラグインの型から既定を返す
DECLARE_PMID(kImplementationIDSpace, kKESCMUIDrawEventHandlerImpl, kKCMUIPrefix + 36)	// IDrwEvtHandler 実装(押下中 HUD の描画だけ。画面専用＝PDF 書き出しに出なくてよい。KESCMUIDrawEvent.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMSplitterEHImpl, kKCMUIPrefix + 34)	// IEventHandler 実装(CEventHandler派生＝全メソッドが kFalse を返すだけの基底をそのまま使う)。パネルの分割バーが押下を受け取らなくなる＝ドラッグで動かせない(KESCMSplitterEH.cpp)
// ActionIDs:
DECLARE_PMID(kActionIDSpace, kKESCMAboutActionID, kKCMUIPrefix + 0)
DECLARE_PMID(kActionIDSpace, kKESCMPanelWidgetActionID, kKCMUIPrefix + 1)	// パネルの表示/非表示(ウィンドウメニュー)
DECLARE_PMID(kActionIDSpace, kKESCMPopupAboutThisActionID, kKCMUIPrefix + 2)	// パネルのフライアウトの「このプラグインについて」
DECLARE_PMID(kActionIDSpace, kKESCMPopupAboutScriptActionID, kKCMUIPrefix + 3)	// (撤去・予約)旧「About Scripting」フライアウト項目。2026-07-25 削除(ユーザー指定)。ID スロットは予約のまま
DECLARE_PMID(kActionIDSpace, kKESCMPopupUsageActionID, kKCMUIPrefix + 4)	// パネルのフライアウトの「使い方」
// kActionIDSpace +5 は現在空き(旧 kKESCMPopupTestSplitActionID; Split Test 検証メニューは撤去済み)
// kActionIDSpace +6 は現在空き(旧 kKESCMPopupSplitTargetActionID; Split Target on Start は 2026-07-04 撤去。
//   仕組みは docs/ai-notes/kescm-split-target-mechanism.md に保存)
DECLARE_PMID(kActionIDSpace, kKESCMPopupHideUnchangedActionID, kKCMUIPrefix + 7)	// パネルのフライアウトの「Hide Unchanged Spreads」チェック式トグル(ON=変更なしスプレッドを隠す)
DECLARE_PMID(kActionIDSpace, kKESCMPopupShowOldNumsActionID, kKCMUIPrefix + 8)	// パネルのフライアウトの「Show Original Page Numbers」チェック式トグル(枠表示中/印刷ON時に隠す前の元番号バッジ)
DECLARE_PMID(kActionIDSpace, kKESCMPopupSyncViewsActionID, kKCMUIPrefix + 9)	// パネルのフライアウトの「Sync Layout Views」チェック式トグル(他文書のビューへ座標+拡大率を自動同期)
DECLARE_PMID(kActionIDSpace, kKESCMPopupShowSrcMarksActionID, kKCMUIPrefix + 10)	// パネルのフライアウトの「Show Marks on Source」チェック式トグル(Source側にも枠を常時表示。OPPでも表示・印刷にも出す。Startで既定ON)
DECLARE_PMID(kActionIDSpace, kKESCMPageMapToggleActionID, kKCMUIPrefix + 11)	// ページパネルのページ右クリック(RtMenuPagesPanel)のトグル「KCM: Register as Added/Removed Pages」(選択ページを「比較相手なし」として登録/解除。チェック/動的ラベルは kCustomEnabling。KESCMPageMap.cpp)
DECLARE_PMID(kActionIDSpace, kKESCMPopupIgnorePageNumActionID, kKCMUIPrefix + 12)	// パネルのフライアウトの「Ignore Page Number Marker」チェック式トグル(ON=ノンブル(自動ページ番号)マーカーを含むフレームを比較から除外。★既定OFF=sIgnorePageNumberMarker の初期値。KESCMPageNumberMarker.cpp)
DECLARE_PMID(kActionIDSpace, kKESCMPopupStartStopActionID, kKCMUIPrefix + 13)	// パネルのフライアウト先頭の「Start / Stop」(比較の開始/解除。旧トグルボタンをメニュー化。arm 状態で名前が Start↔Stop に動的変化=kCustomEnabling+SetNthActionName。KESCMPanelObserver.cpp の KESCMToggleStartStop)
DECLARE_PMID(kActionIDSpace, kKESCMPopupPrintMarksActionID, kKCMUIPrefix + 14)	// パネルのフライアウトの「Print comparison marks」チェック式トグル(旧パネルのチェックボックスをメニュー化。ON=マークを印刷し画面にも常時表示。KESCMPanelObserver.cpp の KESCMTogglePrintMarks)
DECLARE_PMID(kActionIDSpace, kKESCMPopupOpacity25ActionID, kKCMUIPrefix + 15)	// パネルのフライアウトの「Marks opacity 25%」(旧パネルの opacity ラジオをメニュー化。75% と相互排他=選択中の方に✓。KESCMPanelObserver.cpp の KESCMSetMarkOpacity25)
DECLARE_PMID(kActionIDSpace, kKESCMPopupOpacity75ActionID, kKCMUIPrefix + 16)	// パネルのフライアウトの「Marks opacity 75%」(25% と相互排他)
DECLARE_PMID(kActionIDSpace, kKESCMPopupSep1ActionID, kKCMUIPrefix + 17)	// フライアウト: Start の下の区切り線(MenuDef のパス末尾 ":-"。ActionDef 不要・DoAction 不要=一意なIDだけ要る)
DECLARE_PMID(kActionIDSpace, kKESCMPopupSep2ActionID, kKCMUIPrefix + 18)	// フライアウト: How to Use の上の区切り線
DECLARE_PMID(kActionIDSpace, kKESCMPopupHoldToHideMarksActionID, kKCMUIPrefix + 19)	// パネルのフライアウトの「Hold to Hide Marks」チェック式トグル(ON=枠を画面に常時表示し、ツール左hold中だけ隠す=極性反転。画面のみ・印刷は Print comparison marks が別管理。KESCMActionComponent.cpp)
// kKESCMPopupPanelShortcutActionID (kKCMUIPrefix + 20) は中ボタン撤去(2026-07-13)に伴い廃止。スロットを 2026-07-24 に再利用:
DECLARE_PMID(kActionIDSpace, kKESCMPopupAlignViewsActionID, kKCMUIPrefix + 20)	// パネルのフライアウトの「Align Other Views to Active」(実行アクション)。アクティブ(最前面)文書のビューの位置+拡大率を他文書のビューへ1回そろえる。Start中はページのAdd/Remove補正あり。ショートカット割当可(kKESCMPanelMenuActionArea+VisibleInKBSC)。実体 KESCMPeek.cpp の KESCMAlignOtherViewsToActiveNow
DECLARE_PMID(kActionIDSpace, kKESCMPopupScrollMapActionID, kKCMUIPrefix + 21)	// パネルのフライアウトの「Show Scrollbar Map」チェック式トグル(ON=文書窓の縦スクロールバー脇に変更位置地図stripを表示。既定ON。実体 KESCMScrollMap.cpp の sScrollMapOn)
DECLARE_PMID(kActionIDSpace, kKESCMPopupSavePanelStateActionID, kKCMUIPrefix + 22)	// パネルのフライアウトの「Save Panel Settings」(チェックではなく実行アクション)。現在の設定系トグルを独自JSONでローカルへ保存し保存先パスを表示。読込は起動時(KESCMPeekStartup::Startup。2026-07-15 前倒し)。実体 KESCMPanelState.cpp
DECLARE_PMID(kActionIDSpace, kKESCMPopupSep3ActionID, kKCMUIPrefix + 23)	// フライアウト: Refresh Overset の下(9.50)の区切り線(MenuDef のパス末尾 ":-"。ActionDef 不要)。現配置は下の位置一覧(9.50)が正(2026-07-25 コメント現行化)
DECLARE_PMID(kActionIDSpace, kKESCMPageCheckToggleActionID, kKCMUIPrefix + 24)	// ページパネルのページ右クリック(RtMenuPagesPanel)のトグル「KCM: Check」(選択ページに✓印を付け外し。Start中限定・Stopで消去。チェック/有効無効は kCustomEnabling。実体 KESCMPageCheck.cpp、✓描画は KESCMDrawEventHandler の isThumb 分岐)
DECLARE_PMID(kActionIDSpace, kKESCMPopupSaveChecksActionID, kKCMUIPrefix + 25)	// パネルのフライアウトの「Save Check & Register」(実行アクション)。Start中の Target/Source の現在の Check(✓)+ Register(Added/Removed)を独自JSON(KESCM\KESCMPageChecks.json, v2)へマージ保存し保存先パスを表示。実体 KESCMPageCheck.cpp
DECLARE_PMID(kActionIDSpace, kKESCMPopupLoadChecksActionID, kKCMUIPrefix + 26)	// パネルのフライアウトの「Load Check & Register」(実行アクション)。Start中だけ有効。上記JSONから Register を両文書へ適用→再比較→Check(今もマーク付きのみ)を復元。実体 KESCMPageCheck.cpp
// kKESCMPopupPagesPanelShortcutActionID (kKCMUIPrefix + 27) は中ボタン撤去(2026-07-13)に伴い「Invoke Pages Panel Shortcut」トグルごと廃止。スロットは予約のまま。
DECLARE_PMID(kActionIDSpace, kKESCMPageMapSepActionID, kKCMUIPrefix + 28)	// ページパネルのページ右クリック(RtMenuPagesPanel): KESCM 追加項目(Register / Check)の上の区切り線(MenuDef のパス末尾 ":-"。ActionDef 不要・DoAction 不要=一意なIDだけ要る)。本家メニューと視覚的に分けるため
DECLARE_PMID(kActionIDSpace, kKESCMToolActionID, kKCMUIPrefix + 29)	// ツールボックスのツール選択ショートカット用の ActionID(ToolDef が参照。ActionDef 不要=ツール枠が自動生成)
DECLARE_PMID(kActionIDSpace, kKESCMPageRefreshCompareActionID, kKCMUIPrefix + 30)	// ページパネルのページ右クリック(RtMenuPagesPanel)の実行アクション「KCM: Refresh Page Comparison」(選択ページの比較を再検出して枠/サムネイルを更新。旧 Ctrl+ミドルのスプレッド再比較を移設。Start中限定・kCustomEnabling。実体 KESCMPeek.cpp の KESCMRefreshComparisonForSelectedPages)
DECLARE_PMID(kActionIDSpace, kKESCMPopupFindOversetActionID, kKCMUIPrefix + 31)	// パネルのフライアウトの「Find Overset」チェック式トグル(ON=アクティブ文書を走査し overset のあるページに大きな十字を表示。比較と独立・単独点検。kCustomEnabling。実体 KESCMActionComponent.cpp/KESCMOversetScan.cpp)
DECLARE_PMID(kActionIDSpace, kKESCMPopupRefreshOversetActionID, kKCMUIPrefix + 32)	// パネルのフライアウトの「Refresh Overset」(実行アクション)。Find Overset が ON のときだけ有効(OFF時は灰色)=アクティブ文書を再走査して十字を貼り直す。kCustomEnabling
DECLARE_PMID(kActionIDSpace, kKESCMPopupOversetSepActionID, kKCMUIPrefix + 33)	// フライアウト: Find Overset 群の上の区切り線(MenuDef のパス末尾 ":-"。ActionDef 不要・DoAction 不要=一意なIDだけ要る)
DECLARE_PMID(kActionIDSpace, kKESCMPopupExportChangedPagesActionID, kKCMUIPrefix + 34)	// パネルのフライアウトの「Export Changed Pages...」(実行アクション)。比較中(sDB≠nil)のみ有効=現在の比較の変更ページ一覧をTSV(新/旧/種別)で保存。実体 KESCMChangedPagesTSV.cpp
// (+35 = kKESCMPopupHudActionID「Show HUD」は 2026-08-06 に機能ごと撤去。★**この番号は再利用しない**
//  ＝ショートカット割当は .indk に ActionID の数値で保存されるので、別機能に振り直すと古い割当が
//  その機能を叩いてしまう)
DECLARE_PMID(kActionIDSpace, kKESCMPopupTranslucentPanelActionID, kKCMUIPrefix + 36)	// パネルのフライアウトの「Translucent Panel」チェック式トグル(ON=フローティング中のこのパネルを半透明にする。★Windows 専用・★ドッキング中は選べるが効かない(フラグだけ立ちフローティングに戻すと効く)。既定 OFF。実体 KESCMPanelAlpha.cpp)
DECLARE_PMID(kActionIDSpace, kKESCMPopupTranslucentPagesActionID, kKCMUIPrefix + 37)	// パネルのフライアウトの「Translucent Pages Panel」チェック式トグル(ON=フローティング中の**本体のページパネル**を半透明にする。上の +36 と同じ仕組みで対象だけが違う=WidgetID(kPagesPanelWidgetID)で狙い撃ちする。★Windows 専用・★ドッキング中は選べるが効かない。既定 OFF。実体 KESCMPanelAlpha.cpp)
// ★kKCMUIPrefix + 38 は「Translucent Toolbox」トグルの跡地(2026-08-07 に追加し、同日ユーザー判断で撤去)。
//   **番号は再利用しない** = ショートカット設定(.indk)はアクションを数値の ActionID で保存するので、
//   割り当て済みの番号を別機能へ回すと、そのショートカットが無関係な機能を叩く。押下中 HUD を撤去した
//   ときの +35 とまったく同じ扱い。
DECLARE_PMID(kActionIDSpace, kKESCMPopupCompareBooksActionID, kKCMUIPrefix + 39)	// パネルのフライアウトの「Compare Books」(実行アクション)。★ブックパネルで前面タブのブック=Target / それ以外で最初に開いているブック=Source として、章(ドキュメント)単位で「変更あり/なし」を判定する。★既存の文書比較(Start)とは完全に独立=arm しない・枠を作らない・KESCMDrawEventHandler の static を触らない。kCustomEnabling(2ブックそろい、かつ前面タブが特定できるときだけ有効)。実体 KESCMBookCompare.cpp / 対象の解決 KESCMBookPair.cpp
// ブック比較ダイアログの章行の右クリックメニュー(2026-08-12)。★パネルのフライアウトではなく
// **行の上に出るポップアップ**なので、置き場所は MenuDef のサブツリー kKESCMBookRowMenuName。
// 押されたときに「どの行か」を知る手段はこのアクション自身には無い(ActionID しか渡らない)ので、
// 行は右クリックの時点で KESCMBookSetMenuRow が控える＝KBS の結果行と同じ作り。
DECLARE_PMID(kActionIDSpace, kKESCMBookRowStartActionID, kKCMUIPrefix + 40)	// 章行の右クリック「Start Change Marker」= その章の Target/Source 2文書を窓付きで開き、比較中なら一度 Stop してから比較を開始する(KESCMBookOpen.cpp)。★両側のファイルが揃っていない行(ChapterAdded/ChapterDeleted・ファイル無し)では灰色(kCustomEnabling → KESCMBookRowCanStart)
DECLARE_PMID(kActionIDSpace, kKESCMPopupTranslucentBookDialogActionID, kKCMUIPrefix + 41)	// ★パネルのフライアウトの「Translucent Book Dialog」チェック式トグル(2026-08-13 ユーザー要望「ダイアログも半透明に出来る様に」)。上の +36/+37 と**同じ実体**(KESCMPanelAlpha.cpp)で対象だけが違う。⚠ただし対象がパネルではないので**窓の出所だけが別**＝ダイアログ自身が KESCMSetBookDialogWindow で教える(パネルマネージャは WidgetID で引けるが、ダイアログはそこに載っていない)。★パネル側と違い**ドッキングの概念が無いので「押しても効かない状態」が無い**。既定 OFF
// (+15..+23 are all declared above - stale placeholders for them removed 2026-08-05 audit. Next free: +42)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKCMUIPrefix + 41)
// kKCMUIPrefix + 24/25/26/28 は使用中(KCM: Check / Save Check & Register / Load Check & Register / RtMenuPagesPanel の区切り線)。+27 は廃止・予約(上記)


// WidgetIDs:
DECLARE_PMID(kWidgetIDSpace, kKESCMPanelWidgetID, kKCMUIPrefix + 0)
DECLARE_PMID(kWidgetIDSpace, kKESCMTargetTextWidgetID, kKCMUIPrefix + 1)
DECLARE_PMID(kWidgetIDSpace, kKESCMSourceTextWidgetID, kKCMUIPrefix + 26)
// kWidgetIDSpace +27 は現在空き(旧 kKESCMStartButtonWidgetID; 開始/解除を kKESCMToggleButtonWidgetID に統合)
// kWidgetIDSpace +28 は現在空き(旧 kKESCMClearButtonWidgetID; 同上)
// kWidgetIDSpace +29 は現在未使用(旧 kKESCMPrintCheckWidgetID; 印刷ON/OFF チェックボックスは 2026-07-10 に
//   フライアウト「Print comparison marks」メニュー項目=kKESCMPopupPrintMarksActionID へ移行しパネルから撤去)
// kWidgetIDSpace +30〜+32 は現在未使用(旧 kKESCMOpacityClusterWidgetID / kKESCMOpacity25RadioWidgetID /
//   kKESCMOpacity75RadioWidgetID; 不透明度 25%/75% ラジオは 2026-07-10 にフライアウト
//   kKESCMPopupOpacity25ActionID / kKESCMPopupOpacity75ActionID へ移行しパネルから撤去)
// kWidgetIDSpace +33 は現在空き(旧 kKESCMHintTextWidgetID; 説明文はパネルから撤去しフライアウト「使い方」へ移動)
DECLARE_PMID(kWidgetIDSpace, kKESCMIconOnWidgetID, kKCMUIPrefix + 34)
DECLARE_PMID(kWidgetIDSpace, kKESCMIconOffWidgetID, kKCMUIPrefix + 35)
DECLARE_PMID(kWidgetIDSpace, kKESCMStatusTextWidgetID, kKCMUIPrefix + 36)
// kWidgetIDSpace +37 は 2026-07-15 に kKESCMNavPosTextWidgetID として再利用(旧 kKESCMToggleButtonWidgetID;
//   開始/解除ボタンは 2026-07-10 にフライアウト「Start / Stop」項目=kKESCMPopupStartStopActionID へ移行済み)
DECLARE_PMID(kWidgetIDSpace, kKESCMNavPosTextWidgetID, kKCMUIPrefix + 37)	// Prev/Next の間に出す現在位置「3/12」(中央揃え StaticText。KESCMChangeNav.cpp が KESCMSetNavPosition で更新)
DECLARE_PMID(kWidgetIDSpace, kKESCMPrevChangeButtonWidgetID, kKCMUIPrefix + 38)	// 「◀ Prev」= 前の見るべきページへスクロール(KESCMChangeNav.cpp)
DECLARE_PMID(kWidgetIDSpace, kKESCMNextChangeButtonWidgetID, kKCMUIPrefix + 39)	// 「Next ▶」= 次の見るべきページへスクロール(KESCMChangeNav.cpp)
DECLARE_PMID(kWidgetIDSpace, kKESCMScrollMapWidgetID, kKCMUIPrefix + 40)	// スクロールバー地図strip(文書窓の縦スクロールバー左隣に実行時注入; KESCMScrollMap.cpp)
DECLARE_PMID(kWidgetIDSpace, kKESCMToolWidgetID, kKCMUIPrefix + 41)	// ツールボックスのツールボタンのウィジェットID(KESCMTool::InitWidget)
DECLARE_PMID(kWidgetIDSpace, kKESCMToolButtonWidgetID, kKCMUIPrefix + 42)	// ★パネル内のツール切替ボタン(2026-08-07 追加。Prev の左・32x22)。押すと kKESCMToolBoss をアクティブツールにする(KESCMActivateOwnTool)。上の +41 とは別物＝あちらはツールボックス側のツール枠
// ★「Story Edits」セクション(2026-08-09 追加)。パネルを SplitterPanelWidget で上下に割り、下ペインに
//   「テキストが編集されたストーリー」の一覧を出す(段階3)。手本は製品 linksui の「リンク情報」セクション。
DECLARE_PMID(kWidgetIDSpace, kKESCMSplitterWidgetID, kKCMUIPrefix + 43)			// パネルを上下に割る SplitterPanelWidget(Widgets.fh:462 / kSplitterPanelWidgetBoss)
DECLARE_PMID(kWidgetIDSpace, kKESCMTopPaneWidgetID, kKCMUIPrefix + 44)			// 上ペイン=従来のパネル内容一式を丸ごと収めた GenericPanelWidget。★splitter の「伸縮させない方」に指定する
DECLARE_PMID(kWidgetIDSpace, kKESCMStorySectionWidgetID, kKCMUIPrefix + 45)		// 下ペイン=Story Edits 本体(初期は非表示。中身＝列見出しの帯＋罫線＋一覧ツリー)
DECLARE_PMID(kWidgetIDSpace, kKESCMStorySectionToggleWidgetID, kKCMUIPrefix + 46)	// 開閉ボタン(三角)。★上ペインの中に置く=下ペインに置くと閉じたときボタンごと消えて開けなくなる
DECLARE_PMID(kWidgetIDSpace, kKESCMStoryTreeWidgetID, kKCMUIPrefix + 47)		// Story Edits の一覧ツリー本体(下ペインいっぱい)
DECLARE_PMID(kWidgetIDSpace, kKESCMStoryRowTextWidgetID, kKCMUIPrefix + 48)	// 行の左=本文の先頭テキスト
DECLARE_PMID(kWidgetIDSpace, kKESCMStoryRowKindWidgetID, kKCMUIPrefix + 49)	// 行の右=変わった種類(Text / Attr / Other / Added)
DECLARE_PMID(kWidgetIDSpace, kKESCMStorySectionLabelWidgetID, kKCMUIPrefix + 50)	// 上ペインの三角の隣=「Story Edits (3)」。件数は C++ が実行時に付ける
DECLARE_PMID(kWidgetIDSpace, kKESCMStoryRowWidgetID, kKCMUIPrefix + 51)		// 行テンプレート自身。★GetWidgetTypeForNode が返すのはこれ
DECLARE_PMID(kWidgetIDSpace, kKESCMStoryRowUIDWidgetID, kKCMUIPrefix + 52)	// ★行の左端=ストーリーの UID(10進。2026-08-10 ユーザー要望「UID・テキスト・変更部分」)。行の同一性を目で追える識別子＝本文が同じ文言でも別のストーリーだと分かる
// ★一覧の列見出し(2026-08-10 ユーザー要望「一番上の列に UID / Text / 変更のようなのを付けて欲しい」)。
//   ツリーの中ではなく**下ペインの中でツリーの上**に置く固定の帯＝行をスクロールしても動かない。
//   ★3つとも行のセルと**同じ x 座標・同じ binding**を与えてある(KESCM.fr)。それが列が揃い続ける唯一の
//   保証で、片方だけ動かすと可変幅パネルでずれる。
DECLARE_PMID(kWidgetIDSpace, kKESCMStoryHeaderUIDWidgetID, kKCMUIPrefix + 53)	// 見出しの左「UID」(行の kKESCMStoryRowUIDWidgetID と同じ 8〜48・kBindLeft)
DECLARE_PMID(kWidgetIDSpace, kKESCMStoryHeaderTextWidgetID, kKCMUIPrefix + 54)	// 見出しの中「Story」(行の kKESCMStoryRowTextWidgetID と同じ 52〜154・kBindLeft|kBindRight＝広げるとここだけ伸びる)
DECLARE_PMID(kWidgetIDSpace, kKESCMStoryHeaderKindWidgetID, kKCMUIPrefix + 55)	// 見出しの右「Change」(行の kKESCMStoryRowKindWidgetID と同じ 154〜216・kBindRight・右寄せ)
DECLARE_PMID(kWidgetIDSpace, kKESCMStoryHeaderRuleWidgetID, kKCMUIPrefix + 56)	// 見出しと一覧を分ける 1px の罫線＝**ErasablePrimaryResourcePanelWidget を高さ1pxで置き kInterfaceSeparatorColor で erase**(その塗りが線)。⚠stock の RuleWidget(Widgets.fh:887 / kRuleWidgetBoss)を先に試したが**パースもビルドも通って何も描かなかった**ので差し替えた(.fr 側に全文)

// ブック比較ダイアログ(2026-08-11)。★OK/Cancel は stock の WidgetID(kOKButtonWidgetID /
// kCancelButton_WidgetID)を使うので、ここに要るのはダイアログ本体だけ。
DECLARE_PMID(kWidgetIDSpace, kKESCMBookDialogWidgetID, kKCMUIPrefix + 57)
DECLARE_PMID(kWidgetIDSpace, kKESCMBookTargetTextWidgetID, kKCMUIPrefix + 58)	// 「Target: new.indb」(前面タブのブック)
DECLARE_PMID(kWidgetIDSpace, kKESCMBookSourceTextWidgetID, kKCMUIPrefix + 59)	// 「Source: old.indb」(それ以外で最初に開いているブック)
DECLARE_PMID(kWidgetIDSpace, kKESCMBookCompareButtonWidgetID, kKCMUIPrefix + 60)	// 「Compare」ボタン。★押す前に上の2行が目に入るのが要点
DECLARE_PMID(kWidgetIDSpace, kKESCMBookStatusTextWidgetID, kKCMUIPrefix + 61)	// ステータス行(比較の要約。章数を必ず含む)
DECLARE_PMID(kWidgetIDSpace, kKESCMBookTreeWidgetID, kKCMUIPrefix + 62)		// 章一覧のツリー本体(ダイアログの中で一番大きい部品)
// ★★**番号が +2 なのは書き間違いではない**(2026-08-13)。ダイアログのメッセージを2行にする(2行目＝行の
//   右クリックの案内)ために widget を1つ足したが、**+63 から先は KESCL の prefix 領域**(0x205554)なので
//   後ろへは伸ばせない。一方 **+2〜+25 は宣言ごと空いていた**(下のプレースホルダ群。+1 の次は +26)＝
//   予約済み 256 枠の内側の未使用番号なので、**ID 空間の消費はゼロで衝突も無い**。
//   ⇒ 逃げ道は「借用(同じ親の子孫でだけ一意)」だけではなく、**前方の穴**もある＝[[id-prefix-256-slot-budget]]
DECLARE_PMID(kWidgetIDSpace, kKESCMBookHintTextWidgetID, kKCMUIPrefix + 2)	// ステータスの2行目=「Right-click a changed chapter to start Change Marker.」(固定文。.fr が初期テキストとして持ち、C++ は触らない)
// ★★★下の3つは「Story Edits」の行と **WidgetID を共有する**(2026-08-12。新しい番号を1つも使わない)。
//   根拠 = **widget ID がアプリ全体で一意である必要は無い**。公式ガイド vol2-12:71 が、
//   グローバルに一意でなければならない文字列キーと**対比して**こう書いている:
//     "widget identifiers need be unique only within the list of descendants of a given widget,
//      so ... you can reuse a widget identifier (for example, across different dialog boxes or
//      panels you own)"
//   機構の裏づけ = IPanelControlData::FindWidget(WidgetID, searchLevels) は**自分の部分木を再帰探索
//   するだけ**で、グローバルな「widget ID → widget」レジストリではない(IPanelControlData.h:89)。
//   実例 = stock の kOKButtonWidgetID / kCancelButton_WidgetID を source/sdksamples の **39 ファイル**が
//   共有している(上の +57 のブロックで既にそれを使っている)。
//   ここでは「パネルの Story Edits 一覧」と「このダイアログの章一覧」が**別の部分木**に属するので衝突しない。
//   ⇒ これで **+63〜+65 の KESCL 領域(0x205554/55/56)への食い込みが消えた**(下の採番メモ参照)。
//   ⚠ 共有してよいのは**子 widget だけ**。パネル/ダイアログの**ルート**の WidgetID は
//     IPanelMgr::GetPanelFromWidgetID がアプリ全体から引くので一意でなければならない。
//   ⚠ 対応関係は役割まで一致させてある(行テンプレ↔行テンプレ / 左セル↔左セル / 右セル↔右セル)。
//     ずらすと読む側が混乱するだけで、得は何も無い。
DECLARE_PMID(kWidgetIDSpace, kKESCMBookRowWidgetID, kKCMUIPrefix + 51)		// ＝kKESCMStoryRowWidgetID と同値。行テンプレート自身。★GetWidgetTypeForNode が返すのはこれ
DECLARE_PMID(kWidgetIDSpace, kKESCMBookRowNameWidgetID, kKCMUIPrefix + 48)	// ＝kKESCMStoryRowTextWidgetID と同値。行の左=章のファイル名(Failed のときだけ「 - 理由」が後ろに付く)
DECLARE_PMID(kWidgetIDSpace, kKESCMBookRowStateWidgetID, kKCMUIPrefix + 49)	// ＝kKESCMStoryRowKindWidgetID と同値。行の右=判定(Changed / NoChange / ChapterAdded / ChapterDeleted / Failed)。固定幅・右寄せ
//====================================================================================
// ★★採番の上限に注意 — この prefix で使えるのは「+0 〜 +127」(★ID 空間ごとに 128 枠)
//
//  ⚠★★2026-08-15(第2段 Task 6B-2)に**書き換えた**。この節は 2026-08-13 まで
//    「KESCM(0x205515) と KESCL(0x205554) の間隔が 0x3F=63 しかないので **+63 以降は KESCL の領域**」
//    と書いていたが、**その制約はもう存在しない** ---- 2026-08-13 に Adobe が正規の帯を発行し、
//    prefix ごと引っ越したため。旧文面のまま運ぶと、無い制約に縛られて (a)〜(c) を無駄に検討することになる。
//
//  Adobe が発行した帯は **0x1EA500 - 0x1EA5FF の 256 枠**で、それを前半・後半に割ってある:
//      model(KESCM) = 0x1EA500 ／ **UI(KCMUI) = 0x1EA580 ＝このファイル**
//  ∴ kKCMUIPrefix から採れるのは **+127 まで**。
//  ⚠ **+128 は 0x1EA600 ＝ KBS が使っている帯**(2026-08-15 に KBS もこの正規帯へ移った)。そこへ
//    届いた瞬間に「2つのプラグインが同じ ID を主張」する状態になり、起動時のオブジェクトモデル構築
//    ("Completing object model" 段)で **弾かれる**(＝静かな誤動作ではなく、読み込まれない)。
//
//  ★2026-08-15 現在の最大オフセットは **WidgetID の +62**(kKESCMBookTreeWidgetID)＝**半分以上空いている**。
//    ただし下の (a) は今でも最初に検討する価値がある(ID 空間を1つも消費しないため)。
//
//  widget を足すときは、この順に検討する:
//
//    (a) ★★★**そもそも新しい番号を採らない** — **widget ID がアプリ全体で一意である必要は無い**。
//        「**同じ親の子孫の中でだけ**一意」ならよく、**別のダイアログ/パネルでは再利用してよい**
//        (公式ガイド vol2-12:71 が、グローバル一意が必須の文字列キーと**対比して**明言)。
//        機構 = IPanelControlData::FindWidget は**自分の部分木を再帰探索するだけ**(:89)。
//        実例 = stock の kOKButtonWidgetID を source/sdksamples の **39 ファイル**が共有。
//        ⇒ **ID 空間を1つも消費しない**ので、まずこれを検討する。
//        ⚠ ただし**ルート**の WidgetID は IPanelMgr::GetPanelFromWidgetID がアプリ全体から引くので
//          一意でなければならない(パネル本体・ダイアログ本体は再利用不可)。
//        ⚠ widget は状態を saved data に永続するので、**初めて再利用する形を入れたら実機で確認する**。
//
//    (b) **+27 〜 +33 の穴(7個)** — 現在どこからも使われていない。
//        ⚠ 過去に使って消した番号なので、再利用の前に git 履歴を見ること
//        (widget ID はワークスペース＝パネル配置の永続データに現れうる)
//
//    (c) それでも足りなくなったら **セカンダリ prefix を1本用意する**(既存 ID は1つも動かさない。
//        公開済みバージョンとの互換を保ったまま 256 枠を新規に確保できる)。正攻法は
//        wwds@adobe.com に prefix ID の発行を依頼すること。
//
//  調査の全記録 = docs/ai-notes/guide-gs-04-object-model-read-2026-08-12.md §1
//                 docs/ai-notes/guide-vol2-12-ui-fundamentals-read-2026-08-12.md §0 ((a) の根拠)
//====================================================================================
// (+2 is IN USE since 2026-08-13 - kKESCMBookHintTextWidgetID, declared with the dialog's widgets above.)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKCMUIPrefix + 3)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKCMUIPrefix + 4)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKCMUIPrefix + 5)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKCMUIPrefix + 6)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKCMUIPrefix + 7)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKCMUIPrefix + 8)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKCMUIPrefix + 9)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKCMUIPrefix + 10)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKCMUIPrefix + 11)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKCMUIPrefix + 12)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKCMUIPrefix + 13)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKCMUIPrefix + 14)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKCMUIPrefix + 15)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKCMUIPrefix + 16)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKCMUIPrefix + 17)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKCMUIPrefix + 18)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKCMUIPrefix + 19)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKCMUIPrefix + 20)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKCMUIPrefix + 21)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKCMUIPrefix + 22)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKCMUIPrefix + 23)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKCMUIPrefix + 24)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKCMUIPrefix + 25)

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
// Other StringKeys:
#define kKESCMAboutBoxStringKey	kKESCMStringPrefix "kKESCMAboutBoxStringKey"
#define kKESCMRepoURL			"https://github.com/KohakuNekotarou/KohakuChangeMarker"// 配布元URL。「このプラグインについて」本文とパネルのイラストクリックの飛び先で共通
// (kKESCMAboutScriptMenuKey / kKESCMScriptHelpStringKey は「About Scripting」撤去(2026-07-25)に伴い削除)
#define kKESCMUsageMenuKey		kKESCMStringPrefix "kKESCMUsageMenuKey"	// パネルのフライアウト「使い方」のメニュー名(本文は kKESCMHintKey を再利用)
#define kKESCMHideUnchangedMenuKey	kKESCMStringPrefix "kKESCMHideUnchangedMenuKey"	// パネルのフライアウト「Hide Unchanged Spreads」トグルのメニュー名
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
#define kKESCMCompareBooksMenuKey	kKESCMStringPrefix "kKESCMCompareBooksMenuKey"	// パネルのフライアウト「Compare Books」項目のメニュー名(ブック同士を章単位で比較)
#define kKESCMBookDialogTitleKey	kKESCMStringPrefix "kKESCMBookDialogTitleKey"	// ブック比較ダイアログのタイトル
#define kKESCMBookCompareKey		kKESCMStringPrefix "kKESCMBookCompareKey"		// (退役 2026-08-12)旧「Compare」ボタンのラベル。ボタンごと撤去したので参照は無いが、enUS テーブルの行とともに残してある＝復活させるとき対で戻せる
#define kKESCMBookReadyKey			kKESCMStringPrefix "kKESCMBookReadyKey"			// 比較前のステータス文。★2026-08-12 以降ここに来るのは「比較を1度もしていないのにダイアログが開いた」場合だけ(通常は結果の要約で上書きされる)
#define kKESCMBookHintKey			kKESCMStringPrefix "kKESCMBookHintKey"			// ★ステータスの2行目(2026-08-13 ユーザー指示)=行の右クリックで比較を始められることの案内。**固定文**なので .fr が初期テキストとして持ち、C++ は一度も書き換えない(要約と違い run ごとに変わらないため)
// ★ブック比較の確認アラート(2026-08-12 ユーザー指示「Compare... をするとアラートを出し、OK が押されると比較する」)。
//   ⚠★**2026-08-13 訂正**＝旧記述「ここは日本語 UI では日本語で出す＝KESCMLoc の対象が4箇所へ増えた」は
//     **下の確認キーについては失効**。ユーザー指示「英語で良いです」で確認アラートは全ロケール英語に戻り、
//     KESCMLoc の対象は3箇所になった。**日本語のままなのは次の kKESCMBookNoPairKey だけ**。
#define kKESCMBookCompareConfirmKey	kKESCMStringPrefix "kKESCMBookCompareConfirmKey"	// 「これから比較する」確認アラートの1行目(この下に target: / source: のフルパスが続く)
#define kKESCMBookNoPairKey			kKESCMStringPrefix "kKESCMBookNoPairKey"			// 2ブックを解決できなかったときの警告アラート(通常はメニューが灰色なので到達しない)
#define kKESCMBookRowStartMenuKey	kKESCMStringPrefix "kKESCMBookRowStartMenuKey"	// 章行の右クリックメニューの「Start Change Marker」項目名
// 章行の右クリックメニュー(2026-08-12)。この名前の MenuDef サブツリーを KESCMBookRowEH::RButtonDn が
// IMenuManager::HandlePopupMenu でカーソル位置に出す＝製品の Links / Layers パネルの行メニューと
// 同じ機構で、KBS の結果行(kKBSResultRowMenuName)・KESCL のレポート行と同じ作り。
// ★この根の名前は画面に出ないので、翻訳キーではなく素のリテラルでよい。
#define kKESCMBookRowMenuName		"KESCMRtMenuBookRow"
#define kKESCMTranslucentPanelMenuKey	kKESCMStringPrefix "kKESCMTranslucentPanelMenuKey"	// パネルのフライアウト「Translucent Panel」トグルのメニュー名
#define kKESCMTranslucentPagesPanelMenuKey	kKESCMStringPrefix "kKESCMTranslucentPagesPanelMenuKey"	// パネルのフライアウト「Translucent Pages Panel」トグルのメニュー名(対象は本体のページパネル)
#define kKESCMTranslucentBookDialogMenuKey	kKESCMStringPrefix "kKESCMTranslucentBookDialogMenuKey"	// パネルのフライアウト「Translucent Book Dialog」トグルのメニュー名(対象はブック比較ダイアログ。2026-08-13 追加)
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

// Story Edits セクションの文字列。⚠文言に "text" を使わない——この一覧はテキスト以外の変更も載せる
// ので "No text edits" のような言い方は事実と食い違う(設計書 §3-5)。
#define kKESCMStorySectionLabelKey	kKESCMStringPrefix "kKESCMStorySectionLabelKey"	// セクション見出し(件数は C++ が付ける)
#define kKESCMStoryNoEditsKey		kKESCMStringPrefix "kKESCMStoryNoEditsKey"		// 変更0件のときに出す1行
#define kKESCMStoryKindTextKey		kKESCMStringPrefix "kKESCMStoryKindTextKey"		// 行の右=文字が変わった
#define kKESCMStoryKindAttrKey		kKESCMStringPrefix "kKESCMStoryKindAttrKey"		// 行の右=属性が変わった(適用スタイル・オーバーライド・表の罫線を含む)
#define kKESCMStoryKindOtherKey		kKESCMStringPrefix "kKESCMStoryKindOtherKey"	// 行の右=上記以外(実測では出にくい。KESCMStoryStamp.h 参照)
#define kKESCMStoryKindAddedKey		kKESCMStringPrefix "kKESCMStoryKindAddedKey"	// 行の右=Source 側に相手が無い

// 一覧の列見出し(2026-08-10)。★中の語をそのまま使わない: 2列目の見出しは "Text" ではなく "Story"、
// 3列目は "Kind" ではなく "Change"(ユーザー指定)。理由は語の衝突——3列目に出る**値**が "Text" なので、
// 2列目の見出しを "Text" にすると同じ語が1行の中で別の意味で2回出る。
#define kKESCMStoryColUIDKey		kKESCMStringPrefix "kKESCMStoryColUIDKey"		// 見出し左=ストーリーの UID
#define kKESCMStoryColTextKey		kKESCMStringPrefix "kKESCMStoryColTextKey"		// 見出し中=本文の書き出し
#define kKESCMStoryColKindKey		kKESCMStringPrefix "kKESCMStoryColKindKey"		// 見出し右=変わった種類

// PNG アイコンリソース(プラグインに埋め込み; .pln とは別ファイルでは出荷しない)。
#define kKESCMIconOnResID	1001
#define kKESCMIconOffResID	1002
#define kKESCMPaletteIconResID	1003	// パネルが折りたたまれた時に出る小さいドックタブアイコン

// スクロールバー地図stripのビューリソースID(kViewRsrcType; ::CreateObject で実行時生成する。KESCMScrollMap.cpp)
#define kKESCMScrollMapRsrcID	1010

// Story Edits の行テンプレートのビューリソースID(kViewRsrcType; CreateObjectNoInit で1行ずつ生成する。
// KESCMStoryTreeWidgetMgr.cpp)。
#define kKESCMStoryRowRsrcID	1011

// ブック比較ダイアログのビューリソースID(kViewRsrcType)。KESCMBookDialog.cpp が RsrcSpec で指す。
// ★1010/1011 と同じ採番の続き。手本=KESCL の Jump Offset ダイアログ(あちらは kSDKDefDialogResourceID)。
#define kKESCMBookDialogRsrcID	1012

// ブック比較ダイアログの章一覧の、行テンプレートのビューリソースID(kViewRsrcType;
// CreateObjectNoInit で1行ずつ生成する。KESCMBookTreeWidgetMgr.cpp)。★1011(Story Edits の行)と
// 同じ作りで、違うのは中身が2列であることとダイアログ用のフォントを使うことだけ。
#define kKESCMBookRowRsrcID	1013

// 章一覧の行の高さ。★Story Edits の kKESCMStoryRowHeight と同じく .fr と C++ の両方がこの1つの定数を
// 読む(行リソースの Frame・ツリーのスクロール増分・GetNodeWidgetHeight)。
// ★下の 19 と違って、こちらは SDK 標準の kCC2016PanelTreeNodeHeight と同じ 22
// (StdHeightWidthConstants.h:50)。パレットの一覧が 19 なのは**パレットフォントを実測して決めた値**
// (2026-08-11)で、ダイアログのフォントは測っていない＝測っていない側では標準に従う。製品の
// AutoCorrect 環境設定のリストも、ダイアログの中の一覧をこの定数で組んでいる
// (AutoCorrectPrefsPanel_enUS.fr:288)。
#define kKESCMBookRowHeight	22

// 一覧の行の高さ。★.fr と C++ の両方がこの1つの定数を読む(Adobe の StdHeightWidthConstants.h と同じ形)
// ＝行リソースの Frame・ツリーのスクロール増分・GetNodeWidgetHeight が同じ事実を語る。値は KBS の
// kKBSResultRowHeight と同じ 19＝パレットフォントでの実測値で、SDK の kCC2016PanelTreeNodeHeight(=22)ではない。
#define kKESCMStoryRowHeight	19

// 一覧の列見出しの帯の高さ(ラベル 14px ＋ 罫線 1px ＋ 上下の余白 3px。2026-08-10)。
// ★行高と同じく .fr と C++ の両方がこの1つの定数を読む＝帯を厚くすれば、ツリーの位置も
//   セクションの最小・既定の高さも同時に動く(KESCM.fr / KESCMStorySection.cpp)。
#define kKESCMStoryHeaderHeight	18

// ★★パネルの最小サイズ(2026-08-10 ユーザー指定「今のを最小の設定で、パネルの大きさは固定ではなく」)。
//   PanelList を kIsResizable にしたので、下限を守るのは KESCMPanelView::ConstrainDimensions。
// ・幅 = これまでの固定幅そのまま。中の widget はすべて縁に束縛してあるので広げる方向は自由に伸びるが、
//   これより狭めるとステータス欄が読めなくなり、一覧の行は省略記号だけになる。
// ・高さ = 上ペインの設計高。★Story Edits を**閉じている間はこれが上限でもある**——閉じているときの
//   パネルは固定座標のコントロール群だけなので、伸ばしても下に空白の帯ができるだけになる。
//   開いている間の下限は「上ペイン + セクションの最小(= .fr の Bottom snap)」で、C++ 側は
//   分割バーに実際の snap 値を聞く(数字を2か所に書かない)。
// ★2026-08-10: 173 → 185。Story Edits の帯に猫イラストを下ろした分(20px の帯では 32×32 の絵が
//   収まらない)。帯が 12px 高くなり、代わりにステータス欄が右端まで(180→216)伸びた。
#define kKESCMPanelMinWidth		224
#define kKESCMPanelTopPaneHeight	185

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
//   Start/Stop(9.0) → Compare Books(9.05) → ─線Sep1(9.1) →
//   [表示系トグル群] Hold to Hide Marks(9.20) → Ignore Page Number Marker(9.22) → Marks opacity ▸(9.24) →
//     Print comparison marks(9.26) → Show Original Page Numbers(9.28) →
//     Show Marks on Source(9.30) → Show Scrollbar Map(9.32) → Sync Layout Views(9.34) →
//     Translucent Panel(9.36) →
//   ─線OversetSep(9.40) → Find Overset(9.42) → Refresh Overset(9.44) →
//   ─線Sep3(9.50) → [実行アクション群] Align Other Views to Active(9.52) → Export Changed Pages...(9.53) →	★Compare Books はここに居たが 2026-08-12 に Start の直下へ移した
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
#define kKESCMTranslucentBookDialogMenuItemPosition	9.39	// チェック式トグル「Translucent Book Dialog」(★Windows 専用=ブック比較ダイアログを半透明に。2026-08-13 追加。Translucent 3兄弟の末尾＝9.36 Pages / 9.38 Panel / 9.39 ここ)
// ── Overset 群 ──
#define kKESCMOversetSepMenuItemPosition	9.40	// Find Overset 群の上の区切り線(パス末尾 ":-")
#define kKESCMFindOversetMenuItemPosition	9.42	// チェック式トグル「Find Overset」(アクティブ文書の overset ページに十字)
#define kKESCMRefreshOversetMenuItemPosition	9.44	// 実行アクション「Refresh Overset」(ON時のみ有効=再走査)
// ── 実行アクション群 ──
#define kKESCMSep3MenuItemPosition			9.50	// Refresh Overset の下の区切り線(パス末尾 ":-")。この下に実行アクション群を置く
#define kKESCMAlignViewsMenuItemPosition	9.52	// 実行アクション「Align Other Views to Active」を実行アクション群の先頭に(2026-07-24)
#define kKESCMHideUnchangedMenuItemPosition	9.54	// チェック式トグル「Hide Unchanged Spreads」。⚠2026-08-12 まで下の Compare Books と**同じ 9.54 で重複していた**(同値だと並びを決めるのは MenuDef の登録順だけになる)。Compare Books が Start の直下へ抜けたので重複は解消済み
#define kKESCMSavePanelStateMenuItemPosition	9.56	// 実行アクション「Save Panel Settings」
#define kKESCMSaveChecksMenuItemPosition	9.58	// 実行アクション「Save Check & Register」
#define kKESCMLoadChecksMenuItemPosition	9.60	// 実行アクション「Load Check & Register」
#define kKESCMExportChangedPagesMenuItemPosition	9.53	// 実行アクション「Export Changed Pages...」(変更ページ一覧をTSVで保存)。Align の直下(2026-07-25 ユーザー指定)
// ★★Compare Books の位置は 9.54(実行アクション群) → 9.53 → **9.05** と 2026-08-12 に二度動いた(ユーザー指定
//   「一つ上へ」→「Start のすぐ下に」)。9.05 は **Start(9.0) と 区切り線 Sep1(9.1) の間**＝Start との間に線が
//   入らない位置で、比較を**始める**2つの項目が1つの群になる。⚠**旧コメントの「文書比較(Start)とは独立した
//   経路なので Start 群ではなく実行アクション群に置く」は撤回**(2026-08-12 ユーザー判断)。
#define kKESCMCompareBooksMenuItemPosition	9.05	// 実行アクション「Compare Books」(ブック同士を章単位で比較)。Start の直下
// ブック比較ダイアログの章行の右クリックメニュー内の位置。★このメニューは項目が1つしかないので
// 値そのものに意味は無い(パネルのフライアウトとは別の木＝kKESCMBookRowMenuName の下)。
#define kKESCMBookRowStartMenuItemPosition	1.0		// 章行の右クリック「Start Change Marker」
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
#define kKCMUIFirstMajorFormatNumber  RezLong(1)
#define kKCMUIFirstMinorFormatNumber  RezLong(0)

// Data format version numbers for the PluginVersion resource 
#define kKCMUICurrentMajorFormatNumber kKCMUIFirstMajorFormatNumber
#define kKCMUICurrentMinorFormatNumber kKCMUIFirstMinorFormatNumber

#endif // __KCMUIID_h__
