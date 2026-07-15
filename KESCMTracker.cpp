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
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "CTracker.h"
#include "CTrackerEventHandler.h"
#include "IEvent.h"

#include "CursorSpec.h"		// CursorSpec / GetPlugIn()(Alt+左 CMYK のカスタムカーソル)
#include "CursorDefs.h"		// kCrsrTool
#include "ISession.h"		// GetExecutionContextSession(ICursorMgr 取得)
#include "IApplication.h"	// QueryApplication(ICursorMgr 取得)
#include "ICursorMgr.h"		// ClearCache(kFalse カーソル入れ直しの描き直しを確実にする)

#include "KESCMID.h"
#include "KESCMPeek.h"		// KESCMTrackerRevealBegin / KESCMTrackerRevealEnd / CMYK カーソル入口
#include "KESCMCore.h"		// KESCM 共有状態アクセサ(arm/disarm 等)

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
		tracking via the CTrackerEventHandler, not the timer, so this is safe. */
	virtual bool16 WantTimer(ClassID /*trackerTimerBoss*/) { return kFalse; }

	/** Mouse down. Engage on a left-button press only (middle/right keep their normal handling, e.g.
		the context menu). Call the base to do the real tracking setup, then reveal immediately by the
		modifier keys held at press time: 修飾なし=reveal/Hold-to-Hide, Shift=peek 100%,
		Shift+Alt=peek 50%, Alt=CMYK。押下即発動(ホールド待ち時間なし)。
		Return the base's result so the tracking lifecycle stays intact. */
	virtual bool16 BeginTracking(IEvent* theEvent)
	{
		if (theEvent == nil || theEvent->GetType() != IEvent::kLButtonDn)
			return kFalse;

		// ★押下時の「1フレームのゴミ」対策(2026-07-14): Alt 単独(色比較)の押下では、①基底の
		// モーダルカーソル取得(✓の再設定)→②重い CMYK サンプリング(ラスタ化×2)→③CMYK 情報カーソル
		// 設置、とカーソルが多段に切り替わる。ハードウェアカーソルはアプリの処理と独立に OS が合成する
		// ため、この途中状態は実時間でそのまま画面に出る。どこかの段が持つ未初期化の絵にコンポジタの
		// フレームが落ちた時だけゴミが見える(=「出たり出なかったり」の正体。どの段かはカーソル
		// マネージャ実装が非公開のため確定不能)。→ 遷移全体を ICursorMgr::Hide/Show で隠し、完成した
		// CMYK カーソルだけを見せる(タイミング非依存。完成品の表示自体はドラッグ中の入れ直しがゴミゼロ
		// であることで実証済み)。副作用=押下からサンプリング完了までの短い間カーソルが消える。
		const bool16 cmykGesture = (theEvent->OptionAltKeyDown() &&
		                            !theEvent->ShiftKeyDown() && !theEvent->CmdKeyDown());
		InterfacePtr<IApplication> theApp(GetExecutionContextSession()->QueryApplication());
		InterfacePtr<ICursorMgr> cursorMgr(theApp, UseDefaultIID());
		// ★CMYK カーソルが「実際に出る」条件(arm 済み・比較文書生存・Target 窓上)のときだけ Hide/Show で
		//   多段切替を包む。判定は RevealBegin の Alt 分岐と KESCMTrackerCmykCursorWouldShow で共有
		//   (2026-07-15。旧判定は KESCMIsArmed() のみで、Start 済みでも Source 窓や第3の文書上の Alt+左では
		//   CMYK カーソルが出ないのに Hide/Show だけ走り、無駄にカーソルがまたたいていた)。
		const bool16 hideDuringSwitch = (cmykGesture && cursorMgr != nil && KESCMTrackerCmykCursorWouldShow());
		if (hideDuringSwitch)
			cursorMgr->Hide();

		bool16 result = CTracker::BeginTracking(theEvent);
		if (result)
		{
			KESCMTrackerRevealBegin(theEvent->ShiftKeyDown(), theEvent->OptionAltKeyDown(), theEvent->CmdKeyDown());

			// Alt+左「色比較」のとき、カーソル自身に CMYK を描く。CTracker が BeginTracking で用意した
			// modal cursor を自前のカスタムビットマップカーソルへ差し替える(トラッキング終了時に
			// CTracker が自動で元へ戻す)。kTrue(動的)スペックは設定の瞬間に未初期化バッファが見える
			// ため使わない(InstallCmykCursor 参照)。ドラッグ中の数値更新は ContinueTracking が
			// 値の変化時に InstallCmykCursor で入れ直して行う。
			if (KESCMTrackerHasPendingCmykCursor())
				this->InstallCmykCursor();
		}

		// Hide したら必ず対で Show する(サンプリング失敗や result==kFalse の経路も含む。消えっぱなし防止)。
		if (hideDuringSwitch)
			cursorMgr->Show();
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
		KESCMTrackerRevealEnd();
		return result;
	}

	/** トラッキングが中断された(メニュー選択等)場合も reveal 状態を戻す(EndTracking と同じ後始末=
		hold 中に中断されても枠が出っぱなしにならないように)。 */
	virtual void AbortTracking(IEvent* theEvent)
	{
		CTracker::AbortTracking(theEvent);
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
		・ClearCache = CursorID キーのビットマップキャッシュが古い数値の絵を再利用してコールバックが
		  呼ばれないのを防ぐ(キャッシュの実在は ✓ と CursorID を共有した時の取り違えで実測済み)。
		  ドラッグ中でも呼び出しは値の変化時のみ+50ms スロットル付き(最大約20回/秒)。
		・CursorID の交互切替(1021↔1022) = 直前と必ず違うスペックにして、同一スペック再設定の no-op
		  扱いでも描き直しが確実に起きるようにする。HOTC は両IDとも (10,18) なのでカーソル位置は動かない。 */
	void InstallCmykCursor()
	{
		InterfacePtr<IApplication> theApp(GetExecutionContextSession()->QueryApplication());
		InterfacePtr<ICursorMgr> cursorMgr(theApp, UseDefaultIID());
		if (cursorMgr != nil)
			cursorMgr->ClearCache();
		fCmykCursorFlip = !fCmykCursorFlip;
		CursorSpec spec(GetPlugIn()->GetPluginID(), IDFile(),
		                fCmykCursorFlip ? kKESCMCmykCursorResID : kKESCMCmykCursor2ResID,
		                KESCMTrackerCmykCursorProc(), kFalse /*同期描画=ゴミ無し*/);
		this->ChangeModalCursor(spec);
	}
};

CREATE_PMINTERFACE(KESCMTracker, kKESCMTrackerImpl)

// End, KESCMTracker.cpp.
