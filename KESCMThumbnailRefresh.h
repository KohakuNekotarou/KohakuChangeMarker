//========================================================================================
//
//  KESCMThumbnailRefresh.h
//
//  ★実験(2026-07-06): 比較実行後に、Pages パネルの「既に表示済み」のサムネイルを強制的に再生成
//  させられないか複数の公開APIを試す隔離モジュール。過去に単体では不発だった手
//  (InvalidateSpreadWidget / UpdatePagesPanel(bForcePurge) / ForceRedraw)に加え、未検証だった
//  IPendingUpdateController::Update()(描画直前の保留更新の消化)と、共有画像キャッシュの
//  IImageCacheMgr::Purge(db) を合わせて叩く。効果が無ければ呼び出し1行を消すだけで撤去できる。
//  背景と既知の制限: docs memory kescm-pages-panel-thumbnails。
//
//========================================================================================

#ifndef __KESCMThumbnailRefresh_h__
#define __KESCMThumbnailRefresh_h__

class IDataBase;

// db(比較対象文書)の Pages パネルサムネイルの再生成を試みる。パネルが隠れていても画像キャッシュの
// Purge だけは試す。効果は未検証(うまくいかない可能性が高い実験)。安全に何度でも呼べる。
void KESCMTryRefreshPagesPanelThumbnails(IDataBase* db);

#endif // __KESCMThumbnailRefresh_h__
