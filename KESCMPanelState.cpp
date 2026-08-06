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
#include "KESCMCore.h"				// KESCMGetPrintMarks / KESCMGetMarkOpacity25 / KESCMDoSetPrintMarks /
									// KESCMGetLayoutSync / KESCMSetLayoutSync
#include "KESCMDrawEventHandler.h"	// sAlwaysShowMarks / sSrcMarksOn / sShowOldNumbers(公開 static)
#include "KESCMScrollMap.h"			// KESCMGetScrollMapEnabled / KESCMSetScrollMapEnabled
#include "KESCMPageNumberMarker.h"	// KESCMGetIgnorePageNumberMarker / KESCMSetIgnorePageNumberMarker
#include "KESCMPanelAlpha.h"		// KESCMGetPanelTranslucent / KESCMSetPanelTranslucent(Translucent Panel)

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
	std::string json;
	json += "{\n";
	json += "  \"version\": 1,\n";
	json += "  \"printMarks\": ";             json += KESCMBoolLiteral(KESCMGetPrintMarks());                       json += ",\n";
	json += "  \"opacity25\": ";              json += KESCMBoolLiteral(KESCMGetMarkOpacity25());                    json += ",\n";
	json += "  \"holdToHideMarks\": ";        json += KESCMBoolLiteral(KESCMDrawEventHandler::sAlwaysShowMarks);    json += ",\n";
	json += "  \"showSrcMarks\": ";           json += KESCMBoolLiteral(KESCMDrawEventHandler::sSrcMarksOn);         json += ",\n";
	json += "  \"showOldNumbers\": ";         json += KESCMBoolLiteral(KESCMDrawEventHandler::sShowOldNumbers);     json += ",\n";
	json += "  \"syncLayoutViews\": ";        json += KESCMBoolLiteral(KESCMGetLayoutSync());                       json += ",\n";
	json += "  \"scrollbarMap\": ";           json += KESCMBoolLiteral(KESCMGetScrollMapEnabled());                 json += ",\n";
	json += "  \"ignorePageNumberMarker\": "; json += KESCMBoolLiteral(KESCMGetIgnorePageNumberMarker());           json += ",\n";
	json += "  \"translucentPanel\": ";       json += KESCMBoolLiteral(KESCMGetPanelTranslucent());                 json += ",\n";
	json += "  \"translucentPagesPanel\": ";  json += KESCMBoolLiteral(KESCMGetPagesPanelTranslucent());            json += "\n";
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
	// ★パスのみ(「Settings saved:」等のラベルを付けるとステータス行(幅152px×4行)から溢れるため)。
	PMString msg;
	msg.SetTranslatable(kFalse);
	msg.Append(FileUtils::SysFileToPMString(file));
	KESCMSetStatus(msg, kTrue /*forceRedrawNow*/);
}

//----------------------------------------------------------------------------------------
// 復元(起動時=KESCMPeekStartup::Startup から呼ばれる。セッション内一度だけ。
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
	// ★順序: 不透明度に影響する平の static(Hold to Hide Marks)を先に反映してから KESCMDoSetPrintMarks を
	//   呼ぶ。KESCMDoSetPrintMarks は常時表示の画面不透明度(sMarkScreenOpacity)を現在の Hold/25-75 選択から
	//   再計算するため、先に sAlwaysShowMarks を復元しておく必要がある。
	KESCMDrawEventHandler::sAlwaysShowMarks = KESCMJsonReadBool(text, "holdToHideMarks", KESCMDrawEventHandler::sAlwaysShowMarks);
	KESCMDrawEventHandler::sSrcMarksOn      = KESCMJsonReadBool(text, "showSrcMarks",    KESCMDrawEventHandler::sSrcMarksOn);
	KESCMDrawEventHandler::sShowOldNumbers  = KESCMJsonReadBool(text, "showOldNumbers",  KESCMDrawEventHandler::sShowOldNumbers);

	const bool16 printMarks = KESCMJsonReadBool(text, "printMarks", KESCMGetPrintMarks());
	const bool16 opacity25  = KESCMJsonReadBool(text, "opacity25",  KESCMGetMarkOpacity25());
	KESCMDoSetPrintMarks(printMarks, opacity25, nil);	// db=nil: フラグ設定のみ(未 Start なので再描画対象は無い)

	KESCMSetLayoutSync            (KESCMJsonReadBool(text, "syncLayoutViews",         KESCMGetLayoutSync()));
	KESCMSetScrollMapEnabled      (KESCMJsonReadBool(text, "scrollbarMap",           KESCMGetScrollMapEnabled()));
	KESCMSetIgnorePageNumberMarker(KESCMJsonReadBool(text, "ignorePageNumberMarker", KESCMGetIgnorePageNumberMarker()));

	// ★ここでは窓に触らない(触れない): この復元は起動時(KESCMPeekStartup::Startup)に走るので、
	//   まだパネルが存在しない。実際に半透明を貼るのはパネルの AutoAttach と
	//   kPaletteVisibilityChangedMessage の購読(KESCMPanelAlpha.cpp)。
	//   ★ただし「フラグを戻すだけ」ではない: ON を復元すると KESCMSetPanelTranslucent が Win32 の
	//     イベントフックを張る(置き場所だけが変わる遷移を拾う唯一の手段)。パネルがまだ無い間は
	//     コールバックが即 return するので、起動シーケンスへの影響は無い。
	KESCMSetPanelTranslucent      (KESCMJsonReadBool(text, "translucentPanel",       KESCMGetPanelTranslucent()));
	KESCMSetPagesPanelTranslucent (KESCMJsonReadBool(text, "translucentPagesPanel",  KESCMGetPagesPanelTranslucent()));
}

// KESCMPanelState.cpp 終わり。
