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
//
// KCMUI = Kohaku Change Marker の **UI 側**プラグイン。相方の model 側は
// source/KESCMFactoryList.h（比較エンジン・描画・Facade 5本）。
//
// ⚠**登録漏れは完全に無言で失敗する**（`CREATE_PMINTERFACE` を書いてあっても、この表に載って
//   いなければ実体は作られない）。∴**この表は記憶で足さない**——`CREATE_PMINTERFACE` と
//   `CREATE_PERSIST_PMINTERFACE` の**両方**を Grep し、結果と1対1で突き合わせて作ること
//   （★tool と自前 view は PERSIST 版を使うので、前者だけ見ると 3 本落ちる）。
//
// ★★2026-08-15（第2段 Task 6B-2）: **DollyXs 雛形の About 一式を撤去した**
//   （KCMUIActionComponent.cpp ＋ kKCMUIActionComponentBoss / kKCMUIActionComponentImpl /
//    kKCMUIAboutActionID ＋ KCMUI.fr の Class・MenuDef・ActionDef）。
//   理由は2つ:
//     ①**ActionID +0 が衝突する**——ID をオフセット保存で移したので、KESCM の About(+0) と
//       雛形の About(+0) が同じ番号になる。
//     ②**KESCM 本体の ActionComponent が UI 側へ来る**（ActionComponent は UI 必須）ので、
//       About は本来の1つに戻るのが正しい。Task 2 以降 About が2つ並んでいたのは暫定状態だった。
//   ⇒ 下の KESCMActionComponent が About を含むフライアウト全部を担う。

// ---- 2026-08-15: model/UI 分割 第2段 Task 5（葉の UI 部品）で KESCM から移ってきた4本 ----
//
// ★**Impl の「名前」は `kKESCM*Impl` のまま**（`kKCMUI*Impl` へ改名していない）＝分割の方針そのもので、
//   おかげで C++ もコメントも ID 名を1つも書き換えずに済んだ。動いたのは**番号だけ**で、
//   `kKCMUIPrefix + N` として KCMUIID.h が定義し、名指ししている Class ブロックは KCMUI.fr にある。
//
// ⚠2026-08-17（監査 B-U4）にこの節を書き直した。旧記述は「`.fr` の Class / AddIn ブロックはまだ
//   KESCM.fr 側にある／番号の振り替えは boss と一緒（Task 6B）／それまで ID の定義は KESCMID.h に
//   残る／そのため KCMUI.fr は KESCMID.h を include している」で、**4つとも Task 6B-2 の完了で
//   覆っていた**（KCMUI.fr が include しているのは KCMUIID.h と KESCMScriptingDefs.h の2本だけ）。
//   ＝★**予告した作業が終わっても、予告を書いた場所には誰も戻ってこない。**
REGISTER_PMINTERFACE(KESCMIconTip, kKESCMIconTipImpl)		// パネルのイラスト/ツール切替ボタンのツールチップ(KESCMIconTip.cpp)
REGISTER_PMINTERFACE(KESCMNoTip, kKESCMNoTipImpl)			// ツールチップを出さない ITip(KESCMNoTip.cpp)
REGISTER_PMINTERFACE(KESCMSplitterEH, kKESCMSplitterEHImpl)	// 分割バーを掴めなくする IEventHandler(KESCMSplitterEH.cpp)
REGISTER_PMINTERFACE(KESCMPanelView, kKESCMPanelViewImpl)	// パネルの最小サイズを守る(PalettePanelView派生。KESCMPanelView.cpp)

// ---- 2026-08-15: 第2段 Task 6 で移ってきた 26 本（UI 側の残り全部） ----
//
// ★5分割（旧 Task 6〜9）は**成立しなかった**ので一度に移した。パネル・ツリー・ダイアログ・ツールが
//   互いを自由関数で呼び合っており（環になっており）、どこで切っても両側とも未解決シンボルで落ちる。
//   ⇒ 計画書の「Task 5〜9」節の実測表を参照。
// ★Impl ID の扱いは上の4本と同じ（`kKESCM*Impl` のまま。振り替えは Task 6B）。

// 起動/終了と、model からの通知の受け手
REGISTER_PMINTERFACE(KESCMUIStartup, kKESCMUIStartupImpl)	// UI 側の起動/終了(KESCMUIStartup.cpp)。model 側の対は KESCMPeekStartup
REGISTER_PMINTERFACE(KESCMModelChangeObserver, kKESCMModelChangeObserverImpl)	// model の通知を受けて画面を作り直す(KESCMModelChangeObserver.cpp)
REGISTER_PMINTERFACE(KESCMDocsClosedObserver, kKESCMDocsClosedObserverImpl)	// 一括クローズ完了で保留した後片付けを流す(KESCMPeekGesture.cpp)

// 押下中 HUD の描画（★画面専用＝PDF 書き出しには出ない。だから UI 側でよい）
REGISTER_PMINTERFACE(KESCMUIDrawEventSrvc, kKESCMUIDrawEventSrvcImpl)		// UI 専用の描画サービス(KESCMUIDrawEvent.cpp)
REGISTER_PMINTERFACE(KESCMUIDrawEventHandler, kKESCMUIDrawEventHandlerImpl)	// 同上のハンドラ

// パネル本体
REGISTER_PMINTERFACE(KESCMPanelObserver, kKESCMPanelObserverImpl)	// パネルの widget を監視して表示を駆動(KESCMPanelObserver.cpp)
REGISTER_PMINTERFACE(KESCMPanelRollOver, kKESCMPanelRollOverImpl)	// カーソルが乗っている間だけ半透明を解除(IMouseRollOver。KESCMPanelAlpha.cpp)
REGISTER_PMINTERFACE(KESCMPanelVisibilityObserver, kKESCMPanelVisibilityObserverImpl)	// パネルの開閉/ドッキング切り替えで半透明を貼り直す(同上)
REGISTER_PMINTERFACE(KESCMStorySectionToggleObserver, kKESCMStorySectionToggleObserverImpl)	// 「Story Edits」セクションの開閉ボタン(KESCMStorySectionObserver.cpp)

// Story Edits の一覧（kTreeViewWidgetBoss / kTreeNodeWidgetBoss 由来＝UI 確定）
REGISTER_PMINTERFACE(KESCMStoryTreeAdapter, kKESCMStoryTreeAdapterImpl)	// 一覧の中身(ListTreeViewAdapter派生。KESCMStoryTreeAdapter.cpp)
REGISTER_PMINTERFACE(KESCMStoryTreeWidgetMgr, kKESCMStoryTreeWidgetMgrImpl)	// 行の生成と流し込み(CTreeViewWidgetMgr派生。KESCMStoryTreeWidgetMgr.cpp)
REGISTER_PMINTERFACE(KESCMStoryRowEH, kKESCMStoryRowEHImpl)	// 行のクリック=ジャンプ/ダブルクリック=ストーリー全文を選択(KESCMStoryRowEH.cpp)
REGISTER_PMINTERFACE(KESCMStoryTreeEH, kKESCMStoryTreeEHImpl)	// 一覧の↑↓=行を移動して着いた行を表示(KESCMStoryTreeEH.cpp)
REGISTER_PMINTERFACE(KESCMStoryMarkerAdornment, kKESCMStoryMarkerAdornmentImpl)	// 飛んだ先の文字を反転して見せる一時マーカー(グローバルテキストアドーンメント。KESCMStoryMarker.cpp)
REGISTER_PMINTERFACE(KESCMStoryMarkerExpiryTask, kKESCMStoryMarkerExpiryImpl)	// 上のマーカーを1秒ほどで引っ込める IIdleTask(KESCMStoryMarkerExpiry.cpp)
REGISTER_PMINTERFACE(KESCMStoryCellData, kKESCMStoryCellDataImpl)// 変更行のセルが描く3片(前の文脈/変更された文字/後の文脈)の入れ物(KESCMStoryCellView.cpp)
REGISTER_PMINTERFACE(KESCMStoryCellView, kKESCMStoryCellViewImpl)	// ★PERSIST 版(変更行のテキストセル。変更された文字だけ通常色・前後は薄く。DVControlView派生。KESCMStoryCellView.cpp)
REGISTER_PMINTERFACE(KESCMStatusTextData, kKESCMStatusTextDataImpl)	// パネルのメッセージ欄が描く4片(見出し/前の文脈/変更された文字/後の文脈)の入れ物(KESCMStatusTextView.cpp)
REGISTER_PMINTERFACE(KESCMStatusTextView, kKESCMStatusTextViewImpl)	// ★PERSIST 版(パネルのメッセージ欄。折り返しを自前で持ち、変更された文字だけ通常色・見出しと前後は薄く。DVControlView派生。KESCMStatusTextView.cpp)

// ブック比較ダイアログとその章一覧（kDialogBoss 由来＝UI 確定）
REGISTER_PMINTERFACE(KESCMBookDialogController, kKESCMBookDialogControllerImpl)	// モードレスダイアログ(CDialogController派生。KESCMBookDialog.cpp)
REGISTER_PMINTERFACE(KESCMBookTreeAdapter, kKESCMBookTreeAdapterImpl)	// 章一覧の中身(KESCMBookTreeAdapter.cpp)
REGISTER_PMINTERFACE(KESCMBookTreeWidgetMgr, kKESCMBookTreeWidgetMgrImpl)	// 章一覧の行の生成と流し込み(KESCMBookTreeWidgetMgr.cpp)
REGISTER_PMINTERFACE(KESCMBookRowEH, kKESCMBookRowEHImpl)	// 章行のダブルクリック=その章を開く/右クリック=行メニュー(KESCMBookRowEH.cpp)

// ツール・トラッカー・カーソル（kGenericToolBoss 由来＝UI 確定）
REGISTER_PMINTERFACE(KESCMTool, kKESCMToolImpl)	// ★PERSIST 版(ツールの選択状態が IID_IPMPERSIST で保存される。KESCMTool.cpp)
REGISTER_PMINTERFACE(KESCMTracker, kKESCMTrackerImpl)	// ドラッグ操作エンジン(KESCMTracker.cpp)
REGISTER_PMINTERFACE(KESCMTrackerEH, kKESCMTrackerEHImpl)	// 同上のイベントハンドラ
REGISTER_PMINTERFACE(KESCMTrackerRegister, kKESCMTrackerRegisterImpl)	// トラッカーの登録(KESCMTrackerRegister.cpp)
REGISTER_PMINTERFACE(KESCMCheckCursorProvider, kKESCMCursorProviderImpl)	// ツールのカーソル(CToolCursorProvider派生。KESCMCursorProvider.cpp)

// 文書窓に注入するもの・ビュー同期・サムネイル
REGISTER_PMINTERFACE(KESCMScrollMapView, kKESCMScrollMapViewImpl)	// ★PERSIST 版(スクロールバー地図 strip。DVControlView派生。KESCMScrollMap.cpp)
REGISTER_PMINTERFACE(KESCMLayoutSyncObserver, kKESCMLayoutSyncObserverImpl)	// レイアウトビューの同期(KESCMViewSync.cpp)
REGISTER_PMINTERFACE(KESCMThumbIdleTask, kKESCMThumbIdleTaskImpl)	// Pages パネルのサムネイル再生成を次の idle へ(KESCMThumbIdleTask.cpp)

// メニュー（★ActionComponent は UI。kModelPlugIn のサンプル24本に ActionComponent は0本）
REGISTER_PMINTERFACE(KESCMActionComponent, kKESCMActionComponentImpl)	// フライアウトと右クリックメニューの実行(KESCMActionComponent.cpp)
