//========================================================================================
//
//  KESCMPanelAlpha.cpp
//
//  パネル半透明トグルの実装。★Win32 依存はこのファイルに閉じ込める。
//
//  ★手順(2026-07-29 の実測に基づく。変更するときは必ず docs/ai-notes/win32-window-transparency.md を読むこと):
//    1. cls=="OWL.Palette" かつ title==パネル表示名 の窓を自プロセス内から探す
//    2. GetAncestor(GA_ROOT) で「今の」トップレベル窓を得る
//    3. それが "indesign"(メインフレーム)ならドッキング中 → 何もしない
//    4. "OWL.Dock" なら SetLayeredWindowAttributes で alpha を設定
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
#include "KESCMID.h"			// kKESCMDisplayName(=窓 title で引くパネル名)

#ifdef WINDOWS
#include <windows.h>
#endif

// トグル状態(セッション内で保持。永続化は KESCMPanelState.cpp が担当)。★既定 OFF
static bool16 sPanelTranslucent = kFalse;

bool16 KESCMGetPanelTranslucent()
{
	return sPanelTranslucent;
}

void KESCMSetPanelTranslucent(bool16 on)
{
	sPanelTranslucent = on;
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

// パネルが載っている「今の」トップレベル窓のうち、単独で透かせるものだけを返す。
// ドッキング中(GA_ROOT がメインフレーム)なら nullptr。
static HWND KESCMQueryFloatingDock(HWND palette)
{
	if (palette == nullptr)
		return nullptr;

	HWND root = ::GetAncestor(palette, GA_ROOT);
	if (root == nullptr || root == palette)
		return nullptr;

	wchar_t cls[64] = { 0 };
	if (::GetClassNameW(root, cls, 64) == 0)
		return nullptr;

	// "indesign" = メインフレーム = ドッキング中。単独制御はできない。
	return (::wcscmp(cls, L"OWL.Dock") == 0) ? root : nullptr;
}

#endif // WINDOWS

void KESCMApplyPanelTranslucency()
{
#ifdef WINDOWS
	HWND palette = KESCMFindPaletteWindow(kKESCMPaletteWindowTitle);
	HWND dock    = KESCMQueryFloatingDock(palette);
	if (dock == nullptr)
		return;		// パネルが無い / ドッキング中 → 何もしない

	const BYTE alpha = sPanelTranslucent ? kKESCMPanelAlphaValue : 255;

	// ★WS_EX_LAYERED は InDesign が最初から立てているので触らない。
	::SetLayeredWindowAttributes(dock, 0, alpha, LWA_ALPHA);
#endif
}
