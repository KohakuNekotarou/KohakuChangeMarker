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
// ★サムネイル実験(2026-07-06): Pagesパネルのサムネイル生成時(view無し・sxr=0)の枠太さは、ズーム式が
// 使えないため画像幅に対する固定比率で決める。半径 = 画像幅 / この除数(小さいほど太い)。サムネイルは
// 極小表示なので視認性優先で太めにする。8 ≒ 画像幅の 12.5%。見づらければ 6、太すぎれば 10〜12 に。
static const int32 kKESCMThumbRingDivisor = 8;
// ★サムネイルのページ単位マークの太さ。ズーム式が使えないサムネイル(sxr<=0)では、ページ短辺 / 除数 を
// 太さ(pt)にする(小さいほど太い)。極小表示で潰れないよう太め。枠と「/」で見え方が違う(枠は4辺に分散する
// ので同じ太さでも細く見える)ため除数を分けている。
// ・枠(変更ページの赤枠): 短辺 / 6 ≒ 短辺の 16.7%。細ければ 5、太すぎれば 7〜8。
static const int32 kKESCMThumbBorderDivisor = 6;
// ・「/」斜線(Add/Removeの緑・溢れの赤): 短辺 / 10 ≒ 10%。従来値のまま(見え方OK)。
static const int32 kKESCMThumbDiagDivisor = 10;
// ★サムネイルの枠/「/」の不透明度。従来は 1.0(不透明)固定だったが、少し透けて下のページが見える方が
// 好ましいとの判断で 0.75(=透明25%)へ(画面マークの75%ラジオと同じ濃さ)。極小表示で沈まない範囲。
static const PMReal kKESCMThumbMarkOpacity = 0.75;
// ★Find Overset の「＋」(Pages パネルのサムネイル専用)の不透明度。赤＋白縁をくっきり見せたいので
// 枠/「/」(0.75)より濃く不透明(1.0)にする(2026-07-24 ユーザー指定=カンバスには出さずページパネルにだけ出す)。
static const PMReal kKESCMOversetCrossOpacity = 1.0;
// ★Find Overset の「＋」の腕の長さ(ページ中央からの片側長 ÷ ページ短辺)。縦横とも同じ長さにするための
// 基準。0.5=短辺いっぱい(=横は幅いっぱい)。0.20 で横は幅の 40% とさらに短くし、縦も同じ長さにする
// (2026-07-24 ユーザー指定=縦横同じ長さ・さらに短く。0.40→0.30→0.20)。
static const PMReal kKESCMOversetCrossHalfRatio = 0.20;
// ★Find Overset の「＋」の赤線の太さ(ページ短辺 ÷ この値。小さいほど太い)。白縁はこの 2.2 倍。
// 「/」の除数(kKESCMThumbDiagDivisor=10)より太く=8(2026-07-24 ユーザー指定=線を少し太く)。
static const int32 kKESCMOversetCrossWidthDivisor = 8;
static const uint8 kKESCMRingAlpha = 255;	// リングの基本アルファ(0..255)。「通常」=不透明(255)。薄表示は setopacity 側で行う(25%→255×0.25=実25%)
// 枠(リング＋変更数)の不透明度の二択(パネルのラジオ「Marks opacity 25% / 75%」)。選択値は
// ツール左hold中の画面表示・印刷ON中の常時表示・印刷/PDF出力のすべてに共通で効く(KESCMDrawEventHandler::SelectedMarkOpacity)。
static const PMReal kKESCMMarkOpacity25 = 0.25;	// 「25%」(薄い)
static const PMReal kKESCMMarkOpacity75 = 0.75;	// 「75%」(濃いめ・少し透ける)
// 変化判定: 常に CMYK でラスタ化して比較し、CMYK 4ch のどれかがこのしきい値を超えて違えば「変化」とする。
// しきい値=0 は「どんな差も拾う」(=CMYK 1単位でも検出)。CMYK の微差は RGB へ変換すると丸めで消えるため、
// CMYK のまま比較するのが要点(ユーザーは CMYK 数値で考える)。画像/効果の再描画ゆらぎでノイズが出るなら 1〜2 に上げる。
static const int   kKESCMCmykThr = 0;
static const int32 kKESCMBaseRadius = 4;	// リング初期半径(画像px)。描画時にズームから再算出するための初期値
static const PMReal kKESCMResolution = 36.0;	// 保存・表示のラスタ解像度(dpi)。リング画像/マスクはこの解像度で持つ。
												// ★2026-07-04 72→36 へ変更(メモリ約1/4: A4の変更ページ1枚 約3MB→約0.77MB)。
												// 代償=マスク1セルが1pt→2pt角になり枠の輪郭が粗くなる(特にズームイン時と印刷)。
// 【取りこぼし防止】比較だけ高解像度で行い、結果を低解像度に圧縮(マックスプーリング)して記憶する。
// 比較解像度 = kKESCMResolution × kKESCMHiResMul。低解像度では平均化で消える細線/微小ズレを満額で拾う。
static const PMReal kKESCMHiResMul    = 4.0;	// 比較解像度の倍率。★保存36dpi化に合わせ 2.0→4.0(36×4=144dpi で検出力は従来と同一)。
												// 比較時の一時メモリも従来(144dpi)と同じ
static const int32  kKESCMPoolMinCount = 1;	// プーリング: 低解像度1セル内の「高解像度の変化画素数」がこの値以上で変化と判定。
											// 1=最高感度(縁ノイズも拾う)/大きいほどノイズ耐性↑(取りこぼしのリスクも僅かに増)

// ★比較の進捗バー(TaskProgressBar)を出す下限ページ数。これから実際にラスタ化する枚数がこの値以上の
// ときだけバーを出す(ユーザー指定 2026-07-27: 10 ページ以上)。
// ★★しきい値を自前で持つ理由: TaskProgressBar の showImmediate=kFalse(既定)は「時間がかかったら
//   自動で出す」ではなく「出さない」で、100 ページの比較でもバーが現れなかった(2026-07-27 実機)。
//   ProgressBar.h に遅延表示の記述は無く、実機で出る実例(本家 linksui / KBS)はすべて kTrue。
// 使う側: Start の全/差分比較(KESCMCore.cpp)と、Pages パネル選択ページの Refresh(KESCMPeek.cpp)。
static const int32  kKESCMProgressBarMinPages = 10;

// リング色: 通常は赤。ただし枠の下の実ページが「赤っぽい」画素の上では、半透明の赤枠が背景に埋もれて
// 見えなくなるため、視認性確保のためにシアンへ切り替える(画素単位)。シアン=赤の補色(色相180°反対)で、
// 明るさも高い(輝度≒0.79)ため赤上で明暗・色相とも最大コントラスト。純青は暗く細線で沈むため不採用。
static const uint8 kKESCMRingR = 255, kKESCMRingG = 0,   kKESCMRingB = 0;		// 通常(赤)
static const uint8 kKESCMRingAltR = 0,   kKESCMRingAltG = 255, kKESCMRingAltB = 255;	// 赤背景の上(シアン=赤の補色)
static const int   kKESCMRedBgDom = 25;	// 背景を「赤っぽい」と判定する R 優位の閾値(R が G,B の双方より これ以上大きい)。小さいほどピンク/薄い赤も拾う

// 登録済み(比較相手なし="Added"/"Removed")ページの縁枠色。通常の変更マーク(赤/シアン)と区別する
// ため緑固定(背景色による切り替えは無し。ラスタ差分が無く背景判定の材料も無いため)。
static const uint8 kKESCMAddedBorderR = 0, kKESCMAddedBorderG = 200, kKESCMAddedBorderB = 0;

// 「KESCM: Check」でチェックしたページに描く ✓ マークの色(青)。Pages パネルのサムネイル中央と、
// レイアウトビューのページ中央(2026-07-12 追加)の両方で同色。
// ★フォントの ✓ 文字(環境依存)ではなく、線2本(moveto/lineto/stroke)でベクターの ✓ 型を描くので
//   フォント/OS/ロケールに依存しない。緑「/」(登録)や赤「/」(overflow)と色で区別するため青にする。
static const uint8 kKESCMCheckR = 30, kKESCMCheckG = 110, kKESCMCheckB = 235;
// ✓ のレイアウトビュー版(2026-07-12)。ページ中央に「かなり大きく」描く: サイズ=ページ短辺のこの比率
// (サムネイルの 0.52 より大幅に大きい)。線の太さ=✓ サイズのこの比率(ページ比例=ズームしても印刷しても
// 相似形。枠リングの「画面px固定」式とは別方式)。不透明度はパネルの 25%/75% 選択(SelectedMarkOpacity)連動。
static const PMReal kKESCMCheckLayoutSizeRatio   = 0.80;	// ✓ 全体サイズ(ページ短辺比)
static const PMReal kKESCMCheckLayoutStrokeRatio = 0.12;	// 線の太さ(✓ サイズ比)

// ノンブル(自動ページ番号)除外領域を可視化するベタ塗り色と不透明度。除外トグルON時、比較から
// 外している矩形を半透明の緑で塗り、「どこが除外されているか」を目視できるようにする(下のノンブルが
// 透ける程度の薄さ)。塗りはベクター矩形+setopacity なので画面・印刷とも正しく半透明合成される。
static const uint8  kKESCMExcludeFillR = 0, kKESCMExcludeFillG = 200, kKESCMExcludeFillB = 0;
static const PMReal kKESCMExcludeFillOpacity = 0.35;	// 除外領域ベタ塗りの不透明度(0〜1)

// 旧版べた載せ(kescmShowOriginal)で重ねる画像の解像度(dpi)。スクリプト実行時に、対象ページの旧版を
// この解像度で1枚だけラスタ化(オフスクリーン1枚=即破棄)し、不透明でページ矩形いっぱいに重ねる。
// 高いほど鮮明・メモリ大(A4・300dpi で約26〜35MB/ページ)。覗いたページの分だけ保持する。
// 72dpi = 標準(非HiDPI)100%拡大でドキュメント1inch=72px と1:1一致する値。メモリ最軽量(A4で約2MB/頁)・
// 押下時のラスタ化も最速。2x HiDPIで拡大して粗ければ 144 へ戻す。
static const PMReal kKESCMOrigResolution = 72.0;

// (一時トーストの定数は 2026-07-04 撤去。転用時は docs/ai-notes/kescm-toast-mechanism.md 参照)

// クリック点 CMYK サンプリング(旧 Shift＋Ctrl＋Alt＋ミドル)。クリック周りの極小領域だけを高dpi・CMYK で
// ラスタ化し、中心1画素の生値(0..255)を新・旧で読む。AA は OFF(ベクター縁の中間色を避ける)。
static const PMReal kKESCMSampleDpi    = 300.0;	// サンプリングのラスタ解像度(dpi)
static const PMReal kKESCMSampleHalfPt = 1.0;	// サンプル領域の半幅(pt)。300dpi で約2pt四方≒8px→中心1画素を読む

// ★CMYK カーソル設置後の「落ち着き待ち」(ミリ秒)。カーソルを ICursorMgr::Hide() で隠したまま設置し、
// この時間だけ待ってから Show() する(実装=KESCMTracker.cpp の BeginTracking)。0=待たない(既定)。
//   経緯(2026-07-25 の切り分け。すべて実機確認): 押下時に間欠的に出る「ゴミ」(数値が出る前に一瞬)は、
//   カーソル設置が完成した絵を出す前に一瞬だけ別の絵を出すために起きる。ハードウェアカーソルはアプリと
//   独立に OS が合成するので、その1フレームがそのまま画面に出る。切り分け結果:
//     ・Hide/Show 無し(切替を1msに短縮しても)                → ゴミ出る
//     ・Hide→[基底 BeginTracking+設置]→即 Show(待ち 0)      → ゴミ出る
//     ・Hide→[基底 BeginTracking+設置]→60ms 待つ→Show      → ゴミ出ない
//     ・(旧実装) Hide→[基底]→重いサンプリング→設置→Show    → ゴミ出ない
//   =「隠す」だけでは足りず、隠している区間の中に**実時間の隙間**が要る(旧実装ではサンプリングの時間が
//     偶然その隙間になっていた)。設置後の非同期な後始末が流れきるまでの時間、と解釈するのが辻褄が合う。
//     ブロッキングの待ちで効くので、待つ主体はアプリ側の処理ではなく OS/カーソルマネージャ側。
// 調整: この値だけが「押下してからカーソルが見えるまでの消失時間」を決める(重いサンプリングは待ちの外で
//       済ませてある)。ゴミが出るなら増やす / 点滅が気になるなら減らす。
//       実測(コールバック✓カーソル時代): 0=出る / 30=出ない / 60=出ない。
// ★2026-07-25 後半: 常時✓カーソルを PNG リソース化して発生源(基底のモーダル取得によるコールバック
//   再実行)を断ったので、待ちは不要になったはず=0 に戻して検証中。ゴミが出るなら 30 に戻す
//   (30 は実機で消えることを確認済み)。
static const int32 kKESCMCursorSettleMillis = 0;

// 旧ページ番号バッジ(フライアウト「Show Original Page Numbers」)。スプレッドを隠すと「現在のページ番号」
// マーカーが隠し分を飛ばして振り直されるため、枠の可視条件と同じとき(印刷マークONの常時表示/ツール左hold中)に
// 「隠す前の元の番号」をページ下端中央へ描く(印刷マークONなら印刷/PDF にも出る)。
// ★サイズはドキュメント拡大率50%相当で固定(ズーム/印刷に依存せず、ページに対して一定の大きさ。
//   ユーザー指定 2026-07-15)。★セクションプレフィックスは付けない=番号のみ。★背景の白塗りは無し。
// 見た目: 白フチ+青文字(背景なし)。バッジ全体の不透明度はパネルの「Marks opacity 25% / 75%」選択に
// 連動(枠と同じ SelectedMarkOpacity() を使う=画面と印刷で一致)。
static const PMReal kKESCMOldNumFontPx    = 42.0;	// 文字サイズの基準(px)。実サイズは /kKESCMOldNumFixedZoom
static const PMReal kKESCMOldNumMarginPx  = 6.0;	// ページ下端から文字下端までの余白の基準(同上)
static const PMReal kKESCMOldNumFixedZoom = 0.5;	// ★固定拡大率(ドキュメント50%相当)。sxr の代わりに使う=ズーム/印刷非依存
// 文字色: 黒(白フチとの組で明暗どちらの下地でも読める。ユーザー指定 2026-07-15、青→黒に変更)。
static const PMReal kKESCMOldNumR = 0.0, kKESCMOldNumG = 0.0, kKESCMOldNumB = 0.0;
// 白フチ(ハロー): 本体の前に白を中心±で8方向にずらして描き、縁取りにする(オフセット=文字サイズ比)。
static const PMReal kKESCMOldNumHaloEm = 0.06;	// 白フチの太さ(em比)
static const PMReal kKESCMOldNumPadEm  = 0.20;	// 透明グループ bbox の余白(em比。白フチのはみ出しを含む)

// パネル半透明トグル(フライアウト「Translucent Panel」)の alpha 値(0=完全透明 255=不透明)。
// 128 ≒ 50%(ユーザー指定 2026-07-29)。段階指定やスライダーは作らないので、濃さを変えたいときは
// ここ1箇所を書き換える。★Windows 専用(実体 KESCMPanelAlpha.cpp)。
static const uint8 kKESCMPanelAlphaValue = 128;
// ★通知を受けた「あと」にもう一度貼り直す回数と間隔(2026-07-29 実測で追加)。
//   kPaletteVisibilityChangedMessage を受けた時点で alpha を書いても、その直後に InDesign が
//   トップレベル窓を作り直すことがあり、書いた値ごと捨てられる(診断値 rb=128 ＝書けているのに、
//   外から測ると 255。しかも適用先 HWND と、そのとき実在した窓の HWND が別物だった)。
//   → 窓が落ち着くまでアイドルごとに数回だけ貼り直す。★0 にすれば遅延再適用を止められる。
//   ★回数×間隔は実測で決めた(2026-07-29、work/panel-alpha-watch2.txt)。窓の交代を 40ms 間隔で
//     追跡したところ、アイコン化(クリック一発)では 3ms の追いかけで間に合ったが、フローティング化
//     (ドラッグして離す)では間に合わず 255 のままだった。ドラッグ操作は通知から窓確定までの時間差が
//     桁違いに大きいため、合計 400ms ほど追いかける。
static const int32  kKESCMPanelAlphaReapplyTries       = 8;
static const uint32 kKESCMPanelAlphaReapplyDelayMillis = 50;	// 50ms × 8 回 ＝ 約 400ms 追いかける
// (パネルの影(OWL.ShadowView)は alpha ではなく表示/非表示で制御する。影は per-pixel alpha で
//  描かれていて一様 alpha と排他のため、alpha を一度設定すると OFF に戻しても元の影に復帰しない
//  =2026-07-29 に実機で確認。詳細は KESCMPanelAlpha.cpp のコメント。よって濃さの定数は持たない)

#endif // __KESCMConstants_h__
