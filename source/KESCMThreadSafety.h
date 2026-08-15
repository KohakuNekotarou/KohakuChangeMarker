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
//   ・どちらかが nil、または未保存でファイルが無いときは偽
//     ⚠ 未保存文書は BG では同一と判定できない(＝BG ではマークが出ない)。これは
//       分割前から出ていなかったので劣化ではない。★直すならファイル以外の同一性の口が要る。
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
