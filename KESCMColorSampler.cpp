//========================================================================================
//
//  KESCMColorSampler.cpp
//
//  クリック点 CMYK サンプリングの実装(旧 KESCMScriptProvider.cpp から分離)。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IDataBase.h"
#include "IControlView.h"
#include "ILayoutUIUtils.h"
#include "IGeometry.h"
#include "IShape.h"
#include "TransformUtils.h"
#include "PMMatrix.h"
#include "PMPoint.h"
#include "PMRect.h"
#include "PMString.h"
#include "SnapshotUtilsEx.h"
#include "AGMImageAccessor.h"

#include "KESCMConstants.h"
#include "KESCMDrawEventHandler.h"   // KESCMDrawEventHandler::sRasterizing
#include "KESCMCore.h"               // KESCMQueryMouseContentPoint / KESCMFindPageUnderMouse
#include "KESCMPageMap.h"            // KESCMMapTargetToSource / KESCMBuildPairing(除外対応表)
#include "KESCMColorSampler.h"

#include <map>

//----------------------------------------------------------------------------------------
// Alt+左ホールド(ドラッグ)中の target→source ページ対応表キャッシュ(KESCMColorSampler.h 参照)。
// 押下中はページ構成が変わらないので、毎サンプル(≦20回/秒)の KESCMBuildPairing 全ページ再構築を
// 1回に減らす(2026-07-15)。押下の外では常に非アクティブ=単発サンプルは従来どおり毎回構築。
//----------------------------------------------------------------------------------------
static bool16              sDragCacheActive   = kFalse;
static IDataBase*          sDragCacheTargetDB = nil;	// キー照合用(deref しない)
static IDataBase*          sDragCacheSourceDB = nil;
static std::map<UID, UID>  sDragCacheT2S;

void KESCMSampleCmykBeginDrag(IDataBase* targetDB, IDataBase* sourceDB)
{
	sDragCacheT2S.clear();
	sDragCacheTargetDB = targetDB;
	sDragCacheSourceDB = sourceDB;
	sDragCacheActive   = kFalse;
	if (targetDB == nil || sourceDB == nil)
		return;

	std::vector<UID> pairT, pairS;
	KESCMBuildPairing(targetDB, sourceDB, pairT, pairS);
	for (size_t k = 0; k < pairT.size(); ++k)
		sDragCacheT2S[pairT[k]] = pairS[k];
	sDragCacheActive = kTrue;
}

void KESCMSampleCmykEndDrag()
{
	sDragCacheActive   = kFalse;
	sDragCacheTargetDB = nil;
	sDragCacheSourceDB = nil;
	sDragCacheT2S.clear();
}

// pageRef のページを、spreadPt(そのページの spread 座標)まわりの極小領域だけ CMYK・高dpi でラスタ化し、
// 中心1画素の C,M,Y,K 生値(0..255)を out[4] に読む。アクセサ/スナップショットは即破棄(保持ゼロで
// 破棄時クラッシュを回避)。成功で kTrue。
static bool16 KESCMReadCmykPixel(const UIDRef& pageRef, const PMPoint& spreadPt, uint8 out[4])
{
	out[0] = out[1] = out[2] = out[3] = 0;
	if (pageRef.GetDataBase() == nil || pageRef.GetUID() == kInvalidUID)
		return kFalse;

	// クリック点まわりの極小矩形(spread 座標)。boundsToSpreadMatrix=identity(=既に spread 座標)。
	const PMReal hp = kKESCMSampleHalfPt;
	PMRect clip(spreadPt.X() - hp, spreadPt.Y() - hp, spreadPt.X() + hp, spreadPt.Y() + hp);

	SnapshotUtilsEx* snap = new SnapshotUtilsEx(clip, PMMatrix(), pageRef, 1.0, 1.0,
		kKESCMSampleDpi, 72.0, 0.0, SnapshotUtilsEx::kCsCMYK, kFalse);
	KESCMDrawEventHandler::sRasterizing = kTrue;	// この Draw 中の再入でマークを描かせない
	// 枠の比較(KESCMDrawEventHandler)と同じプロキシ描画(fullRes=kFalse)。配置画像のフル解像度生成を
	// 誘発しないので文書を dirty にせず、dirty 回避の SaveRestoreModifiedState guard は不要。
	ErrorCode drew = snap->Draw(IShape::kPreviewMode, kFalse /*fullRes*/, 7.0, kFalse /*AA off*/);
	KESCMDrawEventHandler::sRasterizing = kFalse;
	AGMImageAccessor* acc = (drew == kSuccess) ? snap->CreateAGMImageAccessor() : nil;

	bool16 ok = kFalse;
	if (acc != nil)
	{
		Int32Rect b = acc->GetBounds();
		const int32 w = b.right - b.left, h = b.bottom - b.top;
		const int32 rb = (int32)acc->GetRowBytes();
		const int32 bpp = (int32)acc->GetBitsPerPixel() / 8;
		const uint8* base = acc->GetBaseAddr();
		if (base != nil && w > 0 && h > 0 && rb > 0 && bpp >= 4)
		{
			const int32 cx = w / 2, cy = h / 2;	// 中心画素=クリック点
			const uint8* px = base + (size_t)cy * rb + (size_t)cx * bpp;
			out[0] = px[0]; out[1] = px[1]; out[2] = px[2]; out[3] = px[3];	// C,M,Y,K(offset 0)
			ok = kTrue;
		}
		delete acc;
	}
	delete snap;
	return ok;
}

// 値を必ず3桁(ゼロ埋め)で追記する(現在の呼び出しは CMYK% の 0..100)。Target/Source の C/M/Y/K の桁を
// 縦に揃えて見やすくするため(AppendNumber はゼロ埋めしないので桁ごとに分けて出す。範囲外は 0..999 にクランプ)。
static void KESCMAppend3(PMString& s, int32 v)
{
	if (v < 0)   v = 0;
	if (v > 999) v = 999;
	s.AppendNumber(v / 100);
	s.AppendNumber((v / 10) % 10);
	s.AppendNumber(v % 10);
}

// CMYK ラスタの 8bit 値(0..255) を、本来の CMYK 数値である 0..100% に四捨五入で換算する。
// 例: 255→100 / 0→0 / 128→50。(v*100+127)/255 で round。
static int32 KESCMByteToPct(uint8 v)
{
	return ((int32)v * 100 + 127) / 255;
}

// "C000 M000 Y000 K000"(各値3桁ゼロ埋め、0..100%)を追記する。カーソル用(見出し行はカーソル側で
// グラフィック描画として別途足す=値ごとに文字を付けないぶん幅が詰まる。ユーザー提案 2026-07-13)。
static void KESCMAppendCmyk(PMString& s, const uint8 c[4])
{
	KESCMAppend3(s, KESCMByteToPct(c[0]));
	s.Append(" "); KESCMAppend3(s, KESCMByteToPct(c[1]));
	s.Append(" "); KESCMAppend3(s, KESCMByteToPct(c[2]));
	s.Append(" "); KESCMAppend3(s, KESCMByteToPct(c[3]));
}

// "C 000 M 000 Y 000 K 000"(値ごとに見出し文字を直接付ける)を追記する。パネル用。
// ★パネルのステータス行はプロポーショナルフォント(kPaletteWindowFontId、モノスペース選択肢がSDKに無い)
// なので、見出し行と数値行を別々に描いて縦揃えすることは原理的にできない(文字と数字で字幅が違う)。
// カーソル側(KESCMDrawColumns)のような座標揃えの代わりに、各値へ見出し文字を直接添えて「縦の整列」自体を
// 不要にする(ユーザー指定 2026-07-14)。
static void KESCMAppendCmykLabeled(PMString& s, const uint8 c[4])
{
	s.Append("C"); KESCMAppend3(s, KESCMByteToPct(c[0]));
	s.Append(" M"); KESCMAppend3(s, KESCMByteToPct(c[1]));
	s.Append(" Y"); KESCMAppend3(s, KESCMByteToPct(c[2]));
	s.Append(" K"); KESCMAppend3(s, KESCMByteToPct(c[3]));
}

// ツール Alt+左クリック(旧・中ボタン Shift＋Ctrl＋Alt＋ミドル): マウス下ページのクリック点 CMYK 生値を新(target)・旧(source)で
// サンプリングし、"Target C000 …(改行)Source C000 …"(各値3桁ゼロ埋め)を outMsg に組む。成功で kTrue。
//   新→旧ページは平坦通し番号で対応。クリック点を inner(ページ内)座標へ戻し、新/旧それぞれの spread
//   座標へ写してから各ページを極小ラスタ化する(新旧の幾何一致が前提)。
bool16 KESCMSampleCmykUnderMouse(IDataBase* targetDB, IDataBase* sourceDB,
                                 PMString& outPanel, PMString& outCursor)
{
	if (targetDB == nil)
		return kFalse;
	// sourceDB==nil = Stop 中の単独色ピック(比較相手なし)。target 側だけ読み、CMYK を1行(ラベルなし)で返す。
	const bool16 solo = (sourceDB == nil);

	// マウスが乗っているレイアウトビュー(Split Window対応、KESCMQueryViewUnderMouse参照)。
	InterfacePtr<IControlView> view(KESCMQueryViewUnderMouse());
	PMReal mx = 0.0, my = 0.0;
	if (!KESCMQueryMouseContentPoint(view, mx, my))
		return kFalse;

	// マウス下のページを特定(平坦通し番号も取得)。共有ヘルパ KESCMFindPageUnderMouse に集約。
	KESCMPageHit hit;
	if (!KESCMFindPageUnderMouse(targetDB, mx, my, hit))
		return kFalse;

	const UID tPageUID = hit.hitPageUID;

	// クリック点(pasteboard) → ページ内(inner)座標 → target の spread 座標(solo/比較 共通)。
	InterfacePtr<IGeometry> tGeo(targetDB, tPageUID, UseDefaultIID());
	if (tGeo == nil)
		return kFalse;
	PMMatrix mPB = ::InnerToPasteboardMatrix(tGeo);
	if (mPB.IsSingular())
		return kFalse;
	PMPoint inner(mx, my);
	mPB.Inverse().Transform(&inner);
	PMPoint tSpreadPt(inner.X(), inner.Y());
	::InnerToSpreadMatrix(tGeo).Transform(&tSpreadPt);

	uint8 cN[4];
	if (!KESCMReadCmykPixel(UIDRef(targetDB, tPageUID), tSpreadPt, cN))
		return kFalse;

	// ---- 単独モード(Stop): target の CMYK を1行だけ返す。カーソルは KESCMSplitTwoLines が
	//      2行目(空)を自動スキップするので、1行渡すだけでヘッダー「C M Y K」+1行に収まる。 ----
	if (solo)
	{
		outCursor.SetTranslatable(kFalse);
		KESCMAppendCmyk(outCursor, cN);			// "C.. M.. Y.. K.."(ラベルなし=1文書のみ)
		outPanel.SetTranslatable(kFalse);
		KESCMAppendCmykLabeled(outPanel, cN);	// "C .. M .. Y .. K .."(ラベルなし)
		return kTrue;
	}

	// ---- 比較モード(Start): 新→旧ページ対応(除外対応表=登録済みページを除いた順番対応)を解決し
	//      source も読む。ドラッグ中はキャッシュを引く(BeginDrag で構築済み。対応表に無い=登録済み/
	//      あふれページは値なし)。 ----
	UID sPageUID;
	if (sDragCacheActive && targetDB == sDragCacheTargetDB && sourceDB == sDragCacheSourceDB)
	{
		std::map<UID, UID>::const_iterator it = sDragCacheT2S.find(tPageUID);
		if (it == sDragCacheT2S.end())
			return kFalse;
		sPageUID = it->second;
	}
	else if (!KESCMMapTargetToSource(targetDB, sourceDB, tPageUID, sPageUID))
		return kFalse;
	InterfacePtr<IGeometry> sGeo(sourceDB, sPageUID, UseDefaultIID());
	if (sGeo == nil)
		return kFalse;
	PMPoint sSpreadPt(inner.X(), inner.Y());
	::InnerToSpreadMatrix(sGeo).Transform(&sSpreadPt);

	uint8 cO[4];
	if (!KESCMReadCmykPixel(UIDRef(sourceDB, sPageUID), sSpreadPt, cO))
		return kFalse;

	// 各値はラスタ8bit(0..255)を本来の CMYK 数値 0..100% に換算し、3桁ゼロ埋めで桁を揃える。
	// outCursor = 数値2行、ラベルは t/s(ビットマップカーソルは「C M Y K」見出しを別途描くので数値行のみ)。
	outCursor.SetTranslatable(kFalse);
	KESCMAppendCmyk(outCursor, cN); outCursor.Append(" t");
	outCursor.AppendW(UTF32TextChar(0x0A));	// 改行 → 2行目へ
	KESCMAppendCmyk(outCursor, cO); outCursor.Append(" s");

	// outPanel = 値ごとに見出し文字を直接添える(KESCMAppendCmykLabeled)+ t/s。
	// ★見出し行を別途置いて縦揃えする案は撤回済み: パネルのステータス欄はプロポーショナルフォント
	// (kPaletteWindowFontId)で字幅が揃わないため、値ごとにラベルを直接添える(フォント幅に依らず崩れない)。
	outPanel.SetTranslatable(kFalse);
	KESCMAppendCmykLabeled(outPanel, cN); outPanel.Append(" t");
	outPanel.AppendW(UTF32TextChar(0x0A));
	KESCMAppendCmykLabeled(outPanel, cO); outPanel.Append(" s");
	return kTrue;
}
