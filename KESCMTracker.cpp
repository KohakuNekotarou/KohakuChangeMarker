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

// ★★実験プローブ(一時・2026-07-26)用。トラッカー自身の描画層(CSprite)の検証。撤去時に全部外す。
#include "ISprite.h"						// CreateSprite / Show / Erase / Hide / DestroySprite
#include "NoHandleSprite.h"					// 自前 sprite の基底(= kNoHandleSpriteImpl の実クラス。CSprite 派生で
											// WIDGET_DECL export 済み。実装は WidgetBin.lib 内で .cpp は非公開)
#include "IPathGeometry.h"					// sprite が描くパスの置き場(boss に同居)
#include "PMPathPoint.h"					// AddPoint に渡す点
#include "NonMarkingAGMGraphicsContext.h"	// sprite に渡す gc(純正のトラッカーもこれを使う)
#include "IShape.h"							// kDrawCreateDynamic(Show のフラグ)
#include "IControlView.h"					// GetContentToWindowMatrix(実ズーム D)
#include "IPanorama.h"						// GetBounds(可視範囲=HUD を置く隅を決める。content 座標)
#include "ILayoutUIUtils.h"					// GetMousePasteboardPosition(押下点)
#include "IEventUtils.h"					// GetGlobalMouseLocation
#include "IGraphicsContext.h"				// GetViewPort / GetView(gc からポートを取る)
#include "IGraphicsPort.h"					// selectfont / show / fill(自前描画)
#include "IRasterPort.h"					// CreateAnchorPointPath に渡す(取れるかどうかも検証点)
#include "IGraphicsUtils.h"					// CreateAnchorPointPath(本命の検証対象)
#include "IFontMgr.h"						// フォント取得(QueryFont)とインストール済みフォントの列挙
#include "IPMFont.h"						// GetGlyphID / GetNotDefinedGlyph(その字を持っているかの判定)
#include "IFontGroup.h"						// GetNumFonts(フォント走査の内側)
#include "LocaleSetting.h"					// GetSystemScript(ユーザーの言語からフォントを引く)
#include "AutoGSave.h"
#include "WideString.h"						// show に渡す UTF16
#include "IDocument.h"						// GetName(HUD に出す Source 文書名)
#include "IDocumentList.h"					// FindDocByDataBase(db → 文書)
#include "KESCMDrawEventHandler.h"			// sSrcDB(比較の旧側 = Source 文書の db)
#include "ICallbackTimer.h"					// 押下直後に一発だけ描くための one-shot タイマー
#include "CreateObject.h"					// ::CreateObject(kCallbackTimerBoss, ...)
#include "ShuksanID.h"						// kCallbackTimerBoss / IID_ICALLBACKTIMER
#include "Utils.h"
#include "PMMatrix.h"

// ★★実験プローブ(一時・2026-07-26): 押下直後に sprite を出すための one-shot タイマー。
// BeginTracking の中で(基底の後に)Show しても絵が出ないため、BeginTracking を完全に抜けた直後に
// 一度だけ描かせる。押している間だけ生き、EndTracking/AbortTracking で必ず止める
// (コールバックは参照カウントされない生関数ポインタで、残したままプラグインが降りるとクラッシュする)。
class KESCMTracker;
static ICallbackTimer* sSpriteProbeTimer   = nil;
static KESCMTracker*   sSpriteProbeTracker = nil;
static uint32 KESCMSpriteProbeTimerProc(void* refPtr);

// ★★実験プローブ(一時・2026-07-26): 文字を描く基準点(pasteboard 座標)。ShowSpriteProbe が Show の
// たびに更新し、KESCMSprite::CreateTrackerPaths が参照する。
// ★**パスの bbox 中心を基準にしてはいけない**(2026-07-26 実機): 対照用の右の四角だけ content 固定
// サイズ(12pt)なので、ズームを上げると bbox が右へ伸び、その中心=文字位置が右へずれる
// (100%→300% で約 6px)。上下は四角が基準点に対称なのでズレない、も実機の見え方と一致した。
// 本番の HUD では四角自体を撤去するので、そのときも文字位置が変わらないこの形が正しい。
static PMPoint sSpriteProbeAnchor;

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

/** ★★実験プローブ(一時・2026-07-26): HUD に出す文字列 = 比較の Source(旧)文書名。
	解決経路は KESCMChangedPagesTSV.cpp の DocNameFromDB と同じ(セッション→app→docList→
	FindDocByDataBase。IDocument は no ref で名前を取るだけ)。
	★まだ Start していない(sSrcDB==nil)ときは "(no source)" を返す。空文字にすると、実機で
	「HUD 自体が出ていない」のか「名前が空なだけ」なのか区別できず、位置の検証にならないため。 */
static PMString KESCMSpriteProbeLabel()
{
	PMString out;
	out.SetTranslatable(kFalse);

	IDataBase* db = KESCMDrawEventHandler::sSrcDB;
	if (db != nil)
	{
		ISession* session = GetExecutionContextSession();
		InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
		InterfacePtr<IDocumentList> docList(app != nil ? app->QueryDocumentList() : nil);
		IDocument* doc = (docList != nil) ? docList->FindDocByDataBase(db) : nil;	// no ref(deref せず名前だけ)
		if (doc != nil)
			doc->GetName(out);
	}
	if (out.IsEmpty())
		out = PMString("(no source)");
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
	return nil;
}

// 選定結果のキャッシュ(選定は文字列が変わったときだけ。押下のたびに総当たりしない)。所有する。
static IPMFont* sHudFont = nil;
static PMString sHudFontLabel;

/** labelStr を描けるフォントを返す(キャッシュ付き)。所有はここ側=呼び出し側は Release しない。 */
static IPMFont* KESCMQueryHudFont(const PMString& labelStr, const WideString& label)
{
	if (sHudFont != nil && sHudFontLabel == labelStr)
		return sHudFont;

	if (sHudFont != nil)
	{
		sHudFont->Release();
		sHudFont = nil;
	}
	sHudFont      = KESCMQueryFontForText(label);
	sHudFontLabel = labelStr;
	return sHudFont;
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
}

//____________________________________________________________________________________
//	★★実験プローブ(一時・2026-07-26): トラッカーの描画層(sprite)で自前に描く。
//
//	これまでの実測: Draw Event 経路は、トラッカーで押している間も settled キャッシュ層のままで、
//	rPort のスケールが表示ズーム D と一致しない(= ピクセル指定の CreateAnchorPointPath が拡大する)。
//	では純正のトラッカーが使う描画機構 = sprite ならどうか、を測る。
//
//	基底 = NoHandleSprite(.fr で使っていた kNoHandleSpriteImpl の実クラス。CSprite 派生で
//	WIDGET_DECL export 済み=継承できる。CSprite.cpp / NoHandleSprite.cpp は非公開だが、
//	CreateTrackerPaths が protected virtual なので、既定の「boss の IPathGeometry のパスを描く」
//	動作を残したまま描く中身を足せる)。背景の保存・復元(消去)は基底がやるので自前では書かない。
//
//	ここで2つ試す:
//	  ①文字(selectfont + show) — 出るなら CMYK 数値をカーソルビットマップでなくカンバスに描ける。
//	  ②CreateAnchorPointPath   — この層で rPort が取れるか、取れたとして固定サイズになるか(B=D か)。
//____________________________________________________________________________________
class KESCMSprite : public NoHandleSprite
{
public:
	KESCMSprite(IPMUnknown* boss) : NoHandleSprite(boss) {}
	virtual ~KESCMSprite() {}

protected:
	virtual void CreateTrackerPaths(IGraphicsContext* gc);
	virtual PMRect GetTrackerBounds(IGraphicsContext* gc, int32 flags);
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
	更新領域なので大きめでも見た目は変わらない(オフスクリーンが少し大きくなるだけ)。 */
PMRect KESCMSprite::GetTrackerBounds(IGraphicsContext* gc, int32 flags)
{
	PMRect r = NoHandleSprite::GetTrackerBounds(gc, flags);
	r.Inset(PMReal(-400.0), PMReal(-200.0));	// device px。割らない(上のコメント参照)
	return r;
}

void KESCMSprite::CreateTrackerPaths(IGraphicsContext* gc)
{
	// まず既定の動作(boss の IPathGeometry のパスを描く)。四角2つはこれで出る。
	NoHandleSprite::CreateTrackerPaths(gc);

	if (gc == nil)
		return;

	// gc → ViewPort boss → 各ポート。gPort は必ず取れる。rPort は「画面なら取れる」ので、
	// 取れるかどうか自体が観測点(取れなければ CreateAnchorPointPath はこの層では使えない)。
	InterfacePtr<IGraphicsPort> gPort(gc->GetViewPort(), UseDefaultIID());
	if (gPort == nil)
		return;

	// 描く基準点(pasteboard 座標)。★トラッカーが記録した基準点そのものを使う。パスの bbox 中心は
	// 使わない: 対照用の四角が content 固定サイズなので、bbox 中心はズームで右へ動く
	// (sSpriteProbeAnchor の宣言部のコメント参照。2026-07-26 実機で確認)。
	const PMReal cx = sSpriteProbeAnchor.X();
	const PMReal cy = sSpriteProbeAnchor.Y();

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

	// ①文字。下地を敷かず、色を反転させて描く(ユーザー指定 2026-07-26)。
	//   ★反転の実装を XOR から Difference ブレンドへ変更(2026-07-26 実測):
	//     IRasterPort::SetXORMode(kTrue) は反転自体は成功したが、**マウスを動かすと塗りつぶしに
	//     なった**。XOR は「同じ場所に2回描くと元に戻る」性質で、CSprite の描画サイクル
	//     (背景オフスクリーンの用意と前景描画)で CreateTrackerPaths が複数回呼ばれると反転が
	//     累積するため(= color-inversion-drawing の「二重 XOR 問題」)。
	//   → ブレンドモード Difference は **gstate なので AutoGSave で確実に戻り**、モードの
	//     付け外しがポート全体に残らない。白(1,1,1)で塗ると完全反転(結果 = 1 − 下地)。
	//   ★弱点は XOR と同じ: 中間グレー(50%)の上では効かない(128 → 127 でほぼ同色)。
	{
		// ★フォントは「その文字を持っているもの」を選ぶ(既定フォント固定だと日本語等が化ける)。
		//   選定とキャッシュは KESCMQueryHudFont に集約。所有はキャッシュ側=ここでは Release しない。
		const PMString labelStr = KESCMSpriteProbeLabel();
		WideString label(labelStr);
		IPMFont* font = KESCMQueryHudFont(labelStr, label);
		if (font != nil)
		{
			// ★大きさ・太さは画面ピクセルで指定する(ズームを変えても見た目が変わらない)。
			// 本実装で CMYK 数値を描くときも、この2つを変えるだけで調整できる。
			const PMReal kTextPx    = 36.0;
			const PMReal kOutlinePx = 4.0;	// 白縁の太さ

			// 出す中身は比較の Source(旧)文書名。未 Start なら "(no source)"(label は上で作成済み)。
			// show はベースライン左端を (x,y) に置く。文字が大きくなった分、左と下へ余裕を取る。
			const PMReal tx = cx - (PMReal(100.0) / sx);
			const PMReal ty = cy - (PMReal(70.0)  / sy);

			// (1) 白縁: 通常描画(反転しない)で、文字のアウトラインを太くストロークする。
			//     ★中間グレーの上では Difference が効かない(128 → 127)ため、本体だけだと消える。
			//     縁を「下地に依らない不透明の白」で描いておくと、グレー地では白い輪郭の中抜き文字
			//     として読める(白地では縁が見えず本体の黒が、黒地では縁も本体も白く出る)。
			//     縁は show の kStrokeText で描ける(オフセットを変えて何度も描く必要はない)。
			{
				AutoGSave ag(gPort);
				gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));
				gPort->setlinewidth(kOutlinePx / sy);
				gPort->selectfont(font, kTextPx / sy);
				gPort->show(tx, ty, label.NumUTF16TextChars(), label.GrabUTF16Buffer(nil),
					IGraphicsPort::kStrokeText);
			}

			// (2) 本体: Difference で下地を反転(白で塗ると 結果 = 1 − 下地)。
			{
				AutoGSave ag(gPort);
				gPort->setblendingmode(kPMBlendDifference);
				gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));
				gPort->selectfont(font, kTextPx / sy);
				gPort->show(tx, ty, label.NumUTF16TextChars(), label.GrabUTF16Buffer(nil),
					IGraphicsPort::kFillText);
			}
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

	// ②純正のピクセル指定マーカー。rPort が取れたときだけ。ズームを変えて大きさが変わらなければ、
	//   この層は B=D(ライブ)ということ。
	{
		InterfacePtr<IRasterPort> rPort(gc->GetViewPort(), UseDefaultIID());
		if (rPort != nil)
		{
			AutoGSave ag(gPort);
			gPort->newpath();
			Utils<IGraphicsUtils>()->CreateAnchorPointPath(rPort, gPort,
				PMPoint(cx, cy + (PMReal(60.0) / sy)), PMReal(20.0), kFalse);
			gPort->fill();
		}
	}
}

//____________________________________________________________________________________
//	The KESCM tool's tracker. Reveals the marks while the left button is held.
//____________________________________________________________________________________
class KESCMTracker : public CTracker
{
public:
	KESCMTracker(IPMUnknown* boss) : CTracker(boss), fCmykCursorFlip(kFalse),
		fSpriteProbeAlive(kFalse), fSpriteProbeShown(kFalse)	// ★実験プローブ(一時)
	{
		fWantsToAutoScroll = kFalse;		// no autoscroll while holding (same as the animation sample)
	}
	virtual ~KESCMTracker()
	{
		// ★sprite プローブの保険: EndTracking / AbortTracking のどちらも通らずに破棄される経路
		// (アプリ終了時など)で、one-shot タイマーのコールバックが死んだ this を触らないようにする。
		// 自分が対象のときだけ外す(別インスタンスが登録済みなら手を出さない)。
		if (sSpriteProbeTracker == this)
		{
			if (sSpriteProbeTimer != nil)
			{
				sSpriteProbeTimer->StopTimer();
				sSpriteProbeTimer->Release();
				sSpriteProbeTimer = nil;
			}
			sSpriteProbeTracker = nil;
		}
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

			// ★★実験プローブ(一時・2026-07-26): トラッカー自身の描画層(CSprite)を押下点に出す。
			// 修飾キーに依らず出す。撤去時はこのブロックと ContinueTracking / EndTracking /
			// AbortTracking の対になる呼び出し、およびヘルパー一式を消す。
			//
			// ★押した瞬間から出す(実測 2026-07-26 の経過。どれも出なかった):
			//   ①BeginTracking で ShowSpriteProbe を1回
			//   ②同 2回(初回=ShowFirstTime のオフスクリーン用意、2回目=ShowSprite の実描画、と
			//     CSprite.h のコメントから立てた仮説)
			//   ③BeginTracking から HandleContinueTracking を呼ぶ(+ ContinueTracking 側で
			//     mouseDidMove=kFalse を通すよう修正。押下直後は convertedPoint==fPreviousPoint
			//     なので必ず kFalse で来る = CTracker.cpp:449-452)
			//   → ①〜③が全滅ということは「BeginTracking を抜けるまで画面が確定しない」のが原因。
			//     ∴ 抜けた直後に one-shot タイマーで一度だけ描かせる。
			sSpriteProbeTracker = this;
			if (sSpriteProbeTimer == nil)
				sSpriteProbeTimer = (ICallbackTimer*)::CreateObject(kCallbackTimerBoss, IID_ICALLBACKTIMER);
			if (sSpriteProbeTimer != nil)
				sSpriteProbeTimer->StartTimer(KESCMSpriteProbeTimerProc, 1, nil);
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

		// ★★実験プローブ(一時・2026-07-26): sprite をマウスに追従させる(Erase→組み直し→Show)。
		// ★押下直後は mouseDidMove=kFalse で来る: CTracker::HandleContinueTracking は
		// 「convertedPoint != fPreviousPoint」でしか mouseDidMove を立てず(CTracker.cpp:449-452)、
		// 押した瞬間は当然その2点が同じだから。BeginTracking から自分で HandleContinueTracking を
		// 呼んでも、この条件で弾かれて描画に届かない。→ まだ一度も出していない間は、動いていなくても描く。
		if (mouseDidMove || !fSpriteProbeShown)
			this->ShowSpriteProbe(where);
	}

	/** Mouse up. Call the base first, then hide the marks. */
	virtual bool16 EndTracking(IEvent* theEvent)
	{
		this->HideSpriteProbe();	// ★実験プローブ(一時): 基底より先に画面から取り除く
		bool16 result = CTracker::EndTracking(theEvent);
		KESCMTrackerRevealEnd();
		return result;
	}

	/** トラッキングが中断された(メニュー選択等)場合も reveal 状態を戻す(EndTracking と同じ後始末=
		hold 中に中断されても枠が出っぱなしにならないように)。 */
	virtual void AbortTracking(IEvent* theEvent)
	{
		this->HideSpriteProbe();	// ★実験プローブ(一時): 中断でも出しっぱなしにしない
		CTracker::AbortTracking(theEvent);
		KESCMTrackerRevealEnd();
	}

private:
	bool16 fCmykCursorFlip;		// CMYK カーソルの CursorID 交互切替の現在側(kFalse=次は1021、kTrue=次は1022)

	// ★★実験プローブ(一時・2026-07-26)。トラッカー自身の描画層(CSprite)の状態。撤去時に消す。
	bool16 fSpriteProbeAlive;	// CreateSprite 済み(=Hide/DestroySprite が要る)
	bool16 fSpriteProbeShown;	// 一度でも Show した(=次の Show の前に Erase する)

	//----------------------------------------------------------------------------------------
	// ★★実験プローブ(一時・2026-07-26): トラッカー自身の描画層 = CSprite の振る舞いを見る。
	//
	// Draw Event 経路は、押している間も settled キャッシュ層のままだと実測で判った(rPort のスケールが
	// 表示ズーム D ではなくオフスクリーン基準 B なので、ピクセル指定の描画が拡大する)。では純正の
	// トラッカーが使う描画機構=CSprite はどうなのか、を測る。
	//
	// 押している間、マウス位置に四角を2つ並べて描く:
	//   ・左 = 実ズーム D で 40px/D に逆算した四角  → D が効く層なら、ズームを変えても常に 40px
	//   ・右 = content 座標で固定サイズ(12pt)の四角 → ズームで必ず大きさが変わる(対照)
	// 右が変わり左が変わらなければ「CSprite 層では D 補正が正しく効く」。両方変わるなら別の層にいる。
	//
	// CSprite は XOR ワイヤー(線)で描くので色は指定できない。見るのは大きさだけ。パスは pasteboard
	// 座標で組む(純正 CPathCreationTracker と同じ。"hand make sprite path in pb coords")。
	//----------------------------------------------------------------------------------------
	void BuildSpriteProbePath(const PMPoint& pbWhere)
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

		const PMReal kProbePx = 40.0;	// 左の四角の狙い(画面ピクセル)
		const PMReal kFixedPt = 12.0;	// 右の四角(content 固定=対照)
		const PMReal halfAW = (kProbePx / sx) / PMReal(2.0);
		const PMReal halfAH = (kProbePx / sy) / PMReal(2.0);
		const PMReal halfB  = kFixedPt / PMReal(2.0);
		const PMReal gap    = (kProbePx * PMReal(1.5)) / sx;	// 2つの間隔も px 基準(常に隣り合って見える)

		geom->RemoveAllPaths();

		// 左: D 補正の四角(検証対象)
		{
			const PMReal cx = pbWhere.X() - gap, cy = pbWhere.Y();
			const int32 p = geom->AddNewPath();
			geom->AddPoint(p, PMPathPoint(PMPoint(cx - halfAW, cy - halfAH)));
			geom->AddPoint(p, PMPathPoint(PMPoint(cx + halfAW, cy - halfAH)));
			geom->AddPoint(p, PMPathPoint(PMPoint(cx + halfAW, cy + halfAH)));
			geom->AddPoint(p, PMPathPoint(PMPoint(cx - halfAW, cy + halfAH)));
			geom->ClosePath(p);
		}
		// 右: content 固定の四角(対照。ズームで必ず変わる)
		{
			const PMReal cx = pbWhere.X() + gap, cy = pbWhere.Y();
			const int32 p = geom->AddNewPath();
			geom->AddPoint(p, PMPathPoint(PMPoint(cx - halfB, cy - halfB)));
			geom->AddPoint(p, PMPathPoint(PMPoint(cx + halfB, cy - halfB)));
			geom->AddPoint(p, PMPathPoint(PMPoint(cx + halfB, cy + halfB)));
			geom->AddPoint(p, PMPathPoint(PMPoint(cx - halfB, cy + halfB)));
			geom->ClosePath(p);
		}
	}

	/** sprite を(必要なら作ってから)現在位置で描き直す。2回目以降は Show の前に Erase する
		(純正 CPathCreationTracker::HandleMove と同じ順序)。 */
	void ShowSpriteProbe(const PMPoint& /*pbWhere*/)
	{
		if (fControlView == nil)
			return;
		InterfacePtr<ISprite> sprite(this, IID_ISPRITE);
		if (sprite == nil)
			return;

		// ★★HUD 実験(2026-07-26): マウス位置ではなく「レイアウトビューの可視範囲の左上」に固定して描く。
		// sprite の clip はペーストボードではなく**ビュー全体(ウィンドウ座標)**なので、可視範囲の隅=
		// ペーストボードの外側にも描けるはず、というのがここでの観測点(Draw Event の描画はペースト
		// ボードにクリップされ、窓の隅には出せなかった=layout-screen-overlay)。
		// 可視範囲の左上は IPanorama::GetContentLocationAtFrameOrigin()(content 座標)で取る。
		// ★★2026-07-26 実機で「出ない」→原因: 最初 GetBounds() を使ったが、あれは可視範囲ではなく
		//   「パノラマが抱えるコンテンツ全域(=スクロールできる範囲全部)」の矩形(IPanorama.h:75-84)。
		//   その左上は画面のはるか外なので、描いてはいたが見えない場所に描いていた。
		//   ビューの左上に来ている content 座標を返すのは GetContentLocationAtFrameOrigin(同 199-204)。
		// ウィンドウ座標の定点を落とす方式は、
		// レイアウトビュー widget がルーラー/タブの分だけ下にずれているとビューの外に落ち、
		// 「クリップされて出ない」のか「そもそもビュー外に置いた」のか区別できなくなるため使わない。
		// ※スクロール/ズームで可視範囲は動くが、再計算は Show のたびなので押下中も隅に留まる。
		PMReal sx = 1.0, sy = 1.0;
		{
			const PMMatrix toWindow = fControlView->GetContentToWindowMatrix();
			sx = toWindow.GetXScale();	if (sx < 0) sx = -sx;
			sy = toWindow.GetYScale();	if (sy < 0) sy = -sy;
		}
		if (sx == 0 || sy == 0)
			return;

		// 基準点は可視範囲の左上から画面 110px 内側。四角2つと文字は基準点の左上へ 100px 前後
		// はみ出す(BuildSpriteProbePath の gap / CreateTrackerPaths の tx,ty)ので、その分の余白。
		const PMReal kHudInsetPx = 110.0;
		PMPoint pbWhere;
		InterfacePtr<IPanorama> pano(KESCMQueryPanorama(fControlView));
		if (pano != nil)
		{
			const PMPoint topLeft = pano->GetContentLocationAtFrameOrigin();	// ビュー左上に来ている content 点
			pbWhere = PMPoint(topLeft.X() + kHudInsetPx / sx, topLeft.Y() + kHudInsetPx / sy);
		}
		else
		{
			// 保険: パノラマが取れないビュー。ウィンドウ座標の定点を content 座標へ落とす。
			pbWhere = PMPoint(kHudInsetPx, kHudInsetPx);
			fControlView->GetWindowToContentMatrix().Transform(&pbWhere);
		}

		NonMarkingAGMGraphicsContext gc(fControlView);
		if (!fSpriteProbeAlive)
		{
			// itemList = nil = 「ページアイテムではなく自前パスを描く」(パス作成トラッカーと同じ使い方)。
			sprite->CreateSprite(&gc, nil, PMPoint(), kFalse /* don't draw item list */);
			fSpriteProbeAlive = kTrue;
			fSpriteProbeShown = kFalse;
		}
		if (fSpriteProbeShown)
			sprite->Erase(&gc, PMPoint(), IShape::kDrawCreateDynamic);

		// ★文字の基準点を更新する位置は「Erase の後・Show の前」。Erase は前回の絵を消す描画なので、
		// 先に書き換えると消す位置がずれる(CreateTrackerPaths は Erase でも呼ばれる)。
		sSpriteProbeAnchor = pbWhere;
		this->BuildSpriteProbePath(pbWhere);
		sprite->Show(&gc, PMPoint(), IShape::kDrawCreateDynamic);
		fSpriteProbeShown = kTrue;
	}

public:
	/** ★実験プローブ(一時): one-shot タイマーから呼ぶ入口。現在のマウス位置に sprite を出す。 */
	void ShowSpriteProbeAtMouse()
	{
		if (fControlView == nil)
			return;
		this->ShowSpriteProbe(Utils<ILayoutUIUtils>()->GetMousePasteboardPosition(
			Utils<IEventUtils>()->GetGlobalMouseLocation(), fControlView));
	}

private:
	/** 押下解除/中断で sprite を片付ける。CreateSprite していなければ何もしない(二重解放なし)。 */
	void HideSpriteProbe()
	{
		// ★one-shot タイマーが残っていたら必ず止めて返す(押下の外にコールバックを残さない)。
		if (sSpriteProbeTimer != nil)
		{
			sSpriteProbeTimer->StopTimer();
			sSpriteProbeTimer->Release();
			sSpriteProbeTimer = nil;
		}
		sSpriteProbeTracker = nil;

		if (fSpriteProbeAlive && fControlView != nil)
		{
			InterfacePtr<ISprite> sprite(this, IID_ISPRITE);
			if (sprite != nil)
			{
				NonMarkingAGMGraphicsContext gc(fControlView);
				sprite->Hide(&gc, kFalse);
				sprite->DestroySprite(&gc);
			}
		}
		fSpriteProbeAlive = kFalse;
		fSpriteProbeShown = kFalse;
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

// ★★実験プローブ(一時・2026-07-26): 押下直後の one-shot 発火。BeginTracking を抜けた後に一度だけ
// 呼ばれ、現在のマウス位置に sprite を出す。自分の参照を先に返してから描く(描画中に何が起きても
// タイマーの解放漏れが残らないように)。撤去時はこの関数と file-static 2本、BeginTracking の
// StartTimer、HideSpriteProbe の停止処理を消す。
static uint32 KESCMSpriteProbeTimerProc(void* /*refPtr*/)
{
	// ★ここでは Release しない。RunTask の実行中に自分を解放すると、参照が 0 になって破棄された
	// オブジェクトのまま戻り値を返すことになる(自己破棄しながら実行継続)。しかも先に nil にすると
	// 暴走したときに StopTimer で止める手が無くなる。解放は押下解除(HideSpriteProbe)の1箇所に集約する。
	if (sSpriteProbeTracker != nil)
		sSpriteProbeTracker->ShowSpriteProbeAtMouse();

	// ★★戻り値は IIdleTask::RunTask の再スケジュール値。**0 は「すぐまた呼べ」**であって
	// 「終わり」ではない(SDK のサンプル: `return fThingsLeftToDo ? 0 : kEndOfTime;`)。
	// 一度 0 を返したせいで無限に呼ばれ、毎回 CreateSprite+Show が走って InDesign が固まった
	// (2026-07-26 実機)。しかもコールバックの先頭で Release+nil にしているので外から止められない。
	// one-shot は必ず kEndOfTime を返すこと。
	return IIdleTask::kEndOfTime;
}

void KESCMTrackerShutdownSpriteProbe()
{
	// プラグイン終了時の保険。ICallbackTimer のコールバックは参照カウントされない生関数ポインタで、
	// 予約が残ったままこの .pln が降りると、発火時に消えた関数へ飛んでクラッシュする。押下中に終了する
	// ことは実質ないが、確実に止める(ポインタは deref せず、停止と解放だけ=終了処理中でも安全)。
	if (sSpriteProbeTimer != nil)
	{
		sSpriteProbeTimer->StopTimer();
		sSpriteProbeTimer->Release();
		sSpriteProbeTimer = nil;
	}
	sSpriteProbeTracker = nil;
	KESCMReleaseHudFont();	// フォント参照を .pln が降りる前に必ず返す
}

// End, KESCMTracker.cpp.
