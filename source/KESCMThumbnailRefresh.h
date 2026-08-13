//========================================================================================
//
//  KESCMThumbnailRefresh.h
//
//  比較実行後に、Pages パネルの「既に表示済み」のサムネイルを再生成させる隔離モジュール。
//  ★2026-07-06 実機で切り分け完了。効く最小セット=2手だけ:
//    ④ IImageCacheMgr::Purge で共有画像キャッシュを無効化(=Pagesサムネイルはこの共有キャッシュに
//       載っている。2026-07-05の「内部専用キャッシュで不可」は誤りだった)+ ③ ForceRedraw で即再描画。
//       ★Purge の単位は「ページ UID ごと」(2026-07-07 に db 全体 Purge から変更。理由は .cpp 冒頭:
//       全体 Purge は既存サムネイルの無効化としては効かず、しかもパネル全体を点滅させる)。
//  不要と分かって外した手: ① InvalidateSpreadWidget+UpdatePagesPanel(bForcePurge)、② IPendingUpdateController。
//  ⚠①は IPagesSubPanelController(公開ヘッダー)の公式 API だが、単独では実機で不発(2026-07-05)。
//    SDK 全体で Adobe 自身の使用例もゼロ。「④と組み合わせたら効くか」は未再試験。
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
//   redrawNow(既定 kTrue): kFalse なら Purge だけ行い ForceRedraw をスキップする。Target/Source の
//     2文書を続けて更新する呼び出し側が、前半を kFalse にして最後の1回だけ再描画するためのバッチ化
//     (2026-07-25 監査: 1操作で最大9回走っていた同期 ForceRedraw の多重実行を削減)。
void KESCMTryRefreshPagesPanelThumbnails(IDataBase* db, const std::set<UID>* extraPages = nil, bool16 redrawNow = kTrue);

// ★KESCMCollectChangedPageUIDs は **KESCMCore.h へ移した**(2026-08-13・model/UI 分割 第1段 Task 10)。
//   widget を1つも触らない model の問いで、呼び手も model 側だけだった(逆流台帳 §2-1)。

// db の**全ページ**(通常＋マスター)を per-UID Purge して、Pages パネルのサムネイルを作り直させる。
//
// ★★なぜ「全ページ」という乱暴な入口が要るか(2026-08-13・Task 10)。
//   model は UI を直接呼ばなくなり、代わりに通知(kKESCM*Message)を投げる。**通知は ClassID しか
//   運べない**ので、KESCMTryRefreshPagesPanelThumbnails の extraPages ---- 「再比較の**前**に枠が
//   付いていた旧集合」---- を渡す道が無い。旧集合は再比較で失われるため、今の集合だけを Purge すると
//   **枠が消えたページのサムネイルに古い枠が残る**(この .cpp が 2026-07-08 に直した当の不具合)。
//   全ページを Purge すれば取りこぼしは原理的に起きない ---- ページ数に比例して遅くなるだけ。
//   ⚠**これは一時的な後退**。Task 12 で IKESCMMarkData(model 側が旧集合を答えられる窓口)が入ったら、
//     絞り込みへ戻すこと。TODO ではなく「Task 12 で戻す」と決まっている。
void KESCMPurgeAllPageThumbs(IDataBase* db, bool16 redrawNow = kTrue);

// db 内の指定ページ UID 群だけを per-UID Purge → Pages パネル再描画する。登録/解除トグルの直後など、
// 「変更ページ集合(sEntries/overflow)には自動では入らないが、確実にサムネイルを作り直したい」特定
// ページを明示更新するために使う。特に登録解除は sRegistered から先に消えるため、解除したページの
// 緑「/」を消すにはこの明示 Purge が必要。db が nil か pages が空なら何もしない。安全に何度でも呼べる。
// redrawNow の意味は KESCMTryRefreshPagesPanelThumbnails と同じ(kFalse=Purge のみ、バッチ化用)。
void KESCMRefreshThumbnailsForPages(IDataBase* db, const std::vector<UID>& pages, bool16 redrawNow = kTrue);

// Pages パネルが表示されていれば今すぐ再描画する(Purge 済みサムネイルの作り直しトリガー)。
// redrawNow=kFalse でバッチ化した呼び出し側が、最後に1回だけ呼ぶための公開版。
void KESCMForceRedrawPagesPanelNow();

#endif // __KESCMThumbnailRefresh_h__
