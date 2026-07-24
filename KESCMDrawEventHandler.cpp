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
#include "IDocument.h"
#include "ILayoutUtils.h"
#include "ILayoutUIUtils.h"
#include "IApplication.h"
#include "IDocumentList.h"
#include "ISpread.h"
#include "ISpreadList.h"
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
#include "PMPoint.h"
#include "PMReal.h"
#include "TransformUtils.h"
#include "SnapshotUtilsEx.h"
#include "AGMImageAccessor.h"
#include "GraphicsExternal.h"
#include "IXPUtils.h"

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
#include "KESCMPageCheck.h"          // KESCMPageCheckIsChecked/KESCMPageCheckHasAny(「KESCM: Check」の✓)
#include "KESCMPageNumberMarker.h"   // KESCMGetIgnorePageNumberMarker/KESCMAppendPageNumberMarkerRects(ノンブル除外)
#include "KESCMScrollMap.h"          // KESCMScrollMapNoticeDrawEvent(手動 Hide/Show Spread の検出)
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
bool16 KESCMDrawEventHandler::sSrcMarksOn = kFalse;	// 既定=OFF。Start(KESCMDoMarkChangesDoc)のたびに kTrue へ(フライアウト「Show Marks on Source」)
IDataBase* KESCMDrawEventHandler::sSrcDB = nil;
std::map<UID, UID> KESCMDrawEventHandler::sSrcPageToTarget;
std::map<UID, UID> KESCMDrawEventHandler::sPrevPairTargetToSource;	// 前回比較のペアリング(登録トグルの差分再比較用)
std::set<UID> KESCMDrawEventHandler::sOverflowT;					// overflow("/")ページ集合キャッシュ(Target側)
std::set<UID> KESCMDrawEventHandler::sOverflowS;					// 同(Source側)
IDataBase* KESCMDrawEventHandler::sOverflowCacheDB = nil;			// 上記キャッシュを作った時の sDB
IDataBase* KESCMDrawEventHandler::sOverflowCacheSrcDB = nil;			// 同 sSrcDB
bool16 KESCMDrawEventHandler::sRasterizing = kFalse;	// 自前ラスタ化中だけ kTrue(自己参照防止)
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


//========================================================================================
// overflow キャッシュ("/"の未比較ページ集合)。以前は HandleDrawEvent が描画のたびに
// KESCMBuildPairing(両文書の全ページ走査)を呼んでいたのを、比較実行時に1回作って保持する形へ。
//========================================================================================
void KESCMDrawEventHandler::RebuildOverflowCache()
{
	sOverflowT.clear();
	sOverflowS.clear();
	sOverflowCacheDB    = sDB;
	sOverflowCacheSrcDB = sSrcDB;
	if (sDB != nil && sSrcDB != nil)
	{
		std::vector<UID> tp, sp, tov, sov;
		KESCMBuildPairing(sDB, sSrcDB, tp, sp, &tov, &sov);
		sOverflowT.insert(tov.begin(), tov.end());
		sOverflowS.insert(sov.begin(), sov.end());
	}
}

void KESCMDrawEventHandler::EnsureOverflowCache()
{
	// 控えた (sDB,sSrcDB) が現在と食い違う時だけ作り直す(文書切替・別文書へのスプレッド再比較の保険)。
	// 登録Add/Start/Ignore切替は KESCMDoMarkChangesDoc が RebuildOverflowCache を直接呼ぶので、ここは
	// 「同じ文書対のまま」の通常描画では何もしない=毎描画の全文書走査を避ける。
	if (sOverflowCacheDB != sDB || sOverflowCacheSrcDB != sSrcDB)
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


// (x,y)が rects のいずれかの矩形内にあるか(ノンブル除外領域の判定に使う)。
static bool16 KESCMPointInRects(int32 x, int32 y, const std::vector<Int32Rect>& rects)
{
	for (size_t i = 0; i < rects.size(); ++i)
	{
		const Int32Rect& r = rects[i];
		if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
			return kTrue;
	}
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
	SnapshotUtilsEx* snapTH = new SnapshotUtilsEx(targetRef, 1.0, 1.0, hiRes, hiRes, 0.0, SnapshotUtilsEx::kCsCMYK, kFalse);
	sRasterizing = kTrue;	// この Draw 中に再入する HandleDrawEvent はマークを描かない(自己参照防止)
	// アンチエイリアスを OFF にしてラスタ化する(第4引数 enableAntiAliasing=kFalse)。エッジの中間調(灰にじみ)を
	//   無くし、画素内で収まる微小ズレ由来の帯状ノイズが差分として拾われるのを抑える。fullRes / greek は既定維持。
	//   ※ target / source は必ず同じ AA 設定でラスタ化すること(片方だけだと全エッジが差分になる)。
	ErrorCode drewTH = snapTH->Draw(IShape::kPreviewMode, kFalse, 7.0, kFalse);
	sRasterizing = kFalse;
	AGMImageAccessor* accTH = (drewTH == kSuccess) ? snapTH->CreateAGMImageAccessor() : nil;

	SnapshotUtilsEx* snapSH = new SnapshotUtilsEx(sourceRef, 1.0, 1.0, hiRes, hiRes, 0.0, SnapshotUtilsEx::kCsCMYK, kFalse);
	sRasterizing = kTrue;
	ErrorCode drewSH = snapSH->Draw(IShape::kPreviewMode, kFalse, 7.0, kFalse);	// 同上: AA OFF(両者同条件)
	sRasterizing = kFalse;
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
					std::vector<PMRect> markerRects;
					KESCMAppendPageNumberMarkerRects(targetRef, markerRects);
					KESCMAppendPageNumberMarkerRects(sourceRef, markerRects);
					const PMReal pxScale = hiRes / PMReal(72.0);	// pt → 比較解像度のpx
					for (size_t mi = 0; mi < markerRects.size(); ++mi)
					{
						const PMRect& mr = markerRects[mi];
						Int32Rect epr;
						epr.left   = ::ToInt32(::Round(mr.Left()   * pxScale));
						epr.top    = ::ToInt32(::Round(mr.Top()    * pxScale));
						epr.right  = ::ToInt32(::Round(mr.Right()  * pxScale));
						epr.bottom = ::ToInt32(::Round(mr.Bottom() * pxScale));
						excludeRects.push_back(epr);
					}
				}

				// 【高解像度で比較 → 低解像度セルへ散らす(scatter)】
				// 高解像度の各画素を差分判定(生の各チャンネル最大差>しきい値)し、変化していたら
				// 対応する低解像度セルのカウンタを増やす。セル写像は寸法比(高/低が整数倍でなくてもよい)。
				// CMYK 比較: 先頭から4ch(offset=0)。各chの最大差がしきい値(kKESCMCmykThr)を超えたら変化画素。
				const int  nch       = 4;
				const int32 colorOffH = 0;
				const int  thr        = kKESCMCmykThr;
				for (int32 y = 0; y < hth; ++y)
				{
					const uint8* rowT = ptH + (size_t)y * rbTH;
					const uint8* rowS = psH + (size_t)y * rbTH;
					int32 yl = (int32)((int64)y * hl / hth);
					if (yl >= hl) yl = hl - 1;
					uint16* cntRow = cntHi + (size_t)yl * wl;
					for (int32 x = 0; x < wth; ++x)
					{
						if (!excludeRects.empty() && KESCMPointInRects(x, y, excludeRects))
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
					// mask M から距離変換 dist を1回だけ作って保持(以後の BuildRing はこれ1つで描ける)。
					//   dist 生成後、mask M はもう不要なので解放(常駐メモリは dist が mask を置換=純増ゼロ)。
					e->dist = new (std::nothrow) uint8[N];
					if (e->dist != nil)
						KESCMDistTransform(M, wl, hl, e->dist);
					delete[] M;

					// 初回リング(基準半径)を buf へ直接描く(dist 確保失敗時のみ透明クリアで安全に)。
					// buf 確保失敗(nil)時はここでは触らない。描画側(HandleDrawEvent)が e->buf==nil で skip する。
					e->buf = new (std::nothrow) uint8[(size_t)rbL * hl];
					if (e->buf != nil)
					{
						if (e->dist != nil)
							BuildRing(e->buf, rbL, bppL, wl, hl, e->dist, BG, kKESCMBaseRadius);
						else
							memset(e->buf, 0, (size_t)rbL * hl);
					}
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
					UID key = targetRef.GetUID();
					std::map<UID, KESCMOverlayEntry*>::iterator old = sEntries.find(key);
					if (old != sEntries.end()) { delete old->second; sEntries.erase(old); }
					sEntries[key] = e;

					// Source 側描画(Show Marks on Source)用の対応表もここで記録する。エントリ登録と同じ場所に
					// 置くことで、旧 Ctrl+ミドルのスプレッド再比較(MakeEntry 直呼び)でも対応が自動で維持される。
					// 対応表の掃除は DropAll(エントリと運命共同体)。
					sSrcDB = sourceRef.GetDataBase();
					sSrcPageToTarget[sourceRef.GetUID()] = key;

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
	SnapshotUtilsEx* snap = new SnapshotUtilsEx(sourceRef, 1.0, 1.0, resolution, resolution, 0.0, SnapshotUtilsEx::kCsRGB, kFalse);
	sRasterizing = kTrue;	// この Draw 中に再入する HandleDrawEvent はマークを描かない(自己参照防止)
	ErrorCode drew = snap->Draw(IShape::kPreviewMode);
	sRasterizing = kFalse;
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
	// (トースト撤去(2026-07-04)に伴い、カンバス背景帯用の kAfterLastSpreadDrawMessage 登録は廃止)
	d->RegisterHandler(ClassID(kEndSpreadMessage), this, kDEHLowestPriority);
}

void KESCMDrawEventHandler::UnRegister(IDrwEvtDispatcher* d)
{
	d->UnRegisterHandler(ClassID(kEndSpreadMessage), this);
}


// ビューから IPanorama を取る。ページアイテム系の子ウィジェットは panorama を持たないため、
// CTracker::QueryPanorama と同じく自身→親(LayoutWidget)の順で辿る。呼び出し側で Release すること。
IPanorama* KESCMQueryPanorama(IControlView* view)
{
	if (view == nil)
		return nil;
	IPanorama* pano = (IPanorama*)view->QueryInterface(IID_IPANORAMA);
	if (pano != nil)
		return pano;
	InterfacePtr<IWidgetParent> parent(view, IID_IWIDGETPARENT);
	if (parent == nil)
		return nil;
	return (IPanorama*)parent->QueryParentFor(IID_IPANORAMA);
}

//========================================================================================
// 印刷/PDF 用のリング描画。画面は image() blit でよいが(画素 alpha を honor する)、印刷のフラットナ
// 経路は blit 画像の部分 alpha を honor せず枠が不透明になる。そこで transparencyeffect サンプルと
// 同じ作法=リング形状を「グレーのアルファサーバ」にして純色のベクター fill を setopacity で半透明に
// 描く(透明合成エンジンが honor する)。赤と青(背景適応)を保つため、赤画素・青画素それぞれのグレー
// マスクで2回 fill する。呼び出し側で translate/scale 済み(user 空間 = 画像px)であること。
//   e->buf は ARGB(先頭=alpha, 続いて R,G,B)。
//========================================================================================
static void KESCMDrawRingForPrint(IGraphicsPort* gPort, KESCMOverlayEntry* e)
{
	if (gPort == nil || e == nil || e->buf == nil || e->w <= 0 || e->h <= 0 || e->bpp < 4)
		return;
	// 透明合成ユーティリティ(アルファサーバ生成/解放に使う)。実行中アプリでは常在するが、
	// transparencyeffect サンプル流に、取得できなければ何もしない(クラッシュ回避)。以後この1個を使い回す。
	Utils<IXPUtils> xpUtils;
	if (!xpUtils)
		return;
	const int32 w = e->w, h = e->h, rb = e->rowBytes, bpp = e->bpp;
	const size_t N = (size_t)w * h;

	// e->buf(ARGB)から、赤リング画素=255 / 青リング画素=255 の2枚のグレーマスクを作る。
	uint8* maskR = new (std::nothrow) uint8[N];	// nothrow: 直下の nil チェックを実効化(失敗時は枠を描かないだけ)
	uint8* maskB = new (std::nothrow) uint8[N];
	if (maskR == nil || maskB == nil) { if (maskR) delete[] maskR; if (maskB) delete[] maskB; return; }
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
			}
			else { maskR[idx] = 0; maskB[idx] = 0; }
		}
	}

	// リングの不透明度=パネルで選択中の 25%/75%(画面表示と共通の SelectedMarkOpacity)。
	const PMReal op = KESCMDrawEventHandler::SelectedMarkOpacity();
	// 既知の制限: 透明効果のあるページでは、ここで描く枠/リングがフラットナにラスタ化され、CMYK 変換で
	// 色がやや沈む(透明画像のあるページだけ枠が濃く見える)。色を CMYK 指定にしても解消せず(=色値ではなく
	// 透明機能で描いていることが原因)、不透明ベクター化は25%の「透け」を失うため見送り。現状は元の RGB 指定のまま。
	struct PassDef { uint8* buf; uint8 r, g, b; };
	PassDef passes[2] = { { maskR, 255, 0, 0 }, { maskB, 0, 0, 255 } };	// 赤 / 青

	for (int p = 0; p < 2; ++p)
	{
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
			gPort->setopacity(op, kFalse);							// 半透明(透明合成が honor)
			gPort->setrgbcolor(passes[p].r / PMReal(255.0), passes[p].g / PMReal(255.0), passes[p].b / PMReal(255.0));
			gPort->newpath();
			gPort->rectpath(PMReal(0.0), PMReal(0.0), PMReal(w), PMReal(h));	// user 空間=画像px(呼び出し側で translate/scale 済)
			gPort->fill();
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
static void KESCMDrawEntryOnPage(IGraphicsPort* gPort, KESCMOverlayEntry* e, IDataBase* db, UID pageUID,
	const PMReal& sxr, int32 drawMode, const PMReal& screenOpacity)
{
	if (e == nil || e->buf == nil)
		return;

	const int32 iw = e->w, ih = e->h;
	InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
	if (iw <= 0 || ih <= 0 || pageGeo == nil)
		return;

	// 【座標の肝】kEndSpreadMessage の描画ポートは spread 座標。ページ inner bbox を
	// InnerToSpreadMatrix で spread 座標へ変換してフィットさせる。
	PMRect pr = pageGeo->GetPathBoundingBox();			// ページ inner
	PMMatrix m = ::InnerToSpreadMatrix(pageGeo);
	m.Transform(&pr);									// → spread(=描画ポート)座標

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
		const PMReal kKESCMClipInset = 1.0;	// pt
		gPort->rectclip(pr.Left()   + kKESCMClipInset, pr.Top()    + kKESCMClipInset,
		                pr.Width()  - kKESCMClipInset * 2.0, pr.Height() - kKESCMClipInset * 2.0);
		gPort->translate(pr.Left(), pr.Top());				// ページ左上へ
		gPort->scale(pr.Width() / iw, pr.Height() / ih);	// 画像px → ページ矩形にフィット
		// ★印刷/PDF 時は image() blit だと枠が不透明になる(フラットナが画像の部分 alpha を honor しない)。
		// アルファサーバ＋純色ベクター fill＋setopacity で半透明に描く(透明合成エンジンが honor)。
		// 画面は image() blit(画素 alpha を honor=実測確認済み)+選択不透明度(25%/75%)。
		if (drawMode == kKESCMDrawModePrint)
			KESCMDrawRingForPrint(gPort, e);
		else
		{
			// サムネイル(sxr<=0)は不透明100%で描く(極小表示で 25%/75% だと沈んで見えないため)。
			const PMReal blitOpacity = (sxr <= 0) ? PMReal(1.0) : screenOpacity;
			gPort->setopacity(blitOpacity, kFalse);
			gPort->image(&e->rec, PMMatrix(), 0);			// 自前レコード(buf を指す)を blit
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

	// 【座標】KESCMDrawEntryOnPage と同じく、ページ inner bbox を spread 座標へ変換。
	PMRect pr = pageGeo->GetPathBoundingBox();
	PMMatrix m = ::InnerToSpreadMatrix(pageGeo);
	m.Transform(&pr);

	// 【太さ】画面/印刷はズーム適応(px/sxr)、サムネイル(sxr<=0)はページ短辺の固定比率(枠専用の除数)。
	const PMReal minDim = (pr.Width() < pr.Height() ? pr.Width() : pr.Height());
	PMReal w = (sxr > 0) ? (kKESCMRingTargetPx / sxr) : (minDim / PMReal(kKESCMThumbBorderDivisor));
	const PMReal maxW = minDim / PMReal(2.0) - PMReal(0.5);
	if (w > maxW) w = maxW;
	if (w < PMReal(0.5))
		return;	// ページが小さすぎて太さが潰れる場合は描かない

	// 【クリップ相当】通常マークと同じく、ノドの共有線に届かないよう約1pt内側から描く。
	const PMReal kKESCMClipInset = 1.0;	// pt
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
	gPort->setrgbcolor(cr / PMReal(255.0), cg / PMReal(255.0), cb / PMReal(255.0));
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

	std::vector<PMRect> markerRects;
	KESCMAppendPageNumberMarkerRects(UIDRef(db, pageUID), markerRects);
	if (markerRects.empty())
		return;

	InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
	if (pageGeo == nil)
		return;

	PMRect pr = pageGeo->GetPathBoundingBox();		// ページ inner
	PMMatrix m = ::InnerToSpreadMatrix(pageGeo);
	m.Transform(&pr);								// → spread(=描画ポート)座標

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

	PMRect pr = pageGeo->GetPathBoundingBox();
	PMMatrix m = ::InnerToSpreadMatrix(pageGeo);
	m.Transform(&pr);

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
	const PMReal kKESCMClipInset = 1.0;	// pt
	gPort->rectclip(pr.Left()   + kKESCMClipInset, pr.Top()    + kKESCMClipInset,
	                pr.Width()  - kKESCMClipInset * 2.0, pr.Height() - kKESCMClipInset * 2.0);
	gPort->setopacity(opacity, kFalse);
	gPort->setrgbcolor(cr / PMReal(255.0), cg / PMReal(255.0), cb / PMReal(255.0));
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

	PMRect pr = pageGeo->GetPathBoundingBox();
	PMMatrix m = ::InnerToSpreadMatrix(pageGeo);
	m.Transform(&pr);								// → spread(=描画ポート)座標

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
	const PMReal kKESCMClipInset = 1.0;	// pt
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
// ページ中央に ✓(チェックマーク)をベクター線で描く(色指定)。「KESCM: Check」でチェックした
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

	PMRect pr = pageGeo->GetPathBoundingBox();
	PMMatrix m = ::InnerToSpreadMatrix(pageGeo);
	m.Transform(&pr);

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
	gPort->setrgbcolor(cr / PMReal(255.0), cg / PMReal(255.0), cb / PMReal(255.0));
	gPort->setlinewidth(w);
	gPort->newpath();
	gPort->moveto(lx, ly);
	gPort->lineto(vx, vy);
	gPort->lineto(rx, ry);
	gPort->stroke();
}


bool16 KESCMDrawEventHandler::HandleDrawEvent(ClassID eventID, void* eventData)
{
	DrawEventData* ded = static_cast<DrawEventData*>(eventData);
	if (ded == nil || ded->gd == nil)
		return kFalse;
	// 自前のラスタ化(MakeEntry の比較スナップショット / MakeOrigImage の旧版スナップショット)中の再入は
	// 描かない(自己参照=マークがスナップショットに写り込む feedback を防ぐ)。以前は kPreviewMode ビットで
	// 弾いていたが、それは PDF 書き出しの kPDFExportMode と同一ビット(4096)で export を巻き込んでいたため、
	// 明示的な再入フラグ sRasterizing に置き換えた。
	if (sRasterizing)
		return kFalse;
	// 印刷文脈か(kPrinting=512)。印刷時はマークの ON/OFF を sPrintMarks で決める。通常の画面描画では立たない。
	// ※PDF 書き出し(File>Export)はこのスプレッド描画イベントを発火しないため対象外(print-to-PDF を使う)。
	// 自己参照(自前スナップショット)は上の sRasterizing で防ぐので、ここで kPreviewMode は見ない。
	const bool16 printing = (ded->flags & IShape::kPrinting) != 0;

	// スクロールバー地図: ページパネルからの手動 Hide/Show Spread を検出する軽量チェック(250ms
	// スロットル付きの指紋比較)。手動の隠し/再表示は KESCM のフックを通らないが必ず再描画は起こす
	// ので、スプレッド描画イベントに便乗して拾う(Undo/Redo による変化も同経路)。KESCMScrollMap.cpp。
	if (!printing)
		KESCMScrollMapNoticeDrawEvent();
	// ★サムネイル実験(2026-07-06): Pagesパネルのサムネイル生成(view無し・kPreviewMode・非印刷。診断ログ
	// flags=0x1800=kPreviewMode|kDrawFrameEdge)を検出。sThumbExperiment ON の間は、サムネイルにも枠を
	// 描くため下で wantMarks を強制 ON にする(通常は sPrintMarks/sMarksVisible が OFF だと枠が出ない)。
	// サムネイルでは差分リング画像(KESCMDrawEntryOnPage)ではなく、下の Target/Source ループが
	// isThumb 分岐で KESCMDrawPageBorder(枠)/KESCMDrawPageDiagonal(「/」)を呼ぶ(極小表示で潰れない
	// 固定比率の太さ)。不透明度は kKESCMThumbMarkOpacity(0.75=少し透ける)。
	const bool16 isThumb = sThumbExperiment && !printing &&
		ded->gd != nil && ded->gd->GetView() == nil && (ded->flags & IShape::kPreviewMode) != 0;
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
		(sDB    != nil && KESCMPageCheckHasAny(sDB)) ||		// 「KESCM: Check」の✓(サムネイル描画を起こすため)
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
	// ★「KESCM: Check」の ✓ のレイアウトビュー版(2026-07-12)。画面では「常に」表示(ツール左hold・
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
	if (!wantMarks && !wantOrig && !wantOldNums && !wantSrcMarks && !wantChecks && !wantOversetThumb)
		return kFalse;

	GraphicsData* gd = ded->gd;
	IGraphicsPort* gPort = gd->GetGraphicsPort();
	if (gPort == nil)
		return kFalse;

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

	// changedBy = 今描いているスプレッド。
	InterfacePtr<ISpread> spread(ded->changedBy, UseDefaultIID());
	if (spread == nil)
		return kFalse;
	IDataBase* db = ::GetDataBase(ded->changedBy);
	if (db == nil)
		return kFalse;

	// ★保持マークのドキュメントが閉じられていたら破棄する(クローズ監視の代わり)。draw は開いている
	//   ドキュメントについてのみ発火するので、ここで sDB/sOrigDB の生存を確認できる。
	//   マークが無い通常時(sDB==nil かつ sOrigDB==nil)は何も問い合わせない=コストゼロ。
	//   ★以前はここで DropAll/DropAllOrig だけを個別に呼び、マークだけを消して peek arm やパネル表示は
	//   そのままにしていた(枠は消えるのにボタンは Stop のまま、という食い違いの原因)。通常はドキュメント
	//   クローズ responder(KESCMHandleDocsClosed)がクローズ直後に先回りして片付けるためこの分岐へは実質
	//   到達しないが、保険として残す以上は KESCMHandleDocsClosed に一本化し、Stop 相当のフルクリーンアップ
	//   (peek arm 解除・パネル更新も)を確実に行う。
	if (sDB != nil || sOrigDB != nil || sSrcDB != nil)
	{
		InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
		InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
		if (docList != nil &&
		    ((sDB != nil && docList->FindDocByDataBase(sDB) == nil) ||
		     (sOrigDB != nil && docList->FindDocByDataBase(sOrigDB) == nil) ||
		     (sSrcDB != nil && docList->FindDocByDataBase(sSrcDB) == nil)))
			KESCMHandleDocsClosed();
	}

	// 画面スケール(ズーム)を一度だけ取得。画面描画時のみ非nil。
	PMReal sxr = 0.0;
	IControlView* zview = gd->GetView();
	if (zview != nil)
	{
		PMMatrix toWin = zview->GetContentToWindowMatrix();	// content→window(画面px), 現ズーム
		sxr = toWin.GetXScale(); if (sxr < 0) sxr = -sxr;
	}

	// ★描画モードの決定(サムネイル生成は関数冒頭で早期 return 済みなのでここには来ない)。
	int32 drawMode = printing ? kKESCMDrawModePrint : kKESCMDrawModeScreen;

	// ★印刷/PDF 時は「100% 表示の見た目」に固定する(ズーム連動を切る)。印刷ポートには view が無く
	// sxr=0 / pano=nil になるので、実効 sxr=1.0(=100%・deviceScale 1 相当)を与える。これでリング太さの
	// 式が、画面 100% 表示時とちょうど同じ値になる(下流のズーム適応式をそのまま使い回せる)。
	// 画面描画は従来どおりズーム連動。
	if (printing)
		sxr = 1.0;

	// ★「KESCM: Check」の ✓(サムネイル版): チェック済みページの Pages パネルサムネイル中央に青い ✓ を描く。
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
			PMRect pr = pageGeo->GetPathBoundingBox();		// ページ inner
			PMMatrix m = ::InnerToSpreadMatrix(pageGeo);
			m.Transform(&pr);								// → spread(=描画ポート)座標
			AutoGSave ag(gPort);
			gPort->setopacity(sPeekOpacity, kFalse);		// Shift peek=1.0(不透明) / Ctrl peek=0.5(半透明)
			gPort->translate(pr.Left(), pr.Top());
			gPort->scale(pr.Width() / o->w, pr.Height() / o->h);	// 旧版画像をページ矩形にフィット
			gPort->image(&o->rec, PMMatrix(), 0);			// 旧版を sPeekOpacity で重ねる
		}
	}

	// ★「KESCM: Check」の ✓(レイアウトビュー/印刷版・2026-07-12)。チェック済みページのページ中央に
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
	// kFalse=隠しページを飛ばす(現在マーカーが表示している番号)。書式はセクション込み・セクションの番号スタイル
	// (bUseIntegerStyle=kFalse)=実際のノンブルと同じ見た目。文字は framelabel 流(selectfont+show)。
	// サイズはズーム非依存(fontSize=目標px/sxr。印刷時は sxr=1.0 固定=実寸 pt)。
	// 見た目=トースト風: 白い四角の塗りの上に赤の疑似ボールド。バッジ全体の不透明度は 25%/75% 選択に連動。
	if (wantOldNums && sxr > 0)
	{
		InterfacePtr<IPageList> pageList(db, db->GetRootUID(), UseDefaultIID());
		InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), UseDefaultIID());
		InterfacePtr<IPMFont> numFont(fontMgr != nil ? fontMgr->QueryFont(fontMgr->GetDefaultFontName()) : nil);
		if (pageList != nil && numFont != nil)
		{
			// ★サイズはドキュメント拡大率50%相当で固定(ユーザー指定 2026-07-15)。sxr(画面/印刷の実効
			//   スケール)ではなく固定値で割る=ズームでも印刷でもページに対して一定の大きさになる。
			const PMReal fontSize = kKESCMOldNumFontPx / kKESCMOldNumFixedZoom;
			const PMReal margin   = kKESCMOldNumMarginPx / kKESCMOldNumFixedZoom;
			PMMatrix fontMatrix(fontSize, 0.0, 0.0, fontSize, 0.0, 0.0);
			InterfacePtr<IFontInstance> fontInst(fontMgr->QueryFontInstance(numFont, fontMatrix));

			const int32 npn = spread->GetNumPages();
			for (int32 i = 0; i < npn; ++i)
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
				PMRect pr = pageGeo->GetPathBoundingBox();		// ページ inner
				PMMatrix pm = ::InnerToSpreadMatrix(pageGeo);
				pm.Transform(&pr);								// → spread(=描画ポート)座標

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
				// バッジ(白フチ+青文字、背景なし)を透明グループで1つに束ね、グループの合成に
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
				// 白フチ(中心±で8方向にずらして白 show)→ 青本体。背景の白塗りは廃止(ユーザー指定 2026-07-15)。
				// 透明背景でも明暗どちらの下地でも読める(カーソルの✓ハローと同方式)。
				const PMReal halo = fontSize * kKESCMOldNumHaloEm;
				gPort->setrgbcolor(1.0, 1.0, 1.0);	// 白フチ
				for (int32 dy = -1; dy <= 1; ++dy)
					for (int32 dx = -1; dx <= 1; ++dx)
						if (dx != 0 || dy != 0)
							gPort->show(tx + halo * dx, ty + halo * dy, nch, buf16);
				gPort->setrgbcolor(kKESCMOldNumR, kKESCMOldNumG, kKESCMOldNumB);	// 青本体
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
	if (wantSrcMarks && db == sSrcDB && db != sDB)
	{
		const int32 nps = spread->GetNumPages();
		const bool16 fillExcluded = KESCMGetIgnorePageNumberMarker();	// ノンブル除外領域の緑ベタ塗り(除外トグルON時)
		for (int32 i = 0; i < nps; ++i)
		{
			const UID srcPageUID = spread->GetNthPageUID(i);
			std::map<UID, UID>::iterator mp = sSrcPageToTarget.find(srcPageUID);
			if (mp != sSrcPageToTarget.end())
			{
				std::map<UID, KESCMOverlayEntry*>::iterator it = sEntries.find(mp->second);
				if (it != sEntries.end())
				{
					if (isThumb)
						KESCMDrawPageBorder(gPort, db, srcPageUID, sxr, drawMode, SelectedMarkOpacity(), kKESCMRingR, kKESCMRingG, kKESCMRingB);
					else
						KESCMDrawEntryOnPage(gPort, it->second, db, srcPageUID, sxr, drawMode, SelectedMarkOpacity());
				}
			}
			else if (KESCMPageMapIsRegistered(db, srcPageUID))
			{
				// 対応表に無い(=比較対象外)Source ページ。登録済み("Removed")なら緑「/」を描く。
				KESCMDrawPageDiagonal(gPort, db, srcPageUID, sxr, drawMode, SelectedMarkOpacity(), kKESCMAddedBorderR, kKESCMAddedBorderG, kKESCMAddedBorderB);
			}
			else if (sOverflowS.count(srcPageUID) > 0)
			{
				// 登録もされていない、ページ数差であふれたページ。未比較であることを赤斜線で明示する。
				KESCMDrawPageDiagonal(gPort, db, srcPageUID, sxr, drawMode, SelectedMarkOpacity(), kKESCMRingR, kKESCMRingG, kKESCMRingB);
			}
			// 除外トグルON時、実際に比較しているページ(=登録済みRemovedでも overflow でもない=対応表に
			// 入るページ)にだけ除外領域の緑ベタ塗りを重ねる。変更なしで entry が無いページにも出すので
			// 上の if/else とは独立に判定する。Removed/overflow ページは画素比較自体を行わない
			// (ノンブル除外という概念が無い)ので塗らない。
			if (!isThumb && fillExcluded && !KESCMPageMapIsRegistered(db, srcPageUID) && sOverflowS.count(srcPageUID) == 0)
				KESCMDrawPageNumberMarkerFill(gPort, db, srcPageUID);
		}
		return kFalse;	// Source 文書に Target 側オーバーレイは無い=ここで終わり
	}

	// ★2026-07-11: 「登録専用パス」(比較対象でない文書=未 Start 時に登録「/」を描く経路)は撤去した
	//   (ユーザー指定: 未 Start では Add/Remove の「/」をドキュメント・Pages パネルとも出さない)。登録「/」は
	//   Start 中の Target/Source メインループ(下の Target ループ・上の Source ループ)だけが描く。

	// 変更オーバーレイ(リング＋変更数) — マーク済みドキュメントが現スプレッドの db と一致する時だけ。
	// master 表示トグル(sMarksVisible)が OFF の間、またはこのスプレッドを覗き中(旧版べた載せ中)は描かない
	// (データは保持=再表示で即復帰)。覗いていない他のスプレッドのマークは通常どおり残る。
	// ★印刷マーク(sPrintMarks)が ON の間は、ツール左hold に関係なく常に描く(画面=WYSIWYG / 印刷・PDF にも出る)。
	if (peekingThisSpread || !wantMarks || sDB == nil || db != sDB)
		return kFalse;

	// 画面マークの実効不透明度。sMarkScreenOpacity は常に実効値を保持する(下の各ソースが設定):
	//   ・ツール左hold中 = 選択不透明度(パネルの 25%/75%)
	//   ・押していない時 = 基準値 KESCMBaseScreenOpacity()(印刷ONなら選択不透明度 / 印刷OFFは1.0)
	// 離すと基準値へ戻る。printing 経路はここを使わず、KESCMDrawRingForPrint が SelectedMarkOpacity を直接使う。
	const PMReal screenMarkOp = sMarkScreenOpacity;

	// このスプレッドの各ページについて、エントリがあれば描く(描画本体は KESCMDrawEntryOnPage に共通化)。
	const int32 np = spread->GetNumPages();
	const bool16 fillExcluded = KESCMGetIgnorePageNumberMarker();	// ノンブル除外領域の緑ベタ塗り(除外トグルON時)
	for (int32 i = 0; i < np; ++i)
	{
		const UID pageUID = spread->GetNthPageUID(i);
		std::map<UID, KESCMOverlayEntry*>::iterator it = sEntries.find(pageUID);
		if (it != sEntries.end())
		{
			if (isThumb)
				KESCMDrawPageBorder(gPort, db, pageUID, sxr, drawMode, screenMarkOp, kKESCMRingR, kKESCMRingG, kKESCMRingB);
			else
				KESCMDrawEntryOnPage(gPort, it->second, db, pageUID, sxr, drawMode, screenMarkOp);
		}
		else if (KESCMPageMapIsRegistered(db, pageUID))
		{
			// 比較エントリが無い(=対象外)Target ページ。登録済み("Added")なら緑「/」を描く。
			KESCMDrawPageDiagonal(gPort, db, pageUID, sxr, drawMode, screenMarkOp, kKESCMAddedBorderR, kKESCMAddedBorderG, kKESCMAddedBorderB);
		}
		else if (sOverflowT.count(pageUID) > 0)
		{
			// 登録もされていない、ページ数差であふれたページ。未比較であることを赤斜線で明示する。
			KESCMDrawPageDiagonal(gPort, db, pageUID, sxr, drawMode, screenMarkOp, kKESCMRingR, kKESCMRingG, kKESCMRingB);
		}
		// 除外トグルON時、実際に比較しているページ(=登録済みAddedでも overflow でもない=対応表に
		// 入るページ)にだけ除外領域の緑ベタ塗りを重ねる。変更なしで entry が無いページにも出すので
		// 上の if/else とは独立に判定する。Added/overflow ページは画素比較自体を行わない
		// (ノンブル除外という概念が無い)ので塗らない。
		if (!isThumb && fillExcluded && !KESCMPageMapIsRegistered(db, pageUID) && sOverflowT.count(pageUID) == 0)
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
	virtual void GetName(PMString* pName) { pName->SetKey("KESCMDrawEventSrvc\0"); }
	virtual IPlugIn::ThreadingPolicy GetThreadingPolicy() const { return IPlugIn::kMainThreadOnly; }
};

CREATE_PMINTERFACE(KESCMDrawEventSrvc, kKESCMDrawEventSrvcImpl)

