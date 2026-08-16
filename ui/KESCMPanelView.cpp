//========================================================================================
//
//  KESCMPanelView.cpp
//
//  パネルの IControlView。PalettePanelView に「これ以上は小さくできない」という下限を足しただけ。
//
//  ***** なぜ 2026-08-10 に要るようになったのか *****
//
//  この日までパネルは PanelList で kNotResizable ＝ 縁をドラッグできず、大きさは .fr の Frame が
//  すべてだった。Story Edits の一覧を載せたところで「横幅が短い。今のを最小の設定にして、
//  大きさは固定ではなくしよう」というユーザー指定が入り、kIsResizable に変えた。可変にすると
//  下限を決めるものが無くなるので、それをここで答える ---- 枠組みはリサイズ**する前に**聞いてくる
//  ので、ここで断れば「一度縮んでから戻る」ではなく、そもそもドラッグが止まる。
//
//  ***** 高さは下限だけでなく「閉じているときの上限」でもある *****
//
//  Story Edits を閉じているときのパネルは上ペインだけ ---- 固定座標のコントロールが並んだ
//  kKESCMPanelTopPaneHeight ぶんの塊で、その下には何も無い。伸ばせるようにすると空白の帯が
//  できるだけなので、閉じている間はその高さちょうどに留める(ユーザー判断 2026-08-10)。開いている
//  間は、上ペイン＋セクションの最小まで縮められる。
//  ★高さを数字で書かない: 上ペインの設計高は .fr と KCMUIID.h で決まり、実際 2026-08-10 に
//    173→185 へ動いた(帯へ猫イラストを下ろした分)。ここに数字を写すと、その日に嘘になる。
//
//  ★セクションの最小は**数字で書かない**。分割バーに Bottom snap を聞く ---- その値は .fr が
//  持っており(KCMUI.fr の SplitterPanelWidget)、KESCMStorySection.cpp も同じ聞き方で開く高さの
//  下限にしている。ここに 60 と書けば、同じ判断が2か所に分かれる。
//
//  手本は KBSPanelView.cpp(こちらは行の高さで丸める処理も持つ)、その元は KESCL、さらに元は
//  customconditionaltextui。KESCM は丸めない ---- 上ペインが固定高なので、一覧の下に半端な行が
//  出るかどうかはユーザーがどこでドラッグを止めたか次第で、そこまで面倒を見る理由がまだ無い。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// インターフェイス:
#include "IPanelControlData.h"		// FindWidget ---- 自分の中の分割バーを探す

// source/open で公開されているインターフェイス:
//
// インクルードディレクトリを足さずに相対パスで届かせている。理由は KESCMStorySection.cpp の
// 同じ include の隣に書いてある(それを持てる build ファイルがこのプラグインのリポジトリの外に
// あり、チェックアウトし直すと残らない)。
#include "../../open/interfaces/ui/ISplitterPanelControlData.h"

// 一般:
#include "PalettePanelView.h"

// プロジェクト内:
#include "KCMUIID.h"				// kKESCMPanelMinWidth / kKESCMPanelTopPaneHeight

/** パネルのビュー: PalettePanelView に最小サイズを足したもの。 */
class KESCMPanelView : public PalettePanelView
{
public:
	KESCMPanelView(IPMUnknown* boss) : PalettePanelView(boss) {}
	virtual ~KESCMPanelView() {}

	/** 要求されたサイズを、実際に使ってよいサイズへ丸める。
		@param dimensions 要求されたサイズ。
		@return 実際に使うサイズ。
	*/
	virtual PMPoint ConstrainDimensions(const PMPoint& dimensions) const;
};

CREATE_PERSIST_PMINTERFACE(KESCMPanelView, kKESCMPanelViewImpl)

/* ConstrainDimensions
*/
PMPoint KESCMPanelView::ConstrainDimensions(const PMPoint& desiredDimen) const
{
	PMPoint constrained = desiredDimen;

	// 幅 ---- 中の widget は縁に束縛してあるので広げる方向は自由。狭める方向だけ止める。
	if (constrained.X() < PMReal(kKESCMPanelMinWidth))
		constrained.X(PMReal(kKESCMPanelMinWidth));

	// 高さの下限は、少なくとも上ペインぶん。セクションが開いていればその最小も足す。
	PMReal minHeight(kKESCMPanelTopPaneHeight);
	bool16 sectionOpen = kFalse;

	InterfacePtr<const IPanelControlData> panelData(this, IID_IPANELCONTROLDATA);
	if (panelData != nil)
	{
		// パネルが組み立てられる途中では、まだ分割バーがいないことがある。そのときは
		// 「閉じている」と同じ扱い ---- 上ペインぶんだけを守って、上限は付けない。
		InterfacePtr<const ISplitterPanelControlData> splitter(panelData->FindWidget(kKESCMSplitterWidgetID), UseDefaultIID());
		if (splitter != nil)
		{
			sectionOpen = !splitter->IsSinglePanelVisible();
			if (sectionOpen)
				minHeight = PMReal(kKESCMPanelTopPaneHeight + splitter->GetSplitterSnapBottom());
		}
	}

	if (constrained.Y() < minHeight)
		constrained.Y(minHeight);

	// ★閉じている間は下限がそのまま上限でもある。上ペインより高くしたところで、
	//   下に何も無い帯が伸びるだけなので。
	if (!sectionOpen && constrained.Y() > PMReal(kKESCMPanelTopPaneHeight))
		constrained.Y(PMReal(kKESCMPanelTopPaneHeight));

	return constrained;
}

// KESCMPanelView.cpp 終わり。
