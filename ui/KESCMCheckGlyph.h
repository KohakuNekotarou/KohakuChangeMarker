//========================================================================================
//
//  KESCMCheckGlyph.h
//
//  Shared drawing of the KESCM check mark (✓) into an IGraphicsPort: a white halo stroke
//  under a black body, at fixed logical (1x px) coordinates whose vertex (10,18) is the
//  cursor hotspot. Used by the Alt+left CMYK readout cursor, which keeps the ✓ on top of the
//  numbers (KESCMPeek.cpp). The caller owns the buffer clear and setopacity; this only strokes
//  the glyph.
//
//  ★★形を変えるときは PNG も作り直すこと(2026-07-25)。ツール選択中の常時✓カーソルは、押下時のゴミ対策で
//  コールバック描画をやめて PNG リソース(KESCM_Check_10_18.png / KESCM_CheckOff_10_18.png ＋ @2x/@3to2x)に
//  なったため、✓の絵の出どころが「この関数」と「PNG」の2つある。PNG はこの関数と同じ幾何(頂点・線幅・
//  丸端)で生成したもので、再生成スクリプト = work/kescm-make-check-cursor.ps1(引数=出力フォルダー)。
//  生成後は source/sdksamples/KESCM/ui/ へ置き、**KCMUI.fr を touch してから**ビルドすること
//  (PNG だけ差し替えても ODFRC が走らず古い画像がリンクされ続ける既知の罠)。
//
//  Header-only inline (no .cpp / no build-system change): both callers already include
//  IGraphicsPort.h, and inlining avoids an ODR clash across the two translation units.
//
//========================================================================================
#ifndef __KESCMCheckGlyph_h__
#define __KESCMCheckGlyph_h__

#include "IGraphicsPort.h"
#include "PMReal.h"
#include "ICursorUtils.h"	// QueryGraphicsPortForBitmap(KESCMCursorBitmapFinish)
#include "Utils.h"
#include <cstring>			// std::memset(KESCMCursorBitmapBegin の透明クリア)

//----------------------------------------------------------------------------------------
// カーソルビットマップ・コールバック共通の前処理(2026-07-15 に2つのコールバックの重複約16行を集約。
// 使用箇所 = ✓カーソル KESCMCursorProvider.cpp / CMYK 情報カーソル KESCMPeek.cpp)。
// 手順: Begin(確保全域を透明クリア+論理最大サイズ取得) → 呼び出し側が論理サイズを決める →
//       Finish(クランプ+出力サイズ設定+AGM ポート取得)。
//----------------------------------------------------------------------------------------

/** 1) 確保バッファ全域を透明(ARGB=0)にクリアし、論理最大サイズ(1x px)を返す。
	QueryGraphicsPortForBitmap は既存内容を消さないため、描かない画素にゴミが残るのを防ぐ。
	allocW/allocH = 呼び出し側が確保した実サイズ(hiRes 時は論理の2倍)。 */
inline void KESCMCursorBitmapBegin(uchar* buffer, uint32 allocW, uint32 allocH, bool16 hiRes,
                                   uint32& outMaxLogW, uint32& outMaxLogH)
{
	const uint32 scale = hiRes ? 2u : 1u;
	outMaxLogW = allocW / scale;
	outMaxLogH = allocH / scale;
	std::memset(buffer, 0, (size_t)allocW * (size_t)allocH * 4u);
}

/** 2) 論理サイズを最大にクランプして *width/*height/*hasAlpha を確定し、AGM ポートを返す
	(AddRef 済み=呼び出し側が InterfacePtr で受ける。失敗時 nil)。描画は論理座標(1x px)で行い、
	port が hiRes スケールを吸収する。 */
inline IGraphicsPort* KESCMCursorBitmapFinish(uchar* buffer, uint32* width, uint32* height, bool16* hasAlpha,
                                              bool16 hiRes, uint32 logW, uint32 logH,
                                              uint32 maxLogW, uint32 maxLogH)
{
	if (logW > maxLogW) logW = maxLogW;
	if (logH > maxLogH) logH = maxLogH;
	const uint32 scale = hiRes ? 2u : 1u;
	const uint32 actW = logW * scale;
	const uint32 actH = logH * scale;
	*width    = actW;
	*height   = actH;
	*hasAlpha = kTrue;
	return Utils<ICursorUtils>()->QueryGraphicsPortForBitmap(buffer, actW, actH, kTrue /*hasAlpha*/, hiRes);
}

/** Stroke the ✓ into gPort (halo stroke under a thinner body stroke). Coordinates match the
	.fr HOTC for kKESCMCheckCursorResID so the bend sits on the cursor hotspot / click point.
	Two color schemes (2026-07-15, user-specified):
	  - active   (default): white halo + black body — over the armed Target where the tool works.
	  - inactive (inverted): black halo + white body ("白の塗りに黒の縁") — everywhere else,
	    meaning "the tool does nothing here". A gray body was tried first but was hard to tell apart.
	@param bodyGray body stroke gray level (0.0 = black default, 1.0 = white for inactive)
	@param haloGray halo stroke gray level (1.0 = white default, 0.0 = black for inactive)
	@param haloWidth halo (rim) stroke width. Default 4.2 gives a ~0.9px visible rim over the
	       2.4px body (★2026-07-25 にユーザー要望で 3.5=0.55px から太くした。PNG 側 work/
	       kescm-make-check-cursor.ps1 の active 幅と必ず同じ値にすること)。The inactive
	       (black-rimmed) cursor passes a slightly larger value because a dark rim reads thinner
	       than a light one at the same width (irradiation illusion), so it needs to be a touch
	       wider to look the same thickness (user report 2026-07-15). */
inline void KESCMDrawCheckGlyph(IGraphicsPort* gPort,
                                const PMReal& bodyGray = PMReal(0.0),
                                const PMReal& haloGray = PMReal(1.0),
                                const PMReal& haloWidth = PMReal(4.2))
{
	if (gPort == nil)
		return;

	const PMReal ax( 5.0), ay(12.0);	// short arm tip (upper-left)
	const PMReal bx(10.0), by(18.0);	// vertex (bend) = hotspot / click point
	const PMReal cx(20.0), cy( 5.0);	// long arm tip (upper-right)

	gPort->setlinecap(1);	// round cap
	gPort->setlinejoin(1);	// round join

	gPort->setrgbcolor(haloGray, haloGray, haloGray);	// halo (white default: readable on any background)
	gPort->setlinewidth(haloWidth);
	gPort->newpath();
	gPort->moveto(ax, ay);
	gPort->lineto(bx, by);
	gPort->lineto(cx, cy);
	gPort->stroke();

	gPort->setrgbcolor(bodyGray, bodyGray, bodyGray);	// body (black default / white when inactive)
	gPort->setlinewidth(PMReal(2.4));
	gPort->newpath();
	gPort->moveto(ax, ay);
	gPort->lineto(bx, by);
	gPort->lineto(cx, cy);
	gPort->stroke();
}

#endif // __KESCMCheckGlyph_h__
