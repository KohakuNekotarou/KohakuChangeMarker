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

// outPanel = パネルのステータス行用(幅140px制約のため略語 tgt/src の compact 表記)。
// outCursor = カーソル自身に描く用(ラベルは Target/Source のフル表記＋タブ区切りで桁揃え)。
bool16 KESCMSampleCmykUnderMouse(IDataBase* targetDB, IDataBase* sourceDB,
                                 PMString& outPanel, PMString& outCursor);

#endif // __KESCMColorSampler_h__
