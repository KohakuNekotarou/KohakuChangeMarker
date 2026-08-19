//========================================================================================
//
//  KESCMPanelAlpha.h
//
//  パネルのフライアウト「Translucent Panel」/「Translucent Pages Panel」トグルの実体。
//
//  ★Windows 専用。Win32 の SetLayeredWindowAttributes でパネルの窓に alpha をかける
//    (Mac では下の公開関数は残るが KESCMApplyPanelTranslucency が何もしない)。
//  ★効くのは「フローティング中」のときだけ。ドッキング中のパネルはメインフレームの子窓に
//    なるため単独では透かせない(その場合は何もしない=フラグだけ立つ)。
//
//  ★★対象は**3つ**(⚠2026-08-17 訂正＝ここは長く「対象は2つ」のままだった。3つ目は 2026-08-13 に
//    足してあり、**この同じファイルの下のほうにその説明が書いてある**＝新旧が同居していた):
//     ①自分のパネル ②**本体のページパネル**(2026-08-06) ③**自分のブック比較ダイアログ**(2026-08-13)
//     それぞれ独立したトグルを持つ。実装は KESCMPanelAlpha.cpp 内で1本化されていて、
//     ①②は WidgetID(数値)で狙い撃ちする ＝ 窓タイトル(UI 言語で変わる)に依存しない
//     (③だけは窓の見つけ方が違う。下の専用ブロックを見よ)。
//
//  技術的根拠(実測の全記録) = docs/ai-notes/win32-window-transparency.md
//                             memory/win32-window-alpha-transparency.md
//
//========================================================================================

#ifndef __KESCMPanelAlpha_h__
#define __KESCMPanelAlpha_h__

#include "BaseType.h"

// トグルの現在状態(★既定 OFF)。
bool16	KESCMGetPanelTranslucent();

// トグル状態を設定する。★フラグを更新するだけで窓には触らない
// (起動時の設定復元ではパネルがまだ存在しないため、適用と分離してある)。
void	KESCMSetPanelTranslucent(bool16 on);

// 現在のフラグをパネルの窓へ反映する。
//  - パネルが見つからない / ドッキング中 のときは何もしない(エラーにしない)
//  - 呼ぶ場所は**3つ**(2026-08-17 訂正。⚠旧記述は KESCMPanelObserver.cpp を挙げていたが、あちらが
//    呼ぶのは KESCMApplyAllPanelTranslucency のほうで、この関数ではない。逆に**同じファイルの
//    IMouseRollOver の2つ**を数えていなかった):
//      ①メニュー押下時 = KESCMActionComponent.cpp の kKESCMPopupTranslucentPanelActionID
//      ②③カーソルの出入り = KESCMPanelAlpha.cpp の KESCMPanelRollOver::MouseEnter / MouseLeave
//         (どちらもトグル OFF なら呼ばずに返る)
//  - 返り値: 実際に窓へ alpha を設定できたら kTrue。パネルが無い/ドッキング中/Mac なら kFalse
//    (メニュー押下時のステータス文言を「効いた」「ドッキング中なので効かない」で分けるために使う)
bool16	KESCMApplyPanelTranslucency();

//----------------------------------------------------------------------------------------
// 本体のページパネル用(2026-08-06 追加)。上の3つと同じ意味・同じ実装で、対象だけが違う。
//  ★あちらは自分のパネルではないので IMouseRollOver を付けられない。ホバーで不透明に戻す判定は
//    Win32 フック(OBJID_CURSOR)側だけで成立している。
//----------------------------------------------------------------------------------------
bool16	KESCMGetPagesPanelTranslucent();
void	KESCMSetPagesPanelTranslucent(bool16 on);
bool16	KESCMApplyPagesPanelTranslucency();

//----------------------------------------------------------------------------------------
// ブック比較ダイアログ用(2026-08-13 追加。ユーザー要望「ダイアログも半透明に出来る様に」)。
//
//  ★★上の2つと違うのは**窓の見つけ方だけ**。あちらは WidgetID で本体のパネルマネージャに聞けるが、
//    ダイアログはパネルではないので同じ道が無い ---- 代わりに**ダイアログ側が窓を教える**
//    (KESCMBookDialog.cpp が窓を用意した直後に KESCMSetBookDialogWindow を呼ぶ)。
//  ★もう1つの違い: ダイアログは**それ自身がトップレベル窓**なので、パネルのような
//    「今どのドックに載っているか」の解決が要らない ＝ ドッキング中は効かない、という制限も無い。
//  ⚠**窓は開いている間しか無い**。トグルだけ ON にして閉じている間は何も起きず、次に開いたときに
//    効く(適用は KESCMBookDialog.cpp が開くたびに呼ぶ)。
//  ★2026-08-17(監査 B-U9)＝その「開くたびの無条件 Apply」は**正しい**(⚠理由は「kCacheDialog が
//    前回の alpha ごと窓を返すから」ではない——**窓は開くたびに新品で必ず不透明から始まる**ので、
//    ON なら 77 を毎回書き直すしかない。実測＝3回開いて HWND は3つとも別)。
//    その上で**一度も透かしていない窓には触らない**ようにした＝未 layered かつ狙いが 255 なら何もしない。
//----------------------------------------------------------------------------------------
bool16	KESCMGetBookDialogTranslucent();
void	KESCMSetBookDialogTranslucent(bool16 on);
bool16	KESCMApplyBookDialogTranslucency();

// ブック比較ダイアログの窓を教える。★呼び手は**1つだけ**＝KESCMBookDialog.cpp が**開くたび**に呼ぶ。
//  ⚠2026-08-19(B-U9)訂正＝旧記述は「(閉じるときは nil)」と書いていたが、**そう呼ぶ呼び手は一度も
//    存在しない**。閉じるときに何も要らないのは、窓が死ぬからではなく(ハンドルの値は別の窓へ配り
//    直される)、**登録時にこの窓の題名も控えて以後の使用を毎回それと突き合わせる**から＝使い回された
//    ハンドルは書く前に捨てられる。呼ぶ側にも同じことが書いてある(KESCMBookDialog.cpp の
//    「there is no matching "forget" call to write」)。nil を渡すこと自体は今も安全。
// ★HWND をこのヘッダーに出さないために void* で受ける ---- KESCMPanelAlpha.h は BaseType.h しか
//   include しておらず、windows.h を持ち込むと他の .cpp 全部に波及する。実体側でキャストする。
void	KESCMSetBookDialogWindow(void* sysWindow);

// (ツールボックス用の3つは 2026-08-07 に追加し、同日ユーザー判断で撤去した。対象を1つ足すだけで
//  動いたが、本体 UI の見た目を変える機能なので KESCM には載せない、という判断。実測値の記録は
//  memory/translucent-toolbox-idea.md に残してある。)

// 全対象へ貼り直す。★パネルの表示状態が変わったとき等、「どれが対象か」を呼び出し側が
//   知らなくてよい場面で使う(対象が増えても呼び出し側を直さずに済む)。
void	KESCMApplyAllPanelTranslucency();

// パネルの表示状態変化(開く/閉じる/ドッキング⇄フローティング)の購読を始める。
// ★呼ぶ場所は**2か所**で、何度呼んでも安全: KESCMUIStartup::Startup と パネルの AutoAttach
//   (KESCMPanelObserver.cpp)。★2つめは念のためではない ---- パネルマネージャは本体の起動シーケンスの
//   途中で立ち上がるので Startup の時点では nil のことがあり、その購読は AutoAttach の回で拾う。
//   どの購読も IsAttached を先に聞くので、重ねて呼んでも二重には付かない。
// ★仕組み: kPanelManagerBoss の IID_IPANELMGR subject に飛ぶ kPaletteVisibilityChangedMessage を
//   受ける(2026-07-29 に Debug 版の Spy で実測して特定。ドッキング切り替えのたびに飛ぶ)。
//   もう1つ kAppBoss / IID_IAPPLICATION も購読する(関数本体のコメント参照)。
void	KESCMAttachPanelVisibilityObserver();

// 上が張った購読を全部外す。プラグイン終了時(KESCMUIStartup::Shutdown)から、**KESCMShutdownPanelAlpha
// より前に**呼ぶ ＝ 通知を止めてからタイマーとフックを畳む。
// ★なぜ要るか(2026-08-12): 購読している間、セッションが持っているのはこの .pln の中へのポインタで、
//   終了処理中のパネル破棄は実際に通知を飛ばす ---- 消えかけのコードで Update が走ることになる。
//   KBS が 2026-08-08 に同じ理由で新設した対で、こちらには来ていなかった分。
void	KESCMDetachPanelVisibilityObserver();

// (KESCMResetPanelHover は 2026-07-29 に撤去。「カーソルが乗っているか」は旗で持たず
//  KESCMApplyPanelTranslucency のたびに Win32 で実測する方式へ変更したので、落とすべき状態が
//  そもそも存在しない＝張り付きが構造的に起きない。)

// 遅延再適用に使う one-shot タイマーの後始末。プラグイン終了時(KESCMUIStartup::Shutdown)から
// 呼ぶ。★ICallbackTimer のコールバックは参照カウントされない生関数ポインタなので、予約を残した
// まま .pln が降りるとクラッシュする。実体は KESCMPanelAlpha.cpp(Mac では空実装)。
void	KESCMShutdownPanelAlpha();

#endif // __KESCMPanelAlpha_h__
