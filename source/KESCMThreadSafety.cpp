//========================================================================================
//
//  KESCMThreadSafety.cpp
//
//  スレッド安全のための道具の実装。設計の根拠と公式の手本はヘッダー冒頭に書いてある。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"

// General includes:
#include "IDThreadingPrimitives.h"	// IDThreading::IsMainThreadDomain
#include "FileUtils.h"				// FileUtils::IsEqual(IDFile,IDFile)
#include "IDFile.h"

// Project includes:
#include "KESCMThreadSafety.h"

//----------------------------------------------------------------------------------------
bool16 KESCMIsMainThread()
{
	return IDThreading::IsMainThreadDomain() ? kTrue : kFalse;
}

//----------------------------------------------------------------------------------------
bool16 KESCMIsSameDoc(IDataBase* a, IDataBase* b)
{
	// 同一ポインタ = 同じ文書(メインスレッドの通常経路。両方 nil は「同じ」とは言わない)。
	if (a == b)
		return (a != nil) ? kTrue : kFalse;
	if (a == nil || b == nil)
		return kFalse;

	// ★ここから先がバックグラウンド用の道。BG のクローン DB は別ポインタだが、
	//   元の文書と同じファイルを指す。⚠GetSysFile() は未保存文書では nil を返す
	//   (IDataBase.h:270-274 "Returns nil if there is no file associated yet")。
	const IDFile* fa = a->GetSysFile();
	const IDFile* fb = b->GetSysFile();
	if (fa == nil || fb == nil)
		return kFalse;

	return FileUtils::IsEqual(*fa, *fb);
}

//----------------------------------------------------------------------------------------
// ★ファイルスコープの static にしてある(関数内 static ではない)。ガイド vol1-07 L126-128 は
//   "Remove any **function-local static** variables" と名指しするので、公式の hyphenator と
//   同じく「クラス/ファイルスコープの static な mutex」の形に揃えた。
//   mutex 自体は構築後に代入されないので、静的初期化のままで安全。
//----------------------------------------------------------------------------------------
static boost::recursive_mutex sKESCMMarkStateMutex;

boost::recursive_mutex& KESCMMarkStateMutex()
{
	return sKESCMMarkStateMutex;
}
