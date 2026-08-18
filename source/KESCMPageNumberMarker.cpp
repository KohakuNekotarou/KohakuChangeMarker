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
//   ⑤★さらに「実際に描かれる数字」のグリフ実インク範囲を union してフレーム矩形と合成する
//     (2026-07-07。KESCMRealNumberInkInSpread)。フレームから少しはみ出すグリフ(大きなノンブルの
//     ディセンダー/オーバーシュート等)が除外領域の外にこぼれて差分と誤検知されるのを防ぐ。
//     ★マスターの wax はプレースホルダ文字で組まれ実ページの数字と違うため、ベースライン/フォント/
//     座標変換だけをマスター wax から借り、グリフ形状は実ページ番号(GetPageString)から取る。
//     wax/フォント/文字列が取れない場合はフレーム矩形のみ=従来動作にフォールバック。
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
#include "IWaxStrand.h"			// 文字インク境界(はみ出しグリフ対応)用。KESCL と同じ既知良好パターン
#include "IWaxIterator.h"
#include "IWaxLine.h"			// GetToSpreadMatrix / QueryWaxGlyphIterator
#include "IWaxGlyphIterator.h"	// グリフ単位走査(先頭グリフの配置行列=ベースライン)
#include "IWaxGlyphs.h"
#include "IWaxRun.h"				// GetWaxRun → IWaxRenderData
#include "IWaxRenderData.h"		// run→フォント/フォント行列(GetGlyphBBox 用 IFontInstance を作る)
#include "IFontMgr.h"			// QueryFontInstance
#include "IPMFont.h"
#include "IFontInstance.h"		// AppendGlyphIDs / GetGlyphBBox(グリフ輪郭の bbox=真のタイト境界)
#include "K2Vector.h"
#include "TextID.h"				// kFrameListBoss / IID_IWAXSTRAND
#include "K2SmartPtr.h"			// K2::scoped_ptr(IWaxIterator の解放)
#include "TextChar.h"			// kTextChar_PageNumber(ノンブルマーカーの文字コード 0x18)
#include "textiterator.h"
#include "TransformUtils.h"		// InnerToSpreadMatrix
#include "PMMatrix.h"
#include "PMRect.h"
#include "PMPoint.h"
#include "UIDList.h"
#include "Utils.h"

#include "IPageList.h"			// 実ページ番号の文字列(GetPageString)
#include "CTextEnum.h"			// Text::GlyphID

#include <algorithm>			// std::find(ノンブルマーカーの走査)
#include <map>					// 除外矩形キャッシュ((db,page) → 矩形列)
#include <utility>				// std::pair(同キー)

#include "KESCMPageNumberMarker.h"

// 既定=kFalse(通常はノンブルの違いも変更として検出する。無視したい時だけフライアウトでON)。
// セッション内のみ(文書には保存しない)。
static bool16 sIgnorePageNumberMarker = kFalse;

bool16 KESCMGetIgnorePageNumberMarker()
{
	return sIgnorePageNumberMarker;
}

void KESCMSetIgnorePageNumberMarker(bool16 on)
{
	sIgnorePageNumberMarker = on;
	// ★切り替えのたびに除外矩形キャッシュを捨てる(2026-08-06 の監査 E-3)。キャッシュは比較時の値を
	//   持ち続けるので、ノンブルフレームを動かした後に測り直させる手段がユーザー側に要る。トグルの
	//   OFF→ON がその逃げ道になる(再比較でも当然更新される)。
	KESCMInvalidatePageNumberMarkerRects();
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
	// TextIterator is a genuine STL bidirectional iterator (textiterator.h:115-119 declares
	// value_type = UTF32TextChar and iterator_category = std::bidirectional_iterator_tag), so the
	// scan is one std::find instead of a hand-written loop. Official precedent:
	// xmlmarkupinjector/XMLMrkSuiteTextCSB.cpp:447 runs std::find_first_of over the same iterator.
	TextIterator iter(textModel, start);
	TextIterator endIter(textModel, start + span);
	return std::find(iter, endIter, kTextChar_PageNumber) != endIter;
}

// itemUID のテキストフレームに自動ページ番号マーカー(0x18)を含む文字があるか。あれば、その
// テキストモデルと範囲も返す(★インク境界計算用に 2026-07-07 拡張。従来の bool 検出と同一の走査)。
// アイテムの実体は文脈で違う:
//   (a) グラフィックフレーム(スプライン=テキスト枠の親)。★マスターアイテムはこれで来る
//       (AppendMasterPageItems は kSkipChildren 既定で親フレームを返し、テキストを持つ子は返さない)。
//       IGraphicFrameData::QueryMCTextFrame() で子の MC テキストフレームへ降りてテキストモデルを得る。
//   (b) アイテム自身が IMultiColumnTextFrame / ITextFrameColumn を実装している場合(直に取れる)。
// (a)(b) いずれかでテキストモデルが取れれば走査。どれも無ければ kFalse。
static bool16 KESCMQueryMarkerText(IDataBase* db, UID itemUID,
	InterfacePtr<ITextModel>& outModel, TextIndex& outStart, int32& outSpan)
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
			{
				outModel = textModel; outStart = childMcf->TextStart(); outSpan = childMcf->TextSpan();
				return kTrue;
			}
		}
	}
	// (b) アイテム自身が MC テキストフレーム
	InterfacePtr<IMultiColumnTextFrame> mcf(db, itemUID, UseDefaultIID());
	if (mcf != nil)
	{
		InterfacePtr<ITextModel> textModel(mcf->QueryTextModel());
		if (KESCMTextRangeHasPageNumberMarker(textModel, mcf->TextStart(), mcf->TextSpan()))
		{
			outModel = textModel; outStart = mcf->TextStart(); outSpan = mcf->TextSpan();
			return kTrue;
		}
	}
	// (b) アイテム自身がテキストフレームカラム(単一列)
	InterfacePtr<ITextFrameColumn> tfc(db, itemUID, UseDefaultIID());
	if (tfc != nil)
	{
		InterfacePtr<ITextModel> textModel(tfc->QueryTextModel());
		if (KESCMTextRangeHasPageNumberMarker(textModel, tfc->TextStart(), tfc->TextSpan()))
		{
			outModel = textModel; outStart = tfc->TextStart(); outSpan = tfc->TextSpan();
			return kTrue;
		}
	}
	return kFalse;
}

// ノンブルフレームの「実際に描かれる数字」のインク範囲を、そのフレームが属するスプレッド座標で返す。
// ★2026-07-07: マスターのノンブルフレームの wax はプレースホルダ文字(「A」等)で組まれ、実ページに
// 出る本物の数字(「3」等)とはグリフが違う=マスターの wax から実ページのはみ出しは測れない、と実機の
// 診断で判明した。そこで:
//   ①マスター wax からは「ベースライン位置(先頭グリフの配置行列 gm)」「スプレッド変換(line→spread)」
//     「フォント(級数込み IFontInstance)」だけを借りる(これらはどの数字でも同じ=流用可)。
//   ②実際に出る数字の文字列は IPageList::GetPageString(pageUID) で取る(＝画面の表示番号)。
//   ③その各文字のグリフ bbox(IFontInstance::GetGlyphBBox)をベースライン上に置いて union する。
// これで「実ページの本物の数字がどれだけ溢れるか」ぴったりのインク範囲になる(オーバーシュート/
// ディセンダー込み)。横位置は先頭グリフ位置に積むだけ(呼び出し側がフレーム矩形と union するので X は
// フレームで被覆される)。返り値の座標系はフレーム自身のスプレッド(マスター由来は呼び出し側で offset 合成)。
// ★Recompose は意図的にしない(描画イベント中に呼ばれ得るため再入回避)。wax/フォント/文字列のどれかが
// 取れなければ空 rect を返し、呼び出し側はフレーム矩形のみ=従来動作に自然フォールバックする。
static PMRect KESCMRealNumberInkInSpread(ITextModel* masterTextModel, TextIndex start,
	IDataBase* db, UID pageUID)
{
	PMRect result(0, 0, 0, 0);	// 空(呼び出し側は IsEmpty で判定)
	if (masterTextModel == nil || db == nil || pageUID == kInvalidUID)
		return result;

	// ---- ① マスター wax から baseline 配置(gm)・line→spread(lm)・フォント(fi)を借りる ----
	InterfacePtr<IWaxStrand> waxStrand((IWaxStrand*)masterTextModel->QueryStrand(kFrameListBoss, IID_IWAXSTRAND));
	if (waxStrand == nil)
		return result;
	// ★読み取り専用イテレータ: この walk は wax を変えず Apply もしない(ベースライン・フォント・
	// 変換を借りるだけ)。IWaxStrand.h:100-106 が「wax を変えず Apply もしないコード(描画等)向け」と
	// して用意しているのがこちらで、製品コードもこれを使う(spellpanel/PrivateSpellingUtils.cpp:371,579)。
	// ⚠サンプル(SnpEstimateTextDepth.cpp:208)は通常版=2流儀。製品側に揃える(KBSGlyphScanEngine.cpp:312 も同じ)。
	K2::scoped_ptr<const IWaxIterator> waxIter(waxStrand->NewReadOnlyWaxIterator());
	if (waxIter == nil)
		return result;
	const IWaxLine* line = waxIter->GetFirstWaxLine(start);
	if (line == nil)
		return result;	// 未組版/オーバーセット → フレーム矩形のみ
	// ★composer が捨てた line を strand がまだ渡してくることがある。触る前に弾く
	// (製品 PrivateSpellingUtils.cpp:387-389。コメントに bug fix 538392 と明記のある実バグ由来のガード)。
	// ここは下の通り意図的にリコンポーズしないので、公式より踏みやすい立場にいる。
	// ⚠併記されている IsDamaged の方は入れない: あちらは「どうせ描き直される」描画側の判断で、
	//   この用途(ベースライン/フォント/変換を借りるだけ)は damaged でも成立する。弾くとフレーム矩形
	//   だけに縮む=除外領域が痩せて誤検知が増える方向になる。
	if (line->IsDestroyed())
		return result;

	const PMMatrix lineToSpread = line->GetToSpreadMatrix();

	// 先頭グリフの配置行列(pen(0,0)=行頭・ベースライン。gmXsc=1.0 で再スケール無し=実測確認済み)と
	// そのランのフォント(級数込み IFontInstance)を取る。
	InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), UseDefaultIID());
	if (fontMgr == nil)
		return result;
	K2::scoped_ptr<IWaxGlyphIterator> git(line->QueryWaxGlyphIterator(kFalse));
	if (git == nil)
		return result;
	git->Reset();
	IWaxGlyphs* g = git->GetWaxGlyphsContainer();
	if (g == nil)
		return result;
	PMMatrix baselinePlacement;			// 先頭グリフの GetGlyphMatrix(行頭・ベースライン配置)
	PMPoint pen(0, 0);
	git->GetGlyphMatrix(&baselinePlacement, &pen);
	IWaxRun* run = git->GetWaxRun();
	InterfacePtr<IWaxRenderData> rd(run, UseDefaultIID());
	if (rd == nil)
		return result;
	InterfacePtr<IPMFont> font(rd->QueryFont());
	if (font == nil)
		return result;
	const PMMatrix fontMatrix = rd->GetFontMatrix();
	InterfacePtr<IFontInstance> fontInst(fontMgr->QueryFontInstance(font, fontMatrix));
	if (fontInst == nil)
		return result;

	// ---- ② 実ページに表示される番号文字列(＝画面の現在番号)を取る ----
	InterfacePtr<IPageList> pageList(db, db->GetRootUID(), UseDefaultIID());
	if (pageList == nil)
		return result;
	PMString numStr;
	// KESCMDrawEventHandler の「現在番号(cur)」と同じ呼び方(IPageList.h:141-146):
	//   第3 bIncludeSectionName=kFalse … ★番号のみ(2026-08-06 の監査で kTrue から修正。バッジ側
	//     KESCMDrawEventHandler.cpp の「現在番号(cur)」も元から kFalse)。kTrue が返すのは "A:12" 形式で、
	//     これは Pages パネル等の表記であってページに描かれるノンブルの見た目ではない。
	//     ⚠セクションの「ページ番号にプレフィックスを含める」が ON なら実際にも前置きは付くが、
	//       その場合でも区切りの ":" は入らないので kTrue でも一致しない。★この実装は全グリフを
	//       先頭位置に重ねて Y 方向の bbox を union するだけなので、余計な文字が混じって効くのは
	//       縦方向だけ。それでも「実際に描かれる字だけを測る」kFalse の方が確かに近い。
	//   第4 bUseIntegerStyle=kFalse … セクションの番号スタイルそのまま(ローマ数字等も実際の見た目どおり)
	//   第7 bIncludePagesOfHiddenSpread=kFalse … 隠しスプレッドを飛ばして数えた番号
	//     ★★★2026-08-18(不具合再検査 B10 の2周目)に**この行の説明を訂正した**。旧記述は「(=画面に
	//       出ている番号)」だったが、実機で測ると **InDesign はページ番号を2つ持っている**:
	//         ①ページパネル / ページ番号フィールド / DOM page.name / GetPageString(…,kTrue)
	//            …… 隠しスプレッドのページも数える(隠しても元の番号のまま)
	//         ②ページに組版される実ノンブル / GetPageString(…,kFalse)
	//            …… 隠しスプレッドを飛ばす(先頭スプレッドを隠すと2ページ目に "1" が刷られる。撮影で確認)
	//       ⇒ 「画面に出ている番号」は①のほうで、ここが使っているのは②。
	//     ★**ここは kFalse のままで正しい**: この関数は「実際に刷られる数字」のインク範囲を測るので、
	//       実ノンブルと同じ数え方でなければならない。⚠同日に TSV(KESCMChangedPagesTSV.cpp)・
	//       Prev/Next(KESCMChangeNav.cpp)・Story Edits(KESCMStoryJump.cpp)の3つは kTrue へ変えたが、
	//       あちらは「人にページを名指しする」用途＝ページパネルと同じ綴りでなければならない側。
	//       **用途が違うので揃えてはいけない。この非対称は意図的。**
	pageList->GetPageString(pageUID, &numStr, kFalse, kFalse, kDefaultPageType, kTrue, kFalse);
	const int32 nch = numStr.NumUTF16TextChars();
	if (nch <= 0)
		return result;
	const UTF16TextChar* buf = numStr.GrabUTF16Buffer(nil);
	if (buf == nil)
		return result;

	// ---- ③ 各文字のグリフ bbox をベースライン上に置いて union ----
	K2Vector<Text::GlyphID> gids;
	fontInst->AppendGlyphIDs(buf, nch, gids);
	for (int32 i = 0; i < (int32)gids.size(); ++i)
	{
		PMRect bbox = fontInst->GetGlyphBBox(gids[i]);	// サイズ適用済み・ベースライン基準(実測確認済み)
		if (bbox.IsEmpty())
			continue;								// notdef 等
		baselinePlacement.Transform(&bbox);		// glyph 空間 → wax(行頭・ベースライン)
		lineToSpread.Transform(&bbox);			// wax → スプレッド
		result.Union(bbox);
	}
	return result;
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
		InterfacePtr<ITextModel> markerModel;
		TextIndex mStart = 0; int32 mSpan = 0;
		if (!KESCMQueryMarkerText(db, itemUID, markerModel, mStart, mSpan))
			continue;
		InterfacePtr<IGeometry> itemGeo(db, itemUID, UseDefaultIID());
		if (itemGeo == nil)
			continue;
		const PMMatrix itemToSpread = ::InnerToSpreadMatrix(itemGeo);
		PMRect r = KESCMItemRectToPageInner(db, itemUID, itemToSpread, spreadToPage);
		// ★実際に描かれる数字のインク範囲を union: フレームからはみ出すグリフ(大きなノンブルの
		// ディセンダー/オーバーシュート等)も除外領域に含める(はみ出し画素の誤検知を防ぐ)。ローカル
		// アイテムの wax は描画先スプレッドそのものに属するので、そのまま spreadToPage でページ inner へ。
		PMRect ink = KESCMRealNumberInkInSpread(markerModel, mStart, db, pageUID);
		if (!ink.IsEmpty())
		{
			spreadToPage.Transform(&ink);
			r.Union(ink);
		}
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
			InterfacePtr<ITextModel> markerModel;
			TextIndex mStart = 0; int32 mSpan = 0;
			if (!KESCMQueryMarkerText(db, itemUID, markerModel, mStart, mSpan))
				continue;
			InterfacePtr<IGeometry> itemGeo(db, itemUID, UseDefaultIID());
			if (itemGeo == nil)
				continue;
			// マスターアイテム自身の inner→(マスタースプレッド座標)に、AppendMasterPageItems が返す
			// offset(マスタースプレッド座標→描画先スプレッド座標)を合成する(m1.m2 = m1を適用後m2を適用)。
			const PMMatrix itemToMasterSpread = ::InnerToSpreadMatrix(itemGeo);
			const PMMatrix itemToDrawingSpread = itemToMasterSpread * offsets[i];
			PMRect r = KESCMItemRectToPageInner(db, itemUID, itemToDrawingSpread, spreadToPage);
			// ★実際に描かれる数字のインク範囲を union(ローカル側と同じ趣旨)。マスターフレームの wax は
			// プレースホルダ文字で組まれるが、KESCMRealNumberInkInSpread は「ベースライン/フォント/変換」
			// だけをマスター wax から借り、グリフ形状は実ページ番号(GetPageString(pageUID))から取るので、
			// 実際に出る数字のはみ出しにぴったり合う。座標はマスタースプレッド → offsets[i](描画先スプレッド)
			// → spreadToPage でページ inner へ(パス矩形と同じ流れ)。
			PMRect ink = KESCMRealNumberInkInSpread(markerModel, mStart, db, pageUID);
			if (!ink.IsEmpty())
			{
				offsets[i].Transform(&ink);
				spreadToPage.Transform(&ink);
				r.Union(ink);
			}
			r.Left(r.Left() - origin.X());   r.Right(r.Right() - origin.X());
			r.Top(r.Top() - origin.Y());     r.Bottom(r.Bottom() - origin.Y());
			outRects.push_back(r);
		}
	}
}


//========================================================================================
// 除外矩形のキャッシュ(KESCMPageNumberMarker.h で宣言。2026-08-06 の監査 E-3)
//   上の実測はページ1枚につき「全アイテム列挙＋各テキストフレームの全文字走査＋マスターページ
//   アイテム収集＋wax 走査＋グリフ bbox」を回す。これを描画イベントのたびに全ページぶんやっていた
//   (緑ベタ塗りの可視化)ので、結果を (db, ページUID) で覚えて引くだけにする。
//   ★db ポインタは照合専用で deref しない。閉じた db のエントリは Invalidate で捨てる。
//
// ⚠★★**なぜここはロック(KESCMMarkStateLock)を取らないのか**(2026-08-17・不具合再検査 B3 の2周目で
//   明文化)。形は sEntries とまったく同じ ---- **描画イベントの中から引かれる std::map** で、
//   引くだけに見えて**キャッシュミスなら insert する**(＝木を回す)。それでも要らない理由:
//     ★**読み手も書き手もメインスレッドにしか居ない**。呼び手は4つで、
//       ①KESCMDrawPageNumberMarkerFill(描画) …… 呼び出し側が `fillExcluded = !printing && …` で
//         ゲートしている＝**BG(PDF の非同期書き出し)は printing なので、ここへ一度も来ない**
//         (緑ベタ塗りは「どこを比較から外したか」を見せる**画面用の診断表示**で、印刷/PDF には出さない
//          ＝2026-08-06 の監査 E-4 の決定。その決定がそのままスレッドの境界にもなっている)
//       ②MakeEntry(比較) …… 比較はメインスレッドでしか走らない
//       ③★**ブック比較(KESCMBookCompare.cpp の KESCMGetPageNumberMarkerRects 呼び2本)** …… 章の対を
//         1組ずつ開いて比べる経路。これも main。
//         ⚠2026-08-19(不具合再検査 B-U5 3周目)に行番号(:284-285)から関数名へ。**+11 ずれていた**
//           (実体は 273-274)。同じ +11 が KESCMDrawEventHandler.h の KESCMRasterizingGuard の参照にも
//           出ており、**ズレ幅が揃う＝同じ1回の編集が両方を腐らせた**([[verify-claims-in-comments]])。
//         ⚠2026-08-18(不具合再検査 B9)に**足した**: ここは「呼び手は3つ」と書いて**この1本を数え落として
//           いた**。結論(全部 main)は変わらないが、**数え落としたまま「3つとも main」と読むと、
//           4本目が別スレッドから来たときに気づけない**。⇒ 呼び手を数える主張は grep で数え直す。
//         ★あちらは第2引数 refresh=kTrue(強制再取得)で呼ぶ。**章を次々に開いて閉じる経路では
//           それが要る** ---- キーは (IDataBase*, ページUID) で、閉じた章の db アドレスは
//           次の章に再利用され得るから([[uidref-reuse-after-close]])。
//       ④KESCMSetIgnorePageNumberMarker / KESCMHandleDocsClosed / Shutdown(捨てる側) …… いずれも main
//   ⇒ ★**DropAllOrig と同じ形の「読み手が main だけだから守らなくてよい」**(あちらは
//     KESCMDrawEventHandler.h に理由が書いてある。ここには書いていなかったので足した)。
//   ★★**この前提が崩れる変更**＝「緑ベタ塗りを印刷/PDF にも出す」。そのときは
//     KESCMGetPageNumberMarkerRects と KESCMInvalidatePageNumberMarkerRects の両方に
//     KESCMMarkStateLock を入れること(捨てる側だけ守るのは無意味＝B3 §5 で踏んだ形)。
//========================================================================================
typedef std::pair<IDataBase*, UID>              KESCMMarkerRectKey;
typedef std::map<KESCMMarkerRectKey, std::vector<PMRect> > KESCMMarkerRectMap;
static KESCMMarkerRectMap sMarkerRectCache;

const std::vector<PMRect>& KESCMGetPageNumberMarkerRects(const UIDRef& pageRef, bool16 refresh)
{
	// 引数不正のときに返す不変の空(キャッシュには入れない=無効なキーを覚えない)。
	static const std::vector<PMRect> kNoRects;

	IDataBase* const db      = pageRef.GetDataBase();
	const UID        pageUID = pageRef.GetUID();
	if (db == nil || pageUID == kInvalidUID)
		return kNoRects;

	const KESCMMarkerRectKey key(db, pageUID);
	KESCMMarkerRectMap::iterator it = sMarkerRectCache.find(key);
	if (it != sMarkerRectCache.end() && !refresh)
		return it->second;	// 覚えている値をそのまま返す(★矩形ゼロも覚える=最も重い空振りを消せる)

	std::vector<PMRect> rects;
	KESCMAppendPageNumberMarkerRects(pageRef, rects);
	if (it == sMarkerRectCache.end())
		it = sMarkerRectCache.insert(std::make_pair(key, std::vector<PMRect>())).first;
	it->second.swap(rects);	// コピーせず中身を入れ替える(古い値はローカル側と一緒に破棄)
	return it->second;
}

void KESCMInvalidatePageNumberMarkerRects()
{
	sMarkerRectCache.clear();
}
