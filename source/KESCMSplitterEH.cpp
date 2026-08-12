//========================================================================================
//
//  KESCMSplitterEH.cpp
//
//  何も受け取らない IEventHandler。パネルの分割バーを**ドラッグで動かせなくする**ために載せる。
//
//  ***** なぜ「動かなくする」のに実装が要るのか *****
//
//  分割バーは SplitterPanelWidget(KESCM.fr)そのものの一部で、掴んで動かす処理は stock の
//  kSplitterPanelWidgetBoss が持つ IID_IEVENTHANDLER(kSplitterPanelEHImpl)が入口になっている
//  (実機の boss ダンプで確認)。★継承した boss からインターフェイスを**取り除く道は無い**ので、
//  止め方は「別の答えを返す実装で上書きする」になる ---- KESCMNoTip.cpp がツールチップを黙らせて
//  いるのと同じ形で、あちらが空文字を返すのに対し、こちらは「どのイベントも処理しない」を返す。
//
//  ***** 何が失われるのか(＝これで消えるのはドラッグだけ) *****
//
//  この widget は上ペインと下ペインを入れる器で、自分自身の面はバーの帯しかない。子の widget は
//  それぞれ自分の event handler を持っており、イベントはカーソルの下の widget に配られてから
//  上へ抜けていくので、器の handler が黙っていても中身の操作は何も変わらない。素の
//  GenericPanelWidget が IID_IEVENTHANDLER を**そもそも持っていない**(ダンプで確認)ことが、
//  「何も処理しない器」が普通の姿だという裏付けでもある。
//
//  ★基底に CEventHandler を選んだ理由: CEventHandler.h:36-37 は「これは EventFilter か非 widget 用で、
//  widget なら DVControlEventHandler か widget 用の handler を使え」と勧めている。ここでその勧めに
//  従わないのは、欲しいものが**振る舞いゼロ**だからで、CEventHandler は全メソッドが kFalse を
//  返すだけの実装(CEventHandler.cpp:87-117 ほか)＝一行も書かずに目的そのものになる。DV 側の
//  IID_IDVEVENTHANDLER は kBaseWidgetBoss 由来のまま**触っていない**ので、widget としての土台は
//  素の状態で残る。
//
//  ⚠上方向のドラッグは元から止まっていた(Top snap = 上ペインの設計高。Widgets.fh:418
//  「slider doesn't move beyond snap pos」)。残っていたのは下げる方向だけで、下げると上ペインが
//  固定座標のコントロールの塊より高くなり、下に何も無い帯が残る。セクションの高さは**パネルの縁**の
//  ドラッグで決める ---- KESCMPanelView::ConstrainDimensions は元からその前提で書かれている。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// インターフェイス:
#include "CEventHandler.h"		// IEventHandler の入門実装(全メソッドが kFalse を返す)

// プロジェクト内:
#include "KESCMID.h"

/** どのイベントも処理しない IEventHandler。基底をそのまま名前を付けて載せるだけ。

	override は1つも要らない ---- CEventHandler が IEventHandler の全メソッドを「処理しなかった」
	(kFalse)で実装しているので、これがそのまま「掴めない分割バー」になる。
*/
class KESCMSplitterEH : public CEventHandler
{
public:
	KESCMSplitterEH(IPMUnknown* boss);
	virtual ~KESCMSplitterEH();
};

CREATE_PMINTERFACE(KESCMSplitterEH, kKESCMSplitterEHImpl)

KESCMSplitterEH::KESCMSplitterEH(IPMUnknown* boss) : CEventHandler(boss)
{
}

KESCMSplitterEH::~KESCMSplitterEH()
{
}

// KESCMSplitterEH.cpp 終わり。
