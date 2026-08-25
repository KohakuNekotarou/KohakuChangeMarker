//========================================================================================
//
//  KCMCursorProvider.cpp
//
//  KCM ツール選択中の「常時✓カーソル」。ツールの既定カーソルを差し替えるカーソルプロバイダ
//  (CToolCursorProvider 派生)。ツールボックスで KCM ツールを選ぶと、レイアウトビュー上で
//  マウスが✓チェックマークになる。✓の折れ点(頂点)がホットスポット=クリック位置(座標取得点)で、
//  ホットスポットは .fr の HOTC(kKCMCheckCursorResID) で指定する。
//
//  ★2026-07-25: ✓画像を「CursorSpec のコールバック描画」から **PNG リソース(PNGC)** へ変更した。
//  経緯: 押下(Alt+左)の瞬間に間欠的に出る「ゴミ」の発生源が、基底 CTracker::BeginTracking の中の
//  ✓再設置(InitializeModalCursor / UpdateModalCursor がモーダルカーソルを取得し直す)でコールバックが
//  呼ばれ、完成前のバッファが1フレーム見えることだった(切り分け 2026-07-25)。リソースカーソルなら
//  再設置でバッファ描画が起きないので、発生源そのものが無くなる。
//  ※コールバック描画がプロバイダ経路(GetCursor)でも効くことは 2026-07-13〜実機で実証済み。手法自体は
//    有効で、CMYK 情報カーソル(KCMCmykCursor.cpp の KCMCmykCursorBitmapProc = 毎回内容が変わるので
//    リソース化できない)では引き続き使っている。✓は内容が固定なのでリソースで足りる。
//  画像は KCMCheckGlyph.h と同じ幾何(頂点 5,12 - 10,18 - 20,5 / halo 4.2(active) or 5.0(inactive) /
//  body 2.4 / 丸端)で生成したもの: KCM_Check_10_18.png(黒✓) / KCM_CheckOff_10_18.png(白抜き✓)、
//  各 @2x / @3to2x。
//  ⚠halo は 2026-08-19(不具合再検査 B-U6)まで「3.5 or 5.0」と書いてあったが、**3.5 は
//    2026-07-25 にユーザー要望で 4.2 へ太くする前の値**。KCMCheckGlyph.h の既定値も
//    生成スクリプト work/kescm-make-check-cursor.ps1 も 4.2 で、**この1行だけが旧値のまま**だった
//    (実物の PNG は 4.2 で生成済み＝見た目は食い違っていない)。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "KCMUIID.h"

#include "CToolCursorProvider.h"	// 基底(ツール用カーソルプロバイダ。ズーム/ハンド等の既定処理を持つ)
#include "ICursorMgr.h"			// eCursorModifierState
#include "CursorSpec.h"			// CursorSpec
#include "CursorDefs.h"			// kCrsrNone

#include "KCMCmykCursor.h"	// KCMToolCursorShouldBeBlack(黒/白抜きの判定。Start 中は文書を問わず黒)

//----------------------------------------------------------------------------------------
//  カーソルプロバイダ本体
//----------------------------------------------------------------------------------------
/** KCM ツールのカーソルプロバイダ。ツール選択中は常時✓を出す。
	手本 = sdksamples/snapshot/SnapCursorProvider.cpp。
*/
class KCMCheckCursorProvider : public CToolCursorProvider
{
	public:
		KCMCheckCursorProvider(IPMUnknown* boss) : CToolCursorProvider(boss) {}
		~KCMCheckCursorProvider() {}

		virtual CursorSpec	GetCursor(IControlView* viewUnderMouse, const SysPoint globalMouse, ICursorMgr::eCursorModifierState modifiers) const;
};

CREATE_PMINTERFACE(KCMCheckCursorProvider, kKCMCursorProviderImpl)

CursorSpec KCMCheckCursorProvider::GetCursor(IControlView* viewUnderMouse, const SysPoint globalMouse, ICursorMgr::eCursorModifierState modifiers) const
{
	// 修飾キーによるズーム/ハンド等の標準カーソルは基底に任せる(spacebar=ハンド等を維持)。
	CursorSpec base = CToolCursorProvider::GetCursor(viewUnderMouse, globalMouse, modifiers);
	if (base.GetID() != kCrsrNone)
		return base;

	// それ以外は常時✓。画像は .fr の PNGC リソース(ID ごと。2x/1.5x も同 ID+オフセットで登録済み)、
	// ホットスポットは HOTC(各 ResID)から取る。★コールバック(proc)は渡さない=設置のたびにバッファへ
	// 描き直すことがなくなり、押下時の「完成前の1フレーム」が原理的に発生しない(2026-07-25)。
	// 黒✓=「Start 中(比較を実行中)」。マウス下がどの文書でも黒にする(ユーザー指定 2026-07-26。以前は
	// Target 文書の上だけ黒だった)。Stop 中は白抜き✓(黒フチ+白本体)。2状態は CursorID ごと分けるので
	// 境界をまたいだ瞬間にスペック違いで確実に切り替わる。
	// ★PluginCursorSpec = CursorSpec(GetPlugIn()->GetPluginID(), id) の公式マクロ(CursorSpec.h:145-149)。
	//   ヘッダーが「PluginID 定数をコードに埋める必要をなくすため」と用途を明記している。手本=製品
	//   open/components/buttonui/misc/AppearancePlaceBehaviorUI.cpp:124,132(2026-08-06 ブロック7 監査 A-2)。
	if (KCMToolCursorShouldBeBlack(viewUnderMouse))
		return PluginCursorSpec(kKCMCheckCursorResID);
	return PluginCursorSpec(kKCMCheckCursorInactiveResID);
}

// End, KCMCursorProvider.cpp.
