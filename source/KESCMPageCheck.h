//========================================================================================
//
//  KESCMPageCheck.h
//
//  「KCM: Check」機能の入口。ページパネルのページを選択→右クリックのトグル「KCM: Check」で、
//  そのページに「チェック済み」印を付け外しする。チェックすると、そのページの Pages パネル
//  サムネイル中央に青い ✓(ベクター線描画=フォント非依存)を表示する。
//
//  ・登録(KESCMPageMap の Added/Removed)とは完全に独立した別の集合(用途=ユーザーの任意の目印)。
//  ・Start 中(arm 済み)かつ選択文書が Target/Source のときだけ操作可(メニューはそれ以外グレーアウト)。
//  ・セッション内のみ(文書ファイルには保存しない=dirty にもならない)。Stop で全消去(忘れる)。
//  ・✓ の描き先は2つ(いずれも KESCMDrawEventHandler): ① Pages パネルのサムネイル(isThumb 分岐)、
//    ② レイアウトビューのページ中央(2026-07-12 追加。かなり大きい ✓。Target/Source とも画面では常時表示、
//    印刷/PDF は「Print comparison marks」ON のときのみ。不透明度はパネルの 25%/75% 選択に連動)。
//
//========================================================================================
#ifndef __KESCMPageCheck_h__
#define __KESCMPageCheck_h__

#include "BaseType.h"		// int32, bool16
#include "OMTypes.h"		// UID
#include <map>				// KESCMPageCheckPruneToMarked の「外したページ」(文書ごと)
#include <set>

#include "KESCMPageMap.h"	// KESCMPageToggleState ---- Register と共通の答えの形(型のためだけ)

class IDataBase;

// ページパネル右クリックのトグル「KCM: Check」の実行。選択ページのチェックを付け外しする
// (1つでも未チェックがあれば全チェック、全部チェック済みなら全解除)。結果はパネルのステータス行に
// 出す。トグルしたページのサムネイルは即更新して ✓ を反映する。実体は KESCMPageCheck.cpp。
void KESCMPageCheckToggleSelectedPages();

// 上のトグル(kCustomEnabling)が今どう見えるべきか。fEnabled=有効/無効(未 Start・第3文書・選択なし・
// 選択にマーク付きページが無ければグレー)、fTick=全部チェック済みなら All / 一部なら Some。
// ⚠**fRole は使わない**(Check のラベルは固定)。★メニューに触らないのは Register 側と同じ。
KESCMPageToggleState KESCMPageCheckGetToggleState();

// ドキュメントクローズ後の生存スイープ(KESCMHandleDocsClosed から呼ぶ)。閉じた文書のチェックを
// 状態だけ捨てる。★閉じた db は deref しない(FindDocByDataBase へのポインタ比較のみ)。
void KESCMPageCheckSweepClosedDocs();

// 全文書のチェックを丸ごと忘れる。Stop(KESCMDoClearMarks)で呼び、比較を解除したらチェックも
// 残さない。ポインタは触らず map を空にするだけ(deref なし=安全)。
void KESCMPageCheckClearAllDocs();

// 再比較後に呼ぶ。各文書のチェック済みページのうち、もうマーク(枠/「/」)を持たなくなったページの
// チェックを外す(ユーザー指定 2026-07-11:「枠が無くなったらチェックの記憶も外れる」)。マーク集合は
// KESCMCollectChangedPageUIDs で引く(db が比較対象=sDB/sSrcDB でなければ空=その文書のチェックは
// 全部外れる)。KESCMDoMarkChangesDoc の末尾(サムネイル更新の前)から呼ぶ。
//
// ★outUnchecked(任意, nil可) = **実際に外したページ**を文書ごとに追記する(2026-08-16・API 監査 B5)。
//   ⚠**サムネイルを per-UID で作り直す呼び手には必須**——✓ が外れたページは絵が変わるのに、
//     外れた後は「今チェックが付いている集合」のどこにも居ないので**現在状態から復元できない**
//     (B4 が Register/✓ のトグルで踏んだのと同じ形。KESCMModelNotify.h の fPagesA 参照)。
//   ★ここが答えるのは「この prune が外した分」だけで、呼び手が自分で触ったページとは別物。
//     ∴ 呼び手は**自分の集合に足す**のであって、置き換えてはいけない。
//   ⚠キーの IDataBase* は deref しない(集合の持ち主を指すだけ)。呼び手も deref する前に
//     生存を確かめること(この関数は「閉じた文書のチェック」も掃除の対象にする)。
void KESCMPageCheckPruneToMarked(std::map<IDataBase*, std::set<UID> >* outUnchecked = nil);

// pageUID(db内)がチェック済みか。db が nil、または該当文書のチェックが無ければ kFalse。
// 描画側(KESCMDrawEventHandler の isThumb 分岐)が ✓ を描くかの判定に使う。
bool16 KESCMPageCheckIsChecked(IDataBase* db, UID pageUID);

// db にチェック済みページが1つでもあるか(存在チェックのみ)。描画側の早期 return 判定
// (KESCMDrawEventHandler::HandleDrawEvent の anyMarkableContent)に使う。
bool16 KESCMPageCheckHasAny(IDataBase* db);

// フライアウト「Save Check & Register」: Start 中(arm 済み)の Target/Source について、現在の Check(✓)と
// Register(Added/Removed=緑「/」)の両方を独自 JSON(ローミング環境設定フォルダー直下の KESCMPageChecks.json,
// version 2)へ保存する。キーは文書ファイルのフルパス、値は checks[] と registered[] の2配列。既存ファイルに
// 「マージ」する(今 Start 中の2文書ぶんだけ更新し、他の文書の保存済み分は温存)。保存先パスをステータス行に
// 出す。未 Start や未保存文書(パス無し)は対象外。実体は KESCMPageCheck.cpp。
void KESCMPageCheckSaveToFile();

// フライアウト「Load Check & Register」: Start 中(arm 済み)だけ有効。上記 JSON を読み、Target/Source について
//   ①まず Register を両文書へ適用(KESCMPageMapReplaceRegistered)→ 一度だけ再比較(除外対応表を張り直し、
//     Added/Removed の緑「/」サムネイルも更新)
//   ②その後 Check を復元(保存済みのうち「今もマーク付き(枠/「/」)」のページだけ。その文書の現在のチェックを置換)
// の順に処理する。復元件数をステータス行に出す。旧 v1 ファイル("pages" 配列)は checks として寛容に受理する。
// 未 Start なら何もしない。実体は KESCMPageCheck.cpp。
void KESCMPageCheckLoadFromFile();

#endif // __KESCMPageCheck_h__
