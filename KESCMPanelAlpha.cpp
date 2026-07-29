//========================================================================================
//
//  KESCMPanelAlpha.cpp
//
//  パネル半透明トグルの実装。★Win32 依存はこのファイルに閉じ込める。
//
//  ★手順(2026-07-29 の実測に基づく。変更するときは必ず docs/ai-notes/win32-window-transparency.md を読むこと):
//    1. cls=="OWL.Palette" かつ title==パネル表示名 の窓を自プロセス内から探す
//    2. GetAncestor(GA_ROOT) で「今の」トップレベル窓を得る
//    3. それが "indesign"(メインフレーム)ならドック内で展開中 → 何もしない
//    4. "OWL.Dock"(フローティング) / "OWL.FrameDrawer"(アイコンをクリックしたドロワー展開) なら
//       SetLayeredWindowAttributes で alpha を設定
//
//  ★OWL.Dock の HWND はパネルを閉じて開き直すと変わる(使い回しプールから再割り当て)。
//    OWL.Palette の HWND は不変。だから HWND を保持せず毎回探し直す。
//  ★OWL.Dock は InDesign 自身が WS_EX_LAYERED を立てている(EXSTYLE=0x08080000)。
//    スタイルの追加も除去も不要。★除去すると本体の描画が壊れる(復元は alpha=255 のみ)。
//  ★子窓(OWL.Palette)に直接 WS_EX_LAYERED を立てるな。半透明にならず色が壊れる(実測)。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// プロジェクト内:
#include "KESCMPanelAlpha.h"
#include "KESCMConstants.h"		// kKESCMPanelAlphaValue
#include "KESCMID.h"			// kKESCMDisplayName(=窓 title で引くパネル名)/独自 IID・ImplID

// パネルの表示状態変化を購読するオブザーバ用:
#include "CObserver.h"
#include "ISubject.h"			// AttachObserver / IsAttached
#include "ISession.h"			// GetExecutionContextSession(終了処理中は nil になり得る)
#include "IApplication.h"		// QueryPanelManager
#include "IActiveContext.h"		// オブザーバ実体の同居先(kActiveContextBoss)
#include "IPanelMgr.h"			// IID_IPANELMGR(購読する subject)
#include "AppUIID.h"			// ★kPaletteVisibilityChangedMessage(公開ヘッダー。AppUIID.h:325)
#include "ShuksanID.h"			// kApplicationSuspendMsg(ShuksanID.h:1151。別アプリへ切り替わった通知)

// 通知の「あと」に窓が作り直されるので、一巡させてから貼り直すための one-shot タイマー:
#include "ICallbackTimer.h"		// StartTimer/StopTimer(IIdleTask 派生。kEndOfTime もここ経由)
#include "CreateObject.h"		// ::CreateObject(kCallbackTimerBoss, IID_ICALLBACKTIMER)

// カーソルが乗っている間だけ不透明に戻すため:
#include "CPMUnknown.h"			// 実装の基底
#include "IMouseRollOver.h"		// MouseEnter/MouseOver/MouseLeave(ui/IMouseRollOver.h)

// ★windows.h は SDK ヘッダーより後に置くこと(マクロが SDK 側の名前とぶつからないように)。
#ifdef WINDOWS
#include <windows.h>
#endif

// トグル状態(セッション内で保持。永続化は KESCMPanelState.cpp が担当)。★既定 OFF
static bool16 sPanelTranslucent = kFalse;

#ifdef WINDOWS

// カーソルが対象窓(パネルが今載っているトップレベル窓)の上にあるか。
//
// ★★旗を持たず毎回実測する(2026-07-29 に変更)。以前は IMouseRollOver が上下させる static な旗
//   だったが、次の 2 つの弱点があった。実測ならどちらも構造的に起きない:
//     (a)MouseLeave は「カーソルを乗せたままパネルを閉じる/ドッキングする/別アプリへ切り替える」
//        経路では飛ばない。取りこぼすと「乗っている」が張り付き、トグルが ON でも一切薄くならない。
//     (b)IMouseRollOver が見るのは**パネル本体の widget だけ**なので、タブ帯("Kohaku Change Marker"
//        と出ている帯)やタイトル帯(<< / x の帯)にカーソルを乗せても反応しない(ユーザー要望の発端)。
//
// ★対象窓はタブ帯もタイトル帯もパネル本体も含む 1 つの窓なので、この判定だけで「パネルのどこかに
//   乗っている」が丸ごと取れる。★★タブ帯を SDK 側で取る道が無いことは実機で確定済み(2026-07-29):
//   パネル widget の親は kOWLHostedPanelWrapperBoss(0x1645a) の 1 段で尽き(QueryParent()==nil)、
//   しかもその bbox はパネル本体と同一だった ＝ クロムは widget ツリーの外(OWL 側)。
//
// ★矩形(GetWindowRect+PtInRect)だけで判定してはいけない。他の窓が上に重なっていても「乗っている」に
//   なってしまう。矩形は安い足切りに使い、確定は WindowFromPoint → GA_ROOT の一致で行う。
//   ★この足切りのおかげで、カーソルがパネルから離れている間は整数比較だけで終わる。
static bool KESCMCursorOverWindow(HWND target)
{
	if (target == nullptr)
		return false;

	POINT pt;
	if (!::GetCursorPos(&pt))
		return false;

	RECT rc;
	if (!::GetWindowRect(target, &rc) || !::PtInRect(&rc, pt))
		return false;		// 矩形の外 = 確実に乗っていない(大半のマウス移動はここで終わる)

	HWND under = ::WindowFromPoint(pt);
	return (under != nullptr && ::GetAncestor(under, GA_ROOT) == target);
}

// ★実効 alpha ＝ トグルが ON で、かつカーソルが乗っていないときだけ薄くする。
//   ここ1箇所に集約しておくこと(適用側・フックの判定側の両方から使う)。
//   ★OFF ならカーソル位置すら見ない(この機能は ON のときだけ動く=ユーザー方針 2026-07-29)。
//   ※Mac には適用そのものが無いので置かない(未使用関数の警告を出さないため)。
static uint8 KESCMEffectiveAlpha(HWND target)
{
	if (!sPanelTranslucent)
		return 255;

	return KESCMCursorOverWindow(target) ? 255 : kKESCMPanelAlphaValue;
}

// ★Win32 イベントフックの出し入れ(実体は下の WINDOWS ブロック)。ON の間だけ張る。
static void KESCMInstallWinEventHook();
static void KESCMRemoveWinEventHook();
#endif

bool16 KESCMGetPanelTranslucent()
{
	return sPanelTranslucent;
}

void KESCMSetPanelTranslucent(bool16 on)
{
	sPanelTranslucent = on;

#ifdef WINDOWS
	// ★「ドック内展開 ⇄ フローティング」と「ドロワー展開 → フローティング」は SDK 通知が
	//   1本も飛ばない(2026 で Debug/Release 両方から確認)。唯一の手掛かりが Win32 の
	//   親変更イベントなので、ON の間だけフックを張る。OFF なら外す(常駐させない)。
	if (on)
		KESCMInstallWinEventHook();
	else
		KESCMRemoveWinEventHook();
#endif
}

#ifdef WINDOWS

// ★探すパネルの window title。KESCM_enUS.fr / KESCM_jaJP.fr の kKESCMPanelTitleKey が
//   両ロケールとも kKESCMDisplayName と同じ "Kohaku Change Marker" なので、そこから作る
//   (=ロケール非依存)。★.fr のパネル名だけを変えるとここが一致しなくなり、機能が黙って
//   効かなくなる。パネル名を変えるときは両方を揃えること。
#define KESCM_WIDEN_(x)	L ## x
#define KESCM_WIDEN(x)	KESCM_WIDEN_(x)
static const wchar_t* const kKESCMPaletteWindowTitle = KESCM_WIDEN(kKESCMDisplayName);

// EnumWindows/EnumChildWindows のコールバックへの受け渡し用。
struct KESCMFindPaletteCtx
{
	const wchar_t*	fTitle;
	DWORD			fPid;
	HWND			fFound;
};

static bool KESCMWindowMatches(HWND h, KESCMFindPaletteCtx* ctx)
{
	wchar_t cls[64] = { 0 };
	if (::GetClassNameW(h, cls, 64) == 0)
		return false;
	if (::wcscmp(cls, L"OWL.Palette") != 0)
		return false;

	wchar_t title[256] = { 0 };
	::GetWindowTextW(h, title, 256);
	return ::wcscmp(title, ctx->fTitle) == 0;
}

static BOOL CALLBACK KESCMEnumChildProc(HWND h, LPARAM lp)
{
	KESCMFindPaletteCtx* ctx = reinterpret_cast<KESCMFindPaletteCtx*>(lp);
	if (KESCMWindowMatches(h, ctx))
	{
		ctx->fFound = h;
		return FALSE;		// 見つけたので打ち切る
	}
	return TRUE;
}

static BOOL CALLBACK KESCMEnumTopProc(HWND h, LPARAM lp)
{
	KESCMFindPaletteCtx* ctx = reinterpret_cast<KESCMFindPaletteCtx*>(lp);

	DWORD pid = 0;
	::GetWindowThreadProcessId(h, &pid);
	if (pid != ctx->fPid)
		return TRUE;		// 他プロセスの窓は見ない

	if (KESCMWindowMatches(h, ctx))
	{
		ctx->fFound = h;
		return FALSE;
	}
	::EnumChildWindows(h, KESCMEnumChildProc, lp);
	return (ctx->fFound == nullptr) ? TRUE : FALSE;
}

// パネル名から OWL.Palette の HWND を探す。見つからなければ nullptr。
static HWND KESCMFindPaletteWindow(const wchar_t* title)
{
	KESCMFindPaletteCtx ctx;
	ctx.fTitle = title;
	ctx.fPid   = ::GetCurrentProcessId();
	ctx.fFound = nullptr;

	::EnumWindows(KESCMEnumTopProc, reinterpret_cast<LPARAM>(&ctx));
	return ctx.fFound;
}

// ★見つけた OWL.Palette を覚えておく。パネルを閉じて開き直しても OWL.Palette の HWND は
//   変わらない(変わるのは親の OWL.Dock のほう)ので、キャッシュが効く。
//   ここを入れる理由: 購読している kPaletteVisibilityChangedMessage は「文書を1つ開く」だけでも
//   複数回飛ぶ(2026-07-29 実測)。そのたびに全窓を EnumWindows で舐めるのは無駄。
static HWND sPaletteWnd = nullptr;

// キャッシュ優先でパネル窓を得る。★ハンドルは OS が使い回すので、生存確認だけでなく
//   クラス名/タイトルの一致も見てから使う(全窓走査より遥かに安い)。
static HWND KESCMQueryPaletteWindow()
{
	KESCMFindPaletteCtx ctx;
	ctx.fTitle = kKESCMPaletteWindowTitle;
	ctx.fPid   = ::GetCurrentProcessId();
	ctx.fFound = nullptr;

	if (sPaletteWnd != nullptr && ::IsWindow(sPaletteWnd) && KESCMWindowMatches(sPaletteWnd, &ctx))
		return sPaletteWnd;

	sPaletteWnd = KESCMFindPaletteWindow(kKESCMPaletteWindowTitle);
	return sPaletteWnd;
}

// 窓のクラス名が期待どおりか。
static bool KESCMClassIs(HWND h, const wchar_t* wanted)
{
	if (h == nullptr)
		return false;
	wchar_t cls[64] = { 0 };
	if (::GetClassNameW(h, cls, 64) == 0)
		return false;
	return ::wcscmp(cls, wanted) == 0;
}

// パネルが載っている「今の」トップレベル窓のうち、単独で透かせるものだけを返す。
// ドック内で展開中(GA_ROOT がメインフレーム)なら nullptr。
//
// ★GA_ROOT は 3 分岐する(2026-07-29 に Pages パネルで 3 状態を実測。OWL.Palette 側の HWND は不変):
//     "indesign"        ドック内で展開(メインウィンドウにくっついた状態)。EXSTYLE=0x00000100
//                       ＝WS_EX_LAYERED が無く、立てるとアプリ全体が対象になる → 単独制御は不可
//     "OWL.Dock"        フローティング。EXSTYLE=0x08080000
//     "OWL.FrameDrawer" アイコンをクリックしたドロワー展開。EXSTYLE=0x08080000
//   後ろ 2 つは InDesign 自身が WS_EX_LAYERED を立てているので、まったく同じ扱いでよい。
// ⚠"OWL.Dock" だけで判定していると、ドロワー展開が黙って対象外になる(2026-07-29 まで実際にそうだった)。
static HWND KESCMQueryTranslucentTarget(HWND palette)
{
	if (palette == nullptr)
		return nullptr;

	HWND root = ::GetAncestor(palette, GA_ROOT);
	if (root == nullptr || root == palette)
		return nullptr;

	wchar_t cls[64] = { 0 };
	if (::GetClassNameW(root, cls, 64) == 0)
		return nullptr;

	if (::wcscmp(cls, L"OWL.Dock") == 0 || ::wcscmp(cls, L"OWL.FrameDrawer") == 0)
		return root;

	return nullptr;		// "indesign" = メインフレーム = ドック内で展開中
}

#endif // WINDOWS

bool16 KESCMApplyPanelTranslucency()
{
#ifdef WINDOWS
	HWND palette = KESCMQueryPaletteWindow();
	HWND target  = KESCMQueryTranslucentTarget(palette);
	if (target == nullptr)
		return kFalse;		// パネルが無い / ドック内で展開中 → 何もしない

	// ★カーソルが乗っている間は不透明に戻す(KESCMEffectiveAlpha が今の位置を実測して判断)。
	//   タブ帯・タイトル帯の上でも「乗っている」になる ＝ 対象窓がそれらを含むため。
	const BYTE alpha = KESCMEffectiveAlpha(target);

	// ★WS_EX_LAYERED は InDesign が最初から立てているので触らない。
	const BOOL ok = ::SetLayeredWindowAttributes(target, 0, alpha, LWA_ALPHA);

	// ★影(OWL.ShadowView)も一緒に処理する。影は Dock の owner にあたる「別のトップレベル窓」なので、
	//   Dock だけ透かすと影は不透明のまま残り、その部分だけ濃く見えて違和感が出る
	//   (2026-07-29 ユーザー報告: 「ドラッグ中は綺麗だが離すと影の部分だけ濃い」
	//    = ドラッグ中は影が出ず、確定した瞬間に影が描かれるため)。半透明の間は影ごと隠す。
	//
	// ★★ここで SetLayeredWindowAttributes を使ってはいけない(2026-07-29 に実機で壊して確認):
	//   影は UpdateLayeredWindow による per-pixel alpha(ぼかしのある影)で描かれている。Win32 では
	//   一様 alpha(SetLayeredWindowAttributes)と per-pixel alpha は排他で、一度でも前者を設定すると
	//   255 に戻しても per-pixel 描画には復帰せず、OFF にしたとき影が不自然な塊になる。
	//   表示/非表示の切り替えなら描画方式に触らないので安全。
	//   ※ SW_SHOWNA = アクティブ化せずに表示(影の窓は WS_EX_NOACTIVATE で、前面化させたくない)。
	//   ※ドロワー展開("OWL.FrameDrawer")の owner が ShadowView でない場合は、下の判定で素通りする。
	//   ★★影の出し入れは alpha ではなく**トグルの ON/OFF だけ**で決める(2026-07-29 修正)。
	//     ON の間はカーソルで不透明に戻っていても影は隠したままにする。
	//     ⚠理由(ユーザー報告の不具合): パネルはタブ帯／タイトル帯をつかんで動かすが、そこは
	//       「カーソルが乗っている」＝不透明なので、alpha で判定すると**ドラッグ中に影を表示**
	//       してしまう。InDesign はドラッグ中に影を出さない(確定した瞬間に描く)作りなので、
	//       こちらが強引に出すと**位置が更新されない影が元の場所に取り残される**。
	//     ★副次効果: カーソルがパネルを出入りするたびに影が点いたり消えたりするちらつきも消える。
	const bool16 hideShadow = KESCMGetPanelTranslucent();
	HWND shadow = ::GetWindow(target, GW_OWNER);
	if (KESCMClassIs(shadow, L"OWL.ShadowView"))
		::ShowWindow(shadow, hideShadow ? SW_HIDE : SW_SHOWNA);

	return ok ? kTrue : kFalse;
#else
	return kFalse;		// Mac: 半透明化の手段が無いので常に「適用しなかった」
#endif
}

//========================================================================================
// 遅延再適用 — 窓の作り直しに負けないようにする
//
//   ★なぜ要るか(2026-07-29 実測で判明): 下のオブザーバが kPaletteVisibilityChangedMessage を
//     受けてすぐ適用しても、その直後に InDesign がトップレベル窓を作り直すことがあり、書いた
//     alpha ごと捨てられる。症状は「アイコンからフローティングに戻すと不透明に戻る。ただし
//     メニューで OFF→ON し直すと効く」。
//     ★診断値が決定的だった —— 適用直後の読み返しは rb=128 で成功しているのに、外部ツールで
//     後から測ると alpha=255。しかも適用先 HWND(dk=0x5B0BF0)と、そのとき実在した窓(0x21656)が
//     別物だった。＝「値を上書きされた」のではなく「別の窓に作り替えられた」。
//   ★対策: 通知の直後に加えて、イベントを一巡させてから「そのときの GA_ROOT」へ貼り直す。
//     窓が落ち着くまで数回だけ追いかける(kKESCMPanelAlphaReapplyTries)。回数で必ず止まる。
//   ⚠ICallbackTimer のコールバックは参照カウントされない生関数ポインタなので、予約を残したまま
//     この .pln が降りるとクラッシュする → KESCMShutdownPanelAlpha() で必ず停止・解放する。
//========================================================================================

#ifdef WINDOWS

static ICallbackTimer* sReapplyTimer = nil;
static int32           sReapplyLeft  = 0;			// 残り回数(0 で打ち切り＝暴走止め)

static uint32 KESCMReapplyTimerProc(void* refPtr);

// 通知を受けた側から呼ぶ。窓が落ち着くまでの貼り直しを(再)開始する。
// ★★「予約中なら重ねない」門番(旧 sReapplyPending)は撤去した(2026-07-29 の自己レビュー)。
//   下の連鎖が何かの拍子に途切れると、その旗が立ったまま戻らず**この関数が以後ずっと no-op**になる
//   ＝そのセッションでは二度と貼り直せない、という壊れ方をする構造だった。ICallbackTimer は
//   1 インスタンス 1 予約なので、生きている予約に重ねて StartTimer しても置き換わるだけ。
//   新しい通知が来たら無条件に武装し直す方が、連鎖の実装差に関わらず確実に動く
//   (回数も毎回 kKESCMPanelAlphaReapplyTries に戻るので、実質デバウンスとして働く)。
static void KESCMScheduleReapply()
{
	sReapplyLeft = kKESCMPanelAlphaReapplyTries;	// 通知のたびに回数を戻す
	if (sReapplyLeft <= 0)
		return;		// 定数 0 = 遅延再適用そのものを止める設定

	if (sReapplyTimer == nil)
		sReapplyTimer = (ICallbackTimer*)::CreateObject(kCallbackTimerBoss, IID_ICALLBACKTIMER);
	if (sReapplyTimer == nil)
		return;

	sReapplyTimer->StartTimer(KESCMReapplyTimerProc, kKESCMPanelAlphaReapplyDelayMillis, nil);
}

static uint32 KESCMReapplyTimerProc(void* /*refPtr*/)
{
	--sReapplyLeft;

	// ★ここで Release しない(RunTask の実行中に自分を解放すると自己破棄になる)。
	//   解放は KESCMShutdownPanelAlpha() の1箇所に集約する。
	if (!KESCMGetPanelTranslucent())
	{
		sReapplyLeft = 0;		// OFF になったので追いかけをやめる
		return IIdleTask::kEndOfTime;
	}

	KESCMApplyPanelTranslucency();

	// ★★連鎖は「戻り値」で行う(2026-07-29 修正)。以前はここから StartTimer を呼び直して
	//   いたが、その直後に kEndOfTime を返していたため予約が打ち消され、8 回のはずが
	//   2 回しか走っていなかった(実測 rp=2)。戻り値が再スケジュール値そのものなので、
	//   続けたいときは待ち時間を返せばよい。
	//   ⚠ICallbackTimer の公開契約は one-shot(ヘッダー: "register a one time only callback")なので、
	//     この連鎖は実装依存の観測に乗っている。万一効かない環境でも、次の通知で
	//     KESCMScheduleReapply が無条件に武装し直すので「二度と動かない」状態にはならない。
	if (sReapplyLeft > 0)
		return kKESCMPanelAlphaReapplyDelayMillis;

	// ★★戻り値は IIdleTask::RunTask の再スケジュール値。**0 は「すぐまた呼べ」**であって終了では
	//   ない(KESCMTracker.cpp で 0 を返して InDesign を固めた前科がある)。終わるなら kEndOfTime。
	return IIdleTask::kEndOfTime;
}

//========================================================================================
// ★★Win32 イベントフック — SDK 通知が飛ばない遷移を拾う唯一の手段(2026-07-29)
//
//   実測で確定したこと(Debug 2026 / Release 2026 の両方で一致):
//     ・kPaletteVisibilityChangedMessage は名前のとおり「**可視性**が変わったとき」だけ飛ぶ。
//       パネルの開閉・アイコン化・ドロワー展開では飛ぶが、**置き場所だけが変わる遷移**
//       (ドック内展開 ⇄ フローティング、ドロワー展開 → フローティング)では飛ばない。
//     ・kDockedPaletteAreaChangedByUserMsg は 2025 では飛んだが **2026 では飛ばない**
//       (しかも飛び先は kPanelManagerBoss ではなく kAppBoss だった)。
//     ・ビュー再計算(kFitInViewCmdBoss 等)への相乗りも考えたが、ドロワーからの引き出しでは
//       ドック幅が変わらないため何も起きず、これも使えない。
//   → 残る手掛かりは「OWL.Palette の親が付け替わる」という Win32 の事実だけ。
//
//   ★自プロセス限定 + WINEVENT_OUTOFCONTEXT(他プロセスへの DLL 注入なし)で影響範囲は閉じている。
//   ★ON の間だけ張り、OFF と終了時に必ず外す(UnhookWinEvent 漏れはリソースリークになる)。
//========================================================================================

static HWINEVENTHOOK sWinEventHook = nullptr;

static void CALLBACK KESCMWinEventProc(HWINEVENTHOOK /*hook*/, DWORD /*event*/, HWND /*hwnd*/,
									   LONG idObject, LONG idChild,
									   DWORD /*thread*/, DWORD /*time*/)
{
	// 見るのは 2 種類だけ:
	//   ①窓そのもののイベント(OBJID_WINDOW) = 窓の作り直し・移動。従来からの本命。
	//   ★②カーソルの移動(OBJID_CURSOR) = マウスが動いた合図(2026-07-29 追加)。これを拾うことで
	//     **タブ帯やタイトル帯にカーソルが乗ったとき**も不透明に戻せる(そこは widget ツリーの外なので
	//     SDK の IMouseRollOver では届かない)。フックも定期タイマーも増やさず、既に張ってある
	//     このフックへ流れてくるものを捨てずに使うだけ。
	//   ⚠子要素(idChild != CHILDID_SELF)は窓イベントのときだけ弾く。カーソルのイベントは
	//     idChild にカーソルの状態が入ることがあるため、ここで弾くと取りこぼす。
	const bool isWindowEvent = (idObject == OBJID_WINDOW && idChild == CHILDID_SELF);
	const bool isCursorEvent = (idObject == OBJID_CURSOR);
	if (!isWindowEvent && !isCursorEvent)
		return;

	// ★この機能はトグルが ON のときだけ動く(OFF ならフック自体を張っていないが、外れる直前の
	//   取りこぼしもここで弾く)。以降の判定に進むのは ON のときだけ。
	if (!KESCMGetPanelTranslucent() || sPaletteWnd == nullptr)
		return;

	// ★★キャッシュしたハンドルでも「今も自分のパネルか」を必ず確かめる(2026-07-29 の自己レビューで追加)。
	//   HWND は OS が使い回すので、パネルを閉じたあと同じ値が別の窓へ再割り当てされうる。検証せずに
	//   GA_ROOT を辿ると**他人のパネルを透かす**(しかもフックは OFF にするまで外れないので、
	//   ON のままパネルを閉じた後もここへ流れ込み続ける)。
	//   ⚠ここでは全窓走査(EnumWindows)はしない: 失効していたらキャッシュを捨てて戻るだけにして、
	//     引き直しは SDK 通知や Apply の経路(KESCMQueryPaletteWindow)へ任せる。このフックは
	//     大量に飛ぶので、1件あたりを軽く保つのが最優先。
	if (!::IsWindow(sPaletteWnd))
	{
		sPaletteWnd = nullptr;		// 失効(パネルを閉じた等)。以後このフックは上の行で即 return する
		return;
	}

	// ★クラス名/タイトルの照合は**窓のイベントのときだけ**行う(2026-07-29)。カーソルが動いただけの
	//   回まで GetClassNameW + GetWindowTextW を叩くのは無駄(秒間 60〜100 回走る)。
	//   ⚠安全性の根拠: HWND が別の窓へ使い回されるには、その窓が作られて**表示される**必要がある。
	//     表示は EVENT_OBJECT_SHOW ＝窓イベントなので、すり替わった瞬間には必ずこの照合を通る。
	//     非表示のままの窓は KESCMQueryTranslucentTarget が "OWL.Dock"/"OWL.FrameDrawer" 以外として
	//     弾くので、透かす対象にもならない。
	if (isWindowEvent)
	{
		KESCMFindPaletteCtx ctx;
		ctx.fTitle = kKESCMPaletteWindowTitle;
		ctx.fPid   = ::GetCurrentProcessId();
		ctx.fFound = nullptr;
		if (!KESCMWindowMatches(sPaletteWnd, &ctx))
		{
			sPaletteWnd = nullptr;
			return;
		}
	}

	// ★★当初は「hwnd == sPaletteWnd」で絞っていたが、**OWL.Palette 宛てのイベントは
	//   PARENTCHANGE も LOCATIONCHANGE も一度も飛んでこなかった**(実測 hk=1/0)。子ウィンドウの
	//   移動ではシステムがこれらを生成しないことがある。
	//   → 発信元は問わず、「イベントが来たら自分のパネルの今のトップレベル窓を引き直して、
	//     alpha がずれていたら貼る」方式にする。判定は GetAncestor + 属性読みだけで、
	//     ずれていなければ即 return するので、大量に飛んできても実害はない。
	HWND target = KESCMQueryTranslucentTarget(sPaletteWnd);
	if (target == nullptr)
		return;					// ドック内で展開中 = 対象外

	// ★★ここは移動中に何度も呼ばれる。「望みの状態と違うときだけ」実処理へ進むこと
	//   (実測では総イベント 1477 件に対し、実際の書き込みは 1 件だけだった)。

	// ①alpha が望みの値か(カーソルが乗っていれば不透明が「望みの値」になる)
	const BYTE want = KESCMEffectiveAlpha(target);
	BYTE  cur = 0;
	DWORD key = 0, flags = 0;
	const bool16 alphaOk = (::GetLayeredWindowAttributes(target, &key, &cur, &flags) && cur == want) ? kTrue : kFalse;

	// ②影(OWL.ShadowView)の表示状態が望みどおりか。
	//   ★パネルをドラッグで動かすと InDesign が影を出し直す。alpha だけを見ていると
	//     「影だけ戻って濃く見える」状態が残る(2026-07-29 実機で判明)。
	bool16 shadowOk = kTrue;
	HWND   shadow   = ::GetWindow(target, GW_OWNER);
	if (KESCMClassIs(shadow, L"OWL.ShadowView"))
	{
		const bool16 visible = ::IsWindowVisible(shadow) ? kTrue : kFalse;
		// ★望みの状態は alpha ではなく**トグルの ON/OFF**で決まる(上の適用側と必ず揃えること)。
		//   ここに到達するのは ON のときだけなので、望みは常に「隠れている」。
		shadowOk = (visible == kFalse);
	}

	if (alphaOk && shadowOk)
		return;					// どちらも望みどおり = 何もしない

	KESCMApplyPanelTranslucency();

	// ★遅延の貼り直し(8 回 × 50ms)は**窓のイベントのときだけ**。窓が作り直されて alpha ごと
	//   捨てられるのを追いかけるための仕掛けなので、カーソルが動いただけの回で回すのは純粋な無駄
	//   (しかもカーソルは何度も動くので、その都度 8 回の連鎖を張り直すことになる)。
	if (isWindowEvent)
		KESCMScheduleReapply();
}

static void KESCMInstallWinEventHook()
{
	if (sWinEventHook != nullptr)
		return;		// 既に張ってある

	// ★範囲は SHOW(0x8002)〜LOCATIONCHANGE(0x800B)。当初 PARENTCHANGE(0x800F) だけを張ったが
	//   **一度も発火しなかった**(実測 hk=1/0)。OWL は SetParent ではない方法で窓を組み替えている。
	//   パネルが移動する以上 LOCATIONCHANGE は必ず飛ぶので、そこまで含めて拾う。
	//   ⚠この範囲は他の窓でも大量に飛ぶ。コールバック側で「自分のパネル窓」かつ
	//     「alpha が期待値と違う」ときだけ動くよう二段で絞ってある。
	sWinEventHook = ::SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_LOCATIONCHANGE,
									  nullptr,						// フック DLL 無し(自プロセス内の関数)
									  KESCMWinEventProc,
									  ::GetCurrentProcessId(), 0,	// ★自プロセスの全スレッド限定
									  WINEVENT_OUTOFCONTEXT);		// ★注入なし
}

static void KESCMRemoveWinEventHook()
{
	if (sWinEventHook != nullptr)
	{
		::UnhookWinEvent(sWinEventHook);
		sWinEventHook = nullptr;
	}
}

void KESCMShutdownPanelAlpha()
{
	// プラグイン終了時の保険。ポインタは deref せず、停止と解放だけ(終了処理中でも安全)。
	KESCMRemoveWinEventHook();		// ★フックを残したまま .pln が降りると危険

	if (sReapplyTimer != nil)
	{
		sReapplyTimer->StopTimer();
		sReapplyTimer->Release();
		sReapplyTimer = nil;
	}
	sReapplyLeft = 0;
}

#else	// Mac: 適用そのものが無いので、予約も後始末も要らない

static void KESCMScheduleReapply() {}
void        KESCMShutdownPanelAlpha() {}

#endif // WINDOWS

//========================================================================================
// パネルの開閉・ドッキング切り替えへの追随
//
//   ★半透明は「今のトップレベル窓」に付けるので、その窓が作り直されると失われる。具体的には
//     (a)パネルを閉じて開き直す (b)ドック⇄フローティングを切り替える
//     (c)アイコン状態からフローティングへ引き出す (d)アイコンをクリックしてドロワー展開する。
//     ★★4 つとも下の通知 1 本で拾える(2026-07-29 に Spy で実測)ので、ON のままなら自動で貼り直る。
//
//   ★購読する通知の特定は 2026-07-29 に Debug 版 InDesign の Spy で実測した:
//       kPaletteVisibilityChangedMessage @ kPanelManagerBoss (IID_IPANELMGR)
//     ドッキング切り替えのたびに飛ぶ。本体側の受け手(kLibraryPanelWindowObserverBoss /
//     kBookPaletteWindowObserverBoss)も普通の IID_IOBSERVER で受けている。
//     ★★上記(a)〜(d)のどれでも「widget の作り直し(Observer 再 Attach 46 件) → 本メッセージ」
//       という同じ順序で流れる(2026-07-29 実測)。つまり Update が呼ばれた時点で widget は
//       再構築済みなので、そこで貼り直してよい。
//     ⚠事前に候補と考えた kDockedPaletteAreaChangedMsg は一度も飛ばなかった(=使えない)。
//     ⚠kPanelChangedMessage(widgetid.h) も別物。CPanelControlData が送る「子 widget 構成の
//       変更」通知で、パレットの表示状態とは無関係(パネル開閉でも飛ばないことを実測で確認)。
//     ⚠「パネルが開いた」専用の通知は存在しない(閉じる直前の kAboutToClosePaletteMsg に
//       対応するものは無く、開閉もドッキング切り替えもアイコン復帰もこの1本にまとまっている)。
//
//   ★オブザーバ実体は kActiveContextBoss に AddIn して同居させる(.fr)。レイアウト同期オブザーバ・
//     一括クローズオブザーバと同じ実証済みの構成。
//   ★終了時に明示 detach はしない(アプリも同居先もセッションと同じ寿命。detach 自体が
//     クラッシュ要因になる)。既存2つのオブザーバと同じ方針。
//========================================================================================

//========================================================================================
// カーソルが乗っている間だけ不透明に戻す(IMouseRollOver)
//
//   ★狙い: 半透明は「下が見えて邪魔にならない」ためのものだが、読みたい/操作したいときは
//     不透明の方がよい。カーソルが乗ったら解除し、離れたら戻す。
//   ★仕組み: IMouseRollOver(ui/IMouseRollOver.h)は widget に roll-over 挙動を付けるための
//     公開インターフェイス。MouseEnter / MouseOver / MouseLeave が呼ばれる。
//     .fr でパネル boss(kKESCMPanelWidgetBoss)に IID_IMOUSEROLLOVER として AddIn する。
//   ★★載せ先 = kKESCMPanelWidgetBoss(パネル本体。kPalettePanelWidgetBoss 派生)。AddIn は KESCM.fr:159。
//     **パネル全域で反応することを実機で確認済み**(2026-07-29。記録は KESCM.fr:154 のコメント)。
//     ⚠**ファクトリ登録(KESCMFactoryList.h)を忘れると、何のエラーも出ずに黙って呼ばれない**
//       (CREATE_PMINTERFACE だけでは足りない)。効かなくなったらまずそこを疑う。
//   ★調査の記録: 実機ダンプ(IObjectModel_RomanFS.txt)で IID_IMOUSEROLLOVER を実際に持つ boss を洗うと、
//     本体側で実装を持っているのは次の系統だった(載せ先を変えるときの手掛かり)。
//       kRollOverIconButtonBoss 系(アイコンボタン全般) → kMouseRollOverImpl
//       kPanelWithRolloverWidgetBoss(.fr 型 PanelWithRollOverWidget) → kPanelMouseRollOverImpl
//       kClickableTextWidgetBoss 系(リンク文字) → kHyperlinkRollOverImpl
//       kGIFPlayerWidgetBoss → kGIFMouseRollOverImpl
//     ＝**MouseEnter を呼ぶ側は widget 側の実装**なので、載せ替えるときは受け手があるかを先に確かめる。
//   ⚠判定範囲は**パネル本体の widget まで**。タイトルバーやタブ帯(OWL クロム)の上では
//     反応しない(クロムはマウスイベントを app dispatcher に流さない)。
//
//   ★★2026-07-29 変更: 「乗っているか」の判定はもうここでは持たない。KESCMCursorOverWindow() が
//     呼ばれるたびにカーソル位置を実測する。理由は 2 つ:
//       (a)上の⚠のとおり、タブ帯・タイトル帯では一切呼ばれない。そこはユーザーが実際に触る場所
//          なので、届かないままにはできない(実機のツリーダンプでクロムが widget ツリーの外＝
//          kOWLHostedPanelWrapperBoss で親が尽きることを確定させた)。
//       (b)MouseLeave は取りこぼす経路があり、旗方式だと「乗っている」が張り付いて半透明が
//          二度と効かなくなる壊れ方をしていた。
//     ＝ここは「マウスが動いたから貼り直せ」と伝えるだけの**補助トリガー**に降格した。旗を持たない
//       ので、どちら側のイベントを取りこぼしても状態がずれたままにならない。
//========================================================================================

/** パネルにカーソルが乗り降りしたら半透明を貼り直す(判定は持たない=補助トリガー)。 */
class KESCMPanelRollOver : public CPMUnknown<IMouseRollOver>
{
public:
	KESCMPanelRollOver(IPMUnknown* boss) : CPMUnknown<IMouseRollOver>(boss) {}
	~KESCMPanelRollOver() {}

	virtual void	MouseEnter(const PMPoint& localMousePos);
	virtual void	MouseOver(const PMPoint& localMousePos);
	virtual void	MouseLeave();
	virtual bool8	IsMouseOver() const;
	virtual PMPoint	GetMouseOverPosition() const	{ return fLastPos; }

private:
	PMPoint	fLastPos;
};

CREATE_PMINTERFACE(KESCMPanelRollOver, kKESCMPanelRollOverImpl)

void KESCMPanelRollOver::MouseEnter(const PMPoint& localMousePos)
{
	fLastPos = localMousePos;
	KESCMApplyPanelTranslucency();		// → 実測して不透明へ(OFF のときは中で弾かれる)
}

void KESCMPanelRollOver::MouseOver(const PMPoint& localMousePos)
{
	// ★移動のたびに呼ばれる。位置を控えるだけにして窓へは書きに行かない
	//   (Enter で不透明にし終えているので、毎回 SetLayeredWindowAttributes を叩く意味がない)。
	fLastPos = localMousePos;
}

void KESCMPanelRollOver::MouseLeave()
{
	KESCMApplyPanelTranslucency();		// → 実測して元の半透明へ
}

bool8 KESCMPanelRollOver::IsMouseOver() const
{
	// ★旗を持たないので、その場で実測して答える(このインターフェイスの契約どおり
	//   「今カーソルが乗っているか」を返す)。
#ifdef WINDOWS
	return KESCMCursorOverWindow(KESCMQueryTranslucentTarget(KESCMQueryPaletteWindow())) ? kTrue : kFalse;
#else
	return kFalse;
#endif
}

/** パネルの表示状態が変わったら半透明を貼り直すオブザーバ。購読先は パネルマネージャの subject。 */
class KESCMPanelVisibilityObserver : public CObserver
{
public:
	KESCMPanelVisibilityObserver(IPMUnknown* boss) : CObserver(boss, IID_IKESCMPANELVISIBILITYOBSERVER) {}
	~KESCMPanelVisibilityObserver() {}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);
};

CREATE_PMINTERFACE(KESCMPanelVisibilityObserver, kKESCMPanelVisibilityObserverImpl)

void KESCMPanelVisibilityObserver::Update(const ClassID& theChange, ISubject* /*theSubject*/, const PMIID& protocol, void* /*changedBy*/)
{
	// ★★購読している subject は2つ(2026-07-29 に Debug 版の Spy で実測):
	//   ①kPanelManagerBoss / IID_IPANELMGR の kPaletteVisibilityChangedMessage
	//     ＝パネルの開閉・アイコンからの復帰・ドロワー展開。widget 再構築の直後に飛ぶ。
	//     ★名前のとおり「可視性」が変わったときだけで、置き場所だけが変わる遷移では飛ばない。
	//   ②kAppBoss / IID_IAPPLICATION の kDockedPaletteAreaChangedByUserMsg
	//     ＝ドック内で展開している状態からドラッグでフローティングにしたとき。
	//     ⚠**2025 では飛ぶが 2026 では飛ばない**(実測)。2026 のためにこれを当てにしてはいけない。
	//       残しているのは 2025 で動かしたときのため。2026 では Win32 フックが本命(上のブロック)。
	//     ⚠飛び先は kPanelManagerBoss ではなく kAppBoss。長く「飛ばない」と誤解していたのは
	//       購読先を間違えていたため。
	//   ⚠kAppBoss / IID_IAPPLICATION には kApplicationResumeMsg / kApplicationSuspendMsg も
	//     流れてくるので、theChange で必ず絞ること。
	const bool16 isPaletteMsg = (protocol == IID_IPANELMGR    && theChange == kPaletteVisibilityChangedMessage);
	const bool16 isDockMsg    = (protocol == IID_IAPPLICATION && theChange == kDockedPaletteAreaChangedByUserMsg);
	// ★③アプリが背面へ回った(kApplicationSuspendMsg)。2026-07-29 追加。
	//   ⚠これが無いと: カーソルをタブ帯／タイトル帯に乗せたまま別アプリへマウスを出すと、
	//     自プロセス限定の Win32 フックにはもうカーソルイベントが来ないので、**不透明のまま固まる**
	//     (パネル本体から出た場合だけは IMouseRollOver の MouseLeave が救ってくれるが、クロムの上は
	//      そもそも MouseLeave の対象外)。ここで一度貼り直せば、実測で「乗っていない」と分かって薄く戻る。
	//   ★alpha を1つ書くだけで、モデルにも UI にも触らない ＝ 非アクティブ化の最中に呼んでも安全
	//     ([[app-resume-and-safe-timing]] のガード集は「重い自動処理」向けで、ここには当たらない)。
	const bool16 isSuspendMsg = (protocol == IID_IAPPLICATION && theChange == kApplicationSuspendMsg);
	if (!isPaletteMsg && !isDockMsg && !isSuspendMsg)
		return;

	// ★OFF のときは何もしない。この通知は「文書を1つ開く」だけでも複数回飛ぶ(実測)ので、
	//   使っていない人にまで窓探索を走らせない(貼り直しが要るのは ON のときだけ)。
	if (!KESCMGetPanelTranslucent())
		return;

	KESCMApplyPanelTranslucency();

	// ★ここで書いた alpha は、直後に InDesign が窓を作り直すと捨てられる(実測)。
	//   イベントを一巡させてから「そのときの窓」へ貼り直す。
	//   ★ただし背面へ回っただけ(Suspend)のときは窓に変化が無いので、追いかけは要らない。
	if (!isSuspendMsg)
		KESCMScheduleReapply();
}

void KESCMAttachPanelVisibilityObserver()
{
	ISession* session = GetExecutionContextSession();
	IActiveContext* ctx = (session != nil) ? session->GetActiveContext() : nil;
	if (ctx == nil)
		return;

	InterfacePtr<IObserver> obs((IObserver*)ctx->QueryInterface(IID_IKESCMPANELVISIBILITYOBSERVER));
	if (obs == nil)
		return;

	// ★パネルマネージャは本体の起動シーケンスの途中で立ち上がる(kPanelMgrHasStartedMsg が存在する)。
	//   起動サービスから呼ぶとここが nil になる可能性がある。
	InterfacePtr<IApplication> app(session->QueryApplication());
	InterfacePtr<IPanelMgr> panelMgr(app != nil ? app->QueryPanelManager() : nil);
	if (panelMgr == nil)
		return;

	InterfacePtr<ISubject> subject(panelMgr, IID_ISUBJECT);
	if (subject == nil)
		return;

	if (!subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IPANELMGR, IID_IKESCMPANELVISIBILITYOBSERVER))
		subject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IPANELMGR, IID_IKESCMPANELVISIBILITYOBSERVER);

	// ★2つめの購読先 = kAppBoss / IID_IAPPLICATION。
	//   「ドック内で展開 → ドラッグでフローティング」は PanelMgr には出ず、2025 ではここに
	//   kDockedPaletteAreaChangedByUserMsg として飛ぶ。⚠**2026 では飛ばない**ので、
	//   2026 での本命は Win32 フックのほう。これは 2025 で動かしたときの保険。
	InterfacePtr<ISubject> appSubject(app, IID_ISUBJECT);
	if (appSubject != nil &&
		!appSubject->IsAttached(ISubject::kRegularAttachment, obs, IID_IAPPLICATION, IID_IKESCMPANELVISIBILITYOBSERVER))
	{
		appSubject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IAPPLICATION, IID_IKESCMPANELVISIBILITYOBSERVER);
	}
}
