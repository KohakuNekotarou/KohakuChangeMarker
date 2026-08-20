//========================================================================================
//
//  KESCMPanelState.cpp
//
//  パネルのフライアウトの設定系トグルを、独自 JSON ファイルとしてローカルのユーザー環境設定
//  フォルダーへ保存/復元する(KESCMPanelState.h 参照)。InDesign 本体のデータには一切書かない。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// 一般:
#include "PMString.h"
#include "FileUtils.h"		// GetAppRoamingDataFolder / AppendPath / OpenFile / DoesFileExist / SysFileToPMString
#include "IDFile.h"

#include <string>
#include <cstdio>			// FILE / fread / fwrite / fclose

// プロジェクト内(各トグルの状態アクセサ):
#include "KESCMPanelState.h"
#include "Utils.h"					// Utils<IKESCMCompareFacade>()
#include "IKESCMCompareFacade.h"	// 印刷マーク設定の読み書き(2026-08-13・分割 第1段 Task 11 で Facade 経由へ)
#include "KESCMUIShared.h"	// panel / status line / nav readout / tool button (split from KESCMCore.h on 2026-08-13)
#include "KESCMViewSync.h"			// KESCMGetLayoutSync / KESCMSetLayoutSync(2026-08-13 に KESCMCore.h から移動)
#include "KESCMScrollMap.h"			// KESCMGetScrollMapEnabled / KESCMSetScrollMapEnabled
#include "KESCMPanelAlpha.h"		// KESCMGetPanelTranslucent / KESCMSetPanelTranslucent(Translucent Panel)
#include "KESCMPanelTitle.h"		// KESCMPanelTitle::Update(復元した比較モードをタブへ反映)

// 保存ファイル名(Roaming 直下。★サブフォルダーは 2026-07-12 に廃止=下の KESCMPanelStateFile と
// KESCMPanelState.h:11 の説明が正)。
static const char* const kKESCMPanelStateFileName = "KESCMPanelState.json";

//----------------------------------------------------------------------------------------
// 保存先の解決
//----------------------------------------------------------------------------------------

// ローミング環境設定フォルダー(locale 付き)直下の KESCMPanelState.json への IDFile を outFile に返す。
// ★サブフォルダーは作らない(ユーザー指定 2026-07-12)。GetAppRoamingDataFolder の subFolderName に
//   ファイル名をそのまま渡すと、そのフォルダー直下の「ファイルの」IDFile が返る(SDK 実例:
//   SnpShareAppResources.cpp / SuppUISysFileData.cpp)。親フォルダーは InDesign が環境設定用に既に
//   作っているので CreateFolderIfNeeded は不要(旧実装で "KESCM" サブフォルダー作成が要ったのは、
//   存在しないサブフォルダー配下へ開こうとしていたため)。取得できなければ kFalse。
static bool16 KESCMPanelStateFile(IDFile& outFile)
{
	return FileUtils::GetAppRoamingDataFolder(&outFile, PMString(kKESCMPanelStateFileName));
}

//----------------------------------------------------------------------------------------
// 極小 JSON(自前で書き/寛容 read)
//   保存内容はフラットな真偽値だけなので、boost(IJsonUtils)依存を避けて自前で扱う。
//   ★公式クラス(`public/interfaces/utils/IJsonUtils.h` の `JSON`)の実例と、寄せない理由の全文は
//     `source/KESCMPageCheck.cpp` の保存/読み込みブロック(2026-08-16・API 監査 B4)。
//   ★**stdio(FileUtils::OpenFile)を使い IPMStream を使わない理由も同じ場所**＝IPMStream の
//     Close()/Flush() が void で、ディスクフルを検出できないため(2026-08-10 に KBS 側で決着)。
//----------------------------------------------------------------------------------------

static const char* KESCMBoolLiteral(bool16 b)
{
	return b ? "true" : "false";
}

// text の中から "key" を探し、その後の最初の ':' に続く true/false を読む。見つからなければ defVal。
static bool16 KESCMJsonReadBool(const std::string& text, const char* key, bool16 defVal)
{
	std::string needle("\"");
	needle += key;
	needle += "\"";

	const size_t k = text.find(needle);
	if (k == std::string::npos)
		return defVal;
	const size_t colon = text.find(':', k + needle.size());
	if (colon == std::string::npos)
		return defVal;

	size_t p = colon + 1;
	while (p < text.size() && (text[p] == ' ' || text[p] == '\t' || text[p] == '\n' || text[p] == '\r'))
		++p;

	if (text.compare(p, 4, "true") == 0)
		return kTrue;
	if (text.compare(p, 5, "false") == 0)
		return kFalse;
	return defVal;
}

// text の中から "key" を探し、その後の最初の ':' に続く "文字列" を読む。見つからなければ空。
// ★★**bool にしなかった理由**（2026-08-21）＝比較モードは enum で、`"storyMode": true/false` と
//   書くと3つ目のモードが増えた日に**保存ファイルの意味が変わる**（false が「pixel」なのか
//   「story ではない何か」なのか言えなくなる）。名前で書けばその日に読み手を足すだけで済む。
static std::string KESCMJsonReadString(const std::string& text, const char* key)
{
	std::string needle("\"");
	needle += key;
	needle += "\"";

	const size_t k = text.find(needle);
	if (k == std::string::npos)
		return std::string();
	const size_t colon = text.find(':', k + needle.size());
	if (colon == std::string::npos)
		return std::string();

	const size_t open = text.find('"', colon + 1);
	if (open == std::string::npos)
		return std::string();
	const size_t close = text.find('"', open + 1);
	if (close == std::string::npos)
		return std::string();

	return text.substr(open + 1, close - open - 1);
}

//----------------------------------------------------------------------------------------
// 保存(フライアウトの「Save Panel Settings」から呼ばれる)
//----------------------------------------------------------------------------------------

void KESCMSavePanelState()
{
	IDFile file;
	if (!KESCMPanelStateFile(file))
	{
		PMString err("Save failed (folder)");	// パネルのステータス行に表示(幅が狭いので短く)
		err.SetTranslatable(kFalse);
		KESCMSetStatus(err, kTrue /*forceRedrawNow*/);
		return;
	}

	// 現在の状態を JSON 文字列に組み立てる。
	// ★何度も聞くので InterfacePtr で1回引く(`Utils.h:74-80`＝「several places で使うなら一度引いて
	//   InterfacePtr に持て」)。⚠**公式は回数を数字で示していない**——「3回以上なら」は手元の目安で、
	//   旧コメントはそれを「公式」と書いていた(2026-08-16・監査 B-U3 で訂正)。
	InterfacePtr<IKESCMCompareFacade> compare(Utils<IKESCMCompareFacade>().QueryUtilInterface());
	std::string json;
	json += "{\n";
	json += "  \"version\": 1,\n";
	json += "  \"printMarks\": ";             json += KESCMBoolLiteral(compare->GetPrintMarks());                   json += ",\n";
	json += "  \"opacity25\": ";              json += KESCMBoolLiteral(compare->GetMarkOpacity25());                json += ",\n";
	json += "  \"holdToHideMarks\": ";        json += KESCMBoolLiteral(compare->GetHoldToHideMarks());              json += ",\n";
	json += "  \"showSrcMarks\": ";           json += KESCMBoolLiteral(compare->GetShowSourceMarks());              json += ",\n";
	json += "  \"showOldNumbers\": ";         json += KESCMBoolLiteral(compare->GetShowOldPageNumbers());           json += ",\n";
	json += "  \"syncLayoutViews\": ";        json += KESCMBoolLiteral(KESCMGetLayoutSync());                       json += ",\n";
	json += "  \"scrollbarMap\": ";           json += KESCMBoolLiteral(KESCMGetScrollMapEnabled());                 json += ",\n";
	json += "  \"ignorePageNumberMarker\": "; json += KESCMBoolLiteral(compare->GetIgnorePageNumberMarker());               json += ",\n";
	json += "  \"translucentPanel\": ";       json += KESCMBoolLiteral(KESCMGetPanelTranslucent());                 json += ",\n";
	json += "  \"translucentPagesPanel\": ";  json += KESCMBoolLiteral(KESCMGetPagesPanelTranslucent());            json += ",\n";
	json += "  \"translucentBookDialog\": ";  json += KESCMBoolLiteral(KESCMGetBookDialogTranslucent());            json += ",\n";
	// ★比較モード（2026-08-21・ユーザー指定）。⚠**唯一の非 bool の項目**なので、上と違って
	//   値を引用符で囲む。⚠**古い設定ファイルにはこのキーが無い**が、読み手が「無ければ今の値」を
	//   採るので、旧ファイルを読んでも既定（Pixel）のままになるだけで害は無い。
	json += "  \"compareMode\": \"";
	json += (compare->GetCompareMode() == kKESCMModeStory ? "story" : "pixel");
	json += "\"\n";
	json += "}\n";

	FILE* fp = FileUtils::OpenFile(file, "wb");
	if (fp == nil)
	{
		PMString err("Save failed (open)");	// パネルのステータス行に表示
		err.SetTranslatable(kFalse);
		KESCMSetStatus(err, kTrue /*forceRedrawNow*/);
		return;
	}
	// ★書込バイト数と fclose の成否を確認(2026-07-25 監査で追加): ディスクフル等の部分書込を
	//   「保存できた」(保存先パス表示)と誤報告しない。
	const size_t wrote = fwrite(json.data(), 1, json.size(), fp);
	const int closed = fclose(fp);
	if (wrote != json.size() || closed != 0)
	{
		PMString err("Save failed (write)");	// パネルのステータス行に表示
		err.SetTranslatable(kFalse);
		KESCMSetStatus(err, kTrue /*forceRedrawNow*/);
		return;
	}

	// 保存先のフルパスをパネルのステータス行に表示する(ユーザー要望 2026-07-11: モーダルからパネル表示へ)。
	// ★パスのみ(「Settings saved:」等のラベルを付けるとステータス行から溢れるため)。
	// ⚠寸法は書き写さない＝正本は `ui/KCMUI.fr` の kKESCMStatusTextWidgetID(StaticMultiLineTextWidget)の
	//   `Frame(8,76,216,150)`(208×74px・4行)。旧「幅152px×4行」は 2026-07-15 世代の値で、同じ数字が
	//   3ファイルに散っていた(不具合再検査 B5)。
	//   (⚠旧引用 ":1921" は**空行**を指していた＝7行ずれ。2026-08-18・不具合再検査 B-U3 で
	//    widget 名で引く形へ。行番号は黙って嘘になる＝[[verify-claims-in-comments]]。)
	PMString msg;
	msg.SetTranslatable(kFalse);
	msg.Append(FileUtils::SysFileToPMString(file));
	KESCMSetStatus(msg, kTrue /*forceRedrawNow*/);
}

//----------------------------------------------------------------------------------------
// 復元(起動時=KESCMUIStartup::Startup から呼ばれる。セッション内一度だけ。
//   パネル AutoAttach からの呼び出しは内部ガードで no-op になる保険として残る。KESCMPanelState.h 参照)
//----------------------------------------------------------------------------------------

void KESCMLoadPanelStateIfPresent()
{
	static bool16 sLoaded = kFalse;
	if (sLoaded)
		return;
	sLoaded = kTrue;	// 成否に関わらずセッションで一度だけ試みる

	IDFile file;
	if (!KESCMPanelStateFile(file))
		return;
	if (!FileUtils::DoesFileExist(file))
		return;		// 保存データが無い=初回。既定値のまま。

	FILE* fp = FileUtils::OpenFile(file, "rb");
	if (fp == nil)
		return;
	std::string text;
	char buf[1024];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
		text.append(buf, n);
	const bool readFailed = (ferror(fp) != 0);
	fclose(fp);
	if (readFailed)
		return;		// ★読み取りが途中で失敗した部分テキストで適用しない(2026-08-06 再点検。
					//   KESCMReadWholeFile(KESCMPageCheck.cpp)と同じ作法。全トグル既定値のままにする)
	if (text.empty())
		return;

	// ---- 各トグルへ適用 ----
	// ★順序: 不透明度に影響する表示トグル(Hold to Hide Marks)を先に反映してから SetPrintMarks を
	//   呼ぶ。SetPrintMarks は常時表示の画面不透明度を現在の Hold/25-75 選択から再計算するため、
	//   先に Hold to Hide Marks を復元しておく必要がある。
	InterfacePtr<IKESCMCompareFacade> compare(Utils<IKESCMCompareFacade>().QueryUtilInterface());
	compare->SetHoldToHideMarks   (KESCMJsonReadBool(text, "holdToHideMarks", compare->GetHoldToHideMarks()));
	compare->SetShowSourceMarks   (KESCMJsonReadBool(text, "showSrcMarks",    compare->GetShowSourceMarks()));
	compare->SetShowOldPageNumbers(KESCMJsonReadBool(text, "showOldNumbers",  compare->GetShowOldPageNumbers()));

	const bool16 printMarks = KESCMJsonReadBool(text, "printMarks", compare->GetPrintMarks());
	const bool16 opacity25  = KESCMJsonReadBool(text, "opacity25",  compare->GetMarkOpacity25());
	compare->SetPrintMarks(printMarks, opacity25, nil);	// db=nil: フラグ設定のみ(未 Start なので再描画対象は無い)

	KESCMSetLayoutSync            (KESCMJsonReadBool(text, "syncLayoutViews",         KESCMGetLayoutSync()));
	KESCMSetScrollMapEnabled      (KESCMJsonReadBool(text, "scrollbarMap",           KESCMGetScrollMapEnabled()));
	compare->SetIgnorePageNumberMarker(
		KESCMJsonReadBool(text, "ignorePageNumberMarker", compare->GetIgnorePageNumberMarker()));

	// ★ここでは窓に触らない(触れない): この復元は起動時(KESCMUIStartup::Startup)に走るので、
	//   まだパネルが存在しない。実際に半透明を貼るのはパネルの AutoAttach と
	//   kPaletteVisibilityChangedMessage の購読(KESCMPanelAlpha.cpp)。
	//   ★ただし「フラグを戻すだけ」ではない: ON を復元すると KESCMSetPanelTranslucent が Win32 の
	//     イベントフックを張る(置き場所だけが変わる遷移を拾う唯一の手段)。パネルがまだ無い間は
	//     コールバックが即 return するので、起動シーケンスへの影響は無い。
	KESCMSetPanelTranslucent      (KESCMJsonReadBool(text, "translucentPanel",       KESCMGetPanelTranslucent()));
	KESCMSetPagesPanelTranslucent (KESCMJsonReadBool(text, "translucentPagesPanel",  KESCMGetPagesPanelTranslucent()));
	// ★ダイアログの分(2026-08-13)。上の但し書きがそのまま当てはまり、しかも**より素直**: あちらは
	//   「パネルがまだ無い」だが、こちらは「ダイアログはそもそも開いていない」のが常態で、窓は開くたびに
	//   KESCMBookDialog.cpp が教えてくる。ここで戻すのは旗だけでよい。
	KESCMSetBookDialogTranslucent (KESCMJsonReadBool(text, "translucentBookDialog",  KESCMGetBookDialogTranslucent()));

	// ★★比較モード（2026-08-21・ユーザー指定）。
	//   ⚠**ここで SetCompareMode を呼ぶのは安全**＝あれは「設定を変えるだけで、走っている比較を
	//     やり直さない」と契約に明記されている（IKESCMCompareFacade.h）。再比較するかを決めるのは
	//     呼び手で、フライアウトは再比較し、**起動時の復元はしない**。まさにこの場所のための分岐。
	//   ⚠キーが無ければ今の値のまま（＝旧い設定ファイルは Pixel のまま）。知らない綴りも同じ扱いに
	//     する ---- 将来モードが増えた版で保存したファイルを古い版で読んだとき、「知らないから
	//     Pixel にする」より「触らない」ほうが壊れない。
	const std::string mode = KESCMJsonReadString(text, "compareMode");
	if (mode == "story")
		compare->SetCompareMode(kKESCMModeStory);
	else if (mode == "pixel")
		compare->SetCompareMode(kKESCMModePixel);

	// ★タブの名前も復元後の状態に合わせる。起動時（KESCMUIStartup::Startup）から呼ばれた回は
	//   パネルがまだ無いので中で黙って戻り、実際に書かれるのはパネルの AutoAttach ---- そちらも
	//   同じ関数を呼ぶ。ここに置くのは「パネルが既にある状態でこの関数が走った回」のため。
	KESCMPanelTitle::Update();
	// (「translucentToolbox」= ツールボックスの半透明は 2026-08-07 に機能ごと撤去。古い設定ファイルに
	//  このキーが残っていても、読まなくなっただけで害は無い＝KESCMJsonReadBool はキーを名指しで探す。)
}

// KESCMPanelState.cpp 終わり。
