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
//    - Sync Layout Views(復元しても実際の同期は Start 中のみ発火するので実害なし)
//    - Show Scrollbar Map
//    - Invoke Panel Shortcut
//    - Ignore Page Number Marker
//
//========================================================================================

#ifndef __KESCMPanelState_h__
#define __KESCMPanelState_h__

// フライアウトの「Save Panel Settings」項目から呼ぶ。現在の設定系トグルの状態を JSON ファイルへ
// 書き出し、保存先のフルパスをモーダルダイアログで表示する(書き込み失敗時はその旨を表示)。
// 実体は KESCMPanelState.cpp。
void	KESCMSavePanelState();

// パネルが初めて開かれたとき(KESCMPanelObserver::AutoAttach)に呼ぶ。保存済みの JSON ファイルが
// あれば読み込み、各トグルへ適用する(無ければ何もしない)。★セッション内で一度だけ実行する内部
// ガードを持つので、パネルを開き直すたびに再ロードはしない(=途中で変えた設定が巻き戻らない)。
// 実体は KESCMPanelState.cpp。
void	KESCMLoadPanelStateIfPresent();

#endif // __KESCMPanelState_h__
