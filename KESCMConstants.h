//========================================================================================
//
//  KESCMConstants.h
//
//  ChangeMarker (KESCM) のチューニング定数。描画エンジン/peek/色サンプラなど
//  複数のファイルで共有する。すべて static const なので各 TU が自分のコピーを持つ(ODR 問題なし)。
//
//========================================================================================
#ifndef __KESCMConstants_h__
#define __KESCMConstants_h__

#include "BaseType.h"
#include "PMReal.h"

static const PMReal kKESCMRingTargetPx = 8.0;	// リングの目標太さ(画面px)。ズームに依らず一定に見せる
static const uint8 kKESCMRingAlpha = 255;	// リングの基本アルファ(0..255)。「通常」=不透明(255)。薄表示は setopacity 側で行う(25%→255×0.25=実25%)
// 枠(リング＋変更数)の不透明度の二択(パネルのラジオ「Marks opacity 25% / 75%」)。選択値は
// ミドル押下中の画面表示・印刷ON中の常時表示・印刷/PDF出力のすべてに共通で効く(KESCMDrawEventHandler::SelectedMarkOpacity)。
static const PMReal kKESCMMarkOpacity25 = 0.25;	// 「25%」(薄い)
static const PMReal kKESCMMarkOpacity75 = 0.75;	// 「75%」(濃いめ・少し透ける)
// 変化判定: 常に CMYK でラスタ化して比較し、CMYK 4ch のどれかがこのしきい値を超えて違えば「変化」とする。
// しきい値=0 は「どんな差も拾う」(=CMYK 1単位でも検出)。CMYK の微差は RGB へ変換すると丸めで消えるため、
// CMYK のまま比較するのが要点(ユーザーは CMYK 数値で考える)。画像/効果の再描画ゆらぎでノイズが出るなら 1〜2 に上げる。
static const int   kKESCMCmykThr = 0;
static const int32 kKESCMBaseRadius = 4;	// リング初期半径(画像px)。描画時にズームから再算出するための初期値
static const PMReal kKESCMResolution = 72.0;	// 保存・表示のラスタ解像度(dpi)。リング画像/マスクはこの解像度で持つ(軽い)
// 【取りこぼし防止】比較だけ高解像度で行い、結果を低解像度に圧縮(マックスプーリング)して記憶する。
// 比較解像度 = kKESCMResolution × kKESCMHiResMul。低解像度では平均化で消える細線/微小ズレを満額で拾う。
static const PMReal kKESCMHiResMul    = 2.0;	// 比較解像度の倍率(2=144dpi)。上げるほど検出力↑/一時メモリ↑。300dpi 相当なら≒4.17
static const int32  kKESCMPoolMinCount = 1;	// プーリング: 低解像度1セル内の「高解像度の変化画素数」がこの値以上で変化と判定。
											// 1=最高感度(縁ノイズも拾う)/大きいほどノイズ耐性↑(取りこぼしのリスクも僅かに増)

// リング色: 通常は赤。ただし枠の下の実ページが「赤っぽい」画素の上では、半透明の赤枠が背景に埋もれて
// 見えなくなるため、視認性確保のためにシアンへ切り替える(画素単位)。シアン=赤の補色(色相180°反対)で、
// 明るさも高い(輝度≒0.79)ため赤上で明暗・色相とも最大コントラスト。純青は暗く細線で沈むため不採用。
static const uint8 kKESCMRingR = 255, kKESCMRingG = 0,   kKESCMRingB = 0;		// 通常(赤)
static const uint8 kKESCMRingAltR = 0,   kKESCMRingAltG = 255, kKESCMRingAltB = 255;	// 赤背景の上(シアン=赤の補色)
static const int   kKESCMRedBgDom = 25;	// 背景を「赤っぽい」と判定する R 優位の閾値(R が G,B の双方より これ以上大きい)。小さいほどピンク/薄い赤も拾う

// 旧版べた載せ(kescmShowOriginal)で重ねる画像の解像度(dpi)。スクリプト実行時に、対象ページの旧版を
// この解像度で1枚だけラスタ化(オフスクリーン1枚=即破棄)し、不透明でページ矩形いっぱいに重ねる。
// 高いほど鮮明・メモリ大(A4・300dpi で約26〜35MB/ページ)。覗いたページの分だけ保持する。
// 72dpi = 標準(非HiDPI)100%拡大でドキュメント1inch=72px と1:1一致する値。メモリ最軽量(A4で約2MB/頁)・
// 押下時のラスタ化も最速。2x HiDPIで拡大して粗ければ 144 へ戻す。
static const PMReal kKESCMOrigResolution = 72.0;

// (一時トーストの定数は 2026-07-04 撤去。転用時は docs/ai-notes/kescm-toast-mechanism.md 参照)

// クリック点 CMYK サンプリング(Shift＋Ctrl＋Alt＋ミドル)。クリック周りの極小領域だけを高dpi・CMYK で
// ラスタ化し、中心1画素の生値(0..255)を新・旧で読む。AA は OFF(ベクター縁の中間色を避ける)。
static const PMReal kKESCMSampleDpi    = 300.0;	// サンプリングのラスタ解像度(dpi)
static const PMReal kKESCMSampleHalfPt = 1.0;	// サンプル領域の半幅(pt)。300dpi で約2pt四方≒8px→中心1画素を読む

// 旧ページ番号バッジ(フライアウト「Show Original Page Numbers」)。スプレッドを隠すと「現在のページ番号」
// マーカーが隠し分を飛ばして振り直されるため、枠の可視条件と同じとき(印刷マークONの常時表示/ミドル押下中)に
// 「隠す前の元の番号」をページ下端中央へ描く(印刷マークONなら印刷/PDF にも出る)。
// サイズはズーム非依存(印刷時は実効スケール1.0固定=そのまま pt)。
// 見た目=トースト風: 白い四角の塗りの上に赤の太字。バッジ全体の不透明度はパネルの「Marks opacity
// 25% / 75%」選択に連動(枠と同じ SelectedMarkOpacity() を使う=画面と印刷で一致)。
static const PMReal kKESCMOldNumFontPx   = 42.0;	// 文字サイズ(画面px/印刷pt)。当初14の3倍
static const PMReal kKESCMOldNumMarginPx = 6.0;		// ページ下端から文字下端までの余白(画面px/印刷pt)
// 文字色: 赤(比較マークの枠と同じ)。
static const PMReal kKESCMOldNumR = 1.0, kKESCMOldNumG = 0.0, kKESCMOldNumB = 0.0;
// 疑似ボールド: 既定フォントのまま、中心+8方向の計9回重ね描きでストロークを太らせる(オフセット=文字サイズ比)。
// 既定フォントのボールド変種を名前で探すのはフォント環境依存で壊れやすいため、重ね描き方式にする。
static const PMReal kKESCMOldNumBoldEm = 0.025;	// 重ね描きオフセット(em比)。0.04→0.025 へ少し細め(2026-07-04ユーザー指定)
static const PMReal kKESCMOldNumPadEm  = 0.20;		// 白い四角の塗りの余白(em比、文字の周囲に付く)

#endif // __KESCMConstants_h__
