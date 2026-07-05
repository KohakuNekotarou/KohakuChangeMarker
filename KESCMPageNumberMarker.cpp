//========================================================================================
//
//  KESCMPageNumberMarker.cpp
//
//  自動ページ番号(ノンブル)マーカーの検出(KESCMPageNumberMarker.h で宣言)。
//
//  検出手順(pageRef 1ページぶん):
//   ①このページの平坦ローカルアイテム(ISpread::GetItemsOnPage)を調べる。
//   ②このページに適用中のマスタースプレッド(IMasterPage::GetMasterSpreadUID)から、
//     このスプレッドに実際に描画されるマスターアイテム一覧を求める(IMasterSpreadUtils::
//     AppendMasterPageItems。ドキュメント上「このスプレッド/ページに描画されるマスターアイテム」を
//     返す=ページ側で上書き済みのものは含まれない、という理解で実装。★要実機確認)。
//   ③各アイテムについて、IMultiColumnTextFrame経由でテキストモデルを取り、その範囲(TextStart〜
//     TextStart+TextSpan)を1文字ずつ走査して kTextChar_PageNumber(0x18)が含まれるか見る
//     (★ノンブルはこの文字コードとして格納される。実機IDMLで確認済み。詳細は
//     KESCMTextRangeHasPageNumberMarker のコメント)。
//   ④見つかったフレームの矩形(GetPathBoundingBox)を、そのアイテム自身の InnerToSpreadMatrix で
//     スプレッド座標へ、マスター由来アイテムはさらに AppendMasterPageItems が返す offset 行列で
//     描画先スプレッド座標へ変換してから、ページの逆行列でページinner座標へ戻す。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IDataBase.h"
#include "IGeometry.h"
#include "IHierarchy.h"
#include "ISpread.h"
#include "IMasterPage.h"
#include "IMasterSpreadUtils.h"
#include "IMultiColumnTextFrame.h"
#include "ITextFrameColumn.h"
#include "IGraphicFrameData.h"	// グラフィックフレーム(テキスト枠の親)→子MCテキストフレーム取得
#include "ITextModel.h"
#include "TextChar.h"			// kTextChar_PageNumber(ノンブルマーカーの文字コード 0x18)
#include "textiterator.h"
#include "TransformUtils.h"		// InnerToSpreadMatrix
#include "PMMatrix.h"
#include "PMRect.h"
#include "PMPoint.h"
#include "UIDList.h"
#include "Utils.h"

#include "KESCMPageNumberMarker.h"

// 既定=kTrue(ユーザー要望の主目的機能なので最初からON)。セッション内のみ(文書には保存しない)。
static bool16 sIgnorePageNumberMarker = kTrue;

bool16 KESCMGetIgnorePageNumberMarker()
{
	return sIgnorePageNumberMarker;
}

void KESCMSetIgnorePageNumberMarker(bool16 on)
{
	sIgnorePageNumberMarker = on;
}

// [start, start+span) の範囲に自動ページ番号(ノンブル)マーカーがあるか。
// ★実体は文字コード kTextChar_PageNumber(0x18)。実機 IDML で確認済み(Content が <?ACE 18?>=0x18)。
//   最初に探していた kTextChar_CurrentPageNumber(0xE025)は Find/Change 専用の表現で、実文書の
//   文字ストリームには格納されない(TextChar.h:496「used only by Find/Change, and are not stored in a
//   document」)。0x18 は制御コードで通常テキストには現れず、current/next/previous のページ番号は
//   すべてこの 0x18 として格納される(種別は文字属性で区別)ので、0x18 の有無だけでノンブルを検出できる。
static bool16 KESCMTextRangeHasPageNumberMarker(ITextModel* textModel, TextIndex start, int32 span)
{
	if (textModel == nil || span <= 0)
		return kFalse;
	TextIterator iter(textModel, start);
	TextIterator endIter(textModel, start + span);
	for (; iter != endIter; ++iter)
	{
		if (*iter == kTextChar_PageNumber)
			return kTrue;
	}
	return kFalse;
}

// itemUID のテキストフレームに自動ページ番号マーカー(0x18)を含む文字があるか。
// アイテムの実体は文脈で違う:
//   (a) グラフィックフレーム(スプライン=テキスト枠の親)。★マスターアイテムはこれで来る
//       (AppendMasterPageItems は kSkipChildren 既定で親フレームを返し、テキストを持つ子は返さない)。
//       IGraphicFrameData::QueryMCTextFrame() で子の MC テキストフレームへ降りてテキストモデルを得る。
//   (b) アイテム自身が IMultiColumnTextFrame / ITextFrameColumn を実装している場合(直に取れる)。
// (a)(b) いずれかでテキストモデルが取れれば走査。どれも無ければ kFalse。
static bool16 KESCMItemHasPageNumberMarker(IDataBase* db, UID itemUID)
{
	// (a) グラフィックフレーム → 子 MC テキストフレーム
	InterfacePtr<IGraphicFrameData> gfd(db, itemUID, UseDefaultIID());
	if (gfd != nil)
	{
		InterfacePtr<IMultiColumnTextFrame> childMcf(gfd->QueryMCTextFrame());
		if (childMcf != nil)
		{
			InterfacePtr<ITextModel> textModel(childMcf->QueryTextModel());
			if (KESCMTextRangeHasPageNumberMarker(textModel, childMcf->TextStart(), childMcf->TextSpan()))
				return kTrue;
		}
	}
	// (b) アイテム自身が MC テキストフレーム
	InterfacePtr<IMultiColumnTextFrame> mcf(db, itemUID, UseDefaultIID());
	if (mcf != nil)
	{
		InterfacePtr<ITextModel> textModel(mcf->QueryTextModel());
		if (KESCMTextRangeHasPageNumberMarker(textModel, mcf->TextStart(), mcf->TextSpan()))
			return kTrue;
	}
	// (b) アイテム自身がテキストフレームカラム(単一列)
	InterfacePtr<ITextFrameColumn> tfc(db, itemUID, UseDefaultIID());
	if (tfc != nil)
	{
		InterfacePtr<ITextModel> textModel(tfc->QueryTextModel());
		if (KESCMTextRangeHasPageNumberMarker(textModel, tfc->TextStart(), tfc->TextSpan()))
			return kTrue;
	}
	return kFalse;
}

// itemUID(テキストフレーム、ページinner座標系とは限らない自身の inner 座標系)の矩形を、
// itemToSpread(そのアイテム自身の InnerToSpreadMatrix、マスターアイテムなら masterOffset も
// 掛け合わせたもの)経由でスプレッド座標へ、さらに spreadToPage でページinner座標へ変換して返す。
static PMRect KESCMItemRectToPageInner(IDataBase* db, UID itemUID,
	const PMMatrix& itemToSpread, const PMMatrix& spreadToPage)
{
	InterfacePtr<IGeometry> itemGeo(db, itemUID, UseDefaultIID());
	PMRect r;
	if (itemGeo == nil)
		return r;
	r = itemGeo->GetPathBoundingBox();
	itemToSpread.Transform(&r);
	spreadToPage.Transform(&r);
	return r;
}

//========================================================================================
// KESCMAppendPageNumberMarkerRects(KESCMPageNumberMarker.h で宣言)
//   pageRef のノンブルフレーム矩形を、ページの左上を原点とする pt 座標で集める。
//========================================================================================
void KESCMAppendPageNumberMarkerRects(const UIDRef& pageRef, std::vector<PMRect>& outRects)
{
	IDataBase* db = pageRef.GetDataBase();
	const UID pageUID = pageRef.GetUID();
	if (db == nil || pageUID == kInvalidUID)
		return;

	InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
	InterfacePtr<IHierarchy> pageHier(db, pageUID, UseDefaultIID());
	if (pageGeo == nil || pageHier == nil)
		return;

	const UID spreadUID = pageHier->GetSpreadUID();
	InterfacePtr<ISpread> spread(db, spreadUID, UseDefaultIID());
	if (spread == nil)
		return;

	// このページのスプレッド内インデックス(GetItemsOnPage/AppendMasterPageItemsが要求する)。
	int32 pgPos = -1;
	{
		const int32 np = spread->GetNumPages();
		for (int32 p = 0; p < np; ++p)
			if (spread->GetNthPageUID(p) == pageUID) { pgPos = p; break; }
	}
	if (pgPos < 0)
		return;

	const PMMatrix pageToSpread = ::InnerToSpreadMatrix(pageGeo);
	const PMMatrix spreadToPage = pageToSpread.Inverse();
	// ページの左上(page-inner bboxのLeft/Top)を原点に取り直すためのオフセット。
	const PMRect pageBoundsInPage = pageGeo->GetPathBoundingBox();
	const PMPoint origin(pageBoundsInPage.Left(), pageBoundsInPage.Top());

	// ---- ①ローカルアイテム(このページに実際に所属するアイテム) ----
	UIDList localItems(db);
	spread->GetItemsOnPage(pgPos, &localItems, kFalse /*ページ図形自体は除く*/, kFalse, kTrue);
	const int32 nLocal = localItems.Length();
	for (int32 i = 0; i < nLocal; ++i)
	{
		const UID itemUID = localItems[i];
		if (!KESCMItemHasPageNumberMarker(db, itemUID))
			continue;
		InterfacePtr<IGeometry> itemGeo(db, itemUID, UseDefaultIID());
		if (itemGeo == nil)
			continue;
		const PMMatrix itemToSpread = ::InnerToSpreadMatrix(itemGeo);
		PMRect r = KESCMItemRectToPageInner(db, itemUID, itemToSpread, spreadToPage);
		r.Left(r.Left() - origin.X());   r.Right(r.Right() - origin.X());
		r.Top(r.Top() - origin.Y());     r.Bottom(r.Bottom() - origin.Y());
		outRects.push_back(r);
	}

	// ---- ②マスター由来(未上書き)アイテム ----
	InterfacePtr<IMasterPage> masterPage(db, pageUID, UseDefaultIID());
	if (masterPage != nil && masterPage->IsValid())
	{
		PMRect pageBoundsInSpread = pageBoundsInPage;
		pageToSpread.Transform(&pageBoundsInSpread);

		UIDList onThesePages(db);
		onThesePages.Append(pageUID);
		PMRectCollection pageBoundsList;
		pageBoundsList.push_back(pageBoundsInSpread);

		UIDList masterItems(db);
		UIDList itemPages(db);
		PMMatrixCollection offsets;
		Utils<IMasterSpreadUtils>()->AppendMasterPageItems(db, spreadUID, onThesePages, pageBoundsList,
			masterItems, itemPages, offsets);

		const int32 nMaster = masterItems.Length();
		for (int32 i = 0; i < nMaster; ++i)
		{
			const UID itemUID = masterItems[i];
			if (!KESCMItemHasPageNumberMarker(db, itemUID))
				continue;
			InterfacePtr<IGeometry> itemGeo(db, itemUID, UseDefaultIID());
			if (itemGeo == nil)
				continue;
			// マスターアイテム自身の inner→(マスタースプレッド座標)に、AppendMasterPageItems が返す
			// offset(マスタースプレッド座標→描画先スプレッド座標)を合成する(m1.m2 = m1を適用後m2を適用)。
			const PMMatrix itemToMasterSpread = ::InnerToSpreadMatrix(itemGeo);
			const PMMatrix itemToDrawingSpread = itemToMasterSpread * offsets[i];
			PMRect r = KESCMItemRectToPageInner(db, itemUID, itemToDrawingSpread, spreadToPage);
			r.Left(r.Left() - origin.X());   r.Right(r.Right() - origin.X());
			r.Top(r.Top() - origin.Y());     r.Bottom(r.Bottom() - origin.Y());
			outRects.push_back(r);
		}
	}
}
