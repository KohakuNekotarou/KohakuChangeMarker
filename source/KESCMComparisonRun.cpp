//========================================================================================
//
//  KESCMComparisonRun.cpp
//
//  比較の開始/解除と、それに付いてくる表示設定2つ(KESCMPanelObserver.cpp から分離。2026-08-13 の
//  model/UI 分割 第1段 Task 4)。どの2文書を比べるかの解決子と、その2文書で実際に始める手順、
//  Start/Stop のトグル、印刷マークと不透明度の切替を持つ。
//
//  ★分離では関数の中身を1行も変えていない。変えたのは「どのファイルに座るか」と「誰から見えるか」だけ。
//    解決子(KESCMResolveComparisonPair / KESCMFirstOtherDoc)も一緒に移した——使うのは
//    KESCMCanStartComparison と KESCMToggleStartStop の2本だけで、どちらもここへ来るため。
//
//  model 側: 比較そのものを動かす。呼ぶ側(フライアウトの項目・ブック比較の行の右クリック)は UI に残る。
//  ⚠この時点では6本とも末尾で KESCMRefreshPanel / KESCMSetStatus を呼んでいる＝逆流。
//    Task 9 で通知へ反転する。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// インターフェイス:
#include "ISession.h"				// GetExecutionContextSession(終了処理中は nil になり得るので型を明示して受ける)
#include "IApplication.h"			// QueryApplication
#include "IDocument.h"
#include "IDocumentList.h"
#include "IDataBase.h"
#include "PersistUtils.h"			// ::GetUIDRef
#include "PMString.h"

// プロジェクト内:
#include "KESCMComparisonRun.h"
#include "KESCMCore.h"				// arm/disarm・比較実行・印刷マーク設定・KESCMSetStatus / KESCMRefreshPanel
#include "KESCMUIShared.h"	// panel / status line / nav readout / tool button (split from KESCMCore.h on 2026-08-13)
#include "KESCMDrawEventHandler.h"	// sSrcMarksOn / sOversetOn / sOversetDB
#include "KESCMScrollMap.h"			// スクロールバー地図strip(Startで注入/Stopで取り外し)
#include "KESCMOversetApply.h"		// KESCMApplyOversetForDoc(Start/Stop 時の overset 貼り直し)

//----------------------------------------------------------------------------------------
// 解決子(どの2文書を比べるか)
//----------------------------------------------------------------------------------------

// (アクティブ(前面)文書=比較の Target の解決は KESCMActiveDoc(KESCMCore)に統合。2026-07-25 重複解消)

// 比較の Target/Source を解決する唯一の場所。アクティブ(前面)文書=Target、それ以外で最初に開いて
// いる文書=Source。両方引けたときだけ kTrue を返す(引けなかった側は nil のまま返るので、呼び出し側は
// どちらが欠けたかで文言を選べる)。
// ★ここに集約する理由: 「比較を始められるか」をメニューの有効/無効(KESCMCanStartComparison)と実行
//   (KESCMToggleStartStop)の2か所で別々に書くと、必ずどこかでずれる([[one-question-one-place]])。
static bool16 KESCMResolveComparisonPair(IDocument*& outTarget, IDocument*& outSource);

// target 以外で最初に開いている文書 = 比較の Source(旧版)。
static IDocument* KESCMFirstOtherDoc(IDocument* target)
{
	InterfacePtr<IApplication> app(GetExecutionContextSession() ? GetExecutionContextSession()->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return nil;
	const int32 n = docList->GetDocCount();
	for (int32 i = 0; i < n; ++i)
	{
		IDocument* d = docList->GetNthDoc(i);
		if (d != nil && d != target)
			return d;
	}
	return nil;
}

// 上で宣言した解決子の実体。
static bool16 KESCMResolveComparisonPair(IDocument*& outTarget, IDocument*& outSource)
{
	outTarget = KESCMActiveDoc();
	outSource = (outTarget != nil) ? KESCMFirstOtherDoc(outTarget) : nil;
	return (outTarget != nil && outSource != nil) ? kTrue : kFalse;
}

// 比較を開始できるか(KESCMComparisonRun.h で宣言)。フライアウトの「Start」を有効にしてよいかの判定で、
// 実行側 KESCMToggleStartStop と同じ解決子を通る。
bool16 KESCMCanStartComparison()
{
	IDocument* target = nil;
	IDocument* source = nil;
	return KESCMResolveComparisonPair(target, source);
}

//----------------------------------------------------------------------------------------
// アクション
//----------------------------------------------------------------------------------------

// KESCMToggleStartStop(KESCMComparisonRun.h で宣言) — 比較の開始/解除トグル。旧パネルの Start/Stop ボタンの
// DoStart/DoClear を統合した自由関数で、フライアウト項目 kKESCMPopupStartStopActionID の DoAction から
// 呼ぶ。arm 済みなら解除、未 arm なら開始。表示更新は KESCMRefreshPanel(可視パネルを arm 状態へ)。
// KESCMStopComparison(KESCMComparisonRun.h で宣言) — 比較を解除する(旧 DoClear)。
// ★KESCMToggleStartStop の解除分岐をそのまま切り出したもの。切り出した理由は下の
//   KESCMStartComparisonFor と同じ＝**手順を1か所にする**([[one-question-one-place]])。
void KESCMStopComparison()
{
	// アクティブ文書は再描画にだけ使う(nil でも可)。KESCMDoClearMarks / KESCMDoDisarmMousePeek は
	// 内部で「実際にマークが描かれていた文書」(sDB / arm 済み target)を控えて再描画するので、
	// 文書が1つも開いていなくても消去・解除は常に成立させる。
	IDocument* active = KESCMActiveDoc();
	IDataBase* db = (active != nil) ? ::GetUIDRef(active).GetDataBase() : nil;

	KESCMDoClearMarks(db);
	KESCMDoDisarmMousePeek(db);
	// スクロールバー地図: まず比較用の strip を全窓(Target/Source 両方)から取り外す。
	// ★Find Overset が単独 ON 中なら、続けて overset 文書(sOversetDB)だけへ strip を貼り直す
	//   (比較解除で Source 窓に strip が残らないようにする 2026-07-24)。あわせて overset を再走査して
	//   リフレッシュする(ユーザー報告: overset 有りで Start→編集で解消→Stop すると、集合が編集前のまま
	//   残り「まだ有る」と判断していた)。KESCMApplyOversetForDoc は sOversetDB を再走査し、サムネイル/
	//   地図の Attach+Invalidate/Prev-Next をまとめて更新する。
	KESCMScrollMapDetachAll();	// スクロールバー地図stripを全窓(Target/Source)から取り外す
	if (KESCMDrawEventHandler::sOversetOn)
		KESCMApplyOversetForDoc(KESCMDrawEventHandler::sOversetDB);
	PMString s("marks cleared"); s.SetTranslatable(kFalse);
	KESCMSetStatus(s);

	KESCMRefreshPanel();	// Target/Source 名・アイコン・Prev/Next 有効無効を arm 状態へ更新
}

// KESCMStartComparisonFor(KESCMComparisonRun.h で宣言) — **この2文書で**比較を開始する(旧 DoStart の本体)。
//
// ★★**解決子(どの2文書か)と手順(何をするか)を分けてある。** ここには「どの文書を選ぶか」の判断が
//   一切無く、渡された2つでそのまま始める。理由＝呼び手が2つあるため:
//     ①KESCMToggleStartStop  … アクティブ文書=Target・別の開いている文書=Source と解決してから呼ぶ
//     ②ブック比較の行の右クリック「Start Change Marker」… その章の2ファイルを開いてから呼ぶ
//   手順をそれぞれに書き写すと必ずずれる([[one-question-one-place]])。実際 Start の手順は
//   「sSrcMarksOn を戻す」「キャンセルなら arm しない」「strip を両窓へ」「overset を貼り直す」の
//   4つの決定を含んでいて、どれも忘れると静かに壊れる種類のもの。
void KESCMStartComparisonFor(IDocument* target, IDocument* source)
{
	if (target == nil || source == nil)
		return;

	IDataBase* targetDB = ::GetUIDRef(target).GetDataBase();
	IDataBase* sourceDB = ::GetUIDRef(source).GetDataBase();

	PMString report;
	// 「Show Marks on Source」は Start のたびに既定 ON へ戻す(仕様)。再比較(登録トグル/Ignore 切替)で
	// 黙って ON に戻さないよう、KESCMDoMarkChangesDoc 側ではなく Start 経路のここで立てる(2026-07-25)。
	KESCMDrawEventHandler::sSrcMarksOn = kTrue;
	// ★比較をユーザーがキャンセルしたら(ページ数が多いときは進捗バーに Cancel が出る)Start しない。
	//   マークは KESCMDoMarkChangesDoc 側で破棄済みなので、arm も strip 注入もせず「押す前」の状態へ
	//   戻す(中途半端に arm だけ残して、枠が1つも無い Start 中を作らない)。
	if (KESCMDoMarkChangesDoc(targetDB, sourceDB, report) == kSuccess)
	{
		KESCMDoArmMousePeek(targetDB, sourceDB);
		KESCMScrollMapAttach(targetDB);	// Target の各文書窓にスクロールバー地図stripを注入
		KESCMScrollMapAttach(sourceDB);	// Source 窓にも表示(2026-07-11 ユーザー要望。strip 側が窓の文書を見て供給元を切替)
		// ★Find Overset が ON のままなら、Start 時に必ず比較 Target を再走査して overset を貼り直す(2026-07-24)。
		//   これで (a) Start 後も Prev/Next が「変更(枠)→ overset」を同じ Target 文書で巡れる(overset が
		//   sOversetDB!=sDB で黙って巡回対象から外れる不具合の防止)＋(b) 同一文書でも編集で増減した overset を
		//   リフレッシュできる(ユーザー報告: 同一文書だと再走査せず古い集合が残っていた)。
		if (KESCMDrawEventHandler::sOversetOn)
			KESCMApplyOversetForDoc(targetDB);
	}
	KESCMSetStatus(report);

	KESCMRefreshPanel();
}

void KESCMToggleStartStop()
{
	const bool16 armed = KESCMIsArmed() && (KESCMArmedTargetDB() != nil);
	if (armed)
	{
		KESCMStopComparison();
		return;
	}

	// 開始。アクティブ(前面)文書=Target、別の開いている文書=Source。
	// ★フライアウトの Start は文書が2つ揃っていなければ灰色なので(KESCMCanStartComparison=同じ
	//   解決子を通る)、通常ここで欠けることは無い。メニューを開いたまま文書が閉じた場合などの保険。
	IDocument* target = nil;
	IDocument* source = nil;
	if (!KESCMResolveComparisonPair(target, source))
	{
		// 実際に欠けているものを言う(target が居るなら足りないのは Source だけ。2026-08-06 再点検)。
		PMString s(target == nil ? "Target and source documents not found."
		                         : "Source document not found.");
		s.SetTranslatable(kFalse);
		KESCMSetStatus(s);
		// ★この経路でも要る。⚠**旧実装では呼ばれていなかった**——この分岐は else ブロックの中で return
		//   しており、関数末尾の KESCMRefreshPanel には届いていなかった。∴これは切り出しに伴う挙動の
		//   **変更**(望ましい方向の)で、元の動作の保存ではない。
		KESCMRefreshPanel();
		return;
	}

	KESCMStartComparisonFor(target, source);
}

// KESCMSetMarkOpacity25(KESCMComparisonRun.h で宣言) — 枠の不透明度を 25%/75% に設定。旧パネルの opacity ラジオの
// 代わりに、フライアウト項目 kKESCMPopupOpacity25ActionID / kKESCMPopupOpacity75ActionID の DoAction から
// 呼ぶ。現在の印刷フラグ(KESCMGetPrintMarks)を維持したまま不透明度だけを反映する。ラジオ相当の見た目
// (選択中の項目に✓)はメニューを開いたときに UpdateActionStates が KESCMGetMarkOpacity25 を読んで反映する。
void KESCMSetMarkOpacity25(bool16 op25)
{
	IDocument* active = KESCMActiveDoc();
	IDataBase* db = (active != nil) ? ::GetUIDRef(active).GetDataBase() : nil;

	const bool16 flag = KESCMGetPrintMarks();	// 現在の印刷 ON/OFF を維持
	KESCMDoSetPrintMarks(flag, op25, db);

	PMString report;
	report.SetTranslatable(kFalse);
	report.Append(op25 ? "kescm: marks opacity 25%" : "kescm: marks opacity 75%");
	report.Append(flag ? "; will print (and stay visible on screen)"
	                   : "; screen-only (won't print)");
	KESCMSetStatus(report);
}

// KESCMTogglePrintMarks(KESCMComparisonRun.h で宣言) — 印刷マーク ON/OFF トグル。旧パネルのチェックボックスの
// 代わりに、フライアウト項目 kKESCMPopupPrintMarksActionID の DoAction から呼ぶ。現在の印刷フラグを反転し、
// 不透明度は現在の選択(KESCMGetMarkOpacity25)を維持して反映する。表示更新はステータス行のみ
// (チェックマークはメニューを開いたときに UpdateActionStates が KESCMGetPrintMarks を読んで反映する)。
void KESCMTogglePrintMarks()
{
	IDocument* active = KESCMActiveDoc();
	IDataBase* db = (active != nil) ? ::GetUIDRef(active).GetDataBase() : nil;

	const bool16 newFlag = !KESCMGetPrintMarks();
	const bool16 op25    = KESCMGetMarkOpacity25();
	KESCMDoSetPrintMarks(newFlag, op25, db);

	PMString report;
	report.SetTranslatable(kFalse);
	report.Append(op25 ? "kescm: marks opacity 25%" : "kescm: marks opacity 75%");
	report.Append(newFlag ? "; will print (and stay visible on screen)"
	                      : "; screen-only (won't print)");
	KESCMSetStatus(report);
}

// KESCMComparisonRun.cpp 終わり。
