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
#include "KESCMID.h"			// kKESCMDisplayName(=窓 title で引くパネル名)/独自 IID・ImplID

// パネルの表示状態変化を購読するオブザーバ用:
#include "CObserver.h"
#include "ISubject.h"			// AttachObserver / IsAttached
#include "ISession.h"			// GetExecutionContextSession(終了処理中は nil になり得る)
#include "IApplication.h"		// QueryPanelManager
#include "IActiveContext.h"		// オブザーバ実体の同居先(kActiveContextBoss)
#include "IPanelMgr.h"			// IID_IPANELMGR(購読する subject)
#include "AppUIID.h"			// ★kPaletteVisibilityChangedMessage(公開ヘッダー。AppUIID.h:325)

// ★windows.h は SDK ヘッダーより後に置くこと(マクロが SDK 側の名前とぶつからないように)。
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

bool16 KESCMApplyPanelTranslucency()
{
#ifdef WINDOWS
	HWND palette = KESCMQueryPaletteWindow();
	HWND dock    = KESCMQueryFloatingDock(palette);
	if (dock == nullptr)
		return kFalse;		// パネルが無い / ドッキング中 → 何もしない

	const BYTE alpha = sPanelTranslucent ? kKESCMPanelAlphaValue : 255;

	// ★WS_EX_LAYERED は InDesign が最初から立てているので触らない。
	return ::SetLayeredWindowAttributes(dock, 0, alpha, LWA_ALPHA) ? kTrue : kFalse;
#else
	return kFalse;		// Mac: 半透明化の手段が無いので常に「適用しなかった」
#endif
}

//========================================================================================
// パネルの開閉・ドッキング切り替えへの追随
//
//   ★半透明は「今のトップレベル窓(OWL.Dock)」に付けるので、その窓が作り直されると失われる。
//     具体的には (a)パネルを閉じて開き直す (b)ドッキング⇄フローティングを切り替える の2つ。
//     どちらも下の通知で拾えるので、ON のままなら自動で貼り直る。
//
//   ★購読する通知の特定は 2026-07-29 に Debug 版 InDesign の Spy で実測した:
//       kPaletteVisibilityChangedMessage @ kPanelManagerBoss (IID_IPANELMGR)
//     ドッキング切り替えのたびに飛ぶ。本体側の受け手(kLibraryPanelWindowObserverBoss /
//     kBookPaletteWindowObserverBoss)も普通の IID_IOBSERVER で受けている。
//     ⚠事前に候補と考えた kDockedPaletteAreaChangedMsg は一度も飛ばなかった(=使えない)。
//     ⚠「パネルが開いた」専用の通知は存在しない(閉じる直前の kAboutToClosePaletteMsg に
//       対応するものは無く、開閉もドッキング切り替えもこの1本にまとまっている)。
//
//   ★オブザーバ実体は kActiveContextBoss に AddIn して同居させる(.fr)。レイアウト同期オブザーバ・
//     一括クローズオブザーバと同じ実証済みの構成。
//   ★終了時に明示 detach はしない(アプリも同居先もセッションと同じ寿命。detach 自体が
//     クラッシュ要因になる)。既存2つのオブザーバと同じ方針。
//========================================================================================

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
	if (protocol != IID_IPANELMGR || theChange != kPaletteVisibilityChangedMessage)
		return;

	// ★OFF のときは何もしない。この通知は「文書を1つ開く」だけでも複数回飛ぶ(実測)ので、
	//   使っていない人にまで窓探索を走らせない(貼り直しが要るのは ON のときだけ)。
	if (!KESCMGetPanelTranslucent())
		return;

	KESCMApplyPanelTranslucency();
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

	InterfacePtr<IApplication> app(session->QueryApplication());
	InterfacePtr<IPanelMgr> panelMgr(app != nil ? app->QueryPanelManager() : nil);
	InterfacePtr<ISubject> subject(panelMgr, IID_ISUBJECT);
	if (subject == nil)
		return;

	if (!subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IPANELMGR, IID_IKESCMPANELVISIBILITYOBSERVER))
		subject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IPANELMGR, IID_IKESCMPANELVISIBILITYOBSERVER);
}
