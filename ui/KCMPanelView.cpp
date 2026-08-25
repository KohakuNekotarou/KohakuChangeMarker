//========================================================================================
//
//  KCMPanelView.cpp
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
//  ***** 呼ばれ方は3通り(2026-08-18・不具合再検査 B-U3 で一時診断ビルドを入れて実測) *****
//
//  この関数は「リサイズのたびに1回」ではない。パネルマネージャは**極端な値を渡して min と max を
//  聞き出す**ので、1回の操作につき次の3種類が飛んでくる:
//    ・実リサイズ        req=240x303 → 240x303      (通したいサイズ)
//    ・最小サイズの問い合わせ req=15x15     → 224x263  (下限をそのまま答えとして持ち帰る)
//    ・最大サイズの問い合わせ req=32000x32000 → 32000x32000(開)/32000x185(閉)
//  ∴ **下の上限は「ドラッグを止める」だけでなく、パネルの max height そのものを決めている。**
//  閉じている間の max が 185 になるのはそのため(下の「上限でもある」の実体)。
//  ★min/max の問い合わせは**パネルを開くだけでも3組**飛ぶ(=実測ログ)。重い処理を足さないこと。
//
//  ***** 高さは下限だけでなく「閉じているときの上限」でもある *****
//
//  Story Edits を閉じているときのパネルは上ペインだけ ---- 固定座標のコントロールが並んだ
//  kKCMPanelTopPaneHeight ぶんの塊で、その下には何も無い。伸ばせるようにすると空白の帯が
//  できるだけなので、閉じている間はその高さちょうどに留める(ユーザー判断 2026-08-10)。開いている
//  間は、上ペイン＋セクションの最小まで縮められる。
//  ★高さを数字で書かない: 上ペインの設計高は .fr と KCMUIID.h で決まり、実際 2026-08-10 に
//    173→185 へ動いた(帯へ猫イラストを下ろした分)。ここに数字を写すと、その日に嘘になる。
//
//  ★セクションの最小は**数字で書かない**。分割バーに Bottom snap を聞く ---- その値は .fr が
//  持っており(KCMUI.fr の SplitterPanelWidget)、KCMStorySection.cpp も同じ聞き方で開く高さの
//  下限にしている。ここに 60 と書けば、同じ判断が2か所に分かれる。
//
//  ⚠★**上ペインの高さだけは、その原則から外れている**(2026-08-18・不具合再検査 B-U3)。ここは
//  KCMUIID.h の kKCMPanelTopPaneHeight を読むが、KCMStorySection.cpp の DesignedTopPaneHeight は
//  **同じ数字を splitter の Top snap から**取る ---- そちらのコメントは「その数字はレイアウトを
//  記述しているリソースの隣にあるべきで、ここで繰り返すべきではない」と書いている。現状は
//  **両方 185 で一致している**(KCMUIID.h の kKCMPanelTopPaneHeight と KCMUI.fr の Top snap。
//  2026-08-18 実測)ので実害は無いが、
//  片方だけ動かすと閉じる操作の目標高さと上限がずれる ⇒ **動かすときは必ず両方**([[one-question-one-place]])。
//  ここを splitter に聞く形へ寄せきれないのは、下のとおり **splitter が引けないときの答えも要る**ため。
//
//  手本は KBSPanelView.cpp(こちらは行の高さで丸める処理も持つ)、その元は KESCL、さらに元は
//  customconditionaltextui。KCM は丸めない ---- 上ペインが固定高なので、一覧の下に半端な行が
//  出るかどうかはユーザーがどこでドラッグを止めたか次第で、そこまで面倒を見る理由がまだ無い。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// インターフェイス:
#include "IPanelControlData.h"		// FindWidget ---- 自分の中の分割バーを探す

// source/open で公開されているインターフェイス:
//
// インクルードディレクトリを足さずに相対パスで届かせている。理由は KCMStorySection.cpp の
// 同じ include の隣に書いてある(それを持てる build ファイルがこのプラグインのリポジトリの外に
// あり、チェックアウトし直すと残らない)。
#include "../../open/interfaces/ui/ISplitterPanelControlData.h"

// 一般:
#include "PalettePanelView.h"

// プロジェクト内:
#include "KCMUIID.h"				// kKCMPanelMinWidth / kKCMPanelTopPaneHeight

/** パネルのビュー: PalettePanelView に最小サイズを足したもの。 */
class KCMPanelView : public PalettePanelView
{
public:
	KCMPanelView(IPMUnknown* boss) : PalettePanelView(boss) {}
	virtual ~KCMPanelView() {}

	/** 要求されたサイズを、実際に使ってよいサイズへ丸める。
		@param dimensions 要求されたサイズ。
		@return 実際に使うサイズ。
	*/
	virtual PMPoint ConstrainDimensions(const PMPoint& dimensions) const;
};

CREATE_PERSIST_PMINTERFACE(KCMPanelView, kKCMPanelViewImpl)

/* ConstrainDimensions
*/
PMPoint KCMPanelView::ConstrainDimensions(const PMPoint& desiredDimen) const
{
	PMPoint constrained = desiredDimen;

	// 幅 ---- 中の widget は縁に束縛してあるので広げる方向は自由。狭める方向だけ止める。
	if (constrained.X() < PMReal(kKCMPanelMinWidth))
		constrained.X(PMReal(kKCMPanelMinWidth));

	// 高さの下限は、少なくとも上ペインぶん。セクションが開いていればその最小も足す。
	PMReal minHeight(kKCMPanelTopPaneHeight);
	bool16 sectionOpen = kFalse;

	InterfacePtr<const IPanelControlData> panelData(this, IID_IPANELCONTROLDATA);
	if (panelData != nil)
	{
		// パネルが組み立てられる途中では、まだ分割バーがいないことがある。そのときは
		// 「閉じている」と**まったく同じ扱い**になる ---- 下限も上限も上ペインぶん。
		// ⚠★旧コメントはここを「上ペインぶんだけを守って、上限は付けない」と書いていたが、
		//   sectionOpen は kFalse のままなので**下の上限クランプはそのまま効く**(2026-08-18・
		//   不具合再検査 B-U3 で実装と読み合わせて訂正)。実装のほうを直さなかったのは、
		//   **splitter が引けない呼び出しを一度も観測できなかった**ため ---- 一時診断ビルドで
		//   起動・パネルの開閉4往復・セクションの開閉2往復を記録して、20回超すべて splitter=1
		//   (最初の1回＝起動時の min 問い合わせから既に引けている)。∴「上限を外す」と書き換えると、
		//   **観測できていない状況のために現に効いている上限を弱める**ことになる。
		InterfacePtr<const ISplitterPanelControlData> splitter(panelData->FindWidget(kKCMSplitterWidgetID), UseDefaultIID());
		if (splitter != nil)
		{
			sectionOpen = !splitter->IsSinglePanelVisible();
			if (sectionOpen)
				minHeight = PMReal(kKCMPanelTopPaneHeight + splitter->GetSplitterSnapBottom());
		}
	}

	if (constrained.Y() < minHeight)
		constrained.Y(minHeight);

	// ★閉じている間は下限がそのまま上限でもある。上ペインより高くしたところで、
	//   下に何も無い帯が伸びるだけなので。
	if (!sectionOpen && constrained.Y() > PMReal(kKCMPanelTopPaneHeight))
		constrained.Y(PMReal(kKCMPanelTopPaneHeight));

	return constrained;
}

// KCMPanelView.cpp 終わり。
