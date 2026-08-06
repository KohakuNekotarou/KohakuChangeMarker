//========================================================================================
//
//  KESCMPanelState.h
//
//  パネルのフライアウト(ポップアップ)メニューにある「設定系」トグルの状態を、こちらで扱いやすい
//  独自の JSON ファイルとしてローカル(ユーザーのローミング環境設定フォルダー)に保存/復元する。
//  ★InDesign 本体のデータ(ワークスペース SavedData・ドキュメント)には一切書き込まない。
//
//  保存先: FileUtils::GetAppRoamingDataFolder(.., "KESCMPanelState.json") 直下
//    (Windows 例) %APPDATA%\Adobe\InDesign\Version XX.0\<locale>\KESCMPanelState.json
//  ★サブフォルダーは作らない(ユーザー指定 2026-07-12)。この場所は InDesign の環境設定と同じユーザー
//  領域だが、独立した独自 JSON ファイルなので InDesign 側のファイルとは無関係。
//
//  保存対象(=設定系トグルのみ。Start/Stop の arm 状態や Hide Unchanged の隠し実行状態のような
//  「その瞬間の作業状態」は、復元すると副作用があるので保存しない):
//    - Print comparison marks / Marks opacity(25% or 75%)
//    - Hold to Hide Marks
//    - Show Marks on Source
//    - Show Original Page Numbers
//    - Sync Layout Views(既定 ON。同期の発火条件は Start 中の Target↔Source、または Stop 中+
//      KESCM ツール選択中の全文書=KESCMSyncOtherDocViewportsTo のガード参照)
//    - Show Scrollbar Map
//    - Ignore Page Number Marker
//    - Translucent Panel / Translucent Pages Panel(★Windows 専用。前者は自パネル、後者は本体の
//      ページパネルが対象で、保存キーは "translucentPanel" / "translucentPagesPanel")
//      ★復元されるのはフラグだけで、窓への適用はパネルの AutoAttach とパネル表示状態の購読が
//      行う(起動時にはまだ自パネルが無いため)。
//      ★★**再起動直後に貼られることを実機で確認した(2026-08-07)** —— ページパネルを ON にして
//      Save Panel Settings → InDesign を起動し直すと、ページパネルは半透明で出てくる。
//      2026-08-06 時点の「他人の窓なので、ワークスペース復元で最初から開いていると購読開始より
//      前に開き終わっているのではないか」という懸念は**杞憂だった**。
//      ⚠ただし観測は「KESCM パネルも一緒に開いている」状態のもの。その場合は
//      KESCMPanelObserver.cpp の AutoAttach が両対象を貼り直すので、そちらで効いた可能性がある
//      (kPaletteVisibilityChangedMessage の購読が間に合っていたのかは切り分けていない)。
//      ∴「KESCM パネルを閉じたまま起動して、ページパネルだけ半透明になるか」は依然として未測定。
//      効かない報告が来たらそこを疑うこと(手当ては KESCMPanelAlpha.cpp の Win32 フックで、
//      窓イベントのときだけキャッシュ未取得の対象を引き直す)。
//
//========================================================================================

#ifndef __KESCMPanelState_h__
#define __KESCMPanelState_h__

// フライアウトの「Save Panel Settings」項目から呼ぶ。現在の設定系トグルの状態を JSON ファイルへ
// 書き出し、保存先のフルパスをモーダルダイアログで表示する(書き込み失敗時はその旨を表示)。
// 実体は KESCMPanelState.cpp。
void	KESCMSavePanelState();

// 保存済みの JSON ファイルがあれば読み込み、各トグルへ適用する(無ければ何もしない)。
// ★呼び出しタイミングは起動時(KESCMPeekStartup::Startup。2026-07-15 に前倒し): 同期が Stop 中+
//   ツール選択でも動くようになったため、パネルを開く前でも保存設定(特に Sync OFF)を効かせる。
//   復元先は全部エンジン側のフラグ/購読でパネル・文書に依存せず、起動時に呼んで安全。
// ★セッション内で一度だけ実行する内部ガードを持つので、パネル AutoAttach からの既存呼び出しは
//   no-op の保険として残り、途中で変えた設定が巻き戻ることもない。実体は KESCMPanelState.cpp。
void	KESCMLoadPanelStateIfPresent();

#endif // __KESCMPanelState_h__
