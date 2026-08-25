//========================================================================================
//
//  KCMThreadSafety.cpp
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
#include "PMString.h"				// IDataBase::GetDocumentID() の戻り(未保存文書の同一性)

// Project includes:
#include "KCMThreadSafety.h"

//----------------------------------------------------------------------------------------
bool16 KCMIsMainThread()
{
	return IDThreading::IsMainThreadDomain() ? kTrue : kFalse;
}

//----------------------------------------------------------------------------------------
bool16 KCMIsSameDoc(IDataBase* a, IDataBase* b)
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
	if (fa != nil && fb != nil)
		return FileUtils::IsEqual(*fa, *fb);

	// ★★2026-08-18(不具合再検査 B9): **未保存文書のための第2の口**。ここは以前 kFalse を返して
	//   いたので、**一度も保存していない2文書を比較すると BG(PDF の非同期書き出し)でマークが
	//   1つも出なかった**(画面には出る＝「画面と書き出しが食い違う」形)。
	//   GetDocumentID() は未保存でも値を持ち、BG のクローン DB でも main と一致することを
	//   2026-08-16 の API 監査 B9 で実測済み。理由と選択の根拠はヘッダーに書いてある。
	//   ⚠**片方だけファイルがある場合もここへ来る**(保存済み ⇔ 未保存)。その2つは ID が違うので
	//     正しく偽になる ---- ファイルの有無で先に切り捨てると、BG のクローンが
	//     GetSysFile を返さない事態(未確認)で静かに壊れるため、判定は ID に委ねる。
	const PMString ida = a->GetDocumentID();
	const PMString idb = b->GetDocumentID();
	if (ida.IsEmpty() || idb.IsEmpty())
		return kFalse;	// 空どうしを「同じ」と答えない(名前が無いことは同一性の証拠にならない)

	return (ida.Compare(kTrue /*caseSensitive*/, idb) == 0) ? kTrue : kFalse;
}

//----------------------------------------------------------------------------------------
// ★ファイルスコープの static にしてある(関数内 static ではない)。ガイド vol1-07 L126-128 は
//   "Remove any **function-local static** variables" と名指しするので、公式の hyphenator と
//   同じく「クラス/ファイルスコープの static な mutex」の形に揃えた。
//   mutex 自体は構築後に代入されないので、静的初期化のままで安全。
//----------------------------------------------------------------------------------------
static boost::recursive_mutex sKCMMarkStateMutex;

boost::recursive_mutex& KCMMarkStateMutex()
{
	return sKCMMarkStateMutex;
}
