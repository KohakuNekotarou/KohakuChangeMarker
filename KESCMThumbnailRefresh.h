//========================================================================================
//
//  KESCMThumbnailRefresh.h
//
//  比較実行後に、Pages パネルの「既に表示済み」のサムネイルを再生成させる隔離モジュール。
//  ★2026-07-06 実機で切り分け完了。効く最小セット=2手だけ:
//    ④ IImageCacheMgr::Purge(db) で共有画像キャッシュを無効化(=Pagesサムネイルはこの共有キャッシュに
//       載っている。2026-07-05の「内部専用キャッシュで不可」は誤りだった)+ ③ ForceRedraw で即再描画。
//  不要と分かって外した手: ① InvalidateSpreadWidget+UpdatePagesPanel(bForcePurge)、② IPendingUpdateController。
//  背景と切り分け経過: docs memory kescm-pages-panel-thumbnails。
//
//========================================================================================

#ifndef __KESCMThumbnailRefresh_h__
#define __KESCMThumbnailRefresh_h__

class IDataBase;

// db(比較対象文書)の Pages パネルサムネイルの再生成を試みる。パネルが隠れていても画像キャッシュの
// Purge だけは試す。効果は未検証(うまくいかない可能性が高い実験)。安全に何度でも呼べる。
void KESCMTryRefreshPagesPanelThumbnails(IDataBase* db);

#endif // __KESCMThumbnailRefresh_h__
