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
#include "KESCMBoundaryID.h"	// KESCMCompareMode(境界に出る型。model/UI の両方が読む場所に在る)
#include "PMString.h"
#include "PMReal.h"			// PMReal(ヒットテストヘルパのマウス座標)
#include "OMTypes.h"			// UID (typedef IDType<UID_tag>)
#include <vector>
#include <set>				// KESCMCollectChangedPageUIDs の出力集合

class IDataBase;
class IControlView;

// ドキュメント内の全ページUIDを、文書のページ順(平坦)で集める。比較(KESCMDoMarkChangesDoc)と
// 色サンプラが共有するヘルパ。実体は KESCMCore.cpp。
// ★2026-08-16(監査 B3・A-3): 中身は **IPageList**(`GetPageCount`/`GetNthPageUID`)。ヘッダー自身が
//   「other sources から同じ情報を計算するより *much* more efficient」と名指しする公式の道。
//   **隠しスプレッドのページも含む**(実測で確認済み。理由は実体側のコメント)。
void		KESCMCollectPageUIDs(IDataBase* db, std::vector<UID>& out);

// マスタースプレッドのページUIDを、マスタースプレッド順・ページ順で out に足す(out はクリアしない)。
// ★上の KESCMCollectPageUIDs とは別物。あちらは **IPageList**＝文書の通常ページだけを平坦に返すヘルパで
//   (マスターを含まないのは契約＝`IPageList.h:81` "does not include master pages")、
//   比較のページ対応・Prev/Next・TSV・Sync・Hide Unchanged が共有している。そこへマスターを混ぜると
//   「比較する対象そのものが変わる」ので、マスターは常に別に集めて呼び手が足す(overset と同じ流儀)。
// ⚠out をクリアしないのは、通常ページの列の後ろへ連結する使い方を想定しているため。
void		KESCMCollectMasterPageUIDs(IDataBase* db, std::vector<UID>& out);

// そのページが載っているスプレッドが「隠されている」か(Hide Unchanged Spreads / ページパネルの
// Hide Spread のどちらで隠したかは問わない)。マスターページは隠せないので常に kFalse。
// ★2026-08-18(不具合再検査 B10 の2周目)に新設。KESCM 内には隠し判定が既に5か所あるが、どれも
//   「ISpreadList を回りながらそのスプレッドを見る」文脈で、**ページ UID から聞く**問いはここが初めて
//   ---- 6か所目を素で書かず1本に立てた([[one-question-one-place]])。
// ★用途: 隠れているページは画面にもPDFにも出ないので、そのページを名指しする出力(Export Changed
//   Pages の一覧)から外す(ユーザー指定 2026-08-18)。判定は kSpreadBoss 上の IBoolData
//   (IID_IHIDESPREADBOOLDATA、kTrue=隠し中)で読む＝Hide Unchanged が隠す/除外するのと同じ読み方。
bool16		KESCMIsPageOnHiddenSpread(IDataBase* db, UID pageUID);

// db が現在の比較対象(sDB/sSrcDB)なら、「今マークが出得るページ UID」(変更リング + overflow「/」+
// 登録「/」)を outPages へ**足して** kTrue を返す。比較対象でなければ何もせず kFalse。
// ★「何がマーク済みか」の定義はこの1箇所に集約する。マークの種類を増やす時はここへ足せば、
//   再比較前の退避(KESCMDoMarkChangesDoc)と、UI 側のサムネイル Purge の両方が自動で追随する。
// ★★2026-08-13 に KESCMThumbnailRefresh.h からここへ移した(model/UI 分割 第1段 Task 10)。
//   widget にも view にも触らない **model の問い**で、呼び手も model 側だけ ---- UI 側ヘッダーに
//   置いてあったせいで、呼び手3ファイルが UI を include しているように見えていた(逆流台帳 §2-1)。
//   ⚠ UI 側(KESCMThumbnailRefresh.cpp)は今後もこれを**呼ぶ**。UI→model は許される向き。
bool16		KESCMCollectChangedPageUIDs(IDataBase* db, std::set<UID>& outPages);

// ページアイテムの UID → そのアイテムが載っているページ UID(どのページにも載らないなら kInvalidUID)。
// あふれ位置の報告(KESCMOversetScan)と Story Edits の一覧が同じ問いを持つので1本を共有する。
// ★答えは必ず実ページ(kPageBoss)で、spread の UID は返らない。理由は実体側のコメント(KESCMCore.cpp)。
UID			KESCMFramePageUID(IDataBase* db, UID frameUID);

// 文書の生存確認: db がまだ開いている文書のものなら kTrue。★閉じた db は deref 禁止のため、
// IDocumentList::FindDocByDataBase へのポインタ比較のみで判定する(KESCM 全体の共通規約)。
// Hide Unchanged の復元・遅延サムネイル更新などが共有する。実体は KESCMCore.cpp。
bool16		KESCMIsDocDBOpen(IDataBase* db);

// (★ビューに向かって聞く4本(KESCMQueryMouseContentPoint / KESCMQueryViewUnderMouse /
//  KESCMFindDocDbForView / KESCMForgetViewDbHint)の宣言は、2026-08-13 の model/UI 分割 第1段 Task 3 で
//  **KESCMViewLookup.h** へ移した。実体も KESCMViewLookup.cpp。どれも IControlView を受け取るか返す
//  UI 側の問いで、model からは呼べない。)

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

// マウス下のページを特定した結果(KESCMFindPageUnderMouse 参照)。globalPageBase は自身の文書内での
// 平坦ページ番号(KESCMCollectPageUIDs と一致)。旧ドキュメント側のページは(登録済み=比較相手なし
// ページの除外を考慮するため)ここから直接インデックスせず、除外対応表(KESCMPageMap.h の
// KESCMMapTargetToSource/KESCMMapSourceToTarget)を使うこと。
struct KESCMPageHit
{
	int32 spreadIndex;		// 当たったスプレッドのスプレッドリスト内インデックス(★マスターは -1)
	UID   spreadUID;		// そのスプレッドのUID(必要に応じて ISpread を引き直す)
	int32 numPages;			// そのスプレッドのページ数
	int32 globalPageBase;	// このスプレッド先頭の平坦ページ番号(★マスターは -1＝平坦列に居ない)
	int32 hitPageIndex;		// スプレッド内でカーソル下にあるページの 0 始まりインデックス
	UID   hitPageUID;		// そのページのUID
	// ★2026-08-16: 当たったのがマスタースプレッドのページか。
	//   ⚠**kTrue のとき spreadIndex と globalPageBase は -1 で意味を持たない**——マスターは
	//     ISpreadList にも IPageList にも居ないので、平坦ページ番号という概念が無い。
	//   ★相手ページの引き方は変わらない: KESCMMapTargetToSource / KESCMMapSourceToTarget が
	//     通常とマスターの両方を引く(マスターは名前対応＝KESCMBuildMasterPairing)。
	bool16 isMaster;
};

// マウス(content/ペーストボード座標)を targetDB の全ページにスプレッド順・ページ順でヒットテストする。
// 最初に (mx,my) を含むページで 'out' を埋めて kTrue を返す。無ヒットなら kFalse。
//
// ★★★onlySpreadUID(2026-08-16・ユーザー報告「マスターページで peek も CMYK も出ない」の決着／
//   ★2026-08-19・不具合再検査 B-U6 で**絞りすぎを修正**):
//   **表示中スプレッドと同じ「種別」のページだけを見る**——表示中がマスターなら**そのマスターだけ**、
//   表示中が通常なら**通常スプレッドは全部**(マスターは見ない)。kInvalidUID なら従来どおり全走査。
//
//   ⚠★★★**2026-08-19 の修正＝絞る単位は「スプレッド」ではなく「種別」**。
//     2026-08-16 版はここを「**そのスプレッドのページだけを見る**(通常/マスターを問わない)」と定義し、
//     実装も2つのループが揃ってそうなっていた。⇒ **通常スプレッド同士まで落ちる**ので、画面に複数
//     スプレッドが見えていても**表示中スプレッド以外のページでは CMYK が `---` になり、
//     Shift+ の peek も出ない**(ユーザー報告 2026-08-19)。
//     ★**重なるのはマスター⇔通常の間だけ**で、**通常スプレッド同士は重ならない**——裏付けは、
//     2026-08-16 に絞りを入れるまで**この関数はずっと通常を全走査していて、通常同士の取り違えが
//     一度も出ていない**こと。∴ 曖昧さを解くのに必要な絞りは「種別」の一段で足りる。
//   ⚠**渡すのは「そのビューが今表示しているスプレッド」**＝`ILayoutControlData::GetSpreadRef()`
//     (`ILayoutControlData.h:256`「the spread this view is currently viewing」)。UI 側が観測して渡す。
//
//   ★★**なぜ必要か(実測 2026-08-16)**＝**マスタースプレッドと通常スプレッドのペーストボード矩形は重なる。**
//     マスタースプレッドを表示していても、マウスの content 座標は通常スプレッドのページにも当たるので、
//     全走査だと**通常ページを掴んでしまう**(診断で `normal` と出た)。その結果:
//       ・peek …… 通常ページの旧版を作るが、描画中のスプレッドはマスター＝**何も出ない**
//       ・CMYK … **通常ページの色を「マスターの色」として表示する**(値が出るので気づけない)
//     ⇒ **「どのスプレッドを見ているか」は窓にしか答えられない問い**なので、model では解けない。
//   ⚠**順序では解けない**(通常を先に見てもマスターを先に見ても、片方が必ず誤る)。
bool16		KESCMFindPageUnderMouse(IDataBase* targetDB, PMReal mx, PMReal my, KESCMPageHit& out,
                                    UID onlySpreadUID = kInvalidUID);

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

// (★Find Overset の走査結果を反映する KESCMApplyOversetForDoc の宣言は、2026-08-13 の
//  model/UI 分割 第1段 Task 2 で **KESCMOversetApply.h** へ移した。実体も KESCMOversetApply.cpp。
//  対象文書を選ぶ KESCMOversetScanTargetDB も同じヘッダーに出ている。)

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

// マークの色の選択。★★2026-08-24: 背景による自動切り替え(赤い下地の上だけシアン)を廃止して
// これに置き換えた。Pixel の枠と Story の色地の両方に効く(どちらも SelectedMarkColor を通る)。
void		KESCMDoSetMarkColor(bool16 cyan, IDataBase* db);
bool16		KESCMGetMarkColorCyan();	// kFalse=赤(既定) / kTrue=シアン

// ★比較モード（2026-08-20）。定義は KESCMBoundaryID.h（境界に出る型なので両側が読む場所に置いた）。
//   ⚠**設定を変えるだけで、比較そのものはやり直さない**＝arm 中に変えたときの再比較は呼び手の仕事
//   （UI 側の KESCMSetCompareMode。ここに再比較まで書くと「モードを変える」と「比較する」が1つの
//   関数に混ざり、起動時の復元でも比較が走ってしまう）。
KESCMCompareMode	KESCMGetCompareMode();
void				KESCMSetCompareMode(KESCMCompareMode mode);

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

// (★widget に触る8本の宣言は、2026-08-13 の model/UI 分割 第1段 Task 5 で **KESCMUIShared.h** へ移した
//  ＝KESCMGetVisibleOwnPanel / KESCMRefreshPanel / KESCMSetStatus / KESCMSetNavPosition /
//  KESCMSetToolButtonSelected / KESCMActivateOwnTool / KESCMIsOwnToolActive / KESCMOpenAboutURL。
//  ★★**model 側のファイルが KESCMUIShared.h を include していたら、それが逆流**——という判定基準を
//  作るための分割で、今それを破っている箇所の全量は
//  docs/ai-notes/kescm-reverse-flow-ledger-2026-08-13.md に台帳化してある(Task 6〜10 で空にする)。)

// (★比較の開始/解除の6本(KESCMToggleStartStop / KESCMStopComparison / KESCMStartComparisonFor /
//  KESCMCanStartComparison / KESCMTogglePrintMarks / KESCMSetMarkOpacity25)の宣言は、2026-08-13 の
//  model/UI 分割 第1段 Task 4 で **KESCMComparisonRun.h** へ移した。実体も KESCMComparisonRun.cpp
//  (パネルのファイルに同居していたが、動かしているのはパネルではなく比較そのもの＝model 側)。
//  Facade が転送する先はこの6本になる。)

// (★ステータス文字列の保持(KESCMGetSessionStatus / KESCMClearSessionStatus)は、2026-08-13 の
//  model/UI 分割 第1段 Task 9 で **KESCMModelNotify.h** へ移した。実体も KESCMModelNotify.cpp
//  ＝**保持は model・表示は UI**(app.kcmStatus はパネルを閉じていても答えるので、記憶は model 側で
//  なければならない。設計書 §3.3)。)

// (Split Target on Start(KESCMGetSplitOnStart/KESCMDoSplitTarget)は 2026-07-04 撤去。
//  仕組みは docs/ai-notes/kescm-split-target-mechanism.md と git 履歴 69c4b07 に保存)

// (★フライアウト「Hide Unchanged Spreads」の宣言3本(KESCMResetHideUnchanged /
//  KESCMGetHideUnchangedDB / KESCMGetHideUnchangedSrcDB)は、2026-08-13 の model/UI 分割
//  第1段 Task 2 で **KESCMHideUnchanged.h** へ移した。実体も KESCMHideUnchanged.cpp で、
//  トグル本体(旧 KESCMActionComponent::DoHideUnchangedToggle)も一緒に移っている。)

// (★フライアウト「Sync Layout Views」トグル(KESCMGetLayoutSync / KESCMSetLayoutSync)と
//  「Align Other Views to Active」(KESCMAlignOtherViewsToActiveNow)の宣言は、2026-08-13 の
//   model/UI 分割 第1段 Task 1 で **KESCMViewSync.h** へ移した。実体も KESCMViewSync.cpp。
//   どちらも IControlView / IPanorama を相手にする UI 側の機能で、model からは呼べない。)

#endif // __KESCMCore_h__
