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

#include "KESCMID.h"

#include "CToolCursorProvider.h"	// 基底(ツール用カーソルプロバイダ。ズーム/ハンド等の既定処理を持つ)
#include "ICursorMgr.h"			// eCursorModifierState
#include "CursorSpec.h"			// CursorSpec / CreateCursorBitmapProc
#include "CursorDefs.h"			// kCrsrNone
#include "ICursorUtils.h"		// QueryGraphicsPortForBitmap(自前バッファに AGM 描画)
#include "IGraphicsPort.h"		// ✓のストローク描画
#include "Utils.h"				// Utils<ICursorUtils>()

#include "KESCMCheckGlyph.h"	// KESCMDrawCheckGlyph(✓描画を CMYK カーソルと共有)
#include "KESCMPeek.h"			// KESCMToolCursorShouldBeBlack(黒/白抜きの判定。Target 上でだけ黒)

//----------------------------------------------------------------------------------------
//  ✓カーソルの描画コールバック
//----------------------------------------------------------------------------------------
//  buffer は呼び出し側が (最大カーソルサイズ)²×4 で確保済み。*width/*height は入力=最大サイズ
//  (hiRes 時は 2 倍)、出力=実使用サイズ。hasAlpha=kTrue で 32bit ARGB。hiRes 時は 2x 解像度で
//  buffer/寸法が倍。描画は論理座標(1x px)で行い、port が hiRes スケールを吸収する。
//  コールバックは引数でデータを渡せないため、黒/白抜きは「別 CursorID+別コールバック」で分ける
//  (キャッシュも ID 別に効く=ClearCache 不要で切り替わる。2026-07-15)。
static void KESCMCheckCursorBitmapCommon(uchar* bitmapBuffer, uint32* width, uint32* height, bool16* hasAlpha, bool16 hiRes,
                                         const PMReal& bodyGray, const PMReal& haloGray)
{
	// 前処理(透明クリア+サイズ確定+ポート取得)は CMYK カーソルと共有(KESCMCheckGlyph.h)。
	// 論理サイズ(1x px)は標準カーソルと同程度の 24x24。
	uint32 maxLogW = 0, maxLogH = 0;
	KESCMCursorBitmapBegin(bitmapBuffer, *width, *height, hiRes, maxLogW, maxLogH);
	InterfacePtr<IGraphicsPort> gPort(KESCMCursorBitmapFinish(
		bitmapBuffer, width, height, hasAlpha, hiRes, 24u, 24u, maxLogW, maxLogH));
	if (gPort == nil)
		return;

	// ✓ (white halo + body). Shared with the CMYK readout cursor (KESCMPeek.cpp) via
	// KESCMDrawCheckGlyph so both draw the identical shape. Vertex (10,18) = the .fr HOTC
	// (kKESCMCheckCursorResID / kKESCMCheckCursorInactiveResID) hotspot.
	gPort->setopacity(PMReal(1.0), kFalse);
	KESCMDrawCheckGlyph(gPort, bodyGray, haloGray);
}

// 黒✓=白フチ+黒本体(Start 中かつマウス下が Target 文書=ツールが効く場所)。
static void KESCMCheckCursorBitmapProc(uchar* bitmapBuffer, uint32* width, uint32* height, bool16* hasAlpha, bool16 hiRes)
{
	KESCMCheckCursorBitmapCommon(bitmapBuffer, width, height, hasAlpha, hiRes, PMReal(0.0), PMReal(1.0));
}

// 白抜き✓=黒フチ+白本体(Source・第3の文書・未 Start=ツールが効かない場所の明示)。
// ★灰色本体(0.55)を先に試したが判別しづらいとのユーザー報告(2026-07-15)→白の塗りに黒の縁へ反転。
static void KESCMCheckCursorInactiveBitmapProc(uchar* bitmapBuffer, uint32* width, uint32* height, bool16* hasAlpha, bool16 hiRes)
{
	KESCMCheckCursorBitmapCommon(bitmapBuffer, width, height, hasAlpha, hiRes, PMReal(1.0), PMReal(0.0));
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
	// (毎回同じ絵=キャッシュ可)。ホットスポットは HOTC(各 ResID)から取る。
	// 黒✓=「Start 中かつマウス下が Target 文書」(ツールが効く場所)のみ。それ以外は白抜き✓(黒フチ+白本体)で
	// 「ここではツールは効かない」を示す(ユーザー指定 2026-07-15)。2状態は CursorID ごと分けるので
	// キャッシュはそのまま効き、境界をまたいだ瞬間にスペック違いで確実に切り替わる(ClearCache 不要)。
	if (KESCMToolCursorShouldBeBlack(viewUnderMouse))
		return CursorSpec(GetPlugIn()->GetPluginID(), IDFile(), kKESCMCheckCursorResID, &KESCMCheckCursorBitmapProc, kFalse);
	return CursorSpec(GetPlugIn()->GetPluginID(), IDFile(), kKESCMCheckCursorInactiveResID, &KESCMCheckCursorInactiveBitmapProc, kFalse);
}

// End, KESCMCursorProvider.cpp.
