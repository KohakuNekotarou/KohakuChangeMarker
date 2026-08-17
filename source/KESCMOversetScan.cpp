//========================================================================================
//
//  KESCMOversetScan.cpp
//
//  Find Overset の検出本体(KESCMOversetScan.h 参照)。アクティブ1文書の全ユーザーストーリーを走査し、
//  オーバーセット(あふれ)を含むページの UID を集める。overset の判定は Utils<ITextUtils>::IsOverset、
//  「+」を出しているフレーム(=最後の配置済みパーセルのフレーム)の特定は KBSOversetLocator.cpp の
//  ロジックをここにインライン複製した(KBS プラグインへの依存を持たないため)。フレーム→ページ UID の
//  変換は KESCMFramePageUID(KESCMCore.cpp)に任せる。ページに載らないフレームには kInvalidUID が返る
//  ので、ペーストボードは自然に脱落する。
//  ⚠★**その中身をここで説明しない**——2026-08-09 に KESCMCore.cpp へ移した関数で、どの API を主に
//    しどれをフォールバックにし何を検証するかは**あちらのコメントが正本**。ここには 2026-08-17 まで
//    「GetPageUIDRef を主に GetOwnerPageUID をフォールバックに…kPageBoss を検証して」という**移設前の
//    実装説明**が残っていた(下の :180 で「移した」と書いてある同じファイルの冒頭で)。
//    ⇒ **関数を別ファイルへ移しても、その関数を説明したコメントと include は一緒に移らない。**
//  走査は読み取り目的。ただし聞く前に古い組版だけは最新化する(RecomposeThruLastFrame。あふれは組版の
//  結果なので、組み直さずに聞くと「もう直したあふれ」「まだ出ていないあふれ」を答え得る)。窓なし文書でも
//  dirty にしないよう全体を IDataBase::SaveRestoreModifiedState で囲む。
//
//  ★★「あふれ箇所を列挙する」には**本体のプリフライトという上位ルートが実在する**——ルール boss
//    kOversetTextRuleBoss(PackageAndPreflightID.h:165)、criteria は3つに分かれていて
//    kPreflightRC_OversetTextFrame / **OversetFootnote** / OversetTableCell(:938-940)、
//    入口も Facade::IPreflightFacade がある。**それでも自前で走査している理由は3つで、どれも決定的**
//    (2026-08-16 の API 監査 B6 で照合):
//      (1) **結果に座標が無い**。GetPreflightResults が返すのは「Node ID / エラー名 / **ページ番号の
//          文字列**」の3つ組だけ(IPreflightFacade.h:528-537)。KESCM が要るのは「+」点のペーストボード
//          座標なので、プリフライトに聞いてもこの走査は結局要る。
//      (2) **ユーザーの設定に依存する**。プロファイルで overset ルールが有効でなければ結果が出ず、
//          TurnOnPreflighting はユーザーのプリフライト設定そのものを書き換えてしまう(:120)。
//      (3) **非同期**(background idle loop)。Find Overset は押した瞬間に答えが要る。
//    ⇒ 寄せない。⚠**この検討を次に最初からやり直さないために、ここに理由を残す。**
//
//========================================================================================

#include "VCPlugInHeaders.h"

// オブジェクトモデル / テキスト / テーブル / レイアウト:
#include "IDataBase.h"			// GetRootUID / SaveRestoreModifiedState
#include "IStoryList.h"			// GetUserAccessibleStoryCount / GetNthUserAccessibleStoryUID
#include "ITextModel.h"			// QueryFrameList / GetPrimaryStoryThreadSpan / QueryTextParcelList
#include "IFrameList.h"			// IsOverset の引数 / GetFirstDamagedFrameIndex(組版が古いか)
#include "IFrameListComposer.h"	// RecomposeThruLastFrame(あふれを聞く前に古い組版を最新化する)
#include "ITextParcelList.h"
#include "IParcelList.h"		// GetLastParcelKey / GetPreviousParcelKey / GetParcelFrameUID
#include "ITextUtils.h"			// IsOverset
#include "ITableUtils.h"		// InsideTable / TableToPrimaryTextIndex(セルが押し出された時の anchor 登り)
#include "IGeometry.h"			// フレーム inner→pasteboard 変換(「+」点の算出)
// ⚠ILayoutUtils.h / IHierarchy.h / SpreadID.h は 2026-08-17 に外した。3本とも
//   KESCMFramePageUID が使っていたもので、あの関数は 2026-08-09 に KESCMCore.cpp へ移っている
//   (向こうの include はそのまま生きている)。**関数は移り、include は残っていた。**
#include "TransformUtils.h"		// ::InnerToPasteboardMatrix
#include "PMMatrix.h"
#include "PMPoint.h"			// PBPMPoint
#include "PMRect.h"				// GetParcelBounds の右下角
// テーブルセル単独あふれ(赤丸)の走査用:
#include "ITextStoryThreadDictHier.h"	// NextUID(story 内の全スレッド辞書を階層ごと平坦化して列挙)
#include "ITableModel.h"			// const_iterator / GetGridID / begin/end
#include "ITextStoryThreadDict.h"	// QueryThread(gridID)(kTableModelBoss に載る)
#include "ITextStoryThread.h"		// GetTextStart(セルスレッド先頭 TextIndex)
#include "TableTypes.h"				// GridAddress / GridID(source/public/includes 配下)

// 一般:
#include "ParcelKey.h"			// ParcelKey::IsValid
#include "Utils.h"

// プロジェクト内:
#include "KESCMCore.h"			// KESCMFramePageUID(フレーム→ページ。Story Edits と共有する1本)
#include "KESCMOversetScan.h"


//========================================================================================
// このスレッド(pos が compose する parcel list)の「最後の配置済みパーセル」の outport を求める。
// 末尾から遡り、初めて有効フレーム(GetParcelFrameUID != kInvalidUID)を持つパーセルの角を、パーセル
// →フレーム inner→ペーストボードへ変換して返す(KBSOversetLocator.cpp の LocateInThread と同一算出)。
// 1つも配置済みが無ければ(全パーセルが未配置)kFalse。outFrame=「+」を出しているフレーム、outPb=「+」点。
//
// ★縦組みでも同じ式でよい(2026-08-06 実機で確認)。採るのは**パーセル内在座標**の (Right, Bottom) で、
//   組み方向の違いは GetParcelToFrameMatrix が吸収する。日本語版で縦組みのあふれを Find Overset →
//   Prev/Next で巡回させると、InDesign が実際に「+」を描く位置(縦組みは左下)に着地した。
//   ∴「書字方向で分岐する」必要は無い。⚠この式を「横組み専用」と読んで分岐を足さないこと。
//========================================================================================
static bool16 KESCMLastPlacedOutport(ITextModel* textModel, IDataBase* db, TextIndex pos,
	UID& outFrame, PBPMPoint& outPb)
{
	if (textModel == nil || db == nil)
		return kFalse;
	InterfacePtr<ITextParcelList> tpl(textModel->QueryTextParcelList(pos));
	if (tpl == nil)
		return kFalse;
	InterfacePtr<IParcelList> pl(tpl, UseDefaultIID());
	if (pl == nil)
		return kFalse;

	for (ParcelKey k = pl->GetLastParcelKey(); k.IsValid(); k = pl->GetPreviousParcelKey(k))
	{
		const UID frameUID = pl->GetParcelFrameUID(k);
		if (frameUID == kInvalidUID)
			continue;	// この断片は未配置=あふれ。さらに手前(配置済み)へ遡る

		InterfacePtr<IGeometry> frameGeo(db, frameUID, UseDefaultIID());
		if (frameGeo == nil)
			continue;

		const PMRect  parcelBounds  = pl->GetParcelBounds(k);				// parcel-local
		const PMMatrix toFrame      = pl->GetParcelToFrameMatrix(k);			// parcel → frame inner
		const PMMatrix toPasteboard = ::InnerToPasteboardMatrix(frameGeo);	// frame inner → pasteboard

		PMPoint corner(parcelBounds.Right(), parcelBounds.Bottom());		// パーセル内在座標での outport 角
		toFrame.Transform(&corner);
		toPasteboard.Transform(&corner);

		outFrame = frameUID;
		outPb    = PBPMPoint(corner.X(), corner.Y());
		return kTrue;
	}
	return kFalse;
}


//========================================================================================
// このスレッドは「実際にどこかへ組まれている」か＝フレーム UID を持つパーセルを1つでも持つか。
//
// ★★これが「このセルが溢れた」と「このセルは組まれる機会が無かった」を見分ける。
//   GetIsOverset は**両方に yes と答える**: 自分の文字が下端を越えたセルと、表ごとフレームの外へ
//   押し出されて何ひとつ組まれなかったセル。後者は独立した報告ではない——**フレームの方が報告**であり、
//   公式プリフライトもそう言う(KBS が 2026-08-05 に実測: 押し出された表を含む文書で InDesign は
//   「Text Frame / Overset text」を2件出し、その中の10セルには一言も触れなかったのに、ガードの無い
//   スキャンは12件報告した。全記録 docs/ai-notes/kbs-overset-scan.md §6.5.5)。
//   見分けはパーセルにある: 溢れたセルは先頭の数行がページに出ている=フレーム UID を持つパーセルが
//   ある。押し出されたセルはどのパーセルも kInvalidUID を返す。
//
// ★下の KESCMLastPlacedOutport と同じ walk だが、あちらは「+」を描く位置(幾何)まで作り、こちらは
//   「在ったか」だけを聞く。呼び手が欲しいものが違うので分けてある(KBS が ThreadHasPlacedParcel を
//   KBSOversetLocator と別に持つのと同じ理由)。
// 末尾から遡るのは、あふれたスレッドは末尾側に未配置パーセルが並ぶため=失敗する経路では探している
// 配置済みパーセルが末尾に近く、成功する経路では最初の一歩で答えが出る。
//========================================================================================
static bool16 KESCMThreadHasPlacedParcel(ITextModel* textModel, TextIndex pos)
{
	if (textModel == nil)
		return kFalse;
	InterfacePtr<ITextParcelList> tpl(textModel->QueryTextParcelList(pos));
	if (tpl == nil)
		return kFalse;
	InterfacePtr<IParcelList> pl(tpl, UseDefaultIID());
	if (pl == nil)
		return kFalse;

	for (ParcelKey k = pl->GetLastParcelKey(); k.IsValid(); k = pl->GetPreviousParcelKey(k))
	{
		if (pl->GetParcelFrameUID(k) != kInvalidUID)
			return kTrue;
	}
	return kFalse;
}


//========================================================================================
// あふれているスレッドの「+」の位置を求める。まず pos 自身のスレッドを見て、配置済みが無ければ(=セルが
// 行ごとフレーム外へ押し出された等)テーブルアンカーを親スレッドへ登り、最初に配置済みフレームを持つ祖先
// の「+」を採る(KBSFindOversetLocator 相当)。非進行/深ネストは guard で止める。
//
// ⚠★**登り(下のループ)には KESCM の2つの呼び手からは到達しない**(2026-08-17 に数えた)。**残す**が、
//   「効いている」と読まないこと:
//     ・セル経路 …… 呼ぶ直前に KESCMThreadHasPlacedParcel が真を確かめている＝押し出されたセルは
//                    そこで弾かれるので、最初の KESCMLastPlacedOutport が必ず答える
//     ・プライマリ経路 … pos はプライマリスレッドの末尾なので InsideTable が偽＝初回の break
//   ∴ ここが説明している状況(「配置済みが無い」)を、**呼び手のほうが先に落としている**。
//   ★**移植元の KBS では生きている**——あちらの KBSFindOversetLocator は KBSJump からも呼ばれ、
//     そちらに HasPlacedParcel のガードは無い。**同じコードでも呼び手が違えば生死が変わる**ので、
//     KBS と揃えたまま残す(消すと次に移植を突き合わせたとき差分の意味が読めなくなる)。
//========================================================================================
static bool16 KESCMFindOversetOutport(ITextModel* textModel, IDataBase* db, TextIndex pos,
	UID& outFrame, PBPMPoint& outPb)
{
	if (KESCMLastPlacedOutport(textModel, db, pos, outFrame, outPb))
		return kTrue;

	TextIndex cur = pos;
	for (int32 guard = 0; guard < 32; ++guard)
	{
		if (!Utils<ITableUtils>()->InsideTable(textModel, cur))
			break;
		const TextIndex up = Utils<ITableUtils>()->TableToPrimaryTextIndex(textModel, cur);
		if (up == cur)
			break;	// 進んでいない
		cur = up;
		if (KESCMLastPlacedOutport(textModel, db, cur, outFrame, outPb))
			return kTrue;
	}
	return kFalse;
}


// ★フレーム UID → ページ UID の KESCMFramePageUID は KESCMCore.cpp へ移した(2026-08-09)。
//   Story Edits の一覧が「ストーリーの先頭フレームはどのページか」を同じ問いとして必要としたため。
//   ここに残して向こうへ写すと、同じプラグインの中に同じ処理が2つ並ぶ——KESCM が過去に何度も
//   踏んだ「割れ」の形なので、写さずに1本を共有する。宣言は KESCMCore.h。


//========================================================================================
// pos のスレッドが overset か。テーブルセルは ITextUtils::IsOverset(IFrameList*) の対象外
// (フレームリストを持たない)ため、スレッドの ITextParcelList の正式判定 GetIsOverset を使う。
// ★重要(2026-07-24 修正): 当初は GetParcelFrameUID(GetLastParcelKey())==kInvalidUID で判定していたが
// これは**セルに効かない**。ITextParcelList.h:705-713 が明記するとおり、テーブル等の複雑内容のパーセルは
// 「その TextParcelList 自体が overset にならずに」overset になり得る=セル領域パーセルは配置済み(frameUID
// 有効)なので kInvalidUID にならず取りこぼす。GetIsOverset()(ITextParcelList.h:116)は「最後の CR 以外の
// 内容がパーセルに composed されていなければ overset」という正式判定で、「最後の CR だけ」は overset 扱い
// しない(InDesign の赤丸の実挙動・DOM cell.overflows と一致)。
//========================================================================================
static bool16 KESCMThreadIsOverset(ITextModel* textModel, TextIndex pos)
{
	if (textModel == nil)
		return kFalse;
	InterfacePtr<ITextParcelList> tpl(textModel->QueryTextParcelList(pos));
	if (tpl == nil)
		return kFalse;
	return tpl->GetIsOverset();		// スレッド単位の正式な overset 判定
}


//========================================================================================
// story 内の全テーブルの全セルを走査し、単独あふれ（赤丸。親フレームは非あふれ）のセルが載るページ UID を
// out に足す。セルテキストは親と同一 ITextModel の別スレッド（より大きな TextIndex）＝プライマリの
// IsOverset では拾えないので、各セルの先頭 TextIndex を StoryThreadDict 経路
// （SnpIterTableUseDictHier.cpp:247-262 が公式の手本）で取り、KESCMThreadIsOverset で判定→あふれなら
// KESCMFindOversetOutport+KESCMFramePageUID で位置追加。
//   ★★テーブルへは ITableModelList ではなく**スレッド辞書の階層**から到達する(2026-08-06 ブロック10 監査
//     で寄せた)。どちらでも動くが、SDK 自身がどちらが新しいかを明言している——ITableModelList を使う
//     スニペットが自分を "an older way" と呼び「better technique はこちら」と名指しする
//     (SnpIterTableStories.cpp:68-70, :151-154)。下の walk は SnpIterTableUseDictHier.cpp:147-199。
//     ★これで**入れ子の表**が契約として入る: ITextStoryThreadDictHier::NextUID は階層を平坦化する
//     (ITextStoryThreadDictHier.h:63-66)ので、セルの中に錨を下ろした表もトップレベルの表と同じ列に
//     並ぶ。旧ルートでは「入れ子も返るはず」が実測頼みの前提で、取りこぼしたら再帰を足す宿題だった。
//   ★★平坦なので再帰はしない(するとセルの中の表を二度歩き、入れ子セルを二重に報告する)。
//   取得: story(kTextStoryBoss)の UID から NextUID で辞書を辿り、ITableModel を Query できた辞書＝表
//         (できない辞書＝プライマリストーリー自身。SnpIterTableUseDictHier.cpp:219-225 の見分け方)。
//         各表は const_iterator で**格子要素**を回し、GetGridID→dict->QueryThread→GetTextStart。
//   ⚠★★2026-08-16(API 監査 B6)訂正: 旧「const_iterator で**アンカーセル**を回し」は**誤り**。
//     **イテレータはアンカーを返さない。格子要素を全部返す**(ITableModel.h:391-392「traverse through
//     the **GridAddress locations**」。CellIterator::MoveForward() の実装は SDK に無く本体内なので
//     契約だけが頼りだが、**IsAnchor(:137) が在ること自体が「アドレスはアンカーとは限らない」の証拠**)。
//     ★**1セル1回になるのは下の QueryThread が非アンカーに nil を返すから**——セルのスレッドは
//     **アンカーの GridID にだけ**紐づく(ITextStoryThreadDict.h:78-83)。∴ 結合セルは何マス占めても1回。
//     ⚠★**公式の手本はこの nil 検査を持っていない**(SnpIterTableUseDictHier.cpp:256-262 は QueryThread
//       の返りをそのまま GetTextStart に渡す)。**手本どおりに写すと非アンカーの格子要素で落ちる**ので、
//       ここは意図的に手本と違う。KBS も同じ検査を持ち、その旨を書いている
//       (KBSOversetScanEngine.cpp:269-270「The official walk is these same three lines without the
//       test」)。2026-08-17 に KESCM 側にも書き足した＝**移植で落ちていた但し書き**。
//     ⚠**QueryThread を経由しない走査を足すときは自分で IsAnchor で弾く必要がある**(例＝セルの属性や
//     GetCellType を直接引く)。★KBS は 2026-08-11 に自分で訂正済み(KBSOversetScanEngine.cpp:271-272)で、
//     その訂正がこちらに届いていなかった。結論(1セル1回)は合っていたが**理由が違った**。
//   ⚠★**脚注はこの walk を通らない**——見るのは ITableModel を持つ辞書だけなので、脚注のスレッド辞書は
//     素通りする。**それで取りこぼさない**: 脚注が入りきらないと**フレームリスト自体が overset になる**
//     ので、下の (1) が既に拾っている(KBS が 2026-08-10 に実測＝本文64字の下に脚注4,183字で、最後の
//     フレームが overflows=true)。⚠Adobe のプリフライトは OversetFootnote を独立した criteria にして
//     いる(ファイル冒頭)ので、「脚注を別に数えるべきでは」は将来また出る問い。**出たらこの3行を読む。**
//   ★性能: コストは Σ(rows×cols)。大きな表で重い（Find Overset はオンデマンドなので許容）。
//   ⚠★★2026-08-16(同上)訂正: 旧「安価な『表にあふれセルが在るか』の事前判定 API は SDK に無い
//     （確認済み）」は**誤り**。**在る**——ITextParcelList::GetParcelContainsOversetContent
//     (ITextParcelList.h:705-713)＝「Tables や Footnotes のような複雑内容が overset か」をパーセル
//     1つにつき1コールで答える。**採らないのは実測の結果**(KBS が 2026-08-10 に5文書で突き合わせ):
//     ①表については答えが完全に一致 ②速度は**どちらにも転ぶ**(表の無い500ストーリーで walk 70us 対
//     parcels 1,540us ／ 861 あふれセルの表1つで walk 53us 対 parcels 24us)＝事前判定として安いとは
//     限らない ③唯一の食い違いは**あふれた脚注**(あちらは見える)で、それは上のとおり (1) が拾う。
//========================================================================================
static void KESCMCollectOversetCells(IDataBase* db, const UIDRef& storyRef, ITextModel* textModel,
	std::vector<KESCMOversetLoc>& out)
{
	if (db == nil || textModel == nil)
		return;
	// 辞書の階層は kTextStoryBoss に集約されている(表1つにつき ITextStoryThreadDict 1つ)。
	InterfacePtr<ITextStoryThreadDictHier> dictHier(textModel, UseDefaultIID());
	if (dictHier == nil)
		return;

	// 出発点はストーリー自身の UID(kTextStoryBoss も辞書を1つ持つ＝プライマリストーリースレッド)。
	// それは下の ITableModel Query で落ちる。
	for (UID nextUID = storyRef.GetUID(); nextUID != kInvalidUID; nextUID = dictHier->NextUID(nextUID))
	{
		InterfacePtr<ITextStoryThreadDict> dict(db, nextUID, UseDefaultIID());
		if (dict == nil)
			continue;

		// この辞書は表のものか。kTableModelBoss は辞書と ITableModel を一緒に持ち、kTextStoryBoss は
		// 辞書だけを持つ——プライマリストーリースレッドはこれで見分ける。
		InterfacePtr<ITableModel> tableModel(dict, UseDefaultIID());
		if (tableModel == nil)
			continue;

		for (ITableModel::const_iterator it(tableModel->begin()), end(tableModel->end()); it != end; ++it)
		{
			const GridAddress ga = *it;					// ★格子要素。アンカーとは限らない(上の ★★ 参照)
			const GridID gridID = tableModel->GetGridID(ga);
			InterfacePtr<ITextStoryThread> thread(dict->QueryThread(gridID));	// ref+1
			if (thread == nil)
				continue;
			int32 span = 0;
			const TextIndex cellPos = thread->GetTextStart(&span);	// セルスレッド先頭 TextIndex
			if (span <= 0)
				continue;								// 空セルはあふれない
			if (!KESCMThreadIsOverset(textModel, cellPos))
				continue;
			// ★★このセルは「溢れた」のか「組まれる機会が無かった」のか(上の KESCMThreadHasPlacedParcel)。
			//   表がフレームごと押し出されると中の全セルが GetIsOverset に yes と答えるので、ガードが
			//   無いと 4行×2列の表で 8 個の「あふれ」を報告してしまう。しかも下の
			//   KESCMFindOversetOutport が祖先へ登るため、**位置は 8 個とも親フレームの同じ「+」点**＝
			//   Prev/Next の巡回では同じ場所に 8 回止まる(枝番だけが増える)。フレーム自体のあふれは
			//   上の (1) プライマリスレッド走査が既に拾っているので、失うものは無い。
			//   ★KBS が 2026-08-05 に「本命」として直した穴と同じもの(2026-08-06 にユーザー指示で移植)。
			if (!KESCMThreadHasPlacedParcel(textModel, cellPos))
				continue;
			UID frameUID = kInvalidUID; PBPMPoint pb;
			if (!KESCMFindOversetOutport(textModel, db, cellPos, frameUID, pb))
				continue;
			const UID pageUID = KESCMFramePageUID(db, frameUID);
			if (pageUID != kInvalidUID)
				out.push_back(KESCMOversetLoc(pageUID, pb));
		}
	}
}


//========================================================================================
// KESCMCollectOversetLocations(KESCMOversetScan.h で宣言)
//========================================================================================
void KESCMCollectOversetLocations(IDataBase* db, std::vector<KESCMOversetLoc>& outLocs)
{
	if (db == nil)
		return;

	// 読み取りのみ。走査で lazy recompose 等が起きても文書を dirty にしない(KBS/KESCL と同じ作法)。
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);

	InterfacePtr<IStoryList> storyList(db, db->GetRootUID(), UseDefaultIID());
	if (storyList == nil)
		return;

	const int32 n = storyList->GetUserAccessibleStoryCount();
	for (int32 i = 0; i < n; ++i)
	{
		const UIDRef storyRef = storyList->GetNthUserAccessibleStoryUID(i);
		InterfacePtr<ITextModel> textModel(storyRef, UseDefaultIID());
		if (textModel == nil)
			continue;

		// ★★(0) 聞く前に、古くなった組版を最新化する(2026-08-06 ブロック10 監査で追加)。
		//   あふれは**組版の結果**であって現在の状態ではない——下の2つの判定はどちらも「コンポーザが
		//   最後にそう言った」を読むだけなので、最後に組んでから編集されたストーリーは、もう直した
		//   あふれを報告したり、今出たばかりのあふれを黙っていたりする。
		//   公式ルート= SnpInspectTextModel.cpp:724-733(damaged 添字を見てから RecomposeThruLastFrame)。
		//   ★兄弟の KBS は 2026-08-03 に同じ手当てを入れている(KBSOversetScanEngine.cpp:428-437 /
		//   KBSJump.cpp:161-163 が理由・:175-179 が実装)が、KESCM に届いていなかった。
		//   ⚠2026-08-17 に実測して行番号を直した——旧「:371-384 / :169-174」は**どちらも別の段落**を
		//     指していた(:371-384 は「キャンセルを章の中で聞く」の説明)。
		//   ★下の (2) のセル走査も最新の組版を読む。⚠**ただし理由は「フレームリストを組めばテーブルも
		//     落ち着くから」ではない**(2026-08-17 訂正)——それは [[text-composition-damage-and-recompose]]
		//     が「未確認」と書いている問いで、ここが勝手に断言していた。**本当の理由は、セルの判定に使う
		//     ITextParcelList::GetIsOverset() が聞くだけで組むから**(KBS 実測)。∴ (2) はこの (0) が
		//     無くても最新を読む。(0) が要るのは (1) の IsOverset(frameList) のほう——あちらは frame list
		//     の damage を自分では解消しない。
		//   ⚠この関数の呼び手は KESCMApplyOversetForDoc ただ1つだが、**その Apply の呼び手は4つある**
		//     (Find Overset / Refresh Overset / Start / Stop。全数は KESCMOversetApply.h)。
		//     ★**「描画イベント中には走らない」は、その4つを数えて初めて言える。** 4つとも数えた＝UI の
		//     メニュー2つ／Start は KESCMDoMarkChangesDoc が全ページのラスタ化を終えた後／Stop はマーク
		//     破棄の後。どれも描画イベントの外。**呼び手が増えたら数え直すこと**(2026-08-17 まで
		//     「オンデマンド1経路だから」を根拠にしており、Start と Stop を数えていなかった)。
		//   ⚠★**組めば文書は dirty になる**。冒頭の SaveRestoreModifiedState は「dirty にしない」ので
		//   はなく「**入る前が clean だったときに限り、出るときに clean へ戻す**」ガード
		//   (IDataBase.h:389-412。既に dirty なら fDB=nil にして何もしない=ユーザーの編集を消さない)。
		//   ★この呼び出しで新しく生じる副作用ではない: スレッドに組版のことを聞けば**聞くだけで組む**ので、
		//   下の IsOverset / GetIsOverset だけでも同じことが起きていた——ガードが元からここに在るのは
		//   そのため(KBSOversetScanEngine.cpp:393-396 が同じ理由を書いている。⚠旧「:340-343」は脚注の
		//   測定結果を指していた＝2026-08-17 に実測して訂正)。明示的に組むのは、暗黙に
		//   起きていたことを確実かつ最後まで行うだけで、生成される wax も次の描画で出来るものと同じ。
		InterfacePtr<IFrameList> frameList(textModel->QueryFrameList());
		if (frameList != nil && frameList->GetFirstDamagedFrameIndex() != -1)
		{
			InterfacePtr<IFrameListComposer> composer(frameList, UseDefaultIID());
			if (composer != nil)
				composer->RecomposeThruLastFrame();
		}

		// (1) プライマリスレッドのあふれ(通常フレームの赤「+」)。purpose-built の IsOverset で判定し、
		//     あふれていれば末尾位置から「最後の配置済みフレーム」の outport(「+」点)→ページ。span<=0(空)は対象外。
		//     ★ここで continue しないこと: 親が非あふれでもテーブルのセルは単独であふれ得る((2)で拾う)。
		if (frameList != nil && Utils<ITextUtils>()->IsOverset(frameList))
		{
			const int32 span = textModel->GetPrimaryStoryThreadSpan();
			if (span > 0)
			{
				UID frameUID = kInvalidUID; PBPMPoint pb;
				if (KESCMFindOversetOutport(textModel, db, span - 1, frameUID, pb))
				{
					const UID pageUID = KESCMFramePageUID(db, frameUID);
					if (pageUID != kInvalidUID)
						outLocs.push_back(KESCMOversetLoc(pageUID, pb));	// kInvalidUID はペーストボードのみ=スキップ
				}
			}
		}

		// (2) テーブルのセル単独あふれ(赤丸)。親スレッドの IsOverset では拾えないため、全テーブル・全セルの
		//     スレッド先頭 TextIndex を個別に overset 判定する(親が非あふれのストーリーでも必ず実行する)。
		KESCMCollectOversetCells(db, storyRef, textModel, outLocs);
	}
}

// KESCMOversetScan.cpp 終わり。
