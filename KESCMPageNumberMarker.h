//========================================================================================
//
//  KESCMPageNumberMarker.h
//
//  自動ページ番号(ノンブル、Type > Insert Special Character > Markers > Current Page Number)を
//  含むテキストフレームの領域を検出し、比較(差分ラスタ)から除外するための入口。
//
//  背景(2026-07-05 ユーザー指摘): ページ数の差分(追加/削除ページ)を登録して正しく再整列しても、
//  自動採番のノンブルは新旧文書で連番が違う(削除で全体がずれる等)ため、実デザインが同一でも
//  印字される数字が違い、CMYKピクセル比較では「変更あり」と誤検知され続ける。ノンブルは通常
//  マスターページ側に配置され、各ページでは上書きされていないのが普通なので、判別には
//  ローカルアイテムだけでなく、適用マスタースプレッド側のアイテムも見る必要がある。
//
//  実装方針: KESCMDrawEventHandler::MakeEntry のCMYK比較ループの直前に、対象ページ・比較元ページ
//  それぞれのノンブルフレームの矩形(ページinner座標)を求め、比較解像度(hiRes)のピクセル座標へ
//  変換して、その領域内の画素は差分判定から除外する。
//
//========================================================================================
#ifndef __KESCMPageNumberMarker_h__
#define __KESCMPageNumberMarker_h__

#include "BaseType.h"		// bool16
#include "PMReal.h"
#include "PMRect.h"			// PMRect
#include "UIDRef.h"			// UIDRef
#include <vector>

class IDataBase;

// フライアウト「Ignore Page Number Marker」の状態(セッション内のみ・既定=kTrue)。
bool16	KESCMGetIgnorePageNumberMarker();
void	KESCMSetIgnorePageNumberMarker(bool16 on);

// pageRef のページに実際に描画される「Current Page Number」マーカーを含むテキストフレームの
// 矩形を、そのページの左上を原点とする pt 座標(ページinner bboxのLeft/Topを0とする)で
// outRects へ追加する(既存の内容はクリアしない=target/source 両方をまとめて1本のリストへ積める
// 呼び方を想定)。ローカルアイテム・マスター由来(未上書き)アイテムの両方を対象にする。
// トグルが OFF の間は呼び出し不要(KESCMGetIgnorePageNumberMarker で判定してから呼ぶこと)。
// ★ピクセル座標への変換(比較解像度 hiRes 換算)は呼び出し側(KESCMDrawEventHandler.cpp、
// Int32Rect が既に使える文脈)で行う。実体は KESCMPageNumberMarker.cpp。
void	KESCMAppendPageNumberMarkerRects(const UIDRef& pageRef, std::vector<PMRect>& outRects);

#endif // __KESCMPageNumberMarker_h__
