//========================================================================================
//
//  KESCMPeek.h
//
//  ツール(左ボタン)の「peek(覗き)」。修飾キー＋ツール左ボタンを押している間だけ、カーソル下スプレッドの旧版を
//  表示する(または CMYK をサンプリングする)。離すと元に戻す。peek 状態(arm 済みの target/source DB、
//  押下中フラグ)と起動/終了サービスを所有する(旧・中ボタンのイベントウォッチャは撤去済み 2026-07-13)。
//  arm/disarm/状態アクセサは KESCMCore.h にある。
//
//========================================================================================
#ifndef __KESCMPeek_h__
#define __KESCMPeek_h__

#include "PMReal.h"
#include "CursorSpec.h"		// CreateCursorBitmapProc(Alt+左 CMYK のカスタムカーソル)

class IControlView;			// KESCMToolCursorShouldBeBlack の引数(前方宣言で足りる)

// 常時表示マークの画面上の「基準」不透明度。印刷設定から決まる(印刷ON => 選択不透明度25%/75%、印刷OFF => 1.0)。
// peek を離したときの経路と KESCMDoSetPrintMarks が使う。実体は KESCMPeek.cpp。
PMReal KESCMBaseScreenOpacity();

// ★ビューポート同期(Sync Layout Views / Align Other Views)のホットパス用キャッシュを捨てる(2026-07-25 追補)。
// スクロール追従は毎秒数十回の通知で駆動されるため、KESCMPeek.cpp 側で「文書のページ矩形表」と
// 「除外対応表」と「前回複製した手本の状態」を短時間だけ覚えている(既定 250ms で自動失効)。
// 前提が変わったことが分かっている場面では、失効を待たずにここで明示的に捨てる:
//   ・arm / disarm(比較対象の組み合わせが変わる)
//   ・Sync Layout Views の ON/OFF、Align の実行(明示操作なので必ず最新で計算する)
//   ・文書クローズ(ページ構成もポインタも当てにならない)
//   ・再比較(登録 Add/Remove で対応表が動く)
//   ・Shutdown(保持物を残さない)
// ★キャッシュは「正しさ」ではなく「速さ」のためのものなので、呼び忘れても最大 250ms 遅れて追従する
//   だけで壊れない。実体は KESCMPeek.cpp。
void KESCMInvalidateSyncCaches();

// トラッカー(左ボタン)用の共有入口。KESCM ツール選択中に左ボタンを押している間だけ、押下時の修飾キーで
// 選んだ動作を行う。Begin=押下(押下時の修飾キー状態を渡す)、End=解放。実体は
// KESCMPeek.cpp(peek の file-local 状態と描画状態にアクセスできる唯一の場所)。KESCMTracker.cpp から呼ぶ。
// (由来: いずれも旧・中ボタン＋修飾キーのジェスチャをツールの左ボタンへ移植したもの。)
//   ・修飾なし        = マーク一時表示(reveal) / Hold to Hide 反転(常時表示の枠を押下中だけ隠す)
//   ・Shift           = 旧版べた載せ peek 100%(旧・中ボタン Shift+ミドル)
//   ・Shift+Alt       = 旧版べた載せ peek 50%(旧・中ボタン Shift+Alt+ミドル)
//   ・Alt(単独)       = クリック点の CMYK 生値を新/旧サンプリングしステータス行へ(旧・中ボタン Shift+Ctrl+Alt+ミドル)
//   ・Ctrl(cmd)含む  = 未対応。何もしない(再比較はページ右クリックメニュー/パネル操作はフライアウトへ移行済み)。
//   ・Mac の Control  = 未対応(下の macCtrlDown 参照)。
// ★キー名の対応(SDK の IEvent が吸収する): OptionAltKeyDown = Win の Alt / Mac の Option、
//   CmdKeyDown = Win の Ctrl / Mac の Command。よって上表の "Alt" は Mac では Option になる。
void KESCMTrackerRevealBegin(bool16 shiftDown, bool16 altDown, bool16 cmdDown, bool16 macCtrlDown = kFalse);
void KESCMTrackerRevealEnd();

// 修飾キー→ジェスチャの分類。★割当の定義はこの1本だけ(2026-07-15 に3箇所の独立判定を統合):
// KESCMTracker.cpp の Hide/Show 事前判定・RevealBegin の分岐・Hold to Hide の temp-hide 判定が
// すべてこれを使う。ジェスチャ割当を変えるときは KESCMClassifyGesture(KESCMPeek.cpp)だけを直す。
enum KESCMGesture
{
	kKESCMGestureNone = 0,	// Ctrl(cmd)または Mac の Control を含む=未割当(何もしない)
	kKESCMGestureReveal,	// 修飾なし: マーク一時表示(reveal) / Hold to Hide の temp-hide
	kKESCMGesturePeek100,	// Shift: 旧版べた載せ peek 100%
	kKESCMGesturePeek50,	// Shift+Alt(Mac: Shift+Option): 旧版べた載せ peek 50%
	kKESCMGestureCmyk		// Alt 単独(Mac: Option 単独): CMYK 色サンプリング(カーソル表示)
};
// ★macCtrlDown(= IEvent::MacCtrlDown。Windows では常に kFalse)は「未割当」に倒す(2026-07-25 追補 Mac 対応):
//   macOS の Control+クリックは OS/アプリが副ボタン(コンテキストメニュー)として扱う標準ジェスチャなので、
//   もし左ボタン押下として届いても KESCM が reveal を横取りしないようにする。cmdDown(Mac の Command)を
//   未割当にしているのと同じ趣旨。既定引数 kFalse なので Windows 側の呼び出しは影響を受けない。
KESCMGesture KESCMClassifyGesture(bool16 shiftDown, bool16 altDown, bool16 cmdDown, bool16 macCtrlDown = kFalse);

// Alt+左「色比較」のカスタムカーソル(CMYK をカーソル自身に描く)。KESCMTracker.cpp が使う:
// BeginTracking で KESCMTrackerRevealBegin 後、Pending が立っていれば
// ChangeModalCursor(CursorSpec(KESCMTrackerCmykCursorProc(), ...)) を呼ぶ。実体は KESCMPeek.cpp。
bool16 KESCMTrackerHasPendingCmykCursor();
CreateCursorBitmapProc KESCMTrackerCmykCursorProc();

// Alt+左(単独)の CMYK カーソルが「実際に出る」条件か(=arm 済み・比較文書生存・Target 窓上)。
// KESCMTracker.cpp の BeginTracking が Hide/Show ラップの要否判定に使う。RevealBegin の Alt 分岐と
// 同じ判定を1本で共有し、CMYK カーソルが出ないのに Hide/Show だけ走ってカーソルがまたたくのを防ぐ
// (2026-07-15)。実体は KESCMPeek.cpp。
bool16 KESCMTrackerCmykCursorWouldShow();

// ツール常時✓カーソルを黒で出してよいか=「Start 中(比較文書生存)かつ viewUnderMouse が Target 文書の
// レイアウトビュー」。それ以外(Source・第3の文書・未 Start・view 不明)は kFalse=白抜き✓を出す
// (「ここではツールは効かない」の明示。ユーザー指定 2026-07-15)。KESCMCursorProvider.cpp の
// GetCursor から毎ムーブ呼ばれる(文書数×ビュー数の走査のみ=軽量)。実体は KESCMPeek.cpp。
bool16 KESCMToolCursorShouldBeBlack(IControlView* viewUnderMouse);

// ドラッグ中の CMYK ライブ更新。トラッカーの ContinueTracking(マウス移動)から呼ぶ。現在のマウス位置で
// CMYK を再サンプルし(スロットル付き=連続ラスタ化で重くならないように)、値が変わったら
// sCmykCursorText を更新して kTrue を返す。呼び出し側はそのとき ChangeModalCursor でカーソルを描き直す。
// Alt+左 CMYK モード中(Pending)でなければ、またはスロットル中/ページ外/値不変なら kFalse。実体は KESCMPeek.cpp。
bool16 KESCMTrackerUpdateCmykDrag();

// ページパネルのページ右クリック「KESCM: Refresh Page Comparison」の実体。選択ページの比較を再検出して
// 枠/サムネイルを更新する(旧 Ctrl+ミドルのスプレッド再比較を移設。2026-07-13)。arm 済み(Start 後)かつ
// 前面文書が Target のときだけ動く(★2026-07-15 Target 限定=ユーザー指定)。outPages=実際に再比較した
// ページ数 / outChanged=うち変化ページ数(いずれも nil 可)。戻り=1ページ以上処理したか。
// 実体は KESCMPeek.cpp。KESCMActionComponent.cpp から呼ぶ。
bool16 KESCMRefreshComparisonForSelectedPages(int32* outPages, int32* outChanged);

// 上記メニューの有効/無効判定(KESCMActionComponent.cpp の UpdateActionStates 用)。arm 済みかつ前面文書が
// Target なら kTrue(Source では無効=コンテキストメニューでは項目ごと非表示になる想定)。実体は KESCMPeek.cpp。
bool16 KESCMRefreshComparisonAvailable();

#endif // __KESCMPeek_h__
