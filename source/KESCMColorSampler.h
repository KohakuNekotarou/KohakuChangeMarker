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
#include "PMString.h"

class IDataBase;

// hoverDB       = マウスが乗っている窓の文書。ここのページを実際にヒットテストする=**1行目**に出る側。
// otherDB       = 比較相手の文書(ページ対応で解決して2行目に出す)。nil なら単独モード=1行だけ返す
//                 (Stop 中、および Start 中でも比較に無関係な第3の文書の上のとき)。
// hoverIsTarget = hover が比較の Target(新)側なら kTrue、Source(旧)側なら kFalse。ページ対応の向き
//                 (KESCMMapTargetToSource / KESCMMapSourceToTarget)と行末ラベル(t/s)の割り当てに効く。
//                 単独モードでは使わない。
// outPanel  = パネルのステータス行用(幅152px制約のため略語 t/s の compact 表記)。
// outCursor = カーソル自身に描く用(ラベルは t/s の1文字。C/M/Y/K見出しはKESCMPeek.cppのビットマップ
//             カーソル側で別途描画するため、渡す文字列は数値行のみでよい)。
bool16 KESCMSampleCmykUnderMouse(IDataBase* hoverDB, IDataBase* otherDB, bool16 hoverIsTarget,
                                 PMString& outPanel, PMString& outCursor);

// Alt+左ホールド(ドラッグ)中の hover→other ページ対応表キャッシュ。Begin=押下時(RevealBegin の
// Cmyk 分岐)に対応表を1回だけ構築、End=解放時(RevealEnd)に破棄。Begin〜End の間、
// KESCMSampleCmykUnderMouse は毎サンプル(≦20回/秒)の KESCMBuildPairing 全ページ再構築を省いて
// キャッシュを引く(ページ構成はトラッキング中に変わらない。2026-07-15)。Begin なしの単発サンプルは
// 従来どおり毎回構築(挙動不変)。単独モード(otherDB==nil)では呼ばない=ページ対応が要らない。
void KESCMSampleCmykBeginDrag(IDataBase* hoverDB, IDataBase* otherDB, bool16 hoverIsTarget);
void KESCMSampleCmykEndDrag();

#endif // __KESCMColorSampler_h__
