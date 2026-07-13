//========================================================================================
//
//  KESCMTracker.cpp
//
//  CONFIRMATORY BUILD (2026-07-13): a "proper" capturing tracker for the KESCM tool, written
//  the way the SDK samples do it, to test two things:
//    (1) does it avoid the freeze/crash of the earlier attempt? (the earlier attempt overrode
//        DisableUpdates()/EnableUpdates() to no-ops, which no SDK sample does),
//    (2) does the reveal (KESCM marks) show while the left button is held, given the base
//        CTracker disables document-view updates during tracking?
//
//  Pattern copied from open/components/dynamicdocumentsui AnimationUIButtonTriggerTracker
//  (the SDK's real capturing tracker): override BeginTracking/EndTracking but ALWAYS call the
//  base CTracker::BeginTracking/EndTracking, return the base's result, and NEVER touch
//  DisableUpdates/EnableUpdates. A companion CTrackerEventHandler (IID_IEVENTHANDLER on the same
//  boss) forwards the button-up during capture to EndTracking.
//
//  While this tool is active, a left-button press captures and reveals the marks; releasing
//  hides them. The middle-button gestures are untouched (both input methods coexist).
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "CTracker.h"
#include "CTrackerEventHandler.h"
#include "IEvent.h"

#include "CursorSpec.h"		// CursorSpec / GetPlugIn()(Alt+左 CMYK のカスタムカーソル)
#include "CursorDefs.h"		// kCrsrTool
#include "ShuksanID.h"		// kPatientUserBoss(修飾なし reveal のホールド遅延タイマー)

#include "KESCMID.h"
#include "KESCMPeek.h"		// KESCMTrackerRevealBegin / KESCMTrackerRevealEnd / CMYK カーソル入口
#include "KESCMCore.h"		// KESCMTogglePanelAtCursor(トリプルクリックで KESCM パネルの表示/非表示)

//____________________________________________________________________________________
//	修飾なし左押下の「reveal」を、押下直後ではなく少しホールドしてから出すための遅延(ms)。
//	狙い: 左ダブルクリック(=ビューポート同期)の 1 打目でマーク枠が一瞬ちらつくのを消す。1 打目は
//	この遅延より前に離れる(EndTracking が下のタイマーを止める)ので reveal は commit されない。純粋な
//	ホールド(覗き見)ではこの遅延の後に枠が出る(ハンドツールのパワーズームが一定ホールドで発動するのと
//	同じ操作感)。値はダブルクリックの 1 打目のホールド時間より長ければよい。実機で調整可。
//	★kKESCMDeferPlainReveal=kFalse にすると従来どおり押下即 reveal(案A)へ戻る(緊急退避用のキルスイッチ)。
//____________________________________________________________________________________
static const bool16 kKESCMDeferPlainReveal   = kTrue;
static const uint32 kKESCMRevealHoldDelayMs  = 100;

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
	KESCMTracker(IPMUnknown* boss) : CTracker(boss), fRevealPending(kFalse)
	{
		fWantsToAutoScroll = kFalse;		// no autoscroll while holding (same as the animation sample)
	}
	virtual ~KESCMTracker() {}

	/** EXPERIMENT (2026-07-13): do NOT suppress document-view updates while tracking.
		CTracker::BeginTracking calls DisableUpdates()->DisableUpdateAllDocumentViews(), which is
		exactly what silences KESCM's InvalidateViews-based mark reveal. By no-op'ing BOTH
		DisableUpdates and EnableUpdates the global suppression counter is left untouched, so views
		stay live during the hold and InvalidateViews works (the marks can show). */
	virtual void DisableUpdates() {}
	virtual void EnableUpdates()  {}

	/** Kill the continuous tracking timers. CTracker::WantTimer returns kTrue for kMouseTrackerBoss,
		which drives HandleContinueTracking/ContinueTracking on a repeating idle even when the mouse
		is steady. With live views that would re-run the heavy KESCM mark compositing every tick and
		freeze the UI. We only need a static reveal, so refuse every timer. Mouse-up still ends
		tracking via the CTrackerEventHandler, not the timer, so this is safe. */
	virtual bool16 WantTimer(ClassID /*trackerTimerBoss*/) { return kFalse; }

	/** Mouse down. Engage on a left-button press or a left double-click (any modifiers; middle/right
		keep their normal handling, e.g. the context menu). Call the base to do the real tracking
		setup, then dispatch by the event kind and the modifier keys held at press time (修飾なし):
		  ・単発押下(kLButtonDn)      = reveal/Hold-to-Hide(ホールド遅延後。下記 ArmDeferredReveal)。
		  ・ダブルクリック(kDoubleClick) = ビューポート同期(中ボタン Alt+ミドルから移植)。さらに押し続けたら
		                                (ホールド遅延後)reveal も出す=単発押下と同じ遅延タイマーを張る。
		  ・トリプルクリック(kTripleClick) = KESCM パネルの表示/非表示トグル(中ボタン Shift+Ctrl+ミドルから移設)。
		  ・クァッドクリック(kQuadrupleClick) = InDesign 標準「ページ」パネルの表示/非表示トグル(中ボタン Ctrl+Alt+ミドルから移設)。
		  ・修飾つき押下                 = Shift=peek 100% / Shift+Alt=peek 50% / Alt=CMYK(押下即発動)。
		★2〜4 打目は SDK が kDoubleClick / kTripleClick / kQuadrupleClick として BeginTracking へ渡す(snapshot
		  サンプル SnapTracker と同じ)。1 打目は通常の kLButtonDn。クリック数を増やす途中で下位のクリック
		  (ダブル=同期 / トリプル=KESCMパネル)が一度ずつ発火する点に注意(クリック数エスカレーションの仕様上
		  避けられない)。
		Return the base's result so the tracking lifecycle stays intact. */
	virtual bool16 BeginTracking(IEvent* theEvent)
	{
		if (theEvent == nil)
			return kFalse;
		const IEvent::EventType et = theEvent->GetType();
		if (et != IEvent::kLButtonDn && et != IEvent::kDoubleClick &&
		    et != IEvent::kTripleClick && et != IEvent::kQuadrupleClick)
			return kFalse;

		bool16 result = CTracker::BeginTracking(theEvent);
		if (result)
		{
			const bool16 noMods = !theEvent->ShiftKeyDown() && !theEvent->OptionAltKeyDown() && !theEvent->CmdKeyDown();

			if (et == IEvent::kQuadrupleClick && noMods)
			{
				// 修飾なしの左クァッドクリック = InDesign 標準「ページ」パネルの表示/非表示トグル
				// (中ボタン Ctrl+Alt+ミドルから移設)。位置移動はしない(単純な表示/非表示)。
				KESCMTogglePagesPanel();
			}
			else if (et == IEvent::kTripleClick && noMods)
			{
				// 修飾なしの左トリプルクリック = KESCM パネルの表示/非表示トグル(中ボタン Shift+Ctrl+ミドルから
				// 移設)。フローティング時はカーソル位置付近にポップ(KESCMTogglePanelAtCursor の仕様)。
				KESCMTogglePanelAtCursor();
			}
			else if (et == IEvent::kDoubleClick && noMods)
			{
				// 修飾なしの左ダブルクリック = ビューポート同期(1 回)。加えて、その 2 打目を押し続けたら
				// (ホールド遅延後)通常の reveal(枠表示)も出す=単発押下と同じ遅延タイマーを張る。2 打目を
				// 素早く離してトリプルクリックへ続けた場合は EndTracking がタイマーを止めるので reveal は出ない。
				KESCMTrackerDocSync(theEvent);
				ArmDeferredReveal();
			}
			else if (noMods)
			{
				// 修飾なしの単発押下(kLButtonDn): reveal を「押下即」ではなくホールド遅延後に出す。
				ArmDeferredReveal();
			}
			else
			{
				// 修飾つき押下(Shift=peek / Shift+Alt=peek50 / Alt=CMYK)は従来どおり押下即発動
				// (ダブル/トリプルクリックは修飾なし限定なので、これらは遅延させる必要がない)。
				KESCMTrackerRevealBegin(theEvent->ShiftKeyDown(), theEvent->OptionAltKeyDown(), theEvent->CmdKeyDown());

				// Alt+左「色比較」のとき、パネル状態行に加えてカーソル自身にも CMYK を描く。CTracker が
				// BeginTracking で用意した modal cursor を、自前のカスタムビットマップカーソルへ差し替える
				// (トラッキング終了時に CTracker が自動で元へ戻す)。bDynamicBitmap=kTrue で毎回描き直し
				// (前回サンプルのビットマップがキャッシュされないように)。
				if (KESCMTrackerHasPendingCmykCursor())
				{
					CursorSpec spec(GetPlugIn()->GetPluginID(), IDFile(), kCrsrTool,
					                KESCMTrackerCmykCursorProc(), kTrue /*bDynamicBitmap*/);
					this->ChangeModalCursor(spec);
				}
			}
		}
		return result;
	}

	/** 修飾なし押下の reveal(枠表示)を「ホールド遅延後」に出すための予約。ホールド判定タイマー
		(kPatientUserBoss=ワンショット用途。連続再描画でフリーズする kMouseTrackerBoss とは別物)を張るだけで、
		発火は TimerMessage()。素早く離せば EndTracking がタイマーを止めるので reveal は出ない(ダブルクリックの
		ちらつき防止、およびトリプルクリックへ続けても reveal しない)。単発押下・ダブルクリック後ホールドの両方が使う。
		★キルスイッチ kKESCMDeferPlainReveal=kFalse のときは従来どおり押下即 reveal に戻す。 */
	void ArmDeferredReveal()
	{
		if (kKESCMDeferPlainReveal)
		{
			fRevealPending = kTrue;
			this->StartTimer(kPatientUserBoss, kKESCMRevealHoldDelayMs);
		}
		else
		{
			KESCMTrackerRevealBegin(kFalse /*shift*/, kFalse /*alt*/, kFalse /*cmd*/);	// 修飾なし即時 reveal
		}
	}

	/** ホールド遅延タイマー(kPatientUserBoss)の発火。修飾なし押下が遅延中(fRevealPending)で、まだ
		トラッキング継続中なら、ここで初めて reveal を出す。以後の繰り返し発火を避けるため即 StopTimer し、
		ワンショット化する(ContinueTracking は駆動しないタイマーなので連続再描画のフリーズは起きない)。 */
	virtual void TimerMessage(int32 /*flags*/)
	{
		if (fRevealPending && this->IsTracking())
		{
			fRevealPending = kFalse;
			this->StopTimer(kPatientUserBoss);
			KESCMTrackerRevealBegin(kFalse /*shift*/, kFalse /*alt*/, kFalse /*cmd*/);	// 修飾なし reveal
		}
	}

	/** Mouse up. Call the base first, then hide the marks. Also cancel any pending hold-delay reveal
		(素早い離し=ダブルクリックの 1 打目なら、ここで reveal を出さずに終える)。 */
	virtual bool16 EndTracking(IEvent* theEvent)
	{
		if (fRevealPending)
		{
			fRevealPending = kFalse;
			this->StopTimer(kPatientUserBoss);
		}
		bool16 result = CTracker::EndTracking(theEvent);
		KESCMTrackerRevealEnd();
		return result;
	}

	/** トラッキングが中断された(メニュー選択等)場合も、保留中のホールド遅延タイマーを片付けて
		reveal 状態を戻す(EndTracking と同じ後始末)。 */
	virtual void AbortTracking(IEvent* theEvent)
	{
		if (fRevealPending)
		{
			fRevealPending = kFalse;
			this->StopTimer(kPatientUserBoss);
		}
		CTracker::AbortTracking(theEvent);
		KESCMTrackerRevealEnd();
	}

private:
	bool16 fRevealPending;	// 修飾なし押下で reveal をホールド遅延中(タイマー発火待ち)か
};

CREATE_PMINTERFACE(KESCMTracker, kKESCMTrackerImpl)

// End, KESCMTracker.cpp.
