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
//  ⚠ ここに置いてよいのは境界の ID と、**両側が同じ定義で知っていなければならない型**だけ。
//    **model 専用は KESCMID.h、UI 専用は KCMUIID.h。**
//    ★2026-08-20 に **KESCMCompareMode（enum）** を追加した。ID ではないが、置く理由は ID と同じ
//      ＝**UI が書き、model が読んで動きを変える値**で、定義が片側にしか無いと、もう片方が
//      「相手のフォルダのヘッダー」を読むことになる。
//
//  Created 2026-08-15 for the model/UI split (Stage 2, Task 6B).
//
//========================================================================================

#ifndef __KESCMBoundaryID_h__
#define __KESCMBoundaryID_h__

#include "SDKDef.h"

//----------------------------------------------------------------------------------------
// 両側が同じ値で名乗る**表示文字列**（ID ではないが、食い違うと製品が2つに見える）
//
// ★★2026-08-15（第2段 Task 6B-2）にここへ集約した。理由は [[one-question-one-place]]:
//   版数と表示名は **1 つの製品の事実**で、model と UI が別々に持つと必ずずれる。
//   実際、KCMUI は雛形の kSDKDefPluginVersionString を名乗ったまま Task 2〜6 を通ってきた
//   ＝プラグイン一覧に **別の版数の別プラグイン**として並んでいた。
//
//   使い手: 両側の .rc（FileDescription / FileVersion）／両側の .fr（PluginVersion・
//   ExtraPluginInfo・About 文字列・メニュー束ね名）／両側の KESCMLoc.h（kKESCMAltKeyName）。
//
// ⚠ kKESCMFileName（出力 .pln 名）は**側ごとに違う**のでここには置かない
//   ---- model は KESCMID.h、UI は KCMUIID.h が自分のファイル名を持つ。
//
// ★★2026-08-15（第2段 Task 11）に **kKESCMPluginName もここへ来た**。UI 側が
//   `PluginDependency` リソースで「自分は model プラグインが無ければ意味を成さない」と宣言する
//   のに、**依存先の内部名と PluginID が要る**ため（ガイド gs-03:55）。
//   ⇒ 相方の名前を名乗る以上、これは境界の情報になった。
//----------------------------------------------------------------------------------------
#define kKESCMCompanyKey	"KohakuNekotarou"	// Company name used internally for menu paths and the like. Must be globally unique, only A-Z, 0-9, space and "_".
#define kKESCMCompanyValue	"KohakuNekotarou"	// Company name displayed externally.
#define kKESCMDisplayName	"Kohaku Change Marker"	// 表示名(About メニュー項目・About ボックス本文・パネル/ツール名)。KBS の "Kohaku Search Panel" に合わせ、単語間をスペースで区切る(2026-07-25)。
#define kKESCMVersion		"1.6.0"				// ★製品の版数（model と UI で必ず同じ）。About ボックス本文・両側の .rc の FileVersion・両側の PluginVersion リソースに出る。履歴と「次に提出する分」の増分は **KESCMID.h の長いコメント**が正本。

// ★プラットフォーム別の修飾キー表記（2026-07-25 追補 Mac 対応）。
//   実装側は SDK の IEvent が差を吸収する（OptionAltKeyDown = Win の Alt / Mac の Option、
//   CmdKeyDown = Win の Ctrl / Mac の Command）ので、切り替えるのは「ユーザーに見せる名前」だけ。
//   この定数は文字列リテラルなので、.fr の StringTable でも C++ でも隣接連結でそのまま埋め込める
//   （例: "Hold Left + " kKESCMAltKeyName "="）。MACINTOSH は Mac ビルドの xcconfig
//   （GCC_PREPROCESSOR_DEFINITIONS）と odfrc の双方で定義される。
// ★**両側に要る**: UI 側の How to Use 本文（**KCMUI_enUS.fr** の kKESCMHintKey と ui/KESCMLoc.h の
//   日本語版）と、model 側の KESCMLoc.h が同じ表記を使う。
//   ⚠2026-08-16（監査 B-U1）に "KESCM_enUS.fr" を訂正した＝文字列テーブルは 2026-08-15 の分割で
//     UI 側へ丸ごと移っており、**この行だけが移る前の名前を指したまま**だった。
#ifdef MACINTOSH
#define kKESCMAltKeyName	"Option"
#else
#define kKESCMAltKeyName	"Alt"
#endif

// model プラグインの内部名（ID 系・.rc の InternalName）。互換のため据え置き。
// ★UI 側は `PluginDependency` でこの名前を名乗る（下の kKESCMPluginID と対）。
#define kKESCMPluginName	"KohakuExtendScriptChangeMarker"

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

// model プラグインの PluginID。
// ★★2026-08-15（第2段 Task 11）に KESCMID.h からここへ移した＝**UI 側が `PluginDependency` で
//   依存先として名指しする**（ガイド gs-03:55「UI プラグインは model が無ければ意味を成さない」／
//   手本＝`transparencyeffectui/TranFxUI.fr:77-86`）。⇒ 両側が同じ値で知る必要がある。
// ⚠ UI 自身の PluginID は `kKCMUIPluginID`（KCMUIID.h）で別物。**依存の向きは UI → model の一方向**
//   なので、model 側が KCMUI の PluginID を知る必要は無い（知ったらそれ自体が逆流）。
DECLARE_PMID(kPlugInIDSpace, kKESCMPluginID, kKESCMPrefix + 0)

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

//----------------------------------------------------------------------------------------
// 比較モード（2026-08-20）
//----------------------------------------------------------------------------------------
// ★**比較で何をするか**を決める値。パネルのフライアウトで選び、model が持つ（値を読んで実際に
//   走らせるのが model なので、置き場所も model 側＝IKESCMCompareFacade の Get/Set）。
//
// ⚠**ID ではなく型なのでここに置いた。** このヘッダーは model と UI の**両方**が include する
//   唯一の場所（IID を配るために両側が読む）で、境界に出る型はここに在るのがいちばん安全＝
//   どちらか片側のヘッダーに置くと、もう片方が「相手のヘッダー」を読むことになる。
//
// ★★**2つの結果を同時に持たない。** モードを変えたら比較はやり直す。両方の結果を保持すると
//   「いま画面は何を見せているのか」の答えが2か所に生まれる（[[one-question-one-place]]）。
enum KESCMCompareMode
{
	kKESCMModePixel = 0,	// 既定。ページをラスタ化して画素を比べる（KESCM 本来の比較）
	kKESCMModeStory = 1		// ストーリーの本文を段落→文字の二段で比べる（2026-08-20 追加）
};

#endif // __KESCMBoundaryID_h__
