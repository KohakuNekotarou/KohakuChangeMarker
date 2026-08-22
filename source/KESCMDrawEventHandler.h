//========================================================================================
//
//  KESCMDrawEventHandler.h
//
//  差分オーバーレイの描画エンジン。ページUID→オーバーレイ(KESCMOverlayEntry)を保持し、
//  スプレッド描画イベント時にリング(比較枠)・登録/あふれの斜線・旧ページ番号バッジ・旧版べた載せを
//  描く。共有状態は public static メンバとして公開し、他モジュール(peek/コア処理)から参照させる。
//  (変更数の数字は描かない。changedCells は Prev/Next の割合表示(KESCMChangeNav)だけが使う。)
//
//========================================================================================
#ifndef __KESCMDrawEventHandler_h__
#define __KESCMDrawEventHandler_h__

#include <map>
#include <set>
#include <vector>
#include "KESCMConstants.h"
#include "KESCMOversetScan.h"	// KESCMOversetLoc(sOversetLocs の要素型)
#include "CPMUnknown.h"
#include "IDrwEvtHandler.h"
#include "GraphicsExternal.h"   // AGMImageRecord (構造体メンバ)
#include "UIDRef.h"             // UID / UIDRef
#include "PMReal.h"
#include "IDThreading.h"        // IDThreading::ThreadLocal(下の tl_Rasterizing)
#include "KESCMThreadSafety.h"  // ★KESCMMarkStateMutex/KESCMMarkStateLock(sEntries を delete する側で使う)

class IDataBase;
class IDrwEvtDispatcher;
class IControlView;
class IPanorama;

struct KESCMOverlayEntry
{
	uint8*         buf;			// 自前の ARGB バッファ(リング画像)。所有
	AGMImageRecord rec;			// buf を指す自前の画像レコード(blit 用)
	uint8*         dist;		// 差分マスクのチェスボード距離変換(w*h, uint8, 0=変化画素, clamp255)。所有。
								//   リング = 0<dist<=radius。BuildRing が膨張なしの1パスで塗れる(mask は dist 生成後は破棄)。
	uint8*         bgRed;		// 対象ページが「赤っぽい」画素=1 のマップ(w*h)。リングの青切替に使う。所有(nil可)
	int32          w, h;
	int32          rowBytes;	// buf の行バイト数(= rec.byteWidth)
	int32          bpp;			// バイト/ピクセル
	int32          lastRadius;	// 最後に描いたリング半径(px)。-1=未描画
	int32          changedCells;	// 変化した低解像度セル数(MakeEntry の diffCount)。Prev/Next で飛んだ先の
									//   「変更の割合」表示に使う。★割合の分母(ページ全体のセル数)は持たない
									//   = w * h がそのまま分母なので、同じ値を二重に持たせない。

	KESCMOverlayEntry() : buf(nil), dist(nil), bgRed(nil), w(0), h(0), rowBytes(0), bpp(0), lastRadius(-1),
		changedCells(0)
	{
		rec.baseAddr = nil; rec.decodeArray = nil;
		rec.colorTab.numColors = 0; rec.colorTab.theColors = nil;
	}
	~KESCMOverlayEntry()
	{
		if (buf)   delete[] buf;
		if (dist)  delete[] dist;
		if (bgRed) delete[] bgRed;
	}
};


//========================================================================================
// KESCMOrigImage
//   1ページ分の「旧版(比較相手)」の不透明画像。kescmShowOriginal 実行時にその場で生成して保持し、
//   トグルが ON の間、対応するページの矩形いっぱいに不透明 blit する(べた載せ)。
//   SnapshotUtilsEx/accessor は保持せず、画素を buf へコピーして自前 rec を組む(切り離し=破棄時クラッシュ回避)。
//========================================================================================
struct KESCMOrigImage
{
	uint8*         buf;			// 自前の画像バッファ(不透明)。所有
	AGMImageRecord rec;			// buf を指す自前の画像レコード(blit 用)
	int32          w, h;
	int32          rowBytes;
	int32          bpp;

	KESCMOrigImage() : buf(nil), w(0), h(0), rowBytes(0), bpp(0)
	{
		rec.baseAddr = nil; rec.decodeArray = nil;
		rec.colorTab.numColors = 0; rec.colorTab.theColors = nil;
	}
	~KESCMOrigImage()
	{
		if (buf) delete[] buf;
	}
};


//========================================================================================
// KESCMDrawEventHandler
//   ページUID→オーバーレイの集合を保持し、スプレッド描画時に、そのスプレッドに属する
//   各ページのリングを blit する。リング太さは描画時のズームに追従。非永続=.indd に残らない。
//
// ★★★バックグラウンドスレッド(PDF の非同期書き出し)からも、このハンドラは呼ばれる。
//   2026-08-14 にここへ「第2段(kModelPlugIn 化)の**前に**必ず読め・測る前に本番を回すな」と書いた警告は、
//   **第2段の完了(2026-08-15)で2つとも片付いた**。⚠その後も「これから直す」形のまま残っていたので、
//   2026-08-17 の不具合再検査 B3 で**現状**へ書き換えた。根拠はどちらもガイド vol1-07 Multithreading:
//
//   (1) **BG スレッドが見る DB は「クローンされた別の DB」**
//       ("provides a separate execution context (a cloned copy of the database) for each thread")。
//       ⇒ 描画に渡ってくる db は sDB とは**必ず別ポインタ**になる。
//       ✅**解決済み** = 描画側の同一性判定は **`KESCMIsSameDoc()`**(ファイルで聞く。KESCMThreadSafety.h)。
//         ★同時に [[uidref-reuse-after-close]](閉じた文書のポインタがアドレス再利用で別文書と一致する)も消えた。
//       ⚠**`sOverflowCacheDB` / `sOverflowCacheSrcDB` だけは今も生ポインタ比較で、それが正しい**——
//         あれが比べるのは **static どうし**(sOverflowCacheDB と sDB)なので、どのスレッドから見ても
//         同じ答えになる(理由は .cpp の EnsureOverflowCache)。**「全部 GetSysFile へ寄せる」ではない。**
//
//   (2) **スレッドは object model のインスタンスは共有しないが、static は共有する**
//       ("Threads do not share object-model instances. They do share globals and statics")。
//       ⇒ 下の可変 static は main と BG から同時に触られる。とくに **sEntries は生ポインタの map で
//         DropAll() が delete する**ので、**BG が読んでいる最中に main が Stop すると解放済みメモリを読む**
//         ("InDesign will behave inconsistently and **may randomly crash**" =ガイドの原文)。
//       ✅**解決済み** = マーク集合を触る所は **KESCMMarkStateLock** で守る(DropAll / DropOneEntry /
//         MakeEntry の置換 / HandleDrawEvent の描画2ループ / RebuildOverflowCache の差し替え)。
//         ラスタ化中フラグは**スレッドローカル**(tl_Rasterizing)。
//       ⚠**守る条件は「main が書き、BG が描画で読む」**＝新しい共有状態を足したら、その条件に当てはまるかを
//         必ず数えること。過去に **sSrcPageToTarget と overflow キャッシュの「書き手」が2つとも漏れていた**
//         (捨てる側だけ守られていた。2026-08-16 の API 監査 B3 §5 で是正)。
//
//   ★実測の中身は KESCMThreadSafety.h、経緯は docs/ai-notes/kescm-task12-pdf-export-marks-2026-08-15.md
//     と kescm-bg-clone-db-probe-2026-08-15.md。
//========================================================================================
/** ★★★2026-08-20: **このクラスはもう `IDrwEvtHandler` の実装ではない。**

	マークの描画を**グローバルページアイテムアドーンメント**(KESCMRingAdornment.cpp)に**一本化**し、
	Draw Event の受け口(`HandleDrawEvent` / `Register` / `UnRegister`)と `kKESCMDrawEventServiceBoss`・
	`KESCMDrawEventSrvc` を撤去した(2026-08-20 ユーザー判断＝「登録に失敗していたら枠が出なくてよい」)。

	⚠**それまで残していたのは「アドーンメントの登録に失敗したときのフォールバック」としてで、
	  機能上の役割はゼロだった** ---- 削除した `HandleDrawEvent` の中身は
	  「アドーンメントが生きていれば return / さもなくば `DrawSpreadMarks` を呼ぶ」の2行だけ。
	  ∴ **絵は1つも失われない**(✓チェック・斜線・×・ノンブル塗り・Find Overset の「＋」まで、
	  描くものは全部 `DrawSpreadMarks` の中にある)。

	⇒ ここに残っているのは**描画本体 `DrawSpreadMarks` と、マークの状態を持つ static 群**だけで、
	  **インスタンスは1つも作られない**(可変状態はすべて static だったので、そのまま成り立つ)。
	⚠**クラス名とファイル名は歴史的なもの**。`IDrwEvtHandler` を継承していないので boss には載らない。
	★`DrawEventData` 型だけは今も使う ---- 「今描いているスプレッド + GraphicsData + flags」を
	  1つにまとめて渡す器として都合がよいため(`IDrwEvtHandler.h` が定義している構造体)。 */
class KESCMDrawEventHandler
{
public:
	// ★描画本体。呼び手は**グローバルページアイテムアドーンメント(KESCMRingAdornment.cpp)ただ1つ**で、
	//   スプレッドに対して DrawEventData を組んで呼ばれる。
	//   ⚠渡される changedBy は「今描いているスプレッド」(この関数は changedBy をスプレッドとしか読まない)。
	//   ★static なのはインスタンスの状態を1つも使わないから（この class の可変状態はすべて static メンバ）。
	static bool16 DrawSpreadMarks(DrawEventData* ded);

	// ページUID → オーバーレイ。変化のあったページだけ登録される。
	// ⚠★中身は生ポインタで DropAll() が delete する = BG と main で同時に触ると解放済み読みになる
	//   ⇒ **触る所は KESCMMarkStateLock で守ってある**(冒頭の(2))。新しい呼び手を足すときも同じ。
	static std::map<UID, KESCMOverlayEntry*> sEntries;
	// 全エントリが属する単一ドキュメント。別dbをmarkしたら作り直す(UIDはdb内のみ一意なため)。
	// ⚠★BG には**クローンの別ポインタ**が渡るので、**この値と生ポインタで比べてはいけない**
	//   ⇒ 描画側は `KESCMIsSameDoc(db, sDB)` で聞く(冒頭の(1))。
	static IDataBase* sDB;
	// 上書き表示(変更リング)の master 表示トグル。データ(sEntries)は消さず
	// 表示だけ切り替える。★既定=kFalse(非表示)。シングルツール左ボタンを押している間だけ kTrue にして枠等を
	// 表示し、離すと kFalse に戻す。kFalse の間はこれら全部を描かない。旧版べた載せ(sShowOriginal)は
	// このトグルの影響を受けない(ダブルクリックで別管理)。
	static bool16 sMarksVisible;
	// 画面マーク(リング)に掛ける「実効」不透明度。★既定=1.0。リング blit に掛ける。
	//   ・ツール左hold中 = SelectedMarkOpacity()(パネルで選択中の 25%/75%)
	//   ・押していない常時表示時 = 基準値 KESCMBaseScreenOpacity()(印刷ONなら選択不透明度 / 印刷OFFは1.0)
	static PMReal sMarkScreenOpacity;
	// 変更マーク(リング)を印刷/PDF にも出すか(KESCMDoSetPrintMarks)。★既定=kFalse(画面のみ)。
	// ON の間は、ツール左hold に関係なく画面でも常時表示(WYSIWYG)＋印刷/PDF にも描く。マークデータとは独立に保持。
	static bool16 sPrintMarks;
	// 枠の不透明度の選択(パネルのラジオ「Marks opacity 25% / 75%」)。kTrue=25% / kFalse=75%。★既定=kTrue(25%)。
	// ツール左hold中の画面表示・印刷ON中の常時表示(KESCMBaseScreenOpacity)・印刷/PDF出力(KESCMDrawRingForPrint)の
	// すべてが SelectedMarkOpacity() 経由でこの選択を使う(画面と印刷の見た目を一致)。
	static bool16 sMarkOpacity25;
	// Source(旧文書)側にも枠を出すトグル(フライアウト「Show Marks on Source」のチェック式)。★既定=kFalse
	// だが Start 経路だけが kTrue へ戻す(=Start で既定 ON、OFF にしたければメニューで外す。
	// ★KESCMDoMarkChangesDoc では戻さない=登録トグル/Ignore 切替の再比較でも通る関数のため。2026-07-25 に移動)。
	// ⚠★「Start 経路」＝**KESCMStartComparisonFor**(KESCMComparisonRun.cpp)であって
	//   KESCMToggleStartStop ではない。後者は前者の呼び手2つのうちの1つで、もう1つは**ブック比較の
	//   章行の右クリック「Start Change Marker」**。∴ブック行から始めた比較でも Source 枠は ON に戻る。
	//   (「KESCMToggleStartStop だけ」と書いてあった。2026-08-19 不具合再検査 B-U5 3周目で訂正。
	//    ★手順が1か所に集めてあるからこう書ける＝KESCMComparisonRun.cpp の [[one-question-one-place]])
	// ON の間、Source 文書の対応ページに同じリング画像を「常時」表示する(ツール左hold と無関係)。★既定 OFF で、Start は触らない(2026-08-22 変更。設定はパネル設定に保存され起動時に復元される)。不透明度は
	// パネルの 25%/75% 選択(SelectedMarkOpacity)に連動し、OPP(オーバープリントプレビュー)でも隠さず、
	// 印刷/PDF にも常に出す(Target 側の sPrintMarks とは独立)。
	static bool16 sSrcMarksOn;
	// ★「Show Marks on Target」(2026-08-22 ユーザー要望「ツールでボタンを押さなくても常にマークが出る様に」)。
	//   ON の間、Target 文書のマークを**画面に常時**表示する(ツール左hold と無関係)。上の Source 版と対で、
	//   ★★Story 変更モードでは反転マークが同じトグルで常時表示になる
	//   (ui/KESCMStoryPressMarks.cpp)＝「ピクセルの方もストーリーの方にも」。
	// ⚠**画面だけ**＝Source 版と違い印刷/PDF には出さない。Target 側の出力は「Print comparison marks」
	//   (sPrintMarks)が決める仕様で、こちらが出力に効くとあのトグルの意味が消える。
	static bool16 sTgtMarksOn;
	// Source 文書の db。比較実行(KESCMDoMarkChangesDoc/MakeEntry)時に設定し、DropAll で nil に戻す。
	static IDataBase* sSrcDB;
	// SourceページUID → TargetページUID の対応表。比較は平坦ページ番号どうしの対応なので、Source の
	// スプレッド描画時にこの表→sEntries の順で引けば、同じリング画像を Source ページに重ねられる。
	// MakeEntry がエントリ登録と同時に記録する(=旧 Ctrl+ミドルのスプレッド再比較でも対応が維持される)。
	static std::map<UID, UID> sSrcPageToTarget;
	// 前回の比較で使った TargetページUID → SourceページUID のペアリング(除外対応表の zip 結果)。
	// 登録トグル(比較相手なしページの追加/解除)による再比較を差分化するために保持する:
	// 新旧ペアリングを突き合わせ、ペアが不変のページは MakeEntry を呼ばず前回結果を再利用する
	// (KESCMDoMarkChangesDoc の allowIncremental 経路)。全再比較(Start 等)のたびに丸ごと更新し、
	// DropAll で破棄する。sEntries と違い、変化ゼロのページも含めた「前回比較した全ペア」を持つ点が肝
	// (エントリの有無だけでは不変ページを再利用判定できないため)。
	static std::map<UID, UID> sPrevPairTargetToSource;
	// overflow(登録されていないのに文書間のページ数差で比較相手が無い="/"のページ)集合のキャッシュ。
	// Target側=sOverflowT / Source側=sOverflowS。以前は描画のたびに KESCMBuildPairing(両文書の全ページ
	// 走査)を走らせていたが、比較実行(KESCMDoMarkChangesDoc)時に1回だけ作って保持する。どの (sDB,sSrcDB)
	// 用に作ったかを sOverflowCacheDB/sOverflowCacheSrcDB に控え、描画時に食い違えば(文書切替・スプレッド
	// 再比較で別文書に移った等)EnsureOverflowCache が作り直す。DropAll で破棄。
	// ★生のページ挿入/削除(Start無し)には追従しない=次の Start/再比較まで固定(枠=リングと同じ挙動)。
	static std::set<UID> sOverflowT;
	static std::set<UID> sOverflowS;
	static IDataBase* sOverflowCacheDB;
	static IDataBase* sOverflowCacheSrcDB;
	// 旧ページ番号バッジ(フライアウト「Show Original Page Numbers」のチェック式トグル)。★既定=kFalse。
	// ON の間、枠と同じ可視条件(sPrintMarks ON の常時表示、またはツール左hold中 sMarksVisible)で、番号が
	// ズレているページ(=それより前に隠しスプレッドがある)の下端中央に「隠す前の元の番号」を描く
	// (sPrintMarks ON なら印刷/PDF にも出る)。マークデータ(sEntries)とは独立で、どの文書のスプレッド描画
	// でも番号がズレていれば描く(隠しが無ければ現在番号と一致して何も描かない)。
	static bool16 sShowOldNumbers;

	// (★2026-08-22 に「Hold to Hide Marks」トグル(sAlwaysShowMarks)を撤去した。あれは「枠を常時表示し、
	//  ツール左hold中だけ隠す」で、**前半が「Show Marks on Target」と完全に重複**していた。固有だった
	//  後半＝「押している間だけ隠す」は、**トグル ON のときの標準の挙動**として下の sMarksTempHidden に
	//  畳んである。⇒ 規則は「**押している間は反対になる**」の1本＝OFF なら押下中だけ出る、ON なら
	//  押下中だけ隠れる。ActionID +19 は欠番のまま再利用しない。)

	// 「Show Marks on Target」ON のとき、Target 窓でツール左ボタンを押している間だけ kTrue
	// (常時表示の枠を一時退避)。離すと kFalse。
	// KESCMPeekGesture.cpp のトラッカー(KESCMTrackerRevealBegin/End)が上下させる。トグル OFF の間は
	// 常に kFalse で無影響(そちらは「押下中だけ出す」reveal が動く)。
	// ★これは Target 窓上でツール左ボタンを押したときだけ立てる(押した窓の枠だけ隠す=ウィンドウ別)。
	static bool16 sMarksTempHidden;
	// sMarksTempHidden の Source 版。「Show Marks on Source」ON のとき、
	// Source のレイアウト窓上でツール左ボタンを押している間だけ kTrue(その間だけ Source 側の常時表示枠を画面で隠す)。
	// 印刷は Source 枠を常に出す仕様なので影響しない(描画側で !printing ゲート)。
	static bool16 sSrcMarksTempHidden;

	// ★サムネイル実験トグル(2026-07-06)。kTrue の間、Pagesパネルのサムネイル生成(view無し・kPreviewMode)
	// にも枠を描く(通常は sPrintMarks/sMarksVisible が OFF だと出ないが、サムネイルは isThumb で強制ON・
	// 太めの固定比率半径・不透明100%)。加えて比較後に KESCMTryRefreshPagesPanelThumbnails で既表示分の
	// 再生成を試みる。うまく更新できない/不整合が目立つ場合は、この1フラグを kFalse に戻すだけで従来動作
	// (サムネイルには一切描かない)へ即復帰する。関連: docs memory kescm-pages-panel-thumbnails。
	static bool16 sThumbExperiment;

	// 選択中の枠不透明度(0.25 / 0.75)。枠を描く全経路の単一の供給元。
	static PMReal SelectedMarkOpacity() { return sMarkOpacity25 ? kKESCMMarkOpacity25 : kKESCMMarkOpacity75; }
	// 自前のラスタ化(MakeEntry/MakeOrigImage の SnapshotUtilsEx::Draw)中だけ kTrue。HandleDrawEvent が
	// 再入したらマークを描かない(自己参照防止)。kPreviewMode ビットに頼ると PDF 書き出し(同ビット)を巻き込むため。
	// ★★★2026-08-15(第2段 Task 12B)= **スレッドローカルにした**(旧: 素の static bool16)。
	//   理由 = これは「**このスレッドが今ラスタ化の最中か**」という問いで、スレッドをまたぐと意味が壊れる。
	//   素の static のままだと、メインスレッドが比較でラスタ化している間に**バックグラウンドの
	//   PDF 書き出しがこれを見て「再入だ」と判断し、マークを黙って描かない**(＝出たり出なかったりする)。
	//   ⚠この壊れ方は「たまたま同時に走ったときだけ」出るので、1回の書き出しでは絶対に再現しない。
	//   ★公式の手本 = open/components/incopyfileactions/InCopyDocFileHandler.cpp:261 が
	//     **同じ用途(再入防止)** で `IDThreading::ThreadLocalManagedObject< K2Vector<IDataBase*> >` を使う。
	//     `ThreadLocal<bool16>` 自体も open/includes/architecture/bossrecycler.h:159 に前例がある。
	//     命名の `tl_` プレフィックスも公式に合わせた。
	static IDThreading::ThreadLocal<bool16> tl_Rasterizing;

	// 旧版べた載せ(kescmShowOriginal / kescmHideOriginal)。マーク(sEntries)とは完全に独立。
	// 実行時に覗いたページの旧版画像を sOrigImages に保持し、sShowOriginal が ON の間その db のページに不透明 blit する。
	static std::map<UID, KESCMOrigImage*> sOrigImages;	// ページUID(新) → 旧版画像
	static IDataBase* sOrigDB;							// 旧版画像が属する単一ドキュメント(別dbに切替えたら作り直す)
	static bool16 sShowOriginal;						// べた載せ表示 ON/OFF(既定 OFF)
	static PMReal sOrigScale;							// 旧版画像をラスタ化した時の content→window スケール(ズーム×デバイス倍率)。
														// 再 peek 時にズームが変わっていたら作り直す基準。0=未設定
	static PMReal sPeekOpacity;							// 覗き中(peek)の旧版べた載せの不透明度。Shift+左=1.0(不透明)/
														// Shift+Alt+左=0.5(半透明)。描画ブロックが参照する

	// ★オーバーセットページの十字マーク(フライアウト「Find Overset」)。比較(sEntries)とは完全に独立。
	// アクティブ1文書を走査して overset(あふれ)のあるページ UID を sOversetPages に保持し、sOversetOn の
	// 間その文書(sOversetDB)のスプレッド描画で、該当ページにページいっぱいの赤い「＋」を画面のみ描く
	// (色/太さ/不透明度は変更リング枠と同じ)。sOversetDB は「どの文書を走査したか」の識別用で、描画時は
	// db とのポインタ一致だけを見る(deref しない)=閉じても安全。トグルOFF/クローズ時は sOversetPages を空に。
	static bool16 sOversetOn;			// Find Overset トグル(既定 OFF)
	static IDataBase* sOversetDB;		// 走査した文書(pointer 識別のみ。deref しない)
	static std::set<UID> sOversetPages;	// overset を含むページ UID 集合(Pages パネルの枠/＋・スクロール地図の帯用)
	static std::vector<KESCMOversetLoc> sOversetLocs;	// overset「+」箇所ごとの位置(ページ＋pb点)。Prev/Next の巡回先

	// Find Overset のクリア(トグルOFF / 走査文書クローズ / 別文書切替)。集合を空にしトグルも OFF へ。
	static void DropOverset()
	{
		sOversetOn = kFalse;
		sOversetDB = nil;
		sOversetPages.clear();
		sOversetLocs.clear();
	}

	// (一時トースト機構は 2026-07-04 に撤去。メッセージはパネルのステータス行(KESCMSetStatus)へ。
	//  仕組み自体は他プラグインへの転用候補: docs/ai-notes/kescm-toast-mechanism.md と git 履歴 509e830 を参照)

	// 距離変換 dist を使い、buf(ARGB)へリング(0<dist<=radius)を1パスで描く(膨張不要)。
	// 各リング画素の色は、その位置の背景が赤っぽい(bgRed[idx])なら青、そうでなければ赤。
	// リング以外の画素は透明(alpha=0)。dist は KESCMDistTransform で事前生成(0=変化画素)。
	static void BuildRing(uint8* buf, int32 rb, int32 bpp, int32 wt, int32 ht,
		const uint8* dist, const uint8* bgRed, int32 radius);

	// target/source を高解像度(kKESCMResolution×kKESCMHiResMul)で CMYK ラスタ化し、4ch を比較
	// (しきい値 kKESCMCmykThr)。変化px数>0 のときだけ sEntries[target.UID] にエントリ登録(既存は置換)。
	// changed に「変化したか」を返す。
	static ErrorCode MakeEntry(const UIDRef& targetRef, const UIDRef& sourceRef, bool16& changed);

	// sourceRef(旧)を resolution(dpi)で1枚だけラスタ化し、不透明画像を sOrigImages[target.UID] に
	// 保持(既存は置換)。オフスクリーンは即破棄=同時に1枚しか生存しない(安全)。
	// resolution 既定=kKESCMOrigResolution。peek 経路では現在のズームから dpi=72×スケールを渡して常にくっきり。
	static ErrorCode MakeOrigImage(const UIDRef& targetRef, const UIDRef& sourceRef, const PMReal& resolution = kKESCMOrigResolution);

	// overflow キャッシュ(sOverflowT/sOverflowS)を現在の sDB/sSrcDB から作り直す(KESCMBuildPairing を
	// 1回呼ぶ)。比較実行(KESCMDoMarkChangesDoc)から呼び、Start/登録Add/Ignore切替のたびに最新化する。
	static void RebuildOverflowCache();
	// 控えた (sDB,sSrcDB) が現在と食い違う時だけ RebuildOverflowCache する(文書切替・スプレッド再比較で
	// 別文書へ移った場合の保険)。一致していれば何もしない=描画のたびの全文書走査を避ける。
	static void EnsureOverflowCache();

	// 単一ページのオーバーレイ破棄(インクリメンタル再比較の差分適用で使う)。targetUID のエントリを
	// 消し、その target を指していた Source 側対応表(sSrcPageToTarget[oldSourceUID])も掃除する。
	// エントリ/対応表が無ければ何もしない(不変・変化ゼロページに対しても安全に呼べる)。
	static void DropOneEntry(UID targetUID, UID oldSourceUID)
	{
		// ★2026-08-15(第2段 Task 12B): 描画中のバックグラウンドスレッドが同じエントリを読んでいる
		//   可能性があるので、delete する側はロックを取る(理由は KESCMThreadSafety.h)。
		KESCMMarkStateLock lock(KESCMMarkStateMutex());
		std::map<UID, KESCMOverlayEntry*>::iterator it = sEntries.find(targetUID);
		if (it != sEntries.end()) { delete it->second; sEntries.erase(it); }
		std::map<UID, UID>::iterator sp = sSrcPageToTarget.find(oldSourceUID);
		if (sp != sSrcPageToTarget.end() && sp->second == targetUID)
			sSrcPageToTarget.erase(sp);
	}

	// 全エントリ破棄(kescmClearMarks / 別ドキュメント切替時)。Source 側の対応表と db も一緒に破棄する
	// (トグル sSrcMarksOn 自体は「ユーザーの好み」として保持。エントリが無ければ何も描かないので無害)。
	static void DropAll()
	{
		// ★★★2026-08-15(第2段 Task 12B): **ここが最も危ない場所だった。**
		//   sEntries は生ポインタの map で、ここが delete する。バックグラウンドの PDF 書き出しが
		//   同じエントリを描いている最中に main が Stop すると**解放済みメモリの読み取り**になる
		//   (ガイド vol1-07 L95 "may randomly crash")。描画側(HandleDrawEvent の2つのループ)も
		//   同じロックを取るので、待ち合わせが成立する。
		KESCMMarkStateLock lock(KESCMMarkStateMutex());
		for (std::map<UID, KESCMOverlayEntry*>::iterator it = sEntries.begin(); it != sEntries.end(); ++it)
			delete it->second;
		sEntries.clear();
		sDB = nil;
		sSrcPageToTarget.clear();
		sSrcDB = nil;
		sPrevPairTargetToSource.clear();	// 差分用の前回ペアリングも破棄(次の比較で作り直す)
		sOverflowT.clear();  sOverflowS.clear();		// overflow キャッシュも破棄
		sOverflowCacheDB = nil;  sOverflowCacheSrcDB = nil;
	}

	// 旧版画像を全破棄(kescmClearMarks / 別ドキュメント切替時)。表示トグルもOFFへ。
	//
	// ⚠★★**ここは DropAll と違ってロックを取らない。理由**(2026-08-16・API 監査 B5 で明文化):
	//   sOrigImages も生ポインタの map で、ここが delete する ---- 形は DropAll とまったく同じなのに
	//   ロックが無い、という非対称が説明なしで置かれていた。**読み手がメインスレッドにしか居ないから**
	//   で正しい: 描画側の入口が `wantOrig = !suppressForPrint && !printing && sShowOriginal &&
	//   !sOrigImages.empty()` (HandleDrawEvent)で、**`!printing` が先に立つので BG(PDF 書き出し)は
	//   短絡評価で map に一切触れない**。さらに実際に読む所は `wantOrig && !isThumb` の下にある。
	//   ⇒ 旧版べた載せ(peek)は**画面だけの機能**なので、BG と競合しようがない。
	//   ★**この前提が崩れる変更**＝「peek の絵を印刷/PDF にも出す」「サムネイルにも出す」。
	//     そのときは DropAll と同じく KESCMMarkStateLock をここと MakeOrigImage に入れること。
	static void DropAllOrig()
	{
		for (std::map<UID, KESCMOrigImage*>::iterator it = sOrigImages.begin(); it != sOrigImages.end(); ++it)
			delete it->second;
		sOrigImages.clear();
		sOrigDB = nil;
		sShowOriginal = kFalse;
		sOrigScale = 0.0;
	}
};

// tl_Rasterizing を例外安全に立てる/戻す RAII(2026-07-25 監査で追加)。SnapshotUtilsEx::Draw が万一
// throw(AGM 内部の bad_alloc 等)してもフラグが立ちっぱなし(=以後マーク描画が全抑止)にならない。
// ★2026-08-15: 中身がスレッドローカルになったが、**呼び手は1行も変わらない**
//   ——RAII に包んであったおかげで、スレッド対応の変更がこのクラスの中だけで済んだ。
//   ⚠2026-08-18(不具合再検査 B9)訂正: ここは「呼び手(6か所)」と書いていたが**実測5か所**
//     (KESCMDrawEventHandler.cpp:354/369/707・KESCMColorSampler.cpp:137・KESCMBookCompare.cpp)。
//     ⚠2026-08-19(B-U5 3周目): 最後の1件は `:409` と書いてあったが実体は 398 で**+11 ずれていた**
//       ので行番号を外した(この5件は `KESCMRasterizingGuard` を grep すれば全部出る＝**数は数え直せる
//       が、行番号は黙って嘘になる**)。残る3件+1件は同一ファイル内/未編集ファイルなので当たり。
//
// ★★2026-08-18(不具合再検査 B9): **入れ子にしても壊れない形にした**(直前の値を控えて戻す)。
//   旧実装はデストラクタが**無条件に kFalse** を書いていたので、ガードの中でガードを作ると
//   **内側が終わった時点で外側の保護も消える** ---- そこから先のラスタ化には
//   **自分のマークが写り込む**(＝比較結果が静かに嘘になる。落ちも警告も出ない)。
//   ⚠現状5か所とも Draw だけを包む最小スコープで**入れ子は1つも無い**(B9 で全数確認)。
//     ただし**ブック比較が 2026-08-18 にこのガードを使い始めたばかり**で、あの経路から
//     MakeEntry を呼ぶ日が来ると即座に入れ子になる ---- そのとき何も起きないようにしておく。
//   ★2行で済むのは ThreadLocal::Get() が**未設定なら初期値(kFalse)を返す**と保証されているから
//     (IDThreading.h:107 = PublicThreadLocalStorageGet(fKey, fInitialVal))。
//     ∴ BG スレッドの1回目でも fPrev はゴミにならない。
class KESCMRasterizingGuard
{
public:
	KESCMRasterizingGuard()
		: fPrev(KESCMDrawEventHandler::tl_Rasterizing.Get())
	{
		KESCMDrawEventHandler::tl_Rasterizing.Set(kTrue);
	}
	~KESCMRasterizingGuard() { KESCMDrawEventHandler::tl_Rasterizing.Set(fPrev); }
private:
	bool16 fPrev;
};

// (KESCMQueryPanorama は 2026-08-13 に KESCMViewLookup.h へ移した＝model/UI 分割 第1段 Task 12。
//  戻り値が IPanorama = 窓の問いなので UI 側が持つ。呼び手は #include "KESCMViewLookup.h" へ。)

// 旧ページ番号バッジのフォントキャッシュを解放する(KESCMPeekStartup::Shutdown から呼ぶ。2026-07-25)。
// 実体は KESCMDrawEventHandler.cpp(キャッシュ本体と同居)。
void KESCMReleaseOldNumFontCache();

// ノンブル除外領域の行内判定。x が「この行に掛かる矩形」のどれかに入っているか。
// 文書比較(KESCMDrawEventHandler.cpp の MakeEntry)とブック比較(KESCMBookCompare.cpp の
// CompareRasters)が同じ判定を使う。★2026-08-18 に一本化: それまでは各 .cpp に同じ4行が複製され、
// 「片方を直したら他方も直す」という約束をコメントで守っていた(=守り忘れれば黙って割れる形)。
// ★ヘッダーに inline で置く理由: 呼ばれるのは比較ループの**最内(画素ごと)**なので、
//   呼び出し側の TU でインライン展開できないと遅くなる。extern の1定義に寄せると、
//   複製は消えてもブック比較側だけが実関数呼び出しになる。
// ★引数は「その行に掛かる矩形だけ」に絞り込んだ列(2段ふるいの②)。呼び出し側の絞り込みは
//   KESCMDrawEventHandler.cpp の MakeEntry のコメントを参照。
inline bool16 KESCMXInRowRects(int32 x, const std::vector<const Int32Rect*>& rowRects)
{
	for (size_t i = 0; i < rowRects.size(); ++i)
		if (x >= rowRects[i]->left && x < rowRects[i]->right)
			return kTrue;
	return kFalse;
}

#endif // __KESCMDrawEventHandler_h__
