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
#include "AutoBusyCursor.h"	// 押下前サンプリング中のコマンド処理系ビジーカーソル抑止

#include "CursorSpec.h"		// CursorSpec / GetPlugIn()(Alt+左 CMYK のカスタムカーソル)
#include "ISession.h"		// GetExecutionContextSession(ICursorMgr 取得)
#include "IApplication.h"	// QueryApplication(ICursorMgr 取得)
#include "ICursorMgr.h"		// Hide/Show(カーソル設置の1フレームを隠す。ClearCache は撤去済み 2026-07-15)

#include "KESCMID.h"
#include "KESCMConstants.h"	// kKESCMCursorSettleMillis(設置後の落ち着き待ち)
#include "KESCMPeek.h"		// KESCMTrackerRevealBegin / KESCMTrackerRevealEnd / CMYK カーソル入口

// HUD(押している間だけビュー左上に1行出す)用。描画はトラッカー自身の描画層 = sprite で行う。
// 仕様と、ここに至るまでの実測は docs/ai-notes/kescm-tracker-hud.md。
#include "ISprite.h"						// CreateSprite / Show / Erase / Hide / DestroySprite
#include "NoHandleSprite.h"					// 自前 sprite の基底(= kNoHandleSpriteImpl の実クラス。CSprite 派生で
											// WIDGET_DECL export 済み。実装は WidgetBin.lib 内で .cpp は非公開)
#include "IPathGeometry.h"					// ★HUD では「描くパス」ではなく **更新領域の宣言** として使う(BuildHudRegionPath)
#include "PMPathPoint.h"					// AddPoint に渡す点
#include "NonMarkingAGMGraphicsContext.h"	// sprite に渡す gc(純正のトラッカーもこれを使う)
#include "IShape.h"							// kDrawCreateDynamic(Show のフラグ)
#include "IControlView.h"					// GetContentToWindowMatrix(実ズーム D)
#include "IPanorama.h"						// GetContentLocationAtFrameOrigin(ビュー左上の content 点 = HUD の基準)
#include "IGraphicsContext.h"				// GetViewPort / GetView(gc からポートを取る)
#include "IGraphicsPort.h"					// selectfont / show(文字描画)
#include "IFontMgr.h"						// フォント取得(QueryFont)とインストール済みフォントの列挙
#include "IPMFont.h"						// GetGlyphID / GetNotDefinedGlyph(その字を持っているかの判定)
#include "IFontInstance.h"					// MeasureWText / GetAscent / GetDescent(背景ボックスの寸法)
#include "IFontGroup.h"						// GetNumFonts(フォント走査の内側)
#include "LocaleSetting.h"					// GetSystemScript(ユーザーの言語からフォントを引く)
#include "AutoGSave.h"
#include "WideString.h"						// show に渡す UTF16
#include "KESCMCore.h"						// KESCMIsArmed / KESCMArmedTargetDB / KESCMArmedSourceDB / KESCMFindDocDbForView
#include "KESCMDrawEventHandler.h"			// KESCMQueryPanorama(ビューから IPanorama を辿る共有ヘルパ)
#include "ICallbackTimer.h"					// 押下直後に一発だけ描くための one-shot タイマー
#include "CreateObject.h"					// ::CreateObject(kCallbackTimerBoss, ...)
#include "ShuksanID.h"						// kCallbackTimerBoss / IID_ICALLBACKTIMER
#include "Utils.h"
#include "PMMatrix.h"

// ★押下直後に sprite を出すための one-shot タイマー。BeginTracking の中で(基底の後に)Show しても
// 絵が出ないため、BeginTracking を完全に抜けた直後に一度だけ描かせる。押している間だけ生き、
// EndTracking/AbortTracking で必ず止める(コールバックは参照カウントされない生関数ポインタで、
// 予約を残したままプラグインが降りるとクラッシュする)。
class KESCMTracker;
static ICallbackTimer* sHudTimer   = nil;
static KESCMTracker*   sHudTracker = nil;
static uint32 KESCMHudTimerProc(void* refPtr);

// HUD の基準点(pasteboard 座標)。ShowHud が Show のたびに更新し、KESCMSprite::CreateTrackerPaths が読む。
// ★**パスの bbox 中心を基準にしてはいけない**(2026-07-26 実機で確認): bbox に content 固定サイズの
// パスが混じると、ズームを上げるほど bbox がその方向へ伸び、中心=文字位置が動く(実測 100%→300% で
// 約 6px 右。上下は対称なので動かず、症状が「横だけ」だったのが証拠)。基準点は常にここから読む。
static PMPoint sHudAnchor;

// HUD に出す文字列。★押下時に1回決めて押している間は固定する(押している最中に窓は変わらないため、
// 描画のたびに状態を引き直す必要がない)。
static PMString sHudText;

// HUD の ON/OFF(パネルのフライアウト「Show HUD」)。★既定 = ON。パネル設定として保存される
// (KESCMPanelState の "hudOn")。OFF の間は sprite を作らず、one-shot タイマーも動かさない。
static bool16 sHudOn = kTrue;

// one-shot タイマーの予約中フラグ。★予約が生きている間に重ねて StartTimer しないための門番
// (描画イベントごとに再武装すると、実装次第で予約が二重に積まれる)。立てるのは予約した側、
// 倒すのは発火した側(KESCMHudTimerProc)と後始末(HideHud / Shutdown)。
static bool16 sHudRedrawPending = kFalse;

// ShowHud の実行中。★再入ガード: sprite の描画が文書側の再描画を誘発し、その描画イベントから
// また再描画を要求される…という振動を止める(KESCMTrackerRequestHudRedraw が見る)。
static bool16 sHudDrawing = kFalse;

// 1回の押下で、描画イベント由来の描き直しを受け付ける上限と、その回数。
// ★暴走止め: sprite の後始末(DestroySprite)は必要なら invalidate を出す仕様(CSprite.h:104-108)なので、
//   「描き直す → 再描画が起きる → また描き直す」の往復が理屈の上ではありうる。分析上は、ビューが
//   動いていなければ DestroySprite を通らないので1往復で収まるが、one-shot タイマーの暴走で
//   InDesign を固めた前科がある(下の KESCMHudTimerProc のコメント参照)ため、番人を置いておく。
//   打ち止めても失うのは HUD の追従だけ(押し直せば戻る)。通常のスクロールでは数十回も行かない。
static const int32 kKESCMHudMaxRedrawsPerPress = 300;
static int32 sHudRedrawCount = 0;

/** sHudDrawing の RAII(ShowHud には途中 return が多いので、旗の下ろし忘れを型で防ぐ)。 */
struct KESCMHudDrawGuard
{
	KESCMHudDrawGuard()  { sHudDrawing = kTrue;  }
	~KESCMHudDrawGuard() { sHudDrawing = kFalse; }
};

//____________________________________________________________________________________
//	HUD の寸法。**すべて画面ピクセル**(ズームを変えても見た目が変わらない)。
//	★描く側(KESCMSprite::CreateTrackerPaths)と、更新領域を宣言する側(KESCMTracker::BuildHudRegionPath)
//	  の両方がここを見る。片方だけ変えると「領域が足りず文字が切れる」形で壊れるので、値は1箇所に置く。
//	  型は double(POD)= 静的初期化順序に依存しない。使うときに PMReal へ上げる。
//	文字サイズの経緯(2026-07-26 ユーザー指定): 36px → 24px → 16px(小さすぎ) → 20px。
//____________________________________________________________________________________
static const double kKESCMHudTextPx   = 20.0;	// 文字の大きさ
static const double kKESCMHudPadXPx   =  8.0;	// 下地の左右余白
static const double kKESCMHudPadTopPx =  4.0;	// 下地の上余白
static const double kKESCMHudPadBotPx =  4.0;	// 下地の下余白
static const double kKESCMHudOpacity  =  0.6;	// HUD 全体(下地＋文字)に掛ける合成不透明度(1.0=不透明)
// 文字幅が測れなかったときだけ使う保険の想定幅(画面 px)。通常は MeasureWText の実測値を使う。
static const double kKESCMHudFallbackWidthPx = 600.0;

#include <chrono>			// milliseconds(カーソル設置後の落ち着き待ち)
#include <thread>			// std::this_thread::sleep_for(同上。Win/Mac 共通)

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

/** HUD に出す1行を組む。押した窓(view)と比較状態で4通り:
	  比較中 + Target の窓  → "Target"              … この窓が比較の Target(新版)
	  比較中 + Source の窓  → "Source"              … この窓が比較の Source(旧版)
	  比較中 + それ以外     → "Not in comparison"   … この文書は比較の対象ではない
	  Stop 中               → "Not comparing"       … そもそも比較していない
	★出すのは「押した窓が何か」だけ。相手の文書名は出さない(2026-07-27 ユーザー指示。
	  長い文書名で HUD が伸びるより、いま触っている窓の役割が一目で分かる方を採る)。
	★「出ない」を状態表示に使わない(壊れているのか仕様なのか分からなくなるため)。
	★文字は英語固定。翻訳キー扱いで化けないよう SetTranslatable(kFalse) を必ず通す
	  (UI 文字列のリテラルが内蔵訳に化ける事故が KESCM で実際に起きている)。 */
static PMString KESCMBuildHudText(IControlView* view)
{
	PMString out;

	if (!KESCMIsArmed())
		out = PMString("Not comparing");
	else
	{
		IDataBase* const db = KESCMFindDocDbForView(view);	// 押した窓の文書(ポインタ比較のみ)
		if (db != nil && db == KESCMArmedTargetDB())
			out = PMString("Target");
		else if (db != nil && db == KESCMArmedSourceDB())
			out = PMString("Source");
		else
			out = PMString("Not in comparison");
	}

	out.SetTranslatable(kFalse);
	return out;
}

//____________________________________________________________________________________
//	★★HUD のフォント選び(2026-07-26 実機で日本語が化けたことへの対処)
//
//	なぜ要るか: gPort の selectfont+show は **1つの IPMFont が持つグリフしか使わない**。OS のような
//	フォントフォールバック(その字を持つ別フォントで補う)は起きないので、既定フォント固定だと和文などが
//	出せない。パネルが化けないのは UI の文字が GDI 経由(WDrawString.cpp = CreateFontIndirect +
//	DrawTextEx)で描かれ、GDI のフォントリンクが効くから。★同じ経路(PMDrawStringRGB の
//	IGraphicsContext* 版)を sprite から呼んでも描けなかった(実機 2026-07-26)。あれは gc->GetSysPort()
//	の HDC へ GDI で描く実装で、sprite の非マーキング gc にはその HDC が無いためと見られる。
//	∴ **フォント選択は自前でやる**。
//
//	選ぶ順序(和文フォント名のハードコードはしない。それをすると他言語で同じ問題が再発する):
//	  ①既定フォント            … 欧文だけならこれで足りる(いちばん軽い)
//	  ②ユーザーの言語のフォント … 存在しない名前 + LocaleSetting::GetSystemScript() を QueryFont に
//	                              渡す。writingscript は「名前が見つからないとき、その書記系にふさわしい
//	                              代替を探す」ための引数(IFontMgr.h:166-167)なので、これで**環境の言語に
//	                              合ったフォントを InDesign 自身の論理で選ばせられる**
//	  ③総当たり                … それでも足りなければインストール済みフォントを走査し、全文字を持つ
//	                              最初のものを使う(混在言語や特殊記号の保険)
//____________________________________________________________________________________

/** font がその文字列の全文字のグリフを持っているか。持っていない字が1つでもあれば kFalse。
	判定は「グリフ ID が .notdef(または 0)なら無い」。改行など制御文字は判定から外す。 */
static bool16 KESCMFontCoversText(IPMFont* font, const WideString& text)
{
	if (font == nil)
		return kFalse;

	const Text::GlyphID notdef = font->GetNotDefinedGlyph();
	const int32 numChars = text.CharCount();
	for (int32 i = 0; i < numChars; ++i)
	{
		const UTF32TextChar ch = text.GetChar(i);
		if (ch.GetValue() < 0x20)		// 制御文字(LF 等)は対象外
			continue;
		const Text::GlyphID gid = font->GetGlyphID(ch);
		if (gid == 0 || gid == notdef)
			return kFalse;
	}
	return kTrue;
}

/** その文字列を描けるフォントを1本選ぶ。戻り値は呼び出し側が Release する(取れなければ nil)。 */
static IPMFont* KESCMQueryFontForText(const WideString& text)
{
	InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), UseDefaultIID());
	if (fontMgr == nil)
		return nil;

	// ①既定フォント
	{
		IPMFont* font = fontMgr->QueryFont(fontMgr->GetDefaultFontName());
		if (KESCMFontCoversText(font, text))
			return font;
		if (font != nil)
			font->Release();
	}

	// ②ユーザーの書記系にふさわしいフォント(名前は「必ず見つからない」ものを渡し、代替に落とさせる)
	{
		PMString noSuchFont("__KESCM_no_such_font__");
		noSuchFont.SetTranslatable(kFalse);
		IPMFont* font = fontMgr->QueryFont(noSuchFont, IFontMgr::kNormal, LocaleSetting::GetSystemScript());
		if (KESCMFontCoversText(font, text))
			return font;
		if (font != nil)
			font->Release();
	}

	// ③総当たり(ここに来るのは①②で足りなかったときだけ。結果はキャッシュされるので毎フレームは走らない)
	const int32 numGroups = fontMgr->GetNumFontGroups();
	for (int32 g = 0; g < numGroups; ++g)
	{
		InterfacePtr<IFontGroup> group(fontMgr->QueryFontGroup(g));
		if (group == nil)
			continue;
		const int32 numFonts = group->GetNumFonts();
		for (int32 i = 0; i < numFonts; ++i)
		{
			IPMFont* font = fontMgr->QueryFont(group, i);
			if (KESCMFontCoversText(font, text))
				return font;
			if (font != nil)
				font->Release();
		}
	}

	// ④どれも全文字は持っていなかった → 既定フォントで妥協する。
	//   ★ここで nil を返すと HUD が丸ごと消える(文字だけでなく下地も描かれない)。「一部の字が□」は
	//     見ればわかるが、「何も出ない」は壊れているのか OFF なのか区別できず、たちが悪い。
	//   全滅は実際に起こりうる: 文書名に絵文字が1つ混ざるだけで、和文フォントは絵文字を持たず絵文字
	//   フォントは和文を持たないので、どの1本も「全文字を持つ」条件を満たせなくなる。
	return fontMgr->QueryFont(fontMgr->GetDefaultFontName());
}

// 選定結果のキャッシュ(選定は文字列が変わったときだけ。押下のたびに総当たりしない)。所有する。
// ★sHudFontTried = 「この文字列でもう選定を走らせた」印。**失敗(nil)もキャッシュする**ための旗で、
//   これが無いと nil のときだけキャッシュが効かず、再描画のたびにインストール済み全フォントの
//   総当たり(フォント数 × 文字数の GetGlyphID)が走ってドラッグが固まる。
static IPMFont* sHudFont = nil;
static PMString sHudFontLabel;
static bool16   sHudFontTried = kFalse;

// 文字寸法を測るためのインスタンス(= フォント × サイズ行列)。背景ボックスの大きさに要る
// (幅 = MeasureWText、上下 = GetAscent/GetDescent)。サイズは px/D で決まるのでズームで変わる
// → フォントかサイズが変われば作り直す。所有する。
static IFontInstance* sHudFontInst     = nil;
static PMReal         sHudFontInstSize = 0.0;

/** 寸法測定用のインスタンスを返す(押下解除・フォント差し替え・終了で呼ぶ)。 */
static void KESCMReleaseHudFontInstance()
{
	if (sHudFontInst != nil)
	{
		sHudFontInst->Release();
		sHudFontInst = nil;
	}
	sHudFontInstSize = 0.0;
}

/** labelStr を描けるフォントを返す(キャッシュ付き)。所有はここ側=呼び出し側は Release しない。 */
static IPMFont* KESCMQueryHudFont(const PMString& labelStr, const WideString& label)
{
	if (sHudFontTried && sHudFontLabel == labelStr)
		return sHudFont;				// ★nil でもそのまま返す(失敗も1回で打ち切る)

	if (sHudFont != nil)
	{
		sHudFont->Release();
		sHudFont = nil;
	}
	KESCMReleaseHudFontInstance();		// ★フォントが変わればインスタンスも無効(古いフォントを指したまま測らない)
	sHudFont      = KESCMQueryFontForText(label);
	sHudFontLabel = labelStr;
	sHudFontTried = kTrue;
	return sHudFont;
}

/** font をそのサイズで測るインスタンスを返す(キャッシュ付き)。所有はここ側=呼び出し側は Release しない。
	作り方は KESCMDrawEventHandler.cpp の旧ページ番号バッジと同じ(サイズを対角に入れた行列を渡す)。 */
static IFontInstance* KESCMQueryHudFontInstance(IPMFont* font, const PMReal& size)
{
	if (font == nil || size <= 0)
		return nil;
	if (sHudFontInst != nil && sHudFontInstSize == size)
		return sHudFontInst;

	KESCMReleaseHudFontInstance();
	InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), UseDefaultIID());
	if (fontMgr == nil)
		return nil;
	const PMMatrix fontMatrix(size, 0.0, 0.0, size, 0.0, 0.0);
	sHudFontInst     = fontMgr->QueryFontInstance(font, fontMatrix);
	sHudFontInstSize = size;
	return sHudFontInst;
}

/** フォントキャッシュを返す(押下解除とプラグイン終了で呼ぶ)。 */
static void KESCMReleaseHudFont()
{
	if (sHudFont != nil)
	{
		sHudFont->Release();
		sHudFont = nil;
	}
	sHudFontLabel.Clear();
	sHudFontTried = kFalse;
	KESCMReleaseHudFontInstance();
}

/** HUD 1行の描画幅(content 単位)。下地の大きさにも更新領域の宣言にも同じ値が要るので、測るのは
	ここ1本に集約する(片方が固定値だと、長い文書名で領域が足りず右側が切れる)。
	fontSize は content 単位の文字サイズ(= 画面 px ÷ 実ズーム)。測れなければ kFalse を返す。 */
static bool16 KESCMMeasureHudText(const PMString& labelStr, const WideString& label,
                                  const PMReal& fontSize, PMReal& outWidth)
{
	outWidth = 0.0;
	IPMFont* font = KESCMQueryHudFont(labelStr, label);
	if (font == nil)
		return kFalse;
	IFontInstance* inst = KESCMQueryHudFontInstance(font, fontSize);
	if (inst == nil)
		return kFalse;
	inst->MeasureWText(label, outWidth);
	return (outWidth > 0);
}

//____________________________________________________________________________________
//	HUD の描画層。トラッカーが押されている間だけ、レイアウトビューの左上に1行描く。
//
//	なぜ sprite なのか: Draw Event 経路は押している間も settled キャッシュ層のままで、しかも描画が
//	**ペーストボードにクリップ**されるので窓の隅には描けない。sprite の clip は「レイアウトビュー全体」
//	なので隅に置ける(2026-07-26 実機で確認)。∴ 押下中限定の HUD はこの層でしか作れない。
//
//	基底 = NoHandleSprite(.fr の kNoHandleSpriteImpl の実クラス。CSprite 派生で WIDGET_DECL export
//	済み=継承できる。CSprite.cpp / NoHandleSprite.cpp は非公開だが CreateTrackerPaths が
//	protected virtual)。背景の保存・復元(消去)は基底がやるので自前では書かない。
//
//	★基底の CreateTrackerPaths は呼ばない。基底は「boss の IPathGeometry のパスを描く」実装だが、
//	  HUD ではその IPathGeometry を**描くためではなく「更新領域を宣言する」ためだけ**に使うので、
//	  描かれると余計な矩形が見えてしまう(領域の決め方は GetTrackerBounds のコメント参照)。
//____________________________________________________________________________________
class KESCMSprite : public NoHandleSprite
{
public:
	KESCMSprite(IPMUnknown* boss) : NoHandleSprite(boss) {}
	virtual ~KESCMSprite() {}

protected:
	virtual void CreateTrackerPaths(IGraphicsContext* gc);
	virtual PMRect GetTrackerBounds(IGraphicsContext* gc, int32 flags);

	// ★★中心の「X」を描かせない(2026-07-26 実機。文字の左 20〜30px に出ていた青いゴミの正体)。
	//   NoHandleSprite は「**ハンドル**を描かない」だけで、**中心の X は描く**(NoHandleSprite.h:54-59
	//   "draws the X in the center of the sprite, but does not draw any of the shape's handles")。
	//   実体は CSprite::DrawCenterX = 「ドラッグ中、各ページアイテムの中心に小さな X を XOR で描く」
	//   (CSprite.h:447-451)。XOR の色はレイヤー色(SetHiliteColor 同 :349-352)で既定はライトブルー
	//   = 見えていた青と一致する。位置が文字の近くだったのは、CreateSprite に渡す開始点が原点
	//   (PMPoint())だから。CreateTrackerPaths を潰しても**別経路**なので消えなかった。
	//   → 経路は2本(Show から DrawSpriteHandles / CreateTrackerPaths から CreateHandlePaths)あるので
	//     両方とも空にする。HUD は文字だけを描くので、これで失うものは無い
	//     (NoHandleSprite::DrawSpriteHandles が描く Smart Dimension guides も不要)。
	virtual void CreateHandlePaths(IGraphicsContext* /*gc*/, bool16 /*bDoDirection*/) {}
	virtual void DrawSpriteHandles(IGraphicsContext* /*gc*/, int32 /*flags*/, PMMatrix* /*xForm*/) {}
};

CREATE_PMINTERFACE(KESCMSprite, kKESCMSpriteImpl)

/** sprite の更新領域(=描ける範囲)。既定は boss の IPathGeometry の制御点 bbox なので、その外に
	描いた文字はクリップされる(アドーンメントで枠外に描くときに GetPaintedAdornmentBounds を広げる
	のと同じ考え)。CSprite はこれと GetShapeDeviceBounds/GetHandleDeviceBounds の和集合を「view で
	クリップして」使う(CSprite.h:174-184)。

	★★2026-07-26 実測で判明: **この矩形は device(ウィンドウ)px 座標**。
	当初は pasteboard(content)座標だと思い込み、欲しい画面 px の余白を実ズーム D で割って足していた
	(-(200/sx), -(120/sy))。すると余白はズームを上げるほど画面上で縮み、
	  100% → 余白120px: 文字(基準点の 70〜99px 上)は収まる = 見える
	  200% → 余白 60px: 文字の上 20px 分が範囲外 = 上半分が切れる
	  300% → 余白 40px: 文字が丸ごと範囲外  = 消える
	という実機の症状に一致した(四角は基準点±20px なので常に残る)。content 座標なら余白は常に画面
	120px で、300% でも文字は収まるはず=見えるので、この一点で content 説は否定される。
	∴ 余白は **px のまま足す**。値は「文字が確実に収まる」大きさ(左右 400 / 上下 200)。ここは
	更新領域なので大きめでも見た目は変わらない(オフスクリーンが少し大きくなるだけ。CSprite が最後に
	view でクリップするので、ビューより大きくはならない)。

	※ただしヘッダーの記述とは食い違っている: NoHandleSprite::GetTrackerBounds は「boss の
	  IPathGeometry の **GetCtrlPointsBoundingBox** を返す」= content 座標、と書いてある
	  (NoHandleSprite.h:69-74)。content だとすれば余白は低ズームほど画面上で小さくなり、25% では
	  400pt≒100px しかない。上の実測(高ズームで切れた)は device 説で説明できるが、低ズーム側は
	  試していないので断定はしない。**どちらであっても足りるように、この余白には頼らず
	  BuildHudRegionPath が実測幅で領域を確保する**(そちらのコメント参照)。 */
PMRect KESCMSprite::GetTrackerBounds(IGraphicsContext* gc, int32 flags)
{
	PMRect r = NoHandleSprite::GetTrackerBounds(gc, flags);
	r.Inset(PMReal(-400.0), PMReal(-200.0));	// device px。割らない(上のコメント参照)
	return r;
}

void KESCMSprite::CreateTrackerPaths(IGraphicsContext* gc)
{
	// ★基底は呼ばない(上のクラスコメント参照。IPathGeometry は領域宣言専用で、描くと矩形が見える)。
	if (gc == nil || sHudText.IsEmpty())
		return;

	// gc → ViewPort boss → gPort。
	InterfacePtr<IGraphicsPort> gPort(gc->GetViewPort(), UseDefaultIID());
	if (gPort == nil)
		return;

	// ★カレントパスを残さない(2026-07-26。実機で「文字の左 20〜30px に青いゴミ」が出たことへの対策)。
	//   この関数は CSprite の描画サイクルから呼ばれるが、CSprite 側は「CreateTrackerPaths でパスを
	//   組ませ、DrawTrackerExtra がそれを XOR で描く」設計に見える(CSprite.h:256-261 "Draws the paths
	//   in xor mode" / :383-388 "walks/draws the path stored in the IPathGeometry")。XOR の色は
	//   レイヤー色(SetHiliteColor = CSprite.h:349-352)で、既定レイヤーは**ライトブルー**=ゴミの色と一致。
	//   ∴ 自前の描画がポートにカレントパスを残すと、後段がそれを青くストロークしうる。入口と各描画の
	//   前後で newpath() して、この関数はパスを持ち込まず・持ち帰らないようにする。
	gPort->newpath();

	// 描く基準点(pasteboard 座標)= 文字のベースライン左端。★トラッカーが決めた基準点をそのまま使う
	// (パスの bbox 中心は使わない。sHudAnchor の宣言部のコメント参照)。
	const PMReal cx = sHudAnchor.X();
	const PMReal cy = sHudAnchor.Y();

	// 実ズーム D(ピクセル指定を content サイズへ逆算するのに使う)。
	PMReal sx = 1.0, sy = 1.0;
	IControlView* view = gc->GetView();
	if (view != nil)
	{
		const PMMatrix toWindow = view->GetContentToWindowMatrix();
		sx = toWindow.GetXScale();
		sy = toWindow.GetYScale();
		if (sx < 0) sx = -sx;
		if (sy < 0) sy = -sy;
	}
	if (sx == 0 || sy == 0)
		return;

	// ★描き方 = **半透明の下地を敷いて、その上に普通の黒文字**(ユーザー指定 2026-07-26)。
	//   下の絵に依存しないので、太さも色も素直に決められる。
	//
	//   〈これ以前に試して捨てた方式(戻さないための記録)〉
	//   ・XOR 反転(IRasterPort::SetXORMode) … 反転はしたが**マウスを動かすと塗りつぶしになった**。
	//     XOR は「同じ場所に2回描くと戻る」性質で、CSprite の描画サイクルで CreateTrackerPaths が
	//     複数回呼ばれると反転が累積する(= color-inversion-drawing の「二重 XOR 問題」)。
	//   ・Difference ブレンド＋白フチ … gstate なので AutoGSave で確実に戻り、累積もしない。ただし
	//     中間グレー(50%)の上では効かず(128 → 127)、フチ頼みになる。**字を太らせようと塗りに線を
	//     重ねた時点で破綻**(反転・フチ・線が互いに干渉して字が潰れた。実機 2026-07-26)。
	{
		// ★フォントは「その文字を持っているもの」を選ぶ(既定フォント固定だと日本語等が化ける)。
		//   選定とキャッシュは KESCMQueryHudFont に集約。所有はキャッシュ側=ここでは Release しない。
		WideString label(sHudText);
		IPMFont* font = KESCMQueryHudFont(sHudText, label);
		if (font != nil)
		{
			// ★大きさ・余白は画面ピクセルで指定する(ズームを変えても見た目が変わらない)。値そのものは
			//   ファイル先頭の kKESCMHud*Px に置いてある(更新領域を宣言する BuildHudRegionPath と共有。
			//   片方だけ変えると領域が足りず文字が切れる)。
			const PMReal kTextPx     = PMReal(kKESCMHudTextPx);		// 文字の大きさ
			const PMReal kPadXPx     = PMReal(kKESCMHudPadXPx);		// 下地の左右余白
			const PMReal kPadTopPx   = PMReal(kKESCMHudPadTopPx);	// 下地の上余白
			const PMReal kPadBotPx   = PMReal(kKESCMHudPadBotPx);	// 下地の下余白
			// ★HUD 全体(下地＋文字)に掛ける不透明度(1.0=不透明)。下地だけでなく**文字も一緒に**薄くする
			//   (ユーザー指定 2026-07-26)。下地 0.75 ＋ 文字 1.0 の個別指定から、下の透明グループへ移した。
			const PMReal kHudOpacity = PMReal(kKESCMHudOpacity);

			const PMReal fontSize = kTextPx / sy;

			// show はベースライン左端を (x,y) に置く。基準点がそのままベースライン左端。
			const PMReal tx = cx;
			const PMReal ty = cy;

			// 下地の大きさは**実測**で決める(文字数×固定幅の概算は和文/欧文で外れる)。
			// 幅=MeasureWText、上下=GetAscent/GetDescent(IFontInstance.h:144/205/210)。
			// インスタンスが取れなかったときだけ概算へ落とす(下地が消えるよりはマシ)。
			PMReal textW   = 0.0;
			PMReal ascent  = fontSize * PMReal(0.8);
			PMReal descent = fontSize * PMReal(0.2);
			IFontInstance* inst = KESCMQueryHudFontInstance(font, fontSize);
			if (inst != nil)
			{
				inst->MeasureWText(label, textW);
				ascent  = inst->GetAscent();
				descent = inst->GetDescent();
			}
			if (textW <= 0)
				textW = fontSize * PMReal(0.6) * PMReal(label.CharCount());

			// 下地の矩形(= 透明グループの範囲でもある)。PMRect は (左, 上, 右, 下)。
			const PMRect hudRect(tx - kPadXPx / sx,
			                     ty - ascent - kPadTopPx / sy,
			                     tx + textW + kPadXPx / sx,
			                     ty + descent + kPadBotPx / sy);

			// ★下地と文字を**透明グループで1つに束ね**、合成の不透明度をグループに1回だけ掛ける。
			//   個別に setopacity すると、文字と下地が重なる画素だけ濃くなって「文字だけ濃い」状態に
			//   なる。starttransparencygroup は開始時点の GState(=直前の setopacity)を**グループ合成に**
			//   引き継ぎ、グループ内の alpha は 1.0 にリセットする(IGraphicsPort.h の仕様)。つまり
			//   中は不透明で描いてよく、全体が均一に薄くなる。作法は KESCMDrawEventHandler.cpp の
			//   旧ページ番号バッジ(白フチ＋本体を束ねる箇所)と同じ。cs=nil は非隔離グループ。
			AutoGSave ag(gPort);
			gPort->setopacity(kHudOpacity, kFalse);		// グループ全体の合成不透明度
			gPort->starttransparencygroup(hudRect, nil, kFalse /*non-isolated*/, kFalse /*no knockout*/);

			// (1) 下地: 白ベタ。rectfill は (左, 上, 幅, 高さ)。
			gPort->newpath();
			gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));
			gPort->rectfill(hudRect.Left(), hudRect.Top(), hudRect.Width(), hudRect.Height());
			gPort->newpath();

			// (2) 文字: 黒。ブレンドもフチも使わない(読みやすさは下地が担保する)。
			gPort->setrgbcolor(PMReal(0.0), PMReal(0.0), PMReal(0.0));
			gPort->selectfont(font, fontSize);
			gPort->show(tx, ty, label.NumUTF16TextChars(), label.GrabUTF16Buffer(nil),
				IGraphicsPort::kFillText);
			gPort->newpath();

			gPort->endtransparencygroup();
		}
	}

	// ★★「パネルと同じ描画経路」は sprite では使えない(2026-07-26 実機で確認・撤去済み)。
	//   試したこと = UI と同じ StringUtils::PMDrawStringRGB(IGraphicsContext* 版。DrawStringUtils.h:81-82)
	//   に、パネルの UI フォント(IInterfaceFonts::GetFont(kPaletteWindowFontId))を渡して描く。
	//   結果 = **1行も出なかった**(gPort の行だけ出る)。
	//   理由 = Windows 実装(WDrawString.cpp:47-97)は drawbot ではなく **GDI**。CreateFontIndirect →
	//   SelectObject(gc->GetSysPort()) → DrawTextEx で、**HDC が要る**。sprite に渡す
	//   NonMarkingAGMGraphicsContext はプラットフォームバッファを持たない(純正 CPathCreationTracker の
	//   コメントにも「IDVOffscreenPortData が無いので clip がプラットフォームバッファに入らない」)ため、
	//   GDI の描画先が無い。座標は gc->GetTransform() で変換される(WDrawString.cpp:69-70)ので座標系の
	//   問題ではない。
	//   ★従って「パネルが化けないのは GDI のフォントリンクのおかげ」であり、その恩恵は HDC のある UI 層
	//     でしか受けられない。sprite 側は上の KESCMQueryFontForText で**自前にフォントを選ぶ**しかない。
	//   ※同じ理由で、この経路のブレンド(反転)が効くかも確かめられていない(描画自体が起きないため)。

}

//____________________________________________________________________________________
//	The KESCM tool's tracker. Reveals the marks while the left button is held.
//____________________________________________________________________________________
class KESCMTracker : public CTracker
{
public:
	KESCMTracker(IPMUnknown* boss) : CTracker(boss), fCmykCursorFlip(kFalse),
		fHudAlive(kFalse), fHudShown(kFalse), fHudLastBaseline(PMReal(0.0), PMReal(0.0)),
		fHudLastSx(0.0), fHudLastSy(0.0)
	{
		fWantsToAutoScroll = kFalse;		// no autoscroll while holding (same as the animation sample)
	}
	virtual ~KESCMTracker()
	{
		// ★HUD の保険: EndTracking / AbortTracking のどちらも通らずに破棄される経路(アプリ終了時など)で、
		// one-shot タイマーのコールバックが死んだ this を触らないようにする。
		// 自分が対象のときだけ外す(別インスタンスが登録済みなら手を出さない)。
		if (sHudTracker == this)
		{
			if (sHudTimer != nil)
			{
				sHudTimer->StopTimer();
				sHudTimer->Release();
				sHudTimer = nil;
			}
			sHudTracker       = nil;
			sHudRedrawPending = kFalse;
			sHudRedrawCount   = 0;
		}
		// ※sprite(fHudAlive)はここでは片付けない。DestroySprite には IGraphicsContext が要り、それには
		//   fControlView を deref する必要があるが、この経路(基底の後始末を通らない破棄)では fControlView
		//   が既に無効なことがある。「まれなリークを避ける」ために「終了時に落ちる」危険は取らない。
		//   通常の解放は HideHud(EndTracking / AbortTracking)が必ず行う。
	}

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
		if (hideDuringSwitch)
			cursorMgr->Hide();

		bool16 result = CTracker::BeginTracking(theEvent);
		if (result)
		{
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

			// HUD(押している間だけビュー左上に1行)。ジェスチャに依らず出す=挙動を一定にする。
			// ★文字列はここで1回だけ決めて押している間は固定する(押している最中に窓は変わらない)。
			//   判定に使う「押した窓」は fControlView(このトラッカーが動いているビュー)。
			if (sHudOn)
			{
				sHudText = KESCMBuildHudText(fControlView);

				// ★押した瞬間から出すには one-shot タイマーが要る(実測 2026-07-26。以下は全部出なかった):
				//   ①BeginTracking で Show を1回 ②同 2回(初回=ShowFirstTime のオフスクリーン用意、
				//   2回目=ShowSprite の実描画、と CSprite.h のコメントから立てた仮説) ③BeginTracking から
				//   HandleContinueTracking を呼ぶ(押下直後は convertedPoint==fPreviousPoint なので
				//   mouseDidMove=kFalse で来る = CTracker.cpp:449-452)。
				//   → 全滅=「BeginTracking を抜けるまで画面が確定しない」。∴ 抜けた直後に一度だけ描かせる。
				sHudTracker     = this;
				sHudRedrawCount = 0;	// 暴走止めのカウンタは押下ごとにリセット
				if (sHudTimer == nil)
					sHudTimer = (ICallbackTimer*)::CreateObject(kCallbackTimerBoss, IID_ICALLBACKTIMER);
				if (sHudTimer != nil)
				{
					// 予約中の印を立ててから武装する(この間は KESCMTrackerRequestHudRedraw が重ねない)。
					sHudRedrawPending = kTrue;
					sHudTimer->StartTimer(KESCMHudTimerProc, 1, nil);
				}
			}
		}

		// Hide したら必ず対で Show する(result==kFalse の経路も含む。消えっぱなし防止)。
		// kKESCMCursorSettleMillis > 0 なら見せる前にその時間だけ待つ(既定 0=待たない。KESCMConstants.h の
		// 説明のとおり、包んでさえいれば待ちは不要。ゴミが再発したときの調整用に残してある)。
		if (hideDuringSwitch)
		{
			if (kKESCMCursorSettleMillis > 0)
				std::this_thread::sleep_for(std::chrono::milliseconds(kKESCMCursorSettleMillis));
			cursorMgr->Show();
		}

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

		// HUD は位置固定(ビュー左上)なのでマウス追従は不要だが、ドラッグ中に文書側の再描画が入ると
		// 消えるので描き直す。★one-shot タイマーの発火より先にドラッグが来ることもあるので、まだ一度も
		// 出していない間は動いていなくても描く(押下直後は mouseDidMove=kFalse で来る=CTracker.cpp:449-452)。
		if (mouseDidMove || !fHudShown)
			this->ShowHud();
	}

	/** Mouse up. Call the base first, then hide the marks. */
	virtual bool16 EndTracking(IEvent* theEvent)
	{
		this->HideHud();	// 基底より先に画面から取り除く
		bool16 result = CTracker::EndTracking(theEvent);
		KESCMTrackerRevealEnd();
		return result;
	}

	/** トラッキングが中断された(メニュー選択等)場合も reveal 状態を戻す(EndTracking と同じ後始末=
		hold 中に中断されても枠が出っぱなしにならないように)。 */
	virtual void AbortTracking(IEvent* theEvent)
	{
		this->HideHud();	// 中断でも出しっぱなしにしない
		CTracker::AbortTracking(theEvent);
		KESCMTrackerRevealEnd();
	}

private:
	bool16 fCmykCursorFlip;		// CMYK カーソルの CursorID 交互切替の現在側(kFalse=次は1021、kTrue=次は1022)

	// HUD(sprite)の状態。
	bool16 fHudAlive;	// CreateSprite 済み(=Hide/DestroySprite が要る)
	bool16 fHudShown;	// 一度でも Show した(=次の Show の前に Erase する)

	// 前回 Show したときのビュー状態(基準点=pasteboard 座標 と 実ズーム)。
	// ★スクロール/ズームを検出するために持つ: CSprite の Erase は「保存しておいた背景を描き戻す」
	//   ので、ビューが動いた後に呼ぶと画面へ**古い絵**を貼ってしまう。動いていたら Erase せず
	//   sprite ごと作り直す(ShowHud 参照)。
	PMPoint fHudLastBaseline;
	PMReal  fHudLastSx;
	PMReal  fHudLastSy;

	//----------------------------------------------------------------------------------------
	// HUD の「更新領域」を宣言するパスを組む。
	//
	// ★このパスは**描かれない**(KESCMSprite::CreateTrackerPaths は基底を呼ばない)。CSprite は
	//   GetTrackerBounds = boss の IPathGeometry の制御点 bbox を更新領域の元にするので、そこへ
	//   「文字が収まる矩形」を1つ置いて領域だけを申告する。実際の余白は GetTrackerBounds が
	//   さらに足す(そちらのコメント参照。ただし座標系に不確かさがあるので、その上乗せには頼らない)。
	// パスは pasteboard 座標で組む(純正 CPathCreationTracker と同じ。"hand make sprite path in pb coords")。
	// textWidth = 実測した文字列の描画幅(content 単位。KESCMMeasureHudText の戻り)。
	//----------------------------------------------------------------------------------------
	void BuildHudRegionPath(const PMPoint& pbBaseline, const PMReal& textWidth)
	{
		InterfacePtr<IPathGeometry> geom(this, IID_IPATHGEOMETRY);
		if (geom == nil || fControlView == nil)
			return;

		// 実ズーム D(表示倍率)。負スケール(見開きの左右反転等)もあり得るので絶対値で扱う。
		const PMMatrix toWindow = fControlView->GetContentToWindowMatrix();
		PMReal sx = toWindow.GetXScale();
		PMReal sy = toWindow.GetYScale();
		if (sx < 0) sx = -sx;
		if (sy < 0) sy = -sy;
		if (sx == 0 || sy == 0)
			return;

		// 基準点 = 文字のベースライン左端。★右端は**実測した文字幅**から決める(2026-07-26 修正)。
		// 以前は固定 600px 相当で、それを超える長さの文書名だと領域が足りず右側が切れた。
		// 上下は文字サイズから(ベースラインの上に ascent+余白、下に descent+余白が入る大きさ)。
		const PMReal marginX = PMReal(kKESCMHudPadXPx + 4.0) / sx;
		const PMReal left   = pbBaseline.X() - marginX;
		const PMReal right  = pbBaseline.X() + textWidth + marginX;
		const PMReal top    = pbBaseline.Y() - (PMReal(kKESCMHudTextPx * 1.5) / sy);
		const PMReal bottom = pbBaseline.Y() + (PMReal(kKESCMHudTextPx * 0.8) / sy);

		geom->RemoveAllPaths();
		const int32 p = geom->AddNewPath();
		geom->AddPoint(p, PMPathPoint(PMPoint(left,  top)));
		geom->AddPoint(p, PMPathPoint(PMPoint(right, top)));
		geom->AddPoint(p, PMPathPoint(PMPoint(right, bottom)));
		geom->AddPoint(p, PMPathPoint(PMPoint(left,  bottom)));
		geom->ClosePath(p);
	}

public:
	/** HUD を(必要なら sprite を作ってから)描き直す。2回目以降は Show の前に Erase する
		(純正 CPathCreationTracker::HandleMove と同じ順序)。one-shot タイマーからも呼ぶ。

		位置 = **レイアウトビューの左上**。基準点は `IPanorama::GetContentLocationAtFrameOrigin()`
		(= いまビュー左上に来ている content 点。IPanorama.h:199-204)＋画面 px のインセット。
		★`IPanorama::GetBounds()` は使わない。あれは可視範囲ではなく「パノラマが抱えるコンテンツ全域
		  (スクロールできる範囲全部)」の矩形(同 :75-84)で、その左上は画面のはるか外。実際 2026-07-26 に
		  これで「描いてはいるが見えない」状態になった(エラーも警告も出ないので原因が見えにくい)。
		★ウィンドウ座標の定点を content へ落とす方式も使わない。レイアウトビュー widget がルーラーや
		  タブの分だけ下にずれていると、ビューの外に置いてしまう(保険としてのみ残す)。
		※スクロール/ズームで可視範囲は動くが、再計算は Show のたびなので押下中も隅に留まる。 */
	void ShowHud()
	{
		if (!sHudOn || fControlView == nil || sHudText.IsEmpty())
			return;
		if (sHudDrawing)
			return;		// 再入(この描画自体が起こした再描画から呼ばれた)
		InterfacePtr<ISprite> sprite(this, IID_ISPRITE);
		if (sprite == nil)
			return;
		KESCMHudDrawGuard drawGuard;

		PMReal sx = 1.0, sy = 1.0;
		{
			const PMMatrix toWindow = fControlView->GetContentToWindowMatrix();
			sx = toWindow.GetXScale();	if (sx < 0) sx = -sx;
			sy = toWindow.GetYScale();	if (sy < 0) sy = -sy;
		}
		if (sx == 0 || sy == 0)
			return;

		// ビュー左上からの位置(画面 px)。y は文字のベースライン。
		const PMReal kHudLeftPx     = 20.0;
		const PMReal kHudBaselinePx = 40.0;

		PMPoint pbBaseline;
		InterfacePtr<IPanorama> pano(KESCMQueryPanorama(fControlView));
		if (pano != nil)
		{
			const PMPoint topLeft = pano->GetContentLocationAtFrameOrigin();	// ビュー左上に来ている content 点
			pbBaseline = PMPoint(topLeft.X() + kHudLeftPx / sx, topLeft.Y() + kHudBaselinePx / sy);
		}
		else
		{
			// 保険: パノラマが取れないビュー。ウィンドウ座標の定点を content 座標へ落とす。
			pbBaseline = PMPoint(kHudLeftPx, kHudBaselinePx);
			fControlView->GetWindowToContentMatrix().Transform(&pbBaseline);
		}

		// 文字幅を実測する(更新領域の右端に使う)。測れなかったときだけ固定値の保険へ落ちる。
		PMReal textWidth = PMReal(kKESCMHudFallbackWidthPx) / sx;
		{
			WideString label(sHudText);
			PMReal measured = 0.0;
			if (KESCMMeasureHudText(sHudText, label, PMReal(kKESCMHudTextPx) / sy, measured))
				textWidth = measured;
		}

		NonMarkingAGMGraphicsContext gc(fControlView);

		// ★ビューが動いた(スクロール/ズーム)なら sprite を作り直す。CSprite の Erase は「保存して
		//   おいた背景を描き戻す」ので、動いた後に呼ぶと画面へ**古い絵**を貼ってしまう。ここへ来ている
		//   のは文書側の再描画の後(= 画面はもう描き直されている)なので、そもそも消す作業が要らない。
		if (fHudAlive &&
		    (pbBaseline.X() != fHudLastBaseline.X() || pbBaseline.Y() != fHudLastBaseline.Y() ||
		     sx != fHudLastSx || sy != fHudLastSy))
		{
			sprite->DestroySprite(&gc);
			fHudAlive = kFalse;
			fHudShown = kFalse;
		}

		if (!fHudAlive)
		{
			// itemList = nil = 「ページアイテムではなく自前パスを描く」(パス作成トラッカーと同じ使い方)。
			// ★戻り値 kFalse = オフスクリーンを作れなかった(低メモリ)。その経路の CSprite は XOR で描くので、
			//   下地の塗りが重なるほど累積して読めなくなる(反転方式を捨てたのと同じ理由)。HUD は諦める。
			if (!sprite->CreateSprite(&gc, nil, PMPoint(), kFalse /* don't draw item list */))
			{
				sprite->DestroySprite(&gc);
				return;
			}
			fHudAlive = kTrue;
			fHudShown = kFalse;
		}
		if (fHudShown)
			sprite->Erase(&gc, PMPoint(), IShape::kDrawCreateDynamic);

		// ★基準点を更新する位置は「Erase の後・Show の前」。Erase は前回の絵を消す描画なので、
		// 先に書き換えると消す位置がずれる(CreateTrackerPaths は Erase でも呼ばれる)。
		sHudAnchor = pbBaseline;
		this->BuildHudRegionPath(pbBaseline, textWidth);
		sprite->Show(&gc, PMPoint(), IShape::kDrawCreateDynamic);
		fHudShown        = kTrue;
		fHudLastBaseline = pbBaseline;
		fHudLastSx       = sx;
		fHudLastSy       = sy;
	}

private:
	/** 押下解除/中断で HUD を片付ける。CreateSprite していなければ何もしない(二重解放なし)。 */
	void HideHud()
	{
		// ★one-shot タイマーが残っていたら必ず止めて返す(押下の外にコールバックを残さない)。
		if (sHudTimer != nil)
		{
			sHudTimer->StopTimer();
			sHudTimer->Release();
			sHudTimer = nil;
		}
		sHudTracker       = nil;
		sHudRedrawPending = kFalse;
		sHudRedrawCount   = 0;

		if (fHudAlive && fControlView != nil)
		{
			InterfacePtr<ISprite> sprite(this, IID_ISPRITE);
			if (sprite != nil)
			{
				NonMarkingAGMGraphicsContext gc(fControlView);
				sprite->Hide(&gc, kFalse);
				sprite->DestroySprite(&gc);
			}
		}
		fHudAlive  = kFalse;
		fHudShown  = kFalse;
		fHudLastSx = 0.0;			// 次の押下で「ビューが動いた」判定に古い値を持ち込まない
		fHudLastSy = 0.0;
		sHudText.Clear();
		KESCMReleaseHudFont();		// ★押下中だけ持つフォント選定キャッシュを返す
	}

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

// 押下直後(と、押下中に文書が再描画されたあと)の one-shot 発火。一度だけ呼ばれ、HUD を描き直す。
static uint32 KESCMHudTimerProc(void* /*refPtr*/)
{
	sHudRedrawPending = kFalse;		// 予約は消化した(次の要求から再び武装できる)

	// ★ここでは Release しない。RunTask の実行中に自分を解放すると、参照が 0 になって破棄された
	// オブジェクトのまま戻り値を返すことになる(自己破棄しながら実行継続)。しかも先に nil にすると
	// 暴走したときに StopTimer で止める手が無くなる。解放は押下解除(HideHud)の1箇所に集約する。
	if (sHudTracker != nil)
		sHudTracker->ShowHud();

	// ★★戻り値は IIdleTask::RunTask の再スケジュール値。**0 は「すぐまた呼べ」**であって
	// 「終わり」ではない(SDK のサンプル: `return fThingsLeftToDo ? 0 : kEndOfTime;`)。
	// 一度 0 を返したせいで無限に呼ばれ、毎回 CreateSprite+Show が走って InDesign が固まった
	// (2026-07-26 実機)。しかもコールバックの先頭で Release+nil にしているので外から止められない。
	// one-shot は必ず kEndOfTime を返すこと。
	return IIdleTask::kEndOfTime;
}

void KESCMTrackerRequestHudRedraw()
{
	// ★なぜ要るか(2026-07-26 追加): HUD を描く sprite の絵は、文書側が再描画されると消える。押下中の
	//   描画契機は「押下直後の one-shot」と「ContinueTracking(マウスが動いたときだけ)」の2つしかなく
	//   (WantTimer=kFalse ゆえ定期発火も無い)、押した直後にマーク表示のための再描画が入ると、
	//   マウスを動かさない限り HUD が出ないままになる。スクロール/ズームも同様に取り残される。
	//   → 文書の描画イベント(KESCMDrawEventHandler)からここを叩き、次のアイドルで一度だけ描き直す。
	// 直接 ShowHud を呼ばないのは、描画イベントの最中に sprite を描かせない(再入させない)ため。
	if (!sHudOn || sHudDrawing || sHudRedrawPending)
		return;
	if (sHudTracker == nil || sHudTimer == nil)
		return;		// 押していない(押下中だけ sHudTracker/sHudTimer が生きている)
	if (sHudRedrawCount >= kKESCMHudMaxRedrawsPerPress)
		return;		// 暴走止め(上限に達したらこの押下では追従をやめる)
	++sHudRedrawCount;
	sHudRedrawPending = kTrue;
	sHudTimer->StartTimer(KESCMHudTimerProc, 1, nil);
}

// HUD の ON/OFF(パネルのフライアウト「Show HUD」)。状態はここ1箇所に持ち、パネル設定として保存される。
bool16 KESCMGetHudEnabled()          { return sHudOn; }
void   KESCMSetHudEnabled(bool16 on) { sHudOn = on; }

void KESCMTrackerShutdownHud()
{
	// プラグイン終了時の保険。ICallbackTimer のコールバックは参照カウントされない生関数ポインタで、
	// 予約が残ったままこの .pln が降りると、発火時に消えた関数へ飛んでクラッシュする。押下中に終了する
	// ことは実質ないが、確実に止める(ポインタは deref せず、停止と解放だけ=終了処理中でも安全)。
	if (sHudTimer != nil)
	{
		sHudTimer->StopTimer();
		sHudTimer->Release();
		sHudTimer = nil;
	}
	sHudTracker       = nil;
	sHudRedrawPending = kFalse;
	sHudRedrawCount   = 0;
	sHudText.Clear();
	KESCMReleaseHudFont();	// フォント参照を .pln が降りる前に必ず返す
}

// End, KESCMTracker.cpp.
