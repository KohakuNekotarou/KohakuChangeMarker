//========================================================================================
//
//  KESCMRingAdornment.cpp
//
//  比較マークをグローバルページアイテムアドーンメントとして描く経路。何のためか・どういう仕組みかは
//  KESCMRingAdornment.h に全部書いてある。ここは中身:
//
//    1) KESCMRingAdornmentShape       … IAdornmentShape。**スプレッドに対してだけ**描画本体を呼ぶ
//    2) KESCMRingFlattenerUsage       … IAdornmentFlattenerUsage。★本命＝透明マネージャへの申告口
//    3) 登録/解除/判定の3関数         … セッションのグローバルリストへの出し入れ
//
//  ★描画の中身は1行も持たない。KESCMDrawEventHandler::DrawSpreadMarks() をそのまま呼ぶので、
//    **絵は Draw Event 経路と同一**(リング・斜線・✓・旧番号バッジ・除外塗り、印刷/PDF の
//    アルファサーバ経路まで全部)。この経路が足すのは「誰に呼ばれるか」と「透明の申告」だけ。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IAdornmentShape.h"
#include "IAdornmentFlattenerUsage.h"
#include "IPageItemAdornmentList.h"		// GenericID.h を巻き込むので IID_IGLOBALPAGEITEMADORNMENTLIST もこれで足りる
#include "ISession.h"					// GetExecutionContextSession()
#include "ISpread.h"					// 「今描いているのはスプレッドか」の判定
#include "IShape.h"
#include "IGeometry.h"				// GetStrokeBoundingBox(ink bounds の実験)
#include "IDrwEvtHandler.h"				// DrawEventData(描画本体へ渡す形)
#include "IGraphicsContext.h"			// GraphicsData
#include "IStartupShutdownService.h"	// スレッドごとの登録(このファイルの末尾)
#include "IXPUtils.h"					// QueryXPManager(db)
#include "IXPManager.h"					// ItemXPChanged(＝「透明を持つアイテムの一覧」を作り直させる)
#include "ISpreadList.h"				// 文書のスプレッドを辿る
#include "IDataBase.h"					// GetRootUID

// General includes:
#include "CPMUnknown.h"
#include "UIDList.h"
#include "Utils.h"

// Project includes:
#include "KESCMID.h"
#include "KESCMRingAdornment.h"
#include "KESCMDrawEventHandler.h"		// DrawSpreadMarks(描画本体) / マーク状態の static
#include "KESCMThreadSafety.h"			// KESCMMarkStateMutex/Lock(sEntries を読むため)

//========================================================================================
// 登録状態
//========================================================================================

namespace
{
	// (★ここに「登録できているか」を憶える static bool があったが **2026-08-19 に削除した**。
	//  static はスレッドをまたいで共有されるのに、登録先のセッションはまたがない ---- その食い違いが
	//  「UI の PDF 書き出しから枠が消える」を生んだ。**状態はセッションに聞く。憶えない。**
	//  経緯は KESCMRingAdornmentIsActive() の中のコメント。)

	/** 透明の申告(IsFlattenerRequired_)を出すか。**kTrue が正**で、kFalse は切り戻し用。

		★★★**この1つで PDF 1.3 の全面ベタが決まる**(2026-08-19 実測。A/B とも同一文書・同一プリセット
		`[雑誌広告送稿用]`＝Acrobat 4・透明を1つも含まないページで計測):

		| kDeclareFlattenerUsage | そのページの絵 | 主要色 |
		|---|---|---|
		| **kTrue**  | **リングが半透明** (74,503 画素) | `240,192,176` ＝白地に25%の赤 |
		| kFalse | **ページ全面が赤いベタ** (850,175 画素) | `224,0,16` ＝ほぼ純赤 |

		★同期(メインスレッド)・非同期(BG＝UI の書き出し)とも同じ値になった＝**申告は両方のスレッドで
		  Query されている**(グローバル登録した boss にもちゃんと聞きに来る)。
		⇒ ★**「アドーンメントにする」だけでは足りない。効いているのは申告のほう。**
		  アドーンメント化が要るのは、**申告する口(IID_IADORNMENTFLATTENERUSAGE)がアドーンメント boss
		  にしか載らないから**であって、描き方が変わるからではない。 */
	const bool16 kDeclareFlattenerUsage = kTrue;

	/** アドーンメント経路を使うか。**kTrue が正**で、kFalse は**切り戻し用の非常口**
		(登録しない ⇒ KESCMRingAdornmentIsActive() が常に kFalse ⇒ 従来の Draw Event 経路が全部描く)。

		★残してあるのは、**この2経路が同じ絵を描く**ように作ってあるから ---- 描画の中身は
		KESCMDrawEventHandler::DrawSpreadMarks() 1本で、変わるのは「誰に呼ばれるか」だけ。
		∴ 倒せば 2026-08-19 以前の挙動にそのまま戻る(PDF 1.3 の全面ベタも戻るが、それ以外は同じ)。
		★**測り方の検算にも使った**＝新経路の PDF がおかしいとき、「新経路の症状か / 測り方の問題か」を
		  分けるには従来経路で同じ手順を回すのが唯一の手だった(実際それで
		  「1.4 のつもりが全部 1.3 で書き出されていた」という**測り方の誤り**を見つけた)。 */
	const bool16 kUseRingAdornment = kTrue;

	/** ★**実験用。kFalse が正。** kTrue にすると
		 ①`KESCMRingAdornmentRefreshItemXPState()` を黙って何もしないようにし
		 ②代わりに `AddToContentInkBounds` で「このアイテム全体にインクを置く」と申告する
		---- つまり **spellpanel が持っていて KESCM が持っていなかった唯一の口(ink bounds)が、
		透明マネージャへの通知の代わりになるか**を測るための切り替え。

		★★★**2026-08-20 実測＝ならない。** 同一文書・同一プリセット(`[雑誌広告送稿用]`＝PDF 1.3)・
		同一の測定スクリプト(`work/kescm-adorn/isolate-doc.ps1`)で:

		| | 変更ページの画素 |
		|---|---|
		| 通知あり(＝現行) | **`red 0` / 淡赤 40,847**(半透明) |
		| 通知を止めて ink bounds を申告 | ⚠**`red 862,283`**(全面ベタ) |

		⇒ **ink bounds はフラットナの判定に一切関与しない**。契約(`IAdornmentShape.h:138-140`＝
		  「枠基準のアドーンメントは実装不要」)と公式のコメント("used for **resizing textframe** etc."
		  ＝`TranFxAdornment.cpp:483`)のとおりだった。
		∴ **`ItemXPChanged` の通知は代替不能。** この定数は結論を書き残すために残してある。 */
	const bool16 kTestInkBoundsInsteadOfNotify = kFalse;
}

//========================================================================================
// 1) アドーンメント本体
//========================================================================================

/** スプレッドに対してだけ、比較マークの描画本体を呼ぶ。 */
class KESCMRingAdornmentShape : public CPMUnknown<IAdornmentShape>
{
public:
	KESCMRingAdornmentShape(IPMUnknown* boss) : CPMUnknown<IAdornmentShape>(boss) {}
	~KESCMRingAdornmentShape() {}

	/** ⚠★★★**単一のビットでなければ1回も呼ばれない**(2026-08-19 KT で実測)。
		`kBeforeShape | kAfterShape` を返すと呼び出しゼロになる ---- 配布は
		IAdornmentIterator(paintOrderMask) が行い(CShape.cpp:127)、複合値はどのパスにも一致しないため。
		★症状が「画面に何も出ない」なので描画コードの不備と見分けが付かない。ここは触らないこと。
		マークは中身の上に重ねるので kAfterShape(シェイプを描いた後)。 */
	virtual AdornmentDrawOrder GetDrawOrderBits() { return kAfterShape; }

	virtual void DrawAdornment(IShape* iShape, AdornmentDrawOrder drawOrder,
							   GraphicsData* gd, int32 flags);

	/** ★`itemBounds` は**呼び出し元が変換し終えた矩形のコピー**で、戻り値はそこへ Union される
		(CShape.cpp:648-660)。∴ **はみ出さないなら素で返すのが正しい**。マークはページ/スプレッドの
		内側にしか描かないので、広げる必要は無い。
		⚠ここで `innertoview` を掛け直すのは二重変換になる ---- KT が 2026-08-19 まで、
		  `framelabel/FrmLblAdornment.cpp:412-430` は今も(逆向きに)そうしている。
		  **正は呼び出し元のコード(Union の相手)だけ**。 */
	virtual PMRect GetPaintedAdornmentBounds(IShape* /*iShape*/, AdornmentDrawOrder /*drawOrder*/,
											 const PMRect& itemBounds, const PMMatrix& /*innertoview*/)
		{ return itemBounds; }

	/** 上と同じ契約。★こちらは WillPrint() が kTrue のときしか到達しない(CShape.cpp:93)。 */
	virtual PMRect GetPrintedAdornmentBounds(IShape* /*iShape*/, AdornmentDrawOrder /*drawOrder*/,
											 const PMRect& itemBounds, const PMMatrix& /*innertoview*/)
		{ return itemBounds; }

	/** ★空実装が正。**契約が明示的にこちらを免除している** ----
		`IAdornmentShape.h:138-140`＝"This is only used by adornments for which the inking bounds
		are based on **the content**. Adornments for which inking bboxes are based **solely on the
		frame** do not need to implement this routine."
		マークはページ/スプレッドの箱を基準に描く(GetPaintedAdornmentBounds が itemBounds を素で返す)
		＝枠基準なので免除側。公式も**枠の外へ滲む transparencyeffect だけが実装**しており、
		`framelabel/FrmLblAdornment.cpp:160` は**空 `{}`**。★用途もフラットナではない ----
		`TranFxAdornment.cpp:483` のコメントが **"used for resizing textframe etc."** と書いている。
		⚠**2026-08-20 に実測でも確かめた**（下の kTestInkBoundsInsteadOfNotify を参照）。 */
	virtual void AddToContentInkBounds(IShape* iShape, PMRect* inOutBounds);

	virtual PMReal GetPriority() { return 0; }

	/** 無効化は従来どおりマーク側(KESCMInvalidate…)が文書ビューごと行うので、ここでは何もしない。 */
	virtual void Inval(IShape* /*iShape*/, AdornmentDrawOrder /*drawOrder*/, GraphicsData* /*gd*/,
					   ClassID /*reasonForInval*/, int32 /*flags*/) {}

	/** ★★kTrue でなければならない。⚠**「描かせるため」ではなく「印刷 bbox に算入させるため」**
		---- DrawPageItemAdornments はこの値を見ない(CShape.cpp:117-141)が、
		UnionPrintingPageItemAdornmentPaintedBBox は見る(:93)。kFalse だと印刷/書き出しの
		描画範囲の計算から丸ごと外れる。
		★「印刷に出すか」自体は従来どおり描画本体が sPrintMarks で決める(ここでは決めない)。 */
	virtual bool16 WillPrint() { return kTrue; }

	/** テキストのオフスクリーン描画を中断させるかどうか(背景へ描くアドーンメント用)。
		マークは前面に重ねるので kFalse。 */
	virtual bool16 WillDraw(IShape* /*iShape*/, AdornmentDrawOrder /*drawOrder*/,
							GraphicsData* /*gd*/, int32 /*flags*/) { return kFalse; }

	/** マークはクリックを拾わない(見えるだけ)。 */
	virtual bool16 HitTest(IShape* /*iShape*/, AdornmentDrawOrder /*adornmentDrawOrder*/,
						   IControlView* /*layoutView*/, const PMRect& /*mouseRect*/) { return kFalse; }
};

CREATE_PMINTERFACE(KESCMRingAdornmentShape, kKESCMRingAdornmentImpl)

void KESCMRingAdornmentShape::DrawAdornment(IShape* iShape, AdornmentDrawOrder drawOrder,
											GraphicsData* gd, int32 flags)
{
	if (drawOrder != kAfterShape || iShape == nil || gd == nil)
		return;

	// ★★グローバルリストに載っているので、**このスプレッド上の全ページアイテム1つ1つについて**
	//   ここへ来る。マークはスプレッド単位で1回描けばよいので、スプレッド以外は全部捨てる。
	//   ⚠**ページ(kPageBoss)でもなくスプレッド(kSpreadBoss)を選ぶ理由は座標系**:
	//     描画本体は「ページの箱を spread 座標で取って描く」形で書かれており、
	//     スプレッドの内部座標＝spread 座標なので**そのまま渡せる**。ページを選ぶと
	//     ページのオフセットぶんずれる。
	InterfacePtr<ISpread> spread(iShape, UseDefaultIID());
	if (spread == nil)
		return;

	// 描画本体へ渡す形を組む。changedBy には iShape をそのまま渡す ---- 本体は changedBy から
	// ISpread と IDataBase を引くだけで、どちらも同じ boss から取れる(iShape と spread は同一 boss)。
	DrawEventData ded(iShape, gd, flags);
	KESCMDrawEventHandler::DrawSpreadMarks(&ded);
}

/** 契約どおり**何もしない**（上の宣言のコメントを参照）。実験スイッチを立てたときだけ、
	spellpanel が GetInkBounds でやっているのと同じ「ここにインクを置く」の申告を出す。 */
void KESCMRingAdornmentShape::AddToContentInkBounds(IShape* iShape, PMRect* inOutBounds)
{
	if (!kTestInkBoundsInsteadOfNotify || iShape == nil || inOutBounds == nil)
		return;
	InterfacePtr<IGeometry> geo(iShape, UseDefaultIID());
	if (geo == nil)
		return;
	PMRect box = geo->GetStrokeBoundingBox();	// inner 座標(契約 :141「The bounds are based on inner coordinates」)
	inOutBounds->Union(box);
}

//========================================================================================
// 2) ★本命 ---- 透明マネージャへの申告
//========================================================================================

/** 「このアドーンメントは透明を使っている」と本体へ答える。手本＝
	`sdksamples/transparencyeffect/TranFxFlattenerUsage.cpp`(中身は `return kTrue` の1行)。

	★★★これが PDF 1.3 の全面ベタを解く鍵。1.3 に透明は無いので、半透明を出すにはフラットナ
	(平坦化)を通るしかない。フラットナは**アートワークを集めてからラスタライズする**ので、
	集める段階で「透明がある」と申告した相手だけが対象になる。Draw Event はアートワークを
	集め終わった後の描画中に呼ばれるため、そもそも申告する機会が無かった ---- それがこの
	クラスを足した理由そのもの。

	⚠**申告の相手は2種類あって別物**(IID も別):
	  ・`IFlattenerUsage`(`IsFlattenerRequired`)          … **ページアイテム**用。SDK に実装例ゼロ
	  ・`IAdornmentFlattenerUsage`(`IsFlattenerRequired_`)… **アドーンメント**用 ← こちら
	末尾のアンダースコアが目印。間違えると誰も Query しないので黙って効かない。 */
class KESCMRingFlattenerUsage : public CPMUnknown<IAdornmentFlattenerUsage>
{
public:
	KESCMRingFlattenerUsage(IPMUnknown* boss) : CPMUnknown<IAdornmentFlattenerUsage>(boss) {}
	~KESCMRingFlattenerUsage() {}

	virtual bool32 IsFlattenerRequired_(IPMUnknown* iThing, const PMMatrix* masterSpread2LayoutSpreadMatrix,
										int32 nFlags);
};

CREATE_PMINTERFACE(KESCMRingFlattenerUsage, kKESCMRingFlattenerUsageImpl)

bool32 KESCMRingFlattenerUsage::IsFlattenerRequired_(IPMUnknown* /*iThing*/,
													 const PMMatrix* /*masterSpread2LayoutSpreadMatrix*/,
													 int32 /*nFlags*/)
{
	// ★★**常に kTrue を返してはいけない。** 透明を申告したページはフラットナでラスタ化され、
	//   CMYK/ブレンド空間の変換を通って**色が沈む**(実測 RGB(255,0,0) → (230,0,20))。
	//   マークを出さない場面まで巻き込むと、何も描いていないのに文書の見え方だけが変わる。
	//   ⇒ **実際に半透明のマークが乗るときだけ申告する。**
	//   ★手本の TranFxFlattenerUsage.cpp:79-83 が、まさにこの設計判断を書いている ----
	//     「付け外しで透明の有無が決まるなら kTrue 固定でよい／設定次第で消えるならそこを見て返せ」。
	//     こちらは後者(トグルで消える)。

	// ★実験用スイッチ(上の kDeclareFlattenerUsage の説明を参照)。
	if (!kDeclareFlattenerUsage)
		return kFalse;

	// 印刷/書き出しにマークを出さない設定なら、出力に透明は生じない。
	//   ・sPrintMarks   … Target 側の「Print comparison marks」
	//   ・sSrcMarksOn   … Source 側の枠(こちらは仕様上、印刷に常に出す)
	if (!KESCMDrawEventHandler::sPrintMarks && !KESCMDrawEventHandler::sSrcMarksOn)
		return kFalse;

	// 描くマークが1つも無ければ同じく透明は生じない。
	// ⚠sEntries は main が書き BG が読む集合なので、読むだけでもロックを取る
	//   (KESCMThreadSafety.h の規律。recursive_mutex なので入れ子でも詰まらない)。
	KESCMMarkStateLock lock(KESCMMarkStateMutex());
	return !KESCMDrawEventHandler::sEntries.empty();
}

//========================================================================================
// 3) セッションのグローバルリストへの出し入れ
//========================================================================================

void KESCMRingAdornmentRegister()
{
	// ★実験用スイッチ(上の kUseRingAdornment の説明を参照)。倒すと従来の Draw Event 経路のまま。
	if (!kUseRingAdornment)
		return;

	// ⚠★★「もう登録した」を static で憶えて早期 return してはいけない ---- **この関数は
	//   実行コンテキストごとに1回ずつ呼ばれる必要がある**(メインスレッド＋バックグラウンドスレッド)。
	//   static で弾くと、最初の1回(メインスレッド)しか登録されず BG が素通りする。
	//   二重登録は下の HasAdornment が防ぐので、ガードはそちらだけでよい。

	// ⚠**専用ヘッダー `IGlobalPageItemAdornmentList.h` は存在しない。** インターフェイスは普通の
	//   IPageItemAdornmentList で、**セッションから別の IID で取る**のが全て
	//   (実機ダンプ＝kSessionBoss / IID_IGLOBALPAGEITEMADORNMENTLIST / kGlobalPageItemAdornmentListImpl)。
	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return;
	InterfacePtr<IPageItemAdornmentList> globalList(session, IID_IGLOBALPAGEITEMADORNMENTLIST);
	if (globalList == nil)
		return;

	// 第2引数 kFalse ＝「文書を dirty にしない」。★グローバルリストはセッションに載るので、
	//   そもそも文書のデータには触れない(＝.indd への永続化も起きない)。
	if (!globalList->HasAdornment(kKESCMRingAdornmentBoss))
		globalList->AddAdornment(kKESCMRingAdornmentBoss, kFalse);
}

void KESCMRingAdornmentUnregister()
{
	if (!kUseRingAdornment)
		return;

	ISession* session = GetExecutionContextSession();	// 終了処理中は nil になり得る
	if (session == nil)
		return;
	InterfacePtr<IPageItemAdornmentList> globalList(session, IID_IGLOBALPAGEITEMADORNMENTLIST);
	if (globalList == nil)
		return;

	if (globalList->HasAdornment(kKESCMRingAdornmentBoss))
		globalList->RemoveAdornment(kKESCMRingAdornmentBoss, kFalse);
}

bool16 KESCMRingAdornmentIsActive()
{
	// ★★★2026-08-19 実測で直した ---- **static を返してはいけない。**
	//
	//   ■ 症状: UI の File > 書き出しで PDF を作ると、**どのページにも枠が出ない**。
	//     画面には正しく出る。同期書き出し(スクリプトの exportFile)にも出る。
	//     出ないのは**非同期書き出し(＝UI の書き出し。バックグラウンドスレッド)だけ**
	//     （実測 2026-08-19・PDF 1.4：同期=77,240 画素 / 非同期=**0**）。
	//
	//   ■ 原因は2つが噛み合ったこと。ガイド vol1-07 の一文が両方を説明する ----
	//     "Threads do not share object-model instances. **They do share globals and statics**"
	//       (1) **前半**: グローバルアドーンメントの登録先は「**セッションのインターフェイス・
	//           インスタンス**」なので、メインスレッドで AddAdornment した内容は
	//           **BG スレッドの実行コンテキストからは見えない** ⇒ BG では誰も DrawAdornment を呼ばない。
	//       (2) **後半**: ところが当時ここが返していた static のフラグは **BG でも kTrue に見える**
	//           ⇒ Draw Event 側が「アドーンメントが描くから」と譲って降りる。
	//     ⇒ **両方が描かない。** ★「登録に失敗しても従来経路が描くから機能は落ちない」という
	//       このファイルの設計は、**BG では成り立っていなかった**。
	//
	//   ■ 直し方: **その実行コンテキストのセッションに、実地で聞く。**
	//     BG では kFalse が返る ⇒ Draw Event 経路が描く ⇒ 書き出しは従来どおり。
	//     ⇒ **フォールバックが宣言どおり働くようになる。**
	//
	//   ★教訓として一般化できる形: **「どちらか一方が担当する」という取り決めを static に持たせると、
	//     スレッドをまたいだ瞬間に「どちらも担当しない」に化ける。** 担当の判定は、
	//     その担当が成立している場所（ここではセッション）に聞くのが正しい。
	//
	// ⚠ ここは描画のたびに通る。実験スイッチが切ってあるときは Query を1つも出さない
	//   (定数なのでコンパイル時に消える)。生きているときの Query 2つは、描画1回あたりでは無視できる。
	if (!kUseRingAdornment)
		return kFalse;

	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return kFalse;
	InterfacePtr<IPageItemAdornmentList> globalList(session, IID_IGLOBALPAGEITEMADORNMENTLIST);
	if (globalList == nil)
		return kFalse;
	return globalList->HasAdornment(kKESCMRingAdornmentBoss);
}

//========================================================================================
// 3.5) ★★★透明マネージャに「聞き直せ」と言う ---- PDF 1.3 の全面ベタの残り半分
//========================================================================================

void KESCMRingAdornmentRefreshItemXPState(IDataBase* db)
{
	// 申告そのものを切ってあるなら、一覧に載せる意味も無い。
	// 申告そのものを切ってあるなら、一覧に載せる意味も無い。
	// kTestInkBoundsInsteadOfNotify は「ink bounds が通知の代わりになるか」を測るための実験スイッチ。
	if (!kUseRingAdornment || !kDeclareFlattenerUsage || kTestInkBoundsInsteadOfNotify)
		return;
	if (db == nil)
		return;

	Utils<IXPUtils> xpUtils;
	if (!xpUtils)
		return;
	InterfacePtr<IXPManager> xpManager(xpUtils->QueryXPManager(db));
	if (xpManager == nil)
		return;

	InterfacePtr<ISpreadList> spreadList(db, db->GetRootUID(), UseDefaultIID());
	if (spreadList == nil)
		return;

	// ★**スプレッドごとに1つで足りる。** 一覧は「そのスプレッドに透明を持つアイテムが在るか」を
	//   答えるための材料で、位置は見ていない(`IXPManager.h:114-116`＝"Info is maintained solely on
	//   the presence of transparent items on spread, **not based on location**")。
	// ⚠**アイテムが1つも無いスプレッドは載せようがない**＝空ページだけの文書では、この手は効かない
	//   (アドーンメントは空ページにも枠を描けるが、透明の有無を答える口がアイテムしか無いため)。
	UIDList items(db);
	const int32 spreadCount = spreadList->GetSpreadCount();
	for (int32 i = 0; i < spreadCount; ++i)
	{
		InterfacePtr<ISpread> spread(db, spreadList->GetNthSpreadUID(i), UseDefaultIID());
		if (spread == nil)
			continue;
		const int32 pageCount = spread->GetNumPages();
		for (int32 p = 0; p < pageCount; ++p)
		{
			UIDList onPage(db);
			spread->GetItemsOnPage(p, &onPage, kFalse /*bIncludePage=ページ自身は要らない*/);
			if (onPage.Length() > 0)
			{
				items.Append(onPage[0]);
				break;			// このスプレッドはもう代表が取れた
			}
		}
	}
	if (items.Length() == 0)
		return;

	// ★`kXPC_MayHaveAddedSomeXP` ＝「設定が変わった。増えたか減ったかは分からない」
	//   (`IXPManager.h:101`)。マークは ON にも OFF にもなるので、これが正しい種別。
	//   答えを決めるのは**こちらではなく上の IsFlattenerRequired_**＝XPManager がアイテムに
	//   聞き直し、アイテムは自分のアドーンメントに聞く。∴ この呼び出しは冪等で、
	//   「マークを出さない設定」のときに透明を偽って申告することにはならない。
	// ⚠**Command 版(`ProcessItemXPChangedCmd` / `kXPItemXPPrePostCmdBoss`)は使わない。**
	//   公式サンプルがそちらなのは**アドーンメントの付け外し自体が文書データの変更で Undo に
	//   乗せる必要がある**から。KESCM の登録はセッション側で文書を1バイトも変えないので、
	//   Undo スタックに項目を積む理由が無い(積めば Ctrl+Z の意味が壊れる)。
	//
	// ⚠★★★**この呼び出しは文書を dirty にする** ---- 2026-08-20 に A/B で実測した
	//   (この関数を外したビルドでは Start→印刷マーク ON→Stop を通しても `modified=false` のまま。
	//    入れたビルドでは Stop の後に `modified=true` になった)。**一覧は文書側のデータ**なので、
	//    公式が Command 経由なのもそのためで、あちらは「アドーンメントを付けた」という本物の
	//    文書変更に伴う dirty だから正しい。**KESCM は文書を変えていないので戻す。**
	//   ★KESCM の作法＝`IDataBase::SaveRestoreModifiedState`(入る前が clean なら出るときに戻す)。
	//     同じ守りが KESCMCore.cpp:527 / KESCMPeek.cpp:194 / KESCMOversetScan.cpp:333 ほか6か所にある。
	{
		IDataBase::SaveRestoreModifiedState dirtyGuard(db);
		xpManager->ItemXPChanged(items, IXPManager::kXPC_MayHaveAddedSomeXP);
	}
}

//========================================================================================
// 4) ★★★スレッドごとの登録 ---- startup/shutdown サービス
//========================================================================================

/** 各実行コンテキストの起動時に登録し、終了時に外す。

	★★★**なぜ専用のサービスが要るのか**（2026-08-19・実測を経て）:
	  グローバル**テキスト**アドーンメントは `.fr` の AddIn に
	  `IID_IK2SERVICEPROVIDER, kGlobalTextAdornmentServiceImpl` と書くだけで済む
	  （spellpanel の動的スペルチェックがその形＝`SpellPanelClass.fr:773-774`）。
	  実行時の登録コードは1行も無く、**サービスは実行コンテキストごとに解決されるので
	  バックグラウンドスレッドでも自動で有効になる。**

	  ⚠**ページアイテム版には、その口が無い**（2026-08-19 に全数確認）＝
	  `kServiceIDSpace` で adornment を名乗るサービスは `kGlobalTextAdornmentService` と
	  InCopy のゲラ用2つだけで、**全部テキスト系**。ページアイテム側にあるのは
	  `kGlobalPageItemAdornmentListImpl`（セッションに載る**リストの実装**）だけで、
	  サービスプロバイダではない。
	  ⇒ **サービスが自動でやっていることを、手で再現するのがこのクラス。**

	⚠★★**既存の KESCMPeekStartup と一緒にしてはいけない。** あちらは `.fr` で
	  `kCMainThreadStartupShutdownProviderImpl` を指定して**メインスレッド限定**にしてある ----
	  Shutdown() が比較状態を丸ごと捨てるので、BG スレッドが終わるたびに呼ばれると
	  **PDF を書き出すたびにマークが消える**（2026-08-15 に実際に踏んで直した箇所）。
	  こちらは**登録と解除しかしない**ので、全スレッドで呼ばれてよい ---- というより
	  **呼ばれなければ意味が無い**。∴ boss を分ける。 */
class KESCMRingAdornmentStartup : public CPMUnknown<IStartupShutdownService>
{
public:
	KESCMRingAdornmentStartup(IPMUnknown* boss) : CPMUnknown<IStartupShutdownService>(boss) {}
	~KESCMRingAdornmentStartup() {}

	virtual void Startup()  { KESCMRingAdornmentRegister(); }
	virtual void Shutdown() { KESCMRingAdornmentUnregister(); }
};

CREATE_PMINTERFACE(KESCMRingAdornmentStartup, kKESCMRingAdornmentStartupImpl)
