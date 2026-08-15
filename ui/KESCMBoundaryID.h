//========================================================================================
//
//  KESCMBoundaryID.h
//
//  model（KohakuExtendScriptChangeMarker）と UI（KohakuChangeMarkerUI）の
//  **両方が同じ値で知っていなければならない ID** だけを集めたヘッダー。
//
//  ★★★このファイルは両側のフォルダに同じ内容で置いてある。
//       相方 = source/sdksamples/KESCM/{source|ui}/KESCMBoundaryID.h
//       ⚠ **どちらかだけ直すと黙ってずれる。必ず両方直すこと。**
//       （Adobe も同じ形: customconditionaltext/CusCondTxtRezDefs.h ⇄ customconditionaltextui/ の同名）
//
//  ★★なぜ「両側にコピー」で「同じ値」なのか
//
//    ID の一意性はプラグイン単位ではなく **値** で決まる。ここにあるのは「片方が書いて片方が読む」
//    3種類だけで、どれも値が食い違った瞬間に **黙って何も起きなくなる**（ビルドは通る）:
//
//      ・Facade の IID    … model が kUtilsBoss へ AddIn し、UI が Utils<IKESCMxxx>() で引く
//      ・protocol IID     … model が ISubject::Change の引数に使い、UI が AttachObserver に使う
//      ・通知の MessageID … model が送り、UI が Update() で振り分ける
//
//    ⇒ **prefix はどちらのコピーでも model 側の 0x1EA500 のまま**。UI 側が自分の 0x1EA580 で
//      名乗り直すと、model が送ったものを UI が受け取れなくなる。
//
//  ⚠ ここに置いてよいのは境界の ID だけ。**model 専用は KESCMID.h、UI 専用は KCMUIID.h。**
//
//  Created 2026-08-15 for the model/UI split (Stage 2, Task 6B).
//
//========================================================================================

#ifndef __KESCMBoundaryID_h__
#define __KESCMBoundaryID_h__

#include "SDKDef.h"

//----------------------------------------------------------------------------------------
// model 側の prefix。
//
// ★**両側のコピーともこの値**（UI 側の KCMUIID.h は自分用に kKCMUIPrefix を別に持つ）。
//   Adobe が発行した帯は 0x1EA500 - 0x1EA5FF の 256 枠で、model 0x1EA500 / UI 0x1EA580 と
//   前半・後半に割ってある。発行の経緯と帯の割り方の全文は **KESCMID.h の長いコメント**にある。
//
// ★kKESCMStringPrefix もここに置く。**文字列キーはグローバルに一意でなければならない**
//   （ガイド vol2-12:71。widget ID と違って借用できない）ので、UI へ移る文字列キーも
//   `kKESCMStringPrefix "..."` の形のまま動かす ＝ キーの値が1つも変わらず、
//   文字列テーブルは丸ごと移すだけで済む。
//----------------------------------------------------------------------------------------
#define kKESCMPrefixNumber	0x1EA500
#define kKESCMPrefix		RezLong(kKESCMPrefixNumber)				// The unique numeric prefix for all object model IDs for this plug-in.
#define kKESCMStringPrefix	SDK_DEF_STRINGIZE(kKESCMPrefixNumber)	// The string equivalent of the unique prefix number for this plug-in.

//----------------------------------------------------------------------------------------
// Facade の InterfaceID — UI が model に頼む窓口（kUtilsBoss へ AddIn。model/UI 分割 第1段）
//
// ★手本 = sdksamples/customconditionaltext の IID_ICUSCONDTXTFACADE。
// ⚠ AddIn する実装は必ず自作（SDK 提供の実装を既存 boss に足すと他社と衝突して起動に失敗する。
//   衝突の単位は IID ではなく ImplementationID）。
//----------------------------------------------------------------------------------------
DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMCOMPAREFACADE, kKESCMPrefix + 4)	// 比較エンジンに頼む（第1段 Task 11）
DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMMARKDATA, kKESCMPrefix + 5)	// 比較結果を**読む**（第1段 Task 12。★読み取り専用＝マークを作るのは上の1か所だけ）
DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMPAGEFLAGSFACADE, kKESCMPrefix + 6)	// Register(Added/Removed)と Check(✓)を書き換える（第1段 Task 13）
DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMSTORYEDITSFACADE, kKESCMPrefix + 7)	// Story Edits の一覧を**読む**（第1段 Task 14。★読み取り専用）
DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMBOOKFACADE, kKESCMPrefix + 8)	// ブック比較を頼む（第1段 Task 15。境界の5本目＝最後）

//----------------------------------------------------------------------------------------
// 通知の protocol IID
//
// model が UI へ知らせる唯一の向き。★model は UI に何があるか知らない ---- アプリの subject に
// 自作 protocol IID で Change を投げるだけで、誰も聞いていなければ何も起きない
// （＝InDesign Server でも安全）。UI 側はこの IID で AttachObserver する。
//----------------------------------------------------------------------------------------
DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMMODELCHANGEOBSERVER, kKESCMPrefix + 9)	// 第1段 Task 9

//----------------------------------------------------------------------------------------
// 通知の MessageID — model が UI へ「何が変わったか」を知らせる（第1段 Task 9）
//
// ★kMessageIDSpace は KESCM がこれまで1つも使っていなかったので +0 から採る。
//   受け手は UI 側の1本の Observer（KESCMModelChangeObserver）だけで、changeID で振り分ける。
//----------------------------------------------------------------------------------------
DECLARE_PMID(kMessageIDSpace, kKESCMMarksRebuiltMessage,      kKESCMPrefix + 0)	// 比較が走ってマークが作り直された（Prev/Next の位置・スクロール地図・Pages サムネイル・Story Edits の一覧が対象）
DECLARE_PMID(kMessageIDSpace, kKESCMMarksClearedMessage,      kKESCMPrefix + 1)	// Stop でマークが消えた
DECLARE_PMID(kMessageIDSpace, kKESCMPageFlagsChangedMessage,  kKESCMPrefix + 2)	// Register(Added/Removed)または Check(✓)が変わった
DECLARE_PMID(kMessageIDSpace, kKESCMStoryEditsRebuiltMessage, kKESCMPrefix + 3)	// Story Edits のモデルが作り直された
DECLARE_PMID(kMessageIDSpace, kKESCMStatusTextMessage,        kKESCMPrefix + 4)	// ステータス行の文字列が変わった（文字列自体は Facade の GetSessionStatus で取る）
DECLARE_PMID(kMessageIDSpace, kKESCMOversetRescannedMessage,  kKESCMPrefix + 5)	// overset の走査結果が更新された
DECLARE_PMID(kMessageIDSpace, kKESCMComparisonDocsClosedMessage, kKESCMPrefix + 6)	// ★比較していた文書が閉じられ、Stop 相当の後片付けが済んだ（第1段 Task 10）。
																					// ⚠**Stop（kKESCMMarksClearedMessage）とは別**にした理由＝UI 側の後始末が3点違う:
																					//   ①サムネイルの作り直しは**次の idle へ遅延**させる（前面切替の過渡で ForceRedraw が
																					//     効かず枠が残る＝2026-07-08 実機で確認）②**一括クローズ中は保留**して全部閉じ終えて
																					//     から1回だけ流す ③Find Overset が単独 ON 中なら strip は**残す**（赤帯だけ描き直す）。
																					//   ★付随データ＝**生存している側**の db を最大3つ（Target/旧版/Source側枠）。閉じた db は
																					//     決して渡さない（通知の受け手が deref するため）。

#endif // __KESCMBoundaryID_h__
