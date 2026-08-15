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
// ★2026-08-13(Task 9): ステータス行へ直接書かず、**出したい文字列を outMessage で返す**。
//   TSV 書き出しは「成功/失敗と保存先パス」を返すのが自然で、通知を投げる理由が無い(設計書 §3.3)。
//   ここは model 側で、表示は呼び手(フライアウトの「Export Changed Pages...」＝UI)が行う。
//   ★成功時は無言＝outMessage は空で返る(従来の仕様どおり)。
void KESCMExportChangedPagesTSV(PMString& outMessage);

#endif // __KESCMChangedPagesTSV_h__
