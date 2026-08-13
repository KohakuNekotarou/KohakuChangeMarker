//========================================================================================
//
//  KESCMViewSync.cpp
//
//  レイアウトビュー同期の実装(KESCMPeek.cpp から分離。2026-08-13 の model/UI 分割 第1段 Task 1)。
//  同期エンジン本体・ホットパス用のキャッシュ・フライアウトトグル(Sync Layout Views)・実行アクション
//  (Align Other Views to Active)・パノラマ購読オブザーバを持つ。
//
//  ★分離では関数の中身を1行も変えていない。変えたのは「どのファイルに座るか」と「誰から見えるか」だけ。
//    arm 状態(sPeekArmed / sPeekTargetDB / sPeekSourceDB)は KESCMPeek.cpp に残るので、ここからは
//    KESCMCore.h が公開しているアクセサ(KESCMIsArmed / KESCMArmedTargetDB / KESCMArmedSourceDB)で読む。
//
//  UI 側: IControlView と IPanorama を相手にするので、model プラグインからは触れない。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// オブジェクトモデル:
#include "PersistUtils.h"
#include "IDataBase.h"
#include "IGeometry.h"
#include "IDocument.h"
#include "IApplication.h"
#include "IDocumentList.h"
#include "ISpread.h"
#include "ISession.h"
#include "ILayoutViewUtils.h"		// GetAllLayoutViews(Split Window両ペインのIControlView*取得)
#include "ILayoutUIUtils.h"			// MakeZoomCmd(kZoomToCmdBoss。ビューポート同期のズーム)
#include "IPasteboardUtils.h"		// QuerySpread/QueryNearestSpread(ビュー中心のページ特定=公式ルート)
#include "CmdUtils.h"				// ProcessCommand(ズームコマンド実行)
#include "ICommand.h"

// レイアウトビュー同期(Sync Layout Views)用:
#include "CObserver.h"				// 同期オブザーバの基底(手本=work/KESLayoutScrollObserver.cpp)
#include "ISubject.h"				// AttachObserver/DetachObserver/IsAttached
#include "IActiveContext.h"			// IID_IACTIVECONTEXT / ContextInfo(文書切替の検知)
#include "widgetid.h"				// IID_IPANORAMA / kScrollToMessage・kScrollByMessage・kScaleToMessage・kScaleByMessage

// ジオメトリ / ビュー:
#include "IControlView.h"
#include "IPanorama.h"
#include "PMPoint.h"
#include "PMRect.h"					// ページのペーストボード矩形(旧 Alt+ミドルの追加/削除補正)
#include "PMReal.h"
#include "IGeometryFacade.h"		// GetItemBounds(ページ矩形をペーストボード座標で。手本=SnapTracker.cpp:610-616)
#include "IHierarchy.h"				// GetSpreadUID(宛先ページが載っているスプレッド。2026-08-11)

#include "ErrorUtils.h"				// PMSetGlobalErrorCode(ズーム失敗を持ち越さない)

#include <map>
#include <vector>
#include <chrono>				// steady_clock(キャッシュの TTL)

// プロジェクト内インクルード:
#include "KESCMID.h"
#include "Utils.h"                   // Utils<IKESCMCompareFacade>()
#include "IKESCMCompareFacade.h"     // arm 状態(2026-08-13・分割 第1段 Task 11 で Facade 経由へ)
#include "KESCMCore.h"               // KESCMCollectPageUIDs
#include "KESCMViewLookup.h"         // KESCMFindDocDbForView / KESCMForgetViewDbHint(2026-08-13 に KESCMCore.h から移動)
                                     // ＋ KESCMQueryPanorama(2026-08-13 に KESCMDrawEventHandler.h から移動)
#include "IKESCMMarkData.h"          // GetPagePairing / GetMasterPagePairing(除外対応表)。
                                     //   2026-08-13 Task 13 で KESCMPageMap.h から移した
#include "KESCMChangeNav.h"          // KESCMEnsureViewShowsSpread(同期先ビューを相手のスプレッドへ。2026-08-11)
#include "KESCMViewSync.h"

//========================================================================================
// ビューポート同期エンジン(共有)
//   手本パノラマの「見えている状態」= 実効ズーム(GetXScaleFactor(kTrue)、モニタPPI補正込み。
//   kZoomToCmdBoss の scaleFactor と同じ次元)+可視中心の content 座標 を、比較相手のドキュメントの
//   全レイアウトビューへ複製する(同一文書のビュー=スプリット相方は対象外)。
//   旧 Alt+ミドル(単発)とフライアウト「Sync Layout Views」(自動)の両方がこの1本を使う。
//   ★2026-07-11(ユーザー指定): 発動は「比較を Start 中(sPeekArmed)」かつ「手本・宛先とも Target/Source」の
//   ときだけ。未 Start や第3文書は関数先頭のガードで弾く(=同期しない)。以前は arm と無関係に全文書へ
//   複製していたが、Target↔Source 間のみへ限定した。
//========================================================================================

// 再入ガード: 複製そのものが対象ビューで kScaleTo/kScrollTo 等の通知を発生させ、同期オブザーバが
// それを拾って同期し返す(無限ループ/ピンポン)のを防ぐ。複製ループの間だけ kTrue。
static bool16 sLayoutSyncBroadcasting = kFalse;

// (KESCMFindDocDbForView は 2026-07-25 に KESCMCore.cpp の共有ヘルパへ移動。宣言は KESCMCore.h。
//  色サンプラの窓同一性ガードでも使うため。本ファイルの呼び出しは全てそのまま)

//========================================================================================
// ★ビューポート同期のホットパス用 短命キャッシュ(2026-07-25 追補)
//
//   Sync Layout Views は「どれかのビューがスクロール/ズームするたび」に通知が飛ぶ。スクロールを
//   ドラッグしている間は毎秒数十回この経路を通るが、旧実装はその都度
//     ・文書の全ページ列挙(ISpreadList → ISpread → GetNthPageUID)
//     ・各ページの IGeometry 取得 + InnerToPasteboardMatrix(ページ数ぶんの行列演算)
//     ・除外対応表の再構築(KESCMBuildPairing = 両文書の全ページ走査 + 登録判定)
//   をやり直していた。ページ数に比例した仕事が1通知ごとに乗るので、長い文書ほど追従が重くなる。
//   これらはいずれも「スクロールしている間は変わらない」ものなので、短時間だけ覚えて使い回す。
//
//   無効化は2本立て:
//     ①明示 … KESCMInvalidateSyncCaches()(arm/disarm・同期 OFF・文書クローズ・Shutdown)
//     ②時間 … kKESCMSyncCacheTtlMs(250ms)経過で自動失効。ページの追加/削除やスプレッドの
//              隠し/再表示に追従するための保険(スクロールを止めれば必ず作り直される)。
//
//   ★古いキャッシュを使っても壊れない設計にしてある: ずれ得るのは「追従側のスクロール位置」だけで、
//     次の通知か 250ms 後には正しい値に戻る。db ポインタは照合にしか使わず deref しない。
//========================================================================================
static const long long kKESCMSyncCacheTtlMs = 250;

// 1文書ぶんの「ページ UID とそのペーストボード矩形」。pages と rects は同じ並び。
// 幾何を取れなかったページは空矩形(幅・高さ 0)にしておき、判定側で自然に落とす。
// ★pages は「通常ページ(スプレッド順) → マスターページ(マスタースプレッド順)」の2段構成で、
//   境目が normalCount(2026-08-11)。マスターを混ぜたのは、同期でマスターページの矩形を引けるように
//   するため。⚠**最近傍探索には後半を使わない**＝マスタースプレッドは中心が原点の別座標空間にいて
//   通常ページと矩形が重なりうるので、混ぜると通常ページ表示中にマスターが最寄りと判定されうる。
struct KESCMPageRectCache
{
	IDataBase*          db;
	std::vector<UID>    pages;
	std::vector<PMRect> rects;
	size_t              normalCount;	// pages[0..normalCount) = 通常ページ / [normalCount..) = マスターページ
	KESCMPageRectCache() : db(nil), normalCount(0) {}
};
// 枠は2つで足りる: arm 中の同期は Target↔Source の2文書だけを行き来する。
static KESCMPageRectCache sPageRectCache[2];

// 除外対応表(登録ページを除いた順番対応)の両方向マップ。arm 中の (Target, Source) 対に紐づく。
static std::map<UID, UID> sSyncPairT2S;
static std::map<UID, UID> sSyncPairS2T;
static IDataBase* sSyncPairTargetDB = nil;
static IDataBase* sSyncPairSourceDB = nil;
static bool16     sSyncPairBuilt    = kFalse;

// 手本ビューの「前回複製した状態」。同じ状態の通知が続けて来たら複製ごと省く(下の Update 参照)。
// ポインタは同一性の照合にしか使わない(deref しない)。
static IPanorama* sLastSrcPano      = nil;
static PMReal     sLastSrcZoom      = 0.0;
static PBPMPoint  sLastSrcCenter;
static bool16     sHaveLastSrcState = kFalse;

static bool16                                  sSyncCacheValid = kFalse;
static std::chrono::steady_clock::time_point   sSyncCacheStamp;

// 同期キャッシュを丸ごと捨てる(KESCMViewSync.h で宣言)。
void KESCMInvalidateSyncCaches()
{
	sSyncCacheValid = kFalse;
	for (int i = 0; i < 2; ++i)
	{
		sPageRectCache[i].db = nil;
		sPageRectCache[i].pages.clear();
		sPageRectCache[i].rects.clear();
	}
	sSyncPairT2S.clear();
	sSyncPairS2T.clear();
	sSyncPairTargetDB = nil;
	sSyncPairSourceDB = nil;
	sSyncPairBuilt    = kFalse;
	sLastSrcPano      = nil;
	sHaveLastSrcState = kFalse;
	KESCMForgetViewDbHint();	// view→db の「直前ヒット」ヒントも一緒に捨てる
}

// 通知1回ぶんの入口で呼ぶ。TTL を過ぎていたらキャッシュを捨てて世代を切り直す。
static void KESCMSyncCacheBeginTick()
{
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	if (sSyncCacheValid)
	{
		const long long ms =
			std::chrono::duration_cast<std::chrono::milliseconds>(now - sSyncCacheStamp).count();
		if (ms < kKESCMSyncCacheTtlMs)
			return;			// まだ有効
		KESCMInvalidateSyncCaches();
	}
	sSyncCacheValid = kTrue;	// 新しい世代を開始
	sSyncCacheStamp = now;
}

// db のページ矩形表を返す(未作成なら作る)。db が nil なら nil。
static const KESCMPageRectCache* KESCMGetPageRects(IDataBase* db);

//----------------------------------------------------------------------------------------
// ページ pageUID(db 内)のペーストボード矩形を得る。パノラマの content 座標=ペーストボード座標
// (PBPMPoint)と同じ空間。ページは回転しないので inner bbox の 2 隅を変換して min/max を取る。
//----------------------------------------------------------------------------------------
static bool16 KESCMPagePasteboardRectRaw(IDataBase* db, UID pageUID, PMRect& outPB)
{
	if (db == nil || pageUID == kInvalidUID)
		return kFalse;
	InterfacePtr<IGeometry> geo(db, pageUID, UseDefaultIID());
	if (geo == nil)
		return kFalse;
	// ★ページ矩形をペーストボード座標で得るのは Facade の仕事(2026-08-06 ブロック12 監査で寄せた。
	//   ブロック10 で ChangeNav を寄せたときの論点が、ここと KESCMCore/KESCMScrollMap に残っていた)。
	//   手本 snapshot/SnapTracker.cpp:610-616 が**ページに対して**同じことをしている。
	//   ★上の nil 判定と下の min/max は残す: 「幾何を持つか」も「矩形が正規化済みか」も Facade は
	//   担保しない(旧実装がついでに担保していたぶん)。ここは1回だけ実測してキャッシュする経路なので、
	//   Facade 呼び出しに変えても同期ホットパスのコストは増えない。
	const PMRect pb = Utils<Facade::IGeometryFacade>()->GetItemBounds(
		::GetUIDRef(geo), Transform::PasteboardCoordinates(), Geometry::PathBounds());
	const PMReal l = (pb.Left() < pb.Right()) ? pb.Left() : pb.Right();
	const PMReal r = (pb.Left() < pb.Right()) ? pb.Right() : pb.Left();
	const PMReal t = (pb.Top()  < pb.Bottom()) ? pb.Top()  : pb.Bottom();
	const PMReal b = (pb.Top()  < pb.Bottom()) ? pb.Bottom() : pb.Top();
	outPB = PMRect(l, t, r, b);
	return kTrue;
}

//----------------------------------------------------------------------------------------
// db のページ矩形表を返す(キャッシュ。未作成なら1回だけ全ページを実測して作る)。
// ★ここが同期ホットパスの重い部分を丸ごと肩代わりする: 旧実装は通知のたびに全ページの IGeometry と
//   InnerToPasteboardMatrix を引き直していた。2枠しか持たないのは、arm 中の同期が Target↔Source の
//   2文書だけを行き来するため(それ以外が来たら古い枠を捨てて作り直す=最悪でも従来と同じ仕事量)。
//----------------------------------------------------------------------------------------
static const KESCMPageRectCache* KESCMGetPageRects(IDataBase* db)
{
	if (db == nil)
		return nil;
	for (int i = 0; i < 2; ++i)
		if (sPageRectCache[i].db == db)
			return &sPageRectCache[i];

	// 空き枠を優先し、両方埋まっていたら 0 番を作り直す。
	const int slot = (sPageRectCache[0].db == nil) ? 0 : ((sPageRectCache[1].db == nil) ? 1 : 0);
	KESCMPageRectCache& c = sPageRectCache[slot];
	c.db = db;
	c.pages.clear();
	c.rects.clear();
	KESCMCollectPageUIDs(db, c.pages);
	c.normalCount = c.pages.size();
	KESCMCollectMasterPageUIDs(db, c.pages);	// ★マスターは後ろへ続ける(境目=normalCount。2026-08-11)
	c.rects.resize(c.pages.size());
	for (size_t i = 0; i < c.pages.size(); ++i)
	{
		if (!KESCMPagePasteboardRectRaw(db, c.pages[i], c.rects[i]))
			c.rects[i] = PMRect(0, 0, 0, 0);	// 幾何が取れないページ=空矩形にして下の判定から落とす
	}
	return &c;
}

//----------------------------------------------------------------------------------------
// ページ pageUID(db 内)のペーストボード矩形(キャッシュ経由)。同期経路はこちらを使う。
// 空矩形(幾何が取れなかったページ)は kFalse を返し、旧実装の「取得失敗」と同じ扱いになる。
//----------------------------------------------------------------------------------------
static bool16 KESCMPagePasteboardRect(IDataBase* db, UID pageUID, PMRect& outPB)
{
	if (pageUID == kInvalidUID)
		return kFalse;
	const KESCMPageRectCache* c = KESCMGetPageRects(db);
	if (c == nil)
		return kFalse;
	for (size_t i = 0; i < c->pages.size(); ++i)
	{
		if (c->pages[i] != pageUID)
			continue;
		const PMRect& r = c->rects[i];
		if (r.Right() <= r.Left() && r.Bottom() <= r.Top())
			return kFalse;	// 空矩形=幾何が取れなかったページ
		outPB = r;
		return kTrue;
	}
	return kFalse;
}

//----------------------------------------------------------------------------------------
// db 内で、ペーストボード点 pb を内包するページ UID を返す。内包が無ければ中心が最も近いページ
// (ページ間の隙間/ペーストボード上をビュー中心が指しているとき)。ページが無ければ kInvalidUID。
// ページ矩形はキャッシュから読むので、通知のたびの全ページ実測は起きない。
//----------------------------------------------------------------------------------------
static UID KESCMFindPageAtPasteboard(IDataBase* db, const PBPMPoint& pb)
{
	const KESCMPageRectCache* c = KESCMGetPageRects(db);
	if (c == nil)
		return kInvalidUID;
	UID best = kInvalidUID;
	PMReal bestDist2(0);
	bool16 haveBest = kFalse;
	// ★通常ページだけを見る(2026-08-11)。マスタースプレッドは中心が原点の別座標空間にいて通常ページと
	//   矩形が重なりうるので、混ぜると「通常ページを見ているのに最寄りはマスター」という答えが出る。
	//   マスターページを指しているかは点からは決められない=見ているスプレッドで決まる話なので、
	//   その判定は呼び出し側(KESCMCorrectedCenterForDoc)が SDK の答えで行う。
	for (size_t i = 0; i < c->normalCount; ++i)
	{
		const PMRect& r = c->rects[i];
		if (r.Right() <= r.Left() && r.Bottom() <= r.Top())
			continue;	// 空矩形=幾何が取れなかったページ
		if (pb.X() >= r.Left() && pb.X() <= r.Right() && pb.Y() >= r.Top() && pb.Y() <= r.Bottom())
			return c->pages[i];	// 内包するページが確定
		const PMReal cx = (r.Left() + r.Right()) / PMReal(2.0);
		const PMReal cy = (r.Top()  + r.Bottom()) / PMReal(2.0);
		const PMReal dx = pb.X() - cx, dy = pb.Y() - cy;
		const PMReal d2 = dx * dx + dy * dy;
		if (!haveBest || d2 < bestDist2) { bestDist2 = d2; best = c->pages[i]; haveBest = kTrue; }
	}
	return best;
}

//----------------------------------------------------------------------------------------
// 除外対応表(登録ページを除いた順番対応)の両方向マップを用意する(キャッシュ。未作成なら1回だけ作る)。
// ★旧実装は KESCMMapTargetToSource / KESCMMapSourceToTarget が呼ばれるたびに KESCMBuildPairing を
//   まるごと作り直していた(=両文書の全ページ列挙 + 登録判定 + 線形探索)。同期の通知は毎秒数十回
//   来るので、ここがページ数に比例した固定費になっていた。1世代(250ms または明示無効化まで)に
//   1回だけ作り、以後は map の O(log n) 探索で引く。
//----------------------------------------------------------------------------------------
static void KESCMEnsureSyncPairing(IDataBase* targetDB, IDataBase* sourceDB)
{
	if (sSyncPairBuilt && sSyncPairTargetDB == targetDB && sSyncPairSourceDB == sourceDB)
		return;
	sSyncPairT2S.clear();
	sSyncPairS2T.clear();
	sSyncPairTargetDB = targetDB;
	sSyncPairSourceDB = sourceDB;
	sSyncPairBuilt    = kTrue;	// 対応が空(全ページ登録済み等)でも「作った」ことは覚える
	if (targetDB == nil || sourceDB == nil)
		return;
	InterfacePtr<IKESCMMarkData> marks(Utils<IKESCMMarkData>().QueryUtilInterface());
	std::vector<UID> pairT, pairS;
	marks->GetPagePairing(targetDB, sourceDB, pairT, pairS);
	for (size_t i = 0; i < pairT.size(); ++i)
	{
		sSyncPairT2S[pairT[i]] = pairS[i];
		sSyncPairS2T[pairS[i]] = pairT[i];
	}

	// ★マスタースプレッドの対応も同じ表に入れる(2026-08-11)。ページ UID は文書内で一意なので、
	//   通常ページの対応と1つの map に同居できる。これで「マスターを見ている窓」でも相手のマスター
	//   ページを引けるようになる=Sync Layout Views と Align Other Views がマスターでも噛み合う。
	//   ★比較の対応表(KESCMCore.cpp)と同じ2本立て(通常=順番対応 / マスター=名前対応)を通す。
	std::vector<UID> mT, mS;
	marks->GetMasterPagePairing(targetDB, sourceDB, mT, mS);
	for (size_t i = 0; i < mT.size(); ++i)
	{
		sSyncPairT2S[mT[i]] = mS[i];
		sSyncPairS2T[mS[i]] = mT[i];
	}
}

// pageUID(db 内)がマスタースプレッドのページか(矩形キャッシュの並びで判定。2026-08-11)。
// ★別に IMasterSpreadList を引き直さないのは、この判定が同期ホットパスから呼ばれるため。
//   キャッシュは同じ通知の中で必ず作られている(矩形もそこから引く)ので追加コストはゼロ。
static bool16 KESCMIsMasterPage(IDataBase* db, UID pageUID)
{
	if (pageUID == kInvalidUID)
		return kFalse;
	const KESCMPageRectCache* c = KESCMGetPageRects(db);
	if (c == nil)
		return kFalse;
	for (size_t i = c->normalCount; i < c->pages.size(); ++i)
		if (c->pages[i] == pageUID)
			return kTrue;
	return kFalse;
}

//----------------------------------------------------------------------------------------
// ★ビュー中心にあるページを SDK に聞く(2026-08-06 の API 監査 A-2 の公式ルート)。
//   ①ビューと点からスプレッドを引く = IPasteboardUtils::QuerySpread(view, pt)(IPasteboardUtils.h:83。
//     どのスプレッドにも入っていない点では nil を返すので、:106 の QueryNearestSpread へ落とす)
//   ②そのスプレッド内の最寄りページを聞く = ISpread::QueryNearestPage(pt, &index)(ISpread.h:195)
//   手本=CPathCreationTracker.cpp:278(QuerySpread(fControlView, fFirstPoint))。
//   ★これで「全ページの矩形を自前で実測して内包判定＋最近傍を探す」仕事が SDK 側に移る。
//   見つからなければ kInvalidUID(呼び出し側が従来の自前探索へ落ちる)。
//----------------------------------------------------------------------------------------
static UID KESCMQueryViewCenterPage(IControlView* srcView, const PBPMPoint& center)
{
	if (srcView == nil)
		return kInvalidUID;

	InterfacePtr<ISpread> hitSpread(Utils<IPasteboardUtils>()->QuerySpread(srcView, center));
	ISpread* rawNear = (hitSpread == nil)
		? Utils<IPasteboardUtils>()->QueryNearestSpread(srcView, center)	// ペーストボードの何もない所
		: nil;
	InterfacePtr<ISpread> nearSpread(rawNear);
	ISpread* spread = (hitSpread != nil) ? (ISpread*)hitSpread : (ISpread*)nearSpread;
	if (spread == nil)
		return kInvalidUID;

	int32 pageIndex = -1;
	InterfacePtr<IGeometry> pageGeo(spread->QueryNearestPage(center, &pageIndex));
	if (pageGeo == nil || pageIndex < 0 || pageIndex >= spread->GetNumPages())
		return kInvalidUID;
	return spread->GetNthPageUID(pageIndex);
}

//----------------------------------------------------------------------------------------
// 旧 Alt+ミドル/自動同期の追加/削除補正: 手本(srcDocDb)のビュー中心にあるページを、比較ペアリング
// (KESCMMapTargetToSource / KESCMMapSourceToTarget=登録ページを除外して残りを順番対応させるので、
// ページの追加/削除で番号がズレていても「本来の相手」を返す)で相手ページへ写像し、ページ中心からの
// 相対オフセットを保ったまま相手ページ上の座標へ変換する。これで「増減があっても比較対象のページ同士が
// 同じ位置に映る」。
//   ★outSkip: 手本中心が Added/Removed(登録)・overflow ページ=相手なしのときは kTrue を返す。
//     呼び出し側はこの宛先文書を同期しない(追従側を動かさず据え置く)。相手のいないページで追従側が
//     生座標へ飛ぶのを避けるため(ユーザー指定 2026-07-10)。
//   補正できないが skip でもない場合(未 arm / arm ペア以外の第3文書 / 中心がページ外 / 幾何取得失敗)は
//   srcCenter をそのまま返す=従来の生同期にフォールバックする(outSkip=kFalse)。
//----------------------------------------------------------------------------------------
static PBPMPoint KESCMCorrectedCenterForDoc(IControlView* srcView, IDataBase* srcDocDb, IDataBase* dstDb,
                                            const PBPMPoint& srcCenter, bool16& outSkip, UID& outDstPage)
{
	outSkip = kFalse;
	outDstPage = kInvalidUID;	// 呼び出し側が「宛先ページのスプレッドを映す」ために使う(2026-08-11)

	// ★arm 状態をこの関数だけで7回聞くので、インターフェイスは1回引いて使い回す
	//  (Utils.h:74-80 が明示している作法＝「several places で使うなら InterfacePtr に受けるほうが効率的」)。
	InterfacePtr<IKESCMCompareFacade> compare(Utils<IKESCMCompareFacade>().QueryUtilInterface());
	if (!compare->IsArmed() || compare->GetArmedTargetDB() == nil || compare->GetArmedSourceDB() == nil)
		return srcCenter;	// 未 arm: 生同期

	// ペアリング方向。arm ペア(Target/Source)以外の第3文書は補正しない=生同期。
	const bool16 t2s = (srcDocDb == compare->GetArmedTargetDB() && dstDb == compare->GetArmedSourceDB());
	const bool16 s2t = (srcDocDb == compare->GetArmedSourceDB() && dstDb == compare->GetArmedTargetDB());
	if (!t2s && !s2t)
		return srcCenter;

	// 相手ページを引くための対応表を先に用意する。Added/Removed(登録)・overflow は相手なし →
	// そのページでは同期しない(skip)。★対応表はキャッシュから引く(2026-07-25 追補)。挙動は
	// KESCMMapTargetToSource/SourceToTarget と同一で、「対応表に無い=相手なし」を skip として扱う点も同じ。
	KESCMEnsureSyncPairing(compare->GetArmedTargetDB(), compare->GetArmedSourceDB());
	const std::map<UID, UID>& pairTable = t2s ? sSyncPairT2S : sSyncPairS2T;

	// 手本ページ(srcDocDb 側でビュー中心にあるページ)。
	// ★まず公式ルート(KESCMQueryViewCenterPage)に聞く(2026-08-06 の監査 A-2)。
	// ★★公式ルートは「文書のどのページか」を広く答えるので、マスタースプレッドのページも返ってくる。
	//   2026-08-11 からは**マスターも対応表に載っている**(KESCMEnsureSyncPairing が名前対応を足す)ので、
	//   マスターを見ている窓でも相手のマスターページが引ける。
	//   ⚠**マスターページのときは下の自前探索へ落とさない**＝自前探索は通常ページしか見ないので、
	//   まったく無関係な通常ページを「最寄り」として掴み、相手窓が別の場所へ飛ぶ。相手のマスターが
	//   無いなら、そこは Added ページと同じ「相手なし」＝追従側を動かさないのが正しい。
	//   ⚠隠しスプレッドのページは**対応表に載っている**(平坦化は hide フラグを見ない)ので、この網では
	//   弾けない=Sync+Hide Unchanged 併用時は隠しページの相手へ同期し得る。これは置き換え前の自前探索
	//   (同じ平坦列を使う)でも同じだった既存挙動で、2026-08-06 の再点検では「隠しスプレッドも網に掛かる」
	//   と書いていた旧コメントの方を訂正した。塞ぐなら参照側に hide 判定(IID_IHIDESPREADBOOLDATA)を足す
	//   (★KESCMCollectPageUIDs 自体は比較ペアリングと共用のため変えない)。
	//   (Added/Removed 登録ページも「対応表に無い」ので自前探索へ落ちるが、そちらも同じページを
	//    返すので結果は変わらない=下の skip 判定に進むだけ。稀なケースで探索が2回になるだけの実害)
	UID srcPage = KESCMQueryViewCenterPage(srcView, srcCenter);
	const bool16 srcIsMaster = KESCMIsMasterPage(srcDocDb, srcPage);
	std::map<UID, UID>::const_iterator pairIt = pairTable.find(srcPage);	// kInvalidUID なら end
	if (pairIt == pairTable.end() && !srcIsMaster)
	{
		srcPage = KESCMFindPageAtPasteboard(srcDocDb, srcCenter);
		if (srcPage == kInvalidUID)
			return srcCenter;	// 文書にページが1枚も無い等: 生同期
		pairIt = pairTable.find(srcPage);
	}
	if (pairIt == pairTable.end() || pairIt->second == kInvalidUID)
	{
		outSkip = kTrue;	// ★相手なし(Added / 相手のいないマスター等): 追従側は動かさない
		return srcCenter;
	}
	const UID dstPage = pairIt->second;
	outDstPage = dstPage;

	// ページ内相対位置(ページ中心からのオフセット)を保って相手ページ中心へ移す。
	PMRect srcRect, dstRect;
	if (!KESCMPagePasteboardRect(srcDocDb, srcPage, srcRect) ||
	    !KESCMPagePasteboardRect(dstDb,    dstPage, dstRect))
		return srcCenter;	// 幾何取得失敗: 生同期にフォールバック(skip はしない)
	const PMReal srcCX = (srcRect.Left() + srcRect.Right()) / PMReal(2.0);
	const PMReal srcCY = (srcRect.Top()  + srcRect.Bottom()) / PMReal(2.0);
	const PMReal dstCX = (dstRect.Left() + dstRect.Right()) / PMReal(2.0);
	const PMReal dstCY = (dstRect.Top()  + dstRect.Bottom()) / PMReal(2.0);
	return PBPMPoint(dstCX + (srcCenter.X() - srcCX), dstCY + (srcCenter.Y() - srcCY));
}

// applyPageOffset=kTrue のとき、各宛先文書へ複製する中心座標に上の追加/削除補正を掛ける。
// ★フライアウト「Sync Layout Views」の自動同期は本仕様として kTrue で呼ぶ(比較 arm 中は比較ペアの
// 相手ページ同士がきっちり並ぶ)。ペア外は関数内で生同期にフォールバック。
// (★2026-07-15: 旧「左ダブルクリック=arm 不問・全文書同期」用の limitToArmedPair=kFalse 分岐は、
//  ジェスチャ自体の全廃で呼び出しゼロになっていたため削除。同期は常に Start 中の Target↔Source 限定。)
// ★srcView は手本(操作した)ビュー。ページ対応の補正で「ビュー中心にあるページ」を SDK に聞くために
//   使う(公式ルート=KESCMQueryViewCenterPage。2026-08-06 の監査 A-2)。nil でも動く(自前探索へ落ちる)。
static void KESCMSyncOtherDocViewportsTo(IControlView* srcView, IPanorama* srcPano, IDataBase* srcDocDb,
                                         bool16 applyPageOffset = kFalse)
{
	if (srcPano == nil)
		return;

	// キャッシュ世代の更新はここでも行う(2026-07-25 追補)。Update 経由なら既に呼ばれているので no-op だが、
	// Align Other Views / Sync ON の初回そろえはこの関数を直接呼ぶため、TTL をここでも効かせておく。
	KESCMSyncCacheBeginTick();

	// 同期の作動条件を2モードで判定する:
	//   (A) 比較を Start 中(arm 済み): 従来通り Target↔Source の間だけ・追加/削除補正あり(ユーザー指定 2026-07-11)。
	//       手本(操作した)ビューが Target/Source のどちらでもない第3文書なら同期しない。
	//   (B) 未 Start(Stop 中): アクティブ(操作した)文書へ他の全文書を同期する。
	//       AddRemove(追加/削除補正)は扱わない=applyPageOffset を強制 kFalse。
	//       ★2026-07-24 変更: 旧仕様は「Stop 中は KESCM ツール選択中のみ同期」(誤同期回避が目的)だったが、
	//       この関数に来る時点で ONトグル(sLayoutSyncOn)は必ず ON 確定なので、それを作動条件と見なし、
	//       ツールの選択状態に関係なく同期する(ユーザー指定)。
	InterfacePtr<IKESCMCompareFacade> compare(Utils<IKESCMCompareFacade>().QueryUtilInterface());	// ★1回引いて使い回す(Utils.h:74-80)
	const bool16 armed = (compare->IsArmed() && compare->GetArmedTargetDB() != nil && compare->GetArmedSourceDB() != nil);
	bool16 stopBroadSync = kFalse;
	if (armed)
	{
		if (srcDocDb != compare->GetArmedTargetDB() && srcDocDb != compare->GetArmedSourceDB())
			return;
	}
	else
	{
		stopBroadSync   = kTrue;
		applyPageOffset = kFalse;	// ★AddRemove(追加/削除補正)は掛けない(ユーザー指定)
	}

	// 手本ビューの「見えている状態」を読む。ズームは実効スケール(kTrue=モニタPPI補正込み)。
	// ズームコマンド(kZoomToCmdBoss)が扱う scaleFactor と同じ次元なので、読み書きが対称になる。
	const PMReal  srcZoom   = srcPano->GetXScaleFactor(kTrue);
	const PBPMPoint srcCenter(srcPano->GetContentLocationAtFrameCenter());

	ISession* session = GetExecutionContextSession();	// 終了処理中は nil になり得る(2026-07-25 追補 統一)
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return;

	// ★宛先文書を先に確定する(2026-07-25 追補)。
	//   arm 中(A) … 手本は上のガードで Target/Source のどちらかに確定しているので、宛先は「対の相手」
	//                1文書しかない。旧実装はそれを求めるために毎回 IDocumentList を端から端まで回し、
	//                文書ごとに ::GetUIDRef(doc).GetDataBase() を作っていた。スクロール追従は毎秒数十回
	//                この関数を通るので、相手を直接名指しして生存確認1回に畳む。
	//   Stop 中(B) … 手本以外の全文書が宛先なので、従来どおり列挙する。
	std::vector<IDataBase*> dstDbs;
	dstDbs.reserve(2);
	if (!stopBroadSync)
	{
		IDataBase* dstDb = (srcDocDb == compare->GetArmedTargetDB()) ? compare->GetArmedSourceDB() : compare->GetArmedTargetDB();
		// 相手がまだ開いているかだけ確認する(旧実装は docList 全走査が暗黙に保証していた条件)。
		// ★FindDocByDataBase へのポインタ比較のみ=閉じた db を deref しない(KESCM 共通規約)。
		if (dstDb != nil && dstDb != srcDocDb && docList->FindDocByDataBase(dstDb) != nil)
			dstDbs.push_back(dstDb);
	}
	else
	{
		const int32 docCount = docList->GetDocCount();
		for (int32 d = 0; d < docCount; ++d)
		{
			IDocument* doc = docList->GetNthDoc(d);
			if (doc == nil)
				continue;
			IDataBase* db = ::GetUIDRef(doc).GetDataBase();
			// ★手本の文書自身は丸ごと対象外(スプリット相方も含む。2026-07-04ユーザー指定:
			// 「他のドキュメントにだけ」)。
			if (db == srcDocDb)
				continue;
			dstDbs.push_back(db);
		}
	}
	if (dstDbs.empty())
		return;	// 複製先が無い(=何もしない)。再入ガードを立てる前に抜ける

	// 再入ガードを RAII で立てる(2026-07-25 監査で変更): 複製ループ中の ProcessCommand が万一 throw
	// してもフラグが立ちっぱなし(=以後の同期が永久に無効化)にならない。
	struct KESCMSyncBroadcastGuard
	{
		KESCMSyncBroadcastGuard()  { sLayoutSyncBroadcasting = kTrue; }	// ここからの通知は自分発なのでオブザーバは無視する
		~KESCMSyncBroadcastGuard() { sLayoutSyncBroadcasting = kFalse; }
	} broadcastGuard;

	for (size_t di = 0; di < dstDbs.size(); ++di)
	{
		IDataBase* db = dstDbs[di];

		// この宛先文書へ複製する中心座標。applyPageOffset のときは追加/削除補正(比較ペアの相手ページへ
		// 写像してページ内相対位置を保つ)を掛ける。手本中心が Added ページ(相手なし)なら skip=この宛先は
		// 同期しない(追従側を据え置く)。補正不能だが skip でもないなら srcCenter がそのまま返る(生同期)。
		PBPMPoint dstCenter = srcCenter;
		UID dstPage = kInvalidUID;
		if (applyPageOffset)
		{
			bool16 skipThisDoc = kFalse;
			dstCenter = KESCMCorrectedCenterForDoc(srcView, srcDocDb, db, srcCenter, skipThisDoc, dstPage);
			if (skipThisDoc)
				continue;	// ★手本中心が Added ページ(相手なし)=この宛先文書は同期しない
		}

		// ★宛先ページが載っているスプレッド(2026-08-11)。下でビューごとに「そのスプレッドを映して
		//   いるか」を確かめる。補正が効かなかった経路(生同期)では kInvalidUID のままで、従来どおり
		//   スクロールだけになる。
		UID dstSpread = kInvalidUID;
		if (dstPage != kInvalidUID)
		{
			InterfacePtr<IHierarchy> pageHier(db, dstPage, UseDefaultIID());
			if (pageHier != nil)
				dstSpread = pageHier->GetSpreadUID();
		}

		K2Vector<IControlView*> views;
		Utils<ILayoutViewUtils>()->GetAllLayoutViews(views, nil, db);

		for (int32 vi = 0; vi < (int32)views.size(); ++vi)
		{
			IControlView* view = views[vi];
			if (view == nil)
				continue;

			InterfacePtr<IPanorama> pano(KESCMQueryPanorama(view));
			if (pano == nil)
				continue;

			// ★★宛先ページのスプレッドを先にこのビューへ映す(2026-08-11)。
			//   スクロール(IPanorama)は今映しているスプレッドの座標空間の中でしか動けないので、
			//   別スプレッド——とくに**マスタースプレッド(中心が原点の別空間)**——へは届かず、
			//   空のペーストボードに着地する([[scroll-needs-spread-switch]])。判定は「マスターか」
			//   ではなく「違うスプレッドか」＝公式の作法(手本 SnapTracker.cpp:224 に特例なし)で、
			//   Prev/Next と同じ関数を通す([[one-question-one-place]])。既に映していれば何もしない
			//   ので、通常スプレッド内をスクロールしている間はコマンドが1回も走らない。
			if (dstSpread != kInvalidUID)
				KESCMEnsureViewShowsSpread(view, db, dstSpread);

			// 既に手本と一致しているビューは触らない。スクロールドラッグ/ズーム操作中は通知が高頻度で
			// 来るため(自動同期経由)、一致済みビューへの再スクロール+forceRedraw を毎回打たない。
			// 中心の許容差はズーム換算で約1.5画面px(スクロール位置は画面px量子化されるため、pt固定の
			// 微小許容差だと低倍率で永遠に「不一致」になる)。この範囲のズレは見た目に出ない。
			const PMReal zoomDiff = abs(pano->GetXScaleFactor(kTrue) - srcZoom);
			const bool16 zoomMatched = (zoomDiff <= PMReal(0.0001));
			if (zoomMatched)
			{
				const PMPoint curCenter = pano->GetContentLocationAtFrameCenter();
				const PMReal dx = abs(curCenter.X() - dstCenter.X());
				const PMReal dy = abs(curCenter.Y() - dstCenter.Y());
				const PMReal tol = (srcZoom > PMReal(0.0001)) ? (PMReal(1.5) / srcZoom) : PMReal(1.0);
				if (dx <= tol && dy <= tol)
					continue;	// 位置も拡大率も一致済み
			}

			// 拡大率を手本と同じ実効スケールへ。ズームは UI のズーム欄と同じ公式コマンド
			// (kZoomToCmdBoss)で行う。既定引数=ビュー中心基準。
			// ★ILayoutViewUtils::ZoomLayoutViews 直呼びは他文書のビューに効かない(実機確認)ため不可。
			// ★★ズームは Command なのに下のスクロールは IPanorama 直操作、という非対称には理由がある
			//   (2026-08-06 の API 監査で確認): 公式のスクロールコマンドは
			//   ILayoutUIUtils::MakeScrollToSpreadCmd(:252) だけで、行き先が「スプレッド中心 または
			//   直前と同じ中心オフセット」に限られ、**任意の content 座標を指定できない**。
			//   スクロール位置はモデルではなくビュー状態なので、Command を通さないこと自体は筋が通る。
			if (!zoomMatched)
			{
				InterfacePtr<ICommand> zoomCmd(Utils<ILayoutUIUtils>()->MakeZoomCmd(view, srcZoom));
				if (zoomCmd == nil || CmdUtils::ProcessCommand(zoomCmd) != kSuccess)
				{
					// ★ズーム合わせは同期表示の便宜で、失敗してもスクロール同期は続けてよい。ただし
					//   エラー状態を持ち越すと後続コマンドが巻き添えで失敗する
					//   ([[command-sequence-rollback-on-error]])ので掃除して続行(2026-08-06 再点検。
					//   KESCMChangeNav.cpp のズームと同じ作法)。
					ErrorUtils::PMSetGlobalErrorCode(kSuccess);
				}
			}
			// 手本の可視中心と同じ content 座標(補正時は相手ページ上の対応座標)をビュー中心へ=
			// 同じ拡大率なら同じ画面が同じように映る。ズーム(コマンド)実行後に行うので、新しい倍率で
			// 正しくセンタリングされる。
			// 自動同期(高頻度通知)ではその都度の同期再描画を避け、invalidate だけして OS の描画
			// サイクルにまとめさせる(軽量化)。スクロール位置は同期更新されるので上の dedup 判定は
			// forceRedraw=kFalse でも正しく効く。
			pano->ScrollContentLocationToFrameCenter(dstCenter, kFalse /*forceRedraw*/);
		}
	}
	// (sLayoutSyncBroadcasting は broadcastGuard のデストラクタが戻す)
}

//========================================================================================
// レイアウトビュー同期(フライアウト「Sync Layout Views」チェック式トグル)
//   ON の間、全ドキュメントの全レイアウトビューの IPanorama subject を購読し、どれかが
//   スクロール/ズームしたら KESCMSyncOtherDocViewportsTo で他文書のビューへ複製する(自動・ライブ)。
//   さらに ActiveContext(文書切替)も購読し、新しく開いた文書のビューを購読へ追加する。
//   Start(枠)とは完全に独立=単独で ON にできる。
//
//   手本=ユーザー自作の旧KESプラグインの KESLayoutScrollObserver(work/ に原本)。改善点:
//   ①購読対象を「アクティブ文書の最初のプレゼンテーションの元側ペインのみ(QueryFrontView 一致時)」
//     から「全文書の全レイアウトビュー(スプリット新側・2枚目以降のウィンドウ込み)」へ拡大。
//     どのウィンドウを動かしてもそれが手本になり、QueryFrontView のアクティブ追跡ズレ
//     (Split Window で実測済みの罠)にも影響されない。
//   ②多重購読でも無限ループ/ピンポンしないよう、再入ガード(sLayoutSyncBroadcasting)で
//     複製中に発生する自分発の通知を無視する(手本は「単一ビューだけ購読」で回避していた)。
//   ③同期エンジンは 旧 Alt+ミドルと共通(kZoomToCmdBoss+実効スケール対称読み書き=本日実機確定の手順)。
//========================================================================================

static bool16 sLayoutSyncOn = kFalse;			// トグル状態(セッション内のみ保持)

// 同期オブザーバの実体を ActiveContext boss から引く(+1 ref、呼び出し側は InterfacePtr で受ける)。
// ★実証済み構成: .fr の AddIn で kActiveContextBoss に IID_IKESCMLAYOUTSYNCOBSERVER として同居させる
// (手本=ユーザー自作 KESLayoutScrollObserver と同じ)。
// ★当初の失敗の原因(特定済み): AttachObserver の第4引数 asObserver は「オブザーバ実装がそのboss上で
// 実際に載っているインターフェイスID」を渡す契約(ISubject.h の記述+IChangeManager は依存を
// (subject, observer, observerIID, interestedIn) で管理し boss+IID で引き直せる前提の設計。
// CSubject.h/IChangeManager.h は docs HTML のみに存在)。当初は実装を IID_IOBSERVER で載せた独立boss
// (CreateObject2)に、boss上に存在しない IID_IKESCMLAYOUTSYNCOBSERVER を asObserver として渡していた
// ため Update が届かなかった。現構成は実装が実際に IID_IKESCMLAYOUTSYNCOBSERVER で載っており整合する。
static IObserver* KESCMQueryLayoutSyncObserver()
{
	// session は終了処理中に nil になり得る(2026-07-25 追補 に KESCM 全体で統一)。
	ISession* session = GetExecutionContextSession();
	IActiveContext* ctx = (session != nil) ? session->GetActiveContext() : nil;
	if (ctx == nil)
		return nil;
	return (IObserver*)ctx->QueryInterface(IID_IKESCMLAYOUTSYNCOBSERVER);
}

// 全レイアウトビューへ購読を付ける(未購読のものだけ)。ON時と、ON中の文書切替時に呼ばれ、
// 新しく開いた文書・新しく現れたウィンドウのビューを取りこぼさない。
static void KESCMLayoutSyncAttachAllPanoramas()
{
	InterfacePtr<IObserver> obs(KESCMQueryLayoutSyncObserver());
	if (obs == nil)
		return;
	K2Vector<IControlView*> views;
	Utils<ILayoutViewUtils>()->GetAllLayoutViews(views, nil, nil);	// db=nil で全ドキュメントの全レイアウトビュー
	for (int32 i = 0; i < (int32)views.size(); ++i)
	{
		if (views[i] == nil)
			continue;
		InterfacePtr<ISubject> subject(views[i], UseDefaultIID());
		if (subject == nil)
			continue;
		if (!subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IPANORAMA, IID_IKESCMLAYOUTSYNCOBSERVER))
			subject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IPANORAMA, IID_IKESCMLAYOUTSYNCOBSERVER);
	}
}

// 全レイアウトビューから購読を外す(OFF時/Shutdown時)。既にクローズされたビューは
// GetAllLayoutViews に現れない=購読はビュー破棄と一緒に消えているので、生存分だけ外せばよい。
static void KESCMLayoutSyncDetachAllPanoramas()
{
	InterfacePtr<IObserver> obs(KESCMQueryLayoutSyncObserver());
	if (obs == nil)
		return;
	K2Vector<IControlView*> views;
	Utils<ILayoutViewUtils>()->GetAllLayoutViews(views, nil, nil);
	for (int32 i = 0; i < (int32)views.size(); ++i)
	{
		if (views[i] == nil)
			continue;
		InterfacePtr<ISubject> subject(views[i], UseDefaultIID());
		if (subject == nil)
			continue;
		if (subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IPANORAMA, IID_IKESCMLAYOUTSYNCOBSERVER))
			subject->DetachObserver(ISubject::kRegularAttachment, obs, IID_IPANORAMA, IID_IKESCMLAYOUTSYNCOBSERVER);
	}
}

// ActiveContext(文書切替の通知源)への購読を付け外しする。ActiveContext はセッションに1つの
// 永続オブジェクトなので、付け外しは ON/OFF 時の各1回でよい。
static void KESCMLayoutSyncAttachContext(bool16 attach)
{
	ISession* session = GetExecutionContextSession();	// 終了処理中は nil になり得る
	IActiveContext* ctx = (session != nil) ? session->GetActiveContext() : nil;
	if (ctx == nil)
		return;
	InterfacePtr<IObserver> obs((IObserver*)ctx->QueryInterface(IID_IKESCMLAYOUTSYNCOBSERVER));
	if (obs == nil)
		return;
	InterfacePtr<ISubject> subject(ctx, UseDefaultIID());
	if (subject == nil)
		return;
	const bool16 attached = subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IACTIVECONTEXT, IID_IKESCMLAYOUTSYNCOBSERVER);
	if (attach && !attached)
		subject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IACTIVECONTEXT, IID_IKESCMLAYOUTSYNCOBSERVER);
	else if (!attach && attached)
		subject->DetachObserver(ISubject::kRegularAttachment, obs, IID_IACTIVECONTEXT, IID_IKESCMLAYOUTSYNCOBSERVER);
}

/** レイアウトビュー同期オブザーバの実装。.fr の AddIn で kActiveContextBoss に
    IID_IKESCMLAYOUTSYNCOBSERVER として同居させている(手本と同じ実証済み構成)。 */
class KESCMLayoutSyncObserver : public CObserver
{
public:
	// ★第2引数は「この実装が boss 上で実際に載っている IID」(CObserver.h:55 の fAttachIID)。
	//   .fr の AddIn は IID_IKESCMLAYOUTSYNCOBSERVER で載せ、Attach もその IID で行うので、
	//   ここも同じものを渡して自己申告と実態を一致させる(公式=layerpanel/CLayoutLayerListObserver.cpp:112。
	//   2026-08-06 の API 監査 A-3。既定のままだと GetAttachIID() が IID_IOBSERVER を返して食い違う)。
	KESCMLayoutSyncObserver(IPMUnknown* boss) : CObserver(boss, IID_IKESCMLAYOUTSYNCOBSERVER) {}
	~KESCMLayoutSyncObserver() {}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);
};

CREATE_PMINTERFACE(KESCMLayoutSyncObserver, kKESCMLayoutSyncObserverImpl)

void KESCMLayoutSyncObserver::Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy)
{
	if (!sLayoutSyncOn || sLayoutSyncBroadcasting)
		return;	// OFF、または自分発(複製中)の通知

	// 文書切替(IID_IDOCUMENT)またはアクティブビュー切替(IID_ICONTROLVIEW)で購読を付け直す
	// (未購読分にだけ付く。手本の KESLayoutScrollObserver は文書切替のみだったが、ビュー切替も見る
	// ことで、ON 中に作られた同一文書の新規ウィンドウ/Split Window の新側ペインも、クリックして
	// アクティブになった瞬間に購読される=そのペインを動かしても手本になれる)。
	if (protocol.Get() == IID_IACTIVECONTEXT)
	{
		IActiveContext::ContextInfo* info = (IActiveContext::ContextInfo*)changedBy;
		if (info != nil && (info->Key() == IID_IDOCUMENT || info->Key() == IID_ICONTROLVIEW))
			KESCMLayoutSyncAttachAllPanoramas();
		return;
	}

	if (protocol.Get() != IID_IPANORAMA)
		return;
	if (theChange != kScrollToMessage && theChange != kScrollByMessage &&
	    theChange != kScaleToMessage  && theChange != kScaleByMessage)
		return;

	// 通知元(=手本)のパノラマ。theSubject はレイアウトビュー boss の subject なので、
	// 同じ boss から IPanorama / IControlView を引ける。
	InterfacePtr<IPanorama> srcPano(theSubject, UseDefaultIID());
	if (srcPano == nil)
		return;

	// ★同一状態の通知を弾く(2026-07-25 追補)。1回のスクロール/ズーム操作で kScrollTo と kScrollBy のように
	//   複数の通知が続けて届くことがあり、その全部で複製一式(ページ対応の解決 + 全宛先ビューの走査)を
	//   走らせるのは無駄。手本の (パノラマ, 実効ズーム, 可視中心) が前回複製したときと完全に同じなら
	//   何も変わっていないので即戻る。この判定はパノラマ2回読みだけで済む=最も安いふるい。
	//   ★取りこぼしは無い: 宛先側だけが動いた場合は、その宛先ビュー自身からも通知が来て、そちらが
	//     手本として処理される(手本と宛先は固定ではない)。
	//   ★sLastSrcPano はポインタ照合にしか使わない(deref しない)。別ビューが同じアドレスを再利用
	//     しても、ズーム/中心まで一致しない限り弾かれないので実害は無い。250ms の TTL でも失効する。
	KESCMSyncCacheBeginTick();	// TTL 超過ならこの中でキャッシュ一式(前回状態を含む)が捨てられる
	const PMReal    curZoom = srcPano->GetXScaleFactor(kTrue);
	const PBPMPoint curCenter(srcPano->GetContentLocationAtFrameCenter());
	if (sHaveLastSrcState && sLastSrcPano == (IPanorama*)srcPano)
	{
		const PMReal dz  = abs(curZoom - sLastSrcZoom);
		const PMReal dcx = abs(curCenter.X() - sLastSrcCenter.X());
		const PMReal dcy = abs(curCenter.Y() - sLastSrcCenter.Y());
		if (dz <= PMReal(0.0) && dcx <= PMReal(0.0) && dcy <= PMReal(0.0))
			return;	// 手本は前回複製時から1ミリも動いていない
	}

	InterfacePtr<IControlView> srcView(theSubject, UseDefaultIID());
	if (srcView == nil)
		return;
	IDataBase* srcDocDb = KESCMFindDocDbForView(srcView);
	if (srcDocDb == nil)
		return;	// 所属文書を特定できない(クローズ途中等)。同期しない

	// ここから実際に複製する=この状態を「前回複製した状態」として記録する。
	sLastSrcPano      = (IPanorama*)srcPano;
	sLastSrcZoom      = curZoom;
	sLastSrcCenter    = curCenter;
	sHaveLastSrcState = kTrue;

	// ★本仕様(2026-07-10 確定): 自動同期(ライブ)にも追加/削除補正を掛ける。比較 arm 中は比較ペアの
	// 相手ページ同士がきっちり並ぶ(実機で使い勝手を確認済み)。未 arm/ペア外は関数内で生同期にフォールバック。
	KESCMSyncOtherDocViewportsTo(srcView, srcPano, srcDocDb, kTrue /*applyPageOffset*/);
}

// KESCMGetLayoutSync / KESCMSetLayoutSync(KESCMViewSync.h で宣言) — フライアウトトグルの実体。
bool16 KESCMGetLayoutSync()
{
	return sLayoutSyncOn;
}

void KESCMSetLayoutSync(bool16 on)
{
	if ((on && sLayoutSyncOn) || (!on && !sLayoutSyncOn))
		return;

	KESCMInvalidateSyncCaches();	// ON/OFF のどちらでも、次の同期は最新の実測から始める(2026-07-25 追補)

	if (on)
	{
		// オブザーバは kActiveContextBoss に AddIn 済み(.fr)。取得できない環境なら ON にしない。
		InterfacePtr<IObserver> obs(KESCMQueryLayoutSyncObserver());
		if (obs == nil)
			return;
		sLayoutSyncOn = kTrue;
		KESCMLayoutSyncAttachContext(kTrue);
		KESCMLayoutSyncAttachAllPanoramas();

		// ON にした瞬間に一度そろえる(手本=最前面のレイアウトビュー)。以後は通知駆動のライブ同期。
		InterfacePtr<IControlView> front(Utils<ILayoutUIUtils>()->QueryFrontView());
		if (front != nil)
		{
			InterfacePtr<IPanorama> pano(KESCMQueryPanorama(front));
			IDataBase* db = KESCMFindDocDbForView(front);
			if (pano != nil && db != nil)
				KESCMSyncOtherDocViewportsTo(front, pano, db, kTrue /*applyPageOffset(本仕様): 初回そろえも補正付き*/);
		}
	}
	else
	{
		sLayoutSyncOn = kFalse;
		KESCMLayoutSyncDetachAllPanoramas();
		KESCMLayoutSyncAttachContext(kFalse);
		// オブザーバ本体は kActiveContextBoss 所属(AddIn)なので、寿命管理は不要。
	}
}

// KESCMAlignOtherViewsToActiveNow(KESCMViewSync.h で宣言) — フライアウト/ショートカットの実行アクション。
// アクティブ(最前面)レイアウトビューの位置+拡大率を他文書の全ビューへ1回そろえる。Sync Layout Views
// トグルの ON/OFF とは独立で、OFF でも押せば1回だけそろう(トグル ON 時の初回そろえ=上の KESCMSetLayoutSync
// と同じ手本ビュー取得+同じ同期エンジン呼び出し)。applyPageOffset=kTrue を渡すので、Start 中(arm)は
// KESCMSyncOtherDocViewportsTo のガードにより Target↔Source 間でページの Add/Remove 補正が掛かり(賢く一致)、
// 未 Start 時は同関数が補正を kFalse に強制して他の全文書へ生同期する。
bool16 KESCMAlignOtherViewsToActiveNow()
{
	// 明示アクションなので、キャッシュの鮮度に関係なく必ず今の実測でそろえる(2026-07-25 追補)。
	KESCMInvalidateSyncCaches();

	InterfacePtr<IControlView> front(Utils<ILayoutUIUtils>()->QueryFrontView());
	if (front == nil)
		return kFalse;
	InterfacePtr<IPanorama> pano(KESCMQueryPanorama(front));
	IDataBase* db = KESCMFindDocDbForView(front);
	if (pano == nil || db == nil)
		return kFalse;
	// ★Start 中(arm)は同期エンジンが Target↔Source 間だけに限定し、最前面が第3文書なら何もせず戻る。
	//   その場合に「そろえた」と誤って成功表示を出さないよう、engine と同じ条件をここで先読みして kFalse を
	//   返す(2026-07-24。下の KESCMSyncOtherDocViewportsTo の armed ガードと必ず同条件に保つ)。
	InterfacePtr<IKESCMCompareFacade> compare(Utils<IKESCMCompareFacade>().QueryUtilInterface());
	if (compare->IsArmed() && compare->GetArmedTargetDB() != nil && compare->GetArmedSourceDB() != nil &&
	    db != compare->GetArmedTargetDB() && db != compare->GetArmedSourceDB())
		return kFalse;
	KESCMSyncOtherDocViewportsTo(front, pano, db, kTrue /*applyPageOffset(arm時のみ補正・未arm時は関数内でkFalse強制)*/);
	return kTrue;
}

// KESCMViewSyncShutdown(KESCMViewSync.h で宣言) — 終了時の後始末。
void KESCMViewSyncShutdown()
{
	// レイアウトビュー同期の後始末は「状態フラグを落とすだけ」にする(以後の通知は Update 先頭の
	// ガードで無視される)。★KESCMSetLayoutSync(kFalse) をここで呼んではならない: その経路は
	// GetActiveContext()/GetAllLayoutViews に触るが、アプリ終了処理中はセッション/コンテキストが
	// 解体中で deref すると 100% クラッシュする(実機で確認済み。ON のまま終了→必ず落ちた)。
	// 手本の KESLayoutScrollObserver も終了時は何も外さず無事故=購読(依存)は subject/observer の
	// boss 破棄と一緒に IChangeManager から消えるので、明示的な解除は不要。
	sLayoutSyncOn = kFalse;
}
