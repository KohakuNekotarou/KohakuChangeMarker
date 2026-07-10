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

// オーバーレイ全体(と旧版画像のキャッシュ)を破棄し、db を再描画する。
void		KESCMDoClearMarks(IDataBase* db);

// マークを印刷に出すか(かつ画面に常時表示するか)と、枠の不透明度の選択を切り替える。
// opacity25Flag: kTrue=25% / kFalse=75%(ミドル押下表示・印刷ON常時表示・印刷出力に共通)。
void		KESCMDoSetPrintMarks(bool16 printFlag, bool16 opacity25Flag, IDataBase* db);

// 旧版のミドルボタン peek を arm / disarm する(パネルの ON/OFF 状態も駆動する)。
void		KESCMDoArmMousePeek(IDataBase* targetDB, IDataBase* sourceDB);
void		KESCMDoDisarmMousePeek(IDataBase* db);

// パネルの状態アクセサ。"Armed" == Start ボタンが実行済みで Clear がまだ、の状態。arm 中はパネルが
// Target/Source 名と ON アイコンを表示し、それ以外では名前を隠して OFF を表示する。
bool16		KESCMIsArmed();
IDataBase*	KESCMArmedTargetDB();
IDataBase*	KESCMArmedSourceDB();

// 現在の印刷マーク設定。パネルを開き直したときにチェック/ラジオを実状態へ復元するために使う。
bool16		KESCMGetPrintMarks();		// 印刷マーク ON/OFF
bool16		KESCMGetMarkOpacity25();	// 枠不透明度の選択: kTrue=25% / kFalse=75%

// ★既知の制限(2026-07-05調査済・対応しないことを決定): ページパネルのサムネイルは「文書の変更」
// でしか無効化されない内部キャッシュ(ページタブアイコンサイズ別に別キャッシュ)を持っており、KESCM の
// 枠は文書を変更しないため、既に一度描画済み・表示中のサムネイルは古いまま残る(比較/Clear/印刷トグル
// 等の直後)。試して効果が無かったもの: サムネイル設定 OFF→ON の全体トグル、
// IPagesSubPanelController::InvalidatePageWidget/InvalidateSpreadWidget、UpdatePagesPanel の
// bForcePurge、IControlView::ForceRedraw。唯一効くのは本物のドキュメント編集(実証済み)だが、
// ICmdHistory 経由でも Redo 履歴を汚さずに済ませる安全な手段が無く、見送りとした。メインのレイアウト
// 表示への枠描画(KESCMDrawEventHandler)は本件と無関係に正常動作する。

// ドキュメントがクローズされた直後(kAfterCloseDoc レスポンダ)に呼ぶ。追跡中の全DB(マーク/旧版画像/
// peek arm)を IDocumentList で生存確認し、閉じていたものだけ確定的にクリーンアップする
// (DropAll/DropAllOrig/無音 disarm)。片付けが起きたらパネルも ON→OFF 更新する。
// どの db が閉じたかは信号から取れない(AfterClose では UIDRef 無効)ため、生存スイープで判定する。
// 実体は KESCMPeek.cpp(peek の file-local 状態にアクセスできる唯一の場所)。
void		KESCMHandleDocsClosed();

// 現在表示中のパネルがあれば、その ON/OFF 表示(Target/Source 名・アイコン・トグルラベル)を
// 現在の arm 状態(KESCMIsArmed 等)に合わせて更新する。パネルが隠れていれば何もしない
// (再表示時に AutoAttach が反映する)。実体は KESCMPanelObserver.cpp。
void		KESCMRefreshPanel();

// 比較の開始/解除トグル(旧パネルの Start/Stop ボタン→2026-07-10 フライアウトメニュー化)。
// arm 済みなら解除(マーク消去+peek 解除)、未 arm なら開始(アクティブ文書=Target・別の開いている
// 文書=Source を解決して比較+peek arm)。実行後に KESCMRefreshPanel でパネル表示を更新する。
// フライアウト項目 kKESCMPopupStartStopActionID の DoAction から呼ぶ。実体は KESCMPanelObserver.cpp。
void		KESCMToggleStartStop();

// 印刷マーク ON/OFF トグル(旧パネルのチェックボックス→2026-07-10 フライアウトメニュー化)。
// 現在の印刷フラグ(KESCMGetPrintMarks)を反転し、不透明度は現在の選択(KESCMGetMarkOpacity25)を維持して
// KESCMDoSetPrintMarks を呼ぶ。フライアウト項目 kKESCMPopupPrintMarksActionID の DoAction から呼ぶ。
// 実体は KESCMPanelObserver.cpp。
void		KESCMTogglePrintMarks();

// パネルのステータス行を更新する(KESCMPanelObserver::SetStatus と同じ処理を自由関数として公開)。
// パネルが隠れていてもセッション状態は覚えておき、再表示時に復元する。forceRedrawNow=kTrue なら、
// この後にブロッキング処理が続く場合でも次のイベントループを待たずに今すぐ描画する
// (KESCMDoMarkChangesDoc の比較ループ前の busyMsg 表示に使う)。実体は KESCMPanelObserver.cpp。
void		KESCMSetStatus(const PMString& s, bool16 forceRedrawNow = kFalse);

// パネルが非表示(閉じている、またはアイコン化/最小化されている)なら表示する。何もしなくても既に
// 見えている場合は何もしない。フォーカスは奪わない(giveKeyFocus=kFalse; ミドルボタン操作の途中で
// 呼ばれるため)。実体は KESCMPanelObserver.cpp。
void		KESCMEnsurePanelShown();

// パネルの一時表示(CMYK比較=3キー+ミドルの押下中だけ)。Begin=押下時: 非表示/アイコン化なら表示して
// 復元待ちにする(既に見えていれば何もしない)。End=ミドル解放時: 元の状態(閉じていた→閉じ直す/
// アイコン化→タブペインを Icon へ戻す)に復元する(復元待ちでなければ無害な no-op)。
// 実体は KESCMPanelObserver.cpp(アイコン判定・復元は PaletteRefUtils)。
void		KESCMPanelTempShowBegin();
void		KESCMPanelTempShowEnd();

// パネルのイラスト(ON/OFF アイコン)をクリックしたときに呼ぶ。「このプラグインについて」に載せている
// 配布元URL(kKESCMRepoURL, KESCMID.h)を既定のブラウザで開く。実体は KESCMActionComponent.cpp。
void		KESCMOpenAboutURL();

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

// フライアウト「Sync Layout Views」トグル(レイアウトビュー同期)。ON の間、どれかのレイアウトビューを
// スクロール/ズームすると、その表示(座標+拡大率)を他のドキュメントの全レイアウトビューへ自動で複製する
// (同一文書のビュー=スプリット相方は対象外。Alt+ミドルと同じ同期エンジン)。Start(枠)とは無関係に
// 単独で ON にできる。実体は KESCMPeek.cpp(オブザーバと同期エンジンの状態がある場所)。
bool16		KESCMGetLayoutSync();
void		KESCMSetLayoutSync(bool16 on);

#endif // __KESCMCore_h__
