//========================================================================================
//
//  KESCMCore.cpp
//
//  ChangeMarker の共有操作(KESCMCore.h で宣言)。KESCMScriptProvider.cpp から分離したもの。
//  スクリプトメソッドとパネルのウィジェットオブザーバが完全に同じ挙動を駆動できるよう、ただの関数に
//  してある。描画エンジン(KESCMDrawEventHandler)・peek モジュールへ委譲する。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "PersistUtils.h"
#include "IDataBase.h"
#include "IDocument.h"
#include "ILayoutUtils.h"
#include "IControlView.h"
#include "IEventUtils.h"
#include "IGeometry.h"
#include "ISpread.h"
#include "ISpreadList.h"
#include "IBoolData.h"				// スプレッドの隠し状態(IID_IHIDESPREADBOOLDATA)の読み取り
#include "SpreadID.h"				// IID_IHIDESPREADBOOLDATA(kSpreadBoss 上の IBoolData。docs の boss 一覧で裏取り済み)
#include "PMString.h"
#include "PMMatrix.h"
#include "PMPoint.h"
#include "PMRect.h"
#include "TransformUtils.h"
#include "IWindow.h"
#include "IWindowUtils.h"
#include "IDocumentPresentation.h"
#include "IPanelControlData.h"
#include "LayoutUIID.h"				// kLayoutWidgetBoss / kLayoutSecondaryPanelWidgetID

#include <vector>

#include "KESCMConstants.h"
#include "KESCMDrawEventHandler.h"   // 描画エンジン＋共有 static
#include "KESCMPeek.h"               // KESCMBaseScreenOpacity
#include "KESCMPageMap.h"            // KESCMBuildPairing(除外対応表)
#include "KESCMCore.h"

//========================================================================================
// ヘルパ: ドキュメント内の全ページUIDを、スプレッド順・ページ順で平坦に集める。
//========================================================================================
void KESCMCollectPageUIDs(IDataBase* db, std::vector<UID>& out)
{
	if (db == nil)
		return;
	InterfacePtr<ISpreadList> spreadList(db, db->GetRootUID(), UseDefaultIID());
	if (spreadList == nil)
		return;
	const int32 ns = spreadList->GetSpreadCount();
	for (int32 s = 0; s < ns; ++s)
	{
		const UID spreadUID = spreadList->GetNthSpreadUID(s);
		InterfacePtr<ISpread> spread(db, spreadUID, UseDefaultIID());
		if (spread == nil)
			continue;
		const int32 np = spread->GetNumPages();
		for (int32 p = 0; p < np; ++p)
			out.push_back(spread->GetNthPageUID(p));
	}
}

//========================================================================================
// マウス位置・ヒットテストの共有ヘルパ(peek と色サンプラが同じ流儀でカーソル位置を求める)。
//========================================================================================
bool16 KESCMQueryMouseContentPoint(IControlView* view, PMReal& outX, PMReal& outY)
{
	outX = 0.0; outY = 0.0;
	if (view == nil)
		return kFalse;
	// マウス: 画面 → 窓 → コンテンツ(ペーストボード)座標。
	GSysPoint gm = Utils<IEventUtils>()->GetGlobalMouseLocation();
	PMPoint pt((PMReal)gm.x, (PMReal)gm.y);
	pt = view->GlobalToWindow(pt);
	view->WindowToContentTransform(&pt);
	outX = pt.X();
	outY = pt.Y();
	return kTrue;
}

// マウス下のレイアウトビューを求める(Split Window対応)。KESCMCore.h のコメント参照。
IControlView* KESCMQueryViewUnderMouse()
{
	GSysPoint globalPt = Utils<IEventUtils>()->GetGlobalMouseLocation();

	InterfacePtr<IWindow> hitWindow(Utils<IWindowUtils>()->QueryWindowUnderPoint(globalPt, kFalse));
	if (hitWindow == nil)
		return nil;

	InterfacePtr<IDocumentPresentation> hitPres(hitWindow, UseDefaultIID());
	if (hitPres == nil)
		return nil;

	InterfacePtr<IPanelControlData> hitPanelData(hitPres, UseDefaultIID());
	if (hitPanelData == nil)
		return nil;

	IControlView* primaryView = hitPanelData->FindWidget(kLayoutWidgetBoss);
	if (primaryView == nil)
		return nil;

	// primaryView は「グローバル→ウィンドウ座標への変換」にだけ使う(どの子ウィジェット経由でも同じ
	// ウィンドウ座標系になるため)。実際にマウス下にあるビューは FindWidget(windowPt) のヒットテストで
	// 特定する(キャンバス以外=ルーラ等に当たった場合は primaryView にフォールバック)。
	IControlView* hitView = primaryView;
	const PMPoint globalPM((PMReal)globalPt.x, (PMReal)globalPt.y);
	const PMPoint winPM = primaryView->GlobalToWindow(globalPM);
	SysPoint winPt;
	winPt.x = ::ToInt32(winPM.X());
	winPt.y = ::ToInt32(winPM.Y());

	IControlView* pointHit = hitPanelData->FindWidget(winPt);
	if (pointHit != nil &&
	    (pointHit->GetWidgetID() == kLayoutWidgetBoss || pointHit->GetWidgetID() == kLayoutSecondaryPanelWidgetID))
		hitView = pointHit;

	hitView->AddRef();	// QueryFrontView() と同じ「+1 ref、呼び出し側で Release」の契約に合わせる
	return hitView;
}

bool16 KESCMFindPageUnderMouse(IDataBase* targetDB, PMReal mx, PMReal my, KESCMPageHit& out)
{
	out.spreadIndex = -1; out.spreadUID = kInvalidUID; out.numPages = 0;
	out.globalPageBase = 0; out.hitPageIndex = -1; out.hitPageUID = kInvalidUID;
	if (targetDB == nil)
		return kFalse;
	InterfacePtr<ISpreadList> spreadList(targetDB, targetDB->GetRootUID(), UseDefaultIID());
	if (spreadList == nil)
		return kFalse;
	const int32 ns = spreadList->GetSpreadCount();
	int32 globalIndex = 0;
	for (int32 s = 0; s < ns; ++s)
	{
		const UID spreadUID = spreadList->GetNthSpreadUID(s);
		InterfacePtr<ISpread> spread(targetDB, spreadUID, UseDefaultIID());
		if (spread == nil)
			continue;
		const int32 np = spread->GetNumPages();

		// ★隠しスプレッド(Hide Unchanged Spreads / ページパネルの Hide Spread)は当たり判定から除外する。
		//   隠すと表示中スプレッドが再配置されて座標が動くのに、隠れたスプレッドの旧座標が同じ場所に
		//   残ってマウスに先にヒットし、peek/再比較/色サンプラの新旧対応(平坦ページ番号)がずれるため。
		//   ページ数の加算(下の globalIndex += np)は続ける=平坦番号は「隠していない時と同じ元の番号」を
		//   維持し、旧ドキュメントの平坦ページ列との対応が崩れない。
		//   隠し状態は kSpreadBoss 上の IBoolData(IID_IHIDESPREADBOOLDATA、kTrue=隠し中)で読む。
		InterfacePtr<IBoolData> hideFlag(targetDB, spreadUID, IID_IHIDESPREADBOOLDATA);
		if (hideFlag != nil && hideFlag->GetBool())
		{
			globalIndex += np;
			continue;
		}

		// マウスがこのスプレッドのいずれかのページ上にあるか?(最初に当たったページを採用)
		for (int32 p = 0; p < np; ++p)
		{
			const UID pageUID = spread->GetNthPageUID(p);
			InterfacePtr<IGeometry> geo(targetDB, pageUID, UseDefaultIID());
			if (geo == nil)
				continue;
			PMRect bb = geo->GetPathBoundingBox();
			PMMatrix m = ::InnerToPasteboardMatrix(geo);
			m.Transform(&bb);
			PMReal L = bb.Left(), R = bb.Right(), T = bb.Top(), B = bb.Bottom();
			if (L > R) { PMReal t = L; L = R; R = t; }
			if (T > B) { PMReal t = T; T = B; B = t; }
			if (mx >= L && mx <= R && my >= T && my <= B)
			{
				out.spreadIndex    = s;
				out.spreadUID      = spreadUID;
				out.numPages       = np;
				out.globalPageBase = globalIndex;
				out.hitPageIndex   = p;
				out.hitPageUID     = pageUID;
				return kTrue;
			}
		}
		globalIndex += np;
	}
	return kFalse;
}

//========================================================================================
// 共有コア操作(KESCMCore.h で宣言)。
//
// 以前はスクリプトメソッド内にインラインで書かれていた本体。今はパネルのウィジェットオブザーバ
// (KESCMPanelObserver.cpp)が完全に同じ挙動を駆動できるよう、ただの(非 static)関数にしてある。
// この翻訳単位に置くのは意図的で、描画エンジン(KESCMDrawEventHandler)と file-local な peek 状態
// (sPeek*)へ直接アクセスできるようにするため。
//========================================================================================

ErrorCode KESCMDoMarkChangesDoc(IDataBase* targetDB, IDataBase* sourceDB, PMString& outReport, bool16 allowIncremental)
{
	if (targetDB == nil || sourceDB == nil)
		return kFailure;

	// 差分再比較の可否。登録トグル専用(allowIncremental=kTrue)で、かつ前回比較と同じドキュメント対を
	// 対象にしていて前回ペアリングが残っている場合のみ差分にする。それ以外(Start・Ignore Page Number
	// マーカー切替・別文書対・前回ペアリング無し)は従来どおり全ページを再ラスタ化する。
	const bool16 doIncremental =
		allowIncremental &&
		KESCMDrawEventHandler::sDB == targetDB &&
		KESCMDrawEventHandler::sSrcDB == sourceDB &&
		!KESCMDrawEventHandler::sPrevPairTargetToSource.empty();

	// 再比較すると「どのスプレッドが変更なしか」の分類が古くなるため、「Hide Unchanged Spreads」で
	// 隠していたスプレッドは先に再表示してトグルを OFF に戻す(何も隠していなければ何もしない)。
	KESCMResetHideUnchanged(kTrue);

	// 両ドキュメントのページ対応を除外対応表(登録済み=比較相手なしページを除いた順番対応)で求める。
	// 差分・全再比較のどちらでも使い、末尾で次回差分用の前回ペアリング(sPrevPairTargetToSource)に記録する。
	std::vector<UID> tPages, sPages;
	KESCMBuildPairing(targetDB, sourceDB, tPages, sPages);
	const size_t n = tPages.size();	// KESCMBuildPairing は既に短い方へ切り詰め済み(tPages/sPagesは同じ長さ)

	// 今回ペアリングの map 化(差分の O(1) 逆引き＋末尾の記録に使う)。
	std::map<UID, UID> newMap;
	for (size_t i = 0; i < n; ++i)
		newMap[tPages[i]] = sPages[i];

	// 比較は同期実行でページをラスタ化するため時間がかかる。ループ前に「Comparing changes...」を
	// パネルステータスへ出し、ForceRedraw で即時に描いてからループに入る(ブロック中も見えるようにする)。
	// 差分の場合はラスタ化枚数が少なく一瞬で終わるが、出しておいても害はない。
	{
		PMString busyMsg("Comparing changes...");
		busyMsg.SetTranslatable(kFalse);
		KESCMSetStatus(busyMsg, kTrue /*forceRedrawNow*/);
	}

	int32 changedCount = 0;
	if (doIncremental)
	{
		// 【差分再比較】前回ペアリング(oldMap)と今回(newMap)を突き合わせる。ペア不変のページは
		// MakeEntry を呼ばず前回のオーバーレイ(または「変化ゼロ=エントリ無し」)をそのまま再利用する。
		const std::map<UID, UID>& oldMap = KESCMDrawEventHandler::sPrevPairTargetToSource;

		// (1) 破棄: 前回ペアの target のうち、今回ペアが消えた/相手が変わったものはエントリを捨てる。
		//     MakeEntry は変化ゼロだと既存エントリを消さないので、相手が変わるページは先にここで消す。
		for (std::map<UID, UID>::const_iterator it = oldMap.begin(); it != oldMap.end(); ++it)
		{
			std::map<UID, UID>::const_iterator nit = newMap.find(it->first);
			if (nit == newMap.end() || nit->second != it->second)
				KESCMDrawEventHandler::DropOneEntry(it->first, it->second);
		}

		// (2) 再計算: 今回ペアの target のうち、前回ペアが無かった/相手が変わったものだけ MakeEntry。
		//     ペア不変ページは触らない(=前回結果を再利用=ラスタ化しない=ここが高速化の核)。
		for (size_t i = 0; i < n; ++i)
		{
			std::map<UID, UID>::const_iterator oit = oldMap.find(tPages[i]);
			if (oit == oldMap.end() || oit->second != sPages[i])
			{
				bool16 changed = kFalse;
				KESCMDrawEventHandler::MakeEntry(UIDRef(targetDB, tPages[i]), UIDRef(sourceDB, sPages[i]), changed);
			}
		}
		changedCount = (int32)KESCMDrawEventHandler::sEntries.size();	// 再利用分も含めた現在の変化ページ総数
	}
	else
	{
		// 【全再比較】ドキュメント単位の総入れ替え(Start・Ignore Page Number 切替・フォールバック)。
		KESCMDrawEventHandler::DropAll();
		KESCMDrawEventHandler::sDB = targetDB;
		for (size_t i = 0; i < n; ++i)
		{
			bool16 changed = kFalse;
			KESCMDrawEventHandler::MakeEntry(UIDRef(targetDB, tPages[i]), UIDRef(sourceDB, sPages[i]), changed);
			if (changed) ++changedCount;
		}
	}

	// 今回のペアリングを次回の差分用に記録する(差分・全再比較のどちらの経路でも)。
	KESCMDrawEventHandler::sPrevPairTargetToSource.swap(newMap);

	// 「Show Marks on Source」は Start のたびに既定 ON(仕様)。OFF にしたければフライアウトで外す。
	// sSrcDB/対応表は MakeEntry が変化ページ登録時に埋めるが、変化ゼロでも db だけは明示しておく
	// (エントリが無ければ wantSrcMarks が空判定で落ちるので描画コストは増えない)。
	KESCMDrawEventHandler::sSrcMarksOn = kTrue;
	KESCMDrawEventHandler::sSrcDB = sourceDB;

	KESCMInvalidateDB(targetDB);
	if (sourceDB != targetDB)
		KESCMInvalidateDB(sourceDB);	// Source 側の常時枠を即反映

	// ★ページパネルのサムネイルは「文書の変更」でしか無効化されないキャッシュ(サイズ別に別キャッシュを
	// 持つ)を内部で持っており、公開APIでは既に描画済み・表示中のサムネイルを更新する手段が無いことを
	// 2026-07-05 に確認済み(IPagesSubPanelController::InvalidatePageWidget/InvalidateSpreadWidget、
	// UpdatePagesPanel の bForcePurge、IControlView::ForceRedraw を全て試したが効果なし。ドキュメントを
	// 一時的に編集してUndoで戻す手も、Redo履歴を汚すため見送り)。既知の制限として受け入れる
	// (メインのレイアウト表示への枠描画はここまでの処理で完結しており正常に動作する)。

	PMString report;
	report.SetTranslatable(kFalse);
	report.Append("marks start");
	report.AppendW(UTF32TextChar(0x0A));	// 改行 → 2行目へ
	report.Append("pages compared="); report.AppendNumber((int32)n);
	report.Append(" changed="); report.AppendNumber(changedCount);
	outReport = report;
	return kSuccess;
}

// db が非nilなら、その IDocument のビューを再描画する。呼び出し側(パネル操作時の「今アクティブな
// 文書」)と「実際にマークが描かれている対象文書」が異なる(例: Source や無関係な第3文書が前面の
// 状態で Stop や印刷マーク切替を行った)場合でも、両方を確実に再描画するために使う共有ヘルパ。
void KESCMInvalidateDB(IDataBase* db)
{
	if (db == nil)
		return;
	InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
	if (doc != nil)
		Utils<ILayoutUtils>()->InvalidateViews(doc);
}

void KESCMDoClearMarks(IDataBase* db)
{
	// マーク(=「変更なし」判定の根拠)が消えるので、「Hide Unchanged Spreads」で隠していた
	// スプレッドも再表示してトグルを OFF に戻す(何も隠していなければ何もしない)。
	KESCMResetHideUnchanged(kTrue);

	// DropAll() で sDB が nil になる前に、実際にマークが描かれていた文書を控えておく。呼び出し側の
	// db(=操作時のアクティブ文書)が前面で Source や無関係な第3文書に切り替わっていても、対象文書の
	// 枠が即座に消えるようにするため(タイル表示等で対象文書が同時に見えている場合に効く)。
	// Source 側の常時枠(Show Marks on Source)も同様に、消える前の db を控えて後で再描画する。
	IDataBase* markedDB = KESCMDrawEventHandler::sDB;
	IDataBase* srcDB    = KESCMDrawEventHandler::sSrcDB;

	// ★登録(Added/Removedページ)もここでクリアする。Target/Sourceの組み合わせを変えて再Start
	// した時に、古い登録が新しい比較へ紛れ込むのを防ぐ(2026-07-05, ユーザー判断)。
	KESCMPageMapClearAll(markedDB);
	KESCMPageMapClearAll(srcDB);

	KESCMDrawEventHandler::DropAll();
	KESCMDrawEventHandler::DropAllOrig();	// 旧版べた載せのキャッシュも解放(メモリ開放)

	KESCMInvalidateDB(markedDB);
	if (db != markedDB)
		KESCMInvalidateDB(db);
	if (srcDB != markedDB && srcDB != db)
		KESCMInvalidateDB(srcDB);			// Source 側の常時枠も即座に消す
}

void KESCMDoSetPrintMarks(bool16 printFlag, bool16 opacity25Flag, IDataBase* db)
{
	KESCMDrawEventHandler::sPrintMarks = printFlag;
	KESCMDrawEventHandler::sMarkOpacity25 = opacity25Flag;
	// 常時表示(画面)の不透明度を印刷設定に合わせて即反映。
	KESCMDrawEventHandler::sMarkScreenOpacity = KESCMBaseScreenOpacity();

	// 実際にマークが描かれている対象文書(sDB)を優先して再描画する。呼び出し側 db(=アクティブ文書)が
	// それと異なっていても(Source や無関係な第3文書が前面の状態で操作した場合)、対象文書の見た目が
	// 即座に更新されるようにするため。Start 前(sDB==nil)は従来どおり db のみ再描画する。
	// Source 側の常時枠(Show Marks on Source)は 25%/75% 選択に連動するので、Source も再描画する。
	KESCMInvalidateDB(KESCMDrawEventHandler::sDB);
	if (db != KESCMDrawEventHandler::sDB)
		KESCMInvalidateDB(db);
	if (KESCMDrawEventHandler::sSrcDB != KESCMDrawEventHandler::sDB && KESCMDrawEventHandler::sSrcDB != db)
		KESCMInvalidateDB(KESCMDrawEventHandler::sSrcDB);
}

// 現在の印刷マーク設定を返す(パネル再表示時の状態復元に使用)。
bool16 KESCMGetPrintMarks()
{
	return KESCMDrawEventHandler::sPrintMarks;
}

bool16 KESCMGetMarkOpacity25()
{
	return KESCMDrawEventHandler::sMarkOpacity25;
}
