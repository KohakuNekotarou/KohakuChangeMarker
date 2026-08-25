//========================================================================================
//
//  KCMPeekGesture.cpp
//
//  ツール(左ボタン)のジェスチャ判定と押下中の表示切替(KCMPeek.cpp から分離。2026-08-13 の
//  model/UI 分割 第1段 Task 1)。修飾キー→ジェスチャの分類、reveal/peek の開始と終了、マウス直下の
//  文書ウィンドウ判定、そして一括クローズ後の UI 後片付けを持つ。
//
//  ★分離では関数の中身を1行も変えていない。変えたのは「どのファイルに座るか」と「誰から見えるか」だけ。
//    arm 状態(sPeekArmed / sPeekTargetDB / sPeekSourceDB)は model 側の KCMPeek.cpp に残るので、
//    ここからは **IKCMCompareFacade** 越しに読む(IsArmed / GetArmedTargetDB / GetArmedSourceDB)。
//    ⚠2026-08-17 訂正: 分離当初の「KCMCore.h のアクセサ(KCMIsArmed 等)で読む」は第1段 Task 11 で
//    Facade 経由に変わっている。**同名の関数は model 側に今も実在する**ため、古い記述を残すと
//    「UI が model を直に呼んでいる」と誤読させる(KCMViewSync.cpp の冒頭にも同じ誤りがあった)。
//    CMYK(Alt+左)の押下中状態は KCMCmykCursor.cpp が持つので、押下の開始/終了はそちらの入口を呼ぶ。
//
//  UI 側: ツールが押されているかという、model からは見えない状態を読む。
//
//  NOTE: ここのクローズ処理は UI 側の半分だけ。model 側の対は KCMPeek.cpp の
//  KCMHandleDocsClosed() で、そちらは閉じた文書の追跡状態を捨てる。同じクローズ通知を別の目的で
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
#include "IKCMMarkData.h"          // マーク/overset の読み取り(2026-08-13 Task 12。押下中の表示状態の
                                     // 読み書きは IKCMCompareFacade 側＝あちらは書ける)
#include "KCMUIShared.h"	// panel / status line / nav readout / tool button (split from KCMCore.h on 2026-08-13)
#include "KCMScrollMap.h"          // 一括クローズ後の地図 strip の撤去/再描画
#include "KCMThumbIdleTask.h"      // クローズ後の再生成を次のidleに遅延(前面切替の過渡を避ける)
#include "Utils.h"                   // Utils<IKCMCompareFacade>()
#include "IKCMCompareFacade.h"     // peek の表示・arm 状態・基準不透明度(2026-08-13・分割 第1段 Task 11)
#include "KCMCmykCursor.h"         // KCMCmykBeginPress / KCMCmykEndPress(押下中の CMYK 状態はあちらが持つ)
#include "KCMBoundaryID.h"         // kKCMModeStory(比較モードの列挙子)
#include "KCMStoryPressMarks.h"    // Story モードで押下中に出す色地マーク(2026-08-22)
#include "KCMViewLookup.h"         // KCMQueryViewUnderMouse / KCMQueryMouseContentPoint /
                                     // KCMQueryPanorama(いずれも UI 側。2026-08-15・第2段 Task 4B)
#include "KCMPeekGesture.h"

// Shift+左=旧版を不透明(100%)で / Shift+Alt+左=旧版を 50% で重ねて peek。
// 押下中だけ表示し、ツール左ボタンを離すと消す(修飾キーは離してもよい)。判定はツール左ボタン押下時に1回見るだけ。
static const PMReal kKCMPeekSemiOpacity = 0.5;	// Shift+Alt+左時の旧版の不透明度(0..1)
static bool16 sPeekActive        = kFalse;	// Shift/Shift+Alt+左を押し込み中(=覗き表示中)か
static bool16 sSingleShowing     = kFalse;	// 修飾なしのツール左hold中(=全マークを選択不透明度25%/75%で一時表示中)か。離すと隠す＋基準opacityへ
// ★★**どちらの窓を覗いているか**(2026-08-22＝Source からも覗けるようにしたときに要った)。
//   離すときに再描画するのは**押した窓**で、それまでは Target 決め打ちだった ---- 旧版の窓から覗くと
//   離しても絵が消えない(sShowOriginal は落ちるが、その文書に再描画が飛ばない)。
//   ⚠**これは UI の状態**＝「どの窓でボタンを押したか」は窓の問いで、model は窓を持たない
//     (model 側の sOrigDB は「どの文書のラスタを作ったか」という別の問いで、同じ答えになるのは結果)。
//   ⚠寿命は sPeekActive と同じ。閉じた文書を指したまま使わないよう、離すときに必ず nil へ戻す。
static IDataBase* sPeekUnderDB   = nil;

// マーク(枠/変更数)の表示を切り替えた後、マークが属するドキュメントを再描画して即反映する。
// arm の有無に依らず使えるよう、peek 用の arm 済み Target ではなく「マークが載っている文書」を使う。
static void KCMInvalidateMarksDoc()
{
	Utils<IKCMCompareFacade>()->InvalidateDB(Utils<IKCMMarkData>()->GetMarkedTargetDB());
}

// マウス下のドキュメントが、arm 済みの対象(Target)文書と一致するか。CMYK サンプリング
// (旧 Shift＋Ctrl＋Alt＋ミドル)とスプレッド枠の部分更新(旧 Ctrl＋ミドル)はヒットテストを sPeekTargetDB の
// ページ座標に対して行うため、マウス下が Source 側や無関係な第3文書のウィンドウだと、そちらの
// ローカル座標を対象文書のページ座標として誤って解釈してしまう。対象文書のウィンドウ上で操作した時
// だけ反応させる。
// ★以前は Utils<ILayoutUIUtils>()->GetFrontDocument()(「front(アクティブ)なドキュメント」)で
// 判定していたが、Split Window の新しい側(kLayoutSecondaryPanelWidgetID)を操作しても OWL 内部の
// アクティブ状態追跡が元側のままになるらしく、判定に失敗していた(ユーザー実測で確認)。
// ⇒ QueryWindowUnderPoint ベースに切り替え、マウス位置そのものからドキュメントを特定する
// (アクティブ状態を一切参照しない)。
// ⚠2026-08-17 訂正(API 監査 B-U7): ここは「KCMSyncScrollOtherWindowsUnderMouse と同じ判定に統一」と
//   書いていたが、**その名前の関数は KCM に存在しない**(全数 Grep でこの行だけ)。旧・中ボタン時代の
//   名残で、今この道を共有しているのは KCMViewLookup.cpp の KCMQueryViewUnderMouse。
// ★★その2つは前半3手(GetGlobalMouseLocation → QueryWindowUnderPoint → IDocumentPresentation)が同じで、
//   **同じ問いを2か所で持っている形**。それでも畳まないのは、聞いていることが違うため:
//     ・こちら … 「マウス下の**文書**は Target/Source か」＝文書の問い。3手で答えが出る
//     ・あちら … 「マウス下の**ビュー**はどれか」＝Split Window のペイン特定まで要る(FindWidget + ヒットテスト)
//   あちらを流用すると、ペイン特定という要らない仕事が付いた上に、**レイアウト widget を持たない窓
//   (ストーリーエディター等)で nil になり答えが変わる**。⇒ 片方の窓解決を直すときは、必ずもう片方も見ること。
// 共通部: マウス直下のドキュメントウィンドウの db を返す(無ければ nil)。Target/Source 判定の
// 差分は比較先 db だけなので、窓解決を1本に畳んだ(2026-07-25 監査で重複解消)。
// ★名前は 2026-08-17 に KCMFrontViewIsOverTarget/Source から改めた＝**この判定は front view を
//   一切見ない**のに、名前だけが GetFrontDocument 時代のまま残っていた(このファイルの7箇所＝定義2・
//   コメント1・**呼び出し4**と、他ファイルのコメント2箇所を同時に追随させた)。
//   ⚠2026-08-19(不具合再検査 B-U7)に数え直した＝旧記述の「呼び手7箇所」は誤りで、**呼び手は4つ**
//     (KCMTrackerBeginPeek / temp-hide の Target・Source / reveal の窓判定)。7は「触った箇所」の数。
static IDataBase* KCMQueryDocDbUnderMouse()
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

static bool16 KCMMouseIsOverTarget()
{
	IDataBase* const armedTarget = Utils<IKCMCompareFacade>()->GetArmedTargetDB();
	return (armedTarget != nil && KCMQueryDocDbUnderMouse() == armedTarget) ? kTrue : kFalse;
}

// マウス下のドキュメントウィンドウが Source(比較の旧側=常時表示枠を載せている sSrcDB)かどうか。
// 「Hold to Hide Marks」＋「Always Show Marks on Source」併用時、Source 窓でツール左ボタンを押したときだけ Source 枠を
// 一時退避させるための窓判定(Target 版 KCMMouseIsOverTarget と対称)。Source マークの所属は sSrcDB を
// 正とする(arm の sPeekSourceDB と同一文書だが、判定はマークの実 db に紐づける)。
static bool16 KCMMouseIsOverSource()
{
	IDataBase* const markedSrcDB = Utils<IKCMMarkData>()->GetMarkedSourceDB();
	return (markedSrcDB != nil &&
	        KCMQueryDocDbUnderMouse() == markedSrcDB) ? kTrue : kFalse;
}

// マウス下の窓が「比較の旧側の文書」かどうか。★上の KCMMouseIsOverSource とは**違う問い**なので
// 別に立ててある(2026-08-22):
//   ・上 = 「Source 枠を載せている db の窓か」(枠を一時退避させるための問い)
//   ・下 = 「旧版の文書を見ているか」(どちらの版の変更箇所を出すかの問い)
// ⚠**Story 変更モードでは上は必ず kFalse になる**＝あのモードは枠を載せないので
//   GetMarkedSourceDB() が nil のまま(KCMDrawEventHandler の drawRings は Story で kFalse)。
//   ⇒ 上を流用すると Source 窓で押しても何も出ない。同じ判定を2か所に置いたのではなく、
//     「載せた db」と「arm した db」という**別の事実**を聞いている。
static bool16 KCMMouseIsOverArmedSource()
{
	IDataBase* const armedSource = Utils<IKCMCompareFacade>()->GetArmedSourceDB();
	return (armedSource != nil &&
	        KCMQueryDocDbUnderMouse() == armedSource) ? kTrue : kFalse;
}

//========================================================================================
// トラッカー(左ボタン)用の共有入口。KCM ツール選択中の左ボタン押下/解放から呼ばれる
// (KCMTracker.cpp)。修飾なし押下=マーク reveal を基本に、修飾キーで peek/CMYK を切り替える。
// ここはファイル内の peek 状態(sSingleShowing)を持ち、描画状態(押下中の表示)は
// IKCMCompareFacade 越しに上下する。
//
// ★由来(2026-07-12〜13): もとは中ボタン＋修飾キーのジェスチャだったものをツールの左ボタンへ移植した。
//   修飾なし=マーク一時表示 / Hold to Hide Marks の窓別 temp-hide(Target/Source) /
//   Shift+左=旧版べた載せ peek 100% / Shift+Alt+左=peek 50% / Alt+左(単独)=CMYK 生値サンプリング。
//   中ボタン経路(および Ctrl 系のパネル/再比較ジェスチャ)は撤去済み(2026-07-13)。再比較はページ
//   右クリックメニュー「Refresh Page Comparison」へ移設。
//========================================================================================

// トラッカー(左ボタン)用の peek 開始。arm 済み(Start 後)かつ Target 窓上のときだけ、マウス下スプレッドの
// 旧版を opacity(1.0=不透明 / 0.5=半透明)で重ねる。ハンドツールへの一時切替はしない(トラッカーが既に
// マウスをキャプチャ済みで、ドラッグは ContinueTracking へ行くため不要)。
//
// ★★2026-08-15(第2段 Task 4B): **ビュー解決3本をここへ引き取った**。以前は model 側の
//   KCMPeekShowUnderMouse がこの3本を自分で呼んでいたが、「どの窓か・そのズームは・マウスはどこか」は
//   窓が無ければ答えの無い問いなので、UI 側で観測して値を渡す形にした。
//   ⚠**2つの早期 return は旧実装の同じ位置**(ビューが取れない / 座標が取れない)。そこで戻ると
//   sPeekActive と SetMarksVisible(kFalse) は立ったままになるが、これも旧実装と同じ
//   (＝ボタンを離したときの KCMTrackerRevealEnd が元に戻すので、状態は取り残されない)。
static void KCMTrackerBeginPeek(PMReal opacity)
{
	// ★★★2026-08-22＝**旧版の窓からも覗ける**(ユーザー要望「ソースの方でも、Shift＋の挙動を入れて
	//   欲しい」「ソースからターゲットを覗く感じ」)。それまでは Target 窓の上でしか反応せず、
	//   重ねられるのは常に旧版という一方通行だった。
	//   ★**押した窓が「下」、相手の文書が「重ねる方」**＝どちらの窓からでも「向こうの版」が見える。
	// ⚠Target を先に見る＝同じ文書を自分自身と比較しているとき(sSrcDB==sDB)、両方の判定が真になるので
	//   順序が意味を持つ。Target 側に倒すのは、それが従来からの挙動だから。
	const bool16 overTarget = KCMMouseIsOverTarget();
	const bool16 overSource = overTarget ? kFalse : KCMMouseIsOverArmedSource();
	if (!Utils<IKCMCompareFacade>()->ArmedDocsAlive() || (!overTarget && !overSource))
		return;	// 未 Start / 比較文書が閉じ済み / どちらの比較文書の窓でもない
	sPeekActive = kTrue;
	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
	compare->SetPeekOpacity(opacity);	// 旧版べた載せの不透明度(描画時に参照)
	sSingleShowing = kFalse;
	compare->SetMarksVisible(kFalse);	// 覗き中は枠等を出さない(旧版だけ)

	// マウスが乗っているレイアウトビュー(Split Window対応、KCMQueryViewUnderMouse参照)。
	InterfacePtr<IControlView> view(KCMQueryViewUnderMouse());
	if (view == nil)
		return;

	PMReal mx = 0.0, my = 0.0;
	if (!KCMQueryMouseContentPoint(view, mx, my))
		return;

	// 観測値2つ。content→window スケール(ズーム×デバイス倍率)と、UI ズーム(ユーザーに見える拡大率)。
	// ★これを何 dpi のラスタにするか(下限 50% の頭打ち・16〜300dpi のクランプ)は model 側の判断なので
	//   ここでは触らない。パノラマが引けなければ uiZoom=0 を渡す＝model 側が「頭打ちなし」で扱う
	//   (分離前に peekPano == nil だったときと同じ)。
	const PMReal viewScale = view->GetContentToWindowMatrix().GetXScale();
	PMReal uiZoom = 0.0;
	InterfacePtr<IPanorama> peekPano(KCMQueryPanorama(view));
	if (peekPano != nil)
		uiZoom = peekPano->GetXScaleFactor(kFalse);

	// ★★★2026-08-16: **そのビューが今表示しているスプレッド**も渡す。これが無いと、マスタースプレッドを
	//   表示していても点が通常ページに当たり(両者はペーストボード座標で重なる)、**旧版が1枚も出ない**。
	//   「どのスプレッドを見ているか」は窓の問い＝UI が観測して model へ渡す(Task 4B と同じ分業)。
	//
	// ★★★**向きは引数の順序だけで決まる**(2026-08-22)。model 側(KCMPeekShowAt)がやるのは
	//   「**第1引数の文書でマウス下のページを特定し、第2引数の対応ページをその上にラスタ化する**」で、
	//   どちらが新版かは一度も問わない ---- ページ対応表(KCMBuildPairing / KCMBuildMasterPairing)も
	//   引数の順に作られ、描画側も sOrigDB を見るだけで文書の役割を見ない。
	//   ⇒ **逆向き専用のコードは1行も要らず、押した窓を第1引数に渡すだけでよかった。**
	// ⚠ただし model 側の「未更新スプレッドは重ねない」最適化は `sDB ==` 第1引数 で書かれているので、
	//   Source から覗くときは成立せず、変化の無いスプレッドでもラスタ化する(＝**遅いだけで正しい**)。
	//   対称にするなら sSrcPageToTarget で引き直すことになるが、まず実機で気になるかを見てから。
	IDataBase* const under = overTarget ? compare->GetArmedTargetDB() : compare->GetArmedSourceDB();
	IDataBase* const over  = overTarget ? compare->GetArmedSourceDB() : compare->GetArmedTargetDB();
	sPeekUnderDB = under;		// 離すときに再描画するのはこの窓(宣言のコメント参照)
	compare->ShowPeekAt(under, over,
	                    mx, my, viewScale, uiZoom,
	                    KCMQuerySpreadUIDForView(view));
}

// 修飾キー→ジェスチャの分類(KCMPeekGesture.h 参照)。★割当の定義はここ1本だけ: トラッカーの押下時分岐
// (KCMTracker.cpp)・下の RevealBegin の分岐・temp-hide 判定がすべてこれを使う(2026-07-15 統合)。
KCMGesture KCMClassifyGesture(bool16 shiftDown, bool16 altDown, bool16 cmdDown, bool16 macCtrlDown)
{
	// Ctrl(cmd)を伴う左ボタンは未割当。再比較はページ右クリックメニューへ移設済み、パネル操作は
	// フライアウトへ移行済みで、いずれもトラッカーは扱わない。
	// ★Mac の Control も未割当(2026-07-25 追補): macOS では Control+クリックが副ボタン(コンテキスト
	//   メニュー)の標準ジェスチャなので、左ボタン押下として届いても reveal を横取りしない。
	//   MacCtrlDown() は Windows では常に kFalse なので Windows の挙動は不変。
	if (cmdDown || macCtrlDown)
		return kKCMGestureNone;
	if (altDown && !shiftDown)
		return kKCMGestureCmyk;		// Alt 単独: CMYK 色サンプリング
	if (shiftDown && altDown)
		return kKCMGesturePeek50;		// Shift+Alt: 旧版べた載せ 50%
	if (shiftDown)
		return kKCMGesturePeek100;	// Shift: 旧版べた載せ 100%
	return kKCMGestureReveal;			// 修飾なし: reveal / temp-hide
}

void KCMTrackerRevealBegin(bool16 shiftDown, bool16 altDown, bool16 cmdDown, bool16 macCtrlDown)
{
	KCMCmykClearPending();	// このプレスで CMYK カーソルを出すかは下の Cmyk 分岐で決める(既定=出さない)

	const KCMGesture gesture = KCMClassifyGesture(shiftDown, altDown, cmdDown, macCtrlDown);
	if (gesture == kKCMGestureNone)
		return;	// 未割当(Ctrl/Command 系、Mac の Control)。トラッカーはキャプチャ済みだが描画状態は変えない。

	// ---- 常時表示中の窓別 temp-hide(押している間だけ枠をどけて素の紙面を見る) ----
	// 隠すジェスチャ=reveal と peek(修飾なし/Shift/Shift+Alt)。
	// ★CMYK(Alt 単独)は隠さない=枠を出したままサンプリング(旧・中ボタン Shift+Ctrl+Alt でも枠は
	// 隠れない仕様に一致)。押した窓の枠だけを隠す(Target/Source 別)。
	// ★★★2026-08-22＝**判定を「Hold to Hide Marks」から各トグル自身へ移した**(ユーザー決定)。
	//   規則は「**押している間は反対になる**」の1本＝**その窓のマークが出ていれば隠し、出ていなければ出す**
	//   (出す側は下の reveal と Story 分岐)。⇒ Hold トグルは撤去した。あれの「常時表示」は
	//   「Show Marks on ...」と完全に重複しており、固有だったのはこの temp-hide だけなので、
	//   それをトグル ON のときの標準の挙動として畳んである。
	const bool16 tempHideGesture = (gesture != kKCMGestureCmyk);
	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
	if (tempHideGesture)
	{
		if (compare->GetShowTargetMarks() && !compare->GetMarksTempHidden() && KCMMouseIsOverTarget())
		{
			compare->SetMarksTempHidden(kTrue);
			KCMInvalidateMarksDoc();	// Target を再描画
		}
		// ★★★Source 側は**トグルを見ずに「押した」とだけ言う**(2026-08-22 ユーザー決定＝実装を規則へ)。
		//   出すのか隠すのかは描画側が sSrcMarksOn と **XOR** して決めるので、ここで
		//   GetShowSourceMarks() を見ると**同じ判断が2か所**になる([[one-question-one-place]])。
		//   ⇒ これで**トグル OFF の Source 窓を押せば枠が出る**＝規則が3か所で宣言していた
		//     「Pixel/Story・Target/Source すべてで同じ」に実装が追いついた。
		//   ⚠Target 側(上)が今も GetShowTargetMarks() を見ているのは非対称に見えるが、あちらは
		//     「出す」を別のフラグ(sMarksVisible・下の reveal)が持っていて、そちらが peek からも
		//     立つため1本に畳めない。**畳めるのは Source だけ。**
		if (!compare->GetSrcMarksPressed() && KCMMouseIsOverSource())
		{
			compare->SetSrcMarksPressed(kTrue);
			compare->InvalidateDB(Utils<IKCMMarkData>()->GetMarkedSourceDB());	// Source を再描画(compare は上で引いてある)
		}
	}

	// ---- ジェスチャ分岐 ----
	if (gesture == kKCMGestureCmyk)
	{
		// ★押下中の CMYK 状態は KCMCmykCursor.cpp が持つ(2026-08-13 の分割)。中身は分割前と同一。
		//   sCmykCursorPending の初期化(このプレスで CMYK カーソルを出すかの既定=出さない)も向こうが行う。
		KCMCmykBeginPress();
		return;
	}
	if (gesture == kKCMGesturePeek50)
	{
		// Shift+Alt+左: 旧版べた載せ peek を 50% で(旧・中ボタン Shift+Alt+ミドル)。
		KCMTrackerBeginPeek(kKCMPeekSemiOpacity);
		return;
	}
	if (gesture == kKCMGesturePeek100)
	{
		// Shift+左: 旧版べた載せ peek を 100% 不透明で(旧・中ボタン Shift+ミドル)。
		KCMTrackerBeginPeek(PMReal(1.0));
		return;
	}

	// ---- 修飾なし・Story 変更モード: 押下中だけ「変更した文字そのもの」に色地を敷く ----
	// ★★Pixel の reveal(下)とは別の道で、絵の作り方が根本的に違う。Pixel は「ページのどこが違って
	//   見えるか」しか知らないので枠を描くが、Story は「どの文字が変わったか」を知っているので、
	//   その文字の下に色地を敷く(ユーザー指定 2026-08-22。★2026-08-24 までは反転だった＝紙に出せず変更)。
	//   ⇒ 拡大率で大きさが変わらず、ページの外枠も要らない。
	// ⚠上の temp-hide が扱うのは**枠**だけ(Story モードには枠が無い＝KCMDrawEventHandler の drawRings が
	//   Story では kFalse)なので、Story の色地マークの「押している間は反対」はここではなく
	//   KCMStoryMarksRefresh が決める＝押した窓のトグルと押下を XOR する(KCMStoryPressMarks.cpp)。
	//   ⇒ **トグル OFF の窓を押せば出て、ON の窓を押せば隠れる**。この分岐は「押した」ことだけを伝える。
	// ★対象は**押した窓の側だけ**(ユーザー選択 2026-08-22)。削除された文字は旧版にしか無く、挿入された
	//   文字は新版にしか無いので、どちらの文書を見ているかでマークできるものが変わる。
	if (compare->GetCompareMode() == kKCMModeStory)
	{
		if (KCMMouseIsOverTarget())
			KCMStoryPressMarksBegin(kFalse /*target*/);
		else if (KCMMouseIsOverArmedSource())
			KCMStoryPressMarksBegin(kTrue /*source*/);
		return;		// 枠の reveal(下)は Story には無い
	}

	// ---- 修飾なし: 通常モードのマーク一時表示(reveal) ----
	// ★「押している間は反対」の**出す側**。Target のマークが既に出ているなら上で temp-hide 済みなので、
	//   ここでは何もしない(2026-08-22＝判定を GetHoldToHideMarks から付け替えた。Hold は撤去)。
	if (compare->GetShowTargetMarks())
		return;

	// 「マークがある」の判定は旧・中ボタンの修飾なし分岐と同一(anyMarkableContent 相当)。
	// ★中身は分割前と同じ5つの OR(変更・overflow 両側・登録 両側)で、overflow 集合を現在の文書対へ
	//   合わせるのも向こうがやる(2026-08-13 Task 12 で IKCMMarkData へ移した)。
	if (!Utils<IKCMMarkData>()->HasAnyMarkableContent())
		return;

	// 通常モード(マーク非表示→押下中だけ表示)。Target 窓の上でだけ reveal する(Source や無関係な窓では
	// 出さない。旧・中ボタンと同じ方針)。
	if (!KCMMouseIsOverTarget())
		return;

	sSingleShowing = kTrue;
	compare->SetMarkScreenOpacity(compare->GetSelectedMarkOpacity());	// パネルの 25%/75%
	compare->SetMarksVisible(kTrue);	// 押下中だけ枠等を表示
	KCMInvalidateMarksDoc();
}

void KCMTrackerRevealEnd()
{
	// ★Alt+左(CMYK)の押下中キャッシュとモード保持の解除は KCMCmykCursor.cpp が持つ(2026-08-13 の分割)。
	//   中身は分割前と同一(フォント解放・hover/other の解除・ステータス行の空白1文字クリア)。
	KCMCmykEndPress();

	// Story 変更モードで押下中に出していた色地マークを消す。⚠押下で出していなければ何もしない
	//   (向こうが自分で覚えている)＝Pixel モードで押して離しても、ジャンプが出した一時マーカーは消えない。
	KCMStoryPressMarksEnd();

	// 押下中に隠していた常時表示の枠を戻す(離すと再表示)＝「押している間は反対になる」の戻り側。
	// 押した窓に応じて Target/Source どちらか(または両方)が立っている。その窓のトグルが OFF なら
	// そもそも立たないので無影響(旧・中ボタン解放時の temp-hide 復元と同一)。
	// ⚠2026-08-22＝旧記述は「『Hold to Hide Marks』で隠していた」と書いていたが、**あのトグルは
	//   同日に撤去された**。立てる条件は各「Show Marks on ...」トグル自身へ移っている
	//   (KCMTrackerRevealBegin)。
	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
	if (compare->GetMarksTempHidden())
	{
		compare->SetMarksTempHidden(kFalse);
		KCMInvalidateMarksDoc();	// Target を再描画
	}
	if (compare->GetSrcMarksPressed())
	{
		compare->SetSrcMarksPressed(kFalse);
		compare->InvalidateDB(Utils<IKCMMarkData>()->GetMarkedSourceDB());	// Source を再描画(compare は上で引いてある)
	}

	if (sPeekActive)
	{
		// Shift／Shift+Alt+左を離した → 旧版べた載せを隠す(マークは触らない)。キャッシュは保持
		// (再 peek は即時)。旧・中ボタン解放時の sPeekActive 復元と同一。
		sPeekActive = kFalse;
		if (compare->GetShowOriginal())
		{
			compare->SetShowOriginal(kFalse);
			// ★**覗いていた窓**を再描画する(2026-08-22)。ここは GetArmedTargetDB() 決め打ちだったので、
			//   Source から覗けるようにした時点で「離しても旧版の窓から絵が消えない」になっていた
			//   ---- sShowOriginal は落ちるのに、その文書へ再描画が飛ばないため。
			//   ⚠採れなかったときは従来どおり Target へ(押していないのにここへ来る道は無いが、
			//     nil を渡して何も起きないより、以前と同じ振る舞いに落ちる方が読める)。
			compare->InvalidateDB((sPeekUnderDB != nil) ? sPeekUnderDB : compare->GetArmedTargetDB());
		}
		sPeekUnderDB = nil;		// 閉じた文書を指したまま残さない
	}
	else if (sSingleShowing)
	{
		// 通常モードの reveal 解除 → 枠表示を解除し、不透明度を基準値へ戻す＋非表示へ(旧・中ボタン解放時の
		// sSingleShowing 復元と同じ)。
		sSingleShowing = kFalse;
		compare->SetMarksVisible(kFalse);
		compare->SetMarkScreenOpacity(compare->GetBaseScreenOpacity());
		KCMInvalidateMarksDoc();
	}
}

// KCMResetPeekGestureState(KCMPeekGesture.h 参照) — 押下中の表示状態を初期化する。
// ★2026-08-13 の分割で新設。分割前は model 側の3か所(KCMDoArmMousePeek / KCMDoDisarmMousePeek /
//   KCMHandleDocsClosed)がいずれも同じ2行を直接書いていた。
//
// ⚠★★2026-08-19(不具合再検査 B-U7)に数え直した＝**呼び手は1つだけ**。旧記述の「呼び手は model 側の
//   3か所」は分割後に成り立っていない ---- model 側は3か所とも「これは UI の状態なので UI がやる」に
//   書き換わり(KCMPeek.cpp の arm/disarm/クローズ掃除)、UI 側で実際に呼んでいるのは
//   **KCMModelChangeObserver の kKCMComparisonDocsClosedMessage 分岐(比較が終わったときだけ)**
//   の1か所。arm(Start)と disarm(Stop)の通知分岐からは呼んでいない。
// ★★**それでも不具合ではない**(2026-08-19 に全経路を確認した):
//   この2つのフラグが立つのは押下中だけで、押下の終わり方は3つしかなく、**3つとも
//   KCMTrackerRevealEnd を通る** ---- KCMTracker.cpp の EndTracking(通常の解放)・AbortTracking
//   (メニュー等で中断)・BeginTracking で基底がトラッキングを断った経路。∴ Start/Stop の時点で
//   フラグが残っていることは無く、万一残っても次の解放で必ず落ちる。
//   ⇒ **足すべきは呼び出しではなく、この説明**(消えかけの「保険」を復活させると、なぜ在るのか
//     分からない行がもう1つ増える)。
void KCMResetPeekGestureState()
{
	sPeekActive    = kFalse;
	sSingleShowing = kFalse;
	// ⚠**ここに来る道の1つが「文書が閉じた」**(KCMModelChangeObserver が通知を受けて呼ぶ)なので、
	//   覗いていた窓のポインタは必ず捨てる ---- 閉じた IDataBase* を持ったまま次の解放で使うと、
	//   その先で再描画を頼むことになる。★2026-08-22 に sPeekUnderDB を足したとき、この関数が
	//   「押下中の状態を初期化する」と名乗っている以上ここも直す、と決めた(名前が契約)。
	sPeekUnderDB   = nil;
}

//========================================================================================
// 一括クローズ(複数文書を続けて閉じる / アプリ終了の close-all)の後片付けを1回に畳む
//
//   kAfterCloseDoc は「閉じた文書ごと」に飛ぶ。そのたびに KCMHandleDocsClosed が UI の後片付け
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
// ★2026-08-13: 呼び手(KCMHandleDocsClosed)が KCMPeek.cpp に残ったので static を外した。
//   保留するかどうかの判断と、保留の受け皿(KCMDeferCloseUi)を同じファイルに置いておくため。
bool16 KCMBatchCloseInProgress()
{
	ISession* session = GetExecutionContextSession();	// 終了処理中は nil になり得る
	if (session == nil)
		return kFalse;
	InterfacePtr<IBoolData> filesClosing(session, IID_IKFILESCLOSING);
	return (filesClosing != nil && filesClosing->GetBool()) ? kTrue : kFalse;
}

// 保留していた UI の後片付けを1回だけ流す(一括クローズ完了時)。
static void KCMFlushDeferredCloseUi()
{
	if (!sDeferredCloseUiPending)
		return;
	sDeferredCloseUiPending = kFalse;

	// ★この関数だけで3回聞く(下の2つの入口ガードと、末尾のループの中の InvalidateDB)ので、
	//   1回引いて使い回す(Utils.h:74-80。2026-08-17 の API 監査 B-U7)。
	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());

	if (compare->IsAppQuitting())
		return;		// 終了中は UI に触らない(状態は Shutdown が破棄する)

	// ★★保留している間に新しい比較が始まっていたら、この後片付けは走らせない(2026-07-30 の再確認で追加)。
	//   この関数は「閉じた文書の分を片付ける」ためのものだが、中身(strip 撤去・"marks cleared" 表示)は
	//   対象を選ばず全部に効く。完了通知 kPendingDocumentsClosedMsg が来ないまま保留が残り(監査 B-2 の
	//   穴)、その後ユーザーが Start して、さらに後の一括クローズ完了でまとめて流れると、**動いている
	//   比較の strip を撤去してステータスを "marks cleared" に上書きしてしまう**。
	//   ★arm 中なら片付けるものは無い: 保留が立つのは KCMHandleDocsClosed が比較状態を破棄した
	//     (=disarm した)ときだけで、その後の Start が strip 注入もステータスもパネル更新も済ませている。
	//     閉じた文書の窓は窓ごと消えているので strip も残らない。∴ 旗を下ろすだけでよい。
	if (compare->IsArmed())
		return;

	// Find Overset が(走査文書が生存したまま)単独 ON なら地図は残す。それ以外は撤去する
	// (KCMHandleDocsClosed 側で即時に行っていた処理と同じ判断)。
	if (Utils<IKCMMarkData>()->GetOversetOn())
		KCMScrollMapInvalidateAll();
	else
		KCMScrollMapDetachAll();

	PMString s("marks cleared");	// Stop ボタン(DoClear)と同じメッセージ
	s.SetTranslatable(kFalse);
	KCMSetStatus(s);

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
			compare->InvalidateDB(db);
			KCMScheduleThumbRefresh(db);	// 遅延サムネイル再生成(同じ db は集約される)
		}
	}

	KCMRefreshPanel();
}

/** 一括クローズ完了(kPendingDocumentsClosedMsg)を受けるだけのオブザーバ。.fr の AddIn で
    kActiveContextBoss に IID_IKCMDOCSCLOSEDOBSERVER として同居させている(同居先の理由は
    レイアウト同期オブザーバと同じ=実証済みの構成)。購読先はアプリの subject。 */
class KCMDocsClosedObserver : public CObserver
{
public:
	KCMDocsClosedObserver(IPMUnknown* boss) : CObserver(boss, IID_IKCMDOCSCLOSEDOBSERVER) {}
	~KCMDocsClosedObserver() {}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);
};

CREATE_PMINTERFACE(KCMDocsClosedObserver, kKCMDocsClosedObserverImpl)

void KCMDocsClosedObserver::Update(const ClassID& theChange, ISubject* /*theSubject*/, const PMIID& protocol, void* /*changedBy*/)
{
	if (protocol == IID_IAPPLICATION && theChange == kPendingDocumentsClosedMsg)
		KCMFlushDeferredCloseUi();
}

// アプリ subject への購読を付ける(Startup から1回)。
//
// ★★**この1本だけ終了時に detach しない。** 同じ構成(kActiveContextBoss に同居・アプリ subject を
//   購読)の兄弟2本 ---- KCMModelChangeObserver と KCMPanelVisibilityObserver ---- は
//   どちらも detach する。**差は Update が終了中に何をするかで決まる**:
//     ・あちらの2本 … 分岐の大半が無防備(ModelChange は6分岐のうち IsAppQuitting ガードを持つのが
//                      1つだけ)なので、消えかけのコードで UI を触りうる ⇒ **detach が要る**
//     ・こちら       … Update の中身は KCMFlushDeferredCloseUi ただ1つで、その**入口が二重に
//                      守られている** ⇒ 走っても何もしない(下の2点)
//       ① KCMPeekGestureShutdown() が sDeferredCloseUiPending を落とすので、
//          KCMFlushDeferredCloseUi の**入口の 1つ目のガード**(!sDeferredCloseUiPending)で即 return
//       ② その先も同関数の **IsAppQuitting() ガード**で UI に触らずに返る
//       ⚠2026-08-18(不具合再検査 B-U2)に行番号(":360" / ":364")をやめて名前で引く形にした。
//         **書いた 2026-08-16 の時点では4件とも正しく、翌日の B-U7(40d231b)の1回の編集で同時に外れた**
//         ---- B7 で拾った「行番号参照は1回の挿入で全部同時に腐る」の再現。
//   ⇒ **この非対称は意図であって書き忘れではない。**⚠ただし**根拠は上の2点だけ**なので、
//     どちらかを外すならここに detach を足すこと(足しても害は無い ---- 実際 ModelChange 側が
//     終了時に同じ GetActiveContext() を触って detach しており、Task 13 の終了安全性で PASS している)。
//
// ⚠★★2026-08-16(監査 B-U2)に**理由を書き直した**。旧記述は「**detach 自体がクラッシュ要因になる**
//   (レイアウト同期オブザーバの Shutdown 方針と同じ)」だったが、これは**一般化しすぎ**だった:
//   あちらで落ちたのは KCMSetLayoutSync(kFalse) の経路＝**GetAllLayoutViews で解体中の全ビューを
//   走査する**からで(KCMViewSync.cpp)、**detach という操作そのものではない**。
//   ★反証は同じプラグインの中にあった＝KCMDetachModelChangeObserver は終了時に GetActiveContext()
//     を触って detach しているのに落ちていない。**「危ないのは何か」を1段細かく見れば済んだ。**
void KCMAttachDocsClosedObserver()
{
	ISession* session = GetExecutionContextSession();
	IActiveContext* ctx = (session != nil) ? session->GetActiveContext() : nil;
	if (ctx == nil)
		return;
	InterfacePtr<IObserver> obs((IObserver*)ctx->QueryInterface(IID_IKCMDOCSCLOSEDOBSERVER));
	if (obs == nil)
		return;
	InterfacePtr<IApplication> app(session->QueryApplication());
	InterfacePtr<ISubject> subject(app, IID_ISUBJECT);
	if (subject == nil)
		return;
	if (!subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IAPPLICATION, IID_IKCMDOCSCLOSEDOBSERVER))
		subject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IAPPLICATION, IID_IKCMDOCSCLOSEDOBSERVER);
}

// KCMDeferCloseUi(KCMPeekGesture.h 参照) — UI の後片付けを保留する。
// ★2026-08-13 の分割で新設。呼び手は model 側の KCMHandleDocsClosed ただ1つ。
void KCMDeferCloseUi()
{
	sDeferredCloseUiPending = kTrue;
}

// KCMPeekGestureShutdown(KCMPeekGesture.h 参照) — 終了時の後始末。
void KCMPeekGestureShutdown()
{
	// ★★★**この1行が守りである。**「念のため状態を残さない」ではない ---- KCMDocsClosedObserver は
	//   終了時に detach しない(理由は KCMAttachDocsClosedObserver の上のコメント)ので、Shutdown の
	//   あとでも kPendingDocumentsClosedMsg が届けば Update は走る。そのとき
	//   KCMFlushDeferredCloseUi を**その入口の 1つ目のガード**(!sDeferredCloseUiPending)で
	//   即 return させているのが、この代入。
	// ⚠2026-08-16(監査 B-U2)に書き直した。旧記述は「**終了後に流れることは無いが**、状態を残さない」で、
	//   **流れないことを前提に、自分が守りであることを認識していなかった**。
	//   ⇒ この行を「無駄だから」と外すと、守りが同関数の IsAppQuitting() 一枚だけになる。
	sDeferredCloseUiPending = kFalse;
}
