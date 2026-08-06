//========================================================================================
//
//  KESCMActionComponent.cpp
//
//  プラグインのメニューアクションを処理する: 「プラグインについて」エントリと、パネルのフライアウトの
//  「このプラグインについて」エントリ。BasicPanel サンプル(BscPnlActionComponent.cpp)を手本にしている。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// 一般:
#include "CActionComponent.h"
#include "CAlert.h"
#include "IActionStateList.h"		// UpdateActionStates(チェックマーク表示)。IPMUnknown 派生ではない
#include "PMString.h"

// Hide Unchanged Spreads(kHideSpreadCmdBoss)用:
#include "CmdUtils.h"				// CreateCommand/ProcessCommand(モデル変更は必ず Command 経由)
#include "ICommand.h"
#include "IBoolData.h"				// kHideSpreadCmdBoss は専用 CmdData を持たず汎用 IBoolData で方向指定(kLockLayerCmdBoss と同型)
#include "UIDList.h"
#include "ISpread.h"
#include "ISpreadList.h"
#include "SpreadID.h"				// kHideSpreadCmdBoss / IID_IHIDESPREADBOOLDATA
#include "ISession.h"				// GetExecutionContextSession(リセット時の文書生存確認)
#include "IApplication.h"
#include "IDocumentList.h"			// FindDocByDataBase(閉じた db を deref しないための生存確認)
#include <vector>
#include <map>
#include <set>

#include "IDataBase.h"				// GetRootUID()(Hide Unchanged のスプレッド走査)

// (Split Target(90/10)機能は 2026-07-04 撤去。専用 include 群も削除。
//  仕組みは docs/ai-notes/kescm-split-target-mechanism.md と git 履歴 69c4b07 に保存)

// プロジェクト内:
#include "KESCMID.h"
#include "KESCMLoc.h"		// 実行時の日本語切替(How to Use と Hide Unchanged の確認の2箇所だけ)
#include "KESCMCore.h"		// KESCMOpenAboutURL
#include "KESCMDrawEventHandler.h"	// sEntries/sDB/sShowOldNumbers(Hide Unchanged と旧番号バッジの状態参照)
#include "KESCMPageMap.h"	// KESCMPageMapToggleSelectedPages / KESCMPageMapUpdateToggleState(追加/削除ページ登録トグル)
							// ＋ KESCMBuildPairing(除外対応表、Hide Unchanged の Source 側分類で使用)
#include "KESCMPageCheck.h"	// KESCMPageCheckToggleSelectedPages / KESCMPageCheckUpdateToggleState(「KESCM: Check」の✓トグル)
#include "KESCMPageNumberMarker.h"	// KESCMGetIgnorePageNumberMarker/KESCMSetIgnorePageNumberMarker(ノンブル除外トグル)
#include "KESCMThumbnailRefresh.h"	// KESCMTryRefreshPagesPanelThumbnails(Source サムネイルの枠を即 ON/OFF)
#include "KESCMPeek.h"				// KESCMBaseScreenOpacity(Hold to Hide Marks 切替時に常時表示の基準不透明度を反映)
#include "KESCMScrollMap.h"		// KESCMScrollMapInvalidateAll(Hide Unchanged 切替後に地図を描き直す)
#include "KESCMPanelState.h"		// KESCMSavePanelState(フライアウト「Save Panel Settings」)
#include "KESCMOversetScan.h"		// KESCMCollectOversetLocations(Find Overset の検出=アクティブ文書走査)
#include "KESCMChangedPagesTSV.h"	// KESCMExportChangedPagesTSV(フライアウト「Export Changed Pages...」)
#include "KESCMChangeNav.h"			// KESCMRefreshNavPosition(overset トグルで Prev/Next の対象数を更新)
#include "KESCMPanelAlpha.h"		// KESCMGetPanelTranslucent/Set/Apply(フライアウト「Translucent Panel」)
#include "IActiveContext.h"			// GetContextDocument(アクティブ文書の解決)
#include "IDocument.h"
#include "PersistUtils.h"			// ::GetUIDRef(doc)(アクティブ文書 → db)

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

// 「Hide Unchanged Spreads」トグルの状態。ON の間、sHiddenSpreads = 自分が隠したスプレッド UID の控え
// (OFF でこれ「だけ」を再表示する。ユーザーがページパネル等で別途隠したスプレッドは巻き込まない)。
// sHiddenDB は隠した先の文書。他の static と同じくセッション内のみ保持(kHideSpreadCmdBoss は永続変更
// なので、隠し状態そのものは .indd に残る=dirty 化はユーザー了承済みの仕様)。
// Source 側も同じ分類で自動的に隠す(sHiddenSrcDB / sHiddenSrcSpreads に別控え。Source も dirty になる)。
static bool16 sHideUnchangedOn = kFalse;
static std::vector<UID> sHiddenSpreads;
static IDataBase* sHiddenDB = nil;
static std::vector<UID> sHiddenSrcSpreads;
static IDataBase* sHiddenSrcDB = nil;

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
	void DoHideUnchangedToggle();
	void DoFindOversetToggle();		// フライアウト「Find Overset」: アクティブ文書を走査して十字表示/消去(トグル)
	void DoRefreshOverset();		// フライアウト「Refresh Overset」: ON時のみ・アクティブ文書を再走査
};

/* Binds the C++ implementation class onto its ImplementationID. */
CREATE_PMINTERFACE(KESCMActionComponent, kKESCMActionComponentImpl)

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
		// 実体は KESCMPanelObserver.cpp の自由関数(arm 状態を見て開始 or 解除、実行後にパネル更新)。
		case kKESCMPopupStartStopActionID:
			KESCMToggleStartStop();
			break;

		// フライアウトの「Print comparison marks」: 印刷マーク ON/OFF トグル(旧パネルのチェックボックス
		// →2026-07-10 メニュー化)。実体は KESCMPanelObserver.cpp の自由関数。
		case kKESCMPopupPrintMarksActionID:
			KESCMTogglePrintMarks();
			break;

		// フライアウトの「Marks opacity 25% / 75%」(ラジオ風): 選んだ方の不透明度に設定する。
		// 実体は KESCMPanelObserver.cpp の自由関数(印刷フラグは維持し不透明度だけ変更)。
		case kKESCMPopupOpacity25ActionID:
			KESCMSetMarkOpacity25(kTrue);
			break;
		case kKESCMPopupOpacity75ActionID:
			KESCMSetMarkOpacity25(kFalse);
			break;

		// (kKESCMPopupAboutScriptActionID / DoAboutScript は 2026-07-25 撤去=About Scripting 項目削除。)

		case kKESCMPopupUsageActionID:
			this->DoUsage();
			break;

		// 「Show Marks on Source」トグル: フラグを反転して Source 文書を再描画するだけ(表示判定と描画は
		// KESCMDrawEventHandler::HandleDrawEvent の Source 分岐。ON の間は常時表示・OPPでも表示・印刷にも
		// 出る。不透明度はパネルの 25%/75% 選択に連動)。Start のたびに既定 ON へ戻る(KESCMDoMarkChangesDoc)。
		case kKESCMPopupShowSrcMarksActionID:
		{
			KESCMDrawEventHandler::sSrcMarksOn = !KESCMDrawEventHandler::sSrcMarksOn;
			KESCMInvalidateDB(KESCMDrawEventHandler::sSrcDB);
			// ★レイアウトビューだけでなく Pages パネルの Source サムネイルも即時更新する。Source 側の枠は
			//   wantSrcMarks(=sSrcMarksOn)に依存し、サムネイル(isThumb)でも強制表示されないため、トグルで
			//   サムネイルを作り直さないと OFF にしても枠が残る/ON にしても出ない。対象ページは Source の
			//   変更/overflow/登録集合(KESCMCollectChangedPageUIDs が引く)で、枠が出得るページと一致する。
			KESCMTryRefreshPagesPanelThumbnails(KESCMDrawEventHandler::sSrcDB);
			PMString msg(KESCMDrawEventHandler::sSrcMarksOn ? "Source marks: on." : "Source marks: off.");
			msg.SetTranslatable(kFalse);
			KESCMSetStatus(msg);
			break;
		}

		// 「Hide Unchanged Spreads」トグル: OFF→ON は確認ダイアログ→変更なしスプレッドを隠す。
		// ON→OFF は自分が隠した分だけ再表示。本体は DoHideUnchangedToggle。
		case kKESCMPopupHideUnchangedActionID:
			this->DoHideUnchangedToggle();
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
			KESCMDrawEventHandler::sShowOldNumbers = !KESCMDrawEventHandler::sShowOldNumbers;
			KESCMInvalidateDB(KESCMDrawEventHandler::sDB);
			if (KESCMArmedSourceDB() != KESCMDrawEventHandler::sDB)
				KESCMInvalidateDB(KESCMArmedSourceDB());
			PMString msg(KESCMDrawEventHandler::sShowOldNumbers ? "Show original page numbers: on." : "Show original page numbers: off.");
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
		// (OFF でも効く)。Start 中はページの Add/Remove 補正あり。実体は KESCMPeek.cpp の
		// KESCMAlignOtherViewsToActiveNow(トグル ON 時の初回そろえと同じ同期エンジン)。
		case kKESCMPopupAlignViewsActionID:
		{
			const bool16 ok = KESCMAlignOtherViewsToActiveNow();
			// false は2通り: (a) 最前面レイアウトビューが無い / (b) Start 中で最前面が Target/Source 以外の
			// 第3文書(engine が同期しない)。どちらも「実際にそろえていない」ので成功表示は出さない。
			PMString msg(ok ? "Aligned other views to the active view."
			                : "Align: no view to align from (while Started, use the Target or Source view).");
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
				if (KESCMDrawEventHandler::sDB    != nil) KESCMScrollMapAttach(KESCMDrawEventHandler::sDB);
				if (KESCMDrawEventHandler::sSrcDB != nil) KESCMScrollMapAttach(KESCMDrawEventHandler::sSrcDB);
				// Find Overset 単独で ON 中なら、その走査文書窓にも地図を復帰させる(2026-07-24)。
				if (KESCMDrawEventHandler::sOversetOn && KESCMDrawEventHandler::sOversetDB != nil)
					KESCMScrollMapAttach(KESCMDrawEventHandler::sOversetDB);
				KESCMScrollMapInvalidateAll();
			}
			else
				KESCMScrollMapDetachAll();	// 既存 strip を全窓から撤去
			PMString msg(on ? "Scrollbar map: on." : "Scrollbar map: off.");
			msg.SetTranslatable(kFalse);
			KESCMSetStatus(msg);
			break;
		}

		// (★「Show HUD」トグルは 2026-08-06 に機能ごと全廃した。ユーザー指示で「押下中にレイアウト
		//  ビュー左上へ Target/Source の1行を出す」機能そのものを無くしたため。)

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

		// 「Hold to Hide Marks」トグル: 枠表示の極性反転(フラグ反転のみ)。ON=画面に枠を常時表示し、
		// ツール左hold中だけ隠す(押下/解放は KESCMPeek.cpp のトラッカー(KESCMTrackerRevealBegin/End)が sMarksTempHidden を上下)。
		// OFF=従来(既定非表示・押下中だけ表示)。画面のみ=印刷は Print comparison marks が別管理。
		// 切替時に一時退避を解除し、常時表示の基準不透明度(常時表示ON中は25%/75%)を反映して sDB を再描画。
		case kKESCMPopupHoldToHideMarksActionID:
		{
			KESCMDrawEventHandler::sAlwaysShowMarks = !KESCMDrawEventHandler::sAlwaysShowMarks;
			KESCMDrawEventHandler::sMarksTempHidden = kFalse;	// モード切替時は一時退避を解除
			KESCMDrawEventHandler::sMarkScreenOpacity = KESCMBaseScreenOpacity();	// 常時表示の不透明度を即反映
			KESCMInvalidateDB(KESCMDrawEventHandler::sDB);
			PMString msg(KESCMDrawEventHandler::sAlwaysShowMarks ? "Hold to Hide Marks: on." : "Hold to Hide Marks: off.");
			msg.SetTranslatable(kFalse);
			KESCMSetStatus(msg);
			break;
		}

		// 「Ignore Page Number Marker」トグル: ノンブル(自動ページ番号)マーカーを含むフレームを
		// 比較(CMYKピクセル差分)から除外するか(既定ON)。フラグを反転し、既にStart済みなら
		// 登録トグルと同じ理由で全体再比較して即座に反映する。
		case kKESCMPopupIgnorePageNumActionID:
		{
			KESCMSetIgnorePageNumberMarker(!KESCMGetIgnorePageNumberMarker());
			PMString msg(KESCMGetIgnorePageNumberMarker() ? "Ignore page number marker: on." : "Ignore page number marker: off.");
			msg.SetTranslatable(kFalse);
			if (KESCMDrawEventHandler::sDB != nil && KESCMDrawEventHandler::sSrcDB != nil)
			{
				PMString report;
				// ★ここは allowIncremental を渡していない=全ページ再比較なので、ページ数が多ければ
				//   進捗バーに Cancel が出る。キャンセルされると KESCMDoMarkChangesDoc 側でマークが
				//   全部破棄され kFailure が返る。戻り値を捨てると arm だけが残って「枠が1つも無い
				//   Start 中」になるので、Start 経路(KESCMToggleStartStop)と同じ考え方で Stop まで戻す
				//   (KESCMToggleStartStop は arm 中に呼べば Stop 分岐に入る)。2026-07-29 の自己レビューで発見。
				if (KESCMDoMarkChangesDoc(KESCMDrawEventHandler::sDB, KESCMDrawEventHandler::sSrcDB, report) == kSuccess)
				{
					msg.Append(" (recompared)");
				}
				else
				{
					KESCMToggleStartStop();		// マーク破棄済み → strip 撤去・disarm まで揃えて Stop へ
					msg.Append(" (cancelled - stopped)");
				}
			}
			KESCMSetStatus(msg);
			break;
		}

		// フライアウトの「Save Panel Settings」: 現在の設定系トグルを独自 JSON でローカルへ保存し、
		// 保存先パスをダイアログ表示する(実体は KESCMPanelState.cpp)。読み込みはパネル初回オープン時。
		case kKESCMPopupSavePanelStateActionID:
			KESCMSavePanelState();
			break;

		// ページパネルのページ右クリック「KESCM: Register as Added/Removed Pages」トグル。
		// 選択ページを「比較相手なし」として登録/解除する(実体は KESCMPageMap.cpp。このステップでは
		// 登録の保持とチェック表示まで。比較の除外対応表への反映は次ステップ)。
		case kKESCMPageMapToggleActionID:
			KESCMPageMapToggleSelectedPages();
			break;

		// ページパネルのページ右クリック「KESCM: Check」トグル。選択ページに✓印を付け外しする
		// (実体は KESCMPageCheck.cpp。✓の描画は KESCMDrawEventHandler の isThumb 分岐)。
		case kKESCMPageCheckToggleActionID:
			KESCMPageCheckToggleSelectedPages();
			break;

		// ページパネルのページ右クリック「KESCM: Refresh Page Comparison」(実行アクション)。選択ページの
		// 比較を再検出して枠/サムネイルを更新する(旧 Ctrl+ミドルのスプレッド再比較を移設。2026-07-13)。
		// 実体は KESCMPeek.cpp。結果をステータス行に短く出す。
		case kKESCMPageRefreshCompareActionID:
		{
			int32 nPages = 0, nChanged = 0;
			bool16 wasCancelled = kFalse;
			if (KESCMRefreshComparisonForSelectedPages(&nPages, &nChanged, &wasCancelled))
			{
				PMString msg("refreshed ");
				msg.SetTranslatable(kFalse);
				msg.AppendNumber(nPages);
				msg.Append(" (changed ");
				msg.AppendNumber(nChanged);
				msg.Append(")");
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
			KESCMPageCheckSaveToFile();
			break;

		// フライアウトの「Load Check & Register」: Start中だけ有効。上記 JSON から Register を両文書へ適用→再比較→
		// Check(今もマーク付きのページだけ)を復元する(実体 KESCMPageCheck.cpp)。
		case kKESCMPopupLoadChecksActionID:
			KESCMPageCheckLoadFromFile();
			break;

		// フライアウトの「Export Changed Pages...」: 現在の比較(Start 後)の変更ページ一覧を
		// TSV(新ページ/旧ページ/種別=変更/挿入/削除)で保存する(実体 KESCMChangedPagesTSV.cpp)。
		// 比較中(sDB≠nil)のみ有効。オーバーセットは含めない。
		case kKESCMPopupExportChangedPagesActionID:
			KESCMExportChangedPagesTSV();
			break;

		default:
			break;
	}
}

/* UpdateActionStates — フライアウトのチェック式トグル5つ(「Show Marks on Source」「Hide Unchanged
   Spreads」「Show Original Page Numbers」「Sync Layout Views」「Ignore Page Number Marker」)は
   常に有効、ON なら kSelectedAction を
   立てる(docwatch の DocWchActionComponent::UpdateActionStates と同じ流儀)。ページパネル右クリックの
   「KESCM: Register as Added/Removed Pages」だけは選択依存の有効/無効・中間チェック・動的ラベルが
   あるため KESCMPageMapUpdateToggleState(KESCMPageMap.cpp)へ委譲する。 */
void KESCMActionComponent::UpdateActionStates(IActiveContext* /*ac*/, IActionStateList* listToUpdate, GSysPoint /*mousePoint*/, IPMUnknown* /*widget*/)
{
	for (int32 i = 0; i < listToUpdate->Length(); i++)
	{
		const ActionID action = listToUpdate->GetNthAction(i);
		if (action == kKESCMPopupStartStopActionID)
		{
			// arm 状態でメニュー名を出し分け(arm 中=Stop / 未 arm=Start)。常に有効。
			// (kSelectedAction は付けない=チェックマークではなく名前そのものを切り替える。)
			const bool16 armed = KESCMIsArmed() && (KESCMArmedTargetDB() != nil);
			PMString name(armed ? "Stop" : "Start");
			name.SetTranslatable(kFalse);
			listToUpdate->SetNthActionName(i, name);
			listToUpdate->SetNthActionState(i, kEnabledAction);
		}
		else if (action == kKESCMPopupPrintMarksActionID)
		{
			int16 actionState = kEnabledAction;
			if (KESCMGetPrintMarks())
				actionState |= kSelectedAction;	// ON ならチェックマーク
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupOpacity25ActionID)
		{
			// ラジオ風: 現在 25% ならこの項目に✓(75% と相互排他)。
			int16 actionState = kEnabledAction;
			if (KESCMGetMarkOpacity25())
				actionState |= kSelectedAction;
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupOpacity75ActionID)
		{
			// ラジオ風: 現在 75%(=!25%)ならこの項目に✓。
			int16 actionState = kEnabledAction;
			if (!KESCMGetMarkOpacity25())
				actionState |= kSelectedAction;
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupHideUnchangedActionID)
		{
			// ★Start 中(arm 済み)でなければ灰色にする(2026-08-06 ユーザー指定)。この機能は比較マーク
			//   (sEntries)を根拠に「変更のないスプレッド」を選ぶので、Start していなければ何も選べない
			//   (DoHideUnchangedToggle 側も同じ理由で先頭にガードがある)。他の実行アクションと同じ
			//   kDisabled_Unselected を使う(Refresh Overset / Export Changed Pages と揃えた)。
			// ★「ON のまま灰色になって戻せない」状態は作れない: Stop(KESCMDoClearMarks)が必ず
			//   KESCMResetHideUnchanged(kTrue) を呼び、隠したスプレッドを戻してトグルを OFF にする
			//   (KESCMCore.cpp:566)。再比較・文書クローズも同じ(KESCMCore.cpp:312 / KESCMPeek.cpp:2267)。
			int16 actionState;
			if (!KESCMIsArmed())
				actionState = kDisabled_Unselected;
			else
				actionState = sHideUnchangedOn ? (kEnabledAction | kSelectedAction) : kEnabledAction;
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupShowOldNumsActionID)
		{
			int16 actionState = kEnabledAction;
			if (KESCMDrawEventHandler::sShowOldNumbers)
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
		else if (action == kKESCMPopupHoldToHideMarksActionID)
		{
			int16 actionState = kEnabledAction;
			if (KESCMDrawEventHandler::sAlwaysShowMarks)
				actionState |= kSelectedAction;	// ON ならチェックマーク
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupShowSrcMarksActionID)
		{
			int16 actionState = kEnabledAction;
			if (KESCMDrawEventHandler::sSrcMarksOn)
				actionState |= kSelectedAction;
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupIgnorePageNumActionID)
		{
			int16 actionState = kEnabledAction;
			if (KESCMGetIgnorePageNumberMarker())
				actionState |= kSelectedAction;
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPageMapToggleActionID)
		{
			// ページパネル右クリックの登録トグル: 有効/無効(選択の有無)・チェック(全登録=✓/
			// 一部=中間)・ラベル(Target=Added/Source=Removed)をまとめて KESCMPageMap.cpp 側で設定。
			KESCMPageMapUpdateToggleState(listToUpdate, i);
		}
		else if (action == kKESCMPageCheckToggleActionID)
		{
			// ページパネル右クリックの「KESCM: Check」トグル: 有効/無効(Start中+Target/Source+選択)と
			// チェック(全部✓/一部=中間)を KESCMPageCheck.cpp 側で設定。
			KESCMPageCheckUpdateToggleState(listToUpdate, i);
		}
		else if (action == kKESCMPageRefreshCompareActionID)
		{
			// ページパネル右クリックの「KESCM: Refresh Page Comparison」(トグルではない実行アクション):
			// Start中(arm済み)かつ前面文書が Target/Source のときだけ有効化。それ以外はグレーアウト。
			listToUpdate->SetNthActionState(i, KESCMRefreshComparisonAvailable() ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKESCMPopupFindOversetActionID)
		{
			int16 actionState = kEnabledAction;
			if (KESCMDrawEventHandler::sOversetOn)
				actionState |= kSelectedAction;	// ON ならチェックマーク
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKESCMPopupRefreshOversetActionID)
		{
			// Find Overset が ON のときだけ有効(=再走査可能)。OFF 時は灰色(kDisabled_Unselected)。
			listToUpdate->SetNthActionState(i, KESCMDrawEventHandler::sOversetOn ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKESCMPopupExportChangedPagesActionID)
		{
			// 比較中(sDB≠nil)のみ有効=書き出す変更データが在り得るとき。未 Start は灰色。
			listToUpdate->SetNthActionState(i, (KESCMDrawEventHandler::sDB != nil) ? kEnabledAction : kDisabled_Unselected);
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
	CAlert::ModalAlert
	(
		// 完成済みテキスト(キーではない): 日本語 UI なら日本語、他は enUS テーブルの英語。
		// ★使い方の案内は日本語 UI では日本語で出す(2026-08-06 ユーザー指示)。初めて使う人への説明なので、
		//   メニュー/パネル/ステータス行を英語で統一する方針の例外にする(KBS と同じ線引き)。
		KESCMLoc::Text(kKESCMHintKey, KESCMJa::kHint),
		kOKString,					// OK button
		kNullString,				// No second button
		kNullString,				// No third button
		1,							// Set OK button to default
		CAlert::eInformationIcon	// Information icon
	);
}

//========================================================================================
// Hide Unchanged Spreads(フライアウトのチェック式トグル)
//========================================================================================

// 状態メッセージをパネルのステータス行へ(既存の Split Target と同じ英語・非翻訳の流儀)。
static void KESCMHideStatus(const char* text)
{
	PMString msg(text);
	msg.SetTranslatable(kFalse);
	KESCMSetStatus(msg);
}

// uids を1つの kHideSpreadCmdBoss で隠す/再表示する。hide=kTrue で隠す。
// ★IBoolData の方向は kTrue=隠す(2026-07-04 実機確認済み。kLockLayerCmdBoss と同型)。
static ErrorCode KESCMProcessHideSpreadCmd(IDataBase* db, const std::vector<UID>& uids, bool16 hide)
{
	if (db == nil || uids.empty())
		return kFailure;

	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kHideSpreadCmdBoss));
	if (cmd == nil)
		return kFailure;

	UIDList list(db);
	for (size_t i = 0; i < uids.size(); ++i)
		list.Append(uids[i]);
	cmd->SetItemList(list);

	InterfacePtr<IBoolData> data(cmd, IID_IBOOLDATA);
	if (data == nil)
		return kFailure;
	data->Set(hide);

	return CmdUtils::ProcessCommand(cmd);
}

/* DoHideUnchangedToggle — フライアウト「Hide Unchanged Spreads」。
   OFF→ON: 確認ダイアログ(Yes/No、ロケール連動文言)→ Yes なら、比較マーク(sEntries)が1ページも
   無いスプレッドを集めて kHideSpreadCmdBoss で一括で隠し、UID を控えてチェック ON。続けて Source 側も
   同じ分類(平坦ページ番号対応)で自動的に隠す(両文書とも dirty になる)。
   ON→OFF: 控えた分だけ両文書とも再表示(確認なし)。
   ガード: 比較マークが無い(Start 前/変更ゼロ)なら何もしない。特に「変更のあったスプレッドが1つも
   ない」場合は全スプレッドを隠すことになり、InDesign は全スプレッド非表示を許さないため中止する
   (Source 側のみ全対象になった場合は Source 側だけスキップ)。 */
void KESCMActionComponent::DoHideUnchangedToggle()
{
	// ON→OFF: 自分が隠した分だけ再表示して状態を捨てる。
	if (sHideUnchangedOn)
	{
		KESCMResetHideUnchanged(kTrue);
		KESCMScrollMapInvalidateAll();	// スクロールバー地図を再表示後の配置で描き直す(2026-07-11)
		KESCMHideStatus("Hide Unchanged: hidden spreads restored.");
		return;
	}

	// OFF→ON。
	IDataBase* db = KESCMDrawEventHandler::sDB;
	if (db == nil)
	{
		// Start 前。
		KESCMHideStatus("Hide Unchanged: Start first.");
		return;
	}

	// ★「/」が付く overflow ページ(登録されていないのに、文書間のページ数差で比較相手が無い=未比較の
	//   ページ)を含むスプレッドは、変更ありページや登録済み("Added")ページと同じく隠さない
	//   (未比較の見落としを防ぐ。ユーザー要望 2026-07-06)。ここで Target 側の overflow 集合を作る
	//   (Source 側は下の分類が対応表外ページを既に「変更あり」扱いにしているので隠れない)。
	std::set<UID> tOverflowSet;
	{
		IDataBase* srcForOverflow = KESCMArmedSourceDB();
		if (srcForOverflow != nil && srcForOverflow != db)
		{
			std::vector<UID> ovT, ovS, tOverflow, sOverflow;
			KESCMBuildPairing(db, srcForOverflow, ovT, ovS, &tOverflow, &sOverflow);
			tOverflowSet.insert(tOverflow.begin(), tOverflow.end());
		}
	}

	// sEntries が空でも、登録済み(比較相手なし="Added")ページや overflow ページがあれば続行する
	// (それら自体が「変更あり=残す」扱いになるため)。全部無ければ全スプレッドが対象になり、
	// 全スプレッド非表示は InDesign が許さないので中止する。
	if (KESCMDrawEventHandler::sEntries.empty() && !KESCMPageMapHasAnyRegistered(db) && tOverflowSet.empty())
	{
		KESCMHideStatus("Hide Unchanged: no changes to hide.");
		return;
	}

	// 変更なしスプレッド = 所属ページが1つも sEntries に載っていないスプレッド。
	InterfacePtr<ISpreadList> spreadList(db, db->GetRootUID(), UseDefaultIID());
	if (spreadList == nil)
	{
		KESCMHideStatus("Hide Unchanged: spread list not available.");
		return;
	}
	std::vector<UID> unchanged;
	int32 visibleCount = 0;		// 現在表示中(=隠れていない)のスプレッド数。全スプレッド非表示ガードの分母
	const int32 ns = spreadList->GetSpreadCount();
	for (int32 s = 0; s < ns; ++s)
	{
		const UID spreadUID = spreadList->GetNthSpreadUID(s);
		InterfacePtr<ISpread> spread(db, spreadUID, UseDefaultIID());
		if (spread == nil)
			continue;
		// 既に隠れているスプレッド(ユーザーがページパネル等で隠した分)は対象にしない。
		// 自分のリストに入れると ON→OFF でユーザーが隠した分まで再表示してしまうため。
		// 隠し状態は kSpreadBoss 上の IBoolData(IID_IHIDESPREADBOOLDATA、kTrue=隠し中)で読む。
		InterfacePtr<IBoolData> hideFlag(db, spreadUID, IID_IHIDESPREADBOOLDATA);
		if (hideFlag != nil && hideFlag->GetBool())
			continue;
		++visibleCount;
		bool16 changed = kFalse;
		const int32 np = spread->GetNumPages();
		for (int32 p = 0; p < np; ++p)
		{
			const UID pageUID = spread->GetNthPageUID(p);
			// 登録済み(比較相手なし="Added")ページは比較対象外で sEntries には載らないが、緑枠つきの
			// 「変更あり」ページとして扱う。overflow ページ("/"、文書間のページ数差で未比較)も同様に
			// 「変更あり=残す」扱いにして、隠さない(誤って未比較ページを隠すのを防ぐ)。
			if (KESCMDrawEventHandler::sEntries.count(pageUID) > 0 ||
			    KESCMPageMapIsRegistered(db, pageUID) ||
			    tOverflowSet.count(pageUID) > 0)
			{
				changed = kTrue;
				break;
			}
		}
		if (!changed)
			unchanged.push_back(spreadUID);
	}

	if (unchanged.empty())
	{
		KESCMHideStatus("Hide Unchanged: all changed; none to hide.");
		return;
	}
	if ((int32)unchanged.size() >= visibleCount)
	{
		// 保険(sEntries が非空なら通常ここへは来ない): 表示中スプレッドを全部隠すことになる場合は中止
		// (InDesign は全スプレッド非表示を許さない。分母は「現在表示中」の数=手動で隠し済みの分は除く)。
		KESCMHideStatus("Hide Unchanged: can't hide all spreads.");
		return;
	}

	// 確認ダイアログ(kHideSpreadCmdBoss は永続変更=文書が dirty になり、隠し状態は保存ファイルにも残る)。
	// 文言はロケール連動(enUS=英語/jaJP=日本語)。ボタンは Windows の制約で標準 Yes/No のみ。
	const int16 clicked = CAlert::ModalAlert
	(
		// "This feature modifies the document file. Continue?" / 日本語 UI では日本語(KESCMLoc)。
		// ★文書を変更する前の確認なので、意味を取り違えられないよう日本語 UI では日本語で出す
		//   (2026-08-06 ユーザー指示)。
		KESCMLoc::Text(kKESCMHideConfirmKey, KESCMJa::kHideConfirm),
		kYesString,
		kNoString,
		kNullString,
		1,							// Yes を既定ボタンに
		CAlert::eWarningIcon
	);
	if (clicked != 1)
		return;						// No: チェックも付けず何もしない

	const ErrorCode err = KESCMProcessHideSpreadCmd(db, unchanged, kTrue /*hide*/);
	if (err != kSuccess)
	{
		KESCMHideStatus("Hide Unchanged: hide command failed.");
		return;
	}

	sHideUnchangedOn = kTrue;
	sHiddenDB = db;
	sHiddenSpreads = unchanged;

	// ---- Source 側も同じ分類で自動的に隠す ----
	// 「変更あり」を除外対応表(登録済み=比較相手なしページを除いた順番対応)経由で Source ページの
	// 集合にし、Source のスプレッドを走査して、変更ありページに対応するページを1つも含まない
	// スプレッドを隠す。対応表に無い Source ページ(登録済み=削除ページ扱い)は安全側で「変更あり」
	// 扱いにする(縁枠合成(ステップ3)が入るまでの暫定方針)。
	// Source 側が失敗/スキップでも Target 側の隠しはそのまま生かす(致命ではないため)。
	IDataBase* srcDB = KESCMArmedSourceDB();
	int32 srcHiddenCount = 0;
	bool16 srcSkippedAll = kFalse;
	if (srcDB != nil && srcDB != db)
	{
		std::vector<UID> tPages, sPages;
		KESCMBuildPairing(db, srcDB, tPages, sPages);
		std::map<UID, bool16> srcChangedMap;	// 対応表にあるSourceページ→対応Targetページが変更ありか
		for (size_t i = 0; i < tPages.size(); ++i)
			srcChangedMap[sPages[i]] = (KESCMDrawEventHandler::sEntries.count(tPages[i]) > 0) ? kTrue : kFalse;

		InterfacePtr<ISpreadList> srcSpreadList(srcDB, srcDB->GetRootUID(), UseDefaultIID());
		if (srcSpreadList != nil)
		{
			std::vector<UID> srcUnchanged;
			int32 srcVisibleCount = 0;	// Source 側の全スプレッド非表示ガードの分母(表示中のみ)
			const int32 nss = srcSpreadList->GetSpreadCount();
			for (int32 s = 0; s < nss; ++s)
			{
				const UID srcSpreadUID = srcSpreadList->GetNthSpreadUID(s);
				InterfacePtr<ISpread> srcSpread(srcDB, srcSpreadUID, UseDefaultIID());
				if (srcSpread == nil)
					continue;
				const int32 np = srcSpread->GetNumPages();
				// 手動で隠し済みの Source スプレッドは巻き込まない(Target 側と同じ方針)。
				InterfacePtr<IBoolData> srcHideFlag(srcDB, srcSpreadUID, IID_IHIDESPREADBOOLDATA);
				if (srcHideFlag != nil && srcHideFlag->GetBool())
					continue;
				++srcVisibleCount;
				bool16 srcChanged = kFalse;
				for (int32 p = 0; p < np; ++p)
				{
					const UID srcPageUID = srcSpread->GetNthPageUID(p);
					std::map<UID, bool16>::const_iterator mit = srcChangedMap.find(srcPageUID);
					if (mit == srcChangedMap.end() || mit->second)
					{
						srcChanged = kTrue;
						break;
					}
				}
				if (!srcChanged)
					srcUnchanged.push_back(srcSpreadUID);
			}

			if (!srcUnchanged.empty())
			{
				if ((int32)srcUnchanged.size() >= srcVisibleCount)
				{
					// 全スプレッド非表示は不可(例: 変更が Target の追加ページに集中していて、対応する
					// Source ページが存在しない場合は全 Source スプレッドが「変更なし」になり得る)。
					// その場合は Source 側だけスキップし、Target 側の隠しは生かす。
					srcSkippedAll = kTrue;
				}
				else if (KESCMProcessHideSpreadCmd(srcDB, srcUnchanged, kTrue /*hide*/) == kSuccess)
				{
					sHiddenSrcDB = srcDB;
					sHiddenSrcSpreads = srcUnchanged;
					srcHiddenCount = (int32)srcUnchanged.size();
				}
			}
		}
	}

	PMString msg("Hide Unchanged: hid ");
	msg.SetTranslatable(kFalse);
	msg.AppendNumber((int32)unchanged.size());
	msg.Append(" target spread(s)");
	if (srcHiddenCount > 0)
	{
		msg.Append(" + ");
		msg.AppendNumber(srcHiddenCount);
		msg.Append(" source spread(s)");
	}
	msg.Append(".");
	if (srcSkippedAll)
		msg.Append(" Source not hidden (would hide all its spreads).");
	KESCMSetStatus(msg);

	// スクロールバー地図を隠し後の配置で描き直す(隠しスプレッドは地図から除外される。Target/Source 両窓)。
	KESCMScrollMapInvalidateAll();
}

// 文書の生存確認は共有ヘルパ KESCMIsDocDBOpen(KESCMCore.h)を使う(旧ここ static、2026-07-10 共有化)。

// 片側(db+控えリスト)の再表示と状態破棄。restore=kTrue かつ db が生存している場合のみ再表示コマンドを
// 打つ(途中で削除されたスプレッドは ISpread クエリが nil になるのでスキップ)。db が閉じていれば
// deref せず黙って状態だけ捨てる。db は参照渡しで nil に戻す。
static void KESCMRestoreHiddenList(IDataBase*& db, std::vector<UID>& list, bool16 restore)
{
	if (restore && db != nil && !list.empty() && KESCMIsDocDBOpen(db))
	{
		std::vector<UID> alive;
		for (size_t i = 0; i < list.size(); ++i)
		{
			InterfacePtr<ISpread> spread(db, list[i], UseDefaultIID());
			if (spread != nil)
				alive.push_back(list[i]);
		}
		if (!alive.empty())
			KESCMProcessHideSpreadCmd(db, alive, kFalse /*unhide*/);
	}
	list.clear();
	db = nil;
}

// KESCMResetHideUnchanged(KESCMCore.h で宣言) — トグル状態のリセット(Target/Source 両側)。
//   restoreSpreads=kTrue: 控えているスプレッドを再表示してから状態を捨てる(再比較/Stop/手動 OFF/
//     クローズスイープ)。文書の生存は内部で確認するので、片方が閉じていても安全(生存側のみ再表示)。
//   restoreSpreads=kFalse: db に一切触れず状態だけ捨てる。
void KESCMResetHideUnchanged(bool16 restoreSpreads)
{
	KESCMRestoreHiddenList(sHiddenDB, sHiddenSpreads, restoreSpreads);
	KESCMRestoreHiddenList(sHiddenSrcDB, sHiddenSrcSpreads, restoreSpreads);
	sHideUnchangedOn = kFalse;
}

IDataBase* KESCMGetHideUnchangedDB()
{
	return sHiddenDB;
}

IDataBase* KESCMGetHideUnchangedSrcDB()
{
	return sHiddenSrcDB;
}

// (KESCMDoSplitTarget(Split Target 90/10)は 2026-07-04 撤去。実装全文と実測知見は
//  docs/ai-notes/kescm-split-target-mechanism.md と git 履歴 69c4b07 に保存=他プラグインへの転用候補)


//========================================================================================
// Find Overset(フライアウト): アクティブ1文書を走査し、overset のあるページに十字を出す/消す。
// 比較(sEntries)とは完全に独立。状態は KESCMDrawEventHandler::sOversetOn/sOversetDB/sOversetPages。
//========================================================================================

// (アクティブ文書の解決は KESCMActiveDocDB(KESCMCore)に統合。2026-07-25 重複解消)

/* KESCMApplyOversetForDoc(KESCMCore.h で宣言) — db を走査して overset を反映する共有処理。
   Find Overset(ON)/Refresh Overset(armed時Target)/Start(overset ON時) から呼ぶ。前回と別文書なら
   前の文書のサムネイル目印を消す。ステータス行は呼び出し側が用途別に出す。 */
void KESCMApplyOversetForDoc(IDataBase* db)
{
	if (db == nil)
		return;

	// ★最終ライン防御(2026-07-24): 大半の呼び出し(DoFindOverset/DoRefreshOverset/Start 経路)は
	//   その場解決した生きた db を渡すが、Stop 経路だけは保存済みの sOversetDB を渡す。クローズ
	//   responder が漏れて閉じた文書のポインタが来た場合に、下の走査(KESCMCollectOversetLocations)で
	//   解放済み IDataBase を deref しないよう、ここで一度だけ生存確認する(FindDocByDataBase への
	//   ポインタ比較のみ=deref しない。KESCM 全体の共通規約)。死んでいたら何もせず戻る。
	InterfacePtr<IApplication> app(GetExecutionContextSession() ? GetExecutionContextSession()->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app != nil ? app->QueryDocumentList() : nil);
	if (docList == nil || docList->FindDocByDataBase(db) == nil)
		return;

	// 前回の状態を退避(別文書へ移ったら前の文書の目印を消す/同一文書で抜けたページの目印を消すため)。
	IDataBase* prevDB = KESCMDrawEventHandler::sOversetDB;
	std::set<UID> oldPages = KESCMDrawEventHandler::sOversetPages;

	// db を走査して overset 位置(＋点)とページを集める。
	std::vector<KESCMOversetLoc> locs;
	KESCMCollectOversetLocations(db, locs);
	std::set<UID> pages;
	for (size_t i = 0; i < locs.size(); ++i)
		pages.insert(locs[i].pageUID);

	KESCMDrawEventHandler::sOversetOn = kTrue;
	KESCMDrawEventHandler::sOversetDB = db;
	KESCMDrawEventHandler::sOversetPages.swap(pages);
	KESCMDrawEventHandler::sOversetLocs.swap(locs);

	// Pages パネルのサムネイル更新。別文書へ移ったら前の文書の枠/＋を消し、新旧のページを作り直す。
	if (prevDB != nil && prevDB != db)
	{
		std::vector<UID> oldVec(oldPages.begin(), oldPages.end());
		// 2文書とも Purge のみ→呼び出しの最後(下の2回目)で1回だけ ForceRedraw(2026-07-25 バッチ化)。
		KESCMRefreshThumbnailsForPages(prevDB, oldVec, kFalse /*redrawNow*/);	// 前の文書の枠/＋を消す
		KESCMInvalidateDB(prevDB);
		std::vector<UID> newVec(KESCMDrawEventHandler::sOversetPages.begin(),
		                        KESCMDrawEventHandler::sOversetPages.end());
		KESCMRefreshThumbnailsForPages(db, newVec, kFalse /*redrawNow*/);
		KESCMForceRedrawPagesPanelNow();
	}
	else
	{
		// 同一文書 or 初回(prevDB==nil): 旧∪新のサムネイルを作り直す(oldPages 空なら新のみ)。
		std::set<UID> u = oldPages;
		u.insert(KESCMDrawEventHandler::sOversetPages.begin(), KESCMDrawEventHandler::sOversetPages.end());
		std::vector<UID> uv(u.begin(), u.end());
		KESCMRefreshThumbnailsForPages(db, uv);
	}

	// スクロールバー地図を db の窓へ注入(既にあればスキップ)＋再描画。比較未Startでも赤帯を出す。
	KESCMScrollMapAttach(db);
	KESCMScrollMapInvalidateAll();
	KESCMInvalidateDB(db);
	KESCMRefreshNavPosition();	// Prev/Next の対象(有効化・位置 k/N)を更新
}

// 比較中なら overset の走査対象は必ず比較 Target 文書(sDB)にする(変更(枠)と overset を同じ文書で
// Prev/Next 巡回できるように=nav の navDB と一致させる)。未 Start ならアクティブ文書。
static IDataBase* KESCMOversetScanTargetDB()
{
	if (KESCMDrawEventHandler::sDB != nil)
		return KESCMDrawEventHandler::sDB;	// = KESCMNavDoc() の armed 分岐と同じ
	return KESCMActiveDocDB();
}

/* DoFindOversetToggle — フライアウト「Find Overset」トグル。
   OFF→ON: 走査対象文書(比較中はTarget/それ以外はアクティブ)を走査→ overset を反映。
   ON→OFF: 集合を空にしてトグル OFF、走査していた文書を再描画して目印を消す。 */
void KESCMActionComponent::DoFindOversetToggle()
{
	// ON→OFF: ＋を消す。
	if (KESCMDrawEventHandler::sOversetOn)
	{
		IDataBase* prevDB = KESCMDrawEventHandler::sOversetDB;
		// Pages パネルのサムネイルから＋を消すため、消える前にページ集合を控える。
		std::vector<UID> prevPages(KESCMDrawEventHandler::sOversetPages.begin(),
		                           KESCMDrawEventHandler::sOversetPages.end());
		KESCMDrawEventHandler::DropOverset();
		KESCMRefreshThumbnailsForPages(prevDB, prevPages);	// サムネイルを作り直して＋を消す
		// スクロールバー地図: 比較もしていなければ全窓から撤去、比較中なら残して赤帯だけ描き直す。
		if (KESCMIsArmed())
			KESCMScrollMapInvalidateAll();
		else
			KESCMScrollMapDetachAll();
		KESCMInvalidateDB(prevDB);	// nil 安全(他の呼び出しと同じ)
		KESCMRefreshNavPosition();	// Prev/Next から overset 箇所を外す(比較のみ/対象なしへ)
		PMString msg("Find Overset: off.");
		msg.SetTranslatable(kFalse);
		KESCMSetStatus(msg);
		return;
	}

	// OFF→ON: 走査対象文書(比較中は Target、未 arm はアクティブ)を走査して反映。
	IDataBase* db = KESCMOversetScanTargetDB();
	if (db == nil)
	{
		PMString msg("Find Overset: no active document.");
		msg.SetTranslatable(kFalse);
		KESCMSetStatus(msg);
		return;
	}
	KESCMApplyOversetForDoc(db);

	PMString msg("Find Overset: on (");
	msg.SetTranslatable(kFalse);
	msg.AppendNumber((int32)KESCMDrawEventHandler::sOversetPages.size());
	msg.Append(" page(s)).");
	KESCMSetStatus(msg);
}

/* DoRefreshOverset — フライアウト「Refresh Overset」。Find Overset が ON のときだけ有効(OFF時は
   UpdateActionStates で灰色)。アクティブ文書を再走査して集合を貼り直す。文書が切り替わっていたら
   前の文書の十字も消す。 */
void KESCMActionComponent::DoRefreshOverset()
{
	if (!KESCMDrawEventHandler::sOversetOn)
		return;	// OFF時は無効(保険。通常はメニューが灰色で呼ばれない)

	IDataBase* db = KESCMOversetScanTargetDB();
	if (db == nil)
	{
		PMString msg("Refresh Overset: no active document.");
		msg.SetTranslatable(kFalse);
		KESCMSetStatus(msg);
		return;
	}
	KESCMApplyOversetForDoc(db);	// 再走査・反映(別文書なら前の文書の目印も消す)は共有処理に集約

	PMString msg("Refresh Overset: ");
	msg.SetTranslatable(kFalse);
	msg.AppendNumber((int32)KESCMDrawEventHandler::sOversetPages.size());
	msg.Append(" page(s).");
	KESCMSetStatus(msg);
}

// KESCMOpenAboutURL(KESCMCore.h で宣言) — パネルのイラストクリックから呼ばれる。「このプラグインに
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
