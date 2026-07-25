//========================================================================================
//
//  KESCMCursorProvider.cpp
//
//  KESCM ツール選択中の「常時✓カーソル」。ツールの既定カーソルを差し替えるカーソルプロバイダ
//  (CToolCursorProvider 派生)。ツールボックスで KESCM ツールを選ぶと、レイアウトビュー上で
//  マウスが✓チェックマークになる。✓の折れ点(頂点)がホットスポット=クリック位置(座標取得点)で、
//  ホットスポットは .fr の HOTC(kKESCMCheckCursorResID) で指定する。
//
//  ★2026-07-25: ✓画像を「CursorSpec のコールバック描画」から **PNG リソース(PNGC)** へ変更した。
//  経緯: 押下(Alt+左)の瞬間に間欠的に出る「ゴミ」の発生源が、基底 CTracker::BeginTracking の中の
//  ✓再設置(InitializeModalCursor / UpdateModalCursor がモーダルカーソルを取得し直す)でコールバックが
//  呼ばれ、完成前のバッファが1フレーム見えることだった(切り分け 2026-07-25)。リソースカーソルなら
//  再設置でバッファ描画が起きないので、発生源そのものが無くなる。
//  ※コールバック描画がプロバイダ経路(GetCursor)でも効くことは 2026-07-13〜実機で実証済み。手法自体は
//    有効で、CMYK 情報カーソル(KESCMPeek.cpp の KESCMCmykCursorBitmapProc = 毎回内容が変わるので
//    リソース化できない)では引き続き使っている。✓は内容が固定なのでリソースで足りる。
//  画像は KESCMCheckGlyph.h と同じ幾何(頂点 5,12 - 10,18 - 20,5 / halo 3.5 or 5.0 / body 2.4 / 丸端)で
//  生成したもの: KESCM_Check_10_18.png(黒✓) / KESCM_CheckOff_10_18.png(白抜き✓)、各 @2x / @3to2x。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "KESCMID.h"

#include "CToolCursorProvider.h"	// 基底(ツール用カーソルプロバイダ。ズーム/ハンド等の既定処理を持つ)
#include "ICursorMgr.h"			// eCursorModifierState
#include "CursorSpec.h"			// CursorSpec
#include "CursorDefs.h"			// kCrsrNone

#include "KESCMPeek.h"			// KESCMToolCursorShouldBeBlack(黒/白抜きの判定。Target 上でだけ黒)

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

	// それ以外は常時✓。画像は .fr の PNGC リソース(ID ごと。2x/1.5x も同 ID+オフセットで登録済み)、
	// ホットスポットは HOTC(各 ResID)から取る。★コールバック(proc)は渡さない=設置のたびにバッファへ
	// 描き直すことがなくなり、押下時の「完成前の1フレーム」が原理的に発生しない(2026-07-25)。
	// 黒✓=「Start 中かつマウス下が Target 文書」(ツールが効く場所)のみ。それ以外は白抜き✓(黒フチ+白本体)で
	// 「ここではツールは効かない」を示す(ユーザー指定 2026-07-15)。2状態は CursorID ごと分けるので
	// 境界をまたいだ瞬間にスペック違いで確実に切り替わる。
	if (KESCMToolCursorShouldBeBlack(viewUnderMouse))
		return CursorSpec(GetPlugIn()->GetPluginID(), kKESCMCheckCursorResID);
	return CursorSpec(GetPlugIn()->GetPluginID(), kKESCMCheckCursorInactiveResID);
}

// End, KESCMCursorProvider.cpp.
