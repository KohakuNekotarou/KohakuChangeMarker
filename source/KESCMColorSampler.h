//========================================================================================
//
//  KESCMColorSampler.h
//
//  ツール Alt+左クリック(旧・中ボタン Shift＋Ctrl＋Alt＋ミドル)で、クリック点の CMYK 生値を
//  「マウスが乗っている側(hover)」と「比較相手(other)」でサンプリングして2行の文字列に組む。
//  クリック点まわりの極小領域だけを高dpi・CMYK でラスタ化して中心1画素を読む。
//
//  ★2026-07-26: 入口を target/source 固定から hover/other へ一般化した(ユーザー指定)。
//  Start 中は Target 窓だけでなく **Source 窓の上でも** 比較2行を出すため、「どちらの文書を
//  マウスが指しているか」で向きが変わる。1行目は必ず hover 側=マウスが乗っている窓の値。
//
//========================================================================================
#ifndef __KESCMColorSampler_h__
#define __KESCMColorSampler_h__

#include "BaseType.h"
#include "PMReal.h"		// サンプリング点(ペーストボード座標)
#include "PMString.h"
#include "OMTypes.h"	// UID(表示中スプレッドの指定。2026-08-16)

class IDataBase;

// hoverDB       = マウスが乗っている窓の文書。ここのページを実際にヒットテストする=**1行目**に出る側。
// otherDB       = 比較相手の文書(ページ対応で解決して2行目に出す)。nil なら単独モード=1行だけ返す
//                 (Stop 中、および Start 中でも比較に無関係な第3の文書の上のとき)。
// hoverIsTarget = hover が比較の Target(新)側なら kTrue、Source(旧)側なら kFalse。ページ対応の向き
//                 (KESCMMapTargetToSource / KESCMMapSourceToTarget)と行末ラベル(t/s)の割り当てに効く。
//                 単独モードでは使わない。
// mx, my        = ★サンプリングする点。hoverDB の**ペーストボード(content)座標**。
// outPanel  = パネルのステータス行用(欄が狭いので略語 t/s の compact 表記)。⚠**寸法をここに書き写さない**
//             ＝正本は `ui/KCMUI.fr` の `kKESCMStatusTextWidgetID` の `Frame(8,76,216,150)`
//             (2026-08-17 実測＝208×74px・4行)。⚠2026-08-19(B-U5 3周目): `:1921` と書いてあったが
//             実体は 1935 で**-14 ずれていた**。★同じ `:1921` が**4ファイルに写っていた**
//             (ここ / KESCMPageMap.cpp / IKESCMCompareFacade.h / KESCMPageCheck.cpp)＝
//             **1つの誤りが4本に増えていた**ので4本とも widget 名へ直した。
//             旧「幅152px」は 2026-07-15 世代の値で、同じ古い数字が3ファイルに残っていた(不具合再検査 B5)。
// outCursor = カーソル自身に描く用(ラベルは t/s の1文字。C/M/Y/K見出しはKESCMCmykCursor.cpp のビットマップ
//             カーソル側で別途描画するため、渡す文字列は数値行のみでよい)。
//
// ★★2026-08-15(第2段 Task 4B)に **「マウス下」から「この点」へ変えた**(旧 KESCMSampleCmykUnderMouse)。
//   以前はこの関数の中で KESCMQueryViewUnderMouse / KESCMQueryMouseContentPoint を呼んでいたが、
//   **どの窓のどこか**は窓が無ければ答えの無い問いで、model プラグインからは引けない
//   (UI プラグインの boss はバックグラウンドスレッドから見えず nil が返る)。
// ⚠**押した窓から外れていないかの判定も呼び手(UI)へ移った**。以前はここで
//   KESCMFindDocDbForView(view) != hoverDB を見て kFalse を返していた ---- **その判定を落とすと、
//   別の窓の座標を hoverDB のページ座標として誤って読む**(2026-07-25 監査で入れたガード)。
//   呼び手は KESCMCmykCursor.cpp の2か所で、どちらも同じ判定を先に通してからここへ来る。
// ★★★viewSpreadUID(2026-08-16) = **そのビューが今表示しているスプレッド**。
//   ⚠**必須の観測値**——マスタースプレッドと通常スプレッドはペーストボード座標で重なるので、
//     これが無いと**マスターを表示しているのに通常ページの色を読んで「マスターの色」として出す**
//     (値が出るので誤りに気づけない＝2026-08-16 に実際に起きていた)。理由の全文は KESCMCore.h。
bool16 KESCMSampleCmykAt(IDataBase* hoverDB, IDataBase* otherDB, bool16 hoverIsTarget,
                         const PMReal& mx, const PMReal& my,
                         UID viewSpreadUID,
                         PMString& outPanel, PMString& outCursor);

// Alt+左ホールド(ドラッグ)中の hover→other ページ対応表キャッシュ。Begin=押下時(RevealBegin の
// Cmyk 分岐)に対応表を1回だけ構築、End=解放時(RevealEnd)に破棄。Begin〜End の間、
// KESCMSampleCmykAt は毎サンプル(≦20回/秒)の KESCMBuildPairing 全ページ再構築を省いて
// キャッシュを引く(ページ構成はトラッキング中に変わらない。2026-07-15)。Begin なしの単発サンプルは
// 従来どおり毎回構築(挙動不変)。単独モード(otherDB==nil)では呼ばない=ページ対応が要らない。
void KESCMSampleCmykBeginDrag(IDataBase* hoverDB, IDataBase* otherDB, bool16 hoverIsTarget);
void KESCMSampleCmykEndDrag();

#endif // __KESCMColorSampler_h__
