//========================================================================================
//
//  KCMUIDrawEvent.cpp
//
//  UI 側の描画サービス(2026-08-13・model/UI 分割 第1段 Task 6 で新設)。
//  **画面にしか出さない描画**——model 側からは原理的にできないもの——だけをここで担う。
//
//  持っているのは2つ:
//    ①押下中 HUD ＝ ツールの左ボタンを押している間、押した窓の左上に「その窓が比較の何なのか」
//      (Target / Source / …)を出す表示。★**押下中かどうかはツール(UI)の状態**で、model からは見えない。
//    ②手動 Hide/Show Spread の検出（2026-08-13 Task 7 で model 側ハンドラから移した）。
//      スクロールバー地図は**文書窓へ strip を注入する widget** ＝ UI なので、その更新のきっかけを
//      拾うのもこちら側の仕事。
//
//  ★kDrawEventService は**複数プロバイダ登録が前提**(本体だけで20以上ある)ので、ここへもう1つ足しても
//    何も壊れない。★このサービスを画面専用にしているのは意図的で、`kUIPlugIn` の boss は
//    バックグラウンドスレッドから作れず、UI の PDF 書き出しはバックグラウンドで走る
//    ＝ここの描画は書き出しファイルに届かない([[model-ui-plugin-separation]])。HUD にとってはそれが正しい。
//    ⚠**書き出しに出なければならない比較マークは model 側のハンドラ(KCMDrawEventHandler.cpp)に残る。**
//
//  ★GetThreadingPolicy は**手書きしない**。CServiceProvider がプラグインの型から既定を返すので、
//    UI プラグイン(KCMUI)であるこちらには自動で kMainThreadOnly が入る(ガイド vol1-07 L245-253)。
//    ⚠2026-08-18(不具合再検査 B-U2)訂正: 旧記述は「手書きすると第2段で消し忘れる元になる(既存の
//    KCMDrawEventSrvc がまさにそれで、**第2段で消す対象**)」だったが、**その手書き override は
//    2026-08-14 に撤去済み**＝model 側 KCMDrawEventHandler.cpp の KCMDrawEventSrvc が
//    「第2段を待たず先に消してある」と自分で書いている。**このファイルが書かれた翌日**に片付いた
//    予告を、第2段の完了(2026-08-15)後も持ち続けていた。
//    ⇒ ★**段階実装の「まだ」は、その段階が終わった日に消す**(同じ B-U2 の回に
//      KCMModelChangeObserver.cpp で拾った教訓の、同じブロック内での再発)。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "CServiceProvider.h"
#include "CPMUnknown.h"
#include "IDrwEvtHandler.h"
#include "IDrwEvtDispatcher.h"
#include "GraphicsData.h"			// GraphicsData(GetGraphicsPort / GetView)＋ DrawEventData
#include "IGraphicsPort.h"
#include "IShape.h"					// IShape::kPrinting(印刷/PDF 書き出しの文脈フラグ)
#include "GraphicsID.h"				// kDrawEventService
#include "DocumentContextID.h"		// kEndSpreadMessage / kAfterLastSpreadDrawMessage
#include "ISpread.h"
#include "IGeometry.h"
#include "IDataBase.h"
#include "PersistUtils.h"			// ::GetDataBase(描いているスプレッドの db)
#include "TransformUtils.h"			// ::InnerToSpreadMatrix / ::InnerToPasteboardMatrix
#include "PMMatrix.h"
#include "PMPoint.h"

#include "KCMUIID.h"
#include "KCMTrackerHud.h"		// 押下中 HUD(2026-08-13 に model 側ハンドラからこちらへ移した)
#include "KCMScrollMap.h"			// KCMScrollMapNoticeDrawEvent(手動 Hide/Show Spread の検出。同上)

//========================================================================================
// pasteboard 座標 → このスプレッドの spread 座標 への変換オフセット(= pasteboard - spread)。
//   pasteboard 座標はドキュメント全体で1つ。スプレッドは pasteboard 上で(主に縦に)積まれ、各々が
//   オフセットを持つ(spread[0] だけ偶然 0)。同一の inner 原点(0,0)を InnerToSpreadMatrix と
//   InnerToPasteboardMatrix の両方で写し、その差を取ればこのスプレッドのオフセットになる。
//   pasteboard 座標の点からこれを引けば、そのスプレッドの spread 座標における点が得られる。
//   ★2026-07-04 のトースト撤去で消えたが、2026-08-07 に押下中 HUD のために戻した(元コード
//     = git 068d8fb^ の KCMDrawEventHandler.cpp:534-548)。
//   ★2026-08-13: **唯一の呼び手である HUD と一緒に**ここへ移した(model 側では未参照になるため)。
//     中身は1行も変えていない。ページの幾何を読むだけでビューには触らないが、使うのは描画の座標合わせ
//     だけなので、置き場は「描く側」でよい。
//========================================================================================
static PMPoint KCMSpreadOffsetFromPasteboard(IDataBase* db, ISpread* spread)
{
	PMPoint off(0.0, 0.0);
	if (db == nil || spread == nil || spread->GetNumPages() < 1)
		return off;
	InterfacePtr<IGeometry> pg(db, spread->GetNthPageUID(0), UseDefaultIID());
	if (pg == nil)
		return off;
	PMMatrix mS = ::InnerToSpreadMatrix(pg);
	PMMatrix mP = ::InnerToPasteboardMatrix(pg);
	PMPoint ps(0.0, 0.0), pp(0.0, 0.0);
	mS.Transform(&ps);
	mP.Transform(&pp);
	return PMPoint(pp.X() - ps.X(), pp.Y() - ps.Y());
}

//========================================================================================
// KCMUIDrawEventHandler
//========================================================================================
class KCMUIDrawEventHandler : public CPMUnknown<IDrwEvtHandler>
{
public:
	KCMUIDrawEventHandler(IPMUnknown* boss) : CPMUnknown<IDrwEvtHandler>(boss) {}
	~KCMUIDrawEventHandler() {}

	virtual void	Register(IDrwEvtDispatcher* d);
	virtual void	UnRegister(IDrwEvtDispatcher* d);
	virtual bool16	HandleDrawEvent(ClassID eventID, void* eventData);
};

CREATE_PMINTERFACE(KCMUIDrawEventHandler, kKCMUIDrawEventHandlerImpl)

// ★HUD は2系統の描画イベントを**両方**使う。片方だけでは窓全域を覆えない(理由は下の HandleDrawEvent)。
//   この2つは元は model 側ハンドラが登録していたもので、kAfterLastSpreadDrawMessage のほうは
//   **HUD のためだけに登録されていた**(2026-08-07 に戻した経緯がコメントに残っていた)ので、
//   model 側からは登録ごと外した(2026-08-13)。
void KCMUIDrawEventHandler::Register(IDrwEvtDispatcher* d)
{
	// スプレッド単位で配られる描画イベント。ポートは spread 座標。
	d->RegisterHandler(ClassID(kEndSpreadMessage), this, kDEHLowestPriority);
	// ウィンドウ単位(全スプレッド描画後に1回)。ポートは pasteboard 座標。
	d->RegisterHandler(ClassID(kAfterLastSpreadDrawMessage), this, kDEHLowestPriority);
}

void KCMUIDrawEventHandler::UnRegister(IDrwEvtDispatcher* d)
{
	d->UnRegisterHandler(ClassID(kEndSpreadMessage), this);
	d->UnRegisterHandler(ClassID(kAfterLastSpreadDrawMessage), this);
}

/* HandleDrawEvent — 押下中 HUD だけを描く。

   ★★2系統を併用する理由(model 側ハンドラの Register に残っていた実測の知見をそのまま引き継ぐ):
     kEndSpreadMessage           … 帯(スプレッド/ペーストボード)に clip されるが**前面**
     kAfterLastSpreadDrawMessage … clip されないが**背面**(= 何も被さらないカンバス部分にだけ見える)
   ∴ 併用すると各画素はどちらか一方だけが担当し、**二重描きなしでビュー全域**を覆える。

   ★戻り値は常に kFalse ＝ 他のハンドラへ流す(既存の作法。model 側のマーク描画がこの後に走る)。
*/
bool16 KCMUIDrawEventHandler::HandleDrawEvent(ClassID eventID, void* eventData)
{
	DrawEventData* ded = static_cast<DrawEventData*>(eventData);
	if (ded == nil || ded->gd == nil)
		return kFalse;

	// 画面だけ。印刷/PDF 書き出しの文脈では何もしない。
	// ★このサービスは UI プラグイン側なので書き出しには元から配られないが、印刷プレビュー等の
	//   画面上の印刷文脈でも出さないという意味でここは要る(model 側の判定と同じ形)。
	if ((ded->flags & IShape::kPrinting) != 0)
		return kFalse;

	// スクロールバー地図: ページパネルからの手動 Hide/Show Spread を検出する軽量チェック(250ms
	// スロットル付きの指紋比較)。手動の隠し/再表示は KCM のフックを通らないが必ず再描画は起こす
	// ので、スプレッド描画イベントに便乗して拾う(Undo/Redo による変化も同経路)。KCMScrollMap.cpp。
	// ★★**HUD の判定より前**に置くこと。これは押下中かどうかと無関係に、非印刷の描画のたびに
	//   走らなければならない(下の HUD 判定は「押していなければ即 return」なので、後ろに置くと
	//   **押している間しか地図が更新されなくなる**)。2026-08-13 に model 側ハンドラから移した際、
	//   元も HandleDrawEvent の先頭付近(マークの判定より前)に在った。
	// ⚠★**移動で1つだけ挙動が変わった**: 元は同じ関数の冒頭にある再入ガード
	//   (自前ラスタ化中は描かない。model 側の `tl_Rasterizing`。★当時は素の static だったが 2026-08-15 に
	//   `IDThreading::ThreadLocal<bool16>` へ移した＝第2段 Task 12B)より**後ろ**に在ったので、
	//   比較のラスタ化中はこの検出が走らなかった。
	//   こちらのハンドラは model 側のその再入フラグを見ないので、**ラスタ化中にも走る**。
	//   ★害が無いと判断した根拠: この検出は「隠し状態の指紋を比べて、変わっていたら strip を
	//     invalidate する」だけで、ラスタ化中に隠し状態は変わらない ⇒ 実質 no-op(しかも 250ms
	//     スロットル付き)。⚠それでも**挙動差であることは事実**なので、実機の確認項目に入れてある。
	KCMScrollMapNoticeDrawEvent();

	// 「押下中」かつ「押した窓のビュー」でなければ何もしない。★view が nil の描画
	// (ページパネルのサムネイル生成など)は KCMTrackerHudWantsDraw が弾く。
	if (!KCMTrackerHudWantsDraw(ded->gd->GetView()))
		return kFalse;

	IGraphicsPort* gPort = ded->gd->GetGraphicsPort();
	if (gPort == nil)
		return kFalse;

	// ウィンドウ単位イベント: ポートは pasteboard 座標なのでオフセット無し。
	if (eventID == ClassID(kAfterLastSpreadDrawMessage))
	{
		KCMTrackerHudDraw(gPort, ded->gd->GetView(), PMPoint(0.0, 0.0));
		return kFalse;
	}

	// スプレッド単位イベント: changedBy = 今描いているスプレッド。ポートは spread 座標なので、
	// pasteboard→spread のオフセットを渡す。
	InterfacePtr<ISpread> spread(ded->changedBy, UseDefaultIID());
	if (spread == nil)
		return kFalse;
	IDataBase* db = ::GetDataBase(ded->changedBy);
	if (db == nil)
		return kFalse;

	KCMTrackerHudDraw(gPort, ded->gd->GetView(), KCMSpreadOffsetFromPasteboard(db, spread));
	return kFalse;
}

//========================================================================================
// KCMUIDrawEventSrvc
//   kDrawEventService サービスとして自身を登録する。アプリ起動時にこのサービスが見つかり、
//   同じ boss 上の IDrwEvtHandler が描画イベントディスパッチャに登録される。
//========================================================================================
class KCMUIDrawEventSrvc : public CServiceProvider
{
public:
	KCMUIDrawEventSrvc(IPMUnknown* boss) : CServiceProvider(boss) {}
	~KCMUIDrawEventSrvc() {}

	virtual ServiceID GetServiceID() { return kDrawEventService; }
	virtual bool16 IsDefaultServiceProvider() { return kFalse; }
	virtual InstancePerX GetInstantiationPolicy() { return IK2ServiceProvider::kInstancePerSession; }
	// 内部名(翻訳対象ではない)なので SetCString。model 側の KCMDrawEventSrvc と同じ流儀。
	virtual void GetName(PMString* pName) { pName->SetCString("KCMUIDrawEventSrvc"); }
	// ★GetThreadingPolicy は**書かない**(ファイル冒頭の理由)。
};

CREATE_PMINTERFACE(KCMUIDrawEventSrvc, kKCMUIDrawEventSrvcImpl)

// KCMUIDrawEvent.cpp 終わり。
