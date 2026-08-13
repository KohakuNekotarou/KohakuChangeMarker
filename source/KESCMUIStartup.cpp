//========================================================================================
//
//  KESCMUIStartup.cpp
//
//  UI 側の起動/終了サービス(2026-08-13・model/UI 分割 第1段 Task 8 で新設)。
//  model 側の対は KESCMPeek.cpp の KESCMPeekStartup で、**2つで元の1本を覆う**。
//
//  ここに在るものは全部 widget・窓・カーソル・購読に触るので、model プラグインには置けない。
//  ★★逆に言うと、**元の起動処理は丸ごと UI だった**——分けてみたら model 側の Startup は空になった。
//
//  ⚠★★**終了時の順序が意味を持つ**: 購読を外してから、購読先の道具を畳む。購読している間セッションが
//    握っているのは**この .pln の中へのポインタ**で、終了処理中のパネル破棄は実際に通知を飛ばす
//    ---- つまり**消えかけのコードで Update が走る**。だから
//    KESCMDetachPanelVisibilityObserver() は KESCMShutdownPanelAlpha() より必ず先。
//    (2026-08-12 に KBS から移植した対策。順序を入れ替えてはいけない。)
//
//  ⚠★**サービスが2本になったことの帰結**: model 側 Shutdown と UI 側 Shutdown の**相対順序は
//    保証されない**(別々の IStartupShutdown サービスなので、アプリがどちらを先に呼ぶかは未定義)。
//    分割時に1行ずつ確認した限り、UI 側の仕事(購読の停止・フォント返却・キャッシュ破棄)と
//    model 側の仕事(コンテナを空にする)の間に前後関係は無い。★もし将来どちらかが相手の状態を
//    読むようになったら、この前提が壊れる ---- そのときは同じサービスに戻すか、明示的な順序を作ること。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "CPMUnknown.h"
#include "IStartupShutdownService.h"

#include "KESCMID.h"
#include "KESCMPanelState.h"		// KESCMLoadPanelStateIfPresent(保存済みパネル設定の復元)
#include "KESCMPanelAlpha.h"		// 半透明トグルの購読/解除と後片付け
#include "KESCMTrackerHud.h"		// KESCMTrackerHudShutdown(押下中 HUD のフォント返却)
#include "KESCMPeekGesture.h"		// KESCMAttachDocsClosedObserver / KESCMPeekGestureShutdown
#include "KESCMThumbIdleTask.h"		// KESCMShutdownThumbIdleTask(遅延サムネイル idle task の解放)
#include "KESCMViewSync.h"			// KESCMInvalidateSyncCaches / KESCMViewSyncShutdown
#include "KESCMCmykCursor.h"		// KESCMCmykShutdown(カーソル文字列とフォント参照)
#include "KESCMUIShared.h"			// KESCMAttachModelChangeObserver / KESCMDetachModelChangeObserver(Task 9)
#include "KESCMModelNotify.h"		// KESCMClearSessionStatus(ステータス記憶の破棄)
									// ★保持は model 側(設計書 §3.3)＝app.kcmStatus はパネルを閉じていても答えるため。
									//   2026-08-13 Task 9 で KESCMPanelObserver.cpp から移した。

class KESCMUIStartup : public CPMUnknown<IStartupShutdownService>
{
public:
	KESCMUIStartup(IPMUnknown* boss) : CPMUnknown<IStartupShutdownService>(boss) {}
	~KESCMUIStartup() {}

	virtual void Startup();
	virtual void Shutdown();
};

CREATE_PMINTERFACE(KESCMUIStartup, kKESCMUIStartupImpl)

void KESCMUIStartup::Startup()
{
	// ★続けて保存済みパネル設定(独自 JSON)をここで読み込む(ユーザー指定 2026-07-15)。
	// 同期は Stop 中でもトグル ON なら動くため、「パネル初回オープン時に復元」の従来タイミングだと、
	// ON を保存したユーザーは起動〜パネルを開くまでの間だけ同期が止まってしまう。起動時に読み込めば
	// その窓が無くなる(保存が無ければ既定 OFF のまま)。
	// 各トグルの復元先は全部エンジン側のフラグ/購読で、パネルにも文書にも依存しない=起動時に安全
	// (KESCMDoSetPrintMarks は db=nil のフラグのみ、ScrollMap/IgnoreMarker/HoldToHide 等は平の代入)。
	// 内部の「セッション一度きり」ガードにより、パネル AutoAttach からの既存呼び出しは no-op のまま残る
	// (起動サービスの順序が万一変わっても取りこぼさない保険)。
	KESCMLoadPanelStateIfPresent();

	// 一括クローズ完了(kPendingDocumentsClosedMsg)の購読を開始する。以後、複数文書を続けて閉じても
	// UI の後片付けは「全部閉じ終わってから1回」に畳まれる(実体は KESCMPeekGesture.cpp)。
	KESCMAttachDocsClosedObserver();

	// パネルの表示状態変化(kPaletteVisibilityChangedMessage)の購読を開始する。「Translucent Panel」が
	// ON のとき、パネルを開き直したりドッキング⇄フローティングを切り替えたりしても半透明が残る
	// (半透明の付け先である OWL.Dock 窓がそのたびに作り直されるため)。実体は KESCMPanelAlpha.cpp。
	KESCMAttachPanelVisibilityObserver();

	// ★model からの通知(kKESCM*Message)の購読を開始する(2026-08-13 Task 9)。これが繋がっていないと
	//   model 側の仕事が画面に出ない ---- ただし**エラーも警告も出ず「何も起きない」形**で現れるので、
	//   実機で必ず確かめること。実体は KESCMModelChangeObserver.cpp。
	KESCMAttachModelChangeObserver();
}

void KESCMUIStartup::Shutdown()
{
	// 遅延サムネイル更新の idle task を解放(予約中なら RemoveTask してから)。
	KESCMShutdownThumbIdleTask();
	// 一括クローズの保留も捨てる(終了後に流れることは無いが、状態を残さない)。
	KESCMPeekGestureShutdown();
	// ★model からの通知の購読も止める(2026-08-13 Task 9)。**パネル周りを畳むより前**＝下の
	//   KESCMDetachPanelVisibilityObserver と同じ理由(消えかけのコードで Update が走るのを避ける)。
	KESCMDetachModelChangeObserver();
	// ★先に購読を止める(2026-08-12)。購読している間セッションが握っているのは**この .pln の中への
	//   ポインタ**で、終了処理中のパネル破棄は実際に通知を飛ばす ---- 消えかけのコードで Update が走る。
	//   通知を止めてから、下の行で道具(タイマーと Win32 フック)を畳む順序。
	//   ★KBS が 2026-08-08 に新設した対を移植した分(KESCM 側にだけ無かった)。
	KESCMDetachPanelVisibilityObserver();
	// パネル半透明の遅延再適用タイマーも同様に止める(同じく生関数ポインタを残さないため)。
	KESCMShutdownPanelAlpha();
	// 押下中 HUD が抱えるフォント参照を返す。押下中に quit した経路でも確実に片付ける。
	KESCMTrackerHudShutdown();

	// 同期のページ矩形表・対応表・前回状態(2026-07-25 追補)。
	KESCMInvalidateSyncCaches();
	// レイアウトビュー同期の後始末(状態フラグを落とすだけ。理由は KESCMViewSync.cpp の実体側)。
	KESCMViewSyncShutdown();

	// ★file-static PMString を空にして、プラグイン unload 時の静的デストラクタを実質 no-op にする
	// (Windows では実害なしの実績だが、Mac は unload 順が異なるため heap バッファを持ち越さない方が
	// 安全。2026-07-15 終了堅牢化)。CMYK 側(カーソル文字列・押下中のフォント/文書ポインタ)。
	KESCMCmykShutdown();
	KESCMClearSessionStatus();	// パネルのステータス記憶(gSessionStatus)も同様に空へ
}

// KESCMUIStartup.cpp 終わり。
