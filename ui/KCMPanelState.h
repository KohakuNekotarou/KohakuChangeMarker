//========================================================================================
//
//  KCMPanelState.h
//
//  パネルのフライアウト(ポップアップ)メニューにある「設定系」トグルの状態を、こちらで扱いやすい
//  独自の JSON ファイルとしてローカル(ユーザーのローミング環境設定フォルダー)に保存/復元する。
//  ★InDesign 本体のデータ(ワークスペース SavedData・ドキュメント)には一切書き込まない。
//
//  保存先: FileUtils::GetAppRoamingDataFolder(.., "KCMPanelState.json") 直下
//    (Windows 例) %APPDATA%\Adobe\InDesign\Version XX.0\<locale>\KCMPanelState.json
//  ★サブフォルダーは作らない(ユーザー指定 2026-07-12)。この場所は InDesign の環境設定と同じユーザー
//  領域だが、独立した独自 JSON ファイルなので InDesign 側のファイルとは無関係。
//
//  保存対象(=設定系トグルのみ。Start/Stop の arm 状態や Hide Unchanged の隠し実行状態のような
//  「その瞬間の作業状態」は、復元すると副作用があるので保存しない):
//    - Marks opacity(25% or 75%)         キー "opacity25"
//    - Mark colour(Red or Cyan)          キー "markColorCyan"(kTrue=Cyan / kFalse=Red・既定)。
//                                        ⚠2026-08-24 の新設時に**保存の口へ入れ忘れ**、
//                                        2026-08-25 の点検で補った(下の警告を参照)
//    - Always Show Marks on Target       キー "showTgtMarks"(2026-08-22 に新設。⚠この一覧への
//                                        追記が漏れていた＝2026-08-25 に補った)
//    - Always Show Marks on Source       キー "showSrcMarks"
//      (★上の2つは 2026-08-25 に改名した。旧名は "Show Marks on Target" / "Show Marks on Source"
//       ＝**保存キーは変えていない**ので、古い設定ファイルもそのまま読める)
//    - Show Original Page Numbers
//    - Sync Layout Views(既定 ON。同期の発火条件は Start 中の Target↔Source、または Stop 中+
//      KCM ツール選択中の全文書=KCMSyncOtherDocViewportsTo のガード参照)
//    - Show Scrollbar Map
//    - Ignore Page Number Marker
//    - Translucent Panel / Translucent Pages Panel / Translucent Book Dialog(★Windows 専用。
//      対象は順に 自パネル / 本体のページパネル / 自分のブック比較ダイアログで、保存キーは
//      "translucentPanel" / "translucentPagesPanel" / "translucentBookDialog"。
//      ⚠3つ目は 2026-08-13 に足したのにこの一覧へ載っていなかった＝2026-08-18・不具合再検査 B-U3 で
//        実ファイル(%APPDATA%\...\KCMPanelState.json)を開いて11キーを数え、追記した)
//      ★復元されるのはフラグだけで、窓への適用はパネルの AutoAttach とパネル表示状態の購読が
//      行う(起動時にはまだ自パネルが無いため)。
//      ★★**再起動直後に貼られることを実機で確認した(2026-08-07)** —— ページパネルを ON にして
//      Save Panel Settings → InDesign を起動し直すと、ページパネルは半透明で出てくる。
//      2026-08-06 時点の「他人の窓なので、ワークスペース復元で最初から開いていると購読開始より
//      前に開き終わっているのではないか」という懸念は**杞憂だった**。
//      ⚠ただし観測は「KCM パネルも一緒に開いている」状態のもの。その場合は
//      KCMPanelObserver.cpp の AutoAttach が両対象を貼り直すので、そちらで効いた可能性がある
//      (kPaletteVisibilityChangedMessage の購読が間に合っていたのかは切り分けていない)。
//      ∴「KCM パネルを閉じたまま起動して、ページパネルだけ半透明になるか」は依然として未測定。
//      効かない報告が来たらそこを疑うこと(手当ては KCMPanelAlpha.cpp の Win32 フックで、
//      窓イベントのときだけキャッシュ未取得の対象を引き直す)。
//    - Compare mode(Pixel / Story)       キー "compareMode"。⚠**唯一の非 bool**＝値は文字列
//                                        ("pixel"/"story")。理由は KCMPanelState.cpp の
//                                        KCMJsonReadString の頭(2026-08-21)
//
//  ★**わざと保存しないもの**(=「保存し忘れ」と区別するためここに明記する。2026-08-25):
//    - **Print comparison marks** …… このトグルは**画面だけでなく紙と PDF に何が出るかを変える**ので、
//      起動のたびに既定の OFF から始める(ユーザー指定)。∴ 出力にマークを載せるのは、その回に明示的に
//      ON にしたときだけになる。⚠**公開版 1.3.0 では保存していた**＝挙動変更なので
//      `source/KCMID.h` の増分⑱に書いてある。
//    - Start/Stop の arm 状態、Hide Unchanged、Find Overset …… 「その瞬間の作業状態」(上記のとおり)。
//    - ページごとの ✓(Check)と地図 …… これは**別の永続化**で、保存先も別(KCMPageCheck.cpp)。
//
//  ⚠★★**トグルを新しく足したら、この一覧・保存・復元の3か所を必ず同時に触ること。**
//    2026-08-25 の点検で**3件の取りこぼしが見つかった**＝①「Always Show Marks on Target」(2026-08-22 新設)は
//    保存も復元もされていたが**この一覧に載っていなかった** ②「Compare mode」(2026-08-21 新設)も同じく
//    一覧漏れ ③★**「Mark colour」(Red/Cyan・2026-08-24 新設)が保存も復元もされていなかった**
//    (=実害のある取りこぼし＝選んでも起動し直すと赤へ戻っていた。✅2026-08-25 に修正)。
//    ★同種の漏れは 2026-08-18 の B-U3 でも1件見つかって
//    いる(Translucent Book Dialog)＝**この一覧は放っておくと必ず実物とずれる**。
//    ⇒ 点検の仕方＝`KCMActionComponent.cpp` の `UpdateActionStates` に並ぶ**チェック式/ラジオ式の
//      分岐を数え上げ**、1つずつ「設定か作業状態か」を判定して、設定ならこのファイルの2か所に在るかを見る。
//
//========================================================================================

#ifndef __KCMPanelState_h__
#define __KCMPanelState_h__

// フライアウトの「Save Panel Settings」項目から呼ぶ。現在の設定系トグルの状態を JSON ファイルへ
// 書き出し、保存先のフルパスをモーダルダイアログで表示する(書き込み失敗時はその旨を表示)。
// 実体は KCMPanelState.cpp。
void	KCMSavePanelState();

// 保存済みの JSON ファイルがあれば読み込み、各トグルへ適用する(無ければ何もしない)。
// ★呼び出しタイミングは起動時(KCMUIStartup::Startup。2026-07-15 に前倒し): 同期が Stop 中+
//   ツール選択でも動くようになったため、パネルを開く前でも保存設定(特に Sync OFF)を効かせる。
//   復元先は全部エンジン側のフラグ/購読でパネル・文書に依存せず、起動時に呼んで安全。
// ★セッション内で一度だけ実行する内部ガードを持つので、パネル AutoAttach からの既存呼び出しは
//   no-op の保険として残り、途中で変えた設定が巻き戻ることもない。実体は KCMPanelState.cpp。
void	KCMLoadPanelStateIfPresent();

#endif // __KCMPanelState_h__
