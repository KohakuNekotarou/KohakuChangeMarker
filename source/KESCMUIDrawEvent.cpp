//========================================================================================
//
//  KESCMUIDrawEvent.cpp
//
//  UI 側の描画サービス(2026-08-13・model/UI 分割 第1段 Task 6 で新設)。
//  **画面にしか出さない描画**——model 側からは原理的にできないもの——だけをここで担う。
//
//  今のところ持っているのは1つ:
//    押下中 HUD ＝ ツールの左ボタンを押している間、押した窓の左上に「その窓が比較の何なのか」
//    (Target / Source / …)を出す表示。★**押下中かどうかはツール(UI)の状態**で、model からは見えない。
//
//  ★kDrawEventService は**複数プロバイダ登録が前提**(本体だけで20以上ある)ので、ここへもう1つ足しても
//    何も壊れない。★このサービスを画面専用にしているのは意図的で、`kUIPlugIn` の boss は
//    バックグラウンドスレッドから作れず、UI の PDF 書き出しはバックグラウンドで走る
//    ＝ここの描画は書き出しファイルに届かない([[model-ui-plugin-separation]])。HUD にとってはそれが正しい。
//    ⚠**書き出しに出なければならない比較マークは model 側のハンドラ(KESCMDrawEventHandler.cpp)に残る。**
//
//  ★GetThreadingPolicy は**手書きしない**。CServiceProvider がプラグインの型から既定を返すので、
//    UI プラグインになれば自動で kMainThreadOnly が入る。手書きすると第2段で消し忘れる元になる
//    (既存の KESCMDrawEventSrvc がまさにそれで、第2段で消す対象)。
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

#include "KESCMID.h"
#include "KESCMTrackerHud.h"		// 押下中 HUD(2026-08-13 に model 側ハンドラからこちらへ移した)

//========================================================================================
// pasteboard 座標 → このスプレッドの spread 座標 への変換オフセット(= pasteboard - spread)。
//   pasteboard 座標はドキュメント全体で1つ。スプレッドは pasteboard 上で(主に縦に)積まれ、各々が
//   オフセットを持つ(spread[0] だけ偶然 0)。同一の inner 原点(0,0)を InnerToSpreadMatrix と
//   InnerToPasteboardMatrix の両方で写し、その差を取ればこのスプレッドのオフセットになる。
//   pasteboard 座標の点からこれを引けば、そのスプレッドの spread 座標における点が得られる。
//   ★2026-07-04 のトースト撤去で消えたが、2026-08-07 に押下中 HUD のために戻した(元コード
//     = git 068d8fb^ の KESCMDrawEventHandler.cpp:534-548)。
//   ★2026-08-13: **唯一の呼び手である HUD と一緒に**ここへ移した(model 側では未参照になるため)。
//     中身は1行も変えていない。ページの幾何を読むだけでビューには触らないが、使うのは描画の座標合わせ
//     だけなので、置き場は「描く側」でよい。
//========================================================================================
static PMPoint KESCMSpreadOffsetFromPasteboard(IDataBase* db, ISpread* spread)
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
// KESCMUIDrawEventHandler
//========================================================================================
class KESCMUIDrawEventHandler : public CPMUnknown<IDrwEvtHandler>
{
public:
	KESCMUIDrawEventHandler(IPMUnknown* boss) : CPMUnknown<IDrwEvtHandler>(boss) {}
	~KESCMUIDrawEventHandler() {}

	virtual void	Register(IDrwEvtDispatcher* d);
	virtual void	UnRegister(IDrwEvtDispatcher* d);
	virtual bool16	HandleDrawEvent(ClassID eventID, void* eventData);
};

CREATE_PMINTERFACE(KESCMUIDrawEventHandler, kKESCMUIDrawEventHandlerImpl)

// ★HUD は2系統の描画イベントを**両方**使う。片方だけでは窓全域を覆えない(理由は下の HandleDrawEvent)。
//   この2つは元は model 側ハンドラが登録していたもので、kAfterLastSpreadDrawMessage のほうは
//   **HUD のためだけに登録されていた**(2026-08-07 に戻した経緯がコメントに残っていた)ので、
//   model 側からは登録ごと外した(2026-08-13)。
void KESCMUIDrawEventHandler::Register(IDrwEvtDispatcher* d)
{
	// スプレッド単位で配られる描画イベント。ポートは spread 座標。
	d->RegisterHandler(ClassID(kEndSpreadMessage), this, kDEHLowestPriority);
	// ウィンドウ単位(全スプレッド描画後に1回)。ポートは pasteboard 座標。
	d->RegisterHandler(ClassID(kAfterLastSpreadDrawMessage), this, kDEHLowestPriority);
}

void KESCMUIDrawEventHandler::UnRegister(IDrwEvtDispatcher* d)
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
bool16 KESCMUIDrawEventHandler::HandleDrawEvent(ClassID eventID, void* eventData)
{
	DrawEventData* ded = static_cast<DrawEventData*>(eventData);
	if (ded == nil || ded->gd == nil)
		return kFalse;

	// 画面だけ。印刷/PDF 書き出しの文脈では何もしない。
	// ★このサービスは UI プラグイン側なので書き出しには元から配られないが、印刷プレビュー等の
	//   画面上の印刷文脈でも出さないという意味でここは要る(model 側の判定と同じ形)。
	if ((ded->flags & IShape::kPrinting) != 0)
		return kFalse;

	// 「押下中」かつ「押した窓のビュー」でなければ何もしない。★view が nil の描画
	// (ページパネルのサムネイル生成など)は KESCMTrackerHudWantsDraw が弾く。
	if (!KESCMTrackerHudWantsDraw(ded->gd->GetView()))
		return kFalse;

	IGraphicsPort* gPort = ded->gd->GetGraphicsPort();
	if (gPort == nil)
		return kFalse;

	// ウィンドウ単位イベント: ポートは pasteboard 座標なのでオフセット無し。
	if (eventID == ClassID(kAfterLastSpreadDrawMessage))
	{
		KESCMTrackerHudDraw(gPort, ded->gd->GetView(), PMPoint(0.0, 0.0));
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

	KESCMTrackerHudDraw(gPort, ded->gd->GetView(), KESCMSpreadOffsetFromPasteboard(db, spread));
	return kFalse;
}

//========================================================================================
// KESCMUIDrawEventSrvc
//   kDrawEventService サービスとして自身を登録する。アプリ起動時にこのサービスが見つかり、
//   同じ boss 上の IDrwEvtHandler が描画イベントディスパッチャに登録される。
//========================================================================================
class KESCMUIDrawEventSrvc : public CServiceProvider
{
public:
	KESCMUIDrawEventSrvc(IPMUnknown* boss) : CServiceProvider(boss) {}
	~KESCMUIDrawEventSrvc() {}

	virtual ServiceID GetServiceID() { return kDrawEventService; }
	virtual bool16 IsDefaultServiceProvider() { return kFalse; }
	virtual InstancePerX GetInstantiationPolicy() { return IK2ServiceProvider::kInstancePerSession; }
	// 内部名(翻訳対象ではない)なので SetCString。model 側の KESCMDrawEventSrvc と同じ流儀。
	virtual void GetName(PMString* pName) { pName->SetCString("KESCMUIDrawEventSrvc"); }
	// ★GetThreadingPolicy は**書かない**(ファイル冒頭の理由)。
};

CREATE_PMINTERFACE(KESCMUIDrawEventSrvc, kKESCMUIDrawEventSrvcImpl)

// KESCMUIDrawEvent.cpp 終わり。
