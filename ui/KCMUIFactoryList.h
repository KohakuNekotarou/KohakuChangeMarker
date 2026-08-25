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
// source/KCMFactoryList.h（比較エンジン・描画・Facade 5本）。
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
//     ①**ActionID +0 が衝突する**——ID をオフセット保存で移したので、KCM の About(+0) と
//       雛形の About(+0) が同じ番号になる。
//     ②**KCM 本体の ActionComponent が UI 側へ来る**（ActionComponent は UI 必須）ので、
//       About は本来の1つに戻るのが正しい。Task 2 以降 About が2つ並んでいたのは暫定状態だった。
//   ⇒ 下の KCMActionComponent が About を含むフライアウト全部を担う。

// ---- 2026-08-15: model/UI 分割 第2段 Task 5（葉の UI 部品）で KCM から移ってきた4本 ----
//
// ★**Impl の「名前」は `kKCM*Impl` のまま**（`kKCMUI*Impl` へ改名していない）＝分割の方針そのもので、
//   おかげで C++ もコメントも ID 名を1つも書き換えずに済んだ。動いたのは**番号だけ**で、
//   `kKCMUIPrefix + N` として KCMUIID.h が定義し、名指ししている Class ブロックは KCMUI.fr にある。
//
// ⚠2026-08-17（監査 B-U4）にこの節を書き直した。旧記述は「`.fr` の Class / AddIn ブロックはまだ
//   KCM.fr 側にある／番号の振り替えは boss と一緒（Task 6B）／それまで ID の定義は KCMID.h に
//   残る／そのため KCMUI.fr は KCMID.h を include している」で、**4つとも Task 6B-2 の完了で
//   覆っていた**（KCMUI.fr が include しているのは KCMUIID.h と KCMScriptingDefs.h の2本だけ）。
//   ＝★**予告した作業が終わっても、予告を書いた場所には誰も戻ってこない。**
REGISTER_PMINTERFACE(KCMIconTip, kKCMIconTipImpl)		// パネルのイラスト/ツール切替ボタンのツールチップ(KCMIconTip.cpp)
REGISTER_PMINTERFACE(KCMNoTip, kKCMNoTipImpl)			// ツールチップを出さない ITip(KCMNoTip.cpp)
REGISTER_PMINTERFACE(KCMSplitterEH, kKCMSplitterEHImpl)	// 分割バーを掴めなくする IEventHandler(KCMSplitterEH.cpp)
REGISTER_PMINTERFACE(KCMPanelView, kKCMPanelViewImpl)	// パネルの最小サイズを守る(PalettePanelView派生。KCMPanelView.cpp)

// ---- 2026-08-15: 第2段 Task 6 で移ってきた 26 本（UI 側の残り全部） ----
//
// ★5分割（旧 Task 6〜9）は**成立しなかった**ので一度に移した。パネル・ツリー・ダイアログ・ツールが
//   互いを自由関数で呼び合っており（環になっており）、どこで切っても両側とも未解決シンボルで落ちる。
//   ⇒ 計画書の「Task 5〜9」節の実測表を参照。
// ★Impl ID の扱いは上の4本と同じ（`kKCM*Impl` のまま。振り替えは Task 6B）。

// 起動/終了と、model からの通知の受け手
REGISTER_PMINTERFACE(KCMUIStartup, kKCMUIStartupImpl)	// UI 側の起動/終了(KCMUIStartup.cpp)。model 側の対は KCMPeekStartup
REGISTER_PMINTERFACE(KCMModelChangeObserver, kKCMModelChangeObserverImpl)	// model の通知を受けて画面を作り直す(KCMModelChangeObserver.cpp)
REGISTER_PMINTERFACE(KCMDocsClosedObserver, kKCMDocsClosedObserverImpl)	// 一括クローズ完了で保留した後片付けを流す(KCMPeekGesture.cpp)

// 押下中 HUD の描画（★画面専用＝PDF 書き出しには出ない。だから UI 側でよい）
REGISTER_PMINTERFACE(KCMUIDrawEventSrvc, kKCMUIDrawEventSrvcImpl)		// UI 専用の描画サービス(KCMUIDrawEvent.cpp)
REGISTER_PMINTERFACE(KCMUIDrawEventHandler, kKCMUIDrawEventHandlerImpl)	// 同上のハンドラ

// パネル本体
REGISTER_PMINTERFACE(KCMPanelObserver, kKCMPanelObserverImpl)	// パネルの widget を監視して表示を駆動(KCMPanelObserver.cpp)
REGISTER_PMINTERFACE(KCMPanelRollOver, kKCMPanelRollOverImpl)	// カーソルが乗っている間だけ半透明を解除(IMouseRollOver。KCMPanelAlpha.cpp)
REGISTER_PMINTERFACE(KCMPanelVisibilityObserver, kKCMPanelVisibilityObserverImpl)	// パネルの開閉/ドッキング切り替えで半透明を貼り直す(同上)
REGISTER_PMINTERFACE(KCMStorySectionToggleObserver, kKCMStorySectionToggleObserverImpl)	// 「Story Edits」セクションの開閉ボタン(KCMStorySectionObserver.cpp)

// Story Edits の一覧（kTreeViewWidgetBoss / kTreeNodeWidgetBoss 由来＝UI 確定）
REGISTER_PMINTERFACE(KCMStoryTreeAdapter, kKCMStoryTreeAdapterImpl)	// 一覧の中身(ListTreeViewAdapter派生。KCMStoryTreeAdapter.cpp)
REGISTER_PMINTERFACE(KCMStoryTreeWidgetMgr, kKCMStoryTreeWidgetMgrImpl)	// 行の生成と流し込み(CTreeViewWidgetMgr派生。KCMStoryTreeWidgetMgr.cpp)
REGISTER_PMINTERFACE(KCMStoryRowEH, kKCMStoryRowEHImpl)	// 行のクリック=ジャンプ/ダブルクリック=ストーリー全文を選択(KCMStoryRowEH.cpp)
REGISTER_PMINTERFACE(KCMStoryTreeEH, kKCMStoryTreeEHImpl)	// 一覧の↑↓=行を移動して着いた行を表示(KCMStoryTreeEH.cpp)
// ★★★2026-08-23＝**マーカーとその timer は model 側へ移設した**(KCMFactoryList.h に居る)。
//   理由＝**UI の File>Export>PDF は BG で走り、kUIPlugIn には描画が1度も配られない**(2026-08-12 実測・無警告)
//   ので、こちら側に居るかぎり書き出した PDF には絶対に出せなかった。
//   ⚠**kKCMUIPrefix + 41 / + 42 は欠番のまま**にする(値さえ衝突しなければよい＝[[id-prefix-256-slot-budget]])。
REGISTER_PMINTERFACE(KCMStoryCellData, kKCMStoryCellDataImpl)// 変更行のセルが描く3片(前の文脈/変更された文字/後の文脈)の入れ物(KCMStoryCellView.cpp)
REGISTER_PMINTERFACE(KCMStoryCellView, kKCMStoryCellViewImpl)	// ★PERSIST 版(変更行のテキストセル。変更された文字だけ通常色・前後は薄く。DVControlView派生。KCMStoryCellView.cpp)
REGISTER_PMINTERFACE(KCMStatusTextData, kKCMStatusTextDataImpl)	// パネルのメッセージ欄が描く4片(見出し/前の文脈/変更された文字/後の文脈)の入れ物(KCMStatusTextView.cpp)
REGISTER_PMINTERFACE(KCMStatusTextView, kKCMStatusTextViewImpl)	// ★PERSIST 版(パネルのメッセージ欄。折り返しを自前で持ち、変更された文字だけ通常色・見出しと前後は薄く。DVControlView派生。KCMStatusTextView.cpp)

// ブック比較ダイアログとその章一覧（kDialogBoss 由来＝UI 確定）
REGISTER_PMINTERFACE(KCMBookDialogController, kKCMBookDialogControllerImpl)	// モードレスダイアログ(CDialogController派生。KCMBookDialog.cpp)
REGISTER_PMINTERFACE(KCMBookTreeAdapter, kKCMBookTreeAdapterImpl)	// 章一覧の中身(KCMBookTreeAdapter.cpp)
REGISTER_PMINTERFACE(KCMBookTreeWidgetMgr, kKCMBookTreeWidgetMgrImpl)	// 章一覧の行の生成と流し込み(KCMBookTreeWidgetMgr.cpp)
REGISTER_PMINTERFACE(KCMBookRowEH, kKCMBookRowEHImpl)	// 章行のダブルクリック=その章を開く/右クリック=行メニュー(KCMBookRowEH.cpp)

// ツール・トラッカー・カーソル（kGenericToolBoss 由来＝UI 確定）
REGISTER_PMINTERFACE(KCMTool, kKCMToolImpl)	// ★PERSIST 版(ツールの選択状態が IID_IPMPERSIST で保存される。KCMTool.cpp)
REGISTER_PMINTERFACE(KCMTracker, kKCMTrackerImpl)	// ドラッグ操作エンジン(KCMTracker.cpp)
REGISTER_PMINTERFACE(KCMTrackerEH, kKCMTrackerEHImpl)	// 同上のイベントハンドラ
REGISTER_PMINTERFACE(KCMTrackerRegister, kKCMTrackerRegisterImpl)	// トラッカーの登録(KCMTrackerRegister.cpp)
REGISTER_PMINTERFACE(KCMCheckCursorProvider, kKCMCursorProviderImpl)	// ツールのカーソル(CToolCursorProvider派生。KCMCursorProvider.cpp)

// 文書窓に注入するもの・ビュー同期・サムネイル
REGISTER_PMINTERFACE(KCMScrollMapView, kKCMScrollMapViewImpl)	// ★PERSIST 版(スクロールバー地図 strip。DVControlView派生。KCMScrollMap.cpp)
REGISTER_PMINTERFACE(KCMLayoutSyncObserver, kKCMLayoutSyncObserverImpl)	// レイアウトビューの同期(KCMViewSync.cpp)
REGISTER_PMINTERFACE(KCMThumbIdleTask, kKCMThumbIdleTaskImpl)	// Pages パネルのサムネイル再生成を次の idle へ(KCMThumbIdleTask.cpp)

// メニュー（★ActionComponent は UI。kModelPlugIn のサンプル24本に ActionComponent は0本）
REGISTER_PMINTERFACE(KCMActionComponent, kKCMActionComponentImpl)	// フライアウトと右クリックメニューの実行(KCMActionComponent.cpp)
