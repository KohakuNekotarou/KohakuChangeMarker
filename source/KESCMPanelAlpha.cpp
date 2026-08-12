//========================================================================================
//
//  KESCMPanelAlpha.cpp
//
//  パネル半透明トグルの実装。★Win32 依存はこのファイルに閉じ込める。
//
//  ★手順(2026-07-29 の実測に基づく。変更するときは必ず docs/ai-notes/win32-window-transparency.md を読むこと):
//    1. パネルの WidgetID から OWL.Palette の窓を得る ——
//       IPanelMgr::GetPanelFromWidgetID → GetPaletteRefContainingPanel → PaletteRef::GetOWLControl
//       ★2026-08-06 にここを入れ替えた。旧実装は「cls=="OWL.Palette" かつ title==パネル表示名」で
//         EnumWindows していたが、**窓タイトルは UI 言語で変わる**(本体のページパネルは
//         英語UI="Pages" / 日本語UI="ページ")ので、本体パネルには通用しなかった。WidgetID は数値。
//    2. GetAncestor(GA_ROOT) で「今の」トップレベル窓を得る
//    3. それが "indesign"(メインフレーム)ならドック内で展開中 → 何もしない
//    4. "OWL.Dock"(フローティング) / "OWL.FrameDrawer"(アイコンをクリックしたドロワー展開) なら
//       SetLayeredWindowAttributes で alpha を設定
//
//  ★OWL.Dock の HWND は「ドッキング → フローティングに戻す」で変わる(古い窓は破棄され、新しく
//    作られる)。⚠パネルを**閉じて開き直す**のは別で、**同じ Dock が alpha ごと生き残る**
//    (2026-08-04 に KBS 側で Release 21.0.2.2 を一段ずつ外部から測って確定。ここに長く書いてあった
//     「閉じて開き直すと変わる」は誤りだった)。OWL.Palette の HWND はどちらでも不変。
//    だから Dock の HWND は保持せず毎回探し直す。
//  ★Dock は 1 つのパネルに属する(起動時から 55〜56 組が非可視で待機し、それぞれ自分のパネル名を
//    名乗っている)ので、あるパネルの alpha が別のパネルへ渡ることはない。
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

// ★★★パネルの窓(HWND)を SDK 側から得るための一式(2026-08-06 に確立)。
//   PaletteRef は内部に HWND を持っている: PaletteRef.h:47 で OWLControlRef は HWND そのもの、
//   :188 の GetOWLControl() がそれを返す。IPanelMgr::GetPanelFromWidgetID(数値の WidgetID)から
//   GetPaletteRefContainingPanel() で PaletteRef を得れば、そこに窓がある。
//   ⇒ **窓タイトル照合(翻訳される)も EnumWindows(全窓走査)も要らない。**
//   ⚠ここは 2026-08-06 のブロック13監査で「パネルの HWND を SDK から得る道は無い(4ルート全滅)」と
//     結論した箇所の訂正。5つ目のルートがこれで、実機で全パネル到達を確認済み。
#include "IControlView.h"		// GetPanelFromWidgetID が返すパネル
#include "PaletteRef.h"			// PaletteRef::GetOWLControl(=HWND) / GetPaletteRefType
#include "PaletteRefUtils.h"	// GetParentOfPalette(階層を上がる)
#include "PagesPanelID.h"		// kPagesPanelWidgetID(:391) - 本体ページパネルの狙い撃ち先

// ★windows.h は SDK ヘッダーより後に置くこと(マクロが SDK 側の名前とぶつからないように)。
#ifdef WINDOWS
#include <windows.h>
#endif

//----------------------------------------------------------------------------------------
// 半透明にできる対象パネル(2026-08-06 に2つへ拡張)。
//
//  ★1つ目 = 自分のパネル。2つ目 = **本体のページパネル**(ユーザー要望)。
//  ★対象を WidgetID(数値)で持てるようになったのが 08-06 の肝 —— 窓タイトルは UI 言語で変わるので
//    本体のパネルには使えなかった(英語UI="Pages" / 日本語UI="ページ")。
//  ★増やすときは enum に1つ足し、kKESCMAlphaWidgetIDs に WidgetID を1つ足すだけでよい。
//    ただしトグル(メニュー項目・永続化キー)は対象ごとに要る。
//  (2026-08-07 に3つ目＝**ツールボックス**を足したが、同日ユーザー判断で撤去した。1つ足すだけで
//   動いた＝この作りが対象を選ばないことの実証にはなっている。実測した窓構造と狙い撃ち先の記録は
//   memory/translucent-toolbox-idea.md に残してあるので、再挑戦するならそこから。)
//----------------------------------------------------------------------------------------
enum
{
	kKESCMAlphaSelf  = 0,		// 自分のパネル(Kohaku Change Marker)
	kKESCMAlphaPages = 1,		// 本体のページパネル
	kKESCMAlphaCount = 2
};

// トグル状態(セッション内で保持。永続化は KESCMPanelState.cpp が担当)。★既定 OFF
// ※Mac でも状態だけは持つ(適用側が何もしないだけ)。
static bool16 sTranslucentOn[kKESCMAlphaCount] = { kFalse, kFalse };

// どれか1つでも ON か。★Win32 フックを張る/外す判断と、遅延再適用を続ける/やめる判断で共有する
//   (同じことを2か所で数えると必ずずれる)。
static bool16 KESCMAnyTranslucentOn()
{
	for (int32 i = 0; i < kKESCMAlphaCount; ++i)
	{
		if (sTranslucentOn[i])
			return kTrue;
	}
	return kFalse;
}

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
//
// ★★★2026-08-07(夜): **KBS と同じ判定に戻した**(ユーザー指示「KBS のようにしてほしい」)。
//   ＝「カーソルがパネルの**矩形の中**にあり、その下にあるのが InDesign 自身の窓なら乗っている」。
//   メニューが出ていても、カーソルがパネルの上にある限り不透明のまま。
//   ★この関数は対象共通(呼び出し側がその対象の窓を target に渡すだけ)なので、**自分のパネルと
//     本体のページパネルの両方**に同じ挙動が効く。
//
//   経緯 —— ここは5回変わっている:
//     2026-07-29  メニューが重なると薄くなるのを「仕様」として許容
//     2026-08-05  KBS でユーザー報告(「行を右クリックすると不透明のパネルがまた薄くなる」)→
//                 「カーソル下が自プロセスの窓なら乗っている」を追加して修正
//     2026-08-06  KESCM へ移植。さらに「矩形の外へはみ出したメニュー」も拾うため、
//                 自プロセス窓 ∧ 容れ物クラスでない ∧ **パネル矩形と交差**、という補助判定を追加
//     2026-08-07  揺れるので全部撤去(＝メニューが出ている間は薄いまま に統一)
//     2026-08-07  **自プロセス判定だけを戻す**(このコード)。撤去すべきだったのは交差判定の方だった。
//
//   ★★揺れの真犯人は 08-06 に足した**交差判定**であって、自プロセス判定ではなかった。ユーザー報告
//   「パネルメニューを出す→枠の透明度を選ぶ→子供のメニューが出てそれを選んでいると、
//     不透明が半透明になる」はこう起きていた:
//     フライアウト本体はパネルに重なる → 交差する → 不透明
//     その子メニューは右へ開く       → 交差しない → 半透明
//   ＝**メニュー窓の位置**で判定していたから、開く向きと長さ(画面位置で変わる)で必ず揺れた。
//
//   ★いま採る判定はそれとは別物で、見るのは**カーソルの位置**だけ:
//     ①パネルの矩形の外 → 乗っていない(メニューがどこに出ていようと無関係)
//     ②矩形の中で、カーソル下が自分(InDesign)の窓 → 乗っている
//   矩形は動かないので、②の中でメニューの親子をどう行き来しても答えは変わらない ＝ 揺れない。
//
//   ⚠この判断で受け入れたもの:
//     ・メニューがパネル矩形の**外**まで伸び、その外側の項目にカーソルを置いている間は薄くなる。
//       判定の基準が「パネルの矩形」1つなので、薄くなる理由と見た目が一致する。
//     ・InDesign の**別の窓**がパネルに重なっている場合、その上にカーソルがあっても「パネルに乗って
//       いる」と数える。隠れているので画面上おかしく見えるものは無く、カーソルが矩形の外へ出れば
//       次の一手で直る。KBS が 2026-08-05 から受け入れているのと同じ割り切り。
static bool KESCMClassIs(HWND h, const wchar_t* wanted);	// 実体は下(窓クラス名の完全一致)

static bool KESCMCursorOverWindow(HWND target)
{
	if (target == nullptr)
		return false;

	POINT pt;
	if (!::GetCursorPos(&pt))
		return false;

	RECT rc;
	if (!::GetWindowRect(target, &rc))
		return false;
	if (!::PtInRect(&rc, pt))
		return false;		// 矩形の外 = 確実に乗っていない(マウス移動のほとんどはここで終わる)

	HWND under = ::WindowFromPoint(pt);
	if (under == nullptr)
		return false;

	const HWND root = ::GetAncestor(under, GA_ROOT);
	if (root == target)
		return true;		// パネル自身 = 普通の答え

	// ★自分(InDesign)の窓でもあるか。ならばそれはパネルが自分の上に出したもの(フライアウト・その子
	//   メニュー・右クリックメニュー・ツールチップ)で、カーソルはパネルから離れていない。
	//   ★**トップレベル窓**に対して聞くこと: メニューはパネルの子ではなく、アプリが所有する独立した
	//     トップレベル窓として作られる。
	DWORD pid = 0;
	::GetWindowThreadProcessId(root, &pid);
	return (pid == ::GetCurrentProcessId());
}

// ★実効 alpha ＝ その対象のトグルが ON で、かつカーソルが乗っていないときだけ薄くする。
//   ここ1箇所に集約しておくこと(適用側・フックの判定側の両方から使う)。
//   ★OFF ならカーソル位置すら見ない(この機能は ON のときだけ動く=ユーザー方針 2026-07-29)。
//   ※Mac には適用そのものが無いので置かない(未使用関数の警告を出さないため)。
static uint8 KESCMEffectiveAlpha(int32 which, HWND target)
{
	if (!sTranslucentOn[which])
		return 255;

	return KESCMCursorOverWindow(target) ? 255 : kKESCMPanelAlphaValue;
}

// ★Win32 イベントフックの出し入れ(実体は下の WINDOWS ブロック)。ON の間だけ張る。
static void KESCMInstallWinEventHook();
static void KESCMRemoveWinEventHook();
#endif

// 対象を指定して状態を書く。下の公開 API はこれを呼ぶだけの薄いラッパー
// (判断はここ1箇所に集約する ＝ トグルが2つに増えても分岐が2か所に散らない)。
static void KESCMSetTranslucentFor(int32 which, bool16 on)
{
	sTranslucentOn[which] = on;

#ifdef WINDOWS
	// ★「ドック内展開 ⇄ フローティング」と「ドロワー展開 → フローティング」は SDK 通知が
	//   1本も飛ばない(2026 で Debug/Release 両方から確認)。唯一の手掛かりが Win32 の
	//   親変更イベントなので、フックを張る。★対象が2つに増えたので、**どれか1つでも ON なら張り、
	//   全部 OFF になったときだけ外す**(片方を OFF にしただけで、もう片方の追随が止まらないように)。
	if (KESCMAnyTranslucentOn())
		KESCMInstallWinEventHook();
	else
		KESCMRemoveWinEventHook();
#endif
}

bool16 KESCMGetPanelTranslucent()
{
	return sTranslucentOn[kKESCMAlphaSelf];
}

void KESCMSetPanelTranslucent(bool16 on)
{
	KESCMSetTranslucentFor(kKESCMAlphaSelf, on);
}

bool16 KESCMGetPagesPanelTranslucent()
{
	return sTranslucentOn[kKESCMAlphaPages];
}

void KESCMSetPagesPanelTranslucent(bool16 on)
{
	KESCMSetTranslucentFor(kKESCMAlphaPages, on);
}

#ifdef WINDOWS

// ★★★パネルの OWL.Palette 窓を SDK 側から得る(2026-08-06 に確立。旧実装 = 窓タイトルで EnumWindows)。
//
//  なぜ変えたか: 旧実装は「クラス名が OWL.Palette かつ 窓タイトル == パネル表示名」で全窓を走査して
//  いた。自分のパネルは表示名が全ロケール英語で固定なので成立していたが、**本体のパネルには通用
//  しない** —— 同じ機械の同じパネルで、英語UI="Pages" / 日本語UI="ページ" と変わることを実測した
//  (2026-08-06。Debug ビルド=英語UI と Release=日本語UI で見比べた)。
//
//  正しい道 = PaletteRef が内部に HWND を持っている:
//    IPanelMgr::GetPanelFromWidgetID(WidgetID)      … WidgetID は数値なので言語に依存しない
//      -> IPanelMgr::GetPaletteRefContainingPanel() … その panel を載せている PaletteRef
//         -> PaletteRef::GetOWLControl()            … PaletteRef.h:47(OWLControlRef=HWND), :188
//
//  ★★「何が返るか」は実測ではなく**契約**(2026-08-07 のブロック13再監査で裏取り):
//    IPanelMgr.h:197-201 が GetPaletteRefContainingPanel について
//    "For regular tabbed palettes, this should return an object of type kTabPanelContainerType"
//    と明記している。＝下の階層の type=8 が返るのはヘッダーの約束であって、環境で変わる観測ではない。
//    (だから下の KESCMQueryPanelPaletteFromSDK は戻り値のクラス名を検証しない。キャッシュ側と
//     フック側がクラス名を見るのは別の理由 ＝ HWND を OS が使い回すことへの対策。)
//
//  実測した階層(2026-08-06。ページパネルと自パネルの両方で同一。PaletteRef.h:87-123 の記述どおり):
//    type=8 kTabPanelContainerType = OWL.Palette   ← ここが返る(上記の契約)
//    type=7 kTabGroupType          = OWL.TabGroup
//    type=6 kTabPaneType           = OWL.TabPane
//    type=3 kDockType              = OWL.Dock      ← alpha を書く窓
//  ★**トップレベル窓への変換は従来どおり下の KESCMQueryTranslucentTarget(GetAncestor)に任せる。**
//    ドロワー展開(OWL.FrameDrawer)とドック内展開(indesign)の面倒を見ているのはあちらで、そこは
//    実績があるので触らない。ここで置き換えたのは「OWL.Palette をどう見つけるか」だけ。
//
//  ⚠2026-08-06 のブロック13監査は「パネルの HWND を SDK から得る道は無い(4ルート全滅)」と結論して
//    いたが、それは誤りだった。これが5つ目のルート。
static HWND KESCMQueryPanelPaletteFromSDK(const WidgetID& panelWidgetID)
{
	// ★終了処理中は session が nil になり得る。
	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return nullptr;

	InterfacePtr<IApplication> app(session->QueryApplication());
	if (app == nil)
		return nullptr;

	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr == nil)
		return nullptr;

	// ★GetPanelFromWidgetID は AddRef しない(IPanelMgr.h:112。「caller must release」と明記されて
	//   いるのは CreatePanel だけ)ので Release もしない。
	IControlView* panel = panelMgr->GetPanelFromWidgetID(panelWidgetID);
	if (panel == nil)
		return nullptr;		// そのパネルはまだ一度も作られていない

	const PaletteRef container = panelMgr->GetPaletteRefContainingPanel(panel);
	if (!container.IsValid())
		return nullptr;

	HWND h = container.GetOWLControl();
	return (h != nullptr && ::IsWindow(h)) ? h : nullptr;
}

// ★見つけた OWL.Palette を覚えておく。パネルを閉じて開き直しても、ドッキングとフローティングを
//   切り替えても OWL.Palette の HWND は変わらない(変わるのは親の OWL.Dock で、しかも
//   「ドッキング → フローティング」のときだけ = 上のファイル冒頭を参照)ので、キャッシュが効く。
//   ここを入れる理由: 購読している kPaletteVisibilityChangedMessage は「文書を1つ開く」だけでも
//   複数回飛ぶ(2026-07-29 実測)。そのたびに SDK へ問い合わせるのは無駄で、しかも下の Win32 フックは
//   マウス移動のたびに走る ＝ **Win32 コールバックからモデルへ触る回数は最小に保ちたい**
//   (KBS が同じ理由で負のキャッシュまで置いている: KBSPanelAlpha.cpp:492-502)。
static HWND sPaletteWnd[kKESCMAlphaCount] = { nullptr, nullptr };

// 対象ごとの狙い撃ち先。★enum の並びと必ず同じ順にすること。
//  ※WidgetID ではなく生の uint32 で持つ: DECLARE_PMID が作るのは uint32 で、静的初期化に使える。
static const uint32 kKESCMAlphaWidgetIDs[kKESCMAlphaCount] =
{
	kKESCMPanelWidgetID,		// kKESCMAlphaSelf  = 自分のパネル
	kPagesPanelWidgetID			// kKESCMAlphaPages = 本体のページパネル
};

// キャッシュ優先でパネル窓を得る。★ハンドルは OS が使い回すので、生存確認だけでなくクラス名の
//   一致も見てから使う(SDK へ問い合わせ直すより安い)。
//   ⚠旧実装はここで窓タイトルも照合していた。それは「本当に自分のパネルか」を見るためだったが、
//     引き直しが WidgetID 狙い撃ちになった今、綴りを合わせる相手がいない。窓の作り直しと HWND の
//     使い回しは kPaletteVisibilityChangedMessage か Win32 フックのどちらかを必ず伴い、
//     そこでキャッシュを捨てているので、ここはクラス名までで足りる。
static HWND KESCMQueryPaletteWindow(int32 which)
{
	HWND cached = sPaletteWnd[which];
	if (cached != nullptr && ::IsWindow(cached) && KESCMClassIs(cached, L"OWL.Palette"))
		return cached;

	sPaletteWnd[which] = KESCMQueryPanelPaletteFromSDK(kKESCMAlphaWidgetIDs[which]);
	return sPaletteWnd[which];
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

// 対象を1つ指定して適用する。★alpha を書く実体はここだけ(公開 API は下の薄いラッパー)。
static bool16 KESCMApplyFor(int32 which)
{
#ifdef WINDOWS
	HWND palette = KESCMQueryPaletteWindow(which);
	HWND target  = KESCMQueryTranslucentTarget(palette);
	if (target == nullptr)
		return kFalse;		// パネルが無い / ドック内で展開中 → 何もしない

	// ★カーソルが乗っている間は不透明に戻す(KESCMEffectiveAlpha が今の位置を実測して判断)。
	//   タブ帯・タイトル帯の上でも「乗っている」になる ＝ 対象窓がそれらを含むため。
	const BYTE alpha = KESCMEffectiveAlpha(which, target);

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
	const bool16 hideShadow = sTranslucentOn[which];
	HWND shadow = ::GetWindow(target, GW_OWNER);
	if (KESCMClassIs(shadow, L"OWL.ShadowView"))
		::ShowWindow(shadow, hideShadow ? SW_HIDE : SW_SHOWNA);

	return ok ? kTrue : kFalse;
#else
	(void)which;
	return kFalse;		// Mac: 半透明化の手段が無いので常に「適用しなかった」
#endif
}

bool16 KESCMApplyPanelTranslucency()
{
	return KESCMApplyFor(kKESCMAlphaSelf);
}

bool16 KESCMApplyPagesPanelTranslucency()
{
	return KESCMApplyFor(kKESCMAlphaPages);
}

// 全対象へ貼り直す。★通知・遅延再適用・Win32 フックからはこちらを呼ぶ
//   (対象が増えても、追随させる側のコードを直さなくて済むように)。
//
// ★★OFF の対象は飛ばす(2026-08-07 修正)。KESCMApplyFor は OFF でも窓を探し、alpha 255 を書き、
//   影を SW_SHOWNA する —— 対象が1つだった頃は呼び出し側が OFF を弾いていたので表に出なかったが、
//   ここが「どちらか一方でも ON なら全部」になったことで OFF 側にも毎回届くようになっていた。
//   ⚠**実害は「両方を同じフローティンググループに入れたとき」に出る**: そのとき2つの対象は同じ
//     OWL.Dock を GA_ROOT に持つので、ON 側が 77 を書いた直後に OFF 側が同じ窓へ 255 を上書きし、
//     半透明が打ち消される(影も SW_HIDE→SW_SHOWNA で往復する)。ほかに、OFF 側の影を勝手に出して
//     しまう(ドラッグ確定前に当たると「位置が更新されない影が取り残される」= 2026-07-29 に潰した形)。
//   ★ここで絞ってよい理由 = OFF へ切り替えた瞬間の 255 復元と影の再表示は、フライアウトのハンドラが
//     対象を名指しで Apply して担っている(KESCMActionComponent.cpp:274 / :299)。この関数は
//     「ON を貼り直す」ためのものなので、OFF を回す必要がそもそも無い。
//     ⚠だから KESCMApplyFor 本体には OFF ガードを入れないこと(上記の復元経路が死ぬ)。
void KESCMApplyAllPanelTranslucency()
{
	for (int32 i = 0; i < kKESCMAlphaCount; ++i)
	{
		if (sTranslucentOn[i])
			KESCMApplyFor(i);
	}
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
static bool            sPanelAlphaShutdown = false;	// ★KESCMShutdownPanelAlpha 済み(以後タイマーを作り直さない)

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
	// ★Shutdown 後の再武装禁止(2026-08-06 再点検)。可視性オブザーバは detach しない設計なので、終了
	//   処理中のパネル破棄が通知を出すと(トグルは ON のまま)ここへ来る。後始末の後にタイマーを
	//   CreateObject し直すと、上の⚠が警告する「生関数ポインタの予約を残したまま .pln が降りる」が
	//   後始末の後から再生成されてしまう。
	if (sPanelAlphaShutdown)
		return;

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
	//   ★対象が複数になったので「**全部** OFF になったら」やめる(2026-08-06)。
	if (!KESCMAnyTranslucentOn())
	{
		sReapplyLeft = 0;		// 全部 OFF になったので追いかけをやめる
		return IIdleTask::kEndOfTime;
	}

	KESCMApplyAllPanelTranslucency();

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

	// ★対象ごとに独立して判断する(2026-08-06。自分のパネルと本体のページパネル)。
	//   片方がドッキング中でも、もう片方は貼り直せなければならないので continue で回す。
	bool16 didApply = kFalse;

	for (int32 which = 0; which < kKESCMAlphaCount; ++which)
	{
		// ★この機能はトグルが ON のときだけ動く(全部 OFF ならフック自体を張っていないが、
		//   外れる直前の取りこぼしもここで弾く)。
		if (!sTranslucentOn[which] || sPaletteWnd[which] == nullptr)
			continue;

		// ★★キャッシュしたハンドルでも「今もそのパネルか」を必ず確かめる(2026-07-29 の自己レビューで追加)。
		//   HWND は OS が使い回すので、パネルを閉じたあと同じ値が別の窓へ再割り当てされうる。検証せずに
		//   GA_ROOT を辿ると**他人のパネルを透かす**(しかもフックは OFF にするまで外れないので、
		//   ON のままパネルを閉じた後もここへ流れ込み続ける)。
		//   ⚠ここでは SDK へ問い合わせ直さない: 失効していたらキャッシュを捨てて次へ進むだけにして、
		//     引き直しは SDK 通知や Apply の経路(KESCMQueryPaletteWindow)へ任せる。このフックは
		//     大量に飛ぶので、1件あたりを軽く保つのが最優先。
		if (!::IsWindow(sPaletteWnd[which]))
		{
			sPaletteWnd[which] = nullptr;	// 失効(パネルを閉じた等)。次からは上の行で弾かれる
			continue;
		}

		// ★クラス名の照合は**窓のイベントのときだけ**行う(2026-07-29)。カーソルが動いただけの回まで
		//   GetClassNameW を叩くのは無駄(秒間 60〜100 回走る)。
		//   ⚠安全性の根拠: HWND が別の窓へ使い回されるには、その窓が作られて**表示される**必要がある。
		//     表示は EVENT_OBJECT_SHOW ＝窓イベントなので、すり替わった瞬間には必ずこの照合を通る。
		//     非表示のままの窓は KESCMQueryTranslucentTarget が "OWL.Dock"/"OWL.FrameDrawer" 以外として
		//     弾くので、透かす対象にもならない。
		//   ⚠2026-08-06: 旧実装はここで窓タイトルも照合していた(自分のパネルかを綴りで見ていた)。
		//     引き直しが WidgetID 狙い撃ちになったので綴りを合わせる相手が無くなり、クラス名までにした。
		//     ここで捨てれば次の Apply が SDK から正しい窓を引き直す。
		if (isWindowEvent && !KESCMClassIs(sPaletteWnd[which], L"OWL.Palette"))
		{
			sPaletteWnd[which] = nullptr;
			continue;
		}

		// ★★当初は「hwnd == sPaletteWnd」で絞っていたが、**OWL.Palette 宛てのイベントは
		//   PARENTCHANGE も LOCATIONCHANGE も一度も飛んでこなかった**(実測 hk=1/0)。子ウィンドウの
		//   移動ではシステムがこれらを生成しないことがある。
		//   → 発信元は問わず、「イベントが来たら対象パネルの今のトップレベル窓を引き直して、
		//     alpha がずれていたら貼る」方式にする。判定は GetAncestor + 属性読みだけで、
		//     ずれていなければ即 continue するので、大量に飛んできても実害はない。
		HWND target = KESCMQueryTranslucentTarget(sPaletteWnd[which]);
		if (target == nullptr)
			continue;				// ドック内で展開中 = 対象外

		// ★★ここは移動中に何度も呼ばれる。「望みの状態と違うときだけ」実処理へ進むこと
		//   (実測では総イベント 1477 件に対し、実際の書き込みは 1 件だけだった)。

		// ①alpha が望みの値か(カーソルが乗っていれば不透明が「望みの値」になる)
		const BYTE want = KESCMEffectiveAlpha(which, target);
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
			//   ここに到達するのはその対象が ON のときだけなので、望みは常に「隠れている」。
			shadowOk = (visible == kFalse);
		}

		if (alphaOk && shadowOk)
			continue;				// どちらも望みどおり = 何もしない

		KESCMApplyFor(which);
		didApply = kTrue;
	}

	// ★遅延の貼り直し(8 回 × 50ms)は**窓のイベントのときだけ**。窓が作り直されて alpha ごと
	//   捨てられるのを追いかけるための仕掛けなので、カーソルが動いただけの回で回すのは純粋な無駄
	//   (しかもカーソルは何度も動くので、その都度 8 回の連鎖を張り直すことになる)。
	//   ★対象が複数でも予約は1本でよい(タイマー側が全対象を貼り直すため)。
	if (isWindowEvent && didApply)
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

// ★★★フックを**本当に外せたときだけ**ハンドルを忘れる(2026-08-12。KBS が 08-11 に直した形を移植)。
//   !以前は戻り値を見ずに nullptr を代入していた。MSDN は UnhookWinEvent が失敗する条件を3つ挙げており
//     (ハンドルが無効／既に外されている／**張ったスレッド以外から呼んだ**)、3つ目のときフックは**まだ生きている**。
//     そこでハンドルを捨てると二度と外す手段が無くなる ---- KESCMShutdownPanelAlpha は nullptr を見て
//     「済んだ」と判断し、**OS が KESCMWinEventProc(参照カウントされない生関数ポインタ)を握ったまま
//     .pln が降りる**。このファイルが ICallbackTimer について二重に防いでいる当の状態そのもの。
//   ⚠★**2026-08-12 訂正＝旧記述「ハンドルを残す副作用は無い」は言いすぎだった**。
//     KESCMInstallWinEventHook は `sWinEventHook != nullptr` を見て即 return するので(上の :671)、
//     **フックが実際には消えているのにハンドルだけ残った場合、張り直しがそのセッション中ずっと no-op**
//     になる＝ドッキング切り替えへの追随が黙って死ぬ。MSDN の失敗3条件のうち「無効なハンドル」
//     「既に外されている」がそれに当たる。
//   ★それでもこの形(戻り値を見て、失敗したら残す)を採るのは、**現実に起こりうる失敗が3つめ
//     (張ったスレッド以外から呼んだ)だけ**だから＝そのときフックは**生きている**ので、Install が
//     no-op になるのは正しい動作になる。呼び手は今もメニュー押下と Shutdown の2つで、どちらもメイン
//     スレッド。⇒ **別スレッドから呼ぶ経路ができたら、ここを再検討する。**
//   !今日の呼び手はすべてメインスレッド(メニュー押下と Shutdown)so実際に失敗したことは無い。
//     狙いは「失敗を握りつぶさない」ことにある。
static void KESCMRemoveWinEventHook()
{
	if (sWinEventHook != nullptr)
	{
		if (::UnhookWinEvent(sWinEventHook))
			sWinEventHook = nullptr;
	}
}

void KESCMShutdownPanelAlpha()
{
	// プラグイン終了時の保険。ポインタは deref せず、停止と解放だけ(終了処理中でも安全)。
	sPanelAlphaShutdown = true;		// ★以後 KESCMScheduleReapply がタイマーを作り直さない(再武装禁止)
	KESCMRemoveWinEventHook();		// ★フックを残したまま .pln が降りると危険

	for (int32 i = 0; i < kKESCMAlphaCount; ++i)
		sPaletteWnd[i] = nullptr;	// 覚えていた HWND も手放す(OS が使い回す値を抱えたままにしない)

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
//   ⚠★★**2026-08-12 訂正＝旧記述「終了時に明示 detach はしない(detach 自体がクラッシュ要因になる)」は
//     撤回**。同日に KESCMDetachPanelVisibilityObserver を新設し、Shutdown から KESCMShutdownPanelAlpha
//     より**前に**呼ぶようにした(理由の全文はそちらの関数コメント)。要は**外さないほうが危ない**＝
//     購読している間セッションが握っているのは「この .pln の中へのポインタ」で、終了処理中のパネル破棄は
//     実際に通知を飛ばすので、消えかけのコードで Update が走る。★KBS が 2026-08-08 に同じ結論へ移って
//     おり、これはその移植。
//     ⚠残る2つのオブザーバ(レイアウト同期・一括クローズ)は今も detach していない。**方針が割れているの
//       ではなく**、「終了処理の途中で壊されるもの(パネル)」を見ているのがこの1本だけ、という違い。
//========================================================================================

//========================================================================================
// カーソルが乗っている間だけ不透明に戻す(IMouseRollOver)
//
//   ★狙い: 半透明は「下が見えて邪魔にならない」ためのものだが、読みたい/操作したいときは
//     不透明の方がよい。カーソルが乗ったら解除し、離れたら戻す。
//   ★仕組み: IMouseRollOver(ui/IMouseRollOver.h)は widget に roll-over 挙動を付けるための
//     公開インターフェイス。MouseEnter / MouseOver / MouseLeave が呼ばれる。
//     .fr でパネル boss(kKESCMPanelWidgetBoss)に IID_IMOUSEROLLOVER として AddIn する。
//   ★★載せ先 = kKESCMPanelWidgetBoss(パネル本体。kPalettePanelWidgetBoss 派生)。AddIn は KESCM.fr:166。
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

	// ★トグルが OFF なら何もしない(2026-08-06 追加)。★ここには長く「OFF のときは中で弾かれる」と
	//   書いてあったが**そうなっていなかった**: KESCMApplyPanelTranslucency は OFF でも窓を探し
	//   (キャッシュが失効していれば SDK へ問い合わせ直す)、alpha=255 を書き、影を SW_SHOWNA する。
	//   ★同じファイルの IsMouseOver() が 2026-07-30 に「OFF なら実測しない」と決めた判断が、
	//     こちら側に届いていなかった(同一ファイル内の割れ)。使っていない人に費用を払わせない。
	//   ⚠OFF へ切り替えた瞬間の 255 への復元は、フライアウトのハンドラが明示的に
	//     KESCMApplyPanelTranslucency() を呼ぶので保証されている(KESCMActionComponent.cpp)。
	if (!KESCMGetPanelTranslucent())
		return;

	KESCMApplyPanelTranslucency();		// → 実測して不透明へ
}

void KESCMPanelRollOver::MouseOver(const PMPoint& localMousePos)
{
	// ★移動のたびに呼ばれる。位置を控えるだけにして窓へは書きに行かない
	//   (Enter で不透明にし終えているので、毎回 SetLayeredWindowAttributes を叩く意味がない)。
	fLastPos = localMousePos;
}

void KESCMPanelRollOver::MouseLeave()
{
	if (!KESCMGetPanelTranslucent())	// ★MouseEnter と同じ理由(上のコメント参照)
		return;

	KESCMApplyPanelTranslucency();		// → 実測して元の半透明へ
}

bool8 KESCMPanelRollOver::IsMouseOver() const
{
	// ★旗を持たないので、その場で実測して答える。
	//   ⚠ヘッダーの契約は厳密には「**直前の MouseEnter/Over/Leave の呼び出しから決まる**」
	//     (IMouseRollOver.h:50)。実測はそれより正確な答えを返すが、契約の文言そのものではない
	//     ——旗を捨てた以上ここで実測する以外に答えようが無く、呼び手の期待にもそちらが合う。
#ifdef WINDOWS
	// ★トグルが OFF なら実測しない(2026-07-30 の再確認で追加)。この AddIn は半透明トグル専用で、
	//   OFF の間は誰もこの答えを使わない。一方 KESCMQueryPaletteWindow はキャッシュが失効していると
	//   SDK(IPanelMgr)へ問い合わせ直すので、使っていない人にその費用を払わせない
	//   (適用側 KESCMEffectiveAlpha が OFF でカーソル位置すら見ないのと同じ方針)。
	// ★この AddIn は**自分のパネルの widget** に付いているので、見るのは自分の側だけ
	//   (本体のページパネルには AddIn できない。あちらのホバーは Win32 フックの OBJID_CURSOR で拾う)。
	if (!sTranslucentOn[kKESCMAlphaSelf])
		return kFalse;
	return KESCMCursorOverWindow(KESCMQueryTranslucentTarget(KESCMQueryPaletteWindow(kKESCMAlphaSelf))) ? kTrue : kFalse;
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

	// ★全部 OFF のときは何もしない。この通知は「文書を1つ開く」だけでも複数回飛ぶ(実測)ので、
	//   使っていない人にまで窓探索を走らせない(貼り直しが要るのは ON のときだけ)。
	if (!KESCMAnyTranslucentOn())
		return;

	KESCMApplyAllPanelTranslucency();

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

	InterfacePtr<IApplication> app(session->QueryApplication());
	if (app == nil)
		return;

	// ★パネルマネージャは本体の起動シーケンスの途中で立ち上がる(kPanelMgrHasStartedMsg が存在する)。
	//   起動サービスから呼ぶとここが nil になる可能性がある。
	// ★★nil でも下の購読へ進むこと(2026-08-06。KBS が 08-04 の監査で直したのと同じ形)。ここで return
	//   すると kAppBoss 側の購読まで道連れになり、**kApplicationSuspendMsg が購読されないまま残る**
	//   ——それは「カーソルをパネルに乗せたまま別アプリへ出ると不透明のまま固まる」を防ぐ唯一の
	//   手掛かりで、パネルマネージャとは何の関係もない。片方の subject が無いことを、もう片方を
	//   あきらめる理由にしない。
	//   ※パレット側の購読はパネルの AutoAttach(KESCMPanelObserver.cpp)がここを呼び直すので後から拾える。
	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr != nil)
	{
		InterfacePtr<ISubject> subject(panelMgr, IID_ISUBJECT);
		if (subject != nil &&
			!subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IPANELMGR, IID_IKESCMPANELVISIBILITYOBSERVER))
		{
			subject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IPANELMGR, IID_IKESCMPANELVISIBILITYOBSERVER);
		}
	}

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

// ★上の鏡像。プラグイン終了時(KESCMPeekStartup::Shutdown)から、**KESCMShutdownPanelAlpha より前に**呼ぶ
//   ＝通知を止めてから道具(タイマーと Win32 フック)を畳む。2026-08-12 追加。
//   ★★なぜ要るか: 購読している間、セッションが握っているのは**この .pln の中へのポインタ**。終了処理の
//     途中でパネルが壊されると通知が飛ぶので、消えかけのコードで Update が走る。
//     ★KBS が 2026-08-08 に同じ理由で新設した(KBSDetachPanelVisibilityObserver)分で、こちらへは
//       歩いてこなかった ---- **修正は兄弟へ自分では歩いてこない**(このプラグインが KBS から
//       ferror チェックや再武装ガードを受け取ったのと、向きが逆になっただけ)。
//   ★Attach 側と**同じ attachment type** で外す(ISubject.h:288 が :280 の対)。Regular で付けたものは
//     Regular で外す。
//   ★外す前に IsAttached を聞くのは、Attach 側が付ける前に聞くのと同じ理由。Attach は2か所から呼ばれる
//     (起動サービスと パネルの AutoAttach)ので、「本当に付いているか」は両側で正直な問い。
//   ★パネルマネージャは終了処理中には既に降りていることがある。そこが nil でも kAppBoss 側の購読は
//     独立so道連れにしない(Attach 側が 2026-08-06 に学んだのと同じ形)。
void KESCMDetachPanelVisibilityObserver()
{
	ISession* session = GetExecutionContextSession();
	IActiveContext* ctx = (session != nil) ? session->GetActiveContext() : nil;
	if (ctx == nil)
		return;

	InterfacePtr<IObserver> obs((IObserver*)ctx->QueryInterface(IID_IKESCMPANELVISIBILITYOBSERVER));
	if (obs == nil)
		return;

	InterfacePtr<IApplication> app(session->QueryApplication());
	if (app == nil)
		return;

	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr != nil)
	{
		InterfacePtr<ISubject> subject(panelMgr, IID_ISUBJECT);
		if (subject != nil &&
			subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IPANELMGR, IID_IKESCMPANELVISIBILITYOBSERVER))
		{
			subject->DetachObserver(ISubject::kRegularAttachment, obs, IID_IPANELMGR, IID_IKESCMPANELVISIBILITYOBSERVER);
		}
	}

	InterfacePtr<ISubject> appSubject(app, IID_ISUBJECT);
	if (appSubject != nil &&
		appSubject->IsAttached(ISubject::kRegularAttachment, obs, IID_IAPPLICATION, IID_IKESCMPANELVISIBILITYOBSERVER))
	{
		appSubject->DetachObserver(ISubject::kRegularAttachment, obs, IID_IAPPLICATION, IID_IKESCMPANELVISIBILITYOBSERVER);
	}
}
