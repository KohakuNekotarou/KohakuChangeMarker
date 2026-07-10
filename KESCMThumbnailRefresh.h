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

#include "OMTypes.h"		// UID
#include <vector>
#include <set>

class IDataBase;
class IControlView;

// 表示中の Pages パネル(kPagesPanelWidgetID)の IControlView を返す(非表示/取得失敗は nil)。
// IPanelMgr→GetVisiblePanel の定型を一本化した共有ヘルパ(ここでのサムネイル再描画と、
// KESCMChangeNav の Pages パネル連動スクロールが共用)。実体は KESCMThumbnailRefresh.cpp。
IControlView* KESCMGetVisiblePagesPanel();

// db(比較対象文書)の Pages パネルサムネイルの再生成を試みる。パネルが隠れていても画像キャッシュの
// Purge だけは試す。安全に何度でも呼べる。
//   extraPages(任意, nil可): 変更ページ集合(sEntries/overflow/登録)に加えて Purge したいページ UID。
//   ★再ペアリング(登録トグル/再Start)で「旧集合からは抜けたが新集合には入らない」ページ(overflow を
//     抜けた赤「/」・変更なしに戻ったリング等)は、新集合しか見ない per-UID Purge から漏れて古い枠が
//     残る。呼び出し側が「再比較前に枠が付いていたページ」をここへ渡すことで確実に作り直させる。
void KESCMTryRefreshPagesPanelThumbnails(IDataBase* db, const std::set<UID>* extraPages = nil);

// db が現在の比較対象(sDB/sSrcDB)なら、「今マークが出得るページ UID」(変更リング+overflow「/」+
// 登録「/」)を outPages へ追加して kTrue を返す。比較対象でなければ何もせず kFalse。
// ★「何がマーク済みか」の定義はこの1箇所に集約する(KESCMDoMarkChangesDoc の再比較前退避もこれを
// 使う)。マークの種類を増やす時はここへ足せば、退避と Purge の両方が自動で追随する。
bool16 KESCMCollectChangedPageUIDs(IDataBase* db, std::set<UID>& outPages);

// db 内の指定ページ UID 群だけを per-UID Purge → Pages パネル再描画する。登録/解除トグルの直後など、
// 「変更ページ集合(sEntries/overflow)には自動では入らないが、確実にサムネイルを作り直したい」特定
// ページを明示更新するために使う。特に登録解除は sRegistered から先に消えるため、解除したページの
// 緑「/」を消すにはこの明示 Purge が必要。db が nil か pages が空なら何もしない。安全に何度でも呼べる。
void KESCMRefreshThumbnailsForPages(IDataBase* db, const std::vector<UID>& pages);

#endif // __KESCMThumbnailRefresh_h__
