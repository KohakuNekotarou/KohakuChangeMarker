//========================================================================================
//
//  KESCMPeek.h
//
//  ツール(左ボタン)の「peek(覗き)」。修飾キー＋ツール左ボタンを押している間だけ、カーソル下スプレッドの旧版を
//  表示する(または CMYK をサンプリングする)。離すと元に戻す。peek 状態(arm 済みの target/source DB、
//  押下中フラグ)と起動/終了サービスを所有する(旧・中ボタンのイベントウォッチャは撤去済み 2026-07-13)。
//  arm/disarm/状態アクセサは KESCMCore.h にある。
//
//  ★2026-08-13 の model/UI 分割 第1段 Task 1 で、UI 側の3領域がここから出ていった:
//    ・ビューポート同期(Sync Layout Views / Align Other Views) → KESCMViewSync.h
//    ・Alt+左の CMYK カーソル                                   → KESCMCmykCursor.h
//    ・ジェスチャ判定と押下中の表示切替(RevealBegin/End)        → KESCMPeekGesture.h
//    このファイルに残っているのは model 側(比較・arm 状態・クローズスイープ)と、
//    それを起動/終了する KESCMPeekStartup。
//
//========================================================================================
#ifndef __KESCMPeek_h__
#define __KESCMPeek_h__

#include "BaseType.h"
#include "PMReal.h"

class IDataBase;

// 常時表示マークの画面上の「基準」不透明度。印刷設定から決まる(印刷ON => 選択不透明度25%/75%、印刷OFF => 1.0)。
// peek を離したときの経路と KESCMDoSetPrintMarks が使う。実体は KESCMPeek.cpp。
PMReal KESCMBaseScreenOpacity();

// (★enum KESCMGesture は 2026-08-14 に **KESCMPeekGesture.h**(UI 側)へ移した＝第1段 Task 16。
//  定義はここに在ったが、**使っていたのは UI 側の3ファイルだけ**で KESCMPeek.cpp は1度も参照して
//  いなかった。修飾キーの読み取りは窓の話なので、置き場所も UI が正しい。これで UI 側から
//  KESCMPeek.h を include する理由が無くなった＝第1段の完了条件1が満たせる。)

// **指定された点**のスプレッドの旧版べた載せを表示する(実体は KESCMPeek.cpp)。
// targetDB=表示中(新)ドキュメント, sourceDB=重ねる旧ドキュメント。そのスプレッドが既にキャッシュ済みなら
// 再利用(即時)、未キャッシュならその場でラスタ化する(保持は常に1スプレッド)。
//   mx, my    = 覗く点。targetDB の**ペーストボード(content)座標**
//   viewScale = その窓の content→window スケール(= ズーム × デバイス倍率)。ラスタ化解像度の基準
//   uiZoom    = その窓の UI ズーム(ユーザーに見える拡大率。デバイス倍率を含まない)。
//               ★**0 以下 = 「パノラマが引けなかった」**＝下限 50% の頭打ちを掛けずに viewScale をそのまま使う
//               (分離前に peekPano == nil だったときと同じ振る舞い)
//
// ★★2026-08-15(第2段 Task 4B)に「マウス下」から「この点」へ変えた(旧 KESCMPeekShowUnderMouse)。
//   ここに在ったビュー解決3本(KESCMQueryViewUnderMouse / KESCMQueryPanorama /
//   KESCMQueryMouseContentPoint)は呼び手(UI)へ出した。
//   ⇒ ★**観測値は UI が採り、その値からどの dpi でラスタ化するかは model が決める。**
//     下限 50% の頭打ちと 16〜300dpi のクランプは「解像度の方針」なので model 側に残っている
//     ＝分離で計算式は1文字も動いていない。
// ★呼び手は KESCMPeekGesture.cpp の KESCMTrackerBeginPeek ただ1つ(Facade 経由)。
void KESCMPeekShowAt(IDataBase* targetDB, IDataBase* sourceDB,
                     const PMReal& mx, const PMReal& my,
                     const PMReal& viewScale, const PMReal& uiZoom);

// armed 中の Target/Source が IDocumentList に現存するかの最終ライン防御(実体は KESCMPeek.cpp)。
// 失格なら KESCMHandleDocsClosed() で Stop 相当のフルクリーンアップ(arm 解除を含む)をして kFalse を返す。
// ★呼び手は KESCMPeekGesture.cpp(peek 開始・CMYK 押下)と KESCMCmykCursor.cpp(カーソル色・ドラッグ中の
//   生存検査)。解放済み IDataBase をサンプリング/peek へ渡さないための保険。
//   (2026-08-13 の分割まではこのファイル内の static だった)
bool16 KESCMArmedDocsAlive();

// ページパネルのページ右クリック「KCM: Refresh Page Comparison」の実体。選択ページの比較を再検出して
// 枠/サムネイルを更新する(旧 Ctrl+ミドルのスプレッド再比較を移設。2026-07-13)。arm 済み(Start 後)かつ
// 前面文書が Target のときだけ動く(★2026-07-15 Target 限定=ユーザー指定)。outPages=実際に再比較した
// ページ数 / outChanged=うち変化ページ数 / outCancelled=進捗バーのキャンセルで中断したか(いずれも nil 可)。
// 戻り=1ページ以上処理したか。★ページ数が多いときは進捗バー＋キャンセルが出る(2026-07-27)。中断しても
// そこまで更新した分は残る(残りのページが古いまま=選択を狭めて実行したのと同じ状態)。キャンセルを押した
// 時点のページは処理済みなので、中断時は「戻り kTrue + outCancelled=kTrue」になる(戻り kFalse は
// 「対象0件で何も処理しなかった」ときだけ)。
// 実体は KESCMPeek.cpp。KESCMActionComponent.cpp から呼ぶ。
bool16 KESCMRefreshComparisonForSelectedPages(int32* outPages, int32* outChanged, bool16* outCancelled = nil, int32* outFailed = nil);

// 上記メニューの有効/無効判定(KESCMActionComponent.cpp の UpdateActionStates 用)。arm 済みかつ前面文書が
// Target なら kTrue(Source では無効=コンテキストメニューでは項目ごと非表示になる想定)。実体は KESCMPeek.cpp。
bool16 KESCMRefreshComparisonAvailable();

#endif // __KESCMPeek_h__
