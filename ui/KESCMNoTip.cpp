//========================================================================================
//
//  KESCMNoTip.cpp
//
//  何も言わない ITip。ツールチップを**出さない**ために載せる。
//
//  ***** なぜ「消す」のに実装が要るのか *****
//
//  静的テキストの widget は自前でツールチップを持っている ---- stock の kStaticTextWidgetBoss が
//  IID_ITIP (kTextWidgetTipImpl) を持ち、セルが文字列を縮めたときに全文をポップアップで見せる
//  (実機の boss ダンプで確認)。ダイアログのラベルなら親切だが、Story Edits の一覧の行では邪魔で、
//  ポインタが一覧を横切るだけで本文の箱が出てしまう(2026-08-10 ユーザー報告)。
//
//  ★継承した boss からインターフェイスを**取り除く道は無い**ので、消し方は「別の答えを返す」になる。
//  ITip.h:37-41 が契約を明記している ---- "To have no tip, return PMString()"。
//  kKESCMStoryRowCellBoss が IID_ITIP にこの実装を名指しして、沈黙を手に入れる。
//
//  ★基底が AbstractTip なのは KESCMIconTip.cpp と同じ理由(製品コードのツールチップが例外なく
//  継承している基底で、UpdateToolTipOnMouseMove と SetTipText を供給してくれる)。そちらの
//  ファイル冒頭に経緯が全部書いてある。
//
//  この一覧に固有のものは何も無い ---- 要らないツールチップを継承してしまった KESCM の widget は、
//  どれでもこの実装を名指しできる。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// インターフェイス:
#include "AbstractTip.h"		// 製品のツールチップが全部継承している基底

// 一般:
#include "PMString.h"

// プロジェクト内:
#include "KCMUIID.h"

/** 常に空を返す ITip。空文字列＝「ツールチップ無し」がインターフェイスの定義。 */
class KESCMNoTip : public AbstractTip
{
public:
	KESCMNoTip(IPMUnknown* boss);
	virtual ~KESCMNoTip();

	virtual PMString GetTipText(const PMPoint& mouseLocation);
};

CREATE_PMINTERFACE(KESCMNoTip, kKESCMNoTipImpl)

KESCMNoTip::KESCMNoTip(IPMUnknown* boss) : AbstractTip(boss)
{
}

KESCMNoTip::~KESCMNoTip()
{
}

PMString KESCMNoTip::GetTipText(const PMPoint& /*mouseLocation*/)
{
	return PMString();
}

// KESCMNoTip.cpp 終わり。
