//========================================================================================
//
//  KESCMDrawEventHandler.h
//
//  差分オーバーレイの描画エンジン。ページUID→オーバーレイ(KESCMOverlayEntry)を保持し、
//  スプレッド描画イベント時にリング/変更数/旧版べた載せを描く。共有状態は public static
//  メンバとして公開し、他モジュール(peek/コア処理)から参照させる。
//
//========================================================================================
#ifndef __KESCMDrawEventHandler_h__
#define __KESCMDrawEventHandler_h__

#include <map>
#include <set>
#include "KESCMConstants.h"
#include "CPMUnknown.h"
#include "IDrwEvtHandler.h"
#include "GraphicsExternal.h"   // AGMImageRecord (構造体メンバ)
#include "UIDRef.h"             // UID / UIDRef
#include "PMString.h"
#include "PMReal.h"

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

	KESCMOverlayEntry() : buf(nil), dist(nil), bgRed(nil), w(0), h(0), rowBytes(0), bpp(0), lastRadius(-1)
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
//========================================================================================
class KESCMDrawEventHandler : public CPMUnknown<IDrwEvtHandler>
{
public:
	KESCMDrawEventHandler(IPMUnknown* boss) : CPMUnknown<IDrwEvtHandler>(boss) {}
	~KESCMDrawEventHandler() {}

	virtual void Register(IDrwEvtDispatcher* d);
	virtual void UnRegister(IDrwEvtDispatcher* d);
	virtual bool16 HandleDrawEvent(ClassID eventID, void* eventData);

	// ページUID → オーバーレイ。変化のあったページだけ登録される。
	static std::map<UID, KESCMOverlayEntry*> sEntries;
	// 全エントリが属する単一ドキュメント。別dbをmarkしたら作り直す(UIDはdb内のみ一意なため)。
	static IDataBase* sDB;
	// 上書き表示(変更リング)の master 表示トグル。データ(sEntries)は消さず
	// 表示だけ切り替える。★既定=kFalse(非表示)。シングルツール左ボタンを押している間だけ kTrue にして枠等を
	// 表示し、離すと kFalse に戻す。kFalse の間はこれら全部を描かない。旧版べた載せ(sShowOriginal)は
	// このトグルの影響を受けない(ダブルクリックで別管理)。
	static bool16 sMarksVisible;
	// 画面マーク(リング＋変更数)に掛ける「実効」不透明度。★既定=1.0。リング blit と数字 show の双方に同率。
	//   ・ツール左hold中 = SelectedMarkOpacity()(パネルで選択中の 25%/75%)
	//   ・押していない常時表示時 = 基準値 KESCMBaseScreenOpacity()(印刷ONなら選択不透明度 / 印刷OFFは1.0)
	static PMReal sMarkScreenOpacity;
	// 変更マーク(リング＋変更数)を印刷/PDF にも出すか(KESCMDoSetPrintMarks)。★既定=kFalse(画面のみ)。
	// ON の間は、ツール左hold に関係なく画面でも常時表示(WYSIWYG)＋印刷/PDF にも描く。マークデータとは独立に保持。
	static bool16 sPrintMarks;
	// 枠の不透明度の選択(パネルのラジオ「Marks opacity 25% / 75%」)。kTrue=25% / kFalse=75%。★既定=kTrue(25%)。
	// ツール左hold中の画面表示・印刷ON中の常時表示(KESCMBaseScreenOpacity)・印刷/PDF出力(KESCMDrawRingForPrint)の
	// すべてが SelectedMarkOpacity() 経由でこの選択を使う(画面と印刷の見た目を一致)。
	static bool16 sMarkOpacity25;
	// Source(旧文書)側にも枠を出すトグル(フライアウト「Show Marks on Source」のチェック式)。★既定=kFalse
	// だが Start(KESCMDoMarkChangesDoc)のたびに kTrue へ戻す(=Start で既定 ON、OFF にしたければメニューで外す)。
	// ON の間、Source 文書の対応ページに同じリング画像を「常時」表示する(ツール左hold と無関係)。不透明度は
	// パネルの 25%/75% 選択(SelectedMarkOpacity)に連動し、OPP(オーバープリントプレビュー)でも隠さず、
	// 印刷/PDF にも常に出す(Target 側の sPrintMarks とは独立)。
	static bool16 sSrcMarksOn;
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

	// 「Hold to Hide Marks」トグル(フライアウトのチェック式。枠表示の極性反転)。★既定=kFalse。
	// ON の間、画面では枠(リング＋変更数)を「常時」表示し、ツール左hold中だけ sMarksTempHidden で
	// 一時的に隠す(離すと戻る)=既定動作(非表示・押下中だけ表示)の逆。画面のみの挙動で、印刷/PDF への
	// 出力は sPrintMarks が独立して決める(下の wantMarks では !printing のときだけ効かせる)。不透明度は
	// パネル選択の 25%/75%(KESCMBaseScreenOpacity が sAlwaysShowMarks ON も選択不透明度を返す)。
	static bool16 sAlwaysShowMarks;
	// Hold to Hide Marks モード中、ツール左ボタンを押している間だけ kTrue(常時表示の枠を一時退避)。離すと kFalse。
	// KESCMPeek.cpp のトラッカー(KESCMTrackerRevealBegin/End)が上下させる。モード OFF の間は常に kFalse で無影響。
	// ★これは Target 窓上でツール左ボタンを押したときだけ立てる(押した窓の枠だけ隠す=ウィンドウ別)。
	static bool16 sMarksTempHidden;
	// sMarksTempHidden の Source 版。「Show Marks on Source」ON かつ「Hold to Hide Marks」ON のとき、
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
	static bool16 sRasterizing;

	// 旧版べた載せ(kescmShowOriginal / kescmHideOriginal)。マーク(sEntries)とは完全に独立。
	// 実行時に覗いたページの旧版画像を sOrigImages に保持し、sShowOriginal が ON の間その db のページに不透明 blit する。
	static std::map<UID, KESCMOrigImage*> sOrigImages;	// ページUID(新) → 旧版画像
	static IDataBase* sOrigDB;							// 旧版画像が属する単一ドキュメント(別dbに切替えたら作り直す)
	static bool16 sShowOriginal;						// べた載せ表示 ON/OFF(既定 OFF)
	static PMReal sOrigScale;							// 旧版画像をラスタ化した時の content→window スケール(ズーム×デバイス倍率)。
														// 再 peek 時にズームが変わっていたら作り直す基準。0=未設定
	static PMReal sPeekOpacity;							// 覗き中(peek)の旧版べた載せの不透明度。Shift+左=1.0(不透明)/
														// Shift+Alt+左=0.5(半透明)。描画ブロックが参照する

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

// IControlView から可視範囲(IPanorama)を辿る小ヘルパ。エンジンと peek の双方で使うため公開する。
IPanorama* KESCMQueryPanorama(IControlView* view);

#endif // __KESCMDrawEventHandler_h__
