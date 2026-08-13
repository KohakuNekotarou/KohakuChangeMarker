//========================================================================================
//
//  KESCMCore.h
//
//  ChangeMarker (KESCM) の共有操作。スクリプトプロバイダとパネル UI の両方から呼べる。
//  描画エンジン本体とその file-local 状態は KESCMDrawEventHandler.cpp にあり、ここはその薄い
//  入口として、パネルのウィジェットオブザーバがスクリプトメソッドと完全に同じ挙動を駆動できるように
//  する(Start = 変更マーク＋peek arm、Clear = マーク消去＋peek disarm、など)。
//
//========================================================================================

#ifndef __KESCMCore_h__
#define __KESCMCore_h__

#include "BaseType.h"		// ErrorCode, bool16
#include "PMString.h"
#include "PMReal.h"			// PMReal(ヒットテストヘルパのマウス座標)
#include "OMTypes.h"			// UID (typedef IDType<UID_tag>)
#include <vector>

class IDataBase;
class IControlView;

// ドキュメント内の全ページUIDを、スプレッド順・ページ順で平坦に集める。比較(KESCMDoMarkChangesDoc)と
// 色サンプラが共有するヘルパ。実体は KESCMCore.cpp。
void		KESCMCollectPageUIDs(IDataBase* db, std::vector<UID>& out);

// マスタースプレッドのページUIDを、マスタースプレッド順・ページ順で out に足す(out はクリアしない)。
// ★上の KESCMCollectPageUIDs とは別物。あちらは ISpreadList=通常スプレッドだけを平坦化するヘルパで、
//   比較のページ対応・Prev/Next・TSV・Sync・Hide Unchanged が共有している。そこへマスターを混ぜると
//   「比較する対象そのものが変わる」ので、マスターは常に別に集めて呼び手が足す(overset と同じ流儀)。
// ⚠out をクリアしないのは、通常ページの列の後ろへ連結する使い方を想定しているため。
void		KESCMCollectMasterPageUIDs(IDataBase* db, std::vector<UID>& out);

// ページアイテムの UID → そのアイテムが載っているページ UID(どのページにも載らないなら kInvalidUID)。
// あふれ位置の報告(KESCMOversetScan)と Story Edits の一覧が同じ問いを持つので1本を共有する。
// ★答えは必ず実ページ(kPageBoss)で、spread の UID は返らない。理由は実体側のコメント(KESCMCore.cpp)。
UID			KESCMFramePageUID(IDataBase* db, UID frameUID);

// 文書の生存確認: db がまだ開いている文書のものなら kTrue。★閉じた db は deref 禁止のため、
// IDocumentList::FindDocByDataBase へのポインタ比較のみで判定する(KESCM 全体の共通規約)。
// Hide Unchanged の復元・遅延サムネイル更新などが共有する。実体は KESCMCore.cpp。
bool16		KESCMIsDocDBOpen(IDataBase* db);

// 現在のマウス位置を、このビューの content(ペーストボード)座標で読む(画面→窓→content)。view が nil なら
// kFalse。peek と色サンプラが同じ流儀でカーソル位置を求めるための共有ヘルパ。
bool16		KESCMQueryMouseContentPoint(IControlView* view, PMReal& outX, PMReal& outY);

// マウス下のレイアウトビューを求める(Split Window対応)。ILayoutUIUtils::QueryFrontView() は
// 「front presentationの代表ビュー」を1つ返すだけでマウス位置を見ないため、1文書がSplit Window中
// (kLayoutWidgetBoss＋kLayoutSecondaryPanelWidgetID)だと常に元側を返してしまう(実測で確認済み:
// スプリットの新しい側で操作しても反応しない)。本関数は QueryWindowUnderPoint→
// IPanelControlData::FindWidget(windowPt) のヒットテストで、実際にマウスが乗っているペイン
// (元側/新しい側)を正しく特定する。戻り値は QueryFrontView() と同じ契約(+1 ref、呼び出し側で
// InterfacePtr 等による Release が必要)。見つからなければ nil。
IControlView*	KESCMQueryViewUnderMouse();

// view がどの文書のレイアウトビューかを特定する(見つからなければ nil)。同期オブザーバの通知元ビューの
// 所属文書判定と、色サンプラの窓同一性ガードが共有する。実体は KESCMCore.cpp
// (2026-07-25 に KESCMPeek.cpp の file-static から移動)。
// ★2026-08-06 の API 監査(A-1)で公式ルートへ変更: レイアウトビュー boss の ILayoutControlData に
//   GetDocument() を聞く(ILayoutControlData.h:181。手本=CPathCreationTracker.cpp:277-285)。
//   引けなかった場合だけ、従来の「全文書 × GetAllLayoutViews のポインタ照合」へ落ちる。
IDataBase*	KESCMFindDocDbForView(IControlView* view);

// 上の関数のフォールバック経路が持つ「直前にヒットした文書」ヒントを捨てる(2026-07-25 追補)。ヒントは
// 答えを決めるものではなく「どの db から照合を試すか」だけなので正しさには影響しないが、文書クローズ・
// arm 切替・同期 OFF の直後は外れが確定しているので捨てておく。実体は KESCMCore.cpp。
// (★公式ルートが効いている限りフォールバックは走らないので、この呼び出しは実質 no-op になる)
void		KESCMForgetViewDbHint();

// アクティブ(前面)文書とその db(無ければ nil)。ActiveContext 経由の解決を1箇所に集約
// (2026-07-25 重複解消: 旧 KESCMPanelObserver 内 KESCMActiveDoc と KESCMActionComponent 内
// KESCMActionActiveDocDB の同一実装2本を統合)。実体は KESCMCore.cpp。
class IDocument;
IDocument*	KESCMActiveDoc();
IDataBase*	KESCMActiveDocDB();

// アプリが終了処理中(IApplication::GetApplicationState() が kQuitting/kShuttingDown)なら kTrue。
// quit の close-all フェーズ(保存確認でキャンセル可能な段階)はまだ kRunning=kFalse。kTrue の間は
// ウィンドウ/パネルの解体順がプラットフォーム依存(特に Mac)のため、widget 操作・再描画・idle task
// 予約などの UI 仕事を全てスキップし、状態(メモリ)の破棄だけに縮退すること。実体は KESCMCore.cpp。
bool16		KESCMAppIsQuitting();

// Shutdown 専用: パネルのステータス行のセッション記憶(gSessionStatus)を空にする。static PMString の
// 静的デストラクタをプラグイン unload 時の実質 no-op にするため(Mac の unload 順は Windows と異なり、
// 破棄時に生きた heap バッファを持たせない方が安全)。実体は KESCMPanelObserver.cpp。
void		KESCMClearSessionStatus();

// マウス下のページを特定した結果(KESCMFindPageUnderMouse 参照)。globalPageBase は自身の文書内での
// 平坦ページ番号(KESCMCollectPageUIDs と一致)。旧ドキュメント側のページは(登録済み=比較相手なし
// ページの除外を考慮するため)ここから直接インデックスせず、除外対応表(KESCMPageMap.h の
// KESCMMapTargetToSource/KESCMMapSourceToTarget)を使うこと。
struct KESCMPageHit
{
	int32 spreadIndex;		// 当たったスプレッドのスプレッドリスト内インデックス
	UID   spreadUID;		// そのスプレッドのUID(必要に応じて ISpread を引き直す)
	int32 numPages;			// そのスプレッドのページ数
	int32 globalPageBase;	// このスプレッド先頭の平坦ページ番号
	int32 hitPageIndex;		// スプレッド内でカーソル下にあるページの 0 始まりインデックス
	UID   hitPageUID;		// そのページのUID
};

// マウス(content/ペーストボード座標)を targetDB の全ページにスプレッド順・ページ順でヒットテストする。
// 最初に (mx,my) を含むページで 'out' を埋めて kTrue を返す。無ヒットなら kFalse。
bool16		KESCMFindPageUnderMouse(IDataBase* targetDB, PMReal mx, PMReal my, KESCMPageHit& out);

// targetDB の各ページを sourceDB の同番号ページと比較し、変更マークのオーバーレイを(再)構築する。
// outReport にはスクリプトメソッドが返すのと同じ状態文字列が入る。
//
// allowIncremental=kTrue のときは「差分再比較」を試みる: 前回比較(sPrevPairTargetToSource)と今回の
// 除外対応表ペアリングを突き合わせ、ペアが不変のページは MakeEntry(=高dpiラスタ化2枚)を呼ばず前回
// 結果を再利用し、ペアが新規/相手変化/消滅したページだけを再計算する。登録トグル(比較相手なしページの
// 追加/解除)専用の高速化で、そこでは文書内容は変わらずペアリングだけが動くため安全に再利用できる。
// ★内容が変わり得る Start や、除外条件が変わる Ignore Page Number Marker 切替では kFalse(既定)にして
//   従来どおり全ページを再ラスタ化すること。状態不整合時(別文書対/前回ペアリング無し)は自動で全再比較に
//   フォールバックする。
ErrorCode	KESCMDoMarkChangesDoc(IDataBase* targetDB, IDataBase* sourceDB, PMString& outReport, bool16 allowIncremental = kFalse);

// db が非nilなら、その IDocument のビューを再描画する(nil や IDocument 取得失敗時は何もしない)。
// Clear/印刷マーク切替/peek disarm が「呼び出し側の db」と「実際にマークが描かれている対象文書」の
// 両方を確実に再描画するための共有ヘルパ(2つが同じ db なら二重には呼ばない)。
void		KESCMInvalidateDB(IDataBase* db);

// Find Overset を db に対して走査・反映する共有処理(実体は KESCMActionComponent.cpp)。db(非nil)の overset
// 位置/ページを集めて sOverset* に格納し、Pages パネルのサムネイル・スクロール地図・Prev/Next の位置表示を
// 更新する。ステータス行は出さない(呼び出し側が用途別メッセージを出す)。★比較中は必ず Target 文書に紐づけ
// 直すために、Find Overset/Refresh(armed 時)と Start(overset ON 時)から呼ぶ。前回と別文書なら前の文書の
// サムネイルの目印も消す。
void		KESCMApplyOversetForDoc(IDataBase* db);

// オーバーレイ全体(と旧版画像のキャッシュ)を破棄し、db を再描画する。
void		KESCMDoClearMarks(IDataBase* db);

// マークを印刷に出すか(かつ画面に常時表示するか)と、枠の不透明度の選択を切り替える。
// opacity25Flag: kTrue=25% / kFalse=75%(ツール左hold表示・印刷ON常時表示・印刷出力に共通)。
void		KESCMDoSetPrintMarks(bool16 printFlag, bool16 opacity25Flag, IDataBase* db);

// 旧版の peek を arm / disarm する(パネルの ON/OFF 状態も駆動する)。
void		KESCMDoArmMousePeek(IDataBase* targetDB, IDataBase* sourceDB);
void		KESCMDoDisarmMousePeek(IDataBase* db);

// パネルの状態アクセサ。"Armed" == Start ボタンが実行済みで Clear がまだ、の状態。arm 中はパネルが
// Target/Source 名と ON アイコンを表示し、それ以外では名前を隠して OFF を表示する。
bool16		KESCMIsArmed();
// Story Edits の一覧を、この2文書の**今の**状態から丸ごと作り直し、ツリーと見出しへ反映する。
// ★呼び手は2つ＝全体比較(KESCMDoMarkChangesDoc)と「KCM: Refresh Page Comparison」。後者は
//   KESCMDoMarkChangesDoc を通らない独立経路なので、ここを共有しないと Refresh の後だけ一覧が
//   古いまま残る(2026-08-10 に実測)。nil は黙って無視する。
void		KESCMRebuildStoryEdits(IDataBase* targetDB, IDataBase* sourceDB);

IDataBase*	KESCMArmedTargetDB();
IDataBase*	KESCMArmedSourceDB();

// 現在の印刷マーク設定。パネルを開き直したときにチェック/ラジオを実状態へ復元するために使う。
bool16		KESCMGetPrintMarks();		// 印刷マーク ON/OFF
bool16		KESCMGetMarkOpacity25();	// 枠不透明度の選択: kTrue=25% / kFalse=75%

// ★ページパネルのサムネイル更新は解決済み(2026-07-06)。実体は KESCMThumbnailRefresh.*。
// 「変更ページの UID を IImageCacheMgr::Purge → Pages パネルを ForceRedraw」の2手で、既に表示済みの
// サムネイルも作り直される(比較/Clear/印刷トグル等の直後)。
// ⚠ここには 2026-07-05 の調査に基づく「内部キャッシュなので更新できない・見送りとした」という記述が
//   2026-08-06 まで残っていた(翌日に解決していたのに更新し忘れ)。効かなかったのは
//   IPagesSubPanelController::InvalidatePageWidget/InvalidateSpreadWidget・UpdatePagesPanel の
//   bForcePurge・サムネイル設定の全体トグルであって、ForceRedraw は現行実装の要の1つ。
//   経緯と切り分けの詳細は KESCMThumbnailRefresh.cpp 冒頭 / memory kescm-pages-panel-thumbnails。

// ドキュメントがクローズされた直後(kAfterCloseDoc レスポンダ)に呼ぶ。追跡中の全DB(マーク/旧版画像/
// peek arm)を IDocumentList で生存確認し、閉じていたものだけ確定的にクリーンアップする
// (DropAll/DropAllOrig/無音 disarm)。片付けが起きたらパネルも ON→OFF 更新する。
// どの db が閉じたかは信号から取れない(AfterClose では UIDRef 無効)ため、生存スイープで判定する。
// 実体は KESCMPeek.cpp(peek の file-local 状態にアクセスできる唯一の場所)。
void		KESCMHandleDocsClosed();

// 表示中の自分のパネル(kKESCMPanelWidgetID)。隠れていれば nil。
// ★終了処理中に session が nil になる経路まで吸収済み(2026-07-25 の規約)。
// 「session → app → panelMgr → GetVisiblePanel」の定型を持つのはこの1か所だけにしてある
// (2026-08-06 監査 C-1 で3か所を一本化 → 2026-08-09 に公開して4人目の使い手を迎えた)。
// 実体は KESCMPanelObserver.cpp。
IControlView*	KESCMGetVisibleOwnPanel();

// 現在表示中のパネルがあれば、その ON/OFF 表示(Target/Source 名・アイコン・トグルラベル)を
// 現在の arm 状態(KESCMIsArmed 等)に合わせて更新する。パネルが隠れていれば何もしない
// (再表示時に AutoAttach が反映する)。実体は KESCMPanelObserver.cpp。
void		KESCMRefreshPanel();

// 比較の開始/解除トグル(旧パネルの Start/Stop ボタン→2026-07-10 フライアウトメニュー化)。
// arm 済みなら解除(マーク消去+peek 解除)、未 arm なら開始(アクティブ文書=Target・別の開いている
// 文書=Source を解決して比較+peek arm)。実行後に KESCMRefreshPanel でパネル表示を更新する。
// フライアウト項目 kKESCMPopupStartStopActionID の DoAction から呼ぶ。実体は KESCMPanelObserver.cpp。
void		KESCMToggleStartStop();

// ★上のトグルを2つに割ったもの(2026-08-12)。**解決子(どの2文書か)と手順(何をするか)を分ける**ため。
// 呼び手が2つになったので切り出した＝フライアウトの Start/Stop と、ブック比較の行の右クリック
// 「Start Change Marker」(その章の2ファイルを開いてから明示的に渡す)。手順を書き写すと必ずずれる
// ([[one-question-one-place]])。どちらもパネル表示の更新(KESCMRefreshPanel)まで自分で行うので、
// 呼び手は結果を整える必要が無い。実体は KESCMPanelObserver.cpp。
//
// KESCMStartComparisonFor: **この2文書で**比較を開始する。どちらを Target にするかは呼び手が決める
// (ここには一切の解決ロジックが無い)。nil を渡したら何もしない。⚠既に arm 中でも構わず上書きするので、
// 「Stop してから Start」にしたい呼び手は先に KESCMStopComparison を呼ぶこと。
void		KESCMStopComparison();
void		KESCMStartComparisonFor(IDocument* target, IDocument* source);

// 比較を開始できるか＝アクティブ(前面)文書があり、かつ別の開いている文書が1つ以上ある(=Target と
// Source が揃う)。フライアウトの「Start」を有効にしてよいかの判定に使う(2026-08-06 ユーザー指定:
// 文書が2つ以上開かれていなければ押せない)。★KESCMToggleStartStop の開始分岐と同じ解決子を通るので、
// メニューの見た目と押した結果がずれない。実体は KESCMPanelObserver.cpp。
bool16		KESCMCanStartComparison();

// 印刷マーク ON/OFF トグル(旧パネルのチェックボックス→2026-07-10 フライアウトメニュー化)。
// 現在の印刷フラグ(KESCMGetPrintMarks)を反転し、不透明度は現在の選択(KESCMGetMarkOpacity25)を維持して
// KESCMDoSetPrintMarks を呼ぶ。フライアウト項目 kKESCMPopupPrintMarksActionID の DoAction から呼ぶ。
// 実体は KESCMPanelObserver.cpp。
void		KESCMTogglePrintMarks();

// 枠の不透明度を 25%(op25=kTrue)/75%(kFalse)に設定(旧パネルの opacity ラジオ→2026-07-10 フライアウト
// メニュー化)。現在の印刷フラグ(KESCMGetPrintMarks)を維持したまま KESCMDoSetPrintMarks を呼ぶ。
// フライアウト項目 kKESCMPopupOpacity25ActionID / kKESCMPopupOpacity75ActionID の DoAction から呼ぶ。
// 実体は KESCMPanelObserver.cpp。
void		KESCMSetMarkOpacity25(bool16 op25);

// パネルのステータス行を更新する(KESCMPanelObserver::SetStatus と同じ処理を自由関数として公開)。
// パネルが隠れていてもセッション状態は覚えておき、再表示時に復元する。forceRedrawNow=kTrue なら、
// この後にブロッキング処理が続く場合でも次のイベントループを待たずに今すぐ描画する
// (KESCMDoMarkChangesDoc の比較ループ前の busyMsg 表示に使う)。実体は KESCMPanelObserver.cpp。
void		KESCMSetStatus(const PMString& s, bool16 forceRedrawNow = kFalse);

// KESCMSetStatus が最後に出した文字列。app.kcmStatus(KESCMScriptProvider.cpp)が返す値で、
// 実体はパネルの widget ではなくモジュール側の変数なので、★パネルを閉じていても答える。
// (パネルの widget から読むと、閉じている間は空になるうえ、再表示のたびに作り直される。)
// 実体は KESCMPanelObserver.cpp。
void		KESCMGetSessionStatus(PMString& out);

// Prev/Next の間に出す現在位置表示(例 "3/12")と、Prev/Next ボタンの有効/無効をまとめて更新する。
// ステータス行とは別ウィジェット(kKESCMNavPosTextWidgetID / kKESCMPrevChangeButtonWidgetID /
// kKESCMNextChangeButtonWidgetID)。posText 空でクリア。navButtonsEnabled=kFalse で両ボタンを無効化。
// 通常は KESCMChangeNav.cpp の KESCMRefreshNavPosition から呼ぶ(表示規則はそちら参照)。パネルが
// 隠れていれば何もしない(再表示時に KESCMRefreshNavPosition が実状態を反映)。実体は KESCMPanelObserver.cpp。
void		KESCMSetNavPosition(const PMString& posText, bool16 navButtonsEnabled);

// パネルのイラスト(ON/OFF アイコン)をクリックしたときに呼ぶ。「このプラグインについて」に載せている
// 配布元URL(kKESCMRepoURL, KESCMID.h)を既定のブラウザで開く。実体は KESCMActionComponent.cpp。
void		KESCMOpenAboutURL();

// ★パネルのツール切替ボタン(kKESCMToolButtonWidgetID、Prev の左)を押したときに呼ぶ。
//   このプラグインのツール(kKESCMToolBoss ＝ ツールボックスに出ている琥珀のツール)を
//   アクティブツールにする。ツールボックスでそのツールを直接クリックしたのと同じ状態になる。
//   ★ツールボックスが無い実行構成(サーバー等)では何もしない。実体は KESCMTool.cpp。
//   戻り値: 実際にアクティブになったら kTrue(SetActiveTool の答えをそのまま返す)。
//   ★押した結果をステータス行に出すために使う ＝ 効かなかったときに無反応に見えないように。
bool16		KESCMActivateOwnTool();

// ★このプラグインのツールが今アクティブか(実体は KESCMTool.cpp)。パネルを組み立て直したときに
//   ボタンの押下表示を実状態へ合わせるために使う ＝ 固定の既定値を書かない([[panel-autoattach-read-real-state]])。
bool16		KESCMIsOwnToolActive();

// ★パネルのツール切替ボタンを「押されている/いない」表示にする(実体は KESCMPanelObserver.cpp)。
//   ★呼び元は KESCMTool::Select / Deselect の2つだけ ＝ ツールボックスで選んでもパネルのボタンで
//     選んでもショートカットでも、必ずここを通る(状態を2か所で管理しない=[[one-question-one-place]])。
//   パネルが隠れていれば何もしない(再表示時に KESCMIsOwnToolActive から復元される)。
void		KESCMSetToolButtonSelected(bool16 selected);

// (Split Target on Start(KESCMGetSplitOnStart/KESCMDoSplitTarget)は 2026-07-04 撤去。
//  仕組みは docs/ai-notes/kescm-split-target-mechanism.md と git 履歴 69c4b07 に保存)

// フライアウト「Hide Unchanged Spreads」トグルの状態リセット(Target/Source 両側)。
// restoreSpreads=kTrue なら、覚えている「自分が隠したスプレッド」を kHideSpreadCmdBoss(kFalse) で
// 再表示してから状態を捨てる(削除済み UID はスキップ)。★文書の生存確認は内部で行う
// (IDocumentList へのポインタ比較のみ)ので、片方が閉じていても kTrue で安全(生存側のみ再表示)。
// kFalse なら db に一切触れず状態だけ捨てる。
// 呼び所: 再比較(KESCMDoMarkChangesDoc)・Stop(KESCMDoClearMarks)・クローズスイープ
// (KESCMHandleDocsClosed)はいずれも kTrue でよい。実体は KESCMActionComponent.cpp。
void		KESCMResetHideUnchanged(bool16 restoreSpreads);

// 「Hide Unchanged Spreads」で現在スプレッドを隠している文書(未使用なら nil)。Target 側と Source 側。
// クローズスイープが生存確認(FindDocByDataBase への比較のみ、deref しない)に使う。
// 実体は KESCMActionComponent.cpp。
IDataBase*	KESCMGetHideUnchangedDB();
IDataBase*	KESCMGetHideUnchangedSrcDB();

// (★フライアウト「Sync Layout Views」トグル(KESCMGetLayoutSync / KESCMSetLayoutSync)と
//  「Align Other Views to Active」(KESCMAlignOtherViewsToActiveNow)の宣言は、2026-08-13 の
//   model/UI 分割 第1段 Task 1 で **KESCMViewSync.h** へ移した。実体も KESCMViewSync.cpp。
//   どちらも IControlView / IPanorama を相手にする UI 側の機能で、model からは呼べない。)

#endif // __KESCMCore_h__
