//========================================================================================
//
//  KCMScrollMap.h
//
//  スクロールバー地図: 文書ウィンドウ(kLayoutPresentationBoss)の縦スクロールバー左隣に、
//  KCM の枠(変更マーク)があるページ位置を示す細い strip ウィジェットを実行時注入する。
//  Visual Studio のスクロールバー検索マークと同じ発想。裏取り調査は
//  docs/ai-notes/scrollbar-minimap.md(SDK ルート)を参照。
//
//  フェーズ1(プローブ=オレンジ塗り)は 2026-07-11 実機表示OK。フェーズ2で実データ描画:
//  変更ページ=赤の塗りつぶし、Add/Remove 登録ページ=緑(色はユーザー指定 2026-07-11)。
//
//========================================================================================

#ifndef __KCMScrollMap_h__
#define __KCMScrollMap_h__

#include "BaseType.h"		// bool16

class IDataBase;

// targetDB の全レイアウトビューから文書ウィンドウ(presentation)を集め、まだ無ければ
// 縦スクロールバー左隣に地図 strip を注入する(既に注入済みの窓はスキップ=何度呼んでも安全)。
// 縦スクロールバーが見つからない窓は黙ってスキップする。
void	KCMScrollMapAttach(IDataBase* targetDB);

// 全ドキュメントの全ウィンドウから、注入済みの地図 strip を探して取り外す。
// ポインタは保持しない設計(毎回 FindWidget で探す)なので、窓が既に閉じられていても安全。
void	KCMScrollMapDetachAll();

// 注入済みの全 strip を再描画(Invalidate)する。比較の実行/再比較/登録トグル後にマークを
// 最新化するために呼ぶ(KCMDoMarkChangesDoc の末尾)。strip が1つも無ければ何もしない。
void	KCMScrollMapInvalidateAll();

// スプレッド描画イベントに便乗した「手動 Hide/Show Spread」検出。
// ★呼び手は**UI 側の描画サービス KCMUIDrawEventHandler::HandleDrawEvent(KCMUIDrawEvent.cpp)**。
//   ⚠2026-08-18(不具合再検査 B-U2)訂正＝「KCMDrawEventHandler から呼ぶ」と書いてあったが、あれは
//   **model 側**のマーク描画ハンドラで、この検出は 2026-08-13(Task 7)にこちらへ移っている。
//   B-U6 が同型15件を直したときの取りこぼし(あちらが探した文字列は KCMPeekStartup だった)。
// ページパネルからの手動の隠し/再表示は KCM のどのフックも通らないが、必ず再描画は起こすので、
// 描画のたび(250msスロットル付き)に隠しフラグ構成の指紋を取り、変化していたら地図を Invalidate する。
// Undo/Redo による隠し状態の変化も同じ経路で拾える。
// ⚠2026-08-19(不具合再検査 B-U8)訂正2件＝①指紋を取る文書は「Target/Source」の2つではなく**3つ**
//   (＋Find Overset の走査文書。Find Overset 単独 ON の窓にも strip が出るため) ②「未 arm なら即 return」も
//   誤りで、**未 arm でも Find Overset が ON なら続行する**(.cpp 側は 2026-07-24 から正しく書いてある)。
//   即 return するのは「Show Scrollbar Map が OFF」と「arm も overset も無い」の2つ。
// ★2026-08-11: 指紋に「今どのマスタースプレッドを表示しているか」も混ぜた。地図はマスター表示中
//   だけ中身が変わる(そのマスターのページだけを載せる)が、スプレッドの切り替えも KCM のどの
//   フックも通らないため、隠しフラグとまったく同じ理由でここに便乗する。
void	KCMScrollMapNoticeDrawEvent();

// スクロールバー地図の有効/無効(フライアウト「Show Scrollbar Map」トグル。既定 ON)。
// OFF の間は KCMScrollMapAttach / KCMScrollMapNoticeDrawEvent が即 return するので、
// Start しても strip を注入しない(既存 strip はトグル操作側で DetachAll して撤去する)。
// ★★このセッターは**旗を書くだけ**で、strip の attach/detach は呼び手の仕事。呼び手は全数2つ:
//     ①KCMActionComponent(フライアウトのトグル) … ON なら Attach＋Invalidate / OFF なら DetachAll
//     ②KCMPanelState(保存済みパネル設定の復元)   … **旗だけ**。
//   ⚠②が①と違って何もしないのは**正しい**(2026-08-19・不具合再検査 B-U8 で確認)＝
//   KCMLoadPanelStateIfPresent は `sLoaded` の一度きりで、走るのは UI 側 Startup。**その時点では
//   比較も始まっておらず strip は1つも無い**ので、attach も detach も対象が無い。
//   ⇒ 「片方だけ契約を守っていない」ように見えるが不具合ではない。**もしこの復元を実行中にも
//     呼べるようにするなら、そのときは①と同じ後始末を足すこと。**
bool16	KCMGetScrollMapEnabled();
void	KCMSetScrollMapEnabled(bool16 on);

#endif // __KCMScrollMap_h__
