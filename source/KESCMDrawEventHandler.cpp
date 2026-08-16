//========================================================================================
//
//  KESCMDrawEventHandler.cpp
//
//  差分オーバーレイ描画エンジンの実装(旧 KESCMScriptProvider.cpp から分離)。リング/変更数/
//  旧版べた載せの描画、比較ラスタ化(MakeEntry/MakeOrigImage)、各種画像ヘルパを持つ。
//  共有状態(static メンバ)と KESCMQueryPanorama は KESCMDrawEventHandler.h で公開している。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// オブジェクトモデル / 描画 / ラスタ化(エンジンが使う SDK ヘッダ):
#include "PersistUtils.h"
#include "IDataBase.h"
#include "IGeometry.h"
#include "IApplication.h"
#include "IDocumentList.h"
#include "ISpread.h"
#include "IShape.h"
#include "IDrwEvtHandler.h"
#include "IDrwEvtDispatcher.h"
#include "CServiceProvider.h"
#include "DocumentContextID.h"
#include "GraphicsID.h"
#include "GraphicsData.h"
#include "IGraphicsPort.h"
#include "AutoGSave.h"
#include "IControlView.h"
#include "IPanorama.h"
#include "IWidgetParent.h"
#include "ISession.h"
#include "PMMatrix.h"
#include "PMReal.h"
#include "PMString.h"			// GetPageString の受け(旧: KESCMDrawEventHandler.h 経由の間接include に依存していた)
// (★TransformUtils.h の include は 2026-08-16 の API 監査 B3・A-1 で外した＝このファイルが使っていた
//  ::InnerToSpreadMatrix が 8箇所とも IGeometryFacade::GetItemBounds へ移り、参照がゼロになったため。)
#include "IGeometryFacade.h"	// ★GetItemBounds(ページの箱を spread 座標で。手本=snapshot/SnapTracker.cpp:621)
#include "SnapshotUtilsEx.h"
#include "AGMImageAccessor.h"
#include "GraphicsExternal.h"
#include "IXPUtils.h"
#include "IXPManager.h"				// ★GetDocumentBlendingSpace / ReleaseBlendingSpace(PDF 書き出しの透明グループ)
#include "IViewPortAttributes.h"		// ★kPDFExportVPAttr / kPDFIsFlattenerTargetVPAttr を GetAttr で聞く
#include "PDFID.h"					// ★同上の ViewPortAttr ID(PDFID.h:1543-1544)
#include "IPDFLibraryUtilsPublic.h"	// ★IsPDFExportPort(PDF 書き出しポートの判別。KESCMIsPDFExportPort)


// 旧ページ番号バッジ(Show Original Page Numbers)用:
#include "IPageList.h"			// GetPageString(..., bIncludePagesOfHiddenSpread) — kTrue=元の番号 / kFalse=隠し反映後の現在番号
#include "IFontMgr.h"			// 既定フォント取得(framelabel の FrmLblAdornment.cpp と同じ流儀)
#include "IPMFont.h"
#include "IFontInstance.h"		// MeasureWText(中央揃えの幅測定) / GetDescent

#include <map>
#include <set>
#include <new>			// std::nothrow(画像バッファ確保。MSVC の通常 new は失敗時 nil でなく throw のため)
#include <string.h>

// プロジェクト内インクルード:
#include "KESCMID.h"
#include "KESCMCore.h"               // KESCMHandleDocsClosed(クローズ検知の後始末を一本化)
#include "KESCMPageMap.h"            // KESCMPageMapIsRegistered/KESCMPageMapHasAnyRegistered(追加/削除ページ縁枠)
#include "KESCMPageCheck.h"          // KESCMPageCheckIsChecked/KESCMPageCheckHasAny(「KCM: Check」の✓)
#include "KESCMPageNumberMarker.h"   // KESCMGetIgnorePageNumberMarker/KESCMAppendPageNumberMarkerRects(ノンブル除外)
#include "KESCMThreadSafety.h"       // ★KESCMIsSameDoc(BG のクローン DB)/KESCMIsMainThread/マーク集合のロック
// (★KESCMScrollMap.h の include は 2026-08-13 Task 7 で外した＝下の理由と同じ)
// (★押下中 HUD は 2026-08-13 に **UI 側の描画サービス** KESCMUIDrawEvent.cpp へ移した
//  ＝model/UI 分割 第1段 Task 6。押下中かどうかはツール(UI)の状態で、model からは見えないため。
//  ⇒ このファイルは KESCMTrackerHud.h を include しない。)
#include "KESCMDrawEventHandler.h"

CREATE_PMINTERFACE(KESCMDrawEventHandler, kKESCMDrawEventHandlerImpl)

std::map<UID, KESCMOverlayEntry*> KESCMDrawEventHandler::sEntries;
IDataBase* KESCMDrawEventHandler::sDB = nil;
bool16 KESCMDrawEventHandler::sMarksVisible = kFalse;	// 既定=非表示。枠等はシングルツール左hold中だけ表示(master トグル)
PMReal KESCMDrawEventHandler::sMarkScreenOpacity = 1.0;	// 既定=不透明。ツール左hold中=選択不透明度(25%/75%)/印刷ON中の常時表示=選択不透明度
bool16 KESCMDrawEventHandler::sPrintMarks = kFalse;	// 既定=画面のみ(印刷/PDF には出さない)
bool16 KESCMDrawEventHandler::sMarkOpacity25 = kTrue;	// 既定=25%(パネルの既定ラジオと一致)。kFalse=75%
bool16 KESCMDrawEventHandler::sShowOldNumbers = kFalse;	// 既定=OFF(フライアウト「Show Original Page Numbers」)
bool16 KESCMDrawEventHandler::sAlwaysShowMarks = kFalse;	// 既定=OFF(フライアウト「Hold to Hide Marks」。ON=枠を画面に常時表示し押下中だけ隠す=極性反転)
bool16 KESCMDrawEventHandler::sMarksTempHidden = kFalse;	// Hold to Hide Marks モード中、Target 窓でツール左hold中だけ kTrue(Target 常時表示枠の一時退避)
bool16 KESCMDrawEventHandler::sSrcMarksTempHidden = kFalse;	// 同上の Source 版。Source 窓でツール左hold中だけ kTrue(Source 常時表示枠の一時退避)
bool16 KESCMDrawEventHandler::sSrcMarksOn = kFalse;	// 既定=OFF。Start(KESCMToggleStartStop)のたびに kTrue へ(フライアウト「Show Marks on Source」。再比較では戻さない 2026-07-25)
IDataBase* KESCMDrawEventHandler::sSrcDB = nil;
std::map<UID, UID> KESCMDrawEventHandler::sSrcPageToTarget;
std::map<UID, UID> KESCMDrawEventHandler::sPrevPairTargetToSource;	// 前回比較のペアリング(登録トグルの差分再比較用)
std::set<UID> KESCMDrawEventHandler::sOverflowT;					// overflow("/")ページ集合キャッシュ(Target側)
std::set<UID> KESCMDrawEventHandler::sOverflowS;					// 同(Source側)
IDataBase* KESCMDrawEventHandler::sOverflowCacheDB = nil;			// 上記キャッシュを作った時の sDB
IDataBase* KESCMDrawEventHandler::sOverflowCacheSrcDB = nil;			// 同 sSrcDB
// ★スレッドローカル(第2段 Task 12B)。初期値 kFalse は「どのスレッドから最初に読んでも kFalse」の意味。
//   宣言側のコメント(KESCMDrawEventHandler.h)に、素の static だと何が壊れるかを書いてある。
IDThreading::ThreadLocal<bool16> KESCMDrawEventHandler::tl_Rasterizing(kFalse);	// 自前ラスタ化中だけ kTrue(自己参照防止)
bool16 KESCMDrawEventHandler::sThumbExperiment = kTrue;	// ★サムネイル実験(2026-07-06)。kFalseで従来動作へ即復帰
std::map<UID, KESCMOrigImage*> KESCMDrawEventHandler::sOrigImages;
IDataBase* KESCMDrawEventHandler::sOrigDB = nil;
bool16 KESCMDrawEventHandler::sShowOriginal = kFalse;	// 既定=非表示(kescmShowOriginal で ON)
PMReal KESCMDrawEventHandler::sOrigScale = 0.0;	// ラスタ化時のズームスケール(0=未設定)
PMReal KESCMDrawEventHandler::sPeekOpacity = 1.0;	// 既定=不透明(Shift peek)。Shift+Alt peek で 0.5 にする
bool16 KESCMDrawEventHandler::sOversetOn = kFalse;	// Find Overset トグル(既定 OFF)
IDataBase* KESCMDrawEventHandler::sOversetDB = nil;	// 走査した文書(pointer 識別のみ)
std::set<UID> KESCMDrawEventHandler::sOversetPages;	// overset を含むページ UID 集合
std::vector<KESCMOversetLoc> KESCMDrawEventHandler::sOversetLocs;	// overset「+」箇所ごとの位置(Prev/Next 巡回先)

// ページ矩形クリップの内側縮め量(pt)。見開きはノドで隙間なく隣接するため、ページ矩形ぴったりで
// クリップすると枠/斜線の最外周が共有線に乗り、隣(変化なし)ページ側に 1px 線が出る。約1pt 内側に
// 縮めて防ぐ(2026-07-25: 4関数のローカル重複定義をファイルスコープへ集約)。
static const PMReal kKESCMClipInset = 1.0;

//========================================================================================
// 旧ページ番号バッジのフォントキャッシュ(2026-07-25 監査): 既定フォント+固定サイズで不変なので、
// DrawEvent 毎の QueryFont/QueryFontInstance 再取得をやめて初回だけ引いて使い回す。
// ★file-static InterfacePtr にはしない(静的破棄タイミングの Release はオブジェクトモデル消滅後で
//   危険=sCmykCursorFont と同じ理由)。生ポインタ+KESCMReleaseOldNumFontCache(Shutdown)で明示解放。
//========================================================================================
static IPMFont*       sOldNumFont      = nil;
static IFontInstance* sOldNumFontInst  = nil;
static bool16         sOldNumFontTried = kFalse;	// 取得失敗を毎描画リトライしない(セッション内1回だけ試す)

void KESCMReleaseOldNumFontCache()
{
	if (sOldNumFontInst != nil) { sOldNumFontInst->Release(); sOldNumFontInst = nil; }
	if (sOldNumFont != nil)     { sOldNumFont->Release();     sOldNumFont = nil; }
	sOldNumFontTried = kFalse;
}

//========================================================================================
// overflow キャッシュ("/"の未比較ページ集合)。以前は HandleDrawEvent が描画のたびに
// KESCMBuildPairing(両文書の全ページ走査)を呼んでいたのを、比較実行時に1回作って保持する形へ。
//========================================================================================
void KESCMDrawEventHandler::RebuildOverflowCache()
{
	// ★★2026-08-16(API 監査 B3 §5)= **走査はロックの外・集合の差し替えだけロックの中。**
	//   ①sOverflowT/S は **main が書き、BG(PDF の非同期書き出し)が描画で count する**
	//     (HandleDrawEvent の2つのループ)＝KESCMThreadSafety.h:76-81 が守れと書いている条件そのもの。
	//     以前は clear() + insert() を素でやっていたので、**main が木を回している最中に BG が count する**
	//     窓が開いていた(sEntries を守っているのと同じ理由・同じ相手)。
	//   ②とはいえ KESCMBuildPairing は**両文書の全ページ走査**なので、ロックしたまま回してはいけない
	//     (同ヘッダー :88-89「ロックしたまま長い処理をしない」)。∴ 先に手元の集合へ作り、swap で差し替える。
	//   ★副産物: 作り直しの最中に描画が来ても**空集合ではなく前回の集合が見える**(以前は clear 直後に
	//     描かれると "/" が一瞬消えた)。swap は O(1) で例外も投げない。
	sOverflowCacheDB    = sDB;
	sOverflowCacheSrcDB = sSrcDB;
	std::set<UID> newT, newS;
	if (sDB != nil && sSrcDB != nil)
	{
		std::vector<UID> tp, sp, tov, sov;
		KESCMBuildPairing(sDB, sSrcDB, tp, sp, &tov, &sov);
		newT.insert(tov.begin(), tov.end());
		newS.insert(sov.begin(), sov.end());
	}
	{
		KESCMMarkStateLock lock(KESCMMarkStateMutex());
		sOverflowT.swap(newT);
		sOverflowS.swap(newS);
	}
}

void KESCMDrawEventHandler::EnsureOverflowCache()
{
	// 控えた (sDB,sSrcDB) が現在と食い違う時だけ作り直す(文書切替・別文書へのスプレッド再比較の保険)。
	// 登録Add/Start/Ignore切替は KESCMDoMarkChangesDoc が RebuildOverflowCache を直接呼ぶので、ここは
	// 「同じ文書対のまま」の通常描画では何もしない=毎描画の全文書走査を避ける。
	// ★比較しているのは**static どうし**(sOverflowCacheDB と sDB)なので、判定結果はスレッドによらず同じ。
	if (sOverflowCacheDB == sDB && sOverflowCacheSrcDB == sSrcDB)
		return;

	// ★★2026-08-15(第2段 Task 12B)= **バックグラウンドでは作り直さない。**
	//   RebuildOverflowCache() は sOverflowT/sOverflowS/sOverflowCacheDB/sOverflowCacheSrcDB という
	//   **共有 static を書き換える**うえ、中で両文書の全ページを走査する。BG(PDF の非同期書き出し)から
	//   これを走らせると、メインスレッドが同じ集合を読んでいる最中に作り替えることになる
	//   (ガイド vol1-07 L104 "They do share globals and statics")。
	//   ⚠ここへ BG で来るのは「main がまだ一度も作っていない/文書対が変わった直後」だけで、
	//     そのときは overflow の "/" が出ないだけ(マーク本体は sEntries から出る)。**描かないほうが安全。**
	if (!KESCMIsMainThread())
		return;

	RebuildOverflowCache();
}

void KESCMDrawEventHandler::BuildRing(uint8* buf, int32 rb, int32 bpp, int32 wt, int32 ht,
	const uint8* dist, const uint8* bgRed, int32 radius)
{
	if (buf == nil || dist == nil || wt <= 0 || ht <= 0 || bpp < 3)
		return;
	if (radius < 1) radius = 1;
	const int32 colorOff = bpp - 3;
	const uint8 rad = (radius > 255) ? 255 : (uint8)radius;	// dist は uint8 clamp255。半径上限は200<255。
	// ★端の欠け対策は下の frame(ページ内縁の枠帯)が兼ねる: 端から radius 以内を無条件に塗るので、
	//   変化がページ端に接していてもその帯が必ず埋まり、辺の枠が痩せて欠けることはない。

	// 距離変換の1パス塗り。リング = 0<dist<=radius(=「半径内に変化画素があり、かつ自身は変化画素でない」)。
	// 旧版の横膨張+縦膨張(各 O(W*H) のスライディングウィンドウ)が消え、ズーム段ごとの仕事が約1/3。
	// チェスボード距離ゆえ角型リングで形状は従来と同一。
	for (int32 y = 0; y < ht; ++y)
	{
		uint8* rowB = buf + (size_t)y * rb;
		const uint8* drow = dist + (size_t)y * wt;
		const uint8* brow = (bgRed != nil) ? (bgRed + (size_t)y * wt) : nil;
		for (int32 x = 0; x < wt; ++x)
		{
			uint8* pixT = rowB + (size_t)x * bpp;	// ARGB 先頭=alpha
			uint8* px = pixT + colorOff;
			const uint8 d = drow[x];
			// 変化画素まわりの外側の帯(距離 <= radius)。ページ端に接する部分の欠け補填は不要:
			// 下の frame(ページ内縁の枠帯)が端から radius 以内を無条件に塗るので、同じ端の画素が必ず埋まる。
			const bool16 ring = (d != 0 && d <= rad);
			// ★ページ内縁の枠帯: 変化の有無に関わらず、ページ端(=バッファ端)から radius 画素以内を
			//   「外枠」として塗る。太さは変化部リングと同じ radius(=毎ズーム再算出ゆえ一定px=ズーム不変)。
			//   色・不透明度もリング画素と同一(赤/赤背景でシアン、alpha=255。薄さは blit 側 opacity が担当)。
			const bool16 frame = (x < radius || (wt - 1 - x) < radius ||
			                      y < radius || (ht - 1 - y) < radius);
			if (ring || frame)
			{
				// リング/枠画素。下の実ページが赤っぽければシアン、そうでなければ赤(画素単位)。
				const bool useAlt = (brow != nil && brow[x]);
				px[0] = useAlt ? kKESCMRingAltR : kKESCMRingR;
				px[1] = useAlt ? kKESCMRingAltG : kKESCMRingG;
				px[2] = useAlt ? kKESCMRingAltB : kKESCMRingB;
				if (bpp >= 4) pixT[0] = kKESCMRingAlpha;	// リング画素の基本アルファ(=255 不透明)。薄表示は setopacity 側
			}
			else { px[0] = 255; px[1] = 255; px[2] = 255; if (bpp >= 4) pixT[0] = 0; }	// 透明
		}
	}
}


//========================================================================================
// ヘルパ: 差分マスク(0/1)のチェスボード距離変換 → out(uint8, 0=変化画素, clamp255)。
//   各画素に「最も近い変化画素までのチェスボード距離(=max(|dx|,|dy|))」を入れる。リング描画は
//   out の閾値処理(0<out<=radius)だけで済む。8近傍・全コスト1の二パス chamfer(前進+後退)。
//   out は呼び出し側が確保(w*h)。
//========================================================================================
static void KESCMDistTransform(const uint8* mask, int32 wt, int32 ht, uint8* out)
{
	if (mask == nil || out == nil || wt <= 0 || ht <= 0)
		return;
	const size_t N = (size_t)wt * ht;
	for (size_t i = 0; i < N; ++i)
		out[i] = mask[i] ? 0 : (uint8)255;

	// 前進パス(左上→右下): 既処理の (左, 上, 左上, 右上) から +1。
	for (int32 y = 0; y < ht; ++y)
	{
		for (int32 x = 0; x < wt; ++x)
		{
			const size_t idx = (size_t)y * wt + x;
			if (out[idx] == 0) continue;
			int32 best = out[idx];
			if (x > 0)                    { int32 v = (int32)out[idx - 1]      + 1; if (v < best) best = v; }
			if (y > 0)                    { int32 v = (int32)out[idx - wt]     + 1; if (v < best) best = v; }
			if (y > 0 && x > 0)           { int32 v = (int32)out[idx - wt - 1] + 1; if (v < best) best = v; }
			if (y > 0 && x < wt - 1)      { int32 v = (int32)out[idx - wt + 1] + 1; if (v < best) best = v; }
			if (best > 255) best = 255;
			out[idx] = (uint8)best;
		}
	}
	// 後退パス(右下→左上): 既処理の (右, 下, 右下, 左下) から +1。
	for (int32 y = ht - 1; y >= 0; --y)
	{
		for (int32 x = wt - 1; x >= 0; --x)
		{
			const size_t idx = (size_t)y * wt + x;
			if (out[idx] == 0) continue;
			int32 best = out[idx];
			if (x < wt - 1)               { int32 v = (int32)out[idx + 1]      + 1; if (v < best) best = v; }
			if (y < ht - 1)               { int32 v = (int32)out[idx + wt]     + 1; if (v < best) best = v; }
			if (y < ht - 1 && x > 0)      { int32 v = (int32)out[idx + wt - 1] + 1; if (v < best) best = v; }
			if (y < ht - 1 && x < wt - 1) { int32 v = (int32)out[idx + wt + 1] + 1; if (v < best) best = v; }
			if (best > 255) best = 255;
			out[idx] = (uint8)best;
		}
	}
}


// ノンブル除外領域の判定(比較ループの最内で使う)。
// ★2026-07-25 追補 に per-pixel の全矩形走査から2段ふるいへ変更。旧実装は画素ごとに全矩形の4辺を
//   比較しており、比較解像度 144dpi の A4 = 約200万画素 × 矩形数ぶんの判定が全ページで走っていた。
//   ノンブル矩形はページ下端(または上端)の薄い帯に限られるので、
//     ①ページ全体の union bbox で行/列を粗くふるう
//     ②行ループの先頭で「その y に掛かる矩形」だけを集めておく
//   の2段にすると、除外帯の外(=大半の行)は「集合が空か」の1回で抜けられる。
//   下の KESCMXInRowRects は②で絞り込んだ行内の矩形について x 方向だけを見る。
static bool16 KESCMXInRowRects(int32 x, const std::vector<const Int32Rect*>& rowRects)
{
	for (size_t i = 0; i < rowRects.size(); ++i)
		if (x >= rowRects[i]->left && x < rowRects[i]->right)
			return kTrue;
	return kFalse;
}

ErrorCode KESCMDrawEventHandler::MakeEntry(const UIDRef& targetRef, const UIDRef& sourceRef, bool16& changed)
{
	changed = kFalse;
	if (targetRef.GetDataBase() == nil || targetRef.GetUID() == kInvalidUID)
		return kFailure;
	if (sourceRef.GetDataBase() == nil || sourceRef.GetUID() == kInvalidUID)
		return kFailure;

	// ラスタ化は3回から2回へ削減。旧版は別途 72dpi の target(snapL)もラスタ化していたが、その画素は
	//   BuildRing が buf を全上書きするため一切使われていなかった。低解像度の寸法は高解像度から割り戻し、
	//   背景の「赤っぽい」判定(bgRed)も高解像度 target をプーリングして作るので、snapL は不要=削除。
	// 【高解像度】差分検出用。target / source を高dpi(kKESCMResolution×kKESCMHiResMul)でラスタ化。
	// 低解像度では平均化で消える細線/微小ズレを満額の差分画素として拾い、取りこぼしを防ぐ。
	const PMReal hiRes = kKESCMResolution * kKESCMHiResMul;
	// 比較は常に CMYK 4ch を不透明ラスタ化して行う(CMYK の微差が RGB 変換で消えるのを回避)。
	// 表示リングは別途 ARGB で合成するので、比較ラスタは不透明(addTransparencyAlpha=kFalse)でよい。
	// nothrow: 本関数の確保方針(下の★コメント)に合わせる(2026-07-25 監査で3箇所とも統一)。
	SnapshotUtilsEx* snapTH = new (std::nothrow) SnapshotUtilsEx(targetRef, 1.0, 1.0, hiRes, hiRes, 0.0, SnapshotUtilsEx::kCsCMYK, kFalse);
	if (snapTH == nil)
		return kFailure;
	// アンチエイリアスを OFF にしてラスタ化する(第4引数 enableAntiAliasing=kFalse)。エッジの中間調(灰にじみ)を
	//   無くし、画素内で収まる微小ズレ由来の帯状ノイズが差分として拾われるのを抑える。
	//   ※ target / source は必ず同じ AA 設定でラスタ化すること(片方だけだと全エッジが差分になる)。
	// ★第2引数 fullResolutionGraphics=kFalse(既定)は**意図的**: kTrue にすると配置画像のフル解像度生成を
	//   誘発し、それが文書を dirty にする(KESCMColorSampler.cpp:92-93 に同じ理由を記載)。KESCM は
	//   「モデルを一切書き換えない」のが設計の核なので、プロキシ描画のまま比較する。
	// ★第3引数 greekBelowPtSize=0.0(=greek 無効)は**意図的**(2026-08-06 の監査 E-1 で既定 7.0 から変更)。
	//   既定のままだと小さい文字が「灰色の帯」として描かれて字形を持たず、target/source とも同条件に
	//   なるため「小さい文字は変わっても差分が出ない」取りこぼしが起きうる(SnapshotUtilsEx.h:224-225。
	//   しきい値は "point size multiplied by the scaling" だが、その scaling が何を指すかはヘッダーに
	//   書かれておらず、7pt 未満が全滅するのか 3.5pt 未満だけなのかは決着しない)。KESCM の目的は
	//   画素比較なので、字形を必ず描かせる 0.0 を渡す(代償=小さい文字が多いページのラスタ化がやや遅くなる)。
	//   ⚠公式サンプル(snapshot/SnapTracker.cpp:318)は既定のままだが、あちらの目的は「見た目のスナップ
	//   ショット」で前提が違う。★target/source は必ず同じ値で(片方だけ greek すると全文字が差分になる)。
	// ★第8引数 bDrawNonPrintingObjects=kFalse は**意図的**(2026-08-12。既定は kTrue)。既定のままだと
	//   「非印刷」に設定したページアイテム(作業用の指示書き・注釈・トンボ脇のメモなど)を動かしただけで
	//   変更ページになり、**刷り上がりは同じなのにマークが出る**。KESCM のマークは「刷り上がりの変更」を
	//   指すものと決めたので描かせない(ガイド vol1-09 通読で発見。根拠=SnapshotUtilsEx.h:241-242)。
	//   ⚠ヘッダーが明記するとおり**レイヤーの非印刷設定には効かない**(非印刷レイヤーの扱いは別の話)。
	//   ⚠第5〜7引数は**既定値をそのまま書いているだけ**(第8引数を指定するために省略できない):
	//     transparencyQuality=kXPHigh(落とすと影・ぼかし・ブレンドの変更を拾えなくなるので下げない) /
	//     abortCheck=nil(中断はページ境界で見る=KESCMCore.cpp) / pVPAttrMap=nil。
	//   ★target/source は必ず同じ値で(片方だけ描かせると全差分になる)。
	ErrorCode drewTH;
	{
		KESCMRasterizingGuard rg;	// この Draw 中に再入する HandleDrawEvent はマークを描かない(自己参照防止)
		drewTH = snapTH->Draw(IShape::kPreviewMode, kFalse, 0.0, kFalse,
		                      SnapshotUtils::kXPHigh, nil, nil, kFalse);
	}
	AGMImageAccessor* accTH = (drewTH == kSuccess) ? snapTH->CreateAGMImageAccessor() : nil;

	SnapshotUtilsEx* snapSH = new (std::nothrow) SnapshotUtilsEx(sourceRef, 1.0, 1.0, hiRes, hiRes, 0.0, SnapshotUtilsEx::kCsCMYK, kFalse);
	if (snapSH == nil)
	{
		if (accTH) delete accTH;
		delete snapTH;
		return kFailure;
	}
	ErrorCode drewSH;
	{
		KESCMRasterizingGuard rg;
		// 同上: greek 無効・AA OFF・非印刷オブジェクトを描かない(両者必ず同条件)
		drewSH = snapSH->Draw(IShape::kPreviewMode, kFalse, 0.0, kFalse,
		                      SnapshotUtils::kXPHigh, nil, nil, kFalse);
	}
	AGMImageAccessor* accSH = (drewSH == kSuccess) ? snapSH->CreateAGMImageAccessor() : nil;

	ErrorCode status = kFailure;
	if (accTH != nil && accSH != nil)
	{
		// 高解像度(比較)の寸法・バッファ
		Int32Rect bth = accTH->GetBounds();
		Int32Rect bsh = accSH->GetBounds();
		const int32 wth = bth.right - bth.left, hth = bth.bottom - bth.top;
		const int32 wsh = bsh.right - bsh.left, hsh = bsh.bottom - bsh.top;
		const int32 rbTH = (int32)accTH->GetRowBytes();
		const int32 rbSH = (int32)accSH->GetRowBytes();
		const int32 bppH = (int32)accTH->GetBitsPerPixel() / 8;
		const uint8* ptH = accTH->GetBaseAddr();
		const uint8* psH = accSH->GetBaseAddr();

		// 低解像度(保存・表示)の寸法は高解像度から割り戻す。buf は ARGB の自前バッファ(行パディング無し)。
		int32 wl = ::ToInt32(::Round(PMReal(wth) / kKESCMHiResMul));
		int32 hl = ::ToInt32(::Round(PMReal(hth) / kKESCMHiResMul));
		if (wl < 1) wl = 1;
		if (hl < 1) hl = 1;
		const int32 bppL = 4;				// 表示リングは常に自前 ARGB(=4)合成。比較ラスタの ch 数(RGB=4/CMYK=4)とは独立
		const int32 rbL = wl * bppL;		// 自前バッファ=行パディング無し

		if (ptH != nil && psH != nil &&
			wth == wsh && hth == hsh && rbTH == rbSH && rbTH > 0 &&
			bppH >= 4 && wl > 0 && hl > 0)
		{
			// ★この関数の確保は全て new (std::nothrow): MSVC の通常 new は失敗時に nil を返さず throw するため、
			//   nothrow にしないと下の nil チェックが機能しない(OOM 時は例外がイベント境界を突き抜けてクラッシュ)。
			//   nothrow なら OOM でも「このページのマークを作らない」だけで安全に続行できる。
			const size_t N = (size_t)wl * hl;
			uint8*  M     = new (std::nothrow) uint8[N];	// 低解像度マスク(保存): プーリング結果
			uint16* cntHi = new (std::nothrow) uint16[N];	// 低解像度セルごとの「高解像度の変化画素数」(プーリング用一時)
			if (M != nil && cntHi != nil)
			{
				memset(cntHi, 0, N * sizeof(uint16));

				// ★ノンブル(自動ページ番号)除外領域。トグルON時のみ、target/source 両ページの
				// 「Current Page Number」マーカーを含むフレームの矩形(ページ左上原点のpt座標)を集め、
				// 比較解像度(hiRes)のピクセル座標に変換してから、その範囲内の画素は下の差分走査で
				// スキップする(実デザインが同一でも新旧で連番が違うことによる誤検知を防ぐ)。
				// target/source は同じページサイズが前提なので、どちらの矩形も同じ (x,y) 座標系に
				// そのまま使える。Int32Rect への変換はここ(KESCMPageNumberMarker.h は PMRect のみを
				// 扱い、Int32Rect には依存しない)。
				std::vector<Int32Rect> excludeRects;
				if (KESCMGetIgnorePageNumberMarker())
				{
					// ★比較のときは必ず実測し直す(refresh=kTrue)。同時にキャッシュも更新されるので、
					//   除外領域の緑ベタ塗り(可視化)はこの比較で使ったのと同じ矩形を描くことになる
					//   (2026-08-06 の監査 E-3。以前は描画側が毎回別に実測していた)。
					// ⚠★2つの参照を同時に持つので、キャッシュは「挿入で既存要素の参照を無効化しない」
					//   コンテナでなければならない。実体は std::map(KESCMPageNumberMarker.cpp:414-416)で
					//   その保証がある。★unordered_map / vector に替えると 2 本目の取得で 1 本目
					//   (tRects)が宙を指す。替えるなら、ここは値でコピーするか 1 本ずつ使い切る形へ
					//   直すこと(2026-08-06 の再確認で明文化)。
					const std::vector<PMRect>& tRects = KESCMGetPageNumberMarkerRects(targetRef, kTrue);
					const std::vector<PMRect>& sRects = KESCMGetPageNumberMarkerRects(sourceRef, kTrue);
					const PMReal pxScale = hiRes / PMReal(72.0);	// pt → 比較解像度のpx
					for (int pass = 0; pass < 2; ++pass)		// 0=target / 1=source(同じ (x,y) 座標系へ積む)
					{
						const std::vector<PMRect>& mrs = (pass == 0) ? tRects : sRects;
						for (size_t mi = 0; mi < mrs.size(); ++mi)
						{
							const PMRect& mr = mrs[mi];
							Int32Rect epr;
							epr.left   = ::ToInt32(::Round(mr.Left()   * pxScale));
							epr.top    = ::ToInt32(::Round(mr.Top()    * pxScale));
							epr.right  = ::ToInt32(::Round(mr.Right()  * pxScale));
							epr.bottom = ::ToInt32(::Round(mr.Bottom() * pxScale));
							excludeRects.push_back(epr);
						}
					}
				}

				// 【高解像度で比較 → 低解像度セルへ散らす(scatter)】
				// 高解像度の各画素を差分判定(生の各チャンネル最大差>しきい値)し、変化していたら
				// 対応する低解像度セルのカウンタを増やす。セル写像は寸法比(高/低が整数倍でなくてもよい)。
				// CMYK 比較: 先頭から4ch(offset=0)。各chの最大差がしきい値(kKESCMCmykThr)を超えたら変化画素。
				const int  nch       = 4;
				const int32 colorOffH = 0;
				const int  thr        = kKESCMCmykThr;

				// 【ノンブル除外の前処理(2026-07-25 追補)】除外矩形の union bbox を先に取る。以降は
				// 「行が bbox の縦範囲外なら判定ゼロ」「x が bbox の横範囲外なら 2 比較」で抜けられる。
				int32 exTop = 0, exBottom = 0, exLeft = 0, exRight = 0;
				if (!excludeRects.empty())
				{
					exTop  = excludeRects[0].top;   exBottom = excludeRects[0].bottom;
					exLeft = excludeRects[0].left;  exRight  = excludeRects[0].right;
					for (size_t mi = 1; mi < excludeRects.size(); ++mi)
					{
						const Int32Rect& r = excludeRects[mi];
						if (r.top    < exTop)    exTop    = r.top;
						if (r.bottom > exBottom) exBottom = r.bottom;
						if (r.left   < exLeft)   exLeft   = r.left;
						if (r.right  > exRight)  exRight  = r.right;
					}
				}
				std::vector<const Int32Rect*> rowRects;	// その行に掛かる矩形だけ(ループ外で確保して再利用)
				rowRects.reserve(excludeRects.size());
				for (int32 y = 0; y < hth; ++y)
				{
					const uint8* rowT = ptH + (size_t)y * rbTH;
					const uint8* rowS = psH + (size_t)y * rbTH;
					int32 yl = (int32)((int64)y * hl / hth);
					if (yl >= hl) yl = hl - 1;
					uint16* cntRow = cntHi + (size_t)yl * wl;

					// この行に掛かる除外矩形だけを集める(bbox の縦範囲外なら空のまま=以降は判定ゼロ)。
					rowRects.clear();
					if (!excludeRects.empty() && y >= exTop && y < exBottom)
					{
						for (size_t mi = 0; mi < excludeRects.size(); ++mi)
							if (y >= excludeRects[mi].top && y < excludeRects[mi].bottom)
								rowRects.push_back(&excludeRects[mi]);
					}
					const bool16 rowHasExclude = rowRects.empty() ? kFalse : kTrue;

					for (int32 x = 0; x < wth; ++x)
					{
						if (rowHasExclude && x >= exLeft && x < exRight && KESCMXInRowRects(x, rowRects))
							continue;	// ノンブル除外領域: 差分扱いしない
						const uint8* px = rowT + (size_t)x * bppH + colorOffH;
						const uint8* sx = rowS + (size_t)x * bppH + colorOffH;
						int cm = 0;
						for (int c = 0; c < nch; ++c)
						{
							const int d = (px[c] > sx[c]) ? px[c] - sx[c] : sx[c] - px[c];
							if (d > cm) cm = d;
						}
						if (cm > thr)
						{
							int32 xl = (int32)((int64)x * wl / wth);
							if (xl >= wl) xl = wl - 1;
							if (cntRow[xl] < 0xFFFF) ++cntRow[xl];
						}
					}
				}

				// 【マックスプーリング】セル内の高解像度変化画素が min-count 以上なら低解像度マスク=1。
				// 1個でも(min=1)立てれば取りこぼしゼロ。min を上げると縁ノイズ耐性が増す。
				size_t diffCount = 0;
				for (size_t i = 0; i < N; ++i)
				{
					uint8 m = (cntHi[i] >= (uint16)kKESCMPoolMinCount) ? 1 : 0;
					M[i] = m;
					if (m) ++diffCount;
				}
				delete[] cntHi; cntHi = nil;

				if (diffCount == 0)
				{
					// 変化なし: エントリを作らない。
					delete[] M;
					status = kSuccess;	// 成功・ただし changed=false
				}
				else
				{
					// 背景(対象ページ)の「赤っぽい」画素マップを、高解像度 target をプーリングして作る
					// (低解像度 snapL を廃止。低解像度セル中心の高解像度画素1点を代表サンプルに)。
					// CMYK 経路は RGB が無いので、サンプル CMYK を近似 RGB に変換してから同じ R 優位判定を使う。
					const int32 colorOffT = 0;
					uint8* BG = new (std::nothrow) uint8[N];	// nil 可(BuildRing が nil bgRed を許容)
					if (BG != nil)
					{
						for (int32 y = 0; y < hl; ++y)
						{
							int32 yh = (int32)(((int64)y * hth + hth / 2) / hl);
							if (yh >= hth) yh = hth - 1;
							const uint8* rowT = ptH + (size_t)yh * rbTH;
							for (int32 x = 0; x < wl; ++x)
							{
								int32 xh = (int32)(((int64)x * wth + wth / 2) / wl);
								if (xh >= wth) xh = wth - 1;
								const uint8* px = rowT + (size_t)xh * bppH + colorOffT;
								// CMYK(0..255) → 近似 RGB: ch=(255-ink)*(255-K)/255 の簡易式
								const int C = px[0], Mk = px[1], Yk = px[2], K = px[3];
								const int r = (255 - C)  * (255 - K) / 255;
								const int g = (255 - Mk) * (255 - K) / 255;
								const int b = (255 - Yk) * (255 - K) / 255;
								BG[(size_t)y * wl + x] = (r - g > kKESCMRedBgDom && r - b > kKESCMRedBgDom) ? 1 : 0;
							}
						}
					}

					// ★buf を指す自前 AGMImageRecord を組んで切り離す(buf は下で BuildRing が全画素を書くので
					//   ラスタ画素のコピーは不要)。SnapshotUtilsEx / accessor は保持しない(下で即破棄)。
					//   GetAGMImageRecord も呼ばない=破棄時クラッシュ(保持 accessor の delete)を根本回避。
					KESCMOverlayEntry* e = new (std::nothrow) KESCMOverlayEntry();
					if (e == nil)
					{
						// OOM 保険(nothrow 化に伴う): ここまでの部分確保を解放し、スナップショットも
						// 破棄してこのページは諦める(MakeOrigImage の確保失敗時と同じ early-return 流儀)。
						if (BG != nil) delete[] BG;
						delete[] M;
						if (accSH)  delete accSH;
						if (snapSH) delete snapSH;
						if (accTH)  delete accTH;
						if (snapTH) delete snapTH;
						return kFailure;
					}
					e->w = wl;  e->h = hl;  e->rowBytes = rbL;  e->bpp = bppL;
					e->bgRed = BG;  e->lastRadius = kKESCMBaseRadius;
					// 変更の割合表示(Prev/Next)用。分母は w * h なので分子だけ覚える。ここは diffCount != 0 が
					// 確定した枝なので必ず 1 以上になる(0 のときは上でエントリを作らずに戻っている)。
					e->changedCells = (int32)diffCount;
					// mask M から距離変換 dist を1回だけ作って保持(以後の BuildRing はこれ1つで描ける)。
					//   dist 生成後、mask M はもう不要なので解放(常駐メモリは dist が mask を置換=純増ゼロ)。
					e->dist = new (std::nothrow) uint8[N];
					if (e->dist != nil)
						KESCMDistTransform(M, wl, hl, e->dist);
					delete[] M;

					// 初回リング(基準半径)を buf へ直接描く。
					e->buf = (e->dist != nil) ? new (std::nothrow) uint8[(size_t)rbL * hl] : nil;

					// ★★dist / buf のどちらかが確保できなかったら、このページのマークは作らない
					//   (2026-07-30 の監査で修正)。以前は「dist が無ければ buf を透明クリア」「buf が
					//   無ければ描画側が skip」というフォールバックだったが、どちらも **sEntries には
					//   エントリが載るのに画面には何も出ない** 状態を作る。changedCells は非 0 なので
					//   Prev/Next は「変更あり」としてそのページへ飛び、しかし枠が見えない=壊れて見える。
					//   OOM でこのページを諦めるのは、上の e==nil / MakeOrigImage の確保失敗と同じ流儀。
					//   ★BG は既に e->bgRed が所有しているので、delete e が dist/buf/bgRed をまとめて解放する。
					if (e->buf == nil)
					{
						delete e;
						if (accSH)  delete accSH;
						if (snapSH) delete snapSH;
						if (accTH)  delete accTH;
						if (snapTH) delete snapTH;
						return kFailure;
					}
					BuildRing(e->buf, rbL, bppL, wl, hl, e->dist, BG, kKESCMBaseRadius);
					e->rec.bounds.xMin = 0;             e->rec.bounds.yMin = 0;
					e->rec.bounds.xMax = (int16)wl;     e->rec.bounds.yMax = (int16)hl;
					e->rec.baseAddr     = e->buf;
					e->rec.byteWidth    = rbL;
					// ARGB(alpha 先頭)。HasAlpha フラグを立てないと透明画素が不透明白で描かれる。
					// 既定が ARGB 順なので SwapAlpha は不要(RGBA なら | kColorSpaceSwapAlpha)。
					e->rec.colorSpace   = (int16)(kRGBColorSpace | kColorSpaceHasAlpha);
					e->rec.bitsPerPixel = (int16)(bppL * 8);
					e->rec.decodeArray  = nil;
					e->rec.colorTab.numColors = 0;  e->rec.colorTab.theColors = nil;

					// 既存エントリがあれば置換。
					// ★2026-08-15(第2段 Task 12B): **置換は古いエントリを delete する**ので、
					//   描画中の BG が同じポインタを読んでいないようロックを取る(DropAll と同じ理由)。
					//   ⚠ロックは「集合をいじる区間」だけ。上のラスタ化・リング生成は外に置いてある。
					UID key = targetRef.GetUID();
					{
						KESCMMarkStateLock lock(KESCMMarkStateMutex());
						std::map<UID, KESCMOverlayEntry*>::iterator old = sEntries.find(key);
						if (old != sEntries.end()) { delete old->second; sEntries.erase(old); }
						sEntries[key] = e;

						// Source 側描画(Show Marks on Source)用の対応表もここで記録する。エントリ登録と同じ場所に
						// 置くことで、旧 Ctrl+ミドルのスプレッド再比較(MakeEntry 直呼び)でも対応が自動で維持される。
						// 対応表の掃除は DropAll(エントリと運命共同体)。
						// ★★2026-08-16(API 監査 B3 §5)= **この2行も同じロックの中に入れた。**
						//   sSrcPageToTarget は **main が insert し、BG(PDF の非同期書き出し)が描画で find する**
						//   (HandleDrawEvent の Source ループ)＝KESCMThreadSafety.h:76-81 が守れと書いている条件
						//   そのもの。以前はロックのスコープを閉じた直後に素で書いていたので、
						//   **main が木を回している最中に BG が find する**窓が開いていた。
						//   ⚠**捨てる側(DropAll)は最初から clear() をロック内でやっていた**＝作る側だけが漏れていた。
						sSrcDB = sourceRef.GetDataBase();
						sSrcPageToTarget[sourceRef.GetUID()] = key;
					}

					// dist / bgRed / buf は entry が所有(mask M は dist 生成後に解放済み)。スナップショットは下の後始末で即破棄。
					changed = kTrue;
					status = kSuccess;
				}
			}
			else
			{
				if (M)     delete[] M;
				if (cntHi) delete[] cntHi;
			}
		}
	}

	// 後始末: 2つのスナップショット/アクセサを破棄(ラスタ化は2回=低解像度 snapL は廃止)。
	if (accSH)  delete accSH;
	if (snapSH) delete snapSH;
	if (accTH)  delete accTH;
	if (snapTH) delete snapTH;
	return status;
}


ErrorCode KESCMDrawEventHandler::MakeOrigImage(const UIDRef& targetRef, const UIDRef& sourceRef, const PMReal& resolution)
{
	if (targetRef.GetDataBase() == nil || targetRef.GetUID() == kInvalidUID)
		return kFailure;
	if (sourceRef.GetDataBase() == nil || sourceRef.GetUID() == kInvalidUID)
		return kFailure;

	// source(旧)を resolution(dpi)で不透明ラスタ化。addTransparencyAlpha=kFalse=ページを不透明に描く(べた載せ用)。
	// オフスクリーンは1枚だけ。画素を自前 buf へコピーしたら即破棄(下)＝同時に複数生存しない=安全。
	// ★引数は既定のまま(greek 7.0・AA on・非印刷オブジェクトも描く)。**比較ラスタとはわざと違える**:
	//   比較(MakeEntry)は「刷り上がりが変わったか」を問うので 2026-08-12 に非印刷オブジェクトを
	//   描かないようにしたが、この画像は**旧版の見た目をそのまま重ねて見せる**ためのもので、
	//   画面で見えていたものが消えると「前はこうだった」の再現にならない。
	//   ⚠∴「旧版画像には見えるのにマークは出ない」差分がありうる(非印刷オブジェクトを動かした場合)。
	//   これは意図した非対称。揃えるなら両方を kFalse にする。
	SnapshotUtilsEx* snap = new (std::nothrow) SnapshotUtilsEx(sourceRef, 1.0, 1.0, resolution, resolution, 0.0, SnapshotUtilsEx::kCsRGB, kFalse);
	if (snap == nil)
		return kFailure;	// nothrow: OOM でもこのページの旧版画像を作らないだけで安全に続行(MakeEntry と同方針)
	ErrorCode drew;
	{
		KESCMRasterizingGuard rg;	// この Draw 中に再入する HandleDrawEvent はマークを描かない(自己参照防止)
		drew = snap->Draw(IShape::kPreviewMode);
	}
	AGMImageAccessor* acc = (drew == kSuccess) ? snap->CreateAGMImageAccessor() : nil;

	ErrorCode status = kFailure;
	if (acc != nil)
	{
		Int32Rect b = acc->GetBounds();
		const int32 w = b.right - b.left, h = b.bottom - b.top;
		const int32 rb = (int32)acc->GetRowBytes();
		const int32 bpp = (int32)acc->GetBitsPerPixel() / 8;
		const uint8* p = acc->GetBaseAddr();
		// AGMImageRecord.bounds は int16。300dpi で超大型ページ(幅/高さ>32767px≒109inch)だと破綻するので弾く。
		if (p != nil && w > 0 && h > 0 && rb > 0 && bpp >= 3 && b.right <= 32767 && b.bottom <= 32767)
		{
			// nothrow: 300dpi の大判ページ(A2 で buf 約140MB)は OOM が現実に起こり得る筆頭。
			// 失敗時は下の early-return が部分確保を解放して安全に抜ける(nil チェックを実効化)。
			KESCMOrigImage* o = new (std::nothrow) KESCMOrigImage();
			uint8* obuf = (o != nil) ? new (std::nothrow) uint8[(size_t)rb * h] : nil;
			if (o == nil || obuf == nil)
			{
				// allocation failed: free any partial state and bail (same safety as MakeEntry)
				if (obuf) delete[] obuf;
				if (o)    delete o;
				if (acc)  delete acc;
				if (snap) delete snap;
				return kFailure;
			}
			o->buf = obuf;
			o->w = w;  o->h = h;  o->rowBytes = rb;  o->bpp = bpp;
			memcpy(o->buf, p, (size_t)rb * h);
			// 不透明保証: ARGB(alpha 先頭)なら alpha を 255 に揃える(べた載せ=下が透けない)。
			// まず格子状(約8×8点)にサンプリングし、全サンプルが既に 255(不透明)なら O(W*H) の
			//   全画素ループを丸ごと省く。ラスタが既に不透明(addTransparencyAlpha=kFalse)なら書き込みを回避。
			//   サンプルに非255が1つでもあれば従来どおり全画素を 255 に揃える(自己補正=どちらでも正しい)。
			if (bpp >= 4)
			{
				bool16 alreadyOpaque = kTrue;
				const int32 sy = (h > 8) ? h / 8 : 1;
				const int32 sx = (w > 8) ? w / 8 : 1;
				for (int32 y = 0; y < h && alreadyOpaque; y += sy)
				{
					const uint8* row = o->buf + (size_t)y * rb;
					for (int32 x = 0; x < w; x += sx)
						if (row[(size_t)x * bpp] != 255) { alreadyOpaque = kFalse; break; }
				}
				if (!alreadyOpaque)
				{
					for (int32 y = 0; y < h; ++y)
					{
						uint8* row = o->buf + (size_t)y * rb;
						for (int32 x = 0; x < w; ++x)
							row[(size_t)x * bpp] = 255;
					}
				}
			}
			o->rec.bounds.xMin = (int16)b.left;   o->rec.bounds.yMin = (int16)b.top;
			o->rec.bounds.xMax = (int16)b.right;  o->rec.bounds.yMax = (int16)b.bottom;
			o->rec.baseAddr     = o->buf;
			o->rec.byteWidth    = rb;
			o->rec.colorSpace   = (int16)((bpp >= 4) ? (kRGBColorSpace | kColorSpaceHasAlpha) : kRGBColorSpace);
			o->rec.bitsPerPixel = (int16)acc->GetBitsPerPixel();
			o->rec.decodeArray  = nil;
			o->rec.colorTab.numColors = 0;  o->rec.colorTab.theColors = nil;

			// 既存があれば置換。
			UID key = targetRef.GetUID();
			std::map<UID, KESCMOrigImage*>::iterator old = sOrigImages.find(key);
			if (old != sOrigImages.end()) { delete old->second; sOrigImages.erase(old); }
			sOrigImages[key] = o;
			status = kSuccess;
		}
	}

	if (acc)  delete acc;
	if (snap) delete snap;
	return status;
}


void KESCMDrawEventHandler::Register(IDrwEvtDispatcher* d)
{
	// スプレッド単位で配られる描画イベント。ポートは spread 座標。枠/変更数・旧版べた載せをこちらで描く。
	d->RegisterHandler(ClassID(kEndSpreadMessage), this, kDEHLowestPriority);

	// ★★2026-08-13(Task 6): **kAfterLastSpreadDrawMessage の登録をやめた。**
	//   あれは 2026-08-07 に**押下中 HUD のためだけ**に戻したもので、HUD が UI 側の描画サービス
	//   (KESCMUIDrawEvent.cpp)へ移った今、このハンドラには用が無い(受けても描くものが無い)。
	//   ★2系統を併用する理由(clip と Z 順の関係)は、使う側である KESCMUIDrawEvent.cpp の
	//     HandleDrawEvent のコメントへ移した。
}

void KESCMDrawEventHandler::UnRegister(IDrwEvtDispatcher* d)
{
	d->UnRegisterHandler(ClassID(kEndSpreadMessage), this);
}


// (KESCMQueryPanorama は 2026-08-13 に KESCMViewLookup.cpp へ移した＝model/UI 分割 第1段 Task 12。
//  IPanorama を返す＝窓が無ければ答えの無い問いなので、描画エンジンの持ち場ではない。)

//========================================================================================
// 印刷/PDF 用のリング描画。画面は image() blit でよいが(画素 alpha を honor する)、印刷のフラットナ
// 経路は blit 画像の部分 alpha を honor せず枠が不透明になる。そこで transparencyeffect サンプルと
// 同じ作法=リング形状を「グレーのアルファサーバ」にして純色のベクター fill を setopacity で半透明に
// 描く(透明合成エンジンが honor する)。赤と青(背景適応)を保つため、赤画素・青画素それぞれのグレー
// マスクで2回 fill する。呼び出し側で translate/scale 済み(user 空間 = 画像px)であること。
//   e->buf は ARGB(先頭=alpha, 続いて R,G,B)。
//========================================================================================
//========================================================================================
// ★★★このポートは「PDF 書き出し」か(2026-08-15・第2段 Task 12B で追加)。
//
//   ★公式の書き方をそのまま採った(2026-08-15 に SDK を実測。**製品コード2本が同じ形**):
//     open/components/buttonui/misc/FormFieldLabelDrawer.cpp:139-140
//     open/components/dynamicdocumentsui/motion/AnimationAdormentDrawer.cpp:112-113
//         Utils<IPDFLibraryUtilsPublic const> utils;
//         if (utils.Exists() && utils->IsPDFExportPort(gd->GetGraphicsPort())) ...
//     ⚠ どちらも「PDF 書き出しなら**描かずに帰る**」ための判定で、KESCM とは目的が逆(こちらは
//       出したい)。**判定の書き方だけを借りている。**
//
//   ★なぜ要るのか = **印刷と PDF 書き出しは同じ kPrinting で来るのに、描ける道具が違う。**
//     下の KESCMDrawRingForPrint はアルファサーバ(CreateImagePaintServer + SetAlphaServer)で
//     リング形状を作るが、**PDF 書き出しポートではそのマスクが落ち、矩形 fill だけが残る**
//     ＝変更ページが**全面ベタ塗り**になってページの中身が完全に隠れる(2026-08-15 実測。
//       PDF の中身も Image=0/SMask=2・増分 220B で、マスク画像が入っていないことを確認)。
//     ★Adobe 自身も同じ性質に言及している —— FormFieldLabelDrawer.cpp:136-137
//       "We don't need to do any of this when exporting to PDF. And the icon stuff breaks
//        (while trying to set up the context for doing the bitmap...)"
//     ⚠**本物の印刷では正常**(Microsoft Print to PDF で実測 = Image=10/SMask=5、枠もリングも正しい絵)。
//       ∴ **印刷経路は1行も触らない。** 分けるのは PDF 書き出しだけ。
//========================================================================================
static bool16 KESCMIsPDFExportPort(IGraphicsPort* gPort)
{
	if (gPort == nil)
		return kFalse;
	Utils<IPDFLibraryUtilsPublic const> utils;	// ★const 版で引くのも公式2本と同じ
	return (utils.Exists() && utils->IsPDFExportPort(gPort)) ? kTrue : kFalse;
}


//========================================================================================
// ★★★2026-08-16: **出力先に応じた色の指定（印刷/PDF は CMYK で塗る）。**
//
//   ■ なぜ要るか＝**PDF/X-1a は RGB を許さない。** マークを RGB で塗ったまま
//     [PDF/X-1a:2001 (日本)] へ書き出すと、バックグラウンドタスクに次の警告が出て
//     **「有効な PDF だが PDF/X-1a 準拠ではない」ファイルになる**（2026-08-16 ユーザー報告）:
//       「配置された画像の 1 つは、カラーを CMYK カラーとして表示できません。非 CMYK カラーは、
//         PDF/X-1a の基準に準拠していません。」
//     ⇒ **入稿用途では実害**（X-1a のつもりが準拠しない）。
//   ■ ★KESCM は**比較ラスタを CMYK でやっている**（設計の核）のに、**マークだけ RGB** という
//     不整合でもあった。ここで揃える。
//   ■ 画面は従来どおり RGB（★リング本体は ARGB 画像の blit なのでそもそもここを通らない）。
//   ■ 変換は標準式（`k = 1-max(r,g,b)` / `c = (max-r)/max` …）。KESCM が使う色は純色に近いので
//     素直に対応する: 赤(255,0,0)→C0 M100 Y100 K0 ／ シアン(0,255,255)→C100 M0 Y0 K0 ／
//     緑(0,200,0)→C100 M0 Y100 K22 ／ 白→すべて0 ／ 黒→K100。
//========================================================================================
static void KESCMSetOutputColor(IGraphicsPort* gPort, uint8 r, uint8 g, uint8 b, bool16 useCMYK)
{
	if (!useCMYK)
	{
		gPort->setrgbcolor(r / PMReal(255.0), g / PMReal(255.0), b / PMReal(255.0));
		return;
	}
	const PMReal rf = r / PMReal(255.0), gf = g / PMReal(255.0), bf = b / PMReal(255.0);
	PMReal mx = rf;  if (gf > mx) mx = gf;  if (bf > mx) mx = bf;
	if (mx <= PMReal(0.0001))
	{
		gPort->setcmykcolor(PMReal(0.0), PMReal(0.0), PMReal(0.0), PMReal(1.0));	// 黒
		return;
	}
	gPort->setcmykcolor((mx - rf) / mx, (mx - gf) / mx, (mx - bf) / mx, PMReal(1.0) - mx);
}


static void KESCMDrawRingForPrint(IGraphicsPort* gPort, IViewPortAttributes* vpAttr, IDataBase* db,
	KESCMOverlayEntry* e)
{
	if (gPort == nil || e == nil || e->buf == nil || e->w <= 0 || e->h <= 0 || e->bpp < 4)
		return;
	// 透明合成ユーティリティ(アルファサーバ生成/解放に使う)。実行中アプリでは常在するが、
	// transparencyeffect サンプル流に、取得できなければ何もしない(クラッシュ回避)。以後この1個を使い回す。
	Utils<IXPUtils> xpUtils;
	if (!xpUtils)
		return;

	//========================================================================================
	// ★★★2026-08-16: **PDF 書き出しのときだけ要る「追加初期化」**（ユーザー指摘を起点に判明）。
	//
	//   ■ 印刷と PDF 書き出しでは透明の扱いが違う:
	//     ・**印刷** … 必ず**透明フラットナ**を通る（プリンタは透明を理解しないので、InDesign が
	//       重なりを計算して不透明な図形とラスタに分解してから送る）⇒ 呼ぶ側は何もしなくてよい。
	//     ・**PDF 書き出し** … PDF がネイティブに持つ透明機能（/Group・/SMask・/ca）へ写す ⇒
	//       **書き出す先の構造（透明グループ）を自分で開いていないと、半透明を書き込む場所が無い。**
	//
	//   ■ 公式の手本 = `transparencyeffect/TranFxAdornment.cpp:392-407`。
	//     Adobe 自身が "**Extra initialisation is required when drawing to a PDF port.**" と書き、
	//     `isPDFExport && !isPDFFlattenerExport` のときだけ
	//     `starttransparencygroup(bounds, xpManager->GetDocumentBlendingSpace(), …)` で囲んでいる。
	//
	//   ■ ★**実測（2026-08-16）**＝[高品質印刷]（PDF 1.4）で書き出した PDF に
	//     **`/Group`=14・`/SMask`=6・`/ca`=6、サイズ +8,623B** が入り、**目視でも枠が半透明で正しく出た**。
	//     ⚠**PDF 1.3 のプリセットでも半透明が効くことをユーザーが実機で確認**（2026-08-16）。
	//       ★私のバイト分析（マーク無しとの差が 221B だから全面ベタだろう）は**目視していない推論**で、
	//         実機の見え方と食い違った。**サイズだけで絵を判定しないこと。**
	//
	//   ⚠★**2026-08-15 の「PDF 書き出しポートは透明を一切通さない」は誤りだった。**
	//     4通り試して全部不透明になったのは事実だが、**書き出しプリセットが Acrobat 4 ＝ PDF 1.3**
	//     だったことと、**この追加初期化を欠いていた**ことが重なっていた。
	//     ⇒ ★**「実測した」が示せるのは「その条件では出なかった」まで**（条件＝プリセットの互換性レベル）。
	//     ★手がかりは台帳（api-official-examples.md「印刷/PDF でも半透明のまま図形を描く」）に最初から
	//       注記してあったのに、引かずに独自の結論へ進んでいた。
	//     全文＝docs/ai-notes/kescm-pdf-transparency-2026-08-16.md
	//========================================================================================
	bool16 needTransparencyGroup = kFalse;
	if (vpAttr != nil)
	{
		const bool32 isPDFFlattenerExport = vpAttr->GetAttr(kPDFIsFlattenerTargetVPAttr, kFalse);
		const bool32 isPDFExport          = vpAttr->GetAttr(kPDFExportVPAttr, kFalse);
		needTransparencyGroup = (isPDFExport && !isPDFFlattenerExport) ? kTrue : kFalse;
	}
	// ★透明マネージャは「文書の」ブレンディング色空間を答えるので db が要る(nil だと引けない契約
	//   ＝IXPUtils.h:72-73)。PDF 書き出しでなければ引かない(無駄な Query をしない)。
	InterfacePtr<IXPManager> xpManager(needTransparencyGroup && db != nil
		? xpUtils->QueryXPManager(db) : nil);
	if (xpManager == nil)
		needTransparencyGroup = kFalse;	// 引けなければ従来どおり(印刷経路と同じ)描く
	const int32 w = e->w, h = e->h, rb = e->rowBytes, bpp = e->bpp;
	const size_t N = (size_t)w * h;

	// e->buf(ARGB)から、赤リング画素=255 / 青リング画素=255 の2枚のグレーマスクを作る。
	uint8* maskR = new (std::nothrow) uint8[N];	// nothrow: 直下の nil チェックを実効化(失敗時は枠を描かないだけ)
	uint8* maskB = new (std::nothrow) uint8[N];
	if (maskR == nil || maskB == nil) { if (maskR) delete[] maskR; if (maskB) delete[] maskB; return; }
	// ★★★2026-08-16: **そのマスクに1画素でも中身があるか**を数える。理由は下の continue のコメント。
	bool16 anyR = kFalse, anyB = kFalse;
	for (int32 y = 0; y < h; ++y)
	{
		const uint8* row = e->buf + (size_t)y * rb;
		for (int32 x = 0; x < w; ++x)
		{
			const uint8* px  = row + (size_t)x * bpp;	// [alpha, R, G, B]
			const size_t idx = (size_t)y * w + x;
			if (px[0] != 0)								// リング画素(alpha!=0)
			{
				const bool16 blue = (px[3] > px[1]);	// B>R = 青(背景適応で青に切り替わった画素)
				maskR[idx] = blue ? 0 : 255;
				maskB[idx] = blue ? 255 : 0;
				if (blue) anyB = kTrue; else anyR = kTrue;
			}
			else { maskR[idx] = 0; maskB[idx] = 0; }
		}
	}

	// リングの不透明度=パネルで選択中の 25%/75%(画面表示と共通の SelectedMarkOpacity)。
	const PMReal op = KESCMDrawEventHandler::SelectedMarkOpacity();
	// 既知の制限: 透明効果のあるページでは、ここで描く枠/リングがフラットナにラスタ化され、CMYK 変換で
	// 色がやや沈む(透明画像のあるページだけ枠が濃く見える)。色を CMYK 指定にしても解消せず(=色値ではなく
	// 透明機能で描いていることが原因)、不透明ベクター化は25%の「透け」を失うため見送り。現状は元の RGB 指定のまま。
	// ★★2026-08-16: **2パス目の色を「青」から「シアン」へ直した（ユーザー指示）。**
	//   画面のリング画像(BuildRing)は赤背景の上で **kKESCMRingAlt* = シアン(0,255,255)** に切り替えるのに、
	//   ここだけ**純青(0,0,255)を塗っていた**＝画面と印刷で色が違うという食い違い。
	//   判定（`B>R`）は青でもシアンでも一致するので**動作では表面化せず**、ずっと残っていた。
	//   ⇒ 画面と同じ定数を使う。これで「画面・印刷・PDF の3つで見た目が一致」が色でも成立する。
	struct PassDef { uint8* buf; uint8 r, g, b; bool16 any; };
	PassDef passes[2] = {
		{ maskR, kKESCMRingR,    kKESCMRingG,    kKESCMRingB,    anyR },	// 赤
		{ maskB, kKESCMRingAltR, kKESCMRingAltG, kKESCMRingAltB, anyB }		// シアン(旧: 純青)
	};

	for (int p = 0; p < 2; ++p)
	{
		// ★★★2026-08-16: **中身が空のマスクは飛ばす（これが「ページが青くベタ塗りになる」の原因だった）。**
		//   青(シアン)のリングは「下地が赤っぽい画素の上」でしか現れないので、**普通のページでは青マスクが
		//   全画素 0**。その全 0 のマスクをアルファサーバに渡すと**マスクが効かず、下の rectpath がそのまま
		//   塗られて「純青の全面ベタ」になる**（ユーザー報告 2026-08-16「2ページ目が青くなる」）。
		//   ⚠2026-08-15 の記録「①の全面ベタが**純青**で出ていた」も、いま思えば同じ現象を見ていた。
		//   ★旧のベクター版には `if (anyRun) fill();` という同じ趣旨のガードがあったのに、
		//     **アルファサーバ版にだけ無かった**＝2つの実装で片方だけが守っていた形
		//     （[[verify-claims-in-comments]]「N か所が守っている、は N か所とも開く」）。
		//   ★これは印刷経路でも同じコードを通るので、印刷側の無駄な塗りも同時に消える。
		if (!passes[p].any)
			continue;

		// マスクを指すグレー(8bpp, alpha無し)の AGMImageRecord。アルファサーバは gray colorspace 必須。
		AGMImageRecord mrec;
		mrec.bounds.xMin = 0;            mrec.bounds.yMin = 0;
		mrec.bounds.xMax = (int16)w;     mrec.bounds.yMax = (int16)h;
		mrec.baseAddr     = passes[p].buf;
		mrec.byteWidth    = w;								// 1byte/px, 行パディング無し
		mrec.colorSpace   = (int16)kGrayColorSpace;
		mrec.bitsPerPixel = 8;
		mrec.decodeArray  = nil;
		mrec.colorTab.numColors = 0;     mrec.colorTab.theColors = nil;

		PMMatrix idm;										// 恒等。user 空間=画像px なので画素(x,y)→user(x,y)
		AGMPaint* alphaPaint = xpUtils->CreateImagePaintServer(&mrec, &idm, 0, nil);
		if (alphaPaint != nil)
		{
			AutoGSave ag(gPort);
			gPort->SetAlphaServer(alphaPaint, kTrue, PMMatrix());	// 形状=リング画素(per-pixel)

			// ★★PDF 書き出しのときだけ、文書のブレンディング色空間で透明グループを開く。
			//   順序は公式どおり **SetAlphaServer → starttransparencygroup → fill → endtransparencygroup**
			//   (TranFxAdornment.cpp:390-425)。
			//   ★`GetDocumentBlendingSpace` は**ポートを取れる**——`IXPManager.h:51-55` が
			//     「in general **the port is needed** so we can inspect the proofing configuration」と
			//     書いているので gPort を渡す(公式サンプルは引数を省いているが、ヘッダー側に寄せる)。
			bool16 startedGroup = kFalse;
			if (needTransparencyGroup)
			{
				AGMColorSpace* blendingSpace = xpManager->GetDocumentBlendingSpace(gPort);
				gPort->starttransparencygroup(
					PMRect(PMReal(0.0), PMReal(0.0), PMReal(w), PMReal(h)),
					blendingSpace, kFalse /*isolation*/, kFalse /*knockout*/);
				startedGroup = kTrue;
				xpManager->ReleaseBlendingSpace(blendingSpace);
			}

			gPort->setopacity(op, kFalse);							// 半透明(透明合成が honor)
			// ★★2026-08-16: **CMYK で塗る**(この関数は印刷/PDF 専用)。PDF/X-1a は RGB を許さない
			//   ＝理由は KESCMSetOutputColor の冒頭。
			//   ⚠**この関数へ来てよいのは「フラットナが働く」場合だけ**（呼び出し側 KESCMDrawEntryOnPage
			//     が振り分ける）。透明を1つも含まないページでは、アルファサーバのマスクが解決されず
			//     **全面ベタ**になる（2026-08-16 実測）。
			KESCMSetOutputColor(gPort, passes[p].r, passes[p].g, passes[p].b, kTrue /*CMYK*/);
			gPort->newpath();
			gPort->rectpath(PMReal(0.0), PMReal(0.0), PMReal(w), PMReal(h));	// user 空間=画像px(呼び出し側で translate/scale 済)
			gPort->fill();
			if (startedGroup)
				gPort->endtransparencygroup();
			xpUtils->ReleasePaintServer(alphaPaint);
		}
	}

	delete[] maskR;
	delete[] maskB;
}


// KESCMDrawEntryOnPage の描画モード。文脈ごとに「太さの決め方」と「描き方」が違う:
//   Screen: 半径=ズーム適応(画面 kKESCMRingTargetPx 相当)、image() blit+選択不透明度(25%/75%)
//   Print:  半径=100%表示相当(sxr=1.0)、アルファサーバ+ベクター fill(フラットナが image() の
//           部分 alpha を honor しないため。不透明度は KESCMDrawRingForPrint 内で選択値を使用)
//   (ページパネルのサムネイル生成(view 無し・kPreviewMode のオフスクリーン)向けの専用モードは
//   2026-07-05 に撤去。★背景=既に一度描画済み・パネルに表示中のサムネイルは、公開APIでは
//   ピンポイントで無効化・再生成する手段が無いため([[kescm-pages-panel-thumbnails]]参照)、
//   一部のページだけ枠が古いまま/新しいまま食い違う不整合な見た目になる。ユーザー判断で
//   パネルには枠を一切出さない方針に変更=HandleDrawEvent 側でこの描画コンテキストを早期 return)
enum { kKESCMDrawModeScreen = 0, kKESCMDrawModePrint = 1 };

//========================================================================================
// ページ1枚分のリング描画(HandleDrawEvent の Target ループから括り出した共通部)。
//   db/pageUID のページ矩形へ e のリング画像をフィットさせて描く。リング太さの再計算
//   (BuildRing)もここ。Target 側と Source 側(Show Marks on Source)の両方から呼ばれる:
//   Source 側は Target のリング画像をそのまま Source ページ矩形に重ねる(比較は平坦ページ番号
//   対応なので位置・形は同一。ページサイズが違えば矩形フィットで引き伸ばされる)。
//   screenOpacity は Screen モードの blit にだけ使う(Print は KESCMDrawRingForPrint が
//   SelectedMarkOpacity を直接使う)。
//   ★Target と Source を別ズームで表示中は e->lastRadius が行き来して BuildRing が走り直すが、
//   リング画像は 36dpi 化済みでバッファが小さく実害はない。
//========================================================================================
//========================================================================================
// ★★★PDF 書き出し専用のリング描画(2026-08-15・第2段 Task 12B)。
//
//   ⚠⚠⚠**2026-08-16 に下の「前提」は誤りだと判明した。真因は別で、しかも直そうとしたら落ちた。**
//     ここを読む人は**必ず下の【2026-08-16 の決着】まで読むこと**。要約:
//       ①「ポートが透明を受け付けない」のではなく、**あの日の書き出しプリセットが PDF 1.3 だった**。
//          ガイド vol1-09「Flattening」＝ "PostScript and PDF 1.3 have no representation of
//          transparency information." ⇒ ①〜④が全滅した理由はこの1点。
//       ②**PDF 1.4 なら透明はちゃんと出る**（実測＝[高品質印刷] で /Group=14・/SMask=6・/ca=6、目視も良好）。
//       ③⚠**しかし公式どおり透明グループを足すと、PDF 1.3 の書き出しで InDesign が落ちる**（実測）。
//          ⇒ **下のベクター塗り（この関数）が、全プリセットで安全な唯一の実装**。動かさないこと。
//
//   ■ （2026-08-15 当時の記述。**上のとおり前提は誤り**だが、実験の内容自体は記録として残す）
//     4つ試し、出来上がった PDF を毎回バイナリで検分して確かめた:
//       ①アルファサーバ(CreateImagePaintServer + SetAlphaServer)
//            → マスクごと落ち、rectpath だけが残って **変更ページが全面ベタ塗り**
//              (PDF は Image=0・増分わずか 220B)。⚠**本物の印刷では正常に効く**のがややこしい。
//       ②setopacity + image()            → PDF の ExtGState は **`/ca 1.0` `/CA 1.0`**(不透明)
//       ③setopacity + 透明グループ + image() → PDF に **`/Group` が1つも出ない**(グループごと無視)
//       ④画素 alpha に不透明度を焼いて image()
//            → ★**決定打**: 出来た PDF の画像オブジェクトは `/Width 213 /Height 284 /BitsPerComponent 8`
//              で **`/SMask` が付いていない**。⇒ **このポートは画像のアルファチャンネルを捨てる。**
//              (①②③が効かない理由も全部これ。「透明が消える」という一つの事実の別の顔)
//     一方 ★**ベクターの塗りと色は正しく出る**(①の全面ベタが「純青」で出ていたのが逆説的な証拠)。
//
//   ■ ⇒ 採った道: **リングの形をベクターで塗り、不透明度は色に溶かす。**
//     ・形 = リング画像の「alpha≠0 の画素」を1行ずつランレングスで矩形にまとめて塗る
//       (画像そのままの形なので、画面・印刷と同じリングになる)
//     ・濃さ = 白と混ぜた淡色 c' = 255 - (255 - c) * opacity で**不透明**に塗る
//       ⚠白背景を前提にした近似。下地が白でない場所ではマークが浮く。それでも
//         「全面ベタで中身が読めない」より遥かによく、印刷(Print to PDF)の見た目とも一致する。
//     ・赤/青の2パスに分けるのは KESCMDrawRingForPrint と同じ(背景が赤っぽい所は青リング)。
//
//========================================================================================
// 【2026-08-16 の決着】★★★この関数を動かす前に必ず読むこと。
//
//  ■ 真因 ＝ **書き出しプリセットの互換性レベル**（ポートの性質ではなかった）
//    実測: 既定で使われていたのは `acrobatCompatibility = "Ac40"` ＝ **Acrobat 4 ＝ PDF 1.3**。
//    ガイド vol1-09「Flattening」が明記 ---- "**PostScript and PDF 1.3 have no representation of
//    transparency information.**" ⇒ /ca が 1.0 に潰れるのも /Group が出ないのも /SMask が付かないのも、
//    **すべてこの1点で説明が付く**。★教訓＝**「実測した」が示せるのは「その条件では出なかった」まで。**
//
//  ■ ⚠**日本の入稿プリセットはほぼ PDF 1.3**（実測）:
//    [PDF/X-1a:2001 (日本)] / [PDF/X-3:2002 (日本)] / [雑誌広告送稿用] ＝ **Acrobat 4**。
//    [高品質印刷]・[プレス品質]＝1.4、[最小ファイルサイズ]＝1.5、[PDF/X-4:2008]＝1.6。
//
//  ■ ★**PDF 1.4 なら透明は出せる**（公式どおりアルファサーバ＋透明グループにした場合）:
//    `transparencyeffect/TranFxAdornment.cpp:392-407` の形（`isPDFExport && !isPDFFlattenerExport` の
//    ときだけ `starttransparencygroup(bounds, xpManager->GetDocumentBlendingSpace(gPort), …)`）で実測
//    ⇒ **[高品質印刷] で /Group=14・/SMask=6・/ca=6、サイズ +8,623B、目視でも枠が半透明**。
//
//  ■ ⚠⚠⚠**だが採用できない。PDF 1.3 で書き出すと InDesign が落ちる**（2026-08-16 実測・PID が変化）。
//    理由＝**公式の判定は互換性レベルを見ていない**。PDF 1.3 の書き出しでも
//    `kPDFExportVPAttr`=1 / `kPDFIsFlattenerTargetVPAttr`=0（KT の `app.ktDrawProbe` で実測）なので
//    条件が成立し、**1.3 の出力先に透明グループを開いてしまう**。
//    ★**描画中に互換性レベルを知る手段は未発見**（上の2属性では判定できない）。見つかれば分岐できる。
//
//  ■ ★**もう一つ否定された道**＝「ベクターのパス ＋ setopacity」（この関数の色計算を setopacity に
//    置き換えたもの）＝ PDF では**不透明のまま**で、さらに**バックグラウンドタスクがエラー**になった。
//    リング1つが数千本の 1px 帯サブパスなので、半透明にするとフラットナが破綻すると見られる。
//
//  ■ ★**対照実験（ユーザー提案。効いた）**＝本体の 25% 不透明度の矩形を [PDF/X-1a] で書き出すと **387KB**
//    （PDF 1.4 なら 6KB）＝**フラットナがラスタ化して見た目を保っている**。つまり「1.3 では半透明が使えない」
//    は本体には当てはまらない。draw event handler の描画は**フラットナの入力に入らない**ので保たれない
//    （ガイド vol1-09「透明は page-item adornments 経由でデバイスへレンダリングされる」と整合）。
//    ⚠ ただし adornment 化は `IPageItemAdornmentList` が**永続インターフェイス**なので .indd に書き込まれる
//      （`AddAdornment(ClassID, bool16)` の第2引数は "**kTrue will mark the document as dirty**" ＝
//       永続の有無ではない＝`IPageItemAdornmentList.h:91-95`）。KESCM の「文書を書き換えない」設計と両立しない。
//
//  ⇒ **結論＝この関数（ベクター塗り・不透明度を色に溶かす）が全プリセットで安全な唯一の実装。**
//     全文＝docs/ai-notes/kescm-pdf-transparency-2026-08-16.md
//========================================================================================
static void KESCMDrawRingVectorForPDF(IGraphicsPort* gPort, KESCMOverlayEntry* e, const PMReal& opacity)
{
	if (gPort == nil || e == nil || e->buf == nil || e->w <= 0 || e->h <= 0 || e->bpp < 4)
		return;
	const int32 w = e->w, h = e->h, rb = e->rowBytes, bpp = e->bpp;

	// ★2026-08-16: 色は画面(BuildRing)と同じ定数を使う。2パス目は**シアン**（旧: 純青。画面と食い違っていた）。
	struct PassDef { uint8 r, g, b; bool16 wantBlue; };
	const PassDef passes[2] = {
		{ kKESCMRingR,    kKESCMRingG,    kKESCMRingB,    kFalse },	// 赤
		{ kKESCMRingAltR, kKESCMRingAltG, kKESCMRingAltB, kTrue  }	// シアン
	};

	for (int p = 0; p < 2; ++p)
	{
		// 白と混ぜた「見た目の色」。opacity=0.25 なら赤は (255,191,191)。
		const PMReal cr = (PMReal(255.0) - (PMReal(255.0) - PMReal(passes[p].r)) * opacity) / PMReal(255.0);
		const PMReal cg = (PMReal(255.0) - (PMReal(255.0) - PMReal(passes[p].g)) * opacity) / PMReal(255.0);
		const PMReal cb = (PMReal(255.0) - (PMReal(255.0) - PMReal(passes[p].b)) * opacity) / PMReal(255.0);

		AutoGSave ag(gPort);
		gPort->setopacity(PMReal(1.0), kFalse);	// 濃さは色に溶かしてあるので、ここは不透明に固定
		// ★2026-08-16: **CMYK で塗る**（PDF/X-1a は RGB を許さない＝KESCMSetOutputColor 参照）。
		//   ⚠ここは「白と混ぜた淡色」なので、CMYK でも淡い色になる（赤25% → C0 M25 Y25 K0）。
		KESCMSetOutputColor(gPort,
			(uint8)::ToInt32(::Round(cr * PMReal(255.0))),
			(uint8)::ToInt32(::Round(cg * PMReal(255.0))),
			(uint8)::ToInt32(::Round(cb * PMReal(255.0))), kTrue /*CMYK*/);
		gPort->newpath();
		bool16 anyRun = kFalse;
		for (int32 y = 0; y < h; ++y)
		{
			const uint8* row = e->buf + (size_t)y * rb;
			int32 x = 0;
			while (x < w)
			{
				// このパスに属する画素 = alpha≠0 かつ 色の判定(青は B>R)が一致するもの
				while (x < w)
				{
					const uint8* px = row + (size_t)x * bpp;	// [alpha, R, G, B]
					const bool16 isBlue = (px[3] > px[1]) ? kTrue : kFalse;
					if (px[0] != 0 && isBlue == passes[p].wantBlue)
						break;
					++x;
				}
				if (x >= w)
					break;
				const int32 x0 = x;
				while (x < w)
				{
					const uint8* px = row + (size_t)x * bpp;
					const bool16 isBlue = (px[3] > px[1]) ? kTrue : kFalse;
					if (px[0] == 0 || isBlue != passes[p].wantBlue)
						break;
					++x;
				}
				// user 空間 = 画像px(呼び出し側で translate/scale 済み)。1px 高の横帯を1本のサブパスに。
				gPort->rectpath(PMReal(x0), PMReal(y), PMReal(x - x0), PMReal(1.0));
				anyRun = kTrue;
			}
		}
		if (anyRun)
			gPort->fill();
	}
}


static void KESCMDrawEntryOnPage(IGraphicsPort* gPort, IViewPortAttributes* vpAttr,
	KESCMOverlayEntry* e, IDataBase* db, UID pageUID,
	const PMReal& sxr, int32 drawMode, const PMReal& screenOpacity)
{
	if (e == nil || e->buf == nil)
		return;

	const int32 iw = e->w, ih = e->h;
	InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
	if (iw <= 0 || ih <= 0 || pageGeo == nil)
		return;

	// 【座標の肝】kEndSpreadMessage の描画ポートは spread 座標。ページの箱を spread 座標で取ってフィットさせる。
	// ★★2026-08-16(API 監査 B3・A-1)= 手組み(GetPathBoundingBox + ::InnerToSpreadMatrix + Transform)から
	//   **公式の Facade へ寄せた**。手本 = snapshot/SnapTracker.cpp:621
	//   (★同じ関数の :616 は同じページを PasteboardCoordinates で取っている＝**座標系を変えて2回呼ぶ**のが
	//     公式の形。⚠ヘッダー IGeometryFacade.h:209 は Pasteboard/Parent/Inner の3つしか挙げないが書き落としで、
	//     製品 CPageItemAdaptiveTransform.cpp:197,362 と public lib CPathCreationTracker.cpp:300 も
	//     SpreadCoordinates で呼んでいる)。
	//   ⚠**IGeometry の Query と nil 判定は残す**——「この UID が本当に幾何を持つか」は Facade が担保しない
	//     (手本 :610-615 も同じ順序)。★渡すのは UIDRef(db,pageUID)＝Facade のために Query を増やさない。
	//   ⚠**Geometry::PathBounds() を渡すこと**(手本は OuterStrokeBounds だが、GetPathBoundingBox と
	//     同義なのはこちら)。この関数の下流はページ矩形しか使わないので、行列そのものはもう要らない。
	PMRect pr = Utils<Facade::IGeometryFacade>()->GetItemBounds(
		UIDRef(db, pageUID), Transform::SpreadCoordinates(), Geometry::PathBounds());

	// 【リング太さ】モードごとに膨張半径(画像px)を決め、前回と違えば描き直す。
	if (e->dist != nil)
	{
		int32 R = -1;	// -1=このモードでは半径を決められない(既存バッファのまま描く)
		if (sxr > 0)
		{
			// 画面/印刷: このページの実寸と現ズーム(印刷は sxr=1.0 固定)から
			// 「画面 kKESCMRingTargetPx 相当」の半径を逆算。拡大時は下限(2)に張り付くので再計算が止まる。
			PMReal denom = (pr.Width() / PMReal(iw)) * sxr;		// 画面px / 画像px
			if (denom > PMReal(0.0001))
			{
				R = ::ToInt32(::Round(kKESCMRingTargetPx / denom));
				if (R < 2) R = 2;								// 最小2px(量子化後は最小4px)
				if (R > 200) R = 200;							// 過大膨張の上限
				// 量子化を 2px→4px 刻みに。ズーム中に R が変わる回数(=BuildRing 再計算)がほぼ半減。
				// 代償=太さの段階がやや粗い。最小は 4、200 は 200 に丸まる。
				R = ((R + 2) / 4) * 4;							// 4px 量子化
			}
		}
		else if (drawMode != kKESCMDrawModePrint)
		{
			// ★サムネイル(view無し=sxr が取れない)。ズーム逆算が使えないので、画像幅に対する固定比率で
			// 太い枠を作る(極小表示ゆえ視認性優先)。半径 = 画像幅 / kKESCMThumbRingDivisor。
			R = iw / kKESCMThumbRingDivisor;
			if (R < 4) R = 4;
			if (R > 200) R = 200;
			R = ((R + 2) / 4) * 4;								// 4px 量子化(画面と同じ流儀)
		}
		if (R > 0 && R != e->lastRadius)
		{
			KESCMDrawEventHandler::BuildRing(e->buf, e->rowBytes, e->bpp, e->w, e->h, e->dist, e->bgRed, R);
			e->lastRadius = R;
		}
	}

	// 枠の画像(リング)を blit する。translate/scale はこの gsave 内だけ。
	{
		AutoGSave ag(gPort);
		// ★この描画を「このページの矩形よりわずかに内側」に限定する(spread 座標でクリップ)。見開きの
		// 2ページはノドで隙間なく隣接し、ページ矩形の端=隣ページの端=共有線になる。単に pr でクリップ
		// すると枠の最外周がその共有線に乗り、隣(変化なし)ページのノドに 1px 線が出る。そこで pr を
		// 約1pt 内側に縮めてクリップし、枠が共有線に届かないようにする(枠はページ端の1px内側=見た目ほぼ不変)。
		gPort->rectclip(pr.Left()   + kKESCMClipInset, pr.Top()    + kKESCMClipInset,
		                pr.Width()  - kKESCMClipInset * 2.0, pr.Height() - kKESCMClipInset * 2.0);
		gPort->translate(pr.Left(), pr.Top());				// ページ左上へ
		gPort->scale(pr.Width() / iw, pr.Height() / ih);	// 画像px → ページ矩形にフィット
		// ★印刷時は image() blit だと枠が不透明になる(フラットナが画像の部分 alpha を honor しない)。
		// アルファサーバ＋純色ベクター fill＋setopacity で半透明に描く(透明合成エンジンが honor)。
		// 画面は image() blit(画素 alpha を honor=実測確認済み)+選択不透明度(25%/75%)。
		//
		// ★★★2026-08-16: **印刷と PDF 書き出しを同じ処理に統一した**（ユーザー指示）。
		//   どちらも KESCMDrawRingForPrint（アルファサーバ＋純色のベクター fill）1本で描き、
		//   **PDF 書き出しのときだけ要る追加初期化（透明グループ）はあちらの中で完結させる**
		//   （公式の手本 = transparencyeffect/TranFxAdornment.cpp:392-407）。
		//   ★**実機で半透明が効くことを確認済み**（PDF 1.4 は /Group=14・/SMask=6 で目視も良好。
		//     PDF 1.3 のプリセットでもユーザーが実機で半透明を確認＝2026-08-16）。
		//   ⇒ 旧実装の「PDF だけランレングスのベクター塗り＋不透明度を白と混ぜた色に溶かす」という
		//     **白背景前提の近似は要らなくなった**（下地が白でない場所でマークが浮く問題も消える）。
		//   ⚠ 旧実装の前提「PDF 書き出しポートは透明を一切通さない」は**誤りだった**
		//     （詳細は KESCMDrawRingForPrint 冒頭と docs/ai-notes/kescm-pdf-transparency-2026-08-16.md）。
		if (drawMode == kKESCMDrawModePrint)
		{
			//========================================================================================
			// ★★★2026-08-16: **PDF 書き出しでは「そのページに透明があるか」で描き方を変える。**
			//
			//   ■ 実測（ユーザーが実機で切り分け。★これが決定打だった）:
			//     ・**透明を含むイラストのあるページ** … アルファサーバのマスクが正しく効き、
			//       **枠が半透明で出る**（PDF/X-1a でも CMYK 指定なら警告も出ない）
			//     ・**透明が1つも無いページ** … **枠のあるページが真っ赤なベタ塗り**になる
			//   ■ ★理由＝**フラットナが働くかどうか**。透明があるページは PDF 書き出しの前に
			//     透明フラットナを通るので、アルファサーバの塗りもその経路で解決される。
			//     透明が無いページはフラットナのオフスクリーンが作られないため、マスクが解決されず
			//     下の矩形だけが残る（＝全面ベタ）。
			//   ■ ⇒ **判定は `kPDFIsFlattenerTargetVPAttr`**（このポートがフラットナ対象か）。
			//     ⚠**この属性は「使えない」と何度も誤解した**——PDF 1.3 でも 0 を返すのを見て
			//     「互換性レベルを区別しない＝役に立たない」と判断していたが、**そもそも問いが違った**。
			//     これは「互換性レベル」ではなく「**このページに平坦化する透明があるか**」を答える属性で、
			//     まさにここで要る判定そのものだった。
			//========================================================================================
			//   ■ ⚠★★**「透明が無いページだけベクター塗りに落とす」分岐は、入れないことにした**
			//     （ユーザー判断 2026-08-16）。理由＝**判定材料が足りない**:
			//     ・`kPDFIsFlattenerTargetVPAttr` は「このポートがフラットナ対象か」を答えるが、
			//       **PDF 1.4 以降でも 0 になる**（1.4 は透明をそのまま持てるのでフラットナを使わない）。
			//     ・つまり「0 だから落とす」と書くと、**本物の半透明が出せる PDF 1.4 まで近似に落ちる**。
			//       ★ユーザー実測「1.4 では（透明の無いページでも）問題なく半透明になっている」。
			//     ⇒ **全面ベタになるのは「PDF 1.3 かつ透明が無いページ」だけ**なので、直すには
			//       **描画中に互換性レベルを知る手段**が要る。候補＝**`kXPFlattenerOffVPAttr`**
			//       （XPID.h:652「フラットナが OFF か」＝1.4 なら 1、1.3 なら 0 のはず・**未実測**）。
			//       測って区別できると分かったら、ここに分岐を戻す。
			//   ⇒ 現状は**アルファサーバ1本**。既知の制限＝**PDF 1.3 で透明を含まないページは枠が全面ベタ**。
			KESCMDrawRingForPrint(gPort, vpAttr, db, e);
		}
		else
		{
			// 画面(とサムネイル)は image() blit。画素 alpha を honor する(実測確認済み)。
			// サムネイル(sxr<=0)は不透明100%で描く(極小表示で 25%/75% だと沈んで見えないため)。
			const PMReal blitOpacity = (sxr <= 0) ? PMReal(1.0) : screenOpacity;
			gPort->setopacity(blitOpacity, kFalse);
			gPort->image(&e->rec, PMMatrix(), 0);		// 自前レコード(buf を指す)を blit
		}
	}
}


//========================================================================================
// ページ全体を囲む縁枠(色指定)。用途: Pages パネルのサムネイルで「変更ページ」を赤枠で示す
// (極小サムネイルでは差分リングが潰れて見えないため、ページ枠に置き換える。KESCMDrawEventHandler の
// isThumb 分岐から呼ぶ)。ベクター矩形塗り+setopacity なので screen/print/サムネイル とも正しく合成される。
// 太さ: 画面/印刷 = 画面 kKESCMRingTargetPx 相当(pt=px/sxr。印刷は呼び出し側で sxr=1.0)。
//       サムネイル(sxr<=0)= ページ短辺 / kKESCMThumbBorderDivisor(ズーム式が使えないので潰れない固定比率)。
// 不透明度: サムネイルは kKESCMThumbMarkOpacity(0.75=少し透ける)、印刷は SelectedMarkOpacity、画面は screenOpacity。
//========================================================================================
static void KESCMDrawPageBorder(IGraphicsPort* gPort, IDataBase* db, UID pageUID,
	const PMReal& sxr, int32 drawMode, const PMReal& screenOpacity,
	uint8 cr, uint8 cg, uint8 cb)
{
	InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
	if (pageGeo == nil)
		return;

	// 【座標】KESCMDrawEntryOnPage と同じく、ページの箱を Facade で spread 座標のまま取る
	// (★2026-08-16 の API 監査 B3・A-1。理由と手本はあちらのコメント)。
	PMRect pr = Utils<Facade::IGeometryFacade>()->GetItemBounds(
		UIDRef(db, pageUID), Transform::SpreadCoordinates(), Geometry::PathBounds());

	// 【太さ】画面/印刷はズーム適応(px/sxr)、サムネイル(sxr<=0)はページ短辺の固定比率(枠専用の除数)。
	const PMReal minDim = (pr.Width() < pr.Height() ? pr.Width() : pr.Height());
	PMReal w = (sxr > 0) ? (kKESCMRingTargetPx / sxr) : (minDim / PMReal(kKESCMThumbBorderDivisor));
	const PMReal maxW = minDim / PMReal(2.0) - PMReal(0.5);
	if (w > maxW) w = maxW;
	if (w < PMReal(0.5))
		return;	// ページが小さすぎて太さが潰れる場合は描かない

	// 【クリップ相当】通常マークと同じく、ノドの共有線に届かないよう約1pt内側から描く。
	const PMReal L = pr.Left()   + kKESCMClipInset, R = pr.Right()  - kKESCMClipInset;
	const PMReal T = pr.Top()    + kKESCMClipInset, B = pr.Bottom() - kKESCMClipInset;
	if (R <= L || B <= T)
		return;

	const PMReal opacity = (sxr <= 0) ? kKESCMThumbMarkOpacity
		: ((drawMode == kKESCMDrawModePrint) ? KESCMDrawEventHandler::SelectedMarkOpacity() : screenOpacity);

	AutoGSave ag(gPort);
	// ★サムネイル生成ポートでは、描画前に有効なクリップ矩形を設定しないと fill が出ない(KESCMDrawEntryOnPage の
	// image blit / KESCMDrawPageDiagonal の stroke も同様に rectclip 後に描いている=これが無いと枠が全く出ない)。
	// ノドの共有線に届かないよう約1pt内側でクリップ(L/R/T/B と同じ inset)。fill 各バーはこの内側なので削れない。
	gPort->rectclip(pr.Left()   + kKESCMClipInset, pr.Top()    + kKESCMClipInset,
	                pr.Width()  - kKESCMClipInset * 2.0, pr.Height() - kKESCMClipInset * 2.0);
	gPort->setopacity(opacity, kFalse);
	// ★2026-08-16: 印刷/PDF は CMYK で塗る(PDF/X-1a は RGB を許さない＝KESCMSetOutputColor 参照)。
	KESCMSetOutputColor(gPort, cr, cg, cb, (drawMode == kKESCMDrawModePrint) ? kTrue : kFalse);
	gPort->rectfill(L,     T,     R - L, w);					// 上
	gPort->rectfill(L,     B - w, R - L, w);					// 下
	gPort->rectfill(L,     T + w, w,     (B - T) - w * PMReal(2.0));	// 左
	gPort->rectfill(R - w, T + w, w,     (B - T) - w * PMReal(2.0));	// 右
}


//========================================================================================
// ノンブル(自動ページ番号)除外領域のベタ塗り(可視化)。
//   除外トグル(KESCMGetIgnorePageNumberMarker)がONの間、pageUID のノンブルフレーム矩形を
//   半透明の緑で塗り、比較から外している領域を目視できるようにする。矩形は
//   KESCMAppendPageNumberMarkerRects がページ左上原点のpt座標で返すので、通常マークと同じく
//   ページ inner bbox を spread(=描画ポート)座標へ変換し、その左上を原点に平行移動して塗る
//   (ページは軸整列前提=比較の除外処理やリング描画と同じ座標の扱い)。ベクター矩形+setopacity
//   ゆえ screen/print とも正しく半透明合成される(KESCMDrawPageBorder と同じ理由)。
//========================================================================================
static void KESCMDrawPageNumberMarkerFill(IGraphicsPort* gPort, IDataBase* db, UID pageUID)
{
	if (db == nil || pageUID == kInvalidUID)
		return;

	// ★キャッシュ経由(refresh=kFalse。2026-08-06 の監査 E-3)。この関数は描画イベントのたびに
	//   全ページぶん呼ばれる(スクロール中は連続)ので、実測は繰り返さない。比較時(MakeEntry)に入った
	//   値があればそれを描く=「この比較で除外した領域」。比較より後にトグルを ON にした場合だけ、
	//   ここで1回だけ実測して覚える。
	const std::vector<PMRect>& markerRects = KESCMGetPageNumberMarkerRects(UIDRef(db, pageUID), kFalse);
	if (markerRects.empty())
		return;

	InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
	if (pageGeo == nil)
		return;

	// ★2026-08-16(B3・A-1): ページの箱は Facade で spread 座標のまま取る(理由は KESCMDrawEntryOnPage)。
	const PMRect pr = Utils<Facade::IGeometryFacade>()->GetItemBounds(
		UIDRef(db, pageUID), Transform::SpreadCoordinates(), Geometry::PathBounds());

	AutoGSave ag(gPort);
	gPort->setopacity(kKESCMExcludeFillOpacity, kFalse);
	gPort->setrgbcolor(kKESCMExcludeFillR / PMReal(255.0), kKESCMExcludeFillG / PMReal(255.0), kKESCMExcludeFillB / PMReal(255.0));
	for (size_t i = 0; i < markerRects.size(); ++i)
	{
		const PMRect& mr = markerRects[i];			// ページ左上原点の pt 座標
		if (mr.Width() > 0 && mr.Height() > 0)
			gPort->rectfill(pr.Left() + mr.Left(), pr.Top() + mr.Top(), mr.Width(), mr.Height());
	}
}


//========================================================================================
// ページに左下→右上の斜線("/")を引く(色指定)。用途2種:
//   ・登録済み(比較相手なし="Added"/"Removed")ページ → 緑「/」(2026-07-06: 従来の緑「枠」から変更。
//     溢れの赤「/」と同じ斜線様式にして「相手なしページ」を一目で対応づける)。
//   ・文書間のページ数差であふれた(登録もされていない)未比較ページ → 赤「/」(通常マークと同色)。
// ラスタ不要のベクター線なので screen/print/サムネイル とも setopacity で正しく合成される。
// 太さ/不透明度は KESCMDrawPageBorder と同じ規則(サムネイルは固定比率・kKESCMThumbMarkOpacity で少し透ける)。
//========================================================================================
static void KESCMDrawPageDiagonal(IGraphicsPort* gPort, IDataBase* db, UID pageUID,
	const PMReal& sxr, int32 drawMode, const PMReal& screenOpacity,
	uint8 cr, uint8 cg, uint8 cb)
{
	InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
	if (pageGeo == nil)
		return;

	// ★2026-08-16(B3・A-1): ページの箱は Facade で spread 座標のまま取る(理由は KESCMDrawEntryOnPage)。
	const PMRect pr = Utils<Facade::IGeometryFacade>()->GetItemBounds(
		UIDRef(db, pageUID), Transform::SpreadCoordinates(), Geometry::PathBounds());

	// 太さ: 画面/印刷=ズーム適応、サムネイル(sxr<=0)=ページ短辺の固定比率(「/」専用の除数)。
	const PMReal minDim = (pr.Width() < pr.Height() ? pr.Width() : pr.Height());
	PMReal w = (sxr > 0) ? (kKESCMRingTargetPx / sxr) : (minDim / PMReal(kKESCMThumbDiagDivisor));
	const PMReal maxW = minDim / PMReal(2.0);
	if (w > maxW) w = maxW;
	if (w < PMReal(0.5))
		return;

	const PMReal opacity = (sxr <= 0) ? kKESCMThumbMarkOpacity
		: ((drawMode == kKESCMDrawModePrint) ? KESCMDrawEventHandler::SelectedMarkOpacity() : screenOpacity);

	AutoGSave ag(gPort);
	// ノドの共有線に届かないよう、通常マークと同じく約1pt内側でクリップしてから対角線を引く。
	gPort->rectclip(pr.Left()   + kKESCMClipInset, pr.Top()    + kKESCMClipInset,
	                pr.Width()  - kKESCMClipInset * 2.0, pr.Height() - kKESCMClipInset * 2.0);
	gPort->setopacity(opacity, kFalse);
	// ★2026-08-16: 印刷/PDF は CMYK で塗る(PDF/X-1a は RGB を許さない＝KESCMSetOutputColor 参照)。
	KESCMSetOutputColor(gPort, cr, cg, cb, (drawMode == kKESCMDrawModePrint) ? kTrue : kFalse);
	gPort->setlinewidth(w);
	gPort->newpath();
	gPort->moveto(pr.Left(),  pr.Bottom());	// 左下
	gPort->lineto(pr.Right(), pr.Top());		// 右上 → "/" の対角線
	gPort->stroke();
}


//========================================================================================
// ページ全体に大きな「＋」を「赤の線＋白い縁取り」で描く(Pages パネルのサムネイル専用)。用途:
// フライアウト「Find Overset」でアクティブ文書を走査し、overset(あふれ)のあるページを、ページ
// パネルのサムネイル上で目立たせる。★2026-07-24 ユーザー指定でカンバス(レイアウトビュー)には
// 一切描かず、ページパネルのサムネイルにだけ出す(旧: カンバスに赤い十字を描いていた)。
//   白い太線(縁取り)を先に引き、その上に少し細い赤線を重ねて「赤＋白縁」を作る。フォント非依存の
//   ベクター線なので極小サムネイルでも潰れない(太さはページ短辺の固定比率=「/」と同じ流儀)。
//   サムネイル生成は view 無し(sxr=0)なので、太さはズーム式ではなく短辺比率で決める。
//========================================================================================
static void KESCMDrawPageCrossOutlined(IGraphicsPort* gPort, IDataBase* db, UID pageUID)
{
	InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
	if (pageGeo == nil)
		return;

	// ★2026-08-16(B3・A-1): ページの箱は Facade で spread 座標のまま取る(理由は KESCMDrawEntryOnPage)。
	const PMRect pr = Utils<Facade::IGeometryFacade>()->GetItemBounds(
		UIDRef(db, pageUID), Transform::SpreadCoordinates(), Geometry::PathBounds());

	// 赤線の太さ = ページ短辺 ÷ 専用除数(「/」より太い)。白縁はこれより太く引いて左右にはみ出させる。
	const PMReal minDim = (pr.Width() < pr.Height() ? pr.Width() : pr.Height());
	PMReal redW = minDim / PMReal(kKESCMOversetCrossWidthDivisor);
	const PMReal maxW = minDim / PMReal(3.0);
	if (redW > maxW) redW = maxW;
	if (redW < PMReal(0.5))
		return;
	const PMReal whiteW = redW * PMReal(2.2);	// 白縁(赤線の左右に約 redW*0.6 ずつはみ出す)

	const PMReal cx = (pr.Left() + pr.Right()) / PMReal(2.0);	// ページ中央 X
	const PMReal cy = (pr.Top()  + pr.Bottom()) / PMReal(2.0);	// ページ中央 Y
	// ★縦横とも同じ長さの「＋」にする(2026-07-24 ユーザー指定)。中央から片側 half の長さで上下左右へ伸ばす。
	//   half=短辺×kKESCMOversetCrossHalfRatio(0.40=横は幅の約80%とやや短め・縦も同じ長さ)。
	const PMReal half = minDim * PMReal(kKESCMOversetCrossHalfRatio);

	AutoGSave ag(gPort);
	// ノドの共有線に届かないよう、通常マークと同じく約1pt内側でクリップしてから引く。
	gPort->rectclip(pr.Left()   + kKESCMClipInset, pr.Top()    + kKESCMClipInset,
	                pr.Width()  - kKESCMClipInset * 2.0, pr.Height() - kKESCMClipInset * 2.0);
	gPort->setopacity(kKESCMOversetCrossOpacity, kFalse);	// くっきり(不透明)

	// 1) 白い縁取り(太線)を先に引く。
	gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));
	gPort->setlinewidth(whiteW);
	gPort->newpath();
	gPort->moveto(cx - half, cy);   gPort->lineto(cx + half, cy);	// 横線(中央・長さ 2*half)
	gPort->moveto(cx, cy - half);   gPort->lineto(cx, cy + half);	// 縦線(中央・横と同じ長さ)
	gPort->stroke();

	// 2) 赤い本体(細線)を白縁の上に重ねる=「赤＋白縁」。
	gPort->setrgbcolor(kKESCMRingR / PMReal(255.0), kKESCMRingG / PMReal(255.0), kKESCMRingB / PMReal(255.0));
	gPort->setlinewidth(redW);
	gPort->newpath();
	gPort->moveto(cx - half, cy);   gPort->lineto(cx + half, cy);	// 横線(中央・長さ 2*half)
	gPort->moveto(cx, cy - half);   gPort->lineto(cx, cy + half);	// 縦線(中央・横と同じ長さ)
	gPort->stroke();
}


//========================================================================================
// ページ中央に ✓(チェックマーク)をベクター線で描く(色指定)。「KCM: Check」でチェックした
// ページに描く。描き先は2通り(layoutStyle で切替):
//   ・kFalse = Pages パネルのサムネイル(従来。呼び出し側で isThumb を判定): サイズ=短辺 0.52、
//     太さ=「/」と同じ固定比率、不透明度=kKESCMThumbMarkOpacity。
//   ・kTrue  = レイアウトビュー/印刷(2026-07-12 追加): サイズ=短辺×kKESCMCheckLayoutSizeRatio
//     (かなり大きい)、太さ=✓サイズ×kKESCMCheckLayoutStrokeRatio(ページ比例=ズーム/印刷とも相似形)、
//     不透明度=渡された screenOpacity(呼び出し側が SelectedMarkOpacity=25%/75% 選択を渡す。印刷も同値)。
// ★フォントの ✓ 文字(U+2713 等)は環境/フォント依存で出ないことがあるため使わず、線2本
//   (左端→下の谷→右上=「レ」を左右反転した ✓ 型)を moveto/lineto/stroke で引く。
//========================================================================================
static void KESCMDrawPageCheck(IGraphicsPort* gPort, IDataBase* db, UID pageUID,
	const PMReal& sxr, int32 drawMode, const PMReal& screenOpacity,
	uint8 cr, uint8 cg, uint8 cb, bool16 layoutStyle = kFalse)
{
	InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
	if (pageGeo == nil)
		return;

	// ★2026-08-16(B3・A-1): ページの箱は Facade で spread 座標のまま取る(理由は KESCMDrawEntryOnPage)。
	const PMRect pr = Utils<Facade::IGeometryFacade>()->GetItemBounds(
		UIDRef(db, pageUID), Transform::SpreadCoordinates(), Geometry::PathBounds());

	const PMReal minDim = (pr.Width() < pr.Height() ? pr.Width() : pr.Height());
	// ✓ 全体サイズ(短辺比): レイアウト版はかなり大きく、サムネイルは従来値(2026-07-11 に 0.42→0.52)。
	const PMReal s = minDim * (layoutStyle ? kKESCMCheckLayoutSizeRatio : PMReal(0.52));
	// 太さ: レイアウト版=✓サイズ比例(ズーム/印刷とも相似形)。サムネイル=「/」と同じ固定比率。
	// (サムネイル経路の sxr>0 は来ない=isThumb は view 無しの生成で sxr=0 だが、従来式のまま残す)
	PMReal w = layoutStyle ? (s * kKESCMCheckLayoutStrokeRatio)
		: ((sxr > 0) ? (kKESCMRingTargetPx / sxr) : (minDim / PMReal(kKESCMThumbDiagDivisor)));
	const PMReal maxW = minDim / PMReal(3.0);
	if (w > maxW) w = maxW;
	if (w < PMReal(0.5))
		return;

	// 不透明度: レイアウト版は画面=screenOpacity(SelectedMarkOpacity が渡る)/印刷=SelectedMarkOpacity
	// (=同値。画面と印刷の見た目一致)。サムネイルは kKESCMThumbMarkOpacity 固定(従来)。
	const PMReal opacity = layoutStyle
		? ((drawMode == kKESCMDrawModePrint) ? KESCMDrawEventHandler::SelectedMarkOpacity() : screenOpacity)
		: ((sxr <= 0) ? kKESCMThumbMarkOpacity
			: ((drawMode == kKESCMDrawModePrint) ? KESCMDrawEventHandler::SelectedMarkOpacity() : screenOpacity));

	// ページ中央基準・短辺の一定比率で ✓ を組む。ページ座標は Top<Bottom(Y 下向き)。
	const PMReal cx = (pr.Left() + pr.Right()) / PMReal(2.0);
	const PMReal cy = (pr.Top()  + pr.Bottom()) / PMReal(2.0);
	const PMReal lx = cx - s * PMReal(0.40), ly = cy - s * PMReal(0.02);	// 左端(やや上)
	const PMReal vx = cx - s * PMReal(0.10), vy = cy + s * PMReal(0.32);	// 下の谷(最下点)
	const PMReal rx = cx + s * PMReal(0.48), ry = cy - s * PMReal(0.40);	// 右上(最上点)

	AutoGSave ag(gPort);
	gPort->setopacity(opacity, kFalse);
	// ★2026-08-16: 印刷/PDF は CMYK で塗る(PDF/X-1a は RGB を許さない＝KESCMSetOutputColor 参照)。
	KESCMSetOutputColor(gPort, cr, cg, cb, (drawMode == kKESCMDrawModePrint) ? kTrue : kFalse);
	gPort->setlinewidth(w);
	gPort->newpath();
	gPort->moveto(lx, ly);
	gPort->lineto(vx, vy);
	gPort->lineto(rx, ry);
	gPort->stroke();
}


// (★pasteboard→spread のオフセットを求める KESCMSpreadOffsetFromPasteboard は、唯一の呼び手だった
//  押下中 HUD と一緒に 2026-08-13 に KESCMUIDrawEvent.cpp へ移した(model/UI 分割 第1段 Task 6)。
//  中身は1行も変えていない。)

bool16 KESCMDrawEventHandler::HandleDrawEvent(ClassID eventID, void* eventData)
{
	DrawEventData* ded = static_cast<DrawEventData*>(eventData);
	if (ded == nil || ded->gd == nil)
		return kFalse;
	// 自前のラスタ化(MakeEntry の比較スナップショット / MakeOrigImage の旧版スナップショット)中の再入は
	// 描かない(自己参照=マークがスナップショットに写り込む feedback を防ぐ)。以前は kPreviewMode ビットで
	// 弾いていたが、それは PDF 書き出しの kPDFExportMode と同一ビット(4096)で export を巻き込んでいたため、
	// 明示的な再入フラグ(現在の **tl_Rasterizing**)に置き換えた。
	// ★2026-08-15 裏取り＝この「同一ビット」は**偶然の衝突ではなく Adobe の意図的な設計**なので、
	//   将来 値がずれることは期待できない＝この置き換えは恒久的に正しい(kPreviewMode に戻さないこと)。
	//   kPreviewMode は draw flags(IShape.h:89)、kPDFExportMode は iterate flags(IShape.h:159)で**別の enum**
	//   なのにどちらも 4096。理由は IShape.h:144 が明言している——
	//     "Due to lack up type checking for IShape enums, this must match kPrinting above
	//      since they are unfortunately used interchangeably in the codebase."
	//   (同じ理由で kIteratePrinting=512 も kPrinting=512 に揃えてある)
	//   ⇒ IShape の flags は enum をまたいで同じ値が渡される前提なので、別 enum の定数とも衝突を検討すること。
	// ★2026-08-15: スレッドローカルになった(第2段 Task 12B)。**このスレッドが**ラスタ化中のときだけ弾く。
	//   以前は素の static だったので、メインスレッドの比較ラスタ化が BG の PDF 書き出しまで巻き添えにしていた。
	if (KESCMDrawEventHandler::tl_Rasterizing.Get())
		return kFalse;
	// 印刷文脈か(kPrinting=512)。印刷時はマークの ON/OFF を sPrintMarks で決める。通常の画面描画では立たない。
	// ★2026-08-12 訂正: 旧コメント「PDF 書き出し(File>Export)はこのスプレッド描画イベントを発火しないため
	//   対象外(print-to-PDF を使う)」は**誤り**。kPrinting は「印刷 **または** PDF 書き出し」で立つ
	//   (公式サンプル basicdrwevthandler/BscDEHDrwEvtHandler.cpp:278-282 が " Printing or PDF Output" と明記)。
	//   draw event の描画が File>Export>PDF に焼き込まれることは実機で確認済み(本体内蔵の透かしをプローブに
	//   実測。docs/ai-notes/draw-event-pdf-export-experiment-2026-08-12.md)。
	//   ∴ここは「印刷にも PDF 書き出しにも効く」判定として読むこと。
	//   ★★★2026-08-15 に解決済み＝**マークは書き出し PDF に出る**(第2段の目的そのもの・実機 PASS)。
	//     旧記述「KESCM は `kUIPlugIn` なので BG から見えず、draw event がそもそもこのハンドラへ配られない」
	//     は**もう当たらない**——KESCM は第2段 Task 11 で **`kModelPlugIn`** になり、**BG にも描画イベントが
	//     配られることを実測した**(Task 11C)。⇒ **このハンドラは BG でも呼ばれる前提で読むこと。**
	//     ⚠**残る注意は「同じ static を共有するが db は別」という点**＝BG が見る `db` は**クローンの別
	//       ポインタ**なので、文書の同一性をポインタで聞いてはいけない(`KESCMIsSameDoc`＝KESCMThreadSafety.h)。
	//     ⚠**PDF 書き出しポートは透明を一切通さない**(Task 12B の実測)＝ベクターで塗り、不透明度は色に溶かす。
	//     ★出力先を測る道具は今も有効＝app.ktDrawProbe(KT/KTDrawProbe.cpp)で「その描画はどの出力先か」を実測する。
	// 自己参照(自前スナップショット)は上の tl_Rasterizing で防ぐので、ここで kPreviewMode は見ない。
	const bool16 printing = (ded->flags & IShape::kPrinting) != 0;

	// (★手動 Hide/Show Spread の検出(KESCMScrollMapNoticeDrawEvent)は 2026-08-13 に UI 側の描画サービス
	//  KESCMUIDrawEvent.cpp へ移した＝model/UI 分割 第1段 Task 7。スクロールバー地図は文書窓へ strip を
	//  注入する widget ＝ UI なので、その更新のきっかけを拾うのも UI 側の仕事。
	//  ⇒ **このファイルから UI ヘッダーの include が1つも無くなった。**)

	// ★サムネイル実験(2026-07-06): Pagesパネルのサムネイル生成(view無し・kPreviewMode・非印刷。診断ログ
	// flags=0x1800=kPreviewMode|kDrawFrameEdge)を検出。sThumbExperiment ON の間は、サムネイルにも枠を
	// 描くため下で wantMarks を強制 ON にする(通常は sPrintMarks/sMarksVisible が OFF だと枠が出ない)。
	// サムネイルでは差分リング画像(KESCMDrawEntryOnPage)ではなく、下の Target/Source ループが
	// isThumb 分岐で KESCMDrawPageBorder(枠)/KESCMDrawPageDiagonal(「/」)を呼ぶ(極小表示で潰れない
	// 固定比率の太さ)。不透明度は kKESCMThumbMarkOpacity(0.75=少し透ける)。
	const bool16 isThumb = sThumbExperiment && !printing &&
		ded->gd->GetView() == nil && (ded->flags & IShape::kPreviewMode) != 0;	// gd の nil は関数冒頭で検査済み

	// ★オーバープリントプレビュー(OPP)は抑制しない(2026-07-05 仕様変更)。以前は kSepPrvOPPEnabledVPAttr を
	// 読んで「OPP=印刷シミュレーション」として印刷と同じ抑制を掛けていたが、OPP はあくまで画面の作業モード
	// なので、ツール左hold の枠・Shift/Shift+Alt の旧版 peek(と押下中の旧番号バッジ)は OPP 中も表示する。
	// 抑制は本物の印刷(kPrinting)だけ=「枠の印刷」OFF なら印刷物に出ない、は従来どおり。
	// Source 側の枠(Show Marks on Source)。トグル ON の間は「常時」表示で、OPP でも隠さず印刷にも常に
	// 出す(Target 側の sPrintMarks とは独立の仕様)。この描画が実際に Source 文書のスプレッドかどうかは
	// db 取得後に判定する(ここでは「描き得るか」だけ)。
	// ★sEntries が空でも、登録済み(比較相手なし="Added"/"Removed")ページや、文書間のページ数差で
	// 対応表からあふれた("/"の)ページがあれば、緑枠/赤斜線を描くために続行する。overflow 判定は
	// キャッシュ(sOverflowT/sOverflowS)を使う。EnsureOverflowCache は (sDB,sSrcDB) が前回作成時と
	// 変わった時だけ作り直す(通常の描画では全文書走査は走らない)。
	EnsureOverflowCache();
	const bool16 anyMarkableContent = !sEntries.empty() ||
		(sDB    != nil && KESCMPageMapHasAnyRegistered(sDB)) ||
		(sSrcDB != nil && KESCMPageMapHasAnyRegistered(sSrcDB)) ||
		(sDB    != nil && KESCMPageCheckHasAny(sDB)) ||		// 「KCM: Check」の✓(サムネイル描画を起こすため)
		(sSrcDB != nil && KESCMPageCheckHasAny(sSrcDB)) ||
		(!sOverflowT.empty() || !sOverflowS.empty());
	// 「Hold to Hide Marks」と併用時のみ: Source のレイアウト窓でツール左ボタンを押している間(sSrcMarksTempHidden)は
	// Source 側の常時表示枠も画面で隠す(押した窓の枠だけ隠す=Target と対称のウィンドウ別の極性反転)。
	// 印刷は Source 枠を常に出す仕様なので !printing でゲート=印刷/PDF は不変。sAlwaysShowMarks OFF や
	// Source 窓以外で押した時は sSrcMarksTempHidden が立たない(KESCMPeek.cpp の窓判定)ので従来どおり常時表示。
	const bool16 srcTempHidden = sAlwaysShowMarks && sSrcMarksTempHidden && !printing;
	const bool16 wantSrcMarks = sSrcMarksOn && sSrcDB != nil && anyMarkableContent && !srcTempHidden;
	// 印刷で「枠の印刷」が OFF のときは、Target 側のオーバーレイ一式を描かない(枠は基本非印刷)。
	// Source 側の枠だけは常に印刷に出す仕様なので、wantSrcMarks が生きていれば処理を続行し、
	// 下の want フラグ側で Target 分だけ落とす。
	const bool16 suppressForPrint = printing && !sPrintMarks;
	if (suppressForPrint && !wantSrcMarks)
		return kFalse;
	// この描画で何を描き得るかを状態フラグだけで先に確定し、全部 No なら即 return する。
	//   ・マーク(リング＋枠): 印刷ON か ツール左hold中の表示ON で、かつエントリがある時だけ
	//   ・旧版べた載せ: 画面描画のみ(印刷には出さない)
	// ★以前は「sEntries が非空」なだけで下の前処理(スプレッド取得・生存スイープ・ズーム行列・
	//   パノラマ探索・マウス位置・可視域変換)を全部実行し、最後の分岐で「マーク非表示」と判定して
	//   捨てていた。Start 済み・マーク非表示(既定=ツール左hold中だけ表示)の待機状態が最頻なので、
	//   ここで落として通常の編集・スクロール中の描画コストをほぼゼロにする。生存スイープも「実際に
	//   何か描く」時だけの保険になる(クローズ後始末の本線は KESCMDocResponder で変わらず)。
	// 「Hold to Hide Marks」(極性反転): モード ON の間は画面(!printing)で枠を常時表示。ただしツール左hold中
	// (sMarksTempHidden)は隠す。画面のみ=印刷/PDF は下の sPrintMarks が独立して決める(alwaysScreen は
	// !printing ゲートで印刷文脈には一切効かせない=印刷は従来どおり Print comparison marks のみで制御)。
	const bool16 alwaysScreen = sAlwaysShowMarks && !sMarksTempHidden && !printing;
	const bool16 wantMarks = !suppressForPrint && (sPrintMarks || sMarksVisible || alwaysScreen || isThumb) && anyMarkableContent;
	const bool16 wantOrig  = !suppressForPrint && !printing && sShowOriginal && !sOrigImages.empty();
	// ★「KCM: Check」の ✓ のレイアウトビュー版(2026-07-12)。画面では「常に」表示(ツール左hold・
	// Hold to Hide Marks・Show Marks on Source 等の枠トグルとは完全に独立)。印刷/PDF は sPrintMarks
	// (Print comparison marks)ON のときだけ(Target/Source とも同条件)。✓ 集合は Start 中の
	// Target/Source(sDB/sSrcDB)にしか無い(Stop で全消去)ので、存在チェックも両 db だけ見れば足りる。
	// サムネイル(isThumb)は下の専用ブロックが従来どおり描くのでここでは対象外。
	const bool16 wantChecks = !isThumb && (!printing || sPrintMarks) &&
		((sDB != nil && KESCMPageCheckHasAny(sDB)) || (sSrcDB != nil && KESCMPageCheckHasAny(sSrcDB)));
	// ★Find Overset の「＋」: 比較(sEntries)・チェック(✓)等とは完全に独立。★2026-07-24 ユーザー指定で
	// カンバス(レイアウトビュー)には一切描かず、Pages パネルのサムネイル(isThumb)にだけ「赤＋白縁」の
	// 「＋」を描く(旧: カンバスに赤い十字を描いていた)。走査済み(sOversetOn)で集合が非空なら描画対象。
	// 実際に描くのは db==sOversetDB のスプレッドのサムネイルだけ(下の描画ブロックで判定)。
	const bool16 wantOversetThumb = isThumb && sOversetOn && sOversetDB != nil && !sOversetPages.empty();
	// 旧ページ番号バッジ: トグルON かつ「枠が見えている」間(=印刷マークON の常時表示、またはツール左hold中)。
	// 枠の可視条件(wantMarks の sPrintMarks || sMarksVisible)と同じ揃え。印刷文脈は suppressForPrint で
	// sPrintMarks ON のときだけ生き残る=印刷に出るのは印刷マークON時のみ(従来どおり)。
	// 番号がズレているかはページごとに後で判定する(ズレていなければ何も描かない)。
	const bool16 wantOldNums = !suppressForPrint && sShowOldNumbers && (sPrintMarks || sMarksVisible || alwaysScreen);
	// ★2026-07-11(ユーザー指定): 登録ページ(Added/Removed=緑「/」)は「比較を Start 中」だけ描く。以前は
	// Start と無関係に描く「登録専用パス」があり、未 Start でも右クリック登録すると緑「/」が出ていたが、
	// これを撤去した(登録自体も arm 済みのときだけ可能に変更)。よって登録「/」は下の Target/Source メイン
	// ループ(db==sDB / db==sSrcDB。=Start 中のみ成立)だけが描く。ここでの Anywhere 判定・専用パスは不要。
	// (★押下中 HUD の判定と描画は 2026-08-13 に UI 側の描画サービス KESCMUIDrawEvent.cpp へ移した。
	//  このハンドラは HUD のことを一切知らない＝押下状態(UI の状態)への依存が無くなった。)

	if (!wantMarks && !wantOrig && !wantOldNums && !wantSrcMarks && !wantChecks && !wantOversetThumb)
		return kFalse;

	GraphicsData* gd = ded->gd;
	IGraphicsPort* gPort = gd->GetGraphicsPort();
	if (gPort == nil)
		return kFalse;
	// ★2026-08-16: この描画がどんな出力先かをポート自身に聞くための属性。PDF 書き出しのときだけ要る
	//   追加初期化（透明グループ）の判定に使う(KESCMDrawRingForPrint の冒頭参照)。
	//   ⚠**nil のことがある**——公式サンプルも ASSERT の後に nil チェックして抜けている
	//     (TranFxAdornment.cpp:267-270)。受け取る側が nil を「従来どおり印刷として描く」と解釈するので、
	//     そのまま渡してよい。
	IViewPortAttributes* vpAttr = gd->GetViewPortAttributes();

	// ★ページパネルのサムネイル生成(view 無し・kPreviewMode のオフスクリーン描画。2026-07-05 診断ログ:
	// flags=0x1800=kPreviewMode|kDrawFrameEdge)。従来は「既表示サムネイルを再生成できず一部だけ枠が
	// 古い/新しいと不整合になる」ため一切描かなかった。2026-07-06 の実験では sThumbExperiment ON の間だけ
	// 描いてみる(比較後に KESCMTryRefreshPagesPanelThumbnails で既表示分の再生成を試みる)。実験を切れば
	// (sThumbExperiment=kFalse → isThumb=kFalse)この分岐で従来どおり早期 return し、完全に元の動作に戻る。
	if (!printing && gd->GetView() == nil && (ded->flags & IShape::kPreviewMode) != 0)
	{
		if (!isThumb)
			return kFalse;	// 実験OFF: サムネイルには一切描かない(従来動作)
		// 実験ON: このまま続行してサムネイルにも枠を描く。
	}

	// (★ウィンドウ単位イベント kAfterLastSpreadDrawMessage の分岐は 2026-08-13 に撤去した。
	//  この登録は押下中 HUD のためだけのもので、HUD ごと KESCMUIDrawEvent.cpp へ移ったため
	//  Register からも外してある＝このハンドラへは二度と配られない。)

	// changedBy = 今描いているスプレッド。
	InterfacePtr<ISpread> spread(ded->changedBy, UseDefaultIID());
	if (spread == nil)
		return kFalse;
	IDataBase* db = ::GetDataBase(ded->changedBy);
	if (db == nil)
		return kFalse;


	// ★★★2026-08-15（第2段 Task 11C）に、ここで実測した3つの答え。**次に読む人は測り直さなくてよい。**
	//
	//   MAIN  db=…23FB4A80  sDB=…23FB4A80  entries=2 firstUID=258 class=1295
	//   *BG*  db=…295BE390  sDB=…23FB4A80  entries=2 firstUID=258 class=1295
	//
	//   ①**バックグラウンドスレッドにも描画イベントが配られる**（`kModelPlugIn` 化が効いている）
	//   ②**渡される db はクローンの別ポインタ**＝`sDB` とは必ず食い違う（ガイド vol1-07 L93 のとおり）
	//   ③★★★**UID はクローンをまたいで保たれる**——`sEntries` のキー(258)を BG の db で引くと
	//      **同じ ClassID(1295) のページが返る**。
	//
	// ⇒ ★**同一性は「db ポインタ」ではなく「UID＋ファイル」で聞ける**。これは
	//   [[uidref-reuse-after-close]]（閉じた文書のポインタはアドレス再利用で別文書と一致する）と
	//   **同じ結論**＝1つ直すと2つ直る。⚠**ポインタ比較を `IDataBase::GetSysFile` へ移す作業は Task 12B。**

	// (★押下中 HUD の「帯の前面」ぶんの描画も 2026-08-13 に KESCMUIDrawEvent.cpp へ移した。
	//  ⚠あちらでは**この位置に置く必要がある**という制約は無い＝HUD だけを見るハンドラなので、
	//    「描くものが無い」経路の return に巻き込まれる心配がそもそも無い。)

	// ★保持マークのドキュメントが閉じられていたら破棄する(クローズ監視の代わり)。draw は開いている
	//   ドキュメントについてのみ発火するので、ここで sDB/sOrigDB の生存を確認できる。
	//   マークが無い通常時(sDB==nil かつ sOrigDB==nil)は何も問い合わせない=コストゼロ。
	//   ★以前はここで DropAll/DropAllOrig だけを個別に呼び、マークだけを消して peek arm やパネル表示は
	//   そのままにしていた(枠は消えるのにボタンは Stop のまま、という食い違いの原因)。通常はドキュメント
	//   クローズ responder(KESCMHandleDocsClosed)がクローズ直後に先回りして片付けるためこの分岐へは実質
	//   到達しないが、保険として残す以上は KESCMHandleDocsClosed に一本化し、Stop 相当のフルクリーンアップ
	//   (peek arm 解除・パネル更新も)を確実に行う。
	//   ★sOversetDB も対象(2026-07-25 監査で追加): Find Overset を未 arm で単独使用中にその文書を閉じ、
	//   responder が漏れた場合でも、stale な sOversetPages がアドレス再利用された新文書のサムネイルへ
	//   誤マークされないようにする。
	//   ★★★2026-08-15（第2段 Task 11C）＝**メインスレッド限定にした。実害を再現して直した箇所。**
	//     この保険は「draw は開いている文書についてしか来ない」＝**文書リストに居なければ閉じた**、という
	//     推論で書かれている。⚠**その推論はバックグラウンドスレッドでは成り立たない**——BG は
	//     **クローンされた別の DB** を見る（ガイド vol1-07 L93）ので、`FindDocByDataBase(sDB)` は
	//     **開いているのに nil を返す**。結果、PDF を非同期で書き出すたびに
	//     `KESCMHandleDocsClosed()`（Stop 相当のフルクリーンアップ）が走り、**マークが全部消えていた**
	//     （2026-08-15 に `Document.asynchronousExportFile()` で再現。`marks cleared` が出る）。
	//   ⇒ **塞いだのは `KESCMHandleDocsClosed()` の入口**（呼び手はここを含めて3つあり、「BG では文書の
	//     生存を判定できない」のは**関数の性質**だから＝[[one-question-one-place]]）。ここは素のまま。
	//   ⚠**「BG では何もしない」で正しいのは、あれが後始末（状態を捨てる側）だから。**
	//     描く側を BG で止めたら、それは第2段の目的そのものを止めることになる。
	if (sDB != nil || sOrigDB != nil || sSrcDB != nil || sOversetDB != nil)
	{
		ISession* session = GetExecutionContextSession();	// 終了処理中は nil になり得る(2026-07-25 追補 統一)
		InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
		InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
		if (docList != nil &&
		    ((sDB != nil && docList->FindDocByDataBase(sDB) == nil) ||
		     (sOrigDB != nil && docList->FindDocByDataBase(sOrigDB) == nil) ||
		     (sSrcDB != nil && docList->FindDocByDataBase(sSrcDB) == nil) ||
		     (sOversetDB != nil && docList->FindDocByDataBase(sOversetDB) == nil)))
			KESCMHandleDocsClosed();
	}

	// 画面スケール(ズーム)を一度だけ取得。画面描画時のみ非nil。
	// ★★2026-08-15（第2段 Task 10）＝**この nil チェックはマルチスレッド適合そのもの**。
	//   `IControlView*` を**描画の署名からフレームワークに手渡されている**形は model 側で正規
	//   （SDK の `FrmLblAdornment.cpp` / `TranFxAdornment.cpp` も同じで、両方 `kModelPlugIn`）。
	//   ⚠ただしガイド vol1-07 L101 が "It is critical that you write model code that expects to be
	//     able to receive nil pointers" と書いている当の場所でもある＝**印刷・PDF 書き出しでは
	//     窓が無いので nil で来る**。ここは元から nil を想定して書かれており（sxr=0 のまま進む）、
	//     直す必要は無い。**この形を新しく書くときも必ずこのガードを付ける。**
	PMReal sxr = 0.0;
	IControlView* zview = gd->GetView();
	if (zview != nil)
	{
		PMMatrix toWin = zview->GetContentToWindowMatrix();	// content→window(画面px), 現ズーム
		sxr = abs(toWin.GetXScale());	// 負スケールもあり得る(PMReal 版 abs = PMReal.h)
	}

	// ★描画モードの決定(サムネイル生成は関数冒頭で早期 return 済みなのでここには来ない)。
	int32 drawMode = printing ? kKESCMDrawModePrint : kKESCMDrawModeScreen;

	// ★印刷/PDF 時は「100% 表示の見た目」に固定する(ズーム連動を切る)。印刷ポートには view が無く
	// sxr=0 / pano=nil になるので、実効 sxr=1.0(=100%・deviceScale 1 相当)を与える。これでリング太さの
	// 式が、画面 100% 表示時とちょうど同じ値になる(下流のズーム適応式をそのまま使い回せる)。
	// 画面描画は従来どおりズーム連動。
	if (printing)
		sxr = 1.0;

	// ★「KCM: Check」の ✓(サムネイル版): チェック済みページの Pages パネルサムネイル中央に青い ✓ を描く。
	//   他のマーク(リング/斜線/Show Marks on Source トグル)とは完全に独立=このスプレッドの db が
	//   Target でも Source でも、その db にチェックがあれば描く(下の Target/Source メインループより前・
	//   それらのゲートに依らない)。レイアウトビュー/印刷版は下の wantChecks ブロック(2026-07-12 追加)。
	//   Start 中限定(チェック集合は Stop で全消去されるので非 arm 時は空だが、保険で arm ゲート)。
	if (isThumb && KESCMIsArmed() && KESCMPageCheckHasAny(db))
	{
		const int32 npChk = spread->GetNumPages();
		for (int32 i = 0; i < npChk; ++i)
		{
			const UID puid = spread->GetNthPageUID(i);
			if (KESCMPageCheckIsChecked(db, puid))
				KESCMDrawPageCheck(gPort, db, puid, sxr, drawMode, kKESCMThumbMarkOpacity,
					kKESCMCheckR, kKESCMCheckG, kKESCMCheckB);
		}
	}

	// ★Find Overset の目印(サムネイル版・2026-07-24)。走査した文書(sOversetDB)の Pages パネル
	//   サムネイル生成時だけ、overset を含むページ(sOversetPages)に (a) 変更ページと同じ赤枠
	//   (KESCMDrawPageBorder)＋ (b) その中央に「赤＋白縁」の「＋」を描く。
	//   ★2026-07-24: 一度は赤枠を撤去し「＋」だけにしたが、十字だけでは視認しにくいとのユーザー指定で
	//   赤枠を復活(変更ページと同じ赤枠。区別より視認性を優先)。カンバス(レイアウトビュー)には一切描かない
	//   (ユーザー指定)。比較(sEntries)・✓ とは完全に独立= sOversetOn の間、非 arm でも描く
	//   (比較していなくてもオーバーセット検査の結果を出す)。
	if (wantOversetThumb && db == sOversetDB)
	{
		const int32 npx = spread->GetNumPages();
		for (int32 i = 0; i < npx; ++i)
		{
			const UID puid = spread->GetNthPageUID(i);
			if (sOversetPages.count(puid) > 0)
			{
				KESCMDrawPageBorder(gPort, db, puid, sxr, drawMode, SelectedMarkOpacity(),
					kKESCMRingR, kKESCMRingG, kKESCMRingB);	// 変更と同じ赤枠(視認性のため復活)
				KESCMDrawPageCrossOutlined(gPort, db, puid);	// 中央に赤＋白縁の＋
			}
		}
	}

	// 今描いている「このスプレッド」を覗いている(旧版べた載せ中)か。覗きで旧版が乗るのはマウス下の1スプレッド
	// だけ(そのページが sOrigImages にある)。覗き中のスプレッドだけ旧版をきれいに見せたいので、マーク
	// (枠)を描かない。それ以外のスプレッドは通常どおりマークを描く。
	// ★サムネイル生成(isThumb)では旧版べた載せをしない: peek 押下中にそのスプレッドのサムネイルが
	// 再生成されると、旧版画像が blit されたサムネイルがキャッシュに残り、離した後も古い絵のままになる。
	bool16 peekingThisSpread = kFalse;
	if (wantOrig && !isThumb && sOrigDB != nil && db == sOrigDB)
	{
		const int32 npChk = spread->GetNumPages();
		for (int32 i = 0; i < npChk; ++i)
			if (sOrigImages.find(spread->GetNthPageUID(i)) != sOrigImages.end())
			{ peekingThisSpread = kTrue; break; }
	}

	// 旧版べた載せ — マーク(sEntries)とは独立。覗き中のスプレッドの各ページに旧版画像を不透明で
	// ページ矩形いっぱいに blit する。
	if (peekingThisSpread)
	{
		const int32 npo = spread->GetNumPages();
		for (int32 i = 0; i < npo; ++i)
		{
			const UID pageUID = spread->GetNthPageUID(i);
			std::map<UID, KESCMOrigImage*>::iterator it = sOrigImages.find(pageUID);
			if (it == sOrigImages.end())
				continue;
			KESCMOrigImage* o = it->second;
			if (o == nil || o->buf == nil || o->w <= 0 || o->h <= 0)
				continue;
			InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
			if (pageGeo == nil)
				continue;
			// ★2026-08-16(B3・A-1): Facade で spread 座標のまま取る(理由は KESCMDrawEntryOnPage)。
			const PMRect pr = Utils<Facade::IGeometryFacade>()->GetItemBounds(
				UIDRef(db, pageUID), Transform::SpreadCoordinates(), Geometry::PathBounds());
			AutoGSave ag(gPort);
			gPort->setopacity(sPeekOpacity, kFalse);		// Shift peek=1.0(不透明) / Shift+Alt peek=0.5(半透明)
			gPort->translate(pr.Left(), pr.Top());
			gPort->scale(pr.Width() / o->w, pr.Height() / o->h);	// 旧版画像をページ矩形にフィット
			gPort->image(&o->rec, PMMatrix(), 0);			// 旧版を sPeekOpacity で重ねる
		}
	}

	// ★「KCM: Check」の ✓(レイアウトビュー/印刷版・2026-07-12)。チェック済みページのページ中央に
	//   青い ✓ を「かなり大きく」(短辺×kKESCMCheckLayoutSizeRatio)描く。Target/Source を問わず、この
	//   スプレッドの db にチェックがあれば描く(枠トグル・ツール左hold とは完全に独立=画面では常時表示)。
	//   印刷/PDF は wantChecks が sPrintMarks でゲート済み。不透明度はパネルの 25%/75% 選択
	//   (SelectedMarkOpacity)を画面・印刷共通で使う。旧版べた載せ(peek)の直後に描く=peek の不透明画像
	//   の上にも ✓ が乗る(常に見える)。この後の Source/Target マークループより前に置くのは、Source
	//   ループが return kFalse で抜けるため(リング等が ✓ の上に重なるのは許容=どちらも半透明マーク)。
	if (wantChecks && KESCMIsArmed() && KESCMPageCheckHasAny(db))
	{
		const int32 npc = spread->GetNumPages();
		for (int32 i = 0; i < npc; ++i)
		{
			const UID puid = spread->GetNthPageUID(i);
			if (KESCMPageCheckIsChecked(db, puid))
				KESCMDrawPageCheck(gPort, db, puid, sxr, drawMode, SelectedMarkOpacity(),
					kKESCMCheckR, kKESCMCheckG, kKESCMCheckB, kTrue /*layoutStyle*/);
		}
	}

	// ★Find Overset の「＋」はカンバス(レイアウトビュー)には描かない(2026-07-24 ユーザー指定)。
	//   Pages パネルのサムネイル(isThumb)にだけ描く=上の isThumb 専用ブロック(wantOversetThumb)を参照。

	// 旧ページ番号バッジ(Show Original Page Numbers)。スプレッドが隠されて「現在のページ番号」マーカーが
	// ズレているページにだけ、「隠す前の元の番号」をページ下端中央へ描く(画面=WYSIWYG、印刷/PDF にも出る)。
	// マーク(sEntries)とは独立=この db がマーク対象かは問わない(隠しが無ければ元番号と現在番号が一致して
	// 何も描かない)。GetPageString の最終引数 bIncludePagesOfHiddenSpread が kTrue=隠しページも数える(元の番号)/
	// kFalse=隠しページを飛ばす(現在マーカーが表示している番号)。書式は★番号のみ(bIncludeSectionName=kFalse
	// =セクションプレフィックス "A:" を付けない。ユーザー指定 2026-07-15)・セクションの番号スタイル
	// (bUseIntegerStyle=kFalse)=実際のノンブルと同じ見た目。文字は framelabel 流(selectfont+show)。
	// サイズはズーム非依存(fontSize=目標px/sxr。印刷時は sxr=1.0 固定=実寸 pt)。
	// 見た目: 白フチ+黒文字(背景の白塗りは 2026-07-15 に廃止)。バッジ全体の不透明度は 25%/75% 選択に連動。
	if (wantOldNums && sxr > 0)
	{
		InterfacePtr<IPageList> pageList(db, db->GetRootUID(), UseDefaultIID());
		// フォント/インスタンスはファイル先頭のキャッシュ(sOldNumFont/sOldNumFontInst)から。初回だけ取得。
		// ★★2026-08-15(第2段 Task 12B)= **初回取得はメインスレッドだけが行う。**
		//   ここは3つの共有 static(sOldNumFontTried/sOldNumFont/sOldNumFontInst)を書き換える唯一の場所で、
		//   BG と main が同時に通ると **QueryFont を二重に発行し、片方のポインタを取りこぼす**(=解放漏れ)。
		//   ⚠BG で未取得のときは numFont==nil になり、下の `numFont != nil` でバッジだけ描かれない。
		//     旧番号バッジは既定 OFF のトグルで、ON なら画面描画(main)が先に必ずキャッシュを埋めるので、
		//     実際に「PDF だけバッジが無い」状態になるのは "画面に一度も出していない" ときだけ。
		if (!sOldNumFontTried && KESCMIsMainThread())
		{
			sOldNumFontTried = kTrue;
			// ★InterfacePtr(p, iid) は p==nil を許す(InterfacePtr.h:459 QueryInterface_ が nil チェック済み)
			//   ので、session が終了処理中に nil でもここは安全に fontMgr==nil になるだけ。明示ガードが
			//   要るのは session->QueryApplication() のような「直接のメソッド呼び出し」だけ(2026-07-25 追補 整理)。
			InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), UseDefaultIID());
			if (fontMgr != nil)
			{
				sOldNumFont = fontMgr->QueryFont(fontMgr->GetDefaultFontName());
				if (sOldNumFont != nil)
				{
					// ★サイズはドキュメント拡大率50%相当で固定(ユーザー指定 2026-07-15)なので、
					//   インスタンス(フォント×行列)も不変=キャッシュ可。
					const PMReal cacheSize = kKESCMOldNumFontPx / kKESCMOldNumFixedZoom;
					PMMatrix fontMatrix(cacheSize, 0.0, 0.0, cacheSize, 0.0, 0.0);
					sOldNumFontInst = fontMgr->QueryFontInstance(sOldNumFont, fontMatrix);
				}
			}
		}
		IPMFont*       numFont  = sOldNumFont;
		IFontInstance* fontInst = sOldNumFontInst;	// nil でも下の分岐がフォールバック値を使う
		if (pageList != nil && numFont != nil)
		{
			// ★サイズはドキュメント拡大率50%相当で固定(ユーザー指定 2026-07-15)。sxr(画面/印刷の実効
			//   スケール)ではなく固定値で割る=ズームでも印刷でもページに対して一定の大きさになる。
			const PMReal fontSize = kKESCMOldNumFontPx / kKESCMOldNumFixedZoom;
			const PMReal margin   = kKESCMOldNumMarginPx / kKESCMOldNumFixedZoom;

			const int32 npn = spread->GetNumPages();

			// ★このスプレッドに「番号がズレているページ」が有り得るかを、先頭ページで1回だけ先に判定する
			//   (2026-07-27 の無駄取り)。ズレは「このスプレッドより前に隠しスプレッドがあるか」で決まるので、
			//   同じスプレッドのページはすべて同じ条件下にある: 先頭が一致＝隠しの影響が及んでいないか、
			//   番号を固定するセクション内にあるかのどちらかで、後続ページはその連番なのでやはり一致する。
			//   隠しスプレッドが1つも無い通常の文書ではこれが常に成立し、以前は毎描画・全ページで
			//   GetPageString を2回ずつ呼んで必ず「一致」で捨てていた(バッジ ON の間ずっと空振り)。
			//   ズレていた場合だけ従来どおり全ページを回す(ループ内の per-page 判定はそのまま残すので、
			//   スプレッドの途中でセクションが始まってズレが解消するページも正しく飛ばせる)。
			bool16 spreadMayShift = kFalse;
			if (npn > 0)
			{
				const UID probeUID = spread->GetNthPageUID(0);
				PMString probeOrig, probeCur;
				pageList->GetPageString(probeUID, &probeOrig, kFalse, kFalse, kDefaultPageType, kTrue, kTrue);
				pageList->GetPageString(probeUID, &probeCur,  kFalse, kFalse, kDefaultPageType, kTrue, kFalse);
				spreadMayShift = (probeOrig != probeCur);
			}

			for (int32 i = 0; spreadMayShift && i < npn; ++i)
			{
				const UID pageUID = spread->GetNthPageUID(i);
				PMString orig, cur;
				// 第3引数 bIncludeSectionName=kFalse=セクションプレフィックス("A:"等)を付けない=番号のみ
				// (ユーザー指定 2026-07-15)。元/現在の両方を同じ設定で取り、ズレ判定を狂わせない。
				pageList->GetPageString(pageUID, &orig, kFalse, kFalse, kDefaultPageType, kTrue, kTrue);	// 元(隠し込みで数えた番号)
				pageList->GetPageString(pageUID, &cur,  kFalse, kFalse, kDefaultPageType, kTrue, kFalse);	// 現在(隠しを飛ばした番号)
				if (orig == cur)
					continue;	// ズレていない(このページより前に隠しスプレッドが無い)

				InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
				if (pageGeo == nil)
					continue;
				// ★2026-08-16(B3・A-1): Facade で spread 座標のまま取る(理由は KESCMDrawEntryOnPage)。
				const PMRect pr = Utils<Facade::IGeometryFacade>()->GetItemBounds(
					UIDRef(db, pageUID), Transform::SpreadCoordinates(), Geometry::PathBounds());

				PMReal textW = 0.0;
				if (fontInst != nil)
					fontInst->MeasureWText(orig, textW);
				const PMReal ascent  = (fontInst != nil) ? fontInst->GetAscent()  : (fontSize * PMReal(0.8));
				const PMReal descent = (fontInst != nil) ? fontInst->GetDescent() : (fontSize * PMReal(0.2));
				const PMReal tx = (pr.Left() + pr.Right()) / 2 - textW / 2;	// 下端中央(横センター)
				const PMReal ty = pr.Bottom() - margin - descent;			// ベースライン(下端から余白+descent 上)

				const int32 nch = orig.NumUTF16TextChars();
				const UTF16TextChar* buf16 = orig.GrabUTF16Buffer(nil);

				AutoGSave ag(gPort);
				// バッジ(白フチ+黒文字、背景なし)を透明グループで1つに束ね、グループの合成に
				// SelectedMarkOpacity(枠と同じ25%/75%連動、画面と印刷で同値)を1回だけ適用する。
				// starttransparencygroup は開始時点の GState(=直前の setopacity)をグループ合成に引き継ぎ、
				// グループ内の alpha は 1.0 にリセットされる(IGraphicsPort.h の仕様)。つまり中は全部不透明で
				// 描けるので、白フチと青本体が重なる縁でも濃度が変わらない(setopacity のまま重ねると
				// 重なった画素だけ濃くなる)。cs=nil は非隔離グループ=親のカラースペースを使うので問題ない。
				const PMReal pad = fontSize * kKESCMOldNumPadEm;	// 透明グループ bbox の余白(白フチのはみ出しを含む)
				const PMRect badgeRect(tx - pad, ty - ascent - pad, tx + textW + pad, ty + descent + pad);
				gPort->setopacity(SelectedMarkOpacity(), kFalse);	// グループ全体の合成不透明度
				gPort->starttransparencygroup(badgeRect, nil, kFalse /*non-isolated*/, kFalse /*no knockout*/);

				gPort->selectfont(numFont, fontSize);
				// 白フチ(中心±で8方向にずらして白 show)→ 本体(既定=黒)。背景の白塗りは廃止(ユーザー指定 2026-07-15)。
				// 透明背景でも明暗どちらの下地でも読める(カーソルの✓ハローと同方式)。
				const PMReal halo = fontSize * kKESCMOldNumHaloEm;
				// ★2026-08-16: 印刷/PDF は CMYK(白 = 全版 0)。PDF/X-1a は RGB を許さない。
				KESCMSetOutputColor(gPort, 255, 255, 255, printing);	// 白フチ
				for (int32 dy = -1; dy <= 1; ++dy)
					for (int32 dx = -1; dx <= 1; ++dx)
						if (dx != 0 || dy != 0)
							gPort->show(tx + halo * dx, ty + halo * dy, nch, buf16);
				// 本体(定数どおり=既定は黒。2026-07-15 に青→黒)。
				// ★2026-08-16: 印刷/PDF は CMYK。⚠定数は PMReal(0.0〜1.0) なので 0..255 へ直してから渡す
				//   (定数を変えても追随するように、直値を書かない)。既定の黒なら K100 になる。
				KESCMSetOutputColor(gPort,
					(uint8)::ToInt32(::Round(kKESCMOldNumR * PMReal(255.0))),
					(uint8)::ToInt32(::Round(kKESCMOldNumG * PMReal(255.0))),
					(uint8)::ToInt32(::Round(kKESCMOldNumB * PMReal(255.0))), printing);
				gPort->show(tx, ty, nch, buf16);

				gPort->endtransparencygroup();
			}
		}
	}

	// overflow("/"の未比較ページ)集合は EnsureOverflowCache() が保持済み(sOverflowT=Target/
	// sOverflowS=Source)。このスプレッドの各ページが overflow 側に入っているかを count で見るだけ。

	// Source 文書側のリング(Show Marks on Source) — 現スプレッドが Source 文書のものなら、対応表
	// (SourceページUID→TargetページUID)経由で同じリング画像を Source ページに重ねる。
	// トグル ON の間は常時表示(ツール左hold と無関係)。不透明度はパネルの 25%/75% 選択
	// (SelectedMarkOpacity)固定で、印刷文脈でも冒頭の suppressForPrint(印刷のみの抑制)を通り抜けて
	// ここへ来る(印刷経路は KESCMDrawRingForPrint が同じ SelectedMarkOpacity を使う=画面と印刷の
	// 見た目一致。OPP は 2026-07-05 からそもそも抑制対象外)。
	// Target と同一 db(想定外の自己比較)は下の Target 側描画に任せ、二重描画を避ける。
	// ★★★2026-08-15(第2段 Task 12B)= **生ポインタ比較を KESCMIsSameDoc へ置き換えた。**
	//   バックグラウンド(PDF の非同期書き出し)には**クローンされた別 DB** が渡るので、`db == sSrcDB` は
	//   必ず偽になり、Source 側の枠が書き出しに一切出なかった。同一性はファイル(GetSysFile)で聞く。
	//   ⚠メインスレッドでは同一ポインタで即決するので、従来の判定と結果は1つも変わらない。
	if (wantSrcMarks && KESCMIsSameDoc(db, sSrcDB) && !KESCMIsSameDoc(db, sDB))
	{
		// ★2026-08-15(第2段 Task 12B): Target 側ループと同じ理由でロックを取る(下の return まで保持)。
		KESCMMarkStateLock srcMarkLock(KESCMMarkStateMutex());
		const int32 nps = spread->GetNumPages();
		// ★緑ベタ塗りは「どこを比較から外しているか」を見せる**画面用の診断表示**なので、印刷/PDF には出さない
		//   (!printing。2026-08-06 の監査 E-4)。リングのような校正マークと違い下のデザインを覆うため。
		//   ⚠以前は Source 側だけが sPrintMarks を見ない経路(Source 枠は常に印刷に出す仕様)に乗っていたので、
		//   「Print comparison marks」OFF でも Source の印刷に緑が乗っていた。Target/Source とも画面限定に揃える。
		const bool16 fillExcluded = !printing && KESCMGetIgnorePageNumberMarker();	// ノンブル除外領域の緑ベタ塗り(除外トグルON・画面のみ)
		for (int32 i = 0; i < nps; ++i)
		{
			const UID srcPageUID = spread->GetNthPageUID(i);
			// 登録済み/overflow の判定は下の if/else 連鎖と除外塗りの両方で要るので、1ページにつき
			// 1回ずつ引いて使い回す(以前は同じ問い合わせをページごとに2回ずつ行っていた)。
			const bool16 isRegistered = KESCMPageMapIsRegistered(db, srcPageUID);
			const bool16 isOverflow   = (sOverflowS.count(srcPageUID) > 0);
			std::map<UID, UID>::iterator mp = sSrcPageToTarget.find(srcPageUID);
			if (mp != sSrcPageToTarget.end())
			{
				std::map<UID, KESCMOverlayEntry*>::iterator it = sEntries.find(mp->second);
				if (it != sEntries.end())
				{
					if (isThumb)
						KESCMDrawPageBorder(gPort, db, srcPageUID, sxr, drawMode, SelectedMarkOpacity(), kKESCMRingR, kKESCMRingG, kKESCMRingB);
					else
						KESCMDrawEntryOnPage(gPort, vpAttr, it->second, db, srcPageUID, sxr, drawMode, SelectedMarkOpacity());
				}
			}
			else if (isRegistered)
			{
				// 対応表に無い(=比較対象外)Source ページ。登録済み("Removed")なら緑「/」を描く。
				KESCMDrawPageDiagonal(gPort, db, srcPageUID, sxr, drawMode, SelectedMarkOpacity(), kKESCMAddedBorderR, kKESCMAddedBorderG, kKESCMAddedBorderB);
			}
			else if (isOverflow)
			{
				// 登録もされていない、ページ数差であふれたページ。未比較であることを赤斜線で明示する。
				KESCMDrawPageDiagonal(gPort, db, srcPageUID, sxr, drawMode, SelectedMarkOpacity(), kKESCMRingR, kKESCMRingG, kKESCMRingB);
			}
			// 除外トグルON時、実際に比較しているページ(=登録済みRemovedでも overflow でもない=対応表に
			// 入るページ)にだけ除外領域の緑ベタ塗りを重ねる。変更なしで entry が無いページにも出すので
			// 上の if/else とは独立に判定する。Removed/overflow ページは画素比較自体を行わない
			// (ノンブル除外という概念が無い)ので塗らない。
			if (!isThumb && fillExcluded && !isRegistered && !isOverflow)
				KESCMDrawPageNumberMarkerFill(gPort, db, srcPageUID);
		}
		return kFalse;	// Source 文書に Target 側オーバーレイは無い=ここで終わり
	}

	// ★2026-07-11: 「登録専用パス」(比較対象でない文書=未 Start 時に登録「/」を描く経路)は撤去した
	//   (ユーザー指定: 未 Start では Add/Remove の「/」をドキュメント・Pages パネルとも出さない)。登録「/」は
	//   Start 中の Target/Source メインループ(下の Target ループ・上の Source ループ)だけが描く。

	// 変更オーバーレイ(リング) — マーク済みドキュメントが現スプレッドの db と一致する時だけ。
	// master 表示トグル(sMarksVisible)が OFF の間、またはこのスプレッドを覗き中(旧版べた載せ中)は描かない
	// (データは保持=再表示で即復帰)。覗いていない他のスプレッドのマークは通常どおり残る。
	// ★印刷マーク(sPrintMarks)が ON の間は、ツール左hold に関係なく常に描く(画面=WYSIWYG / 印刷・PDF にも出る)。
	// ★★★2026-08-15(第2段 Task 12B)= **ここが「PDF 書き出しにマークが出ない」の本体だった。**
	//   `db != sDB` は BG では必ず真(クローンの別ポインタ)になるので、Target 側のマークが1つも描かれず、
	//   非同期書き出しの PDF は「Print comparison marks を OFF にしたもの」と**バイト単位で同一**だった
	//   (2026-08-15 実測 = docs/ai-notes/kescm-task12-pdf-export-marks-2026-08-15.md)。
	//   ⇒ ファイル同一性で聞き直す。★下のループは**ページ UID で sEntries を引く**ので、
	//     ここさえ通れば中身は1行も変えずに BG でも正しく動く(UID がクローンをまたいで保たれることは
	//     Task 11C で実測済み)。
	if (peekingThisSpread || !wantMarks || sDB == nil || !KESCMIsSameDoc(db, sDB))
		return kFalse;

	// 画面マークの実効不透明度。sMarkScreenOpacity は常に実効値を保持する(下の各ソースが設定):
	//   ・ツール左hold中 = 選択不透明度(パネルの 25%/75%)
	//   ・押していない時 = 基準値 KESCMBaseScreenOpacity()(印刷ONなら選択不透明度 / 印刷OFFは1.0)
	// 離すと基準値へ戻る。printing 経路はここを使わず、KESCMDrawRingForPrint が SelectedMarkOpacity を直接使う。
	const PMReal screenMarkOp = sMarkScreenOpacity;

	// このスプレッドの各ページについて、エントリがあれば描く(描画本体は KESCMDrawEntryOnPage に共通化)。
	// ★★2026-08-15(第2段 Task 12B): **ここから先はマーク集合を読むのでロックを取る。**
	//   守っているのは2つ: ①sEntries の要素が読んでいる最中に DropAll/MakeEntry で delete されること
	//   ②KESCMDrawEntryOnPage が e->buf を BuildRing で書き替えるので、main(画面ズーム基準の半径)と
	//     BG(印刷用に sxr=1.0 固定の半径)が**同じバッファを取り合う**こと。
	//   ⚠ロックはこの関数の残り(＝描画ループ)を丸ごと覆う。描画自体は数msなので待ちは実用上問題ない。
	KESCMMarkStateLock markLock(KESCMMarkStateMutex());
	const int32 np = spread->GetNumPages();
	// ★緑ベタ塗りは「どこを比較から外しているか」を見せる**画面用の診断表示**なので、印刷/PDF には出さない
	//   (!printing。2026-08-06 の監査 E-4)。リングのような校正マークと違い下のデザインを覆うため。
	//   ⚠以前は Source 側だけが sPrintMarks を見ない経路(Source 枠は常に印刷に出す仕様)に乗っていたので、
	//   「Print comparison marks」OFF でも Source の印刷に緑が乗っていた。Target/Source とも画面限定に揃える。
	const bool16 fillExcluded = !printing && KESCMGetIgnorePageNumberMarker();	// ノンブル除外領域の緑ベタ塗り(除外トグルON・画面のみ)
	for (int32 i = 0; i < np; ++i)
	{
		const UID pageUID = spread->GetNthPageUID(i);
		// 登録済み/overflow の判定は下の if/else 連鎖と除外塗りの両方で要るので、1ページにつき
		// 1回ずつ引いて使い回す(以前は同じ問い合わせをページごとに2回ずつ行っていた)。
		const bool16 isRegistered = KESCMPageMapIsRegistered(db, pageUID);
		const bool16 isOverflow   = (sOverflowT.count(pageUID) > 0);
		std::map<UID, KESCMOverlayEntry*>::iterator it = sEntries.find(pageUID);
		if (it != sEntries.end())
		{
			if (isThumb)
				KESCMDrawPageBorder(gPort, db, pageUID, sxr, drawMode, screenMarkOp, kKESCMRingR, kKESCMRingG, kKESCMRingB);
			else
				KESCMDrawEntryOnPage(gPort, vpAttr, it->second, db, pageUID, sxr, drawMode, screenMarkOp);
		}
		else if (isRegistered)
		{
			// 比較エントリが無い(=対象外)Target ページ。登録済み("Added")なら緑「/」を描く。
			KESCMDrawPageDiagonal(gPort, db, pageUID, sxr, drawMode, screenMarkOp, kKESCMAddedBorderR, kKESCMAddedBorderG, kKESCMAddedBorderB);
		}
		else if (isOverflow)
		{
			// 登録もされていない、ページ数差であふれたページ。未比較であることを赤斜線で明示する。
			KESCMDrawPageDiagonal(gPort, db, pageUID, sxr, drawMode, screenMarkOp, kKESCMRingR, kKESCMRingG, kKESCMRingB);
		}
		// 除外トグルON時、実際に比較しているページ(=登録済みAddedでも overflow でもない=対応表に
		// 入るページ)にだけ除外領域の緑ベタ塗りを重ねる。変更なしで entry が無いページにも出すので
		// 上の if/else とは独立に判定する。Added/overflow ページは画素比較自体を行わない
		// (ノンブル除外という概念が無い)ので塗らない。
		if (!isThumb && fillExcluded && !isRegistered && !isOverflow)
			KESCMDrawPageNumberMarkerFill(gPort, db, pageUID);
	}

	return kFalse;	// 他のハンドラ・描画を続行させる
}


//========================================================================================
// KESCMDrawEventSrvc
//   kDrawEventService サービスとして自身を登録する。アプリ起動時にこのサービスが見つかり、
//   同じ boss 上の IDrwEvtHandler が描画イベントディスパッチャに登録される。
//========================================================================================
class KESCMDrawEventSrvc : public CServiceProvider
{
public:
	KESCMDrawEventSrvc(IPMUnknown* boss) : CServiceProvider(boss) {}
	~KESCMDrawEventSrvc() {}

	virtual ServiceID GetServiceID() { return kDrawEventService; }
	virtual bool16 IsDefaultServiceProvider() { return kFalse; }
	virtual InstancePerX GetInstantiationPolicy() { return IK2ServiceProvider::kInstancePerSession; }
	// ★内部名(翻訳対象ではない)なので SetCString。製品 dynamicdocumentsui/motion/MotionPathDrawService.cpp:44
	//   と同じ流儀。⚠サンプル basicdrwevthandler/BscDEHDrwEvtSrvc.cpp:131 は SetKey=2流儀(2026-08-06 に製品側へ)。
	//   ⚠末尾の "\0" はそのサンプルの写しだったので落とした(C 文字列は終端で切れるので挙動は元から同じ)。
	virtual void GetName(PMString* pName) { pName->SetCString("KESCMDrawEventSrvc"); }
	// ★★GetThreadingPolicy は**書かない**(2026-08-14 に手書き override を撤去した)。
	//   CServiceProvider が「boss の居るプラグインの型」から既定を返す ---- UI→kMainThreadOnly /
	//   model→kMultipleThreads(ガイド vol1-07「Threading and service providers」)。
	//   ∴ kUIPlugIn の現在は撤去前と**完全に同値**(動作は1バイトも変わらない)で、第2段で
	//   kModelPlugIn にした瞬間に**自動で kMultipleThreads** になる。
	//   ⚠**手書きで kMainThreadOnly を返していると、model にしても PDF 書き出しにマークが出ない**
	//     (2x2 実測の3行目 = docs/ai-notes/draw-event-pdf-export-experiment-2026-08-12.md)。
	//     つまり「消し忘れ」が分割の目的を**無言で**殺す。だから第2段を待たず先に消してある。
	//   ★同じ理由を KESCMUIDrawEvent.cpp の冒頭にも書いてある(あちらは最初から書いていない)。
};

CREATE_PMINTERFACE(KESCMDrawEventSrvc, kKESCMDrawEventSrvcImpl)

