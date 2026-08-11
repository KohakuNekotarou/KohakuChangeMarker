//========================================================================================
//
//  KESCMChangedPagesTSV.h
//
//  フライアウト「Export Changed Pages...」の入口。現在の比較(Start 後)の変更ページ一覧を、
//  KESCL の「Save Check Report」と同じ流儀のタブ区切りテキスト(UTF-8+BOM+CRLF)で保存し、
//  Excel/メモ帳にそのまま貼れるようにする。2列 = Page / Type(Changed / Inserted / Deleted、英語統一)。
//
//  データ源(KESCMDrawEventHandler の static + KESCMPageMap のペアリング):
//    ・変更 = sEntries(変化px>0 のページ)。旧ページは除外対応表(KESCMBuildPairing)で引く。
//    ・挿入 = sOverflowT(文書間ページ数差で相手なし・Target 側) ＋ 手動登録の Added ページ。
//    ・削除 = sOverflowS(同・Source 側) ＋ 手動登録の Removed ページ。
//  ★オーバーセット(sOverset*)は一切参照しない(ユーザー指定 2026-07-24)。
//
//  実体は KESCMChangedPagesTSV.cpp。
//
//========================================================================================
#ifndef __KESCMChangedPagesTSV_h__
#define __KESCMChangedPagesTSV_h__

// 現在の比較の変更ページ一覧を TSV ファイルに保存する。未 Start(sDB=nil)や変更ゼロなら
// 何も書かずステータス行に短く出して戻る。成功時は無言、失敗のみステータス行(KESCL の流儀)。
// フライアウト項目 kKESCMPopupExportChangedPagesActionID の DoAction から呼ぶ。
void KESCMExportChangedPagesTSV();

#endif // __KESCMChangedPagesTSV_h__
