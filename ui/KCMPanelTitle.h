//========================================================================================
//
//  KCMPanelTitle.h
//
//  パネルのタブに「今どちらのモードで比べるのか」を出す（2026-08-21・ユーザー指定）。
//    「Kohaku Change Marker - Pixel」／「Kohaku Change Marker - Story」
//
//  ★★手本は KBS の同名ファイル（`KBS/source/KBSPanelTitle.cpp`）。あちらは検索範囲を
//    「- Document」／「- Book」と出しており、ユーザーの指定も「KBS のドキュメントとブックの様に」。
//    ⇒ 仕組みごと同じにする（[[follow-official-implementation-first]] の社内版）。
//
//  ★**ラベルはパネルではなくパレット（タブを描く容器）の持ち物**。`IPanelMgr::GetPaletteRef
//    ContainingPanel` で容器を取り、`PaletteRefUtils::SetPaletteLabel(..., kTitle_PanelLabel)`。
//
//  ⚠**素の名前は読み戻せない**ので、こちら側で綴っておくしかない
//    （`PaletteRefUtils::GetPaletteLabel` は一度も表示されていないパレットには空を返し、
//      `IWindow::GetTitle` は「最後に SET した値」しか返さない＝どちらも「元の名前」を知らない）。
//    ⇒ 素の名前は `kKCMDisplayName`（表示名の唯一の定義）から組み立てる。
//
//  ★**呼ぶのは3か所**（どれも「今の状態を書き直す」だけなので、いつ何度呼んでもよい）:
//     ① モードを切り替えたとき（KCMActionComponent の KCMApplyCompareMode）
//     ② パネルが表示されたとき（KCMPanelObserver::AutoAttach。widget は毎回作り直されるが、
//        **タブのラベルはパレットの持ち物なので消えない** ---- ただし初回表示までは書く先が無い
//        ＝下の関数は panelView==nil で黙って戻るので、そのときの一手がここになる）
//     ③ 終了時に素へ戻す（KCMUIStartup の Shutdown）
//
//========================================================================================

#ifndef __KCMPanelTitle_h__
#define __KCMPanelTitle_h__

namespace KCMPanelTitle
{
	/** 今の比較モードをタブへ書く。パネルがまだ無ければ何もしない（安全に何度でも呼べる）。 */
	void Update();

	/** タブを素の名前へ戻す。終了処理から呼ぶ。 */
	void Restore();
}

#endif // __KCMPanelTitle_h__
