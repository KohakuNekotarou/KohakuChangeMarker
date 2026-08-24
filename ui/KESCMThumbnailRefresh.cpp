//========================================================================================
//
//  KESCMThumbnailRefresh.cpp
//
//  比較実行後に、Pages パネルの「既に表示済み」のサムネイルを再生成させる(KESCMThumbnailRefresh.h)。
//
//  ★2026-07-06 実機で切り分け完了。効く最小セットは次の2手だけ:
//    ④ IImageCacheMgr::Purge(...)  … 共有画像キャッシュを無効化する。★Pages パネルのサムネイルは
//       この共有キャッシュに載っている。これを外すとサムネイルは古いまま=枠が出ない(実機確認)。
//    ③ ForceRedraw                 … Purge 後に Pages パネル(と両サブパネル)を再描画させ、その場で
//       サムネイルを作り直させる。これを外すと Purge しても即時には反映されない(実機確認)。
//
//  ─────────────────────────────────────────────────────────────────────────────
//  ★2026-07-07 サムネイル点滅の低減(実機で確定):
//    従来の Purge(db) は「その文書の画像キャッシュ全部」を捨てるため、変更のないページの
//    サムネイルまで作り直され、Pages パネル全体が一瞬点滅していた。これを「変更ページ
//    (sEntries / sSrcPageToTarget のキー)だけを per-UID Purge」に絞る。変更のないサムネイルは
//    キャッシュが生き残るので点滅しない(変更ページだけ作り直され、そこは元々枠を描き直したい)。
//
//    ★キャッシュキーの特定(実機プローブ結果): Pages パネルのサムネイルは「ページ UID」で
//    キャッシュされている(page UID の Purge で解放バイト>0 / spread UID では 0)。よって
//    スプレッド単位の Purge は不要で、対象ページの UID を直接 Purge するだけでよい。
//
//    ★対象ページ集合には「変更リング(sEntries)」だけでなく「overflow="/"斜線(sOverflowT/sOverflowS)」も
//    含める(2026-07-08 実機で判明)。斜線は別集合なので、含めないと overflow ページのサムネイルが
//    Purge されず斜線が即時に出ない。
//
//    ★2026-07-08 Stop/クローズの取りこぼし修正(ハイブリッド化):
//    Stop(KESCMDoClearMarks)/クローズ(KESCMHandleDocsClosed)は DropAll 済みで db が sDB/sSrcDB でなく
//    なるため、変更ページ集合を引けず従来は Purge(db) 全体に落ちていた。ところが全体 Purge(db) 一発は
//    既存サムネイルの無効化として実機で効かず(枠が消えない)ことが判明。そこでフォールバックを
//    「全ページを列挙(KESCMCollectPageUIDs)して1ページずつ per-UID Purge」に変更する(proven な per-UID を
//    全ページに回す)。DropAll 済みで枠データが無いので作り直しはクリーン=枠が確実に消える。終端操作
//    なのでパネル全体が一瞬リフレッシュされても許容(START の点滅回避とは別扱い)。
//  ─────────────────────────────────────────────────────────────────────────────
//
//  ※サムネイル自体への枠描画は描画エンジン側(KESCMDrawEventHandler::sThumbExperiment=既定ON)。
//    この関数はあくまで「既表示分を作り直させる」トリガー。安全に何度でも呼べる。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "ISession.h"			// GetExecutionContextSession(終了処理中は nil になり得るので型を明示して受ける)
#include "IApplication.h"
#include "IPanelMgr.h"
#include "IPanelControlData.h"
#include "IControlView.h"
#include "IDataBase.h"
#include "IWorkspace.h"
#include "IImageCacheMgr.h"
#include "IUIDListControlData.h"	// ★Pages パネル自身が持つ選択 UID リスト(`[なし]` の判別。2026-08-24)

#include "PagesPanelID.h"		// kPagesPanelWidgetID / kLayoutPagesSubPanelWidgetID / kMasterPagesSubPanelWidgetID
#include "ImageID.h"			// IID_IIMAGECACHEMGR
#include "Utils.h"

#include <set>
#include <vector>

#include "KESCMThumbnailRefresh.h"
#include "IKESCMMarkData.h"			// GetAllPageUIDs / GetMasterPageUIDs / GetMarkablePageUIDs
									// (2026-08-14 Task 16 で Facade 経由へ)

// サブパネル(Layout 用/Master 用)を1枚再描画する(③)。
static void KESCMForceRedrawSubPanel(IPanelControlData* pcd, const WidgetID& subPanelWID)
{
	if (pcd == nil)
		return;
	IControlView* subView = pcd->FindWidget(subPanelWID);
	if (subView != nil)
		subView->ForceRedraw(nil, kTrue);
}

// ★KESCMCollectChangedPageUIDs は 2026-08-13 に **KESCMCore.cpp へ移した**(model/UI 分割 第1段
//   Task 10)。「今どのページにマークが出得るか」は widget を1つも触らない **model の問い**で、
//   呼び手も model 側(KESCMCore / KESCMPageCheck / KESCMPageMap)だけだった ---- ここに置いてあった
//   せいで、その3ファイルが UI ヘッダーを include し続けていた(逆流台帳 §2-1)。
//   ⚠2026-08-17 訂正(API 監査 B-U8): ここは「宣言は KESCMCore.h」と書いていたが、**ui/ から
//   KESCMCore.h は見えない**(model 側のヘッダー)。UI 側の入口は **IKESCMMarkData::GetMarkablePageUIDs**
//   (境界の Facade。実体は model 側の KESCMCollectChangedPageUIDs)＝下の Purge はそれを呼んでいる。

// ④ 指定ページ UID 群を per-UID Purge(共有画像キャッシュ無効化)。ワークスペースは実効セッションから。
//    UID 列の入れ物は呼び出し側の都合で set/vector の両方が来るため、実体はイテレータ範囲テンプレートに
//    して使い捨てコピー(vector→set 変換)を作らない。順序も重複排除も Purge には不要。
template <class InputIt>
static void KESCMPurgePageThumbsRange(IDataBase* db, InputIt first, InputIt last)
{
	if (db == nil || first == last)
		return;
	// session は終了処理中に nil になり得る(この purge は遅延 idle task 経由でも走る。2026-07-25 追補 統一)。
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IWorkspace> ws(session != nil ? session->QueryWorkspace() : nil);
	InterfacePtr<IImageCacheMgr> cacheMgr(ws, IID_IIMAGECACHEMGR);
	if (cacheMgr == nil)
		return;
	for (; first != last; ++first)
	{
		UIDRef pageRef(db, *first);
		cacheMgr->Purge(pageRef, IImageCacheMgr::kWildCard);
	}
}

static void KESCMPurgePageThumbs(IDataBase* db, const std::set<UID>& pages)
{
	KESCMPurgePageThumbsRange(db, pages.begin(), pages.end());
}

static void KESCMPurgePageThumbs(IDataBase* db, const std::vector<UID>& pages)
{
	KESCMPurgePageThumbsRange(db, pages.begin(), pages.end());
}

// 表示中の Pages パネルを返す(KESCMThumbnailRefresh.h で宣言。ChangeNav の連動スクロールと共用)。
IControlView* KESCMGetVisiblePagesPanel()
{
	ISession* session = GetExecutionContextSession();	// 終了処理中は nil になり得る
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	if (app == nil)
		return nil;
	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr == nil)
		return nil;
	return panelMgr->GetVisiblePanel(kPagesPanelWidgetID);
}

// ★2026-08-24 新設: Pages パネルの選択に実ページが1枚も無いか(＝`[なし]` の行だけ)。理由と実測は宣言側。
bool16 KESCMPagesPanelSelectionHasNoRealPage()
{
	IControlView* panel = KESCMGetVisiblePagesPanel();
	if (panel == nil)
		return kFalse;		// パネルが無い＝この判定は使えない。従来どおりに任せる
	InterfacePtr<IUIDListControlData> uidList(static_cast<IPMUnknown*>(panel), IID_IUIDLISTCONTROLDATA);
	if (uidList == nil)
		return kFalse;		// 取れない＝判定できないので邪魔をしない

	const int32 n = uidList->Length___();
	if (n <= 0)
		return kFalse;		// 空は「選択なし」＝従来の判定に任せる(ここで無効化はしない)

	for (int32 i = 0; i < n; ++i)
	{
		if (uidList->GetUID___(i) != kInvalidUID)
			return kFalse;	// 実ページが1枚でもあれば、そちらに効かせる
	}
	return kTrue;			// 全部 kInvalidUID ＝ `[なし]` の行だけを選んでいる
}

// ③ Pages パネルが表示されていれば、その場で再描画させて(Purge 済みの)サムネイルを作り直させる。
//    隠れていれば触る先が無い(次に開いたとき自然に作られる)。
//    (2026-07-25: バッチ化のため公開版 KESCMForceRedrawPagesPanelNow としてヘッダーへ昇格)
void KESCMForceRedrawPagesPanelNow()
{
	IControlView* panel = KESCMGetVisiblePagesPanel();
	if (panel == nil)
		return;
	InterfacePtr<IPanelControlData> pcd(panel, UseDefaultIID());
	KESCMForceRedrawSubPanel(pcd, kLayoutPagesSubPanelWidgetID);
	KESCMForceRedrawSubPanel(pcd, kMasterPagesSubPanelWidgetID);
	panel->ForceRedraw(nil, kTrue);
}

void KESCMTryRefreshPagesPanelThumbnails(IDataBase* db, bool16 redrawNow)
{
	if (db == nil)
		return;

	std::set<UID> changedPages;
	if (Utils<IKESCMMarkData>()->GetMarkablePageUIDs(db, changedPages))
	{
		// ── 比較中の db(START/再比較) ──
		// ★変更ページ＋overflow斜線ページ＋登録ページ(ページ UID)だけを狙って Purge。変更のない
		//   ページのサムネイルはキャッシュが生き残り点滅しない。サムネイルはページ UID でキャッシュ
		//   されている(2026-07-07 実機プローブで確定)。集合が空(変更ゼロ)なら何も purge せず、
		//   下の ForceRedraw だけ行う(無害)。
		// ⚠2026-08-19(不具合再検査 B-U8)＝ここにあった extraPages(再比較前の旧集合)の合流は撤去した。
		//   **呼び手2つのどちらも渡していなかった**(全数 Grep)。旧集合の面倒を見るのは
		//   KESCMPurgeAllPageThumbs と KESCMRefreshThumbnailsForPages＝理由はヘッダー参照。
		KESCMPurgePageThumbs(db, changedPages);
	}
	else
	{
		// ── Stop/クローズ後(DropAll 済みで db が比較対象でなくなった) ──
		// ★全ページを列挙して1ページずつ per-UID Purge する。全体 Purge(db) 一発は既存サムネイルの
		//   無効化として実機で効かなかった(枠が消えない)ため、proven な per-UID を全ページに回す。
		//   DropAll 済み=枠データが無いので、作り直されるサムネイルはクリーン(枠が消える)。
		//   終端操作なのでパネル全体が一瞬リフレッシュされても許容。
		// ★★2026-08-17(API 監査 B-U8): ここは下の KESCMPurgeAllPageThumbs へ委譲する。
		//   **旧実装は GetAllPageUIDs しか呼んでおらず、マスターページが Purge から漏れていた**
		//   ---- 同じファイルの KESCMPurgeAllPageThumbs は「片方だけ Purge するとマスターのサムネイルに
		//   古い枠が残る」と自分で書いて両方を Purge しているのに、こちらだけが通常ページ止まりだった
		//   (マスターにもマークは出る＝KESCMBuildMasterPairing でペアリングされている)。
		//   ⚠この分岐は今も走る＝クローズ後の遅延 Purge(KESCMThumbIdleTask)がここを通る。
		//   ★★実測で両方向を確認した(2026-08-17。Purge 件数を出す一時診断ビルドと、旧形へ戻した
		//   反証ビルドの2本)＝**通常4ページ＋マスター1ページの文書で、Target を閉じた後の Purge が
		//   旧形では 4、この形では 5**。つまり漏れていたのはちょうどマスターページ1枚だった。
		//   ⇒ 「全ページを Purge する」の定義を1か所に寄せる([[one-question-one-place]])。
		//   redraw は下の if (redrawNow) が担う(KESCMPurgeAllPageThumbs は Purge しかしない)。
		KESCMPurgeAllPageThumbs(db);
	}

	if (redrawNow)
		KESCMForceRedrawPagesPanelNow();
}

void KESCMPurgeAllPageThumbs(IDataBase* db)
{
	if (db == nil)
		return;

	// ★通常ページとマスターページの両方。マスターにもマークは出る(KESCMBuildMasterPairing で
	//   ペアリングされ、KESCMDoMarkChangesDoc の対象に連結されている)ので、片方だけ Purge すると
	//   マスターのサムネイルに古い枠が残る。KESCMCollectMasterPageUIDs は out をクリアしない契約
	//   なので、そのまま後ろへ連結してよい。
	// ★2回続けて聞くので InterfacePtr に1回受ける(Utils.h:74-80。2026-08-17 の API 監査 B-U8)。
	InterfacePtr<IKESCMMarkData> marks(Utils<IKESCMMarkData>().QueryUtilInterface());
	std::vector<UID> allPages;
	marks->GetAllPageUIDs(db, allPages);
	marks->GetMasterPageUIDs(db, allPages);
	KESCMPurgePageThumbs(db, allPages);
	// ★再描画はしない(2026-08-19・不具合再検査 B-U8)。呼び手は全部「2文書ぶんを Purge してから
	//   最後に1回 KESCMForceRedrawPagesPanelNow」の形なので、ここで描くと二重になる。
}

void KESCMRefreshThumbnailsForPages(IDataBase* db, const std::vector<UID>& pages, bool16 redrawNow)
{
	if (db == nil || pages.empty())
		return;
	// トグルしたページを明示 per-UID Purge。登録解除は sRegistered から先に消えるため、上の
	// **IKESCMMarkData::GetMarkablePageUIDs**(実体は model 側の KESCMCollectChangedPageUIDs)経由では
	// 拾えない=ここで直接 Purge して緑「/」を消す。⚠2026-08-19(不具合再検査 B-U8)訂正＝ここは移設前の
	// 名前 KESCMCollectChangedPageUIDs を裸で書いていた。**同じ訂正は同ファイルの :74-80 で 2026-08-17 に
	// 済んでいたのに、130 行下のここが残っていた**＝1本直したら同じ語で grep を1回かける。登録追加でも
	// 同ページを対称に Purge して、赤「/」(overflow)→緑「/」(登録)の載せ替えを即時反映する。
	KESCMPurgePageThumbs(db, pages);
	if (redrawNow)
		KESCMForceRedrawPagesPanelNow();
}

// ★std::set 版(2026-08-16・API 監査 B4)。通知(KESCMNotifyPages)が運んでくる集合の入れ物が set
//   なので、詰め替えずにそのまま流す。Purge 本体は入れ物を選ばない(KESCMPurgePageThumbsRange)。
void KESCMRefreshThumbnailsForPages(IDataBase* db, const std::set<UID>& pages, bool16 redrawNow)
{
	if (db == nil || pages.empty())
		return;
	KESCMPurgePageThumbs(db, pages);
	if (redrawNow)
		KESCMForceRedrawPagesPanelNow();
}

// KESCMThumbnailRefresh.cpp 終わり。
