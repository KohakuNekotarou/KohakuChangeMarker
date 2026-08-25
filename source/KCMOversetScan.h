//========================================================================================
//
//  KCMOversetScan.h
//
//  Find Overset(フライアウト)用の検出。アクティブ1文書を走査し、オーバーセット(あふれ=テキストが
//  フレームに入りきらず赤「+」が出る状態)を含むページの UID を集める。比較(新旧)とは無関係の単独点検。
//  実装は KBS の overset locator ロジック(最後の配置済みフレーム探索+テーブルアンカー鎖登り)を
//  この .cpp にインライン複製しており、KBS プラグインへのビルド依存は持たない。
//
//========================================================================================
#ifndef __KCMOversetScan_h__
#define __KCMOversetScan_h__

#include <vector>
#include "UIDRef.h"		// UID
#include "PMPoint.h"	// PBPMPoint(overset「+」のペーストボード座標)

class IDataBase;

// 1つの overset「+」の位置。pageUID=その「+」が載るページ、pb=「+」点(最後の配置済みパーセルの
// outport=右下。KBS の KBSOversetLocator と同じ算出)のペーストボード座標。Prev/Next の overset ジャンプ先。
struct KCMOversetLoc
{
	UID			pageUID;
	PBPMPoint	pb;
	KCMOversetLoc() : pageUID(kInvalidUID) {}
	KCMOversetLoc(UID p, const PBPMPoint& pt) : pageUID(p), pb(pt) {}
};

// アクティブ文書 db を走査し、overset(あふれ)箇所を1つずつ outLocs に集める(追記; 呼び出し側で事前に
// clear すること)。各ストーリーのプライマリスレッド overset(通常フレームの赤「+」)＋全テーブルの全セル
// 単独あふれ(赤丸)を検出し、それぞれの「+」ペーストボード点とページ UID を1エントリとして積む
// (KBS 流=箇所ごと)。ページに載らない(ペーストボード等)フレームのあふれは載るページが無いのでスキップ。
// ★★あふれを聞く前に、古くなった組版だけは最新化する(RecomposeThruLastFrame)。あふれは組版の結果なので、
// 組み直さずに聞くと「もう直したあふれ」を報告し「今出たあふれ」を見落とす。⚠組めば文書は dirty になるが、
// 入る前が clean なら出るときに戻す(IDataBase::SaveRestoreModifiedState)。詳細は .cpp の (0)。
void KCMCollectOversetLocations(IDataBase* db, std::vector<KCMOversetLoc>& outLocs);

// ⚠ページ UID の集合だけを返す薄いラッパ KCMCollectOversetPages は 2026-08-06(ブロック10 監査)で撤去。
//   唯一の呼び出し予定地だった KCMApplyOversetForDoc は位置列**と**ページ集合の両方を要るので、ラッパを
//   呼ぶと走査が2回になる——だから最初からインラインで畳んでおり、ラッパは誰からも呼ばれていなかった。

#endif // __KCMOversetScan_h__
