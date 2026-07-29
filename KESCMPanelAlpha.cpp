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

// ★カーソルがパネル本体の上にあるか(IMouseRollOver が上下させる)。
//   半透明で見づらいのは「読みたい・操作したい」ときなので、乗っている間だけ不透明に戻す。
//   ⚠これは永続化しない(そのときのマウス位置で決まる一時的な状態)。
static bool16 sPanelHover = kFalse;

// ★実効 alpha ＝ トグルが ON で、かつカーソルが乗っていないときだけ薄くする。
//   ここ1箇所に集約しておくこと(適用側・フックの判定側の両方から使う)。
static uint8 KESCMEffectiveAlpha()
{
	return (sPanelTranslucent && !sPanelHover) ? kKESCMPanelAlphaValue : 255;
}

#ifdef WINDOWS
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

	// ★カーソルが乗っている間は不透明に戻す(KESCMEffectiveAlpha が判断)。
	const BYTE alpha = KESCMEffectiveAlpha();

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
	//   ★影を隠すのは「実際に薄くしているとき」だけ。カーソルが乗って不透明に戻している間は
	//     影も戻す(でないと不透明なのに影だけ消えた不自然な見た目になる)。
	HWND shadow = ::GetWindow(target, GW_OWNER);
	if (KESCMClassIs(shadow, L"OWL.ShadowView"))
		::ShowWindow(shadow, (alpha < 255) ? SW_HIDE : SW_SHOWNA);

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

static ICallbackTimer* sReapplyTimer   = nil;
static bool16          sReapplyPending = kFalse;	// 予約中は重ねない(この通知は連続で飛ぶ)
static int32           sReapplyLeft    = 0;			// 残り回数(0 で打ち切り＝暴走止め)

static uint32 KESCMReapplyTimerProc(void* refPtr);

// 予約を1回ぶんだけ武装する(残り回数がある間のみ)。
static void KESCMArmReapplyTimer()
{
	if (sReapplyPending || sReapplyLeft <= 0)
		return;

	if (sReapplyTimer == nil)
		sReapplyTimer = (ICallbackTimer*)::CreateObject(kCallbackTimerBoss, IID_ICALLBACKTIMER);
	if (sReapplyTimer == nil)
		return;

	sReapplyPending = kTrue;
	sReapplyTimer->StartTimer(KESCMReapplyTimerProc, kKESCMPanelAlphaReapplyDelayMillis, nil);
}

static uint32 KESCMReapplyTimerProc(void* /*refPtr*/)
{
	--sReapplyLeft;

	// ★ここで Release しない(RunTask の実行中に自分を解放すると自己破棄になる)。
	//   解放は KESCMShutdownPanelAlpha() の1箇所に集約する。
	if (KESCMGetPanelTranslucent())
	{
		KESCMApplyPanelTranslucency();

		// ★★連鎖は「戻り値」で行う(2026-07-29 修正)。以前はここから StartTimer を呼び直して
		//   いたが、その直後に kEndOfTime を返していたため予約が打ち消され、8 回のはずが
		//   2 回しか走っていなかった(実測 rp=2)。戻り値が再スケジュール値そのものなので、
		//   続けたいときは待ち時間を返せばよい。
		if (sReapplyLeft > 0)
			return kKESCMPanelAlphaReapplyDelayMillis;
	}

	sReapplyPending = kFalse;		// 連鎖終了。次の通知から改めて武装できる

	// ★★戻り値は IIdleTask::RunTask の再スケジュール値。**0 は「すぐまた呼べ」**であって終了では
	//   ない(KESCMTracker.cpp で 0 を返して InDesign を固めた前科がある)。終わるなら kEndOfTime。
	return IIdleTask::kEndOfTime;
}

// 通知を受けた側から呼ぶ。窓が落ち着くまでの貼り直しを(再)開始する。
static void KESCMScheduleReapply()
{
	sReapplyLeft = kKESCMPanelAlphaReapplyTries;	// 通知のたびに回数を戻す
	KESCMArmReapplyTimer();
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
	// 窓そのもののイベントだけ見る(子要素・非窓オブジェクトは無視)。
	if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF)
		return;
	if (!KESCMGetPanelTranslucent() || sPaletteWnd == nullptr)
		return;

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
	const BYTE want = KESCMEffectiveAlpha();
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
		const bool16 visible     = ::IsWindowVisible(shadow) ? kTrue : kFalse;
		const bool16 wantVisible = (want < 255) ? kFalse : kTrue;
		shadowOk = (visible == wantVisible);
	}

	if (alphaOk && shadowOk)
		return;					// どちらも望みどおり = 何もしない

	// 窓が仕上がりきっていないことがあるので、その場で一度当て、遅延でも当て直す。
	KESCMApplyPanelTranslucency();
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
	sReapplyPending = kFalse;
	sReapplyLeft    = 0;
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
//   ⚠⚠**現状これは呼ばれない**(2026-07-29 実機で確認)。原因も実機ダンプで判明済み:
//     本体は「ロールオーバーするパネル」用に **kPanelWithRolloverWidgetBoss** という専用 boss を
//     用意していて(.fr 型 = `PanelWithRollOverWidget`、Widgets.fh:781)、その boss が独自に持つのは
//     IID_IMOUSEROLLOVER(=kPanelMouseRollOverImpl)**だけ**で、他は全部 kGenericPanelWidgetBoss /
//     kBaseWidgetBoss からの継承だった(IObjectModel_RomanFS.txt 6173-6193 行)。
//     ＝**MouseEnter を呼ぶ側は GenericPanelWidget 系の標準実装**であり、
//       このパネル(kPalettePanelWidgetBoss 派生)はその系統ではないので呼ばれない。
//     ★次の一手: パネルの中に `PanelWithRollOverWidget` を 1 枚置き、その boss を自前派生にして
//       IID_IMOUSEROLLOVER を下の実装で上書きする(.fr のパネル構造に手を入れる必要あり・未実験)。
//   ⚠判定範囲は**パネル本体の widget まで**。タイトルバーやタブ帯(OWL クロム)の上では
//     反応しない(クロムはマウスイベントを app dispatcher に流さない)。
//========================================================================================

/** パネルにカーソルが乗っている間だけ「Translucent Panel」を一時解除する。 */
class KESCMPanelRollOver : public CPMUnknown<IMouseRollOver>
{
public:
	KESCMPanelRollOver(IPMUnknown* boss) : CPMUnknown<IMouseRollOver>(boss) {}
	~KESCMPanelRollOver() {}

	virtual void	MouseEnter(const PMPoint& localMousePos);
	virtual void	MouseOver(const PMPoint& localMousePos);
	virtual void	MouseLeave();
	virtual bool8	IsMouseOver() const				{ return sPanelHover != kFalse; }
	virtual PMPoint	GetMouseOverPosition() const	{ return fLastPos; }

private:
	PMPoint	fLastPos;
};

CREATE_PMINTERFACE(KESCMPanelRollOver, kKESCMPanelRollOverImpl)

void KESCMPanelRollOver::MouseEnter(const PMPoint& localMousePos)
{
	fLastPos = localMousePos;
	if (sPanelHover)
		return;					// 既に乗っている扱い(取りこぼし対策の冪等ガード)

	sPanelHover = kTrue;
	KESCMApplyPanelTranslucency();		// → 不透明へ(OFF のときは中で弾かれる)
}

void KESCMPanelRollOver::MouseOver(const PMPoint& localMousePos)
{
	// ★移動のたびに呼ばれる。位置を控えるだけにして窓へは書きに行かない
	//   (Enter で不透明にし終えているので、毎回 SetLayeredWindowAttributes を叩く意味がない)。
	fLastPos = localMousePos;
}

void KESCMPanelRollOver::MouseLeave()
{
	if (!sPanelHover)
		return;

	sPanelHover = kFalse;
	KESCMApplyPanelTranslucency();		// → 元の半透明へ
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
	if (!isPaletteMsg && !isDockMsg)
		return;

	// ★OFF のときは何もしない。この通知は「文書を1つ開く」だけでも複数回飛ぶ(実測)ので、
	//   使っていない人にまで窓探索を走らせない(貼り直しが要るのは ON のときだけ)。
	if (!KESCMGetPanelTranslucent())
		return;

	KESCMApplyPanelTranslucency();

	// ★ここで書いた alpha は、直後に InDesign が窓を作り直すと捨てられる(実測)。
	//   イベントを一巡させてから「そのときの窓」へ貼り直す。
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
