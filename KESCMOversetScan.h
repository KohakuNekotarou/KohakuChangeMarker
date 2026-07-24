//========================================================================================
//
//  KESCMOversetScan.h
//
//  Find Overset(フライアウト)用の検出。アクティブ1文書を走査し、オーバーセット(あふれ=テキストが
//  フレームに入りきらず赤「+」が出る状態)を含むページの UID を集める。比較(新旧)とは無関係の単独点検。
//  実装は KBS の overset locator ロジック(最後の配置済みフレーム探索+テーブルアンカー鎖登り)を
//  この .cpp にインライン複製しており、KBS プラグインへのビルド依存は持たない。
//
//========================================================================================
#ifndef __KESCMOversetScan_h__
#define __KESCMOversetScan_h__

#include <set>
#include <vector>
#include "UIDRef.h"		// UID
#include "PMPoint.h"	// PBPMPoint(overset「+」のペーストボード座標)

class IDataBase;

// 1つの overset「+」の位置。pageUID=その「+」が載るページ、pb=「+」点(最後の配置済みパーセルの
// outport=右下。KBS の KBSOversetLocator と同じ算出)のペーストボード座標。Prev/Next の overset ジャンプ先。
struct KESCMOversetLoc
{
	UID			pageUID;
	PBPMPoint	pb;
	KESCMOversetLoc() : pageUID(kInvalidUID) {}
	KESCMOversetLoc(UID p, const PBPMPoint& pt) : pageUID(p), pb(pt) {}
};

// アクティブ文書 db を走査し、overset(あふれ)箇所を1つずつ outLocs に集める(追記; 呼び出し側で事前に
// clear すること)。各ストーリーのプライマリスレッド overset(通常フレームの赤「+」)＋全テーブルの全セル
// 単独あふれ(赤丸)を検出し、それぞれの「+」ペーストボード点とページ UID を1エントリとして積む
// (KBS 流=箇所ごと)。ページに載らない(ペーストボード等)フレームのあふれは載るページが無いのでスキップ。
void KESCMCollectOversetLocations(IDataBase* db, std::vector<KESCMOversetLoc>& outLocs);

// 上の薄いラッパ。overset を含むページ UID の集合だけが要る用途(Pages パネルの枠/＋・スクロール地図)向け。
// 内部で KESCMCollectOversetLocations を1回走らせ、各 loc.pageUID を outPages に入れる(追記; 事前 clear)。
void KESCMCollectOversetPages(IDataBase* db, std::set<UID>& outPages);

#endif // __KESCMOversetScan_h__
