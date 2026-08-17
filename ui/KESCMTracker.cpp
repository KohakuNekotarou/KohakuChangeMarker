//========================================================================================
//
//  KESCMTracker.cpp
//
//  A capturing tracker for the KESCM tool. While the tool is active, a LEFT-button press
//  captures the mouse and reveals the KESCM comparison marks for as long as the button is held;
//  releasing hides them again. Modifier keys held at press time pick the variant:
//    ・修飾なし   = マーク一時表示(reveal) / Hold to Hide 反転
//    ・Shift      = 旧版べた載せ peek 100%
//    ・Shift+Alt  = 旧版べた載せ peek 50%
//    ・Alt        = クリック点の CMYK 生値サンプリング(カーソルにも CMYK を描く)
//  All variants fire immediately on press (シンプル: ホールド待ち時間なし)。
//
//  Pattern copied from open/components/dynamicdocumentsui AnimationUIButtonTriggerTracker
//  (the SDK's real capturing tracker): override BeginTracking/EndTracking but ALWAYS call the
//  base CTracker::BeginTracking/EndTracking, return the base's result, and NEVER touch
//  DisableUpdates/EnableUpdates. A companion CTrackerEventHandler (IID_IEVENTHANDLER on the same
//  boss) forwards the button-up during capture to EndTracking.
//  ⚠ONE DELIBERATE DEPARTURE FROM THAT PATTERN, and it is the last clause: this tracker DOES
//    override DisableUpdates/EnableUpdates - as no-ops. The reason is written at those two methods
//    below (the base's suppression is exactly what silences KESCM's InvalidateViews-based reveal).
//
//  ★押下中 HUD(押したレイアウトビューの**左上**に "Target" / "Source" / "Not in comparison" /
//    "Not comparing" の1行を出す機能)は**現役**。このファイルが KESCMTrackerHudBegin / End を呼び、
//    描く実体は Draw Event 経路(KESCMTrackerHud.cpp / KESCMTrackerHud.h)。
//    ⚠2026-08-06 に全廃したのは**旧 sprite 版**(sprite 描画層・専用フォント選定・one-shot タイマー)で、
//      **翌 2026-08-07 に Draw Event 方式で作り直された** ---- 枠とまったく同じ描画パスに乗せて
//      **枠と同時に出す**ため(旧版は押下から遅れて出ていた)。
//    ⚠ここに「全廃した」とだけ書いてあった記述は **2026-08-17(監査 B-U6)に訂正**。同じファイルが
//      HUD を呼んでいるのに全廃したと宣言している状態が10日続いていた。
//      経緯と実測は docs/ai-notes/kescm-tracker-hud.md、仕様は KESCMTrackerHud.h の冒頭。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "CTracker.h"
#include "CTrackerEventHandler.h"
#include "IEvent.h"
#include "AutoBusyCursor.h"	// 押下前サンプリング中のコマンド処理系ビジーカーソル抑止

#include "CursorSpec.h"		// CursorSpec / GetPlugIn()(Alt+左 CMYK のカスタムカーソル)
#include "ISession.h"		// GetExecutionContextSession(ICursorMgr 取得)
#include "IApplication.h"	// QueryApplication(ICursorMgr 取得)
#include "ICursorMgr.h"		// Hide/Show(カーソル設置の1フレームを隠す。ClearCache は撤去済み 2026-07-15)

#include "KCMUIID.h"
#include "KESCMConstants.h"	// kKESCMCursorSettleMillis(設置後の落ち着き待ち)
#include "KESCMPeekGesture.h"	// KESCMClassifyGesture / KESCMTrackerRevealBegin / KESCMTrackerRevealEnd
#include "KESCMCmykCursor.h"	// CMYK カーソル入口(HasPending / CursorProc / UpdateCmykDrag)
#include "KESCMTrackerHud.h"	// 押下中だけビュー左上に Target/Source を出す(描画は Draw Event 側)

#include <chrono>			// milliseconds(カーソル設置後の落ち着き待ち)
#include <thread>			// std::this_thread::sleep_for(同上。Win/Mac 共通)

/** ICursorMgr::Hide() → Show() の対を RAII で保証する(2026-07-30 の監査で追加)。
	押下時のカーソル差し替えの1フレーム(「ゴミ」)を隠すために、設置の前後を Hide/Show で囲む
	(経緯と切り分けは KESCMConstants.h の kKESCMCursorSettleMillis を参照)。★対で呼ばないと
	カーソルが消えたままになるので、明示的な Show ではなくスコープに任せる。
	mgr==nil のときは何もしない(隠す必要が無い=CMYK 以外のジェスチャや値が採れなかった場合)。 */
struct KESCMCursorHideGuard
{
	explicit KESCMCursorHideGuard(ICursorMgr* mgr) : fMgr(mgr)
	{
		if (fMgr != nil)
			fMgr->Hide();
	}
	~KESCMCursorHideGuard()
	{
		if (fMgr == nil)
			return;
		// 設置後の「落ち着き待ち」。既定 0=待たない(ゴミが再発したときの調整用に残してある)。
		if (kKESCMCursorSettleMillis > 0)
			std::this_thread::sleep_for(std::chrono::milliseconds(kKESCMCursorSettleMillis));
		fMgr->Show();
	}

private:
	ICursorMgr* fMgr;	// 借り物(呼び出し側の InterfacePtr がスコープ内で保持している)。AddRef しない
};

//____________________________________________________________________________________
//	Tracker event handler: forwards events (notably the button-up) to the tracker while
//	capturing. A bare subclass of CTrackerEventHandler is enough - the base already forwards
//	LButtonUp -> ITracker::EndTracking, MouseDrag -> ContinueTracking, etc.
//____________________________________________________________________________________
class KESCMTrackerEH : public CTrackerEventHandler
{
public:
	KESCMTrackerEH(IPMUnknown* boss) : CTrackerEventHandler(boss) {}
	virtual ~KESCMTrackerEH() {}
};

CREATE_PMINTERFACE(KESCMTrackerEH, kKESCMTrackerEHImpl)

//____________________________________________________________________________________
//	The KESCM tool's tracker. Reveals the marks while the left button is held.
//____________________________________________________________________________________
class KESCMTracker : public CTracker
{
public:
	KESCMTracker(IPMUnknown* boss) : CTracker(boss), fCmykCursorFlip(kFalse)
	{
		fWantsToAutoScroll = kFalse;		// no autoscroll while holding (same as the animation sample)
	}
	virtual ~KESCMTracker() {}

	/** Do NOT suppress document-view updates while tracking. CTracker::BeginTracking calls
		DisableUpdates()->DisableUpdateAllDocumentViews(), which is exactly what silences KESCM's
		InvalidateViews-based mark reveal. By no-op'ing BOTH DisableUpdates and EnableUpdates the
		global suppression counter is left untouched, so views stay live during the hold and
		InvalidateViews works (the marks can show). */
	virtual void DisableUpdates() {}
	virtual void EnableUpdates()  {}

	/** Kill the continuous tracking timers. CTracker::WantTimer returns kTrue for kMouseTrackerBoss,
		which drives HandleContinueTracking/ContinueTracking on a repeating idle even when the mouse
		is steady. With live views that would re-run the heavy KESCM mark compositing every tick and
		freeze the UI. We only need a static reveal, so refuse every timer. Mouse-up still ends
		tracking via the CTrackerEventHandler, not the timer, so this is safe.
		The blanket kFalse is not a shortcut: CTracker::BeginTracking asks for three timers
		(kPatientUserBoss / kMouseTrackerBoss / kDynamicPauseTimerBoss, CTracker.cpp:350-370) but the
		base only ever answers kTrue for kMouseTrackerBoss (CTracker.cpp:905-911), so refusing all
		three behaves exactly like refusing that one - the other two were already off. */
	virtual bool16 WantTimer(ClassID /*trackerTimerBoss*/) { return kFalse; }

	/** Mouse down. Engage on a left-button press only (middle/right keep their normal handling, e.g.
		the context menu). Call the base to do the real tracking setup, then reveal immediately by the
		modifier keys held at press time: 修飾なし=reveal/Hold-to-Hide, Shift=peek 100%,
		Shift+Alt=peek 50%, Alt=CMYK。押下即発動(ホールド待ち時間なし)。
		Return the base's result so the tracking lifecycle stays intact. */
	virtual bool16 BeginTracking(IEvent* theEvent)
	{
		if (theEvent == nil)
			return kFalse;
		// ★左の押下だけで動く(中/右は通常の処理=コンテキストメニュー等に譲る)。
		//   ⚠kLButtonDn 限定にしない(2026-08-06 再点検): 「押して離して即もう一度押す」の2度目は OS の
		//   ダブルクリック時間内だと kDoubleClick で来得る。kDoubleClick は「任意のボタン」(IEvent.h:236)
		//   なので、LButtonDn()(=このイベント発生時に左が押されていたか。IEvent.h:39-42)で左に絞る。
		//   手本の製品トラッカー(AnimationUIButtonTriggerTracker)は型フィルタ自体を持たない。
		const IEvent::EventType evType = theEvent->GetType();
		const bool16 leftPress =
			(evType == IEvent::kLButtonDn) ||
			(evType == IEvent::kDoubleClick && theEvent->LButtonDn());
		if (!leftPress)
			return kFalse;

		// ジェスチャ分類は KESCMClassifyGesture の1本に集約(独立の修飾キー判定を書かない。KESCMPeek.h)。
		// ★Mac 対応(2026-07-25 追補): MacCtrlDown() も渡す。macOS の Control+クリックは副ボタンの標準
		//   ジェスチャなので KESCMClassifyGesture 側で「未割当」に倒す。Windows では常に kFalse。
		const bool16 shiftDown = theEvent->ShiftKeyDown();
		const bool16 altDown   = theEvent->OptionAltKeyDown();
		const bool16 cmdDown   = theEvent->CmdKeyDown();
		const bool16 macCtrl   = theEvent->MacCtrlDown();
		const bool16 cmykGesture =
			(KESCMClassifyGesture(shiftDown, altDown, cmdDown, macCtrl) == kKESCMGestureCmyk);

		// ★押下時の「1フレームのゴミ」対策(2026-07-25 改訂。要点=隠す区間から重い処理を追い出し、設置後に待つ)。
		//   Alt 単独(色比較)の押下では、①基底のモーダルカーソル取得(✓の再設定)→②重い CMYK サンプリング
		//   (ページ対応表の構築＋極小ラスタ化×2)→③CMYK 情報カーソル設置、とカーソルが多段に切り替わる。
		//   ハードウェアカーソルはアプリの処理と独立に OS が合成するため、設置が完成した絵を出す前に一瞬だけ
		//   出す別の絵(未初期化バッファ等)がそのまま画面に出る=間欠的な「ゴミ」(ユーザー報告 2026-07-25:
		//   数値が出る前に一瞬・毎回ではない)。
		//   ★実測(2026-07-25): ②を隠す区間の外へ出して①→③を1ms程度に縮めただけ(Hide/Show 無し)では
		//     ゴミは消えなかった。→ 原因は「切替に時間がかかること」ではなく「設置そのものが持つ1フレーム」
		//     であり、隠す以外に手段は無い。ただし旧方式のように②まで隠すと、隠れている時間のほぼ全部が
		//     ②の計算時間になってカーソルが目に見えて消える(ユーザー報告 2026-07-25)。
		//   → 現方式: ②は隠す前に済ませ(✓カーソルを出したまま計算)、隠すのは「①+③+落ち着き待ち」だけ。
		//     待ち = kKESCMCursorSettleMillis(KESCMConstants.h)。ゴミがまだ出るならその値を増やす。
		//   ★AutoBusyCursor(kFalse) = サンプリング中にコマンド処理系の自動ビジーカーソルが割り込まない
		//     ようにする抑止(基底が InitializeModalCursor でやっているのと同じことを、前倒しで効かせる)。
		//     スコープを抜けると元の状態に戻る。
		if (cmykGesture)
		{
			AutoBusyCursor noBusyCursorWhileSampling(kFalse);
			KESCMTrackerRevealBegin(shiftDown, altDown, cmdDown, macCtrl);
		}

		// session は終了処理中に nil になり得るので QueryApplication の直呼びだけガードする
		// (InterfacePtr(p, iid) 側は p==nil を許す。InterfacePtr.h:459。2026-07-25 追補 に KESCM 全体で統一)。
		// ★隠すのは「CMYK カーソルを実際に出すとき」=値が採れた(Pending)ときだけ。値が採れないのに隠すと、
		//   CMYK カーソルが出ないのにカーソルがまたたくだけになる(2026-07-15 の教訓。判定は Pending 1本)。
		ISession* session = GetExecutionContextSession();
		InterfacePtr<IApplication> theApp(session != nil ? session->QueryApplication() : nil);
		InterfacePtr<ICursorMgr> cursorMgr(theApp, UseDefaultIID());
		const bool16 hideDuringSwitch =
			(cmykGesture && cursorMgr != nil && KESCMTrackerHasPendingCmykCursor());

		bool16 result = kFalse;
		{
			// ★Hide/Show の対は KESCMCursorHideGuard に任せる(2026-07-30 の監査で RAII 化)。Show が走る
			//   位置=このブロックの終わりで、従来の明示的な Show と同じ場所なので挙動は変わらない。
			//   将来ここに途中 return を足しても「カーソルが消えたまま」を作れなくなる。
			KESCMCursorHideGuard cursorHide(hideDuringSwitch ? (ICursorMgr*)cursorMgr : nil);

			result = CTracker::BeginTracking(theEvent);
			if (result)
			{
				// ★押下中 HUD を「出す」印を立てる。★KESCMTrackerRevealBegin の**前**に立てること:
				//   Begin 自身が再描画を要求する(KESCMTrackerHudInvalidate)ので、先に立てておけば
				//   その1回の描画に枠も HUD も乗る ＝ **枠と同時に出る**(旧 sprite 版が枠に遅れて
				//   いた点の是正)。
				//   ⚠2026-08-07 まで、ここには「reveal が呼ぶ InvalidateViews に相乗りする」と
				//     書いてあったが、あれは**Target 窓の上でしか走らない**
				//     (KESCMPeekGesture.cpp の KESCMTrackerRevealBegin＝`KESCMMouseIsOverTarget()`
				//      が偽なら早期 return する。★2026-08-17 に参照先を訂正＝旧記述の
				//      `KESCMPeek.cpp:1841-1844` は分割前の行で、あのファイルは今 907 行しかない)。
				//     そのため Source 窓・Stop 中・第3の文書では HUD が1度も描かれなかった。以後 HUD は
				//     自分で再描画を要求する(理由の全文は KESCMTrackerHud.cpp の Invalidate のコメント)。
				//   描画の実体は KESCMDrawEventHandler の2系統(帯の前面 / カンバス背景)。
				KESCMTrackerHudBegin(fControlView);

				// CMYK 以外(reveal / peek)は従来どおり基底の後で発動する。CMYK は上で済ませてある。
				if (!cmykGesture)
					KESCMTrackerRevealBegin(shiftDown, altDown, cmdDown, macCtrl);

				// Alt+左「色比較」で値が採れていたら、カーソル自身に CMYK を描く。CTracker が BeginTracking で
				// 用意した modal cursor を自前のカスタムビットマップカーソルへ差し替える(トラッキング終了時に
				// CTracker が自動で元へ戻す)。kTrue(動的)スペックは設定の瞬間に未初期化バッファが見える
				// ため使わない(InstallCmykCursor 参照)。ドラッグ中の数値更新は ContinueTracking が
				// 値の変化時に InstallCmykCursor で入れ直して行う。
				if (KESCMTrackerHasPendingCmykCursor())
					this->InstallCmykCursor();
			}
		}	// ← ここで cursorHide のデストラクタが(隠していれば)Show する

		if (!result && cmykGesture)
		{
			// 基底がトラッキングを断った経路(EndTracking は来ない)。先に走らせたサンプリングの保持物
			// =押下中フォント/ページ対応表キャッシュ/単独ピックの文書/ステータス行 をここで必ず返す。
			KESCMTrackerRevealEnd();
		}
		return result;
	}

	/** Mouse drag (移動中)。CTrackerEventHandler が MouseDrag をここへ転送する。WantTimer=kFalse なので
		タイマー駆動では呼ばれず、実際にマウスが動いたときだけ来る。Alt+左「色比較」中は現在位置で CMYK を
		再サンプル(スロットル付き)し、値が変わったらカーソルを描き直す=ドラッグで数値を拾っていく
		(ユーザー要望 2026-07-13)。それ以外のジェスチャ(reveal / peek)では何もしない(base のみ)。 */
	virtual void ContinueTracking(const PBPMPoint& where, bool16 mouseDidMove)
	{
		CTracker::ContinueTracking(where, mouseDidMove);
		// Alt+左「色比較」中: 現在位置で CMYK を再サンプル(KESCMTrackerUpdateCmykDrag 内で 50ms スロットル)
		// し、値が変わったとき(=kTrue が返ったとき)だけ kFalse カーソルを入れ直して描き直す。
		// 動的カーソル(kTrue)は設定の瞬間に未初期化バッファが見える(初回ゴミの真因)ため使わない。
		// kFalse の入れ直しは「コールバックで描き終えてから表示」なのでドラッグ中の更新でもゴミは出ない。
		if (mouseDidMove && KESCMTrackerHasPendingCmykCursor() && KESCMTrackerUpdateCmykDrag())
			this->InstallCmykCursor();
	}

	/** Mouse up. Call the base first, then hide the marks. */
	virtual bool16 EndTracking(IEvent* theEvent)
	{
		bool16 result = CTracker::EndTracking(theEvent);
		// ★HUD の印は KESCMTrackerRevealEnd の**前**に落とす(End 自身が再描画を要求するので、
		//   先に落としておけばその1回の描画で枠と一緒に消える。順序は BeginTracking と対称)。
		KESCMTrackerHudEnd();
		KESCMTrackerRevealEnd();
		return result;
	}

	/** トラッキングが中断された(メニュー選択等)場合も reveal 状態を戻す(EndTracking と同じ後始末=
		hold 中に中断されても枠が出っぱなしにならないように)。 */
	virtual void AbortTracking(IEvent* theEvent)
	{
		CTracker::AbortTracking(theEvent);
		KESCMTrackerHudEnd();			// EndTracking と同じ後始末(中断でも HUD を残さない)
		KESCMTrackerRevealEnd();
	}

private:
	bool16 fCmykCursorFlip;		// CMYK カーソルの CursorID 交互切替の現在側(kFalse=次は1021、kTrue=次は1022)

	/** CMYK 情報カーソルを kFalse(同期描画)スペックで設定する。初回(BeginTracking)と、ドラッグ中に
		値が変わったときの入れ直し(ContinueTracking)の両方がこれを使う。ゴミが出ない根拠と、入れ直しを
		確実に効かせる2つのガード:
		・kFalse = カーソルマネージャがコールバックを同期実行し、描き終えたバッファからカーソルを作って
		  から表示する(✓カーソルでゴミゼロ実証済み)。kTrue(動的)は「表示→後からコールバック」の順に
		  なり未初期化バッファが一瞬見えるため使わない。
		・CursorID の交互切替(1021↔1022) = 直前と必ず違うスペックにして、同一スペック再設定の no-op
		  扱いでも描き直しが確実に起きるようにする。HOTC は両IDとも (10,18) なのでカーソル位置は動かない。
		  呼び出しはドラッグ中でも値の変化時のみ+50ms スロットル付き(最大約20回/秒)。
		★2026-07-15: 以前は毎回 ICursorMgr::ClearCache() も呼んでいた(「CursorID キーのキャッシュが古い
		  数値の絵を再利用する」懸念への保険)。だが ClearCache 無し(交互ID + kFalse 同期スペックのみ)でも
		  古い絵の再利用は起きないことを実機で確認し撤去した。過去に実測した「取り違え」の真因は ✓ と CMYK が
		  CursorID を共有していたこと(1020 と 1021/1022 の分離で解決済み)であり、ClearCache は本来不要だった。
		  ※万一ドラッグ中に「2回前の数値の絵」が出る個体があれば、ここで ClearCache を復活させること。 */
	void InstallCmykCursor()
	{
		fCmykCursorFlip = !fCmykCursorFlip;
		CursorSpec spec(GetPlugIn()->GetPluginID(), IDFile(),
		                fCmykCursorFlip ? kKESCMCmykCursorResID : kKESCMCmykCursor2ResID,
		                KESCMTrackerCmykCursorProc(), kFalse /*同期描画=ゴミ無し*/);
		this->ChangeModalCursor(spec);
	}
};

CREATE_PMINTERFACE(KESCMTracker, kKESCMTrackerImpl)

// End, KESCMTracker.cpp.
