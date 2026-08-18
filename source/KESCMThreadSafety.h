//========================================================================================
//
//  KESCMThreadSafety.h
//
//  model プラグイン(kModelPlugIn)としてバックグラウンドスレッドから呼ばれても壊れないための道具。
//  ★KESCM が kModelPlugIn になった(第2段 Task 11)時点から、**描画イベントは BG にも配られる**
//    (Task 11C で実測)。ここに置くのは、その世界で必要になった3つだけ:
//
//    1. KESCMIsMainThread()  … 「今メインスレッドか」
//    2. KESCMIsSameDoc()     … ★**2つの IDataBase* が同じ文書を指すか**
//    3. KESCMMarkStateMutex()… マーク集合(sEntries ほか)を守るロック
//
//  ─────────────────────────────────────────────────────────────────────────────
//  ★★★なぜ生ポインタ比較ではいけないのか(第2段 Task 11C の実測)
//
//    ガイド vol1-07 L93 = "InDesign's multithreading environment provides a separate
//    execution context (**a cloned copy of the database**) for each thread."
//
//    実測(2026-08-15):
//      MAIN  db=…23FB4A80  sDB=…23FB4A80  entries=2 firstUID=258 class=1295
//      *BG*  db=…295BE390  sDB=…23FB4A80  entries=2 firstUID=258 class=1295
//
//    ⇒ ①BG の db は**クローンの別ポインタ**で sDB とは必ず食い違う
//       ②しかし**UID はクローンをまたいで保たれる**(BG の db で引いても同じページが返る)
//       ③static はスレッド間で**共有される**(entries=2 が両方から見えている)
//
//    ∴ 「同じ文書か」は**ファイル(IDataBase::GetSysFile)で聞く**のが正しい。
//      ★これは [[uidref-reuse-after-close]](閉じた文書のポインタはアドレス再利用で別文書と
//        一致してしまう)と**同じ結論**＝1つ直すと2つ直る。
//
//  ─────────────────────────────────────────────────────────────────────────────
//  ★公式の手本(2026-08-15 に SDK を実測して確認したもの。真似ないと後で流儀が割れる)
//
//    ・boost::mutex を static メンバで持ち scoped_lock で守る
//        = sdksamples/hyphenator/HypPerformanceData.h:29,64 と .cpp:39,53,61
//          (vcxproj も `$(MODEL_PLUGIN_LINKLIST);$(BoostThreadLib)` = KESCM と同じ形)
//    ・スレッドごとの値(再入防止など)は IDThreading::ThreadLocal / ThreadLocalManagedObject
//        = open/components/incopyfileactions/InCopyDocFileHandler.cpp:261
//          (★**IDataBase* のリストを再入防止のためスレッドローカルで持つ**＝KESCM の
//            ラスタ化中フラグと同じ用途。命名も tl_ プレフィックス)
//    ・UI 側は同期を一切書かない
//        = open/components/linksui と layerpanel に IsMainThreadDomain も mutex も **0件**
//          ⇒ **直すのは model 側だけ**でよい、という切り分けの裏づけ。
//
//========================================================================================
#ifndef __KESCMThreadSafety_h__
#define __KESCMThreadSafety_h__

#include "BaseType.h"		// bool16, kTrue/kFalse

#include <boost/thread/recursive_mutex.hpp>	// 公式の手本 = hyphenator/HypPerformanceData.h:29(あちらは非再帰)

class IDataBase;

//----------------------------------------------------------------------------------------
// 今メインスレッドか。IDThreading::IsMainThreadDomain() の薄いラッパ(bool → bool16)。
// ★「BG では何もしない」で正しいのは**後始末(状態を捨てる側)だけ**。描く側を BG で止めたら
//   第2段の目的そのもの(PDF 書き出しにマークを出す)を止めることになる。
//----------------------------------------------------------------------------------------
bool16 KESCMIsMainThread();

//----------------------------------------------------------------------------------------
// 2つの IDataBase* が「同じ文書」を指すか。
//   ・同一ポインタなら真(メインスレッドの通常経路はここで即決 = コスト増ゼロ)
//   ・違うポインタでも、GetSysFile() が同じファイルを指していれば真
//     ⇒ ★**バックグラウンドのクローン DB でも真になる**(これが本題)
//   ・**ファイルが片方でも無いときは `IDataBase::GetDocumentID()` で聞き直す**(下の★)
//   ・どちらかが nil のとき、または ID が空のときだけ偽
//
//     ★★★**未保存文書の道は 2026-08-18(不具合再検査 B9)に実装した。** それまでは
//       「ファイルが無い＝偽」で終わっていたので、**一度も保存していない2文書を比較すると
//       BG(PDF の非同期書き出し)でマークが1つも出なかった**——画面には出るので
//       **「画面と書き出しが食い違う」**形で、第2段が塞いだはずの穴が未保存文書にだけ残っていた。
//       (⚠旧コメントは「分割前から出ていなかったので劣化ではない」と書いていたが、それは
//        **直さない理由にはならない**。実際 B9 の再検査でそのまま不具合として拾い直した。)
//
//     ★根拠は 2026-08-16・API 監査 B9 の実測4点(未保存文書2つを比較して非同期 PDF 書き出し):
//         ①**未保存文書にも値が付く**——`GetSysFile()` が nil でも `xmp.did:…` が返る
//         ②**Target と Source は別の値**を持つ(a3097be8… ⇔ 6322d72a…)＝**同一性の判定に使える**
//         ③★**BG のクローン DB でも main と完全に一致**(db ポインタは違うのに ID は同じ)
//         ④`sDB` は BG でも main が入れたポインタのまま(static 共有の再確認)
//
//     ⚠**寄せ先が2つあり、内部用の `IDataBase::GetDocumentID()` を選んだ**(2026-08-18・ユーザー判断)。
//       同メソッドは自ら "FOR INTERNAL USE ONLY / FOR EXTERNAL USE : Recommended to use
//       IAdobeMediaMgmtMetaData::GetDocumentID" と書いている(`IDataBase.h:715-717`)。それでも選ぶ理由:
//         ・★**Adobe 製 linksui が同じ内部用の口を使っている**(`ClosingDocumentsResponder.cpp:118`)
//           ——しかも**用途まで同じ**(閉じる文書の同一性キー。あちらは path と ID を連結する)
//         ・`IDataBase*` から直接引ける＝**描画イベントの中から呼べる**(外部用は XMP 経由で
//           Query が要り、nil 経路が増える)。ここは毎描画・毎ページから来る道。
//       ⇒ 外部用へ寄せるなら `IAdobeMediaMgmtMetaData.h` / `SnpPerformXMPCommands.cpp` が入口。
//       全文＝`docs/ai-notes/kescm-api-audit-b9-2026-08-16.md` と `kescm-bug-recheck-b9-2026-08-18.md`
//
//     ⚠**コストは未保存のときだけ増える**: 保存済み文書は従来どおりファイル比較で終わり、
//       main の通常経路はそもそも同一ポインタで即決する(この関数の1行目)。
//----------------------------------------------------------------------------------------
bool16 KESCMIsSameDoc(IDataBase* a, IDataBase* b);

//----------------------------------------------------------------------------------------
// KESCM の共有状態(マーク集合 sEntries／登録ページ集合／✓集合)を守るロック。
//
// ★守る理由 = どれも **main が書き、BG(PDF の非同期書き出し)が描画で読む** から:
//   ・sEntries は**生ポインタの map** で DropAll() が delete する
//     ⇒ BG が読んでいる最中に main が Stop すると**解放済みメモリの読み取り**
//   ・登録ページ/✓ の集合(KESCMDocUidSet)は std::map/std::set
//     ⇒ main が insert して木を回転している最中に BG が find すると壊れる
//   (ガイド vol1-07 L95: "InDesign will behave inconsistently and **may randomly crash**")
//
// ★★**守っていない共有状態が2つある。安全なのは「BG が読まないから」で、それを担保しているのは
//   && の項の順番だけ**(2026-08-17 の不具合再検査 B6 で数えた):
//     ・`sOversetPages`(std::set) / `sOversetLocs`(std::vector) …… main の KESCMApplyOversetForDoc が
//       **swap() で丸ごと入れ替える**。形は上の2つと同じで、BG が読んでいる最中に入れ替われば壊れる。
//   ⇒ **読む側が Pages パネルのサムネイルしかない**ので今は届かない:
//       KESCMDrawEventHandler.cpp の `wantOversetThumb = isThumb && sOversetOn && sOversetDB != nil
//       && !sOversetPages.empty()` ---- ★**`isThumb` が第1項**なので、BG(PDF の非同期書き出し＝
//       isThumb 偽)では**短絡評価で set に一度も触れない**。`count()` を引く描画ブロックも同じ枝の中。
//   ⚠∴ **これは設計された防御ではなく、条件式の並び順が結果的に守っている状態。** 「＋」をサムネイル
//     以外(カンバス・印刷・書き出し)へ出す日が来たら、その瞬間にこの2つをロックの対象へ載せること。
//     判定の順番を入れ替えるだけでも同じ。
//
// ★★★**上の「2つ」は"守っていない集合"の数であって、"守られていない読み"の数ではなかった**
//   (2026-08-18・不具合再検査 B9)。**守っている集合を、守っていない場所で読んでいた**のが別に1つある:
//     ・`HandleDrawEvent` 冒頭の `anyMarkableContent` …… `sEntries.empty()` と
//       `sOverflowT/sOverflowS.empty()` を**ロックの外**で読んでいた。**BG も必ず通る行**で、
//       同じ集合を main は `DropAll()`(delete+clear) / `MakeEntry`(insert) / `swap()` と
//       **全部ロック下で書いている**。つまり**書き手だけが守り、読み手の1つが外に居た**
//       ＝ B3 §5 が踏んだ「捨てる側だけ守るのは無意味」の裏返しの形。
//   ⇒ 2026-08-18 にその計算をロックで囲んだ。**追加コストは実質ゼロ**(同じ関数の下で
//     どのみちロックを取る・recursive なので入れ子でも詰まらない)。
//   ★**教訓＝「守っている/いない」は集合ではなく"触る場所"で数える。** 集合の名前で数えると、
//     同じ集合の3か所目の読みが視界に入らない。
//
// ★★**recursive_mutex にしてある理由**(2026-08-15 に実際に踏みかけた):
//   描画ループは自分でロックを取ったうえで、その中から KESCMPageMapIsRegistered() /
//   KESCMPageCheckIsChecked() を**ページごとに**呼ぶ。呼ばれる側もロックを取るので、
//   非再帰の boost::mutex(公式 hyphenator と同型)だと**同じスレッドで二重ロック＝即デッドロック**になる。
//   ⇒ 「呼び手がロック済みかどうかを気にしなくてよい」ことを優先して再帰ロックにした。
//   ⚠代償: ロックの入れ子が深くなっても気づけない。**ロックしたまま長い処理をしない**という
//     規律は変わらない(描画1スプレッド分＝数ms が上限の目安)。
//
// 使い方:  KESCMMarkStateLock lock(KESCMMarkStateMutex());
//----------------------------------------------------------------------------------------
boost::recursive_mutex& KESCMMarkStateMutex();
typedef boost::recursive_mutex::scoped_lock KESCMMarkStateLock;

#endif // __KESCMThreadSafety_h__
