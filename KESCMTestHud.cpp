//========================================================================================
//
//  KESCMTestHud.cpp
//
//  ★実験(2026-08-07)。左ボタンを押している間だけ、押したビューの右上に "Test" を出す。
//    仕様・経緯・「なぜ Draw Event で隅に描けるのか」は KESCMTestHud.h の冒頭。
//
//  ここが持つのは「押下中か」「どのビューか」の2つだけ。描画の呼び出しは
//  KESCMDrawEventHandler::HandleDrawEvent が2系統(帯の前面 / カンバス背景)から行う。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IControlView.h"		// GetContentToWindowMatrix(ズーム) / GetBBox / WindowToContentTransform
#include "IGraphicsPort.h"		// rectfill / selectfont / show / 透明グループ
#include "IFontMgr.h"			// QueryFont / QueryFontInstance
#include "IPMFont.h"
#include "IFontInstance.h"		// MeasureWText / GetAscent / GetDescent(下地の寸法)
#include "ISession.h"			// GetExecutionContextSession
#include "AutoGSave.h"
#include "WideString.h"			// show に渡す UTF16
#include "PMMatrix.h"
#include "PMRect.h"

#include "KESCMTestHud.h"

//----------------------------------------------------------------------------------------
// 見た目(すべて画面ピクセル指定。ズームを変えても見た目は変わらない = 実ズームで割って content 単位へ)
//----------------------------------------------------------------------------------------
static const PMReal kKESCMTestHudTextPx     = 14.0;		// 文字の大きさ
static const PMReal kKESCMTestHudPadXPx     = 6.0;		// 下地の左右余白
static const PMReal kKESCMTestHudPadTopPx   = 3.0;		// 下地の上余白
static const PMReal kKESCMTestHudPadBotPx   = 3.0;		// 下地の下余白
static const PMReal kKESCMTestHudOpacity    = 0.85;		// 下地＋文字をまとめて薄くする度合い(1.0=不透明)
static const PMReal kKESCMTestHudRightPx    = 20.0;		// ビュー右端からの余白(★右上に置くので右基準)
static const PMReal kKESCMTestHudBaselinePx = 40.0;		// ビュー上端から文字ベースラインまで

//----------------------------------------------------------------------------------------
// 押下中だけ持つ状態
//----------------------------------------------------------------------------------------
static bool16        sActive = kFalse;	// 左ボタンを押している間だけ kTrue
static IControlView* sView   = nil;		// 押されたレイアウトビュー(借り物。比較にしか使わない)

// フォント(所有する)。"Test" は ASCII なので既定フォントで足りる —— 旧 HUD は文書名を出していたので
// 「その字を持つフォントを選ぶ」3段の選定が要ったが(gPort の show は単一フォントのグリフしか使わず、
// OS のようなフォールバックが無いため和文が化けた)、固定の欧文ならその問題は起きない。
static IPMFont*       sFont         = nil;
static IFontInstance* sFontInst     = nil;
static PMReal         sFontInstSize = 0.0;

static void KESCMTestHudReleaseFont()
{
	if (sFontInst != nil)
	{
		sFontInst->Release();
		sFontInst = nil;
	}
	sFontInstSize = 0.0;
	if (sFont != nil)
	{
		sFont->Release();
		sFont = nil;
	}
}

/** 既定フォント(キャッシュ付き)。所有はここ側 = 呼び出し側は Release しない。 */
static IPMFont* KESCMTestHudQueryFont()
{
	if (sFont != nil)
		return sFont;

	InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), UseDefaultIID());
	if (fontMgr == nil)
		return nil;
	sFont = fontMgr->QueryFont(fontMgr->GetDefaultFontName());
	return sFont;
}

/** そのサイズで測る/描くためのインスタンス(キャッシュ付き)。所有はここ側。
	作り方は KESCMDrawEventHandler.cpp の旧ページ番号バッジと同じ(サイズを対角に入れた行列を渡す)。
	★サイズはズームで変わるので、変わったら作り直す。 */
static IFontInstance* KESCMTestHudQueryFontInstance(IPMFont* font, const PMReal& size)
{
	if (font == nil || size <= 0)
		return nil;
	if (sFontInst != nil && sFontInstSize == size)
		return sFontInst;

	if (sFontInst != nil)
	{
		sFontInst->Release();
		sFontInst = nil;
	}
	InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), UseDefaultIID());
	if (fontMgr == nil)
		return nil;
	const PMMatrix fontMatrix(size, 0.0, 0.0, size, 0.0, 0.0);
	sFontInst     = fontMgr->QueryFontInstance(font, fontMatrix);
	sFontInstSize = size;
	return sFontInst;
}

/** HUD に出す文字列。実験なので固定。★SetTranslatable(kFalse) は必須
	(UI 文字列のリテラルは翻訳キー扱いされ、思わぬ訳語に化ける)。 */
static PMString KESCMTestHudLabel()
{
	PMString s("Test");
	s.SetTranslatable(kFalse);
	return s;
}

void KESCMTestHudBegin(IControlView* view)
{
	sActive = (view != nil);
	sView   = view;
}

void KESCMTestHudEnd()
{
	sActive = kFalse;
	sView   = nil;
	// フォントは持ち越してよい(次の押下でそのまま使う)。返すのは Shutdown だけ。
}

bool16 KESCMTestHudWantsDraw(IControlView* view)
{
	// ★押した窓にだけ出す。view が nil の描画(ページパネルのサムネイル生成など)も当然対象外。
	return (sActive && view != nil && view == sView) ? kTrue : kFalse;
}

void KESCMTestHudDraw(IGraphicsPort* gPort, IControlView* view, const PMPoint& spreadOffset)
{
	if (gPort == nil || view == nil)
		return;

	// 実ズーム(画面 px 指定を content 単位へ逆算するのに使う)。負スケールもあり得るので絶対値で。
	PMReal sx = 1.0, sy = 1.0;
	{
		const PMMatrix toWindow = view->GetContentToWindowMatrix();
		sx = toWindow.GetXScale();	if (sx < 0) sx = -sx;
		sy = toWindow.GetYScale();	if (sy < 0) sy = -sy;
	}
	if (sx == 0 || sy == 0)
		return;

	// ビューの可視範囲を pasteboard 座標で得る(トーストと同じ手順 = 窓座標の bbox を content へ変換)。
	// ★ここが「ビューの隅」の正体。スクロールしてもズームしても、この矩形が常に「今見えている範囲」。
	PMRect boundsPb = view->GetBBox();
	view->WindowToContentTransform(&boundsPb);

	const PMString labelStr = KESCMTestHudLabel();
	WideString     label(labelStr);
	IPMFont*       font = KESCMTestHudQueryFont();
	if (font == nil)
		return;

	const PMReal fontSize = PMReal(kKESCMTestHudTextPx) / sy;

	// 下地の大きさは実測で決める(文字数×固定幅の概算は外れる)。取れなければ概算へ落とす。
	PMReal textW   = 0.0;
	PMReal ascent  = fontSize * PMReal(0.8);
	PMReal descent = fontSize * PMReal(0.2);
	IFontInstance* inst = KESCMTestHudQueryFontInstance(font, fontSize);
	if (inst != nil)
	{
		inst->MeasureWText(label, textW);
		ascent  = inst->GetAscent();
		descent = inst->GetDescent();
	}
	if (textW <= 0)
		textW = fontSize * PMReal(0.6) * PMReal(label.CharCount());

	// 右上に置く。show はベースライン左端を (x,y) に置くので、右端から文字幅を戻した位置が左端。
	// ★このポートの座標系へ落とす = pasteboard の値から spreadOffset を引く(平行移動のみ)。
	const PMReal tx = boundsPb.Right() - PMReal(kKESCMTestHudRightPx) / sx - textW - spreadOffset.X();
	const PMReal ty = boundsPb.Top()   + PMReal(kKESCMTestHudBaselinePx) / sy - spreadOffset.Y();

	// 下地の矩形(= 透明グループの範囲でもある)。PMRect は (左, 上, 右, 下)。
	const PMRect hudRect(tx - PMReal(kKESCMTestHudPadXPx) / sx,
	                     ty - ascent - PMReal(kKESCMTestHudPadTopPx) / sy,
	                     tx + textW + PMReal(kKESCMTestHudPadXPx) / sx,
	                     ty + descent + PMReal(kKESCMTestHudPadBotPx) / sy);

	// ★下地と文字を透明グループで束ね、不透明度は**グループに1回だけ**掛ける。個別に setopacity すると
	//   文字と下地が重なる画素だけ濃くなる(旧ページ番号バッジ・旧 HUD と同じ作法)。
	AutoGSave ag(gPort);
	gPort->setopacity(PMReal(kKESCMTestHudOpacity), kFalse);
	gPort->starttransparencygroup(hudRect, nil, kFalse /*non-isolated*/, kFalse /*no knockout*/);

	// (1) 下地: 白ベタ。rectfill は (左, 上, 幅, 高さ)。
	gPort->newpath();
	gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));
	gPort->rectfill(hudRect.Left(), hudRect.Top(), hudRect.Width(), hudRect.Height());
	gPort->newpath();

	// (2) 文字: 黒。読みやすさは下地が担保するので、フチもブレンドも要らない。
	gPort->setrgbcolor(PMReal(0.0), PMReal(0.0), PMReal(0.0));
	gPort->selectfont(font, fontSize);
	gPort->show(tx, ty, label.NumUTF16TextChars(), label.GrabUTF16Buffer(nil), IGraphicsPort::kFillText);
	gPort->newpath();

	gPort->endtransparencygroup();
}

void KESCMTestHudShutdown()
{
	sActive = kFalse;
	sView   = nil;
	KESCMTestHudReleaseFont();	// フォント参照を .pln が降りる前に必ず返す
}

// End, KESCMTestHud.cpp.
