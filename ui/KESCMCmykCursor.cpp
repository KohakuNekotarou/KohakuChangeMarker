//========================================================================================
//
//  KESCMCmykCursor.cpp
//
//  Alt+左「色比較」の実装(KESCMPeek.cpp から分離。2026-08-13 の model/UI 分割 第1段 Task 1)。
//  押下中に固定するモード(どの文書を見ているか)、カーソル自身への CMYK 描画、ドラッグ中のライブ更新、
//  「値なし」表示の組み立てを持つ。
//
//  ★分離では関数の中身を1行も変えていない。変えたのは「どのファイルに座るか」と「誰から見えるか」だけ。
//    ★★押下中の状態(sCmyk*)はこのファイルだけが持つ。分離前は3つのファイル(カーソル描画・ジェスチャ・
//      Shutdown)から直接触られており、それがこの分割で最初に割れなかった相手だった。外から使う3つの
//      塊は KESCMCmykBeginPress / KESCMCmykEndPress / KESCMCmykShutdown として入口を1本ずつ用意した。
//
//  UI 側: カーソルビットマップを作り、gPort へ描く。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// オブジェクトモデル:
#include "IDataBase.h"
#include "IApplication.h"
#include "IDocumentList.h"
#include "ISession.h"

// ジオメトリ / ビュー:
#include "IControlView.h"
#include "PMReal.h"
#include "PMString.h"

// カスタムビットマップカーソル(Alt+左 CMYK 情報をカーソルにも描く):
// (ICursorUtils.h は KESCMCheckGlyph.h 経由で入る=直接シンボルを使わないため直 include は撤去 2026-07-25)
#include "IGraphicsPort.h"			// setrgbcolor/rectfill/selectfont/show
#include "IFontMgr.h"				// 既定フォント取得
#include "IPMFont.h"

#include <chrono>				// steady_clock(ドラッグ中ライブ再サンプルのスロットル)

// プロジェクト内インクルード:
// ★★2026-08-15(第2段 Task 4B): **KESCMColorSampler.h(model 側)の include を落とした**。
//   このファイルは UI 側なので向き自体は合法だったが、呼んでいた3本(KESCMSampleCmykUnderMouse /
//   KESCMSampleCmykBeginDrag / KESCMSampleCmykEndDrag)は**自由関数**で、別 .pln になった途端に
//   リンクできない。⇒ IKESCMCompareFacade の SampleColorAt / BeginColorDrag / EndColorDrag へ通した。
#include "KESCMCheckGlyph.h"         // KESCMDrawCheckGlyph(✓描画を CMYK カーソルと共有)
#include "KESCMUIShared.h"	// panel / status line / nav readout / tool button (split from KESCMCore.h on 2026-08-13)
#include "KESCMViewLookup.h"         // KESCMQueryViewUnderMouse / KESCMFindDocDbForView / KESCMQueryMouseContentPoint
                                     // (2026-08-13 に KESCMCore.h から移動。★3本目は 2026-08-15 に増えた＝
                                     //  サンプリング点の解決がサンプラーからこちらへ来たため)
#include "Utils.h"                   // Utils<IKESCMCompareFacade>()
#include "IKESCMCompareFacade.h"     // arm 状態 / ArmedDocsAlive(2026-08-13・分割 第1段 Task 11)
                                     // ＋ CMYK サンプリング3本(2026-08-15・第2段 Task 4B)
#include "KESCMCmykCursor.h"

// Alt+左(CMYK 色ピック)の押下中モード。押下時に「マウス下の文書」で決めて固定し、RevealEnd で捨てる
// (押下の外では常に nil/既定)。★2026-07-26 にユーザー指定で3通りへ拡張(旧 sSoloCmykDB 1本を置換):
//   Start 中・Target 窓 … hover=Target / other=Source(比較2行、1行目 "t")
//   Start 中・Source 窓 … hover=Source / other=Target(比較2行、1行目 "s")
//   Start 中・第3の文書 / Stop 中 … hover=その文書 / other=nil(単独1行)
// 押下中に別の窓へドラッグしても基準は切り替えない(行の上下が入れ替わらないように。外れている間は
// サンプラが窓の同一性ガードで弾き「値なし ---」になる)。ポインタは照合専用で deref しない。
static IDataBase* sCmykHoverDB       = nil;		// マウスが乗っている側=1行目に出す文書
static IDataBase* sCmykOtherDB       = nil;		// 比較相手(nil=単独モード)
static bool16     sCmykHoverIsTarget = kFalse;	// hover が Target(新)側か=ページ対応の向きとラベル t/s

//========================================================================================
// Alt+左「色比較」の CMYK 情報を、パネル状態行に加えて**カーソル自身**にも描く。
//   カーソルは OS 描画=ドキュメント窓枠を超えマウス追従(仕組み: CursorSpec のコールバックで
//   自前バッファに AGM 描画する「カスタムビットマップカーソル」。ChangeModalCursor はトラッカー
//   =独自ツールを持つ KESCM だから使える特典)。CreateCursorBitmapProc は引数でデータを渡せない
//   ので、描く文字列は file-static sCmykCursorText に置きコールバックから読む。
//   ★これはまず「出るか」を見る実装スパイク(2026-07-13)。座標系(y方向)・alpha・サイズは実機で調整。
//========================================================================================
static PMString sCmykCursorText;			// "… t\n… s"(LF区切り2行、末尾ラベル t/s。1行目=マウスが乗っている窓の側)。色サンプル成功時に格納。
static bool16   sCmykCursorPending = kFalse;	// 直近の BeginTracking で CMYK カーソルを出すべきか

// ドラッグ中ライブ再サンプルのスロットル(既定 50ms ≒ 20回/秒)。★押下ごとに必ず初回を通すため、
// RevealEnd と Shutdown で sCmykDragThrottleStarted を戻す(2026-08-06 の監査 C-1)。以前は
// KESCMTrackerUpdateCmykDrag の関数内 static だったので一度立つと戻らず、2回目以降の押下では
// ドラッグ最初のサンプルが前回の押下から数えたスロットルに引っかかり得た(押下時の値は RevealBegin が
// 出しているので画面が空になることはないが、このファイルの「押下の外では状態を持たない」方針から外れる)。
static std::chrono::steady_clock::time_point sCmykDragLastSample;
static bool16                                sCmykDragThrottleStarted = kFalse;

// Alt+左ドラッグ中だけ保持する既定フォント(取得=RevealBegin の Alt 分岐、解放=RevealEnd)。
// ドラッグ中のカーソル再描画(≦20回/秒)が毎回 IFontMgr の名前引きをしないためのキャッシュ(2026-07-15)。
// ★file-static の InterfacePtr にはしない: 静的破棄タイミングの Release はオブジェクトモデル消滅後で
//   危険なため、生ポインタ+RevealEnd での明示解放(押下の外では常に nil)にする。
static IPMFont* sCmykCursorFont = nil;

// LF(0x0A)で最大2行に分割する。
static void KESCMSplitTwoLines(const PMString& src, PMString& line1, PMString& line2)
{
	line1.Clear(); line1.SetTranslatable(kFalse);
	line2.Clear(); line2.SetTranslatable(kFalse);
	const int32 n = src.NumUTF16TextChars();
	const UTF16TextChar* buf = src.GrabUTF16Buffer(nil);
	bool16 second = kFalse;
	for (int32 i = 0; i < n; ++i)
	{
		if (buf[i] == 0x000A) { second = kTrue; continue; }
		if (!second) line1.AppendW(UTF32TextChar(buf[i]));
		else         line2.AppendW(UTF32TextChar(buf[i]));
	}
}

// スペース区切りの行を「表」状に描く。先頭4トークン(見出し C/M/Y/K、または3桁値)を x0+col*pitch の
// 固定列に、5トークン目以降(ラベル tgt/src)は4列目の右(x0+4*pitch)に置く。ヘッダー行とデータ行を同じ
// x0/pitch で描けば CMYK 見出しと数字の桁が必ず縦にそろう(フォント計測不要=ユーザー要望の縦位置合わせ
// 2026-07-13)。描画は KESCMShowHalo(白フチ＋黒本体)。
static void KESCMShowHalo(IGraphicsPort* gPort, const PMReal& x, const PMReal& y, const PMString& s);	// 前方宣言

static void KESCMDrawColumns(IGraphicsPort* gPort, IPMFont* font, const PMReal& fs,
                             const PMReal& x0, const PMReal& pitch, const PMReal& y, const PMString& row)
{
	if (font == nil)
		return;
	// ★フォントの選択はこの行で1回だけ(2026-08-06 の監査 C-5)。以前は KESCMShowHalo の中で
	//   トークンごとに selectfont していたため、1フレームで最大15回・毎秒20フレームぶん繰り返していた。
	//   同じフォント・同じサイズなので1回で足りる。
	gPort->selectfont(font, fs);

	PMString tok; tok.SetTranslatable(kFalse);
	int32 col = 0;
	const int32 n = row.NumUTF16TextChars();
	const UTF16TextChar* b = row.GrabUTF16Buffer(nil);
	for (int32 i = 0; i <= n; ++i)
	{
		if (i < n && b[i] != 0x0020)	// スペース以外は現在のトークンに積む
		{
			tok.AppendW(UTF32TextChar(b[i]));
			continue;
		}
		if (tok.NumUTF16TextChars() > 0)	// 区切り(スペース or 行末)でトークン確定
		{
			const int32 c = (col < 4) ? col : 4;	// 5番目以降(ラベル)は4列目の右へ
			KESCMShowHalo(gPort, x0 + pitch * PMReal(c), y, tok);
			++col;
			tok.Clear();
			tok.SetTranslatable(kFalse);
		}
	}
}

// (x,y) に文字列を描く(空なら何もしない)。
// ★前提: フォントは呼び出し側(KESCMDrawColumns)が selectfont 済み。ここでは選び直さない(監査 C-5)。
static void KESCMShowHalo(IGraphicsPort* gPort, const PMReal& x, const PMReal& y, const PMString& s)
{
	const int32 n = s.NumUTF16TextChars();
	if (n <= 0)
		return;
	const UTF16TextChar* b = s.GrabUTF16Buffer(nil);

	// 白フチ(8方向に1pxずらして白で描く)→ 黒本体。透明背景でも明暗どちらの下地でも読める。
	static const int kDX[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
	static const int kDY[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
	const PMReal o(1.0);
	gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));
	for (int i = 0; i < 8; ++i)
		gPort->show(x + PMReal(kDX[i]) * o, y + PMReal(kDY[i]) * o, (uint32)n, b);
	gPort->setrgbcolor(PMReal(0.0), PMReal(0.0), PMReal(0.0));
	gPort->show(x, y, (uint32)n, b);
}

// CMYK カーソルの上部に載せる✓。色は arm 状態だけで出し分ける(常時ツールカーソル KESCMCursorProvider.cpp と
// 同じ規則。ユーザー要望 2026-07-24 / 2026-07-26 に「Start 中はどの文書の上でも黒」で確定): Start(arm 済み)は
// 黒✓、Stop(未 arm)は白抜き✓(黒フチ+白本体=KESCMCheckCursorInactiveBitmapProc と同一パラメータ)。
// ★Start 中の第3の文書は表示こそ単独1行(Stop と同じ)だが、✓は黒のまま=「比較は動いている」を示す。
static void KESCMDrawCmykCursorCheck(IGraphicsPort* gPort)
{
	if (Utils<IKESCMCompareFacade>()->IsArmed())
		KESCMDrawCheckGlyph(gPort);											// 黒✓(Start)
	else
		KESCMDrawCheckGlyph(gPort, PMReal(1.0), PMReal(0.0), PMReal(5.0));	// 白抜き✓(Stop)
}

// CursorSpec のコールバック。カーソル描画系が呼ぶ(UIスレッド)。bitmapBuffer は呼び出し側が
// (最大カーソルサイズ)²×4 で確保済み。*width/*height は入力=最大サイズ(hiRes 時は 2 倍)、出力=実使用サイズ。
static void KESCMCmykCursorBitmapProc(uchar* bitmapBuffer, uint32* width, uint32* height, bool16* hasAlpha, bool16 hiRes)
{
	// 前処理(確保全域の透明クリア+論理最大サイズ取得)は ✓カーソルと共有(KESCMCheckGlyph.h)。
	// 背景は透明のまま=黒い箱を出さない(ユーザー指定 2026-07-13)。
	uint32 maxLogW = 0, maxLogH = 0;
	KESCMCursorBitmapBegin(bitmapBuffer, *width, *height, hiRes, maxLogW, maxLogH);

	// 表示文字列(数値2行、各行末尾にラベル t/s)を分解し、最長行から「幅いっぱいに収まる大きめフォント」を
	// 決める(ユーザー要望 2026-07-13: カーソル最大サイズまで使って cmyk＋数値を大きく)。
	PMString line1, line2;
	KESCMSplitTwoLines(sCmykCursorText, line1, line2);
	const int32 chars1 = line1.NumUTF16TextChars();
	const int32 chars2 = line2.NumUTF16TextChars();

	// ★空文字ガード(2026-07-25 追加)。文字列が空のまま呼ばれると下の maxChars が 1 になり、fs が
	//   (maxLogW-8)*100/58 = 100〜200pt まで跳ね上がって、巨大な "C M Y K" がカーソル全面に描かれる
	//   =見た目はまさに「ゴミ」。通常経路(InstallCmykCursor は値が採れた時だけ)では起きないが、
	//   カーソルキャッシュの再生成や DPI 変更で proc が呼ばれると露出しうるので保険を入れる。
	//   ★*width/*height/*hasAlpha を設定せずに return してはいけない(未設定だと最大サイズ・24bit RGB
	//     扱い等で本物のゴミになる)。ツール常時カーソルと同じ「✓だけの 24x24」に倒す。
	if (chars1 <= 0 && chars2 <= 0)
	{
		InterfacePtr<IGraphicsPort> gPortCheckOnly(KESCMCursorBitmapFinish(
			bitmapBuffer, width, height, hasAlpha, hiRes, 24u, 24u, maxLogW, maxLogH));
		if (gPortCheckOnly == nil)
			return;
		gPortCheckOnly->setopacity(PMReal(1.0), kFalse);
		KESCMDrawCmykCursorCheck(gPortCheckOnly);
		return;
	}

	int32 maxChars = (chars2 > chars1) ? chars2 : chars1;
	if (maxChars < 1) maxChars = 1;

	// フォントは使える最大幅から大きめに決める(1文字≒0.58em、下限7pt)。
	// ★上限キャップは撤廃で確定(2026-07-14 検証→2026-07-25 採用を明文化): 18→26→48pt と上げても実機で
	// 変化が無かった=maxLogW(カーソル最大論理サイズ=OS/カーソルマネージャ依存)からの逆算値が実質の
	// 上限として機能しており、人工的なキャップは不要。
	int32 fs = ((int32)maxLogW - 8) * 100 / (maxChars * 58);
	if (fs < 7)  fs = 7;

	// ★ビットマップ幅は「実際の内容幅」にタイトに合わせる。最大幅いっぱいに取ると右側に広い透明余白が
	// でき、その初回フレームがちらついて見える(ゴミ)ため。内容幅 = 左6 + 4列×ピッチ(2.1em) +
	// ラベル(t/s=1文字≒0.58em) + 右4 ≒ 10 + 8.98em(下の描画の pitch=2.1×fs と一致させること。
	// 2026-07-15: ラベルを tgt/src→t/s へ短縮したのに合わせ 10.14em→8.98em に更新=右端の透明余白を除去)。
	const int32 contentW = 10 + (fs * 898) / 100;	// fs>=7 保証(上のクランプ)により常に正
	uint32 logW = (uint32)contentW;					// クランプは KESCMCursorBitmapFinish が行う

	// ✓(上部 y≈18 まで)の下に「ヘッダー C M Y K + データ2行(Target/Source)」を積む。位置・高さは fs から。
	const int32 gap    = (fs * 130) / 100;	// 行間 ≒1.3em
	const int32 yHdr   = 22 + fs;			// ヘッダー行ベースライン(✓の下。全体を少し下げた=ユーザー要望 2026-07-13)
	const int32 yData1 = yHdr + gap;		// Target 行
	const int32 yData2 = yData1 + gap;		// Source 行
	// 最下段(Source 行 "src")はディセンダ(下に伸びる字)が無いので、ベースラインのすぐ下でビットマップを
	// 終える。下端の透明余白を残すと、そこに初回フレームのちらつき(ゴミ)が出る(ユーザー報告: 文字より
	// 約3px下に一瞬。2026-07-13)。ハロー(y+1)とAA ぶんだけ +2 で足りる。
	// ★solo(Stop 単独ピック=line2 空)は Target 行までで終える(2026-07-25 監査で修正): 常に yData2 基準だと
	//   使わない Source 行ぶんの透明帯が下に残り、上の「余白タイト化」方針と矛盾していた。
	int32 needH = ((chars2 > 0) ? yData2 : yData1) + 2;
	uint32 logH = (needH > 0) ? (uint32)needH : 60u;

	// サイズ確定(クランプ込み)+AGM ポート取得(✓カーソルと共有の後処理。KESCMCheckGlyph.h)。
	InterfacePtr<IGraphicsPort> gPort(KESCMCursorBitmapFinish(
		bitmapBuffer, width, height, hasAlpha, hiRes, logW, logH, maxLogW, maxLogH));
	if (gPort == nil)
		return;

	// 背景は透明(上で全域 ARGB=0 にクリア済み)。setopacity は以降のストローク/文字を不透明にするため。
	gPort->setopacity(PMReal(1.0), kFalse);
	/* 背景塗りは廃止=透明のまま。黒い箱を出さない(ユーザー指定 2026-07-13) */

	// ツール選択中と同じ✓を、共有ヘルパ KESCMDrawCheckGlyph でホットスポット(10,18)=✓の折れ点=
	// クリック点に描く(KESCMCursorProvider.cpp と同一形状/座標)。数値表示中もカーソル形状を残す
	// (ユーザー要望 2026-07-14)。★以前は「✓ を stroke で描くと初回フレームのちらつき(ゴミ)が出る」と
	// 考えて rectfill のドットに退避していたが(2026-07-13)、その後の調査でゴミの真因は stroke 描画では
	// なく BeginTracking の多段カーソル切替が OS のハードウェアカーソル合成にそのまま見えていたことだと
	// 判明した(対策は KESCMTracker.cpp の BeginTracking = サンプリングを切替の前へ出す)。stroke 自体は
	// 無罪なので✓に戻して問題ない。色の出し分けは KESCMDrawCmykCursorCheck に集約(空文字ガードと共有)。
	KESCMDrawCmykCursorCheck(gPort);

	// 上から: ヘッダー "C M Y K"(各列先頭にそろえる) / Target 数値 / Source 数値。数値は各値3桁で行頭
	// そろえ、末尾に t/s。フォント fs・行位置は上で計算済み。描画は KESCMShowHalo(白フチ＋黒本体)。
	// フォントは押下中キャッシュ(sCmykCursorFont。ドラッグ再描画≦20回/秒の名前引き回避)を使い、
	// 万一 nil ならローカルに引き直すフォールバック(2026-07-15)。
	IPMFont* font = sCmykCursorFont;
	InterfacePtr<IPMFont> fallbackFont;
	if (font == nil)
	{
		InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), UseDefaultIID());
		if (fontMgr != nil)
			fallbackFont = InterfacePtr<IPMFont>(fontMgr->QueryFont(fontMgr->GetDefaultFontName()));
		font = fallbackFont;
	}
	if (font != nil)
	{
		// 見出し行とデータ2行を同じ固定列(x0, pitch)で描いて桁を縦にそろえる(pitch=3桁+ギャップ)。
		// ヘッダーの C/M/Y/K が各3桁列の真上に来る(ユーザー要望の縦位置合わせ 2026-07-13)。
		const PMReal x0(6.0);
		const PMReal pitch = PMReal(fs) * PMReal(2.1);
		PMString hdr; hdr.SetTranslatable(kFalse); hdr.Append("C M Y K");
		KESCMDrawColumns(gPort, font, PMReal(fs), x0, pitch, PMReal(yHdr),   hdr);	// 見出し C M Y K
		KESCMDrawColumns(gPort, font, PMReal(fs), x0, pitch, PMReal(yData1), line1);	// 1行目=マウスが乗っている窓の側(t or s)
		KESCMDrawColumns(gPort, font, PMReal(fs), x0, pitch, PMReal(yData2), line2);	// 2行目=比較相手(単独モードでは空=自動スキップ)
	}
}

// KESCMTracker.cpp から使う入口。BeginTracking の CMYK 分岐が成功したら Pending が立ち、トラッカーが
// ChangeModalCursor(CursorSpec(KESCMTrackerCmykCursorProc(), …)) を呼ぶ。
bool16 KESCMTrackerHasPendingCmykCursor()          { return sCmykCursorPending; }
CreateCursorBitmapProc KESCMTrackerCmykCursorProc() { return &KESCMCmykCursorBitmapProc; }

// ツール常時✓カーソルの黒/白抜き判定(KESCMCmykCursor.h 参照)。
// ★2026-07-26(ユーザー指定): 黒=「Start 中(比較文書が生存)」だけで決める。マウス下がどの文書かは見ない。
//   Alt+左の CMYK は Start 中ならどの窓でも値を出す(Target/Source 窓=比較2行、第3の文書=単独1行)ので、
//   以前の「Target 窓だけ黒」は実態と合わなくなった。Stop 中は従来どおり白抜き✓(黒フチ+白本体)。
//   viewUnderMouse が nil(レイアウトビュー上に居ない)なら白抜きのまま=カーソル形状の既定側に倒す。
// ★引数 viewUnderMouse は「レイアウトビューの上に居るか」の判定にだけ使う(どの文書のビューかは見ない)。
//   2026-07-26 の仕様変更で「文書を問わず黒」になったため、ビューの中身は色に関与しない(監査 C-2)。
bool16 KESCMToolCursorShouldBeBlack(IControlView* viewUnderMouse)
{
	if (viewUnderMouse == nil)
		return kFalse;
	return Utils<IKESCMCompareFacade>()->ArmedDocsAlive();
}

// KESCMTrackerUpdateCmykDrag(KESCMCmykCursor.h 参照) — ドラッグ中の CMYK ライブ更新。
// トラッカーの ContinueTracking(マウス移動)から呼ばれる。現在のマウス位置で CMYK を再サンプルし、
// 値が変わったら sCmykCursorText を更新して kTrue を返す(呼び出し側がカーソルを描き直す)。
// 連続ラスタ化で重くならないよう時間スロットル(既定 50ms ≒ 20回/秒)を掛ける。
// 前方宣言。定義は KESCMCmykBeginPress の直前(ページ外の「値なし c---」表示を作る)。
// hoverIsTarget= 1行目(=マウスが乗っている窓)のラベルが t か s か。
static void KESCMBuildCmykNoValue(PMString& out, bool16 hoverIsTarget);			// 比較: カーソル用(t/s)
static void KESCMBuildCmykNoValuePanel(PMString& out, bool16 hoverIsTarget);	// 比較: パネル用(見出し文字+t/s)
static void KESCMBuildCmykNoValueSolo(PMString& out);							// 単独: カーソル1行(ラベルなし)
static void KESCMBuildCmykNoValuePanelSolo(PMString& out);						// 単独: パネル1行(ラベルなし)

// 押下中に固定した CMYK 対象文書(sCmykHoverDB / sCmykOtherDB)がまだ開いているか。ドラッグ中に稀な経路で
// 文書が閉じても、解放済み IDataBase をサンプリングへ渡さないための最終ライン防御。
//   比較モード … hover/other は arm 済みの Target/Source なので arm 版の検査に委ねる
//                (KESCMArmedDocsAlive は失格時に Stop 相当のクリーンアップまでやる)。
//   単独モード … マウス下の1文書をドキュメントリストに照合するだけ(第3の文書や Stop 中なので arm と無関係)。
static bool16 KESCMCmykDocsAlive()
{
	if (sCmykHoverDB == nil)
		return kFalse;
	if (sCmykOtherDB != nil)
		return Utils<IKESCMCompareFacade>()->ArmedDocsAlive();
	ISession* session = GetExecutionContextSession();	// 終了処理中は nil になり得る
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	return (docList != nil && docList->FindDocByDataBase(sCmykHoverDB) != nil) ? kTrue : kFalse;
}

bool16 KESCMTrackerUpdateCmykDrag()
{
	if (!sCmykCursorPending)	// Alt+左 CMYK モードでなければ何もしない
		return kFalse;

	// 押下時に固定したモード(hover/other)をそのまま使う。押下中に基準の窓は切り替えない。
	if (sCmykHoverDB == nil)
		return kFalse;
	const bool16 solo = (sCmykOtherDB == nil);

	// スロットル(50ms)。steady_clock は単調増加なのでラップの心配なし。★押下ごとの初回は必ず通す
	// (旗は RevealEnd/Shutdown で戻る＝押下を跨がない。監査 C-1)。
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	if (sCmykDragThrottleStarted)
	{
		const long long ms =
			std::chrono::duration_cast<std::chrono::milliseconds>(now - sCmykDragLastSample).count();
		if (ms < 50)
			return kFalse;
	}
	sCmykDragThrottleStarted = kTrue;
	sCmykDragLastSample = now;

	// スロットル通過後(≦20回/秒)に文書の生存を検査してからサンプリングへ渡す
	// (ドラッグ中の文書クローズはスクリプト経由等の稀な経路。検査は KESCMCmykDocsAlive に集約)。
	if (!KESCMCmykDocsAlive())
		return kFalse;

	// 現在のマウス位置でサンプリング。単独モードは other=nil で hover だけ(1行)。ページ外・押した窓から
	// 外れた・取得失敗なら「値なし(--- …)」表示にして拾えていないことを示す(ユーザー要望 2026-07-13。
	// 直前値を残さない=誤読防止)。
	//
	// ★★2026-08-15(第2段 Task 4B): **マウス位置の読み直しと「押した窓から外れていないか」の判定は、
	//   以前はサンプラー(model 側)の中に在った**。窓に向かって聞く問いなので UI 側へ引き取っている。
	//   ⚠**この2行を落とすと、別の窓の座標を sCmykHoverDB のページ座標として誤って読む**
	//   (2026-07-25 監査で入れたガード)。3つの条件は && の短絡で、旧実装の3連続 return と同じ順序・
	//   同じ結果(どれか1つでも駄目なら「値なし」表示)。
	PMString panelMsg, cursorMsg;
	InterfacePtr<IControlView> viewUnderMouse(KESCMQueryViewUnderMouse());
	PMReal mx = 0.0, my = 0.0;
	if (KESCMFindDocDbForView(viewUnderMouse) != sCmykHoverDB ||
	    !KESCMQueryMouseContentPoint(viewUnderMouse, mx, my) ||
	    // ★2026-08-16: 表示中スプレッドも渡す(無いとマスター表示中に通常ページの色を読む＝KESCMCore.h)。
	    !Utils<IKESCMCompareFacade>()->SampleColorAt(sCmykHoverDB, sCmykOtherDB, sCmykHoverIsTarget,
	                                                 mx, my, KESCMQuerySpreadUIDForView(viewUnderMouse),
	                                                 panelMsg, cursorMsg))
	{
		if (solo) { KESCMBuildCmykNoValueSolo(cursorMsg); KESCMBuildCmykNoValuePanelSolo(panelMsg); }
		else      { KESCMBuildCmykNoValue(cursorMsg, sCmykHoverIsTarget);
		            KESCMBuildCmykNoValuePanel(panelMsg, sCmykHoverIsTarget); }
	}
	if (cursorMsg == sCmykCursorText)	// 値が同じなら描き直し不要(パネルも同じ値なので更新不要)
		return kFalse;

	// パネルのステータス行もドラッグに追従させる(強制表示はしない。KESCMCmykBeginPress と同じ方針)。
	KESCMSetStatus(panelMsg);
	sCmykCursorText = cursorMsg;
	return kTrue;
}

// ページ外など CMYK を拾えないときに出す「値なし」表示("--- --- --- --- t/s")。ダッシュで
// 「ここでは色を拾えていない」ことが分かるようにする(ユーザー要望 2026-07-13)。ラベルは通常と同じ t/s で、
// 1行目は成功時と同じく hover 側(Target 窓なら t、Source 窓なら s。2026-07-26)。
static void KESCMBuildCmykNoValue(PMString& out, bool16 hoverIsTarget)
{
	out.Clear();
	out.SetTranslatable(kFalse);
	out.Append(hoverIsTarget ? "--- --- --- --- t" : "--- --- --- --- s");	// ラベルは t/s(KESCMColorSampler.cpp と同じ短縮。2026-07-14)
	out.AppendW(UTF32TextChar(0x0A));	// 改行 → 2行目へ
	out.Append(hoverIsTarget ? "--- --- --- --- s" : "--- --- --- --- t");
}

// パネル版の「値なし」表示。値ごとに見出し文字を添え t/s にする。KESCMSampleCmykAt
// 成功時のパネル表記(KESCMColorSampler.cpp の KESCMAppendCmykLabeled)と揃える(2026-07-14)。
static void KESCMBuildCmykNoValuePanel(PMString& out, bool16 hoverIsTarget)
{
	out.Clear();
	out.SetTranslatable(kFalse);
	out.Append(hoverIsTarget ? "C--- M--- Y--- K--- t" : "C--- M--- Y--- K--- s");
	out.AppendW(UTF32TextChar(0x0A));
	out.Append(hoverIsTarget ? "C--- M--- Y--- K--- s" : "C--- M--- Y--- K--- t");
}

// 単独ピック(Stop 中、または Start 中の第3の文書)用の「値なし」1行版。ラベル(t/s)なし=1文書のみ。カーソル側は
// KESCMSplitTwoLines が空の2行目を自動スキップするので、1行渡すだけで崩れない。
static void KESCMBuildCmykNoValueSolo(PMString& out)
{
	out.Clear();
	out.SetTranslatable(kFalse);
	out.Append("--- --- --- ---");
}
static void KESCMBuildCmykNoValuePanelSolo(PMString& out)
{
	out.Clear();
	out.SetTranslatable(kFalse);
	out.Append("C--- M--- Y--- K---");
}

// KESCMCmykClearPending(KESCMCmykCursor.h 参照) — このプレスで CMYK カーソルを出すかの既定(=出さない)。
// ★2026-08-13 の分割で KESCMTrackerRevealBegin の冒頭1行から切り出した。**ジェスチャを判定する前に
//   無条件で通る**行なので、下の KESCMCmykBeginPress(Alt 分岐でだけ呼ばれる)とは別の入口が要る。
void KESCMCmykClearPending()
{
	sCmykCursorPending = kFalse;	// このプレスで CMYK カーソルを出すかは Cmyk 分岐で決める(既定=出さない)
}

// KESCMCmykBeginPress(KESCMCmykCursor.h 参照) — Alt+左(CMYK)押下の本体。
// ★2026-08-13 の分割で KESCMTrackerRevealBegin の Cmyk 分岐から切り出した(中身は元の行そのまま)。
//   切り出した理由は「押下中の状態をこのファイルの外から触らせない」ため。呼び手は
//   KESCMPeekGesture.cpp の KESCMTrackerRevealBegin ただ1つ。
void KESCMCmykBeginPress()
{
	// Alt+左(単独、Shift/Ctrl なし): クリック点の CMYK 生値(0..255)をサンプリングしカーソル自身に描画する。
	// ★発火条件=「マウス下にレイアウトビュー+文書がある」だけ(2026-07-26 にユーザー指定で拡張。以前は
	//   Start 中は Target 窓に限っていた)。押した窓で3通りに分岐する:
	//   Start 中・Target 窓  … 新・旧を比較(2行。1行目=Target "t" / 2行目=Source "s")
	//   Start 中・Source 窓  … 同じく比較だが向きが逆(1行目=Source "s" / 2行目=Target "t")
	//   Start 中・第3の文書 / Stop 中 … その1文書を単独ピック(1行、ラベルなし)
	// ★このブロックは基底 CTracker::BeginTracking より前に呼ばれる(KESCMTracker.cpp)。重いサンプリングを
	//   カーソル切替の前で終わらせ、切替を一瞬にするため(押下時のゴミ対策 2026-07-25)。
	InterfacePtr<IControlView> viewUnderMouse(KESCMQueryViewUnderMouse());
	IDataBase* const hoverDB = KESCMFindDocDbForView(viewUnderMouse);
	if (hoverDB != nil)
	{
		// カーソル再描画毎(≦20回/秒)の IFontMgr 名前引きを回避する押下中フォントキャッシュ(解放は RevealEnd)。
		if (sCmykCursorFont == nil)
		{
			InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), UseDefaultIID());
			sCmykCursorFont = (fontMgr != nil) ? fontMgr->QueryFont(fontMgr->GetDefaultFontName()) : nil;
		}

		// 押した窓の文書を Target / Source / それ以外 に分類してモードを固定する(解除は RevealEnd)。
		// 比較モードのときだけページ対応表キャッシュを用意する(サンプル毎の全ページ pairing 再構築を
		// 回避。向きも押下時のまま固定。破棄は RevealEnd)。単独モードはページ対応が無いので不要。
		IDataBase* otherDB      = nil;
		bool16     hoverIsTarget = kFalse;
		InterfacePtr<IKESCMCompareFacade> compare(Utils<IKESCMCompareFacade>().QueryUtilInterface());	// ★この分岐で6回聞く(Utils.h:74-80。2026-08-15 に BeginColorDrag / SampleColorAt が増えて 4→6)
		if (compare->ArmedDocsAlive())	// 比較中か(解放済み db との照合を避けるため生存検査を先に通す)
		{
			if (hoverDB == compare->GetArmedTargetDB())      { otherDB = compare->GetArmedSourceDB(); hoverIsTarget = kTrue;  }
			else if (hoverDB == compare->GetArmedSourceDB()) { otherDB = compare->GetArmedTargetDB(); hoverIsTarget = kFalse; }
		}
		sCmykHoverDB       = hoverDB;
		sCmykOtherDB       = otherDB;
		sCmykHoverIsTarget = hoverIsTarget;

		const bool16 solo = (otherDB == nil);
		if (!solo)
			compare->BeginColorDrag(hoverDB, otherDB, hoverIsTarget);

		// ★2026-08-15(第2段 Task 4B): サンプリング点はここで読む(以前はサンプラーの中で読んでいた)。
		//   ここでは「押した窓から外れていないか」の判定は要らない ---- hoverDB は今まさに
		//   viewUnderMouse から引いた db なので、定義上一致している(ドラッグ中の
		//   KESCMTrackerUpdateCmykDrag ではマウスが動くので、あちらには判定が要る)。
		PMString panelMsg, cursorMsg;
		PMReal mx = 0.0, my = 0.0;
		// ★2026-08-16: 表示中スプレッドも渡す(理由は KESCMCore.h＝マスターと通常は座標が重なる)。
		if (!KESCMQueryMouseContentPoint(viewUnderMouse, mx, my) ||
		    !compare->SampleColorAt(hoverDB, otherDB, hoverIsTarget, mx, my,
		                            KESCMQuerySpreadUIDForView(viewUnderMouse), panelMsg, cursorMsg))
		{
			// ページ外など: 拾えないことを示す(値なし --- 表示)。
			if (solo) { KESCMBuildCmykNoValueSolo(cursorMsg); KESCMBuildCmykNoValuePanelSolo(panelMsg); }
			else      { KESCMBuildCmykNoValue(cursorMsg, hoverIsTarget);
			            KESCMBuildCmykNoValuePanel(panelMsg, hoverIsTarget); }
		}
		// カーソル自身に CMYK を描く(トラッカーが ChangeModalCursor する)のに加えて、パネルのステータス行にも
		// 同じ値を出す。★KESCMSetStatus はパネルが非表示でも「強制的に表示」はしない(ON→表示中なら見える、
		// OFF→隠れたまま状態だけ覚える)。パネルを強制的に開かせることはしない(ユーザー指定)。
		KESCMSetStatus(panelMsg);
		sCmykCursorText    = cursorMsg;
		sCmykCursorPending = kTrue;
	}
}

// KESCMCmykEndPress(KESCMCmykCursor.h 参照) — 押下解放時の後始末。
// ★2026-08-13 の分割で KESCMTrackerRevealEnd の CMYK 部分から切り出した(中身は元の行そのまま)。
void KESCMCmykEndPress()
{
	// Alt+左(CMYK)の押下中キャッシュを返す/捨てる(取得は KESCMCmykBeginPress。押下の外では持たない)。
	if (sCmykCursorFont != nil)
	{
		sCmykCursorFont->Release();
		sCmykCursorFont = nil;
	}
	Utils<IKESCMCompareFacade>()->EndColorDrag();
	// 押下中に固定していた CMYK モード(hover/other)の保持を解除(押下の外では持たない)。
	sCmykHoverDB       = nil;
	sCmykOtherDB       = nil;
	sCmykHoverIsTarget = kFalse;
	sCmykDragThrottleStarted = kFalse;	// 次の押下でドラッグ初回サンプルを必ず通す(監査 C-1)

	// Alt+左(CMYK 色比較)を離したら、押下中にパネルのステータス行へ出していた CMYK 値を消す
	// (ユーザー要望 2026-07-15: ホールド終了でメッセージは消す)。sCmykCursorPending は押下中に
	// CMYK 値を出したときだけ立つので、色比較のときだけクリアし、reveal/peek や Check/Register 等
	// 他機能のステータスには触らない。
	// ★クリアは空文字ではなく「空白1文字」で行う(ユーザー指定 2026-07-15): 完全な空だと
	//   gSessionStatus が「未操作」と区別できず、次回パネルを開いたときに AutoAttach の
	//   CharCount()==0 判定で初回ヒントが再表示されてしまう。空白1文字なら見た目は空のまま
	//   「操作済み」を保てる(再表示でも空白が復元されるだけでヒントは出ない)。
	if (sCmykCursorPending)
	{
		PMString blank(" ");
		blank.SetTranslatable(kFalse);
		KESCMSetStatus(blank);
		sCmykCursorText.Clear();
		sCmykCursorPending = kFalse;
	}
}

// KESCMCmykShutdown(KESCMCmykCursor.h 参照) — 終了時の後始末。
// ★2026-08-13 の分割で KESCMPeekStartup::Shutdown の CMYK 部分から切り出した(中身は元の行そのまま)。
void KESCMCmykShutdown()
{
	// ★file-static PMString を空にして、プラグイン unload 時の静的デストラクタを実質 no-op にする
	// (Windows では実害なしの実績だが、Mac は unload 順が異なるため heap バッファを持ち越さない方が
	// 安全。2026-07-15 終了堅牢化)。
	sCmykCursorText.Clear();
	sCmykCursorPending = kFalse;	// ★押下中に quit した経路で立ったまま残さない(2026-08-06 再点検。
									//   下の sCmykCursorFont/sCmykHoverDB と同じ「押下の外では持たない」の徹底)

	// ★Alt+左ホールド中にアプリが終了する経路(スクリプト quit 等)では RevealEnd を通らず
	//   sCmykCursorFont が生きたまま残るので、ここで解放する(2026-07-25 監査で追加。通常経路では
	//   押下の外は常に nil なので no-op)。
	if (sCmykCursorFont != nil)
	{
		sCmykCursorFont->Release();
		sCmykCursorFont = nil;
	}
	// 同じ経路で残りうる押下中モードの文書ポインタも捨てる(deref しない照合専用だが、
	// 終了後に解放済みポインタを持ち越さない。2026-07-26)。
	sCmykHoverDB       = nil;
	sCmykOtherDB       = nil;
	sCmykHoverIsTarget = kFalse;
	sCmykDragThrottleStarted = kFalse;	// ドラッグ用スロットルの旗も残さない(監査 C-1)

	// 押下中のページ対応表キャッシュ(hover→other)も同様に破棄。
	// ★2026-08-15(第2段 Task 4B)に自由関数 KESCMSampleCmykEndDrag() から Facade 経由へ変えた。
	// ⚠**ここだけは nil 検査を付ける**: 終了処理中は kUtilsBoss 側が先に落ちている可能性があり、
	//   Utils<>()->M() の形だと nil 参照になる。上の KESCMCmykEndPress は押下解放=通常時なので素で呼ぶ。
	InterfacePtr<IKESCMCompareFacade> compare(Utils<IKESCMCompareFacade>().QueryUtilInterface());
	if (compare != nil)
		compare->EndColorDrag();
}
