//========================================================================================
//
//  KCMTrackerHud.cpp
//
//  左ボタンを押している間だけ、押したビューの**左上**に「その窓が比較の何なのか」を出す
//    (Target / Source / Not in comparison / Not comparing)。仕様・経緯・「なぜ Draw Event で
//    隅に描けるのか」は KCMTrackerHud.h の冒頭。
//  ⚠ここは 2026-08-19(不具合再検査 B-U6)まで「右上」と書いてあった。実装は下の
//    kKCMTrackerHudLeftPx が boundsPb.Left() 基準＝左上で、KCMTrackerHud.h も KCMTracker.cpp も
//    「左上」と書いており、**このファイルの1行だけが逆**だった。旧 sprite 版も左上
//    (git 19015e3^:KCMTracker.cpp:908「ビュー左上からの位置(画面 px)」)。
//
//  ここが持つのは「押下中か」「どのビューか」の2つだけ(比較状態は KCMCore/KCMPeek 側に
//  聞く = 状態を二重に持たない)。描画の呼び出しは
//  **KCMUIDrawEventHandler::HandleDrawEvent(KCMUIDrawEvent.cpp)** が2系統(帯の前面 / カンバス背景)
//  から行う。
//  ⚠2026-08-18(不具合再検査 B-U2)訂正＝「KCMDrawEventHandler::HandleDrawEvent が」と書いてあったが、
//    あれは**model 側**のマーク描画ハンドラで、HUD は 2026-08-13(Task 6)にこちらへ移っている
//    (model 側は KCMDrawEventHandler.cpp が「このファイルは KCMTrackerHud.h を include しない」と
//     自分で書いており、両側の記述が食い違っていた)。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IControlView.h"		// GetContentToWindowMatrix(ズーム) / GetBBox / WindowToContentTransform
#include "IGraphicsPort.h"		// rectfill / selectfont / show / 透明グループ
#include "IFontMgr.h"			// QueryFont / QueryFontInstance
#include "IPMFont.h"
#include "IFontInstance.h"		// MeasureWText / GetAscent / GetDescent(下地の寸法)
#include "ISession.h"			// GetExecutionContextSession
#include "AutoGSave.h"
#include "WideString.h"			// show に渡す UTF16
#include "PMMatrix.h"
#include "PMRect.h"

#include "Utils.h"				// Utils<IKCMCompareFacade>()
#include "IKCMCompareFacade.h"	// arm 状態(2026-08-13・分割 第1段 Task 11 で Facade 経由へ)
#include "KCMViewLookup.h"	// KCMFindDocDbForView(2026-08-13 に KCMCore.h から移動)
#include "KCMTrackerHud.h"

//----------------------------------------------------------------------------------------
// 見た目(すべて画面ピクセル指定。ズームを変えても見た目は変わらない = 実ズームで割って content 単位へ)
//----------------------------------------------------------------------------------------
// ★値はすべて 2026-08-06 に全廃した旧 sprite 版 HUD と同じ(2026-08-07 ユーザー指示「位置などは、
//   以前の HUD と同じに左上の方に」)。原本 = git 19015e3^:KCMTracker.cpp の kKCMHud*Px(:128-132)
//   と ShowHud() の kHudLeftPx/kHudBaselinePx(:909-910)。下地=白ベタ・文字=黒 も同じ。
static const PMReal kKCMTrackerHudTextPx     = 20.0;		// 文字の大きさ
static const PMReal kKCMTrackerHudPadXPx     = 8.0;		// 下地の左右余白
static const PMReal kKCMTrackerHudPadTopPx   = 4.0;		// 下地の上余白
static const PMReal kKCMTrackerHudPadBotPx   = 4.0;		// 下地の下余白
static const PMReal kKCMTrackerHudOpacity    = 0.6;		// 下地＋文字をまとめて薄くする度合い(1.0=不透明)
static const PMReal kKCMTrackerHudLeftPx     = 20.0;		// ビュー左端からの余白(★左上に置くので左基準)
static const PMReal kKCMTrackerHudBaselinePx = 40.0;		// ビュー上端から文字ベースラインまで

//----------------------------------------------------------------------------------------
// 押下中だけ持つ状態
//----------------------------------------------------------------------------------------
static bool16        sActive = kFalse;	// 左ボタンを押している間だけ kTrue
static IControlView* sView   = nil;		// 押されたレイアウトビュー(借り物。比較にしか使わない)

// フォント(所有する)。出す4通りはどれも ASCII の英字なので既定フォントで足りる —— 旧 HUD は文書名を
// 出していたので「その字を持つフォントを選ぶ」3段の選定が要ったが(gPort の show は単一フォントの
// グリフしか使わず、OS のようなフォールバックが無いため和文が化けた)、固定の欧文ならその問題は起きない。
// ★「相手の文書名は出さない」というユーザー判断(2026-07-27)が、ここでもそのまま効いている。
static IPMFont*       sFont         = nil;
static IFontInstance* sFontInst     = nil;
static PMReal         sFontInstSize = 0.0;

static void KCMTrackerHudReleaseFont()
{
	if (sFontInst != nil)
	{
		sFontInst->Release();
		sFontInst = nil;
	}
	sFontInstSize = 0.0;
	if (sFont != nil)
	{
		sFont->Release();
		sFont = nil;
	}
}

/** 既定フォント(キャッシュ付き)。所有はここ側 = 呼び出し側は Release しない。 */
static IPMFont* KCMTrackerHudQueryFont()
{
	if (sFont != nil)
		return sFont;

	InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), UseDefaultIID());
	if (fontMgr == nil)
		return nil;
	sFont = fontMgr->QueryFont(fontMgr->GetDefaultFontName());
	return sFont;
}

/** そのサイズで測る/描くためのインスタンス(キャッシュ付き)。所有はここ側。
	作り方は KCMDrawEventHandler.cpp の旧ページ番号バッジと同じ(サイズを対角に入れた行列を渡す)。
	★サイズはズームで変わるので、変わったら作り直す。 */
static IFontInstance* KCMTrackerHudQueryFontInstance(IPMFont* font, const PMReal& size)
{
	if (font == nil || size <= 0)
		return nil;
	if (sFontInst != nil && sFontInstSize == size)
		return sFontInst;

	if (sFontInst != nil)
	{
		sFontInst->Release();
		sFontInst = nil;
	}
	InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), UseDefaultIID());
	if (fontMgr == nil)
		return nil;
	const PMMatrix fontMatrix(size, 0.0, 0.0, size, 0.0, 0.0);
	sFontInst     = fontMgr->QueryFontInstance(font, fontMatrix);
	sFontInstSize = size;
	return sFontInst;
}

/** HUD に出す1行を組む。押した窓(view)と比較状態で4通り:
	  比較中 + Target の窓  → "Target"              … この窓が比較の Target(新版)
	  比較中 + Source の窓  → "Source"              … この窓が比較の Source(旧版)
	  比較中 + それ以外     → "Not in comparison"   … この文書は比較の対象ではない
	  Stop 中               → "Not comparing"       … そもそも比較していない
	★出すのは「押した窓が何か」だけ。相手の文書名は出さない(2026-07-27 ユーザー指示。
	  長い文書名で HUD が伸びるより、いま触っている窓の役割が一目で分かる方を採る)。
	★「出ない」を状態表示に使わない(壊れているのか仕様なのか分からなくなるため)。4通りのどれかが必ず出る。
	★文字は英語固定。翻訳キー扱いで化けないよう SetTranslatable(kFalse) を必ず通す
	  (UI 文字列のリテラルが内蔵訳に化ける事故が KCM で実際に起きている)。
	★この文言と判定は 2026-08-06 に全廃した旧 sprite 版 HUD の KCMBuildHudText
	  (git 19015e3^:KCMTracker.cpp:189-208)をそのまま引き継いだもの。 */
static PMString KCMTrackerHudLabel(IControlView* view)
{
	PMString out;

	if (!Utils<IKCMCompareFacade>()->IsArmed())
		out = PMString("Not comparing");
	else
	{
		IDataBase* const db = KCMFindDocDbForView(view);	// 押した窓の文書(ポインタ比較のみ)
		if (db != nil && db == Utils<IKCMCompareFacade>()->GetArmedTargetDB())
			out = PMString("Target");
		else if (db != nil && db == Utils<IKCMCompareFacade>()->GetArmedSourceDB())
			out = PMString("Source");
		else
			out = PMString("Not in comparison");
	}

	out.SetTranslatable(kFalse);
	return out;
}

/** 押した窓の文書を描き直させる。
	★★これが要る理由(2026-08-07 実機報告「ソースの方で Source と出ない」「Stop 中でも Not と出ない」):
	  HUD は Draw Event で描く = **誰かが再描画を起こしてくれること**が前提になる。押下で再描画を
	  起こしているのは reveal / temp-hide だが、あれは **Target 窓の上でしか走らない**
	  (KCMPeekGesture.cpp の KCMTrackerRevealBegin が `KCMMouseIsOverTarget()` で早期 return
	   する。★2026-08-17 に参照先を訂正＝旧記述の `KCMPeek.cpp:1841-1844` は 2026-08-13 の分割前の
	   行番号で、ジェスチャはあのファイルから出ており、残った本体は今 907 行しかない)。
	  ∴ Source 窓・Stop 中・第3の文書では
	  描く機会そのものが来ず、HUD が1度も描かれなかった。表示も消去も**自分で**要求する
	  (他機能の再描画に相乗りしない)。
	★文書単位(KCMInvalidateDB = Utils<ILayoutUtils>()->InvalidateViews)にしたのは、KCM が枠の
	  表示/非表示で使っている道と同じにするため。HUD が描かれるのは押した窓だけ
	  (KCMTrackerHudWantsDraw の view 一致判定)なので、同じ文書の他のビューが描き直されても
	  見た目は変わらない。押下開始と解除の2回だけなので負荷も問題にならない。
	★Target 窓では reveal 側の再描画と重なるが、InDesign は無効領域をまとめるので描画は1回。 */
static void KCMTrackerHudInvalidate(IControlView* view)
{
	if (view != nil)
		Utils<IKCMCompareFacade>()->InvalidateDB(KCMFindDocDbForView(view));
}

void KCMTrackerHudBegin(IControlView* view)
{
	sActive = (view != nil);
	sView   = view;
	KCMTrackerHudInvalidate(view);	// 出すための再描画を自分で要求する(上のコメント)
}

void KCMTrackerHudEnd()
{
	IControlView* const view = sView;	// 消すための再描画に使うので、nil にする前に控える
	sActive = kFalse;
	sView   = nil;
	// ★順序が肝: 先に旗を落としてから要求する(逆にすると、この再描画で HUD がもう一度描かれる)。
	KCMTrackerHudInvalidate(view);
	// フォントは持ち越してよい(次の押下でそのまま使う)。返すのは Shutdown だけ。
}

bool16 KCMTrackerHudWantsDraw(IControlView* view)
{
	// ★押した窓にだけ出す。view が nil の描画(ページパネルのサムネイル生成など)も当然対象外。
	return (sActive && view != nil && view == sView) ? kTrue : kFalse;
}

void KCMTrackerHudDraw(IGraphicsPort* gPort, IControlView* view, const PMPoint& spreadOffset)
{
	if (gPort == nil || view == nil)
		return;

	// 実ズーム(画面 px 指定を content 単位へ逆算するのに使う)。負スケールもあり得るので絶対値で。
	PMReal sx = 1.0, sy = 1.0;
	{
		const PMMatrix toWindow = view->GetContentToWindowMatrix();
		sx = abs(toWindow.GetXScale());
		sy = abs(toWindow.GetYScale());
	}
	if (sx == 0 || sy == 0)
		return;

	// ビューの可視範囲を pasteboard 座標で得る(トーストと同じ手順 = 窓座標の bbox を content へ変換)。
	// ★ここが「ビューの隅」の正体。スクロールしてもズームしても、この矩形が常に「今見えている範囲」。
	PMRect boundsPb = view->GetBBox();
	view->WindowToContentTransform(&boundsPb);

	const PMString labelStr = KCMTrackerHudLabel(view);
	WideString     label(labelStr);
	IPMFont*       font = KCMTrackerHudQueryFont();
	if (font == nil)
		return;

	const PMReal fontSize = PMReal(kKCMTrackerHudTextPx) / sy;

	// 下地の大きさは実測で決める(文字数×固定幅の概算は外れる)。取れなければ概算へ落とす。
	PMReal textW   = 0.0;
	PMReal ascent  = fontSize * PMReal(0.8);
	PMReal descent = fontSize * PMReal(0.2);
	IFontInstance* inst = KCMTrackerHudQueryFontInstance(font, fontSize);
	if (inst != nil)
	{
		inst->MeasureWText(label, textW);
		ascent  = inst->GetAscent();
		descent = inst->GetDescent();
	}
	if (textW <= 0)
		textW = fontSize * PMReal(0.6) * PMReal(label.CharCount());

	// 左上に置く(旧 HUD と同じ位置)。show はベースライン左端を (x,y) に置くので、左端の余白がそのまま tx。
	// ★このポートの座標系へ落とす = pasteboard の値から spreadOffset を引く(平行移動のみ)。
	const PMReal tx = boundsPb.Left() + PMReal(kKCMTrackerHudLeftPx) / sx - spreadOffset.X();
	const PMReal ty = boundsPb.Top()  + PMReal(kKCMTrackerHudBaselinePx) / sy - spreadOffset.Y();

	// 下地の矩形(= 透明グループの範囲でもある)。PMRect は (左, 上, 右, 下)。
	const PMRect hudRect(tx - PMReal(kKCMTrackerHudPadXPx) / sx,
	                     ty - ascent - PMReal(kKCMTrackerHudPadTopPx) / sy,
	                     tx + textW + PMReal(kKCMTrackerHudPadXPx) / sx,
	                     ty + descent + PMReal(kKCMTrackerHudPadBotPx) / sy);

	// ★下地と文字を透明グループで束ね、不透明度は**グループに1回だけ**掛ける。個別に setopacity すると
	//   文字と下地が重なる画素だけ濃くなる(旧ページ番号バッジ・旧 HUD と同じ作法)。
	AutoGSave ag(gPort);
	gPort->setopacity(PMReal(kKCMTrackerHudOpacity), kFalse);
	gPort->starttransparencygroup(hudRect, nil, kFalse /*non-isolated*/, kFalse /*no knockout*/);

	// (1) 下地: 白ベタ。rectfill は (左, 上, 幅, 高さ)。
	gPort->newpath();
	gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));
	gPort->rectfill(hudRect.Left(), hudRect.Top(), hudRect.Width(), hudRect.Height());
	gPort->newpath();

	// (2) 文字: 黒。読みやすさは下地が担保するので、フチもブレンドも要らない。
	gPort->setrgbcolor(PMReal(0.0), PMReal(0.0), PMReal(0.0));
	gPort->selectfont(font, fontSize);
	gPort->show(tx, ty, label.NumUTF16TextChars(), label.GrabUTF16Buffer(nil), IGraphicsPort::kFillText);
	gPort->newpath();

	gPort->endtransparencygroup();
}

void KCMTrackerHudShutdown()
{
	sActive = kFalse;
	sView   = nil;
	KCMTrackerHudReleaseFont();	// フォント参照を .pln が降りる前に必ず返す
}

// End, KCMTrackerHud.cpp.
