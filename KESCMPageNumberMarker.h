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

// フライアウト「Ignore Page Number Marker」の状態(セッション内のみ・既定=kFalse。実装の
// sIgnorePageNumberMarker が正。2026-07-25 監査でヘッダー側の「既定=kTrue」誤記を訂正)。
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

// ★上記のキャッシュ版(2026-08-06 の監査 E-3)。除外領域の緑ベタ塗り(可視化)は描画イベントのたびに
// 全ページぶん呼ばれるため、そのたびの実測(ページ上の全アイテム列挙＋各フレームの全文字走査＋
// マスターページアイテム収集＋wax 走査＋グリフ bbox)を避けて結果を覚えておく。
//   refresh=kTrue  … 必ず実測してキャッシュを更新する(比較を実行する MakeEntry 用)
//   refresh=kFalse … キャッシュがあればそれを返し、無ければ1回だけ実測して覚える(描画用)
// ★速さのためだけの寄せではない: 緑ベタ塗りが見せているのは「この比較で除外した領域」なので、
//   比較時に確定した矩形をそのまま描く方が意味としても正しい(毎回実測する旧実装では、比較の後に
//   ノンブルフレームを動かすと、枠は前回の比較のままなのに緑だけ今の位置へ動いていた)。
// 戻りはキャッシュ内のベクタへの参照。std::map はノードベースなので、他ページを足しても既存要素への
// 参照は無効化されない。同じページを refresh=kTrue で取り直すか、下の Invalidate を呼ぶまで有効。
const std::vector<PMRect>&	KESCMGetPageNumberMarkerRects(const UIDRef& pageRef, bool16 refresh);

// 上記キャッシュを丸ごと捨てる。呼ぶのは「覚えている値が当てにならなくなる」場面:
//   ・トグル(Ignore Page Number Marker)の切り替え … 切り直せば必ず測り直せる逃げ道になる
//   ・文書クローズ(KESCMHandleDocsClosed) … 閉じた db のエントリを残さない
//   ・Shutdown … 静的コンテナに heap を持ち越さない(KESCM 共通方針)
// ★キーの IDataBase* は照合専用で deref しない(KESCM 共通規約)。
void	KESCMInvalidatePageNumberMarkerRects();

#endif // __KESCMPageNumberMarker_h__
