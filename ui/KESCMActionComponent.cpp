//========================================================================================
//
//  KESCMActionComponent.cpp
//
//  プラグインの全メニューアクションの中枢。About・パネルフライアウトの全項目(Start/Stop・表示トグル・
//  Save/Load・Find Overset 等)・ページパネル右クリック(KCM: Check / Register / Refresh)の DoAction と
//  UpdateActionStates(動的ラベル・条件付き有効化)をここで担う。骨格は BasicPanel サンプル
//  (BscPnlActionComponent.cpp)を手本にしている(当初は About 2項目だけだった)。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// 一般:
#include "CActionComponent.h"
#include "CAlert.h"
#include "IActionStateList.h"		// UpdateActionStates(チェックマーク表示)。IPMUnknown 派生ではない
#include "PMString.h"

// ★Hide Unchanged Spreads(kHideSpreadCmdBoss)と overset 走査の適用は 2026-08-13 に
//   KESCMHideUnchanged.cpp / KESCMOversetApply.cpp へ移した(model/UI 分割 第1段 Task 2)。
//   ここに在った SDK インクルード(CmdUtils / ICommand / IBoolData / UIDList / ISpread /
//   ISpreadList / SpreadID / ISession / IApplication / IDocumentList / IDataBase と <map> / <set>)も、
//   それを使う側と一緒に移っている。
#include <vector>					// DoFindOversetToggle(サムネイル更新へ渡すページ列)

// (Split Target(90/10)機能は 2026-07-04 撤去。専用 include 群も削除。
//  仕組みは docs/ai-notes/kescm-split-target-mechanism.md と git 履歴 69c4b07 に保存)

// プロジェクト内:
#include "KCMUIID.h"
#include "KESCMLoc.h"		// 実行時の日本語切替(How to Use の1箇所だけ。Hide Unchanged の確認文言は
							// 2026-08-13 に本体ごと KESCMHideUnchanged.cpp へ移った)
#include "Utils.h"					// Utils<IKESCMCompareFacade>()
#include "IKESCMCompareFacade.h"	// ★UI が比較エンジンに頼む唯一の窓口(2026-08-13・model/UI 分割 第1段 Task 11)。
									//  Start/Stop・arm 状態・比較の実行・印刷マーク・overset・Hide Unchanged は
									//  すべてこれ経由。手本＝customconditionaltextui が Utils<ICusCondTxtFacade>() だけを使う形
#include "KESCMUIShared.h"	// panel / status line / nav readout / tool button (split from KESCMCore.h on 2026-08-13)
#include "IKESCMMarkData.h"			// マーク/overset の読み取り(2026-08-13 Task 12。表示トグルの読み書きは IKESCMCompareFacade 側)
#include "IKESCMPageFlagsFacade.h"	// Register(追加/削除ページ)と Check(✓)の2トグル＋メニュー状態＋Save/Load。
									// 2026-08-13 Task 13 で KESCMPageMap.h / KESCMPageCheck.h から移した
#include "KESCMThumbnailRefresh.h"	// KESCMTryRefreshPagesPanelThumbnails(Source サムネイルの枠を即 ON/OFF)
#include "KESCMViewSync.h"			// KESCMGetLayoutSync/Set/KESCMAlignOtherViewsToActiveNow(2026-08-13 に KESCMCore.h から移動)
#include "KESCMScrollMap.h"		// KESCMScrollMapAttach/DetachAll/InvalidateAll(地図トグルと Find Overset)
#include "KESCMPanelState.h"		// KESCMSavePanelState(フライアウト「Save Panel Settings」)
#include "KESCMPanelTitle.h"		// KESCMPanelTitle::Update(タブに Pixel / Story を出す)
#include "IKESCMBookFacade.h"		// ResolveBookPair(「Compare Books」を有効にしてよいかの判定)。2026-08-14 Task 15 で Facade 経由へ
#include "KESCMBookPanelLookup.h"	// KESCMGetPanelBookFile(前面タブの観測。2026-08-15 Task 9B で UI 側へ)
#include "KESCMBookRun.h"		// KESCMRunBookComparison(フライアウト「Compare Books」＝確認して比較して見せる)
#include "KESCMBookOpen.h"			// KESCMBookMenuRow/CanStart/StartComparisonForRow(章行の右クリック「Start Change Marker」)
#include "KESCMChangeNav.h"			// KESCMRefreshNavPosition(overset トグルで Prev/Next の対象数を更新)
#include "KESCMStoryRefresh.h"		// KESCMStoryMenuRow/CanRefresh/RefreshMenuRow(Story Edits 行の右クリック「Refresh Story Comparison」)
#include "KESCMPanelAlpha.h"		// KESCMGetPanelTranslucent/Set/Apply(フライアウト「Translucent Panel」)
#include "KESCMStoryPressMarks.h"	// KESCMStoryMarksRefresh(Story モードの常時表示マークを作り直す)
// (★`IActiveContext.h` / `IDocument.h` / `PersistUtils.h` の3本は 2026-08-18 に撤去＝不具合再検査 B-U3。
//  **どれも一度も使っていなかった**。DoAction / UpdateActionStates は `IActiveContext*` を受け取るが
//  仮引数名ごとコメントアウトしてあり、「アクティブ文書 → db」の解決は 2026-08-13 の model/UI 分割で
//  model 側(KESCMActiveDoc / GetOversetScanTargetDB)へ出ている。⚠旧コメントは残った include に
//  「GetContextDocument(アクティブ文書の解決)」と**使っていない機能の説明**を付けており、
//  読む人には「ここで解決している」と見える形だった。)

// ★注意: source/public/includes/URLUtils.h は "namespace URLUtils { PUBLIC_DECL void GoToURL(...); }" と
// 宣言しているが、これはヘッダーとバイナリの不一致(Public.lib 側の実エクスポート名と食い違っている)。
// build/win/objrx64/Public.lib の生シンボルを確認したところ、実際にリンク可能な名前は
// "?GoToURL@GoToURLUtils@@YAXAEBVPMString@@F@Z" = void GoToURLUtils::GoToURL(const PMString&, bool16)
// であり、URLUtils 名前空間版は存在しない(リンクエラー確認済み)。ヘッダーは信用せず、実バイナリに
// 合わせてここで自前に前方宣言する。
namespace GoToURLUtils
{
	PUBLIC_DECL void GoToURL(const PMString& goToURL, bool16 isAGoURL);
}

// (「Hide Unchanged Spreads」の状態5本 —— トグルの旗と、Target/Source 各側の IDataBase* と
//  隠したスプレッド UID の控え —— は 2026-08-13 に KESCMHideUnchanged.cpp へ移した。書き手である
//  トグル本体を一緒に移してあるので、状態が分割の両側に割れることはない。ここから読むのは
//  メニューのチェックマークだけで、Facade の GetHideUnchangedOn() で聞く。)

// (overset 走査の対象文書を返す KESCMOversetScanTargetDB も、2026-08-13 の Task 11 から
//  Facade の GetOversetScanTargetDB() で聞く。以前はこのファイルの下の方に static で持っており、
//  ここに前方宣言があった。)

/** ChangeMarker プラグインのメニュー項目に対する IActionComponent の実装。
*/
class KESCMActionComponent : public CActionComponent
{
public:
	KESCMActionComponent(IPMUnknown* boss) : CActionComponent(boss) {}

	/** Execute the requested menu action. */
	void DoAction(IActiveContext* ac, ActionID actionID, GSysPoint mousePoint = kInvalidMousePoint, IPMUnknown* widget = nil);

	/** チェック式トグル(kCustomEnabling)のチェックマークを現在の状態に合わせて更新する。 */
	virtual void UpdateActionStates(IActiveContext* ac, IActionStateList* listToUpdate, GSysPoint mousePoint = kInvalidMousePoint, IPMUnknown* widget = nil);

private:
	void DoAbout();
	void DoUsage();
	void DoFindOversetToggle();		// フライアウト「Find Overset」: アクティブ文書を走査して十字表示/消去(トグル)
	void DoRefreshOverset();		// フライアウト「Refresh Overset」: ON時のみ・アクティブ文書を再走査
};

/* Binds the C++ implementation class onto its ImplementationID. */
CREATE_PMINTERFACE(KESCMActionComponent, kKESCMActionComponentImpl)

/* KESCMApplyCompareMode — 比較モードを切り替え、Start 中ならその場で比較し直す(2026-08-20)。

   ★**2つの入口が同じ手順を通るように関数にした**。Pixel / Story のどちらを選んでも起きることは
   同じで、違うのは渡す値だけ(Marks opacity 25%/75% が Facade の1関数を共有しているのと同型)。

   ⚠**同じモードを選び直したときは何もしない**。すでに Story を見ている人がもう一度 Story を選んで
   比較が走り直したら、待たされた末に同じ画面が出るだけになる。
*/
static void KESCMApplyCompareMode(KESCMCompareMode mode)
{
	InterfacePtr<IKESCMCompareFacade> compare(Utils<IKESCMCompareFacade>().QueryUtilInterface());
	if (compare == nil || compare->GetCompareMode() == mode)
		return;

	compare->SetCompareMode(mode);

	PMString msg(mode == kKESCMModeStory ? "Compare mode: story changes." : "Compare mode: pixel changes.");
	msg.SetTranslatable(kFalse);

	// ★Start 中なら、新しいモードで全体を比較し直す。比較していないときは設定を変えるだけ＝
	//   次に Start したときに効く。
	InterfacePtr<IKESCMMarkData> marks(Utils<IKESCMMarkData>().QueryUtilInterface());
	IDataBase* const markedDB    = (marks != nil) ? marks->GetMarkedTargetDB() : nil;
	IDataBase* const markedSrcDB = (marks != nil) ? marks->GetMarkedSourceDB() : nil;
	if (markedDB != nil && markedSrcDB != nil)
	{
		PMString report;
		// ★allowIncremental は渡さない=全ページ再比較。差分再比較は「前回と同じ比較方法で、
		//   ペアの変わったページだけやり直す」ための道具で、比較方法そのものが変わるここでは
		//   前回の結果を1つも再利用できない。
		// ★キャンセルされたときの後始末は Ignore Page Number Marker と同じ理由で Stop まで戻す
		//   (マークは破棄済みなので、arm だけ残ると「枠が1つも無い Start 中」になる)。
		if (compare->MarkChanges(markedDB, markedSrcDB, report) == kSuccess)
			msg.Append(" (recompared)");
		else
		{
			compare->ToggleStartStop();
			msg.Append(" (cancelled - stopped)");
		}
	}

	// ★★タブに今のモードを出す（2026-08-21・ユーザー指定「KBS のドキュメントとブックの様に」）。
	//   ここは「モードが変わる唯一の場所」なので、書き直す場所もここ1つで足りる。
	//   （パネルを開き直したときは KESCMPanelObserver::AutoAttach が同じ関数を呼ぶ。）
	KESCMPanelTitle::Update();

	KESCMSetStatus(msg);
}

/* DoAction */
void KESCMActionComponent::DoAction(IActiveContext* /*ac*/, ActionID actionID, GSysPoint /*mousePoint*/, IPMUnknown* /*widget*/)
{
	switch (actionID.Get())
	{
		case kKESCMAboutActionID:
		case kKESCMPopupAboutThisActionID:
			this->DoAbout();
			break;

		// フライアウト先頭の「Start / Stop」: 比較の開始/解除トグル(旧パネルボタン→2026-07-10 メニュー化)。
		// 実体は KESCMComparisonRun.cpp の自由関数(arm 状態を見て開始 or 解除、実行後にパネル更新)。
		case kKESCMPopupStartStopActionID:
			Utils<IKESCMCompareFacade>()->ToggleStartStop();
			break;

		// フライアウトの「Print comparison marks」: 印刷マーク ON/OFF トグル(旧パネルのチェックボックス
		// →2026-07-10 メニュー化)。実体は KESCMPanelObserver.cpp の自由関数。
		case kKESCMPopupPrintMarksActionID:
			Utils<IKESCMCompareFacade>()->TogglePrintMarks();
			// ★★2026-08-23＝**Story モードのマークもこのトグルを入力に持つようになった**ので、
			//   ここでも作り直しを頼む。Print が ON の間は画面にも常時出る（Pixel の枠と同じ
			//   WYSIWYG）ので、頼まないと**トグルを切っても Story のマークが動かない**。
			//   ⚠この case だけ長らく Refresh を呼んでいなかった＝呼んでいる下の4つ（opacity 2つ・
			//     Show Src/Tgt）と揃った。
			KESCMStoryMarksRefresh();
			break;

		// フライアウトの「Marks opacity 25% / 75%」(ラジオ風): 選んだ方の不透明度に設定する。
		// 実体は KESCMPanelObserver.cpp の自由関数(印刷フラグは維持し不透明度だけ変更)。
		// ⚠★★**Story の色地マークは「出したときの不透明度」を焼き込んで持っている**(2026-08-22 の
		//   不具合再検査 A4)。model 側の SetMarkOpacity25 が再描画するのは Pixel の枠だけで、あちらは
		//   描画のたびに現在値を読み直すが、こちらはアドーンメントに載せた値がそのまま残る。
		//   ⇒ **設定を変えたら作り直しを頼む**。★model からは頼めない＝あちらは UI プラグインの
		//     アドーンメントを知らない(model/UI 分割の依存は UI→model の一方向)。∴ここで呼ぶ。
		case kKESCMPopupOpacity25ActionID:
			Utils<IKESCMCompareFacade>()->SetMarkOpacity25(kTrue);
			KESCMStoryMarksRefresh();
			break;
		case kKESCMPopupOpacity75ActionID:
			Utils<IKESCMCompareFacade>()->SetMarkOpacity25(kFalse);
			KESCMStoryMarksRefresh();
			break;

		// ★「Mark colour」(2026-08-24)。⚠**上の opacity と違い、ここでは作り直しを頼まない。**
		//   不透明度は**マークを install したときの値がそのまま載る**ので設定を変えたら作り直しが
		//   要るが、色は Story 側の Draw が**描くたびに SelectedMarkColor() を読み直す**ので、
		//   model 側の再描画(KESCMDoSetMarkColor)だけで新しい色になる。
		//   ★同じ「設定を変えた」でも、値がどこに載っているかで必要な後始末が違う ---- Pixel の
		//     リング画像はキャッシュなので model 側でキャッシュを畳んでいる(KESCMCore.cpp)。
		case kKESCMPopupColorRedActionID:
			Utils<IKESCMCompareFacade>()->SetMarkColor(kFalse);
			break;
		case kKESCMPopupColorCyanActionID:
			Utils<IKESCMCompareFacade>()->SetMarkColor(kTrue);
			break;

		// (kKESCMPopupAboutScriptActionID / DoAboutScript は 2026-07-25 撤去=About Scripting 項目削除。)

		case kKESCMPopupUsageActionID:
			this->DoUsage();
			break;

		// 「Show Marks on Source」トグル: フラグを反転して Source 文書を再描画する(Pixel の表示判定と描画は
		// KESCMDrawEventHandler::HandleDrawEvent の Source 分岐。ON の間は常時表示・OPPでも表示・印刷にも
		// 出る。不透明度はパネルの 25%/75% 選択に連動)。★既定 OFF で Start は触らない(2026-08-22 変更＝
		// 設定はパネル設定に保存され起動時に復元されるので、Start が上書きすると保存した選択が消える)。
		// ⚠★★**Target 版と同じく2つの機構に効く**(2026-08-22 の不具合再検査 A1)＝Pixel の枠は描画側が
		//   sSrcMarksOn を直接見るが、Story の色地マークは別機構(グローバルテキストアドーンメント)なので
		//   こちらから作り直しを頼む。⇒ **これが無いと Story モードでは ON にしても出ず、OFF にしても
		//   消えない**(内部状態が変わらないので、下の InvalidateDB で描き直しても同じ絵が出るだけ)。
		case kKESCMPopupShowSrcMarksActionID:
		{
			InterfacePtr<IKESCMCompareFacade> compare(Utils<IKESCMCompareFacade>().QueryUtilInterface());
			const bool16 srcMarksOn = !compare->GetShowSourceMarks();
			compare->SetShowSourceMarks(srcMarksOn);
			// ⚠★★★**押下フラグはここで触らない**(2026-08-22・独立レビューの指摘で撤去)。
			//   撤去した Hold から引き継いだときは sSrcMarksTempHidden＝「隠している」だったので、
			//   トグルを切り替えるついでに解除するのが正しかった。**同じ日の改名でこれは
			//   「ツールの左ボタンが物理的に押されている」に変わった**——メニュー操作がそれを
			//   「押していない」と言い張ってよい道理は無い。
			//   ★実害＝ショートカットを割り当てて**押下中に**叩くと、離したときに
			//     KESCMTrackerRevealEnd が GetSrcMarksPressed()==kFalse を見て InvalidateDB を
			//     飛ばし、Source の窓に押下中の枠が残る。
			//   ★KESCMTrackerRevealBegin 側は「トグルを見ない」へ正しく直してあった＝これは
			//     同じ変更の片割れの直し忘れ([[one-question-one-place]])。
			KESCMStoryMarksRefresh();		// Story モードの色地マーク(Pixel モードでは何もしない)
			IDataBase* const srcDB = Utils<IKESCMMarkData>()->GetMarkedSourceDB();
			Utils<IKESCMCompareFacade>()->InvalidateDB(srcDB);
			// ★レイアウトビューだけでなく Pages パネルの Source サムネイルも即時更新する。Source 側の枠は
			//   wantSrcMarks(=sSrcMarksOn)に依存し、サムネイル(isThumb)でも強制表示されないため、トグルで
			//   サムネイルを作り直さないと OFF にしても枠が残る/ON にしても出ない。対象ページは Source の
			//   変更/overflow/登録集合(KESCMCollectChangedPageUIDs が引く)で、枠が出得るページと一致する。
			KESCMTryRefreshPagesPanelThumbnails(srcDB);
			PMString msg(srcMarksOn ? "Source marks: on." : "Source marks: off.");
			msg.SetTranslatable(kFalse);
			KESCMSetStatus(msg);
			break;
		}

		// 「Show Marks on Target」トグル: フラグを反転して Target 文書を再描画する。★Source 版と対で、
		// ON の間はツールを押さなくてもマークが出たままになる(2026-08-22 ユーザー要望
		// 「ツールでボタンを押さなくても常にマークが出る様に」)。★既定 OFF で Start は触らない
		// (Source 版と同じ理由＝設定はパネル設定に保存され、起動時に復元される)。
		// ⚠★★**2つの機構に効く**＝Pixel の比較リングは描画側が sTgtMarksOn を直接見る
		//   (KESCMDrawEventHandler の alwaysScreen)が、Story の色地マークは別機構(グローバルテキスト
		//   アドーンメント)なので、こちらから作り直しを頼む。同じトグルで両モードが動くのはそのため。
		// ⚠Pages パネルのサムネイルは触らない＝サムネイルは isThumb で常にマークを描くので、このトグルで
		//   見た目は変わらない(Source 版が作り直すのは、あちらの枠が wantSrcMarks に依存するため)。
		case kKESCMPopupShowTgtMarksActionID:
		{
			InterfacePtr<IKESCMCompareFacade> compare(Utils<IKESCMCompareFacade>().QueryUtilInterface());
			const bool16 tgtMarksOn = !compare->GetShowTargetMarks();
			compare->SetShowTargetMarks(tgtMarksOn);
			// ⚠★★この2行は撤去した「Hold to Hide Marks」から引き継いだ後始末(2026-08-22)。
			//   ①押下中の一時退避を解除する(押したままトグルを切り替える道は無いが、離す前に
			//     何かで状態が残ると「ON なのに出ない」になる)。
			//   ②**常時表示の基準不透明度を即反映する**＝KESCMBaseScreenOpacity は
			//     「印刷 ON、または枠が常時出ている」ときだけ 25%/75% を返す。⇒ ここで更新しないと、
			//     ON にした直後の枠が 1.0(不透明)のまま描かれる。
			compare->SetMarksTempHidden(kFalse);
			compare->SetMarkScreenOpacity(compare->GetBaseScreenOpacity());
			KESCMStoryMarksRefresh();		// Story モードの色地マーク(Pixel モードでは何もしない)
			compare->InvalidateDB(compare->GetArmedTargetDB());
			PMString msg(tgtMarksOn ? "Target marks: on." : "Target marks: off.");
			msg.SetTranslatable(kFalse);
			KESCMSetStatus(msg);
			break;
		}

		// 「Hide Unchanged Spreads」トグル: OFF→ON は確認ダイアログ→変更なしスプレッドを隠す。
		// ON→OFF は自分が隠した分だけ再表示。本体は KESCMHideUnchanged.cpp の自由関数
		// (2026-08-13 に DoHideUnchangedToggle から移動＝隠す/戻すのは model 側の仕事)。
		case kKESCMPopupHideUnchangedActionID:
			Utils<IKESCMCompareFacade>()->HideUnchangedToggle();
			break;

		// フライアウト「Find Overset」トグル: アクティブ文書を走査し overset のあるページへ十字表示/OFFで消去。
		case kKESCMPopupFindOversetActionID:
			this->DoFindOversetToggle();
			break;

		// フライアウト「Refresh Overset」: Find Overset が ON のときだけ有効=アクティブ文書を再走査して貼り直す。
		case kKESCMPopupRefreshOversetActionID:
			this->DoRefreshOverset();
			break;

		// 「Show Original Page Numbers」トグル: フラグを反転して再描画するだけ(バッジの表示判定と描画は
		// KESCMDrawEventHandler::HandleDrawEvent。表示は枠と同じ可視条件=印刷マークONの常時表示、または
		// ツール左hold中)。再描画は隠しの当事者になりやすい Target(sDB)と Source を対象にする(他の文書は
		// 次の自然な再描画で反映される)。
		case kKESCMPopupShowOldNumsActionID:
		{
			InterfacePtr<IKESCMCompareFacade> compare(Utils<IKESCMCompareFacade>().QueryUtilInterface());
			const bool16 showOldNums = !compare->GetShowOldPageNumbers();
			compare->SetShowOldPageNumbers(showOldNums);
			IDataBase* const markedDB = Utils<IKESCMMarkData>()->GetMarkedTargetDB();
			Utils<IKESCMCompareFacade>()->InvalidateDB(markedDB);
			if (compare->GetArmedSourceDB() != markedDB)
				Utils<IKESCMCompareFacade>()->InvalidateDB(compare->GetArmedSourceDB());
			PMString msg(showOldNums ? "Show original page numbers: on." : "Show original page numbers: off.");
			msg.SetTranslatable(kFalse);
			KESCMSetStatus(msg);
			break;
		}

		// 「Sync Layout Views」トグル: レイアウトビュー同期の ON/OFF(既定 ON)。実体は KESCMPeek.cpp の
		// KESCMSetLayoutSync(購読の付け外し+ON時は即時に一度そろえる)。作動条件は2モード(判定は
		// KESCMSyncOtherDocViewportsTo のガード):
		//   (A) Start 中: Target↔Source 間のみ・追加/削除補正あり(2026-07-11)。
		//   (B) Stop 中 + KESCM ツール選択中: アクティブ文書へ他の全文書を同期・補正なし(2026-07-15)。
		// トグル ON でも上記いずれの条件も満たさなければ(Stop かつツール非選択)購読はするが同期は no-op。
		case kKESCMPopupSyncViewsActionID:
		{
			KESCMSetLayoutSync(!KESCMGetLayoutSync());
			PMString msg;
			if (KESCMGetLayoutSync())
				msg = "Sync layout views: on.";
			else
				msg = "Sync layout views: off.";
			msg.SetTranslatable(kFalse);
			KESCMSetStatus(msg);
			break;
		}

		// 「Align Other Views to Active」(実行アクション・ショートカット割当可): アクティブ(最前面)
		// レイアウトビューの位置+拡大率を他文書の全ビューへ1回そろえる。Sync Layout Views トグルとは独立
		// (OFF でも効く)。Start 中はページの Add/Remove 補正あり。実体は ui/KESCMViewSync.cpp の
		// KESCMAlignOtherViewsToActiveNow(トグル ON 時の初回そろえと同じ同期エンジン)。
		// ⚠2026-08-19(不具合再検査 B-U7)に置き場所を訂正＝**分割で KESCMPeek.cpp から出て行った**のに
		//   「実体は KESCMPeek.cpp」と書いたままだった(KCMUIID.h:219 にも同じ誤りが残っていた=兄弟2件)。
		case kKESCMPopupAlignViewsActionID:
		{
			const bool16 ok = KESCMAlignOtherViewsToActiveNow();
			// false は3通り: (a) 最前面レイアウトビューが無い / (b) Start 中で最前面が Target/Source 以外の
			// 第3文書(engine が同期しない) / ★(c) そろえる相手の窓が1つも無い(文書が1つだけ・相手が閉じた・
			// Target と Source が同じ文書)。どれも「実際にそろえていない」ので成功表示は出さない。
			// ⚠(c) は 2026-08-19 まで**呼び手に伝わらず、成功表示になっていた**(不具合再検査 B-U7 の A-1)。
			//   文言も「no view to align **from**」＝手本が無い意味だったので、3通りに当てはまる形へ改めた。
			PMString msg(ok ? "Aligned other views to the active view."
			                : "Align: no other view to align (while Started, use the Target or Source view).");
			msg.SetTranslatable(kFalse);
			KESCMSetStatus(msg);
			break;
		}

		// 「Show Scrollbar Map」トグル: 文書窓の縦スクロールバー脇に変更位置の地図 strip を出すか(既定 ON)。
		// ON にしたら現在の比較対象(sDB/sSrcDB)へ即 attach して表示、OFF にしたら全窓から即 detach。
		// 未 arm(sDB=nil)で ON にした場合は attach が no-op=次の Start で自然に出る(フラグは ON のまま)。
		case kKESCMPopupScrollMapActionID:
		{
			const bool16 on = !KESCMGetScrollMapEnabled();
			KESCMSetScrollMapEnabled(on);
			if (on)
			{
				InterfacePtr<IKESCMMarkData> marks(Utils<IKESCMMarkData>().QueryUtilInterface());
				if (marks->GetMarkedTargetDB() != nil) KESCMScrollMapAttach(marks->GetMarkedTargetDB());
				if (marks->GetMarkedSourceDB() != nil) KESCMScrollMapAttach(marks->GetMarkedSourceDB());
				// Find Overset 単独で ON 中なら、その走査文書窓にも地図を復帰させる(2026-07-24)。
				if (marks->GetOversetOn() && marks->GetOversetDB() != nil)
					KESCMScrollMapAttach(marks->GetOversetDB());
				KESCMScrollMapInvalidateAll();
			}
			else
				KESCMScrollMapDetachAll();	// 既存 strip を全窓から撤去
			PMString msg(on ? "Scrollbar map: on." : "Scrollbar map: off.");
			msg.SetTranslatable(kFalse);
			KESCMSetStatus(msg);
			break;
		}

		// (★「Show HUD」トグルは 2026-08-06 に廃止＝押下中 HUD は**常に出る**ので、出す/出さないを
		//  選ぶメニュー項目が無い。⚠**HUD 自体は現役**: 2026-08-06 に sprite 版を全廃し、2026-08-07 に
		//  Draw Event で作り直した(KESCMTrackerHud.cpp。押した窓が Target/Source かを左上に出す)。
		//  ここには全廃した当日の「機能そのものを無くした」が同日中の作り直しを反映しないまま残っていた。)

		// 「Translucent Panel」トグル: このパネル自身を半透明(alpha は kKESCMPanelAlphaValue=77 ≒ 30%。
		// 2026-07-29 に 128 から変更)にするか
		// (★Windows 専用・既定 OFF)。効くのは「フローティング中」と「アイコンからのドロワー展開中」の
		// 2 つ。ドック内で展開中は選べるが見た目は変わらない=フラグだけ立ち、上記のどちらかに
		// 戻した時点で効く(その追随は KESCMPanelObserver.cpp が
		// kPaletteVisibilityChangedMessage を購読して行う)。実体は KESCMPanelAlpha.cpp。
		case kKESCMPopupTranslucentPanelActionID:
		{
			const bool16 on = !KESCMGetPanelTranslucent();
			KESCMSetPanelTranslucent(on);

			// 実際に窓へ届いたかでステータス文言を分ける。ドック内で展開中に押しても画面が
			// 変わらないので、「なぜ効かないか」を言葉で返す。
			const bool16 applied = KESCMApplyPanelTranslucency();

			// ★★OFF に戻すと alpha 255 と影の再表示を**その対象の今のトップレベル窓**へ書くが、
			//   両パネルが**同じフローティンググループ**にいるとその窓は相手と共有なので、ON のまま
			//   の相手の半透明まで消える。ON の対象だけ貼り直して取り戻す(ON にしたときは呼ばない)。
			//   ⚠2026-08-07 の再点検で発見。同日 48f0a6b が KESCMApplyAllPanelTranslucency に入れた
			//     「OFF は飛ばす」の取り残しで、こちらは**対象を名指しで呼ぶ**復元経路だった。
			if (!on)
				KESCMApplyAllPanelTranslucency();

			PMString msg;
			if (!on)
				msg = "Translucent panel: off.";
			else if (applied)
				msg = "Translucent panel: on.";
			else
				msg = "Translucent panel: on - has no effect while the panel is docked.";
			msg.SetTranslatable(kFalse);
			KESCMSetStatus(msg);
			break;
		}

		// 「Translucent Pages Panel」トグル: **本体のページパネル**を半透明にするか(2026-08-06 追加)。
		// 上の Translucent Panel と同じ仕組み・同じ制約で、対象だけが違う。
		// ★対象の窓は WidgetID(kPagesPanelWidgetID = 数値)から引く。窓タイトルは UI 言語で変わる
		//   ("Pages" / 「ページ」)ので、タイトル照合では本体パネルに届かない。実体 KESCMPanelAlpha.cpp。
		case kKESCMPopupTranslucentPagesActionID:
		{
			const bool16 on = !KESCMGetPagesPanelTranslucent();
			KESCMSetPagesPanelTranslucent(on);

			// 実際に窓へ届いたかでステータス文言を分ける。ドック内で展開中に押しても画面が
			// 変わらないので、「なぜ効かないか」を言葉で返す。
			// ★ページパネルは既定でドックに入っているので、こちらの方が「効かない」に当たりやすい。
			const bool16 applied = KESCMApplyPagesPanelTranslucency();

			// ★上の Translucent Panel と同じ理由で、OFF に戻したときだけ ON の対象を貼り直す
			//   (同じフローティンググループに入れていると、こちらの 255 復元が相手を巻き込むため)。
			if (!on)
				KESCMApplyAllPanelTranslucency();

			PMString msg;
			if (!on)
				msg = "Translucent Pages panel: off.";
			else if (applied)
				msg = "Translucent Pages panel: on.";
			else
				msg = "Translucent Pages panel: on - has no effect while the Pages panel is docked or closed.";
			msg.SetTranslatable(kFalse);
			KESCMSetStatus(msg);
			break;
		}

		// (「Translucent Toolbox」トグルは 2026-08-07 に追加し、同日ユーザー判断で撤去した
		//  ＝本体のツールボックスの見た目を変える機能は KESCM には載せない。ActionID +38 は
		//  欠番のまま再利用しない(.indk はショートカットを数値の ActionID で保存するため)。)

		// 「Translucent Book Dialog」トグル: **自分のブック比較ダイアログ**を半透明にするか
		// (2026-08-13 ユーザー要望「ダイアログも半透明に出来る様に」)。上2つと同じ実体
		// (KESCMPanelAlpha.cpp)で対象だけが違う。
		// ★上2つと違う点が2つある:
		//   ①ダイアログは**常にフローティング**なので「押しても効かない状態」が無い ⇒ 文言を
		//     「ドッキング中なので効かない」で分ける必要がそもそも無い。分かれるのは「今そのダイアログが
		//     開いているか」だけ。
		//   ②**OFF に戻したときの貼り直しが要らない**。上2つが KESCMApplyAllPanelTranslucency を
		//     呼ぶのは、パネル同士が同じフローティンググループに入ると 255 の復元が相手を巻き込むから
		//     (2026-08-07 の実害)。ダイアログは自分だけの窓なので、その共有が起こらない。
		case kKESCMPopupTranslucentBookDialogActionID:
		{
			const bool16 on = !KESCMGetBookDialogTranslucent();
			KESCMSetBookDialogTranslucent(on);

			const bool16 applied = KESCMApplyBookDialogTranslucency();

			PMString msg;
			if (!on)
				msg = "Translucent book dialog: off.";
			else if (applied)
				msg = "Translucent book dialog: on.";
			else
				msg = "Translucent book dialog: on - takes effect the next time the dialog is open.";
			msg.SetTranslatable(kFalse);
			KESCMSetStatus(msg);
			break;
		}

		// (★「Hold to Hide Marks」(+19)は 2026-08-22 に撤去＝ユーザー決定。「常時表示」が
		//  「Show Marks on Target」と完全に重複したため。固有だった「押している間だけ隠す」は
		//  **両トグル ON のときの標準の挙動**になった＝規則は「押している間は反対になる」の1本。
		//  ⚠**この case が持っていた後始末2つは上の2つのトグルへ移してある**＝一時退避の解除と、
		//    常時表示の基準不透明度の即反映(落とすと「ON にしたのに枠が不透明のまま出る」)。
		//  ActionID +19 は欠番のまま再利用しない。)

		// 「Ignore Page Number Marker」トグル: ノンブル(自動ページ番号)マーカーを含むフレームを
		// 比較(CMYKピクセル差分)から除外するか(既定ON)。フラグを反転し、既にStart済みなら
		// 登録トグルと同じ理由で全体再比較して即座に反映する。
		case kKESCMPopupIgnorePageNumActionID:
		{
			InterfacePtr<IKESCMCompareFacade> folio(Utils<IKESCMCompareFacade>().QueryUtilInterface());
			folio->SetIgnorePageNumberMarker(!folio->GetIgnorePageNumberMarker());
			PMString msg(folio->GetIgnorePageNumberMarker() ? "Ignore page number marker: on." : "Ignore page number marker: off.");
			msg.SetTranslatable(kFalse);
			InterfacePtr<IKESCMMarkData> marks(Utils<IKESCMMarkData>().QueryUtilInterface());
			IDataBase* const markedDB    = marks->GetMarkedTargetDB();
			IDataBase* const markedSrcDB = marks->GetMarkedSourceDB();
			if (markedDB != nil && markedSrcDB != nil)
			{
				PMString report;
				// ★ここは allowIncremental を渡していない=全ページ再比較なので、ページ数が多ければ
				//   進捗バーに Cancel が出る。キャンセルされると KESCMDoMarkChangesDoc 側でマークが
				//   全部破棄され kFailure が返る。戻り値を捨てると arm だけが残って「枠が1つも無い
				//   Start 中」になるので、Start 経路(KESCMToggleStartStop)と同じ考え方で Stop まで戻す
				//   (KESCMToggleStartStop は arm 中に呼べば Stop 分岐に入る)。2026-07-29 の自己レビューで発見。
				if (Utils<IKESCMCompareFacade>()->MarkChanges(markedDB, markedSrcDB, report) == kSuccess)
				{
					msg.Append(" (recompared)");
				}
				else
				{
					Utils<IKESCMCompareFacade>()->ToggleStartStop();		// マーク破棄済み → strip 撤去・disarm まで揃えて Stop へ
					msg.Append(" (cancelled - stopped)");
				}
			}
			KESCMSetStatus(msg);
			break;
		}

		// ★★フライアウトの「Compare mode > Pixel Changes / Story Changes」(2026-08-20)。
		//   何を比べるかを切り替える。上の「Ignore Page Number Marker」と同じ形＝設定を変えて、
		//   既に Start 済みならその場で全体を比較し直す。
		//   ⚠**前のモードの結果は捨てる**。2つの結果を同時に持つと「いま画面が見せているのはどちら
		//     なのか」の答えが2か所に生まれる([[one-question-one-place]])。
		case kKESCMPopupModePixelActionID:
			KESCMApplyCompareMode(kKESCMModePixel);
			break;
		case kKESCMPopupModeStoryActionID:
			KESCMApplyCompareMode(kKESCMModeStory);
			break;

		// フライアウトの「Save Panel Settings」: 現在の設定系トグルを独自 JSON でローカルへ保存し、
		// 保存先パスを**パネルのステータス行**に出す(実体は KESCMPanelState.cpp の KESCMSavePanelState。
		// ⚠旧引用 ":132-137" は 2026-08-16 の監査 B-U3 時点で fclose のエラー処理を指していた＝関数名で引く)。
		// 読み込みは起動時(KESCMUIStartup::Startup。2026-07-15 に「パネル初回オープン時」から前倒し=
		// KESCMPanelState.h の説明が正)。(旧コメントの「ダイアログ表示する」は 2026-07-11 に
		// モーダルからステータス行へ変えた時点で陳腐化していた。2026-08-06 監査で現行化。)
		case kKESCMPopupSavePanelStateActionID:
			KESCMSavePanelState();
			break;

		// ページパネルのページ右クリック「KCM: Register as Added/Removed Pages」トグル。
		// 選択ページを「比較相手なし」として登録/解除する(実体は KESCMPageMap.cpp。このステップでは
		// 登録の保持とチェック表示まで。比較の除外対応表への反映は次ステップ)。
		case kKESCMPageMapToggleActionID:
			Utils<IKESCMPageFlagsFacade>()->ToggleRegisterForSelection();
			break;

		// ページパネルのページ右クリック「KCM: Check」トグル。選択ページに✓印を付け外しする
		// (実体は KESCMPageCheck.cpp。✓の描画は KESCMDrawEventHandler の isThumb 分岐)。
		case kKESCMPageCheckToggleActionID:
			Utils<IKESCMPageFlagsFacade>()->ToggleCheckForSelection();
			break;

		// ページパネルのページ右クリック「KCM: Refresh Page Comparison」(実行アクション)。選択ページの
		// 比較を再検出して枠/サムネイルを更新する(旧 Ctrl+ミドルのスプレッド再比較を移設。2026-07-13)。
		// 実体は KESCMPeek.cpp。結果をステータス行に短く出す。
		case kKESCMPageRefreshCompareActionID:
		{
			int32 nPages = 0, nChanged = 0, nFailed = 0;
			bool16 wasCancelled = kFalse;
			if (Utils<IKESCMCompareFacade>()->RefreshSelectedPages(&nPages, &nChanged, &wasCancelled, &nFailed))
			{
				PMString msg("refreshed ");
				msg.SetTranslatable(kFalse);
				msg.AppendNumber(nPages);
				msg.Append(" (changed ");
				msg.AppendNumber(nChanged);
				msg.Append(")");
				// ★比較できなかったページ(ページサイズ不一致・ラスタ化失敗)は隠さない。その分の枠は
				//   前回のまま=最新でないことをユーザーに伝える(2026-08-06 再点検)。
				if (nFailed > 0)
				{
					msg.Append(" (failed ");
					msg.AppendNumber(nFailed);
					msg.Append(")");
				}
				// ★途中で止めた場合は明示する(残りの選択ページは古いままなので、全部終わったと
				//   誤解させない。2026-07-27 に進捗バー＋キャンセルを追加)。
				if (wasCancelled)
					msg.Append(" - cancelled");
				KESCMSetStatus(msg);
			}
			else
			{
				// 有効化判定(KESCMRefreshComparisonAvailable)は選択の中身まで見ないため、選択が空/全ページ
				// 未対応(Added/Removed 登録等)だと何も処理せず kFalse で戻る。その場合も無反応にせず
				// 「今回は何も再比較しなかった」ことをステータス行に出す(前回の refreshed 表示の残留による
				// 成功誤認を防ぐ。2026-07-15)。※キャンセルは押した時点のページを処理済み=上の枝に入るので、
				// ここへ来るのは通常「対象が無かった」ときだけ。出し分けは念のため残す。
				PMString msg(wasCancelled ? "refresh cancelled." : "refresh: no comparable pages.");
				msg.SetTranslatable(kFalse);
				KESCMSetStatus(msg);
			}
			break;
		}

		// フライアウトの「Save Check & Register」: Start中の Target/Source の現在の Check(✓)+ Register
		// (Added/Removed)を独自 JSON(KESCM\KESCMPageChecks.json, v2)へマージ保存し、保存先パスをステータス行に
		// 出す(実体 KESCMPageCheck.cpp)。
		case kKESCMPopupSaveChecksActionID:
			Utils<IKESCMPageFlagsFacade>()->SaveChecksAndRegister();
			break;

		// フライアウトの「Load Check & Register」: 上記 JSON から Register を両文書へ適用→再比較→
		// Check(今もマーク付きのページだけ)を復元する(実体 KESCMPageCheck.cpp)。
		// ⚠★**メニュー項目としては常に押せる**(2026-08-18・不具合再検査 B-U3 で訂正。旧「Start中だけ
		//   有効」はメニューの有効/無効の話に読めるが、下の UpdateActionStates にこの ActionID の分岐は
		//   無い)。`.fr` の ActionDef が kCustomEnabling を付けず kDisableIfLowMem だけにしてあり、
		//   そこに "plain command; guards inside (needs Start)" と書いてある＝**意味を持つのが Start 中
		//   だけで、断るのは実体の側**。Save Check & Register も同じ作り。
		case kKESCMPopupLoadChecksActionID:
			Utils<IKESCMPageFlagsFacade>()->LoadChecksAndRegister();
			break;

		// フライアウトの「Export Changed Pages...」: 現在の比較(Start 後)の変更ページ一覧を
		// TSV(新ページ/旧ページ/種別=変更/挿入/削除)で保存する(実体 KESCMChangedPagesTSV.cpp)。
		// 比較中(sDB≠nil)のみ有効。オーバーセットは含めない。
		case kKESCMPopupExportChangedPagesActionID:
			{
				// ★2026-08-13(Task 9): 書き出し本体は model 側で、**メッセージは戻り値で受けて
				//   ここ(UI)が出す**。成功時は無言＝空で返るので、そのときは何も出さない。
				PMString exportMsg;
				Utils<IKESCMCompareFacade>()->ExportChangedPagesTSV(exportMsg);
				if (exportMsg.CharCount() > 0)
					KESCMSetStatus(exportMsg);
			}
			break;

		// フライアウトの「Compare Books」: ブックパネルで前面タブのブック=Target、それ以外で最初に
		// 開いているブック=Source として、章(ドキュメント)単位で「変更あり/なし」を判定する。
		// ★既存の文書比較(Start)とは完全に独立=arm しない・枠を作らない・sDB/sEntries を触らない。
		// ⚠2026-08-16(監査 B-U3)に**陳腐化を1行削った**＝旧「段階1 の途中: いまは解決した2ブックの
		//   名前をステータスに出すだけ(比較の実体は次の段階)」。**比較も結果ダイアログも章行の右クリックも
		//   完成している**(2026-08-16 の監査 B8 で公式ルートへの照合と実機 PASS まで済み)。
		//   ★★すぐ下の3行が「2026-08-12 に流れが変わった」と新しい姿を説明しており、**新旧が同居**していた。
		case kKESCMPopupCompareBooksActionID:
			// ★★2026-08-12 に流れが変わった(ユーザー指示)。**確認アラート → OK で比較 → 結果ダイアログ**。
			//   旧: ダイアログを先に開き、中の Compare ボタンで実行(そのボタンは撤去済み)。
			//   ⚠**対象2ブックを押す前に見せる**という眼目は変わっていない——見せる場所がダイアログの
			//     2行からアラートの本文へ移り、名前ではなく**フルパス**になった(同名のブックが多いため)。
			KESCMRunBookComparison();
			break;

		// ブック比較ダイアログの**章行の右クリック**「Start Change Marker」(2026-08-12)。
		// その章の Target/Source 2文書を窓付きで開き、比較中なら一度 Stop してから比較を開始する。
		// ★どの行かは右クリックの時点で KESCMBookSetMenuRow が控えている——アクションには ActionID
		//   しか渡らないので、これが「どの章の話か」を知る唯一の手段(KBS の結果行と同じ作り)。
		case kKESCMBookRowStartActionID:
			KESCMBookStartComparisonForRow(KESCMBookMenuRow());
			break;

		// Story Edits の**行の右クリック**「Refresh Story Comparison」(2026-08-21)。その行の
		// ストーリーだけ本文差分を取り直し、子の変更箇所を今の状態に置き換える。
		// ★どの行かは章行と同じ作り＝右クリックの時点で KESCMStorySetMenuRow が控えている。
		// ★直し終えて差分が0件になっても**行は残り子だけ消える**(ユーザー判断 2026-08-21)。
		//   結果はステータス行に出る＝「何も起きなかったのか、差分が無くなったのか」を言い分ける。
		case kKESCMStoryRowRefreshActionID:
			KESCMStoryRefreshMenuRow();
			break;

		default:
			break;
	}
}

/* UpdateActionStates — チェック式トグルは ON なら kSelectedAction を立てる(docwatch の
   DocWchActionComponent::UpdateActionStates と同じ流儀)。条件付きの有効/無効・動的ラベルもここで返す:
   Start/Stop(名前の出し分け＋Start は文書2つ未満で灰色)・Hide Unchanged Spreads(Start 中のみ有効。
   2026-08-06 変更)・Find Overset(走査対象文書が無ければ灰色。ON 中は常に有効)・Refresh Overset/
   Export 等の条件付き有効化。ページパネル右クリックの「KCM: Register as Added/Removed Pages」だけは
   選択依存の有効/無効・中間チェック・動的ラベルがあるため、model へ「今どう見えるべきか」を聞いてから
   メニューへ書き込む(IKESCMPageFlagsFacade::GetRegisterToggleState → KESCMPageMapGetToggleState /
   KESCMPageMap.cpp)。⚠2026-08-17 訂正: ここは旧名 KESCMPageMapUpdateToggleState のうえ「委譲する」と
   書いていた ---- 2026-08-15 の API 監査 B2(A-2)で **SetNthActionState/SetNthActionName を呼ぶのは
   この UI 側**になり、model は答えるだけになっている。 */
void KESCMActionComponent::UpdateActionStates(IActiveContext* /*ac*/, IActionStateList* listToUpdate, GSysPoint /*mousePoint*/, IPMUnknown* /*widget*/)
{
	for (int32 i = 0; i < listToUpdate->Length(); i++)
	{
		const ActionID action = listToUpdate->GetNthAction(i);
		if (action == kKESCMPopupStartStopActionID)
		{
			// arm 状態でメニュー名を出し分け(arm 中=Stop / 未 arm=Start)。
			// (kSelectedAction は付けない=チェックマークではなく名前そのものを切り替える。)
			// ★3回聞くので InterfacePtr で1回引く(`Utils.h:74-80`。2026-08-16・監査 B-U3 で
			//   このファイルの他の分岐と揃えた ---- :141 / :180 / :363 は既にこの形だった)。
			InterfacePtr<IKESCMCompareFacade> compare(Utils<IKESCMCompareFacade>().QueryUtilInterface());
			const bool16 armed = compare->IsArmed() && (compare->GetArmedTargetDB() != nil);
			PMString name(armed ? "Stop" : "Start");
			name.SetTranslatable(kFalse);
			listToUpdate->SetNthActionName(i, name);
			// ★Stop は常に有効: 文書が1つも開いていなくてもマーク消去・peek 解除は成立させる
			//   (KESCMToggleStartStop の解除分岐が「実際にマークが描かれていた文書」を自分で控える)。
			//   Start は Target と Source の2文書が要るので、揃っていなければ灰色にする
			//   (2026-08-06 ユーザー指定)。判定は実行側と同じ CanStartComparison() を通るので、
			//   メニューの見た目と押した結果がずれない。
			listToUpdate->SetNthActionState(i,
				(armed || compare->CanStartComparison()) ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKESCMPopupPrintMarksActionID)
		{
			int16 actionState = kEnabledAction;
			if (Utils<IKESCMCompareFacade>()->GetPrintMarks())
				actionState |= kSelectedAction;	// ON ならチェックマーク
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupOpacity25ActionID)
		{
			// ラジオ風: 現在 25% ならこの項目に✓(75% と相互排他)。
			int16 actionState = kEnabledAction;
			if (Utils<IKESCMCompareFacade>()->GetMarkOpacity25())
				actionState |= kSelectedAction;
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupOpacity75ActionID)
		{
			// ラジオ風: 現在 75%(=!25%)ならこの項目に✓。
			int16 actionState = kEnabledAction;
			if (!Utils<IKESCMCompareFacade>()->GetMarkOpacity25())
				actionState |= kSelectedAction;
			listToUpdate->SetNthActionState(i, actionState);
		}
		// ★「Mark colour」の2項目(2026-08-24)。上の Marks opacity と同じラジオ風＝いま効いている方に✓。
		//   **どちらも常に有効**＝比較していないときでも選べる(次の Start にも効く)。
		else if (action == kKESCMPopupColorRedActionID)
		{
			int16 actionState = kEnabledAction;
			if (!Utils<IKESCMCompareFacade>()->GetMarkColorCyan())
				actionState |= kSelectedAction;		// 赤(既定)ならこちらに✓
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupColorCyanActionID)
		{
			int16 actionState = kEnabledAction;
			if (Utils<IKESCMCompareFacade>()->GetMarkColorCyan())
				actionState |= kSelectedAction;
			listToUpdate->SetNthActionState(i, actionState);
		}
		// ★「Compare mode」の2項目(2026-08-20)。上の Marks opacity と同じラジオ風＝いま効いている
		//   方に✓。**どちらも常に有効**＝比較していないときでも選べる(次の Start に効く)。
		else if (action == kKESCMPopupModePixelActionID)
		{
			int16 actionState = kEnabledAction;
			if (Utils<IKESCMCompareFacade>()->GetCompareMode() == kKESCMModePixel)
				actionState |= kSelectedAction;
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupModeStoryActionID)
		{
			int16 actionState = kEnabledAction;
			if (Utils<IKESCMCompareFacade>()->GetCompareMode() == kKESCMModeStory)
				actionState |= kSelectedAction;
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupHideUnchangedActionID)
		{
			// ★Start 中(arm 済み)でなければ灰色にする(2026-08-06 ユーザー指定)。この機能は比較マーク
			//   (sEntries)を根拠に「変更のないスプレッド」を選ぶので、Start していなければ何も選べない
			//   (DoHideUnchangedToggle 側も同じ理由で先頭にガードがある)。他の実行アクションと同じ
			//   kDisabled_Unselected を使う(Refresh Overset / Export Changed Pages と揃えた)。
			// ★「ON のまま灰色になって戻せない」状態は作れない: Stop(**KESCMDoClearMarks**)が必ず
			//   ResetHideUnchanged(kTrue) を呼び、隠したスプレッドを戻してトグルを OFF にする。
			//   再比較(**KESCMDoMarkChangesDoc**)と文書クローズ(**KESCMHandleDocsClosed**)も同じ。
			// ⚠2026-08-16(監査 B-U3)に**行番号での引用をやめた**——3つとも外れていた
			//   (旧: KESCMCore.cpp:566 / :312 / KESCMPeek.cpp:2267。前2つは無関係な行を指しており、
			//   **KESCMPeek.cpp に至ってはファイルが 906 行しかない**＝EOF の1,300行以上先)。
			//   model/UI 分割でファイルが大きく動いたため。★**関数名で引けば動かない。**
			// ★★2026-08-21(Story 変更モード Task 8): **Story モードでも灰色にする。**
			//   この機能が隠すのは「比較マーク(sEntries)が1ページも無いスプレッド」で、ストーリー差分は
			//   entry を1つも作らない ---- ∴ Story モードで押すと「登録ページや overflow のあるスプレッド
			//   だけ残して、他を全部隠す」になる。
			//   ⚠**実行側の安全網では止まらない**: KESCMHideUnchangedToggle が中止するのは
			//     「sEntries も登録も overflow も**全部**空」のときと「表示中スプレッドを**全部**隠すことに
			//     なる」ときだけで、登録や overflow が1つでもあれば素通りして隠してしまう。⇒ ここで断る。
			//   ★「ON のまま灰色になって戻せない」状態は作れない: モード切替(KESCMApplyCompareMode)は
			//     Start 中なら必ず MarkChanges で全体を比較し直し、その入口の KESCMDoMarkChangesDoc が
			//     KESCMResetHideUnchanged(kTrue) を呼ぶ。Start していなければ IsArmed が偽で元から灰色。
			// ★3回聞くので InterfacePtr で1回引く(Utils.h:74-80。上の Start/Stop 分岐と同じ形)。
			InterfacePtr<IKESCMCompareFacade> compare(Utils<IKESCMCompareFacade>().QueryUtilInterface());
			int16 actionState;
			if (!compare->IsArmed() || compare->GetCompareMode() == kKESCMModeStory)
				actionState = kDisabled_Unselected;
			else
				actionState = compare->GetHideUnchangedOn() ? (kEnabledAction | kSelectedAction) : kEnabledAction;
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupShowOldNumsActionID)
		{
			int16 actionState = kEnabledAction;
			if (Utils<IKESCMCompareFacade>()->GetShowOldPageNumbers())
				actionState |= kSelectedAction;
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupSyncViewsActionID)
		{
			int16 actionState = kEnabledAction;
			if (KESCMGetLayoutSync())
				actionState |= kSelectedAction;
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupScrollMapActionID)
		{
			int16 actionState = kEnabledAction;
			if (KESCMGetScrollMapEnabled())
				actionState |= kSelectedAction;	// ON(既定)ならチェックマーク
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupTranslucentPanelActionID)
		{
			// ★ドッキング中でも選べる(グレーアウトしない)=ユーザー指定 2026-07-29。
			// 押した結果が見えないケースは DoAction 側がステータス文言で伝える。
			int16 actionState = kEnabledAction;
			if (KESCMGetPanelTranslucent())
				actionState |= kSelectedAction;	// ON ならチェックマーク
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupTranslucentPagesActionID)
		{
			// ★上と同じ方針: ページパネルがドッキング中/閉じていても選べる。
			//   (「今フローティングか」で灰色にすると、ドックに入れた瞬間に設定を戻せなくなる。)
			int16 actionState = kEnabledAction;
			if (KESCMGetPagesPanelTranslucent())
				actionState |= kSelectedAction;	// ON ならチェックマーク
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupTranslucentBookDialogActionID)
		{
			// ★上2つと同じ方針: ダイアログが今開いていなくても選べる。閉じている間に決めた設定は
			//   次に開いたときに効く(KESCMBookDialog.cpp が開くたびに貼る)。
			int16 actionState = kEnabledAction;
			if (KESCMGetBookDialogTranslucent())
				actionState |= kSelectedAction;	// ON ならチェックマーク
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupShowSrcMarksActionID)
		{
			int16 actionState = kEnabledAction;
			if (Utils<IKESCMCompareFacade>()->GetShowSourceMarks())
				actionState |= kSelectedAction;
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupShowTgtMarksActionID)
		{
			int16 actionState = kEnabledAction;
			if (Utils<IKESCMCompareFacade>()->GetShowTargetMarks())
				actionState |= kSelectedAction;
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupIgnorePageNumActionID)
		{
			int16 actionState = kEnabledAction;
			if (Utils<IKESCMCompareFacade>()->GetIgnorePageNumberMarker())
				actionState |= kSelectedAction;
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPageMapToggleActionID)
		{
			// ページパネル右クリックの登録トグル: 有効/無効(選択の有無)・チェック(全登録=✓/
			// 一部=中間)・ラベル(Target=Added/Source=Removed)。★**数えるのは model・書くのはここ。**
			// ★★2026-08-15(API 監査 B2 の A-2): model は**答えるだけ**になり、メニューに書き込むのと
			//   ラベルの文字列を持つのはここ(UI)になった。理由＝KESCMPageMap.h の KESCMPageToggleState。
			const KESCMPageToggleState st = Utils<IKESCMPageFlagsFacade>()->GetRegisterToggleState();
			if (!st.fEnabled)
			{
				listToUpdate->SetNthActionState(i, kDisabled_Unselected);
			}
			else
			{
				int16 actionState = kEnabledAction;
				if (st.fTick == kKESCMPageTickAll)
					actionState |= kSelectedAction;			// 全部登録済み=チェック
				else if (st.fTick == kKESCMPageTickSome)
					actionState |= kMultiSelectedAction;	// 一部だけ登録済み=中間チェック
				listToUpdate->SetNthActionState(i, actionState);

				// 動的ラベル(英語固定=パネル UI と同方針)。IActionStateList.h:78 の
				// 「状態でメニュー名を動的に変える」用途そのもの(dynamic menu の仕組みは不要)。
				// ⚠無効のときは名前を触らない ---- 旧実装と同じ挙動(.fr の既定名のまま)。
				PMString name(st.fRole == kKESCMPageRoleSource ? "KCM: Register as Removed Pages"
															   : "KCM: Register as Added Pages");
				name.SetTranslatable(kFalse);
				listToUpdate->SetNthActionName(i, name);
			}
		}
		else if (action == kKESCMPageCheckToggleActionID)
		{
			// ページパネル右クリックの「KCM: Check」トグル: 有効/無効(Start中+Target/Source+選択)と
			// チェック(全部✓/一部=中間)。★登録トグルと同じく、数えるのは model・書くのはここ。
			// ★同上。Check はラベルが固定なので fRole は読まない。
			const KESCMPageToggleState st = Utils<IKESCMPageFlagsFacade>()->GetCheckToggleState();
			if (!st.fEnabled)
			{
				listToUpdate->SetNthActionState(i, kDisabled_Unselected);
			}
			else
			{
				int16 actionState = kEnabledAction;
				if (st.fTick == kKESCMPageTickAll)
					actionState |= kSelectedAction;			// マーク付き選択が全部チェック済み=✓
				else if (st.fTick == kKESCMPageTickSome)
					actionState |= kMultiSelectedAction;	// 一部だけチェック済み=中間チェック
				listToUpdate->SetNthActionState(i, actionState);
			}
		}
		else if (action == kKESCMPageRefreshCompareActionID)
		{
			// ページパネル右クリックの「KCM: Refresh Page Comparison」(トグルではない実行アクション):
			// Start中(arm済み)かつ前面文書が Target/Source のときだけ有効化。それ以外はグレーアウト。
			listToUpdate->SetNthActionState(i, Utils<IKESCMCompareFacade>()->RefreshComparisonAvailable() ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKESCMPopupFindOversetActionID)
		{
			// ★ON の間は常に有効(OFF に戻して十字を消せる必要がある)。OFF のときは走査する文書が
			//   無ければ灰色にする(2026-08-06 ユーザー指定)。対象の決め方は実行側 DoFindOversetToggle
			//   と同じ GetOversetScanTargetDB()＝比較中は Target、未 Start はアクティブ文書。
			//   (従来はここが常に有効で、文書を開かずに押すと "no active document" とだけ出ていた。)
			const bool16 on = Utils<IKESCMMarkData>()->GetOversetOn();
			int16 actionState = (on || Utils<IKESCMCompareFacade>()->GetOversetScanTargetDB() != nil) ? kEnabledAction
			                                                              : kDisabled_Unselected;
			if (on)
				actionState |= kSelectedAction;	// ON ならチェックマーク
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupRefreshOversetActionID)
		{
			// Find Overset が ON のときだけ有効(=再走査可能)。OFF 時は灰色(kDisabled_Unselected)。
			listToUpdate->SetNthActionState(i, Utils<IKESCMMarkData>()->GetOversetOn() ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKESCMPopupExportChangedPagesActionID)
		{
			// 比較中(マークの Target 文書が在る)のみ有効=書き出す変更データが在り得るとき。未 Start は灰色。
			listToUpdate->SetNthActionState(i, (Utils<IKESCMMarkData>()->GetMarkedTargetDB() != nil) ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKESCMPopupCompareBooksActionID)
		{
			// ★実行と同じ解決子を通す=メニューの見た目と押した結果がずれない。有効になるのは
			//   「ブックパネルに前面タブがあり、かつ別のブックも開いている」ときだけ。
			//   ⚠ここはメニューを開くたびに走り、中でパネルを全走査する。フライアウトを開く頻度
			//     でしか呼ばれないので許容している(KBS も同じ走査を同じ場所でしている)。
			//   ★★2026-08-15(第2段 Task 9B): 前面タブの観測が **こちら側(UI)へ移った**。
			//     パネル走査には PaletteRefUtils / IBookUIUtils / IPanelMgr が要り、model プラグイン
			//     はそのどれにも触れない(WidgetBin.lib を外した瞬間にリンカが名指しした)。
			//     ⚠**判定は変わっていない**＝観測に失敗したら Facade を呼ばない。これは以前
			//       model 側の ResolveBookPair が中で kFalse を返していたのと同じ結果。
			IBook* target = nil;
			IBook* source = nil;
			IDFile panelBookFile;
			const bool16 canCompareBooks =
				KESCMGetPanelBookFile(panelBookFile)
				&& Utils<IKESCMBookFacade>()->ResolveBookPair(panelBookFile, target, source);
			listToUpdate->SetNthActionState(i,
				canCompareBooks ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKESCMBookRowStartActionID)
		{
			// ★実行と同じ判定を通す(KESCMBookRowCanStart)=メニューの見た目と押した結果がずれない。
			//   有効になるのは **判定が Changed の行だけ**(ユーザー指定 2026-08-12)で、かつ Target と
			//   Source の両方のファイルを持つ行。∴灰色になるのは片側にしか無い章(ChapterAdded /
			//   ChapterDeleted)とファイルを示さない章に限らず、**NoChange / Failed / NotCompared も灰色**。
			//   ⚠この項目は行メニューの唯一の項目なので、灰色のとき**メニュー自体が出ない**(InDesign の挙動。
			//     それが仕様＝空メニューを出す実装に「改善」しないこと)。
			listToUpdate->SetNthActionState(i, KESCMBookRowCanStart(KESCMBookMenuRow()) ? kEnabledAction
			                                                                            : kDisabled_Unselected);
		}
		else if (action == kKESCMStoryRowRefreshActionID)
		{
			// ★実行と同じ判定をそのまま通す(KESCMStoryRowCanRefresh)＝メニューの見た目と押した
			//   結果がずれない。有効になるのは **Story モードで比較中、かつ相手のあるストーリーの行**
			//   だけ(ユーザー指定 2026-08-21「ストーリーモードでのみで」)。
			//   ⚠この項目も行メニューの唯一の項目なので、灰色のとき**メニュー自体が出ない**——
			//     Pixel モードで右クリックしても何も出ないのは、この一行がそう決めている。
			listToUpdate->SetNthActionState(i, KESCMStoryRowCanRefresh() ? kEnabledAction
			                                                            : kDisabled_Unselected);
		}
	}
}

/* DoAbout */
void KESCMActionComponent::DoAbout()
{
	CAlert::ModalAlert
	(
		// 文字列キーを渡す(CAlert が翻訳する)。★2026-08-06 に UI を英語のみへ戻したので、
		// 実行時の日本語切替(旧 KESCMLoc.h)は撤去した。中身は enUS テーブルの1行=「名前, version x.y.z」。
		PMString(kKESCMAboutBoxStringKey),
		kOKString,					// OK button
		kNullString,				// No second button
		kNullString,				// No third button
		1,							// Set OK button to default
		CAlert::eInformationIcon	// Information icon
	);
}

/* (DoAboutScript は 2026-07-25 撤去=About Scripting 項目削除。スクリプトAPIは元々撤去済みで、
    "No scripts are currently available." を出すだけの項目だった。) */

/* DoUsage — パネルのフライアウト「使い方」。操作リファレンス(=旧パネルの説明文)を表示する。 */
void KESCMActionComponent::DoUsage()
{
	// ★本文は**2本に分けて持ち、ここで連結する**(2026-08-19)。読み手からは1本の文章。
	//   ⚠分けた理由は英語側の都合＝odfrc は StringTable の1文字列に長さ上限があり、ブック比較の節を
	//     足す余地が enUS の kKESCMHintKey に無かった([[odfrc-long-string-limit]])。上限は文字列ごとに
	//     掛かるので、2本に割れば回避できる。**割れ目はブックの節を置きたい位置**(オーバーセットの
	//     検出の前)で決めてあり、2本目は「ブック比較以降の後半すべて」＝免責文もその末尾にある。
	//   ★**Append する前に Translate 済みでなければならない**: KESCMLoc::Text は
	//     「日本語 UI なら日本語リテラル、他は enUS テーブルを引いた結果」を返す**完成テキスト**
	//     (untranslatable)なので、連結しても翻訳キーとして壊れない。**キー同士を連結してはいけない。**
	PMString usage = KESCMLoc::Text(kKESCMHintKey, KESCMJa::kHint);
	usage.Append(KESCMLoc::Text(kKESCMHint2Key, KESCMJa::kHint2));
	usage.SetTranslatable(kFalse);

	CAlert::ModalAlert
	(
		// 完成済みテキスト(キーではない): 日本語 UI なら日本語、他は enUS テーブルの英語。
		// ★使い方の案内は日本語 UI では日本語で出す(2026-08-06 ユーザー指示)。初めて使う人への説明なので、
		//   メニュー/パネル/ステータス行を英語で統一する方針の例外にする(KBS と同じ線引き)。
		usage,
		kOKString,					// OK button
		kNullString,				// No second button
		kNullString,				// No third button
		1,							// Set OK button to default
		CAlert::eInformationIcon	// Information icon
	);
}

// (KESCMDoSplitTarget(Split Target 90/10)は 2026-07-04 撤去。実装全文と実測知見は
//  docs/ai-notes/kescm-split-target-mechanism.md と git 履歴 69c4b07 に保存=他プラグインへの転用候補)


//========================================================================================
// Find Overset(フライアウト): アクティブ1文書を走査し、overset のあるページに十字を出す/消す。
// 比較とは完全に独立。状態は model 側が持ち、ここからは IKESCMMarkData / IKESCMCompareFacade で読み書きする。
//========================================================================================

// (アクティブ文書の解決は KESCMActiveDocDB(KESCMCore)に統合。2026-07-25 重複解消)

/* DoFindOversetToggle — フライアウト「Find Overset」トグル。
   OFF→ON: 走査対象文書(比較中はTarget/それ以外はアクティブ)を走査→ overset を反映。
   ON→OFF: 集合を空にしてトグル OFF、走査していた文書を再描画して目印を消す。 */
void KESCMActionComponent::DoFindOversetToggle()
{
	// ON→OFF: ＋を消す。
	InterfacePtr<IKESCMMarkData> marks(Utils<IKESCMMarkData>().QueryUtilInterface());
	if (marks->GetOversetOn())
	{
		IDataBase* prevDB = marks->GetOversetDB();
		// Pages パネルのサムネイルから＋を消すため、消える前にページ集合を控える。
		std::vector<UID> prevPages;
		marks->GetOversetPageUIDs(prevPages);
		Utils<IKESCMCompareFacade>()->ClearOverset();
		KESCMRefreshThumbnailsForPages(prevDB, prevPages);	// サムネイルを作り直して＋を消す
		// スクロールバー地図: 比較もしていなければ全窓から撤去、比較中なら残して赤帯だけ描き直す。
		if (Utils<IKESCMCompareFacade>()->IsArmed())
			KESCMScrollMapInvalidateAll();
		else
			KESCMScrollMapDetachAll();
		Utils<IKESCMCompareFacade>()->InvalidateDB(prevDB);	// nil 安全(他の呼び出しと同じ)
		KESCMRefreshNavPosition();	// Prev/Next から overset 箇所を外す(比較のみ/対象なしへ)
		PMString msg("Find Overset: off.");
		msg.SetTranslatable(kFalse);
		KESCMSetStatus(msg);
		return;
	}

	// OFF→ON: 走査対象文書(比較中は Target、未 arm はアクティブ)を走査して反映。
	IDataBase* db = Utils<IKESCMCompareFacade>()->GetOversetScanTargetDB();
	if (db == nil)
	{
		PMString msg("Find Overset: no active document.");
		msg.SetTranslatable(kFalse);
		KESCMSetStatus(msg);
		return;
	}
	Utils<IKESCMCompareFacade>()->ApplyOversetForDoc(db);

	// ★トグルが実際に立ったかを聞いてから報告する(2026-08-17)。ApplyOversetForDoc は渡された db が
	//   文書リストに居なければ**何もせず戻る**(閉じた文書のポインタを deref しないための最終ライン防御)
	//   ので、そのときトグルは OFF のまま。以前はここが無条件に "on" と書いていたため、
	//   **OFF なのに「on」と報告し、フライアウトのチェックだけが外れている**状態になり得た。
	//   ⚠到達条件は稀＝GetOversetScanTargetDB() が返した db がその直後に死んでいる場合(クローズの生存
	//   スイープ漏れ)。稀でも「表示と実態が食い違う」形なので、状態を読み直して答える。
	if (!Utils<IKESCMMarkData>()->GetOversetOn())
	{
		PMString msg("Find Overset: document is gone.");
		msg.SetTranslatable(kFalse);
		KESCMSetStatus(msg);
		return;
	}

	PMString msg("Find Overset: on (");
	msg.SetTranslatable(kFalse);
	msg.AppendNumber(Utils<IKESCMMarkData>()->GetOversetPageCount());
	msg.Append(" page(s)).");
	KESCMSetStatus(msg);
}

/* DoRefreshOverset — フライアウト「Refresh Overset」。Find Overset が ON のときだけ有効(OFF時は
   UpdateActionStates で灰色)。アクティブ文書を再走査して集合を貼り直す。文書が切り替わっていたら
   前の文書の十字も消す。 */
void KESCMActionComponent::DoRefreshOverset()
{
	if (!Utils<IKESCMMarkData>()->GetOversetOn())
		return;	// OFF時は無効(保険。通常はメニューが灰色で呼ばれない)

	IDataBase* db = Utils<IKESCMCompareFacade>()->GetOversetScanTargetDB();
	if (db == nil)
	{
		PMString msg("Refresh Overset: no active document.");
		msg.SetTranslatable(kFalse);
		KESCMSetStatus(msg);
		return;
	}
	Utils<IKESCMCompareFacade>()->ApplyOversetForDoc(db);	// 再走査・反映(別文書なら前の文書の目印も消す)は共有処理に集約

	// ★こちらに上の ON 経路と同じ読み直しは要らない(2026-08-17 に数えて確認)。Apply が db の死亡で早期
	//   return しても**トグルは既に ON** なので「on と言いながら OFF」にはならず、前回の件数がそのまま
	//   出る＝「再走査したが増減が無かった」と見分けが付かないだけ。∴ 実行文は足さない。
	PMString msg("Refresh Overset: ");
	msg.SetTranslatable(kFalse);
	msg.AppendNumber(Utils<IKESCMMarkData>()->GetOversetPageCount());
	msg.Append(" page(s).");
	KESCMSetStatus(msg);
}

// KESCMOpenAboutURL(KESCMUIShared.h で宣言) — パネルのイラストクリックから呼ばれる。「このプラグインに
// ついて」本文と同じ配布元URL(kKESCMRepoURL)を既定のブラウザで開く。ドキュメントモデルには一切
// 触れない(=OSへの外部起動要求のみ)ため、Command 化は不要。
// GoToURLUtils::GoToURL は IURLAccess(hyperlink 用の内部インターフェイス)経由で Win/Mac 双方の既定
// ブラウザを起動する InDesign 純正のユーティリティ関数(PUBLIC_DECL、boss/IID 取得不要)。
// isAGoURL=kFalse は Adobe の "go.adobe.com" 短縮リンク専用フラグで、通常の外部URLでは使わない。
void KESCMOpenAboutURL()
{
	PMString url(kKESCMRepoURL);
	url.SetTranslatable(kFalse);
	GoToURLUtils::GoToURL(url, kFalse);
}

// KESCMActionComponent.cpp 終わり。
