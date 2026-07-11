//========================================================================================
//
//  KESCMScrollMap.h
//
//  スクロールバー地図: 文書ウィンドウ(kLayoutPresentationBoss)の縦スクロールバー左隣に、
//  KESCM の枠(変更マーク)があるページ位置を示す細い strip ウィジェットを実行時注入する。
//  Visual Studio のスクロールバー検索マークと同じ発想。裏取り調査は
//  docs/ai-notes/scrollbar-minimap.md(SDK ルート)を参照。
//
//  フェーズ1(プローブ=オレンジ塗り)は 2026-07-11 実機表示OK。フェーズ2で実データ描画:
//  変更ページ=赤の塗りつぶし、Add/Remove 登録ページ=緑(色はユーザー指定 2026-07-11)。
//
//========================================================================================

#ifndef __KESCMScrollMap_h__
#define __KESCMScrollMap_h__

#include "BaseType.h"		// bool16

class IDataBase;

// targetDB の全レイアウトビューから文書ウィンドウ(presentation)を集め、まだ無ければ
// 縦スクロールバー左隣に地図 strip を注入する(既に注入済みの窓はスキップ=何度呼んでも安全)。
// 縦スクロールバーが見つからない窓は黙ってスキップする。
void	KESCMScrollMapAttach(IDataBase* targetDB);

// 全ドキュメントの全ウィンドウから、注入済みの地図 strip を探して取り外す。
// ポインタは保持しない設計(毎回 FindWidget で探す)なので、窓が既に閉じられていても安全。
void	KESCMScrollMapDetachAll();

// 注入済みの全 strip を再描画(Invalidate)する。比較の実行/再比較/登録トグル後にマークを
// 最新化するために呼ぶ(KESCMDoMarkChangesDoc の末尾)。strip が1つも無ければ何もしない。
void	KESCMScrollMapInvalidateAll();

#endif // __KESCMScrollMap_h__
