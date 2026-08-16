//========================================================================================
//
//  KESCMPeekGesture.cpp
//
//  ツール(左ボタン)のジェスチャ判定と押下中の表示切替(KESCMPeek.cpp から分離。2026-08-13 の
//  model/UI 分割 第1段 Task 1)。修飾キー→ジェスチャの分類、reveal/peek の開始と終了、マウス直下の
//  文書ウィンドウ判定、そして一括クローズ後の UI 後片付けを持つ。
//
//  ★分離では関数の中身を1行も変えていない。変えたのは「どのファイルに座るか」と「誰から見えるか」だけ。
//    arm 状態(sPeekArmed / sPeekTargetDB / sPeekSourceDB)は KESCMPeek.cpp に残るので、ここからは
//    KESCMCore.h が公開しているアクセサ(KESCMIsArmed / KESCMArmedTargetDB / KESCMArmedSourceDB)で読む。
//    CMYK(Alt+左)の押下中状態は KESCMCmykCursor.cpp が持つので、押下の開始/終了はそちらの入口を呼ぶ。
//
//  UI 側: ツールが押されているかという、model からは見えない状態を読む。
//
//  NOTE: ここのクローズ処理は UI 側の半分だけ。model 側の対は KESCMPeek.cpp の
//  KESCMHandleDocsClosed() で、そちらは閉じた文書の追跡状態を捨てる。同じクローズ通知を別の目的で
//  聞いている——ひとつにまとめないこと。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// オブジェクトモデル:
#include "PersistUtils.h"
#include "IDataBase.h"
#include "IDocument.h"
#include "IEventUtils.h"
#include "IApplication.h"
#include "IDocumentList.h"
#include "ISession.h"
#include "IWindow.h"
#include "IWindowUtils.h"
#include "IDocumentPresentation.h"

// ビューとその倍率(2026-08-15・第2段 Task 4B で model 側から引き取った peek の観測):
#include "IControlView.h"
#include "IPanorama.h"
#include "PMMatrix.h"				// GetContentToWindowMatrix の戻り値(ズーム倍率の読み取り)

// 一括クローズ(複数文書を続けて閉じる)の集約用:
#include "CObserver.h"				// 完了通知オブザーバの基底
#include "ISubject.h"				// AttachObserver/IsAttached
#include "IActiveContext.h"			// オブザーバの同居先(kActiveContextBoss)
#include "IBoolData.h"				// セッション上の IID_IKFILESCLOSING(「今どれかの文書が閉じている最中か」)
#include "LinksUIID.h"				// ★公開ヘッダー: IID_IKFILESCLOSING / kPendingDocumentsClosedMsg(本体 Links UI が提供)

#include "PMReal.h"
#include "PMString.h"

// プロジェクト内インクルード:
#include "KCMUIID.h"
#include "IKESCMMarkData.h"          // マーク/overset の読み取り(2026-08-13 Task 12。押下中の表示状態の
                                     // 読み書きは IKESCMCompareFacade 側＝あちらは書ける)
#include "KESCMUIShared.h"	// panel / status line / nav readout / tool button (split from KESCMCore.h on 2026-08-13)
#include "KESCMScrollMap.h"          // 一括クローズ後の地図 strip の撤去/再描画
#include "KESCMThumbIdleTask.h"      // クローズ後の再生成を次のidleに遅延(前面切替の過渡を避ける)
#include "Utils.h"                   // Utils<IKESCMCompareFacade>()
#include "IKESCMCompareFacade.h"     // peek の表示・arm 状態・基準不透明度(2026-08-13・分割 第1段 Task 11)
#include "KESCMCmykCursor.h"         // KESCMCmykBeginPress / KESCMCmykEndPress(押下中の CMYK 状態はあちらが持つ)
#include "KESCMViewLookup.h"         // KESCMQueryViewUnderMouse / KESCMQueryMouseContentPoint /
                                     // KESCMQueryPanorama(いずれも UI 側。2026-08-15・第2段 Task 4B)
#include "KESCMPeekGesture.h"

// Shift+左=旧版を不透明(100%)で / Shift+Alt+左=旧版を 50% で重ねて peek。
// 押下中だけ表示し、ツール左ボタンを離すと消す(修飾キーは離してもよい)。判定はツール左ボタン押下時に1回見るだけ。
static const PMReal kKESCMPeekSemiOpacity = 0.5;	// Shift+Alt+左時の旧版の不透明度(0..1)
static bool16 sPeekActive        = kFalse;	// Shift/Shift+Alt+左を押し込み中(=覗き表示中)か
static bool16 sSingleShowing     = kFalse;	// 修飾なしのツール左hold中(=全マークを選択不透明度25%/75%で一時表示中)か。離すと隠す＋基準opacityへ

// マーク(枠/変更数)の表示を切り替えた後、マークが属するドキュメントを再描画して即反映する。
// arm の有無に依らず使えるよう、peek 用の arm 済み Target ではなく「マークが載っている文書」を使う。
static void KESCMInvalidateMarksDoc()
{
	Utils<IKESCMCompareFacade>()->InvalidateDB(Utils<IKESCMMarkData>()->GetMarkedTargetDB());
}

// マウス下のドキュメントが、arm 済みの対象(Target)文書と一致するか。CMYK サンプリング
// (旧 Shift＋Ctrl＋Alt＋ミドル)とスプレッド枠の部分更新(旧 Ctrl＋ミドル)はヒットテストを sPeekTargetDB の
// ページ座標に対して行うため、マウス下が Source 側や無関係な第3文書のウィンドウだと、そちらの
// ローカル座標を対象文書のページ座標として誤って解釈してしまう。対象文書のウィンドウ上で操作した時
// だけ反応させる。
// ★以前は Utils<ILayoutUIUtils>()->GetFrontDocument()(「front(アクティブ)なドキュメント」)で
// 判定していたが、Split Window の新しい側(kLayoutSecondaryPanelWidgetID)を操作しても OWL 内部の
// アクティブ状態追跡が元側のままになるらしく、判定に失敗していた(ユーザー実測で確認)。
// KESCMSyncScrollOtherWindowsUnderMouse と同じ QueryWindowUnderPoint ベースの判定に統一し、
// マウス位置そのものからドキュメントを特定する(アクティブ状態を一切参照しない)。
// 共通部: マウス直下のドキュメントウィンドウの db を返す(無ければ nil)。Target/Source 判定の
// 差分は比較先 db だけなので、窓解決を1本に畳んだ(2026-07-25 監査で重複解消)。
static IDataBase* KESCMQueryDocDbUnderMouse()
{
	GSysPoint globalPt = Utils<IEventUtils>()->GetGlobalMouseLocation();

	InterfacePtr<IWindow> hitWindow(Utils<IWindowUtils>()->QueryWindowUnderPoint(globalPt, kFalse));
	if (hitWindow == nil)
		return nil;

	InterfacePtr<IDocumentPresentation> hitPres(hitWindow, UseDefaultIID());
	if (hitPres == nil)
		return nil;

	return hitPres->GetDocumentUIDRef().GetDataBase();
}

static bool16 KESCMFrontViewIsOverTarget()
{
	IDataBase* const armedTarget = Utils<IKESCMCompareFacade>()->GetArmedTargetDB();
	return (armedTarget != nil && KESCMQueryDocDbUnderMouse() == armedTarget) ? kTrue : kFalse;
}

// マウス下のドキュメントウィンドウが Source(比較の旧側=常時表示枠を載せている sSrcDB)かどうか。
// 「Hold to Hide Marks」＋「Show Marks on Source」併用時、Source 窓でツール左ボタンを押したときだけ Source 枠を
// 一時退避させるための窓判定(Target 版 KESCMFrontViewIsOverTarget と対称)。Source マークの所属は sSrcDB を
// 正とする(arm の sPeekSourceDB と同一文書だが、判定はマークの実 db に紐づける)。
static bool16 KESCMFrontViewIsOverSource()
{
	IDataBase* const markedSrcDB = Utils<IKESCMMarkData>()->GetMarkedSourceDB();
	return (markedSrcDB != nil &&
	        KESCMQueryDocDbUnderMouse() == markedSrcDB) ? kTrue : kFalse;
}

//========================================================================================
// トラッカー(左ボタン)用の共有入口。KESCM ツール選択中の左ボタン押下/解放から呼ばれる
// (KESCMTracker.cpp)。修飾なし押下=マーク reveal を基本に、修飾キーで peek/CMYK を切り替える。
// ここはファイル内の peek 状態(sSingleShowing)を持ち、描画状態(押下中の表示)は
// IKESCMCompareFacade 越しに上下する。
//
// ★由来(2026-07-12〜13): もとは中ボタン＋修飾キーのジェスチャだったものをツールの左ボタンへ移植した。
//   修飾なし=マーク一時表示 / Hold to Hide Marks の窓別 temp-hide(Target/Source) /
//   Shift+左=旧版べた載せ peek 100% / Shift+Alt+左=peek 50% / Alt+左(単独)=CMYK 生値サンプリング。
//   中ボタン経路(および Ctrl 系のパネル/再比較ジェスチャ)は撤去済み(2026-07-13)。再比較はページ
//   右クリックメニュー「KCM: Refresh Page Comparison」へ移設。
//========================================================================================

// トラッカー(左ボタン)用の peek 開始。arm 済み(Start 後)かつ Target 窓上のときだけ、マウス下スプレッドの
// 旧版を opacity(1.0=不透明 / 0.5=半透明)で重ねる。ハンドツールへの一時切替はしない(トラッカーが既に
// マウスをキャプチャ済みで、ドラッグは ContinueTracking へ行くため不要)。
//
// ★★2026-08-15(第2段 Task 4B): **ビュー解決3本をここへ引き取った**。以前は model 側の
//   KESCMPeekShowUnderMouse がこの3本を自分で呼んでいたが、「どの窓か・そのズームは・マウスはどこか」は
//   窓が無ければ答えの無い問いなので、UI 側で観測して値を渡す形にした。
//   ⚠**2つの早期 return は旧実装の同じ位置**(ビューが取れない / 座標が取れない)。そこで戻ると
//   sPeekActive と SetMarksVisible(kFalse) は立ったままになるが、これも旧実装と同じ
//   (＝ボタンを離したときの KESCMTrackerRevealEnd が元に戻すので、状態は取り残されない)。
static void KESCMTrackerBeginPeek(PMReal opacity)
{
	if (!Utils<IKESCMCompareFacade>()->ArmedDocsAlive() || !KESCMFrontViewIsOverTarget())
		return;	// 未 Start / 比較文書が閉じ済み / Target 窓以外では反応しない(旧・中ボタン peek 分岐と同じ条件)
	sPeekActive = kTrue;
	InterfacePtr<IKESCMCompareFacade> compare(Utils<IKESCMCompareFacade>().QueryUtilInterface());
	compare->SetPeekOpacity(opacity);	// 旧版べた載せの不透明度(描画時に参照)
	sSingleShowing = kFalse;
	compare->SetMarksVisible(kFalse);	// 覗き中は枠等を出さない(旧版だけ)

	// マウスが乗っているレイアウトビュー(Split Window対応、KESCMQueryViewUnderMouse参照)。
	InterfacePtr<IControlView> view(KESCMQueryViewUnderMouse());
	if (view == nil)
		return;

	PMReal mx = 0.0, my = 0.0;
	if (!KESCMQueryMouseContentPoint(view, mx, my))
		return;

	// 観測値2つ。content→window スケール(ズーム×デバイス倍率)と、UI ズーム(ユーザーに見える拡大率)。
	// ★これを何 dpi のラスタにするか(下限 50% の頭打ち・16〜300dpi のクランプ)は model 側の判断なので
	//   ここでは触らない。パノラマが引けなければ uiZoom=0 を渡す＝model 側が「頭打ちなし」で扱う
	//   (分離前に peekPano == nil だったときと同じ)。
	const PMReal viewScale = view->GetContentToWindowMatrix().GetXScale();
	PMReal uiZoom = 0.0;
	InterfacePtr<IPanorama> peekPano(KESCMQueryPanorama(view));
	if (peekPano != nil)
		uiZoom = peekPano->GetXScaleFactor(kFalse);

	// ★★★2026-08-16: **そのビューが今表示しているスプレッド**も渡す。これが無いと、マスタースプレッドを
	//   表示していても点が通常ページに当たり(両者はペーストボード座標で重なる)、**旧版が1枚も出ない**。
	//   「どのスプレッドを見ているか」は窓の問い＝UI が観測して model へ渡す(Task 4B と同じ分業)。
	compare->ShowPeekAt(compare->GetArmedTargetDB(), compare->GetArmedSourceDB(),
	                    mx, my, viewScale, uiZoom,
	                    KESCMQuerySpreadUIDForView(view));
}

// 修飾キー→ジェスチャの分類(KESCMPeekGesture.h 参照)。★割当の定義はここ1本だけ: トラッカーの押下時分岐
// (KESCMTracker.cpp)・下の RevealBegin の分岐・temp-hide 判定がすべてこれを使う(2026-07-15 統合)。
KESCMGesture KESCMClassifyGesture(bool16 shiftDown, bool16 altDown, bool16 cmdDown, bool16 macCtrlDown)
{
	// Ctrl(cmd)を伴う左ボタンは未割当。再比較はページ右クリックメニューへ移設済み、パネル操作は
	// フライアウトへ移行済みで、いずれもトラッカーは扱わない。
	// ★Mac の Control も未割当(2026-07-25 追補): macOS では Control+クリックが副ボタン(コンテキスト
	//   メニュー)の標準ジェスチャなので、左ボタン押下として届いても reveal を横取りしない。
	//   MacCtrlDown() は Windows では常に kFalse なので Windows の挙動は不変。
	if (cmdDown || macCtrlDown)
		return kKESCMGestureNone;
	if (altDown && !shiftDown)
		return kKESCMGestureCmyk;		// Alt 単独: CMYK 色サンプリング
	if (shiftDown && altDown)
		return kKESCMGesturePeek50;		// Shift+Alt: 旧版べた載せ 50%
	if (shiftDown)
		return kKESCMGesturePeek100;	// Shift: 旧版べた載せ 100%
	return kKESCMGestureReveal;			// 修飾なし: reveal / temp-hide
}

void KESCMTrackerRevealBegin(bool16 shiftDown, bool16 altDown, bool16 cmdDown, bool16 macCtrlDown)
{
	KESCMCmykClearPending();	// このプレスで CMYK カーソルを出すかは下の Cmyk 分岐で決める(既定=出さない)

	const KESCMGesture gesture = KESCMClassifyGesture(shiftDown, altDown, cmdDown, macCtrlDown);
	if (gesture == kKESCMGestureNone)
		return;	// 未割当(Ctrl/Command 系、Mac の Control)。トラッカーはキャプチャ済みだが描画状態は変えない。

	// ---- 「Hold to Hide Marks」モード(常時表示の極性反転)の窓別 temp-hide ----
	// 隠すジェスチャ=reveal と peek(修飾なし/Shift/Shift+Alt)。
	// ★CMYK(Alt 単独)は隠さない=枠を出したままサンプリング(旧・中ボタン Shift+Ctrl+Alt でも枠は
	// 隠れない仕様に一致)。押した窓の枠だけを隠す(Target/Source 別)。
	const bool16 tempHideGesture = (gesture != kKESCMGestureCmyk);
	InterfacePtr<IKESCMCompareFacade> compare(Utils<IKESCMCompareFacade>().QueryUtilInterface());
	if (compare->GetHoldToHideMarks() && tempHideGesture)
	{
		if (!compare->GetMarksTempHidden() && KESCMFrontViewIsOverTarget())
		{
			compare->SetMarksTempHidden(kTrue);
			KESCMInvalidateMarksDoc();	// Target を再描画
		}
		if (compare->GetShowSourceMarks() && !compare->GetSrcMarksTempHidden() &&
		    KESCMFrontViewIsOverSource())
		{
			compare->SetSrcMarksTempHidden(kTrue);
			Utils<IKESCMCompareFacade>()->InvalidateDB(Utils<IKESCMMarkData>()->GetMarkedSourceDB());	// Source を再描画
		}
	}

	// ---- ジェスチャ分岐 ----
	if (gesture == kKESCMGestureCmyk)
	{
		// ★押下中の CMYK 状態は KESCMCmykCursor.cpp が持つ(2026-08-13 の分割)。中身は分割前と同一。
		//   sCmykCursorPending の初期化(このプレスで CMYK カーソルを出すかの既定=出さない)も向こうが行う。
		KESCMCmykBeginPress();
		return;
	}
	if (gesture == kKESCMGesturePeek50)
	{
		// Shift+Alt+左: 旧版べた載せ peek を 50% で(旧・中ボタン Shift+Alt+ミドル)。
		KESCMTrackerBeginPeek(kKESCMPeekSemiOpacity);
		return;
	}
	if (gesture == kKESCMGesturePeek100)
	{
		// Shift+左: 旧版べた載せ peek を 100% 不透明で(旧・中ボタン Shift+ミドル)。
		KESCMTrackerBeginPeek(PMReal(1.0));
		return;
	}

	// ---- 修飾なし: 通常モードのマーク一時表示(reveal) ----
	// Hold to Hide モード中は上で temp-hide 済み=ここでは何もしない(reveal はしない)。
	if (compare->GetHoldToHideMarks())
		return;

	// 「マークがある」の判定は旧・中ボタンの修飾なし分岐と同一(anyMarkableContent 相当)。
	// ★中身は分割前と同じ5つの OR(変更・overflow 両側・登録 両側)で、overflow 集合を現在の文書対へ
	//   合わせるのも向こうがやる(2026-08-13 Task 12 で IKESCMMarkData へ移した)。
	if (!Utils<IKESCMMarkData>()->HasAnyMarkableContent())
		return;

	// 通常モード(マーク非表示→押下中だけ表示)。Target 窓の上でだけ reveal する(Source や無関係な窓では
	// 出さない。旧・中ボタンと同じ方針)。
	if (!KESCMFrontViewIsOverTarget())
		return;

	sSingleShowing = kTrue;
	compare->SetMarkScreenOpacity(compare->GetSelectedMarkOpacity());	// パネルの 25%/75%
	compare->SetMarksVisible(kTrue);	// 押下中だけ枠等を表示
	KESCMInvalidateMarksDoc();
}

void KESCMTrackerRevealEnd()
{
	// ★Alt+左(CMYK)の押下中キャッシュとモード保持の解除は KESCMCmykCursor.cpp が持つ(2026-08-13 の分割)。
	//   中身は分割前と同一(フォント解放・hover/other の解除・ステータス行の空白1文字クリア)。
	KESCMCmykEndPress();

	// 「Hold to Hide Marks」で押下中に隠していた常時表示の枠を戻す(離すと再表示)。押した窓に応じて
	// Target/Source どちらか(または両方)が立っている。モード OFF なら両方 kFalse なので無影響
	// (旧・中ボタン解放時の temp-hide 復元と同一)。
	InterfacePtr<IKESCMCompareFacade> compare(Utils<IKESCMCompareFacade>().QueryUtilInterface());
	if (compare->GetMarksTempHidden())
	{
		compare->SetMarksTempHidden(kFalse);
		KESCMInvalidateMarksDoc();	// Target を再描画
	}
	if (compare->GetSrcMarksTempHidden())
	{
		compare->SetSrcMarksTempHidden(kFalse);
		Utils<IKESCMCompareFacade>()->InvalidateDB(Utils<IKESCMMarkData>()->GetMarkedSourceDB());	// Source を再描画
	}

	if (sPeekActive)
	{
		// Shift／Shift+Alt+左を離した → 旧版べた載せを隠す(マークは触らない)。キャッシュは保持
		// (再 peek は即時)。旧・中ボタン解放時の sPeekActive 復元と同一。
		sPeekActive = kFalse;
		if (compare->GetShowOriginal())
		{
			compare->SetShowOriginal(kFalse);
			Utils<IKESCMCompareFacade>()->InvalidateDB(compare->GetArmedTargetDB());
		}
	}
	else if (sSingleShowing)
	{
		// 通常モードの reveal 解除 → 枠表示を解除し、不透明度を基準値へ戻す＋非表示へ(旧・中ボタン解放時の
		// sSingleShowing 復元と同じ)。
		sSingleShowing = kFalse;
		compare->SetMarksVisible(kFalse);
		compare->SetMarkScreenOpacity(compare->GetBaseScreenOpacity());
		KESCMInvalidateMarksDoc();
	}
}

// KESCMResetPeekGestureState(KESCMPeekGesture.h 参照) — 押下中の表示状態を初期化する。
// ★2026-08-13 の分割で新設。呼び手は model 側の3か所(KESCMDoArmMousePeek / KESCMDoDisarmMousePeek /
//   KESCMHandleDocsClosed)で、分割前はいずれも同じ2行を直接書いていた。
void KESCMResetPeekGestureState()
{
	sPeekActive    = kFalse;
	sSingleShowing = kFalse;
}

//========================================================================================
// 一括クローズ(複数文書を続けて閉じる / アプリ終了の close-all)の後片付けを1回に畳む
//
//   kAfterCloseDoc は「閉じた文書ごと」に飛ぶ。そのたびに KESCMHandleDocsClosed が UI の後片付け
//   (スクロール地図 strip の撤去・InvalidateViews・サムネイル再生成の予約・パネル/ステータス更新)
//   まで行うと、N 文書を一度に閉じたときに N 回走る。状態(メモリ)の破棄はその場で行い、UI 側だけ
//   保留して、全部閉じ終わったところで1回だけ流す(=「集めてから1回」)。解体が進む場面で widget に
//   触る回数が減るので、終了時の堅牢性(特に Mac)にも効く。
//
//   ★「今どれかの文書が閉じている最中か」と「全部閉じ終わった」は本体(Links UI プラグイン)が
//     公開しており、こちらは読むだけでよい(公開ヘッダー LinksUIID.h):
//       ・IID_IKFILESCLOSING        = セッション boss 上の IBoolData(閉じ始めに kTrue、全部閉じたら kFalse)
//       ・kPendingDocumentsClosedMsg = 全部閉じ終わった瞬間にアプリの subject へ飛ぶ通知
//   ★Links UI が無い/無効な環境ではフラグを引けない。その場合は保留せず、従来どおり毎回その場で
//     片付ける(フォールバック=挙動は元のまま)。
//========================================================================================

static bool16 sDeferredCloseUiPending = kFalse;	// UI の後片付けを保留中か(完了通知で1回だけ流す)

// いま一括クローズの最中か(本体が管理するセッションフラグを読むだけ。引けなければ kFalse)。
// ★2026-08-13: 呼び手(KESCMHandleDocsClosed)が KESCMPeek.cpp に残ったので static を外した。
//   保留するかどうかの判断と、保留の受け皿(KESCMDeferCloseUi)を同じファイルに置いておくため。
bool16 KESCMBatchCloseInProgress()
{
	ISession* session = GetExecutionContextSession();	// 終了処理中は nil になり得る
	if (session == nil)
		return kFalse;
	InterfacePtr<IBoolData> filesClosing(session, IID_IKFILESCLOSING);
	return (filesClosing != nil && filesClosing->GetBool()) ? kTrue : kFalse;
}

// 保留していた UI の後片付けを1回だけ流す(一括クローズ完了時)。
static void KESCMFlushDeferredCloseUi()
{
	if (!sDeferredCloseUiPending)
		return;
	sDeferredCloseUiPending = kFalse;

	if (Utils<IKESCMCompareFacade>()->IsAppQuitting())
		return;		// 終了中は UI に触らない(状態は Shutdown が破棄する)

	// ★★保留している間に新しい比較が始まっていたら、この後片付けは走らせない(2026-07-30 の再確認で追加)。
	//   この関数は「閉じた文書の分を片付ける」ためのものだが、中身(strip 撤去・"marks cleared" 表示)は
	//   対象を選ばず全部に効く。完了通知 kPendingDocumentsClosedMsg が来ないまま保留が残り(監査 B-2 の
	//   穴)、その後ユーザーが Start して、さらに後の一括クローズ完了でまとめて流れると、**動いている
	//   比較の strip を撤去してステータスを "marks cleared" に上書きしてしまう**。
	//   ★arm 中なら片付けるものは無い: 保留が立つのは KESCMHandleDocsClosed が比較状態を破棄した
	//     (=disarm した)ときだけで、その後の Start が strip 注入もステータスもパネル更新も済ませている。
	//     閉じた文書の窓は窓ごと消えているので strip も残らない。∴ 旗を下ろすだけでよい。
	if (Utils<IKESCMCompareFacade>()->IsArmed())
		return;

	// Find Overset が(走査文書が生存したまま)単独 ON なら地図は残す。それ以外は撤去する
	// (KESCMHandleDocsClosed 側で即時に行っていた処理と同じ判断)。
	if (Utils<IKESCMMarkData>()->GetOversetOn())
		KESCMScrollMapInvalidateAll();
	else
		KESCMScrollMapDetachAll();

	PMString s("marks cleared");	// Stop ボタン(DoClear)と同じメッセージ
	s.SetTranslatable(kFalse);
	KESCMSetStatus(s);

	// ★生き残っている文書は、保留した時点のものと同じとは限らない(一括クローズなので、その後さらに
	//   閉じられている)。閉じた db を持ち越さないよう、控えたポインタは使わず「今開いている文書」を
	//   その場で列挙する。マークは既に破棄済みなので、無関係な文書を再描画しても枠は描かれない。
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList != nil)
	{
		const int32 nDocs = docList->GetDocCount();
		for (int32 i = 0; i < nDocs; ++i)
		{
			IDocument* doc = docList->GetNthDoc(i);
			if (doc == nil)
				continue;
			IDataBase* db = ::GetUIDRef(doc).GetDataBase();
			Utils<IKESCMCompareFacade>()->InvalidateDB(db);
			KESCMScheduleThumbRefresh(db);	// 遅延サムネイル再生成(同じ db は集約される)
		}
	}

	KESCMRefreshPanel();
}

/** 一括クローズ完了(kPendingDocumentsClosedMsg)を受けるだけのオブザーバ。.fr の AddIn で
    kActiveContextBoss に IID_IKESCMDOCSCLOSEDOBSERVER として同居させている(同居先の理由は
    レイアウト同期オブザーバと同じ=実証済みの構成)。購読先はアプリの subject。 */
class KESCMDocsClosedObserver : public CObserver
{
public:
	KESCMDocsClosedObserver(IPMUnknown* boss) : CObserver(boss, IID_IKESCMDOCSCLOSEDOBSERVER) {}
	~KESCMDocsClosedObserver() {}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);
};

CREATE_PMINTERFACE(KESCMDocsClosedObserver, kKESCMDocsClosedObserverImpl)

void KESCMDocsClosedObserver::Update(const ClassID& theChange, ISubject* /*theSubject*/, const PMIID& protocol, void* /*changedBy*/)
{
	if (protocol == IID_IAPPLICATION && theChange == kPendingDocumentsClosedMsg)
		KESCMFlushDeferredCloseUi();
}

// アプリ subject への購読を付ける(Startup から1回)。
//
// ★★**この1本だけ終了時に detach しない。** 同じ構成(kActiveContextBoss に同居・アプリ subject を
//   購読)の兄弟2本 ---- KESCMModelChangeObserver と KESCMPanelVisibilityObserver ---- は
//   どちらも detach する。**差は Update が終了中に何をするかで決まる**:
//     ・あちらの2本 … 分岐の大半が無防備(ModelChange は6分岐のうち IsAppQuitting ガードを持つのが
//                      1つだけ)なので、消えかけのコードで UI を触りうる ⇒ **detach が要る**
//     ・こちら       … Update の中身は KESCMFlushDeferredCloseUi ただ1つで、その**入口が二重に
//                      守られている** ⇒ 走っても何もしない(下の2点)
//       ① KESCMPeekGestureShutdown() が sDeferredCloseUiPending を落とすので :360 で即 return
//       ② その先も :364 の IsAppQuitting() ガードで UI に触らずに返る
//   ⇒ **この非対称は意図であって書き忘れではない。**⚠ただし**根拠は上の2点だけ**なので、
//     どちらかを外すならここに detach を足すこと(足しても害は無い ---- 実際 ModelChange 側が
//     終了時に同じ GetActiveContext() を触って detach しており、Task 13 の終了安全性で PASS している)。
//
// ⚠★★2026-08-16(監査 B-U2)に**理由を書き直した**。旧記述は「**detach 自体がクラッシュ要因になる**
//   (レイアウト同期オブザーバの Shutdown 方針と同じ)」だったが、これは**一般化しすぎ**だった:
//   あちらで落ちたのは KESCMSetLayoutSync(kFalse) の経路＝**GetAllLayoutViews で解体中の全ビューを
//   走査する**からで(KESCMViewSync.cpp)、**detach という操作そのものではない**。
//   ★反証は同じプラグインの中にあった＝KESCMDetachModelChangeObserver は終了時に GetActiveContext()
//     を触って detach しているのに落ちていない。**「危ないのは何か」を1段細かく見れば済んだ。**
void KESCMAttachDocsClosedObserver()
{
	ISession* session = GetExecutionContextSession();
	IActiveContext* ctx = (session != nil) ? session->GetActiveContext() : nil;
	if (ctx == nil)
		return;
	InterfacePtr<IObserver> obs((IObserver*)ctx->QueryInterface(IID_IKESCMDOCSCLOSEDOBSERVER));
	if (obs == nil)
		return;
	InterfacePtr<IApplication> app(session->QueryApplication());
	InterfacePtr<ISubject> subject(app, IID_ISUBJECT);
	if (subject == nil)
		return;
	if (!subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IAPPLICATION, IID_IKESCMDOCSCLOSEDOBSERVER))
		subject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IAPPLICATION, IID_IKESCMDOCSCLOSEDOBSERVER);
}

// KESCMDeferCloseUi(KESCMPeekGesture.h 参照) — UI の後片付けを保留する。
// ★2026-08-13 の分割で新設。呼び手は model 側の KESCMHandleDocsClosed ただ1つ。
void KESCMDeferCloseUi()
{
	sDeferredCloseUiPending = kTrue;
}

// KESCMPeekGestureShutdown(KESCMPeekGesture.h 参照) — 終了時の後始末。
void KESCMPeekGestureShutdown()
{
	// ★★★**この1行が守りである。**「念のため状態を残さない」ではない ---- KESCMDocsClosedObserver は
	//   終了時に detach しない(理由は KESCMAttachDocsClosedObserver の上のコメント)ので、Shutdown の
	//   あとでも kPendingDocumentsClosedMsg が届けば Update は走る。そのとき
	//   KESCMFlushDeferredCloseUi を :360 で即 return させているのがこの代入。
	// ⚠2026-08-16(監査 B-U2)に書き直した。旧記述は「**終了後に流れることは無いが**、状態を残さない」で、
	//   **流れないことを前提に、自分が守りであることを認識していなかった**。
	//   ⇒ この行を「無駄だから」と外すと、守りが :364 の IsAppQuitting() 一枚だけになる。
	sDeferredCloseUiPending = kFalse;
}
