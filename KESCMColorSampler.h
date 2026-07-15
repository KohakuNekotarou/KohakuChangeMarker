//========================================================================================
//
//  KESCMColorSampler.h
//
//  ツール Alt+左クリック(旧・中ボタン Shift＋Ctrl＋Alt＋ミドル)で、クリック点の CMYK 生値を新(target)・旧(source)でサンプリング
//  して "Target C.. / Source C.." の文字列に組む。クリック点まわりの極小領域だけを高dpi・CMYK で
//  ラスタ化して中心1画素を読む。
//
//========================================================================================
#ifndef __KESCMColorSampler_h__
#define __KESCMColorSampler_h__

#include "BaseType.h"
#include "PMString.h"

class IDataBase;

// outPanel = パネルのステータス行用(幅152px制約のため略語 t/s の compact 表記)。
// outCursor = カーソル自身に描く用(ラベルは t/s の1文字。C/M/Y/K見出しはKESCMPeek.cppのビットマップ
// カーソル側で別途描画するため、渡す文字列は数値行のみでよい)。
bool16 KESCMSampleCmykUnderMouse(IDataBase* targetDB, IDataBase* sourceDB,
                                 PMString& outPanel, PMString& outCursor);

// Alt+左ホールド(ドラッグ)中の target→source ページ対応表キャッシュ。Begin=押下時(RevealBegin の
// Cmyk 分岐)に対応表を1回だけ構築、End=解放時(RevealEnd)に破棄。Begin〜End の間、
// KESCMSampleCmykUnderMouse は毎サンプル(≦20回/秒)の KESCMBuildPairing 全ページ再構築を省いて
// キャッシュを引く(ページ構成はトラッキング中に変わらない。2026-07-15)。Begin なしの単発サンプルは
// 従来どおり毎回構築(挙動不変)。
void KESCMSampleCmykBeginDrag(IDataBase* targetDB, IDataBase* sourceDB);
void KESCMSampleCmykEndDrag();

#endif // __KESCMColorSampler_h__
