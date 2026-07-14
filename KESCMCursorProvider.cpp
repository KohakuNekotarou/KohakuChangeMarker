//========================================================================================
//
//  KESCMCursorProvider.cpp
//
//  KESCM ツール選択中の「常時✓カーソル」。ツールの既定カーソルを差し替えるカーソルプロバイダ
//  (CToolCursorProvider 派生)。ツールボックスで KESCM ツールを選ぶと、レイアウトビュー上で
//  マウスが✓チェックマークになる。✓の折れ点(頂点)がホットスポット=クリック位置(座標取得点)で、
//  ホットスポットは .fr の HOTC(kKESCMCheckCursorResID) で指定する。
//
//  ★実験的実装(2026-07-13): ✓画像は PNGC/SVGC リソースではなく、CursorSpec のコールバック
//  (KESCMCheckCursorBitmapProc)で毎回 C++ 描画する。SDK 内でコールバック描画カーソルは
//  トラッキング中(ChangeModalCursor)専用の前例しかなく、カーソルプロバイダ(GetCursor)経路で
//  コールバックが効くかは未実証。効かなければ既定の矢印が出る(その場合は PNGC/SVGC 化する)。
//  カーソルへの自前描画手法の詳細は KESCMPeek.cpp の CMYK カーソル(KESCMCmykCursorBitmapProc)参照。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <cstring>				// std::memset(バッファを透明にクリア)

#include "KESCMID.h"

#include "CToolCursorProvider.h"	// 基底(ツール用カーソルプロバイダ。ズーム/ハンド等の既定処理を持つ)
#include "ICursorMgr.h"			// eCursorModifierState
#include "CursorSpec.h"			// CursorSpec / CreateCursorBitmapProc
#include "CursorDefs.h"			// kCrsrNone
#include "ICursorUtils.h"		// QueryGraphicsPortForBitmap(自前バッファに AGM 描画)
#include "IGraphicsPort.h"		// ✓のストローク描画
#include "Utils.h"				// Utils<ICursorUtils>()

#include "KESCMCheckGlyph.h"	// KESCMDrawCheckGlyph(✓描画を CMYK カーソルと共有)

//----------------------------------------------------------------------------------------
//  ✓カーソルの描画コールバック
//----------------------------------------------------------------------------------------
//  buffer は呼び出し側が (最大カーソルサイズ)²×4 で確保済み。*width/*height は入力=最大サイズ
//  (hiRes 時は 2 倍)、出力=実使用サイズ。hasAlpha=kTrue で 32bit ARGB。hiRes 時は 2x 解像度で
//  buffer/寸法が倍。描画は論理座標(1x px)で行い、port が hiRes スケールを吸収する。
static void KESCMCheckCursorBitmapProc(uchar* bitmapBuffer, uint32* width, uint32* height, bool16* hasAlpha, bool16 hiRes)
{
	const uint32 maxAllocW = *width;	// 呼び出し側が確保した最大サイズ(hiRes 時は 2 倍)
	const uint32 maxAllocH = *height;
	const uint32 scale     = hiRes ? 2u : 1u;
	const uint32 maxLogW   = maxAllocW / scale;
	const uint32 maxLogH   = maxAllocH / scale;

	// 論理サイズ(1x px)。標準カーソルと同程度の 24x24。最大を超えないようクランプ。
	uint32 logW = 24; if (logW > maxLogW) logW = maxLogW;
	uint32 logH = 24; if (logH > maxLogH) logH = maxLogH;
	const uint32 actW = logW * scale;
	const uint32 actH = logH * scale;

	// まずバッファ全体を透明(ARGB=0)にクリア。QueryGraphicsPortForBitmap は既存内容を消さないため、
	// 描かなかった画素にゴミが残るのを防ぐ。クリアは確保サイズ全域に対して行う。
	std::memset(bitmapBuffer, 0, (size_t)maxAllocW * (size_t)maxAllocH * 4u);

	*width    = actW;
	*height   = actH;
	*hasAlpha = kTrue;

	InterfacePtr<IGraphicsPort> gPort(Utils<ICursorUtils>()->QueryGraphicsPortForBitmap(
		bitmapBuffer, actW, actH, kTrue /*hasAlpha*/, hiRes));
	if (gPort == nil)
		return;

	// ✓ (white halo + black body). Shared with the CMYK readout cursor (KESCMPeek.cpp) via
	// KESCMDrawCheckGlyph so both draw the identical shape. Vertex (10,18) = the .fr HOTC
	// (kKESCMCheckCursorResID) hotspot.
	gPort->setopacity(PMReal(1.0), kFalse);
	KESCMDrawCheckGlyph(gPort);
}

//----------------------------------------------------------------------------------------
//  カーソルプロバイダ本体
//----------------------------------------------------------------------------------------
/** KESCM ツールのカーソルプロバイダ。ツール選択中は常時✓を出す。
	手本 = sdksamples/snapshot/SnapCursorProvider.cpp。
*/
class KESCMCheckCursorProvider : public CToolCursorProvider
{
	public:
		KESCMCheckCursorProvider(IPMUnknown* boss) : CToolCursorProvider(boss) {}
		~KESCMCheckCursorProvider() {}

		virtual CursorSpec	GetCursor(IControlView* viewUnderMouse, const SysPoint globalMouse, ICursorMgr::eCursorModifierState modifiers) const;
};

CREATE_PMINTERFACE(KESCMCheckCursorProvider, kKESCMCursorProviderImpl)

CursorSpec KESCMCheckCursorProvider::GetCursor(IControlView* viewUnderMouse, const SysPoint globalMouse, ICursorMgr::eCursorModifierState modifiers) const
{
	// 修飾キーによるズーム/ハンド等の標準カーソルは基底に任せる(spacebar=ハンド等を維持)。
	CursorSpec base = CToolCursorProvider::GetCursor(viewUnderMouse, globalMouse, modifiers);
	if (base.GetID() != kCrsrNone)
		return base;

	// それ以外は常時✓。画像はコールバックで毎回描画する。✓は静的なので bDynamicBitmap=kFalse
	// (毎回同じ絵=キャッシュ可)。ホットスポットは HOTC(kKESCMCheckCursorResID) から取る。
	return CursorSpec(GetPlugIn()->GetPluginID(), IDFile(), kKESCMCheckCursorResID, &KESCMCheckCursorBitmapProc, kFalse);
}

// End, KESCMCursorProvider.cpp.
