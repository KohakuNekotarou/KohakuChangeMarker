//========================================================================================
//
//  KESCMChangedPagesTSV.cpp
//
//  「Export Changed Pages...」(パネルのフライアウト)の実体。現在の比較(Start 後)の変更ページ
//  一覧を、KESCL の KESCLReportSave(KESCLReportPanel::SaveReportAsText)と同じ流儀のタブ区切り
//  テキストで保存する。2列 = Page / Type(Changed / Inserted / Deleted)。ラベルは全て英語で統一
//  (ユーザー指定 2026-07-25)。Page はそのページ自身の表示名(変更・挿入=Target / 削除=Source)。
//
//  ・ページ番号は IPageList::GetPageString(セクション込み・表示番号。KESCMPageNumberMarker と
//    同じ呼び方)で「画面に見えている番号」を取る。
//  ・出力は UTF-8 + BOM + CRLF(KESCL と同一)。日本語ページ名でも Excel/メモ帳が化けない。
//  ・成功時は無言、失敗のみステータス行。未 Start / 変更ゼロは短くステータス行に出して戻る。
//  ・日本語リテラルは \uXXXX エスケープの wide 文字列で持つ(ソースは純 ASCII=BOM 不要・
//    CP932 誤読の心配なし)。
//  ・★オーバーセット(sOverset*)は一切参照しない(ユーザー指定 2026-07-24)。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IPMStream.h"
#include "IPageList.h"			// GetPageString(表示ページ番号)
#include "IApplication.h"
#include "IDocumentList.h"		// FindDocByDataBase(閉じた db を deref しないための生存確認 / 文書名)
#include "IDocument.h"			// GetName(suggested filename)
#include "ISession.h"			// GetExecutionContextSession
#include "IDataBase.h"			// GetRootUID

// General includes:
#include "PMString.h"
#include "StreamUtil.h"
#include "SDKFileHelper.h"		// SDKFileSaveChooser (sdksamples/common)

#include <string>
#include <vector>
#include <set>
#include <map>

// Project includes:
#include "KESCMID.h"
#include "KESCMCore.h"				// KESCMSetStatus / KESCMCollectPageUIDs
#include "KESCMDrawEventHandler.h"	// sEntries / sDB / sSrcDB
#include "KESCMPageMap.h"			// KESCMBuildPairing / KESCMPageMapCollectRegistered
#include "KESCMChangedPagesTSV.h"

namespace
{

// 出力は2列(Page / Type)。ラベルは全て英語で統一(ユーザー指定 2026-07-25。KESCM の他 UI と同じ)。
// Type の値 = Changed / Inserted / Deleted。旧ページ列は持たない(ページ名はそのページ自身の
// 文書での表示名: 変更・挿入=Target 側 / 削除=Source 側)。
const wchar_t* const kKindChanged  = L"Changed";
const wchar_t* const kKindInserted = L"Inserted";
const wchar_t* const kKindDeleted  = L"Deleted";
const wchar_t* const kHeaderLine   = L"Page\tType\n";

// 1行分(ページ表示名 + 種別)。
struct KESCMChangeRow
{
	std::wstring page;
	std::wstring kind;
};

// ステータス行(KESCL/KESCM 共通の非翻訳・英語の流儀)。
void ShowStatus(const char* text)
{
	PMString s(text);
	s.SetTranslatable(kFalse);
	KESCMSetStatus(s);
}

// pageUID(db 内)の「画面に見えている表示ページ番号」を wide 文字列で返す。取れなければ空。
// 呼び方は KESCMPageNumberMarker の GetPageString と同一(セクション込み・番号スタイルそのまま・
// 隠しスプレッドを飛ばした表示番号)。
std::wstring PageDisplay(IDataBase* db, UID pageUID)
{
	std::wstring out;
	if (db == nil || pageUID == kInvalidUID)
		return out;
	InterfacePtr<IPageList> pageList(db, db->GetRootUID(), UseDefaultIID());
	if (pageList == nil)
		return out;
	PMString s;
	pageList->GetPageString(pageUID, &s, kTrue, kFalse, kDefaultPageType, kTrue, kFalse);
	const int32 n = s.NumUTF16TextChars();
	if (n <= 0)
		return out;
	const UTF16TextChar* buf = s.GrabUTF16Buffer(nil);
	if (buf == nil)
		return out;
	out.assign(reinterpret_cast<const wchar_t*>(buf), n);
	return out;
}

// Windows のファイル名に使えない9文字を '-' に(KESCL の SanitizeForFileName と同じ)。
std::wstring SanitizeForFileName(const std::wstring& part)
{
	std::wstring out(part);
	for (size_t i = 0; i < out.size(); ++i)
	{
		const wchar_t c = out[i];
		if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?'
			|| c == L'"' || c == L'<' || c == L'>' || c == L'|')
		{
			out[i] = L'-';
		}
	}
	return out;
}

// db を所有する文書の表示名(拡張子は残す。suggested filename 用)。取れなければ空。
// KESCMPanelObserver::KESCMDocNameFromDB と同じ解決経路(セッション→app→docList→FindDocByDataBase)。
std::wstring DocNameFromDB(IDataBase* db)
{
	std::wstring out;
	if (db == nil)
		return out;
	InterfacePtr<IApplication> app(GetExecutionContextSession() ? GetExecutionContextSession()->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app != nil ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return out;
	IDocument* d = docList->FindDocByDataBase(db);	// no ref(deref せず名前を取るだけ)
	if (d == nil)
		return out;
	PMString name;
	d->GetName(name);
	const int32 n = name.NumUTF16TextChars();
	if (n <= 0)
		return out;
	const UTF16TextChar* buf = name.GrabUTF16Buffer(nil);
	if (buf != nil)
		out.assign(reinterpret_cast<const wchar_t*>(buf), n);
	return out;
}

// suggested filename: "KohakuChangeMarker_ChangedPages_<targetDoc>.txt"。target 名が取れなければ
// "KohakuChangeMarker_ChangedPages.txt"。拡張子(.indd 等)は落とす。
PMString BuildSuggestedFileName(IDataBase* targetDB)
{
	std::wstring stem(L"KohakuChangeMarker_ChangedPages");
	std::wstring doc = DocNameFromDB(targetDB);
	const size_t dot = doc.find_last_of(L'.');
	if (dot != std::wstring::npos && dot > 0)
		doc.erase(dot);
	if (!doc.empty())
	{
		stem += L'_';
		stem += SanitizeForFileName(doc);
	}
	stem += L".txt";

	PMString result;
	result.SetTranslatable(kFalse);
	result.AppendW(reinterpret_cast<const UTF16TextChar*>(stem.c_str()));
	return result;
}

// 変更ページ行を集める。行が1つでもあれば kTrue。順序: Target をドキュメント順(変更+挿入)、
// 続けて Source をドキュメント順(削除)。
bool16 CollectRows(IDataBase* targetDB, IDataBase* sourceDB, std::vector<KESCMChangeRow>& rows)
{
	// 挿入/削除の分類に使う「あふれ集合」を取得(対応ペアリング自体は旧ページ列を廃したので不要)。
	std::vector<UID> tPairs, sPairs, tOverflow, sOverflow;
	KESCMBuildPairing(targetDB, sourceDB, tPairs, sPairs, &tOverflow, &sOverflow);

	std::set<UID> overflowT(tOverflow.begin(), tOverflow.end());
	std::set<UID> overflowS(sOverflow.begin(), sOverflow.end());

	// 手動登録(Added=Target 側 / Removed=Source 側)。
	std::set<UID> registeredT, registeredS;
	KESCMPageMapCollectRegistered(targetDB, registeredT);
	KESCMPageMapCollectRegistered(sourceDB, registeredS);

	// ---- Target をドキュメント順に: 変更 → 挿入 ----
	std::vector<UID> targetOrder;
	KESCMCollectPageUIDs(targetDB, targetOrder);
	for (size_t i = 0; i < targetOrder.size(); ++i)
	{
		const UID t = targetOrder[i];
		if (KESCMDrawEventHandler::sEntries.count(t) > 0)
		{
			KESCMChangeRow row;
			row.page = PageDisplay(targetDB, t);
			row.kind = kKindChanged;
			rows.push_back(row);
		}
		else if (registeredT.count(t) > 0 || overflowT.count(t) > 0)
		{
			KESCMChangeRow row;
			row.page = PageDisplay(targetDB, t);
			row.kind = kKindInserted;
			rows.push_back(row);
		}
	}

	// ---- Source をドキュメント順に: 削除(相手なしの Source ページ) ----
	// 変更ページの相手(pairTargetToSource の値)は登録済みでも overflow でもないので、ここには来ない
	// =二重計上しない。
	std::vector<UID> sourceOrder;
	KESCMCollectPageUIDs(sourceDB, sourceOrder);
	for (size_t i = 0; i < sourceOrder.size(); ++i)
	{
		const UID s = sourceOrder[i];
		if (registeredS.count(s) > 0 || overflowS.count(s) > 0)
		{
			KESCMChangeRow row;
			row.page = PageDisplay(sourceDB, s);
			row.kind = kKindDeleted;
			rows.push_back(row);
		}
	}

	return !rows.empty();
}

// 集めた行を1本の wide テキスト(ヘッダー + 各行、行末は '\n')に組む。
std::wstring BuildReportText(const std::vector<KESCMChangeRow>& rows)
{
	std::wstring text(kHeaderLine);
	for (size_t i = 0; i < rows.size(); ++i)
	{
		text += rows[i].page;
		text += L'\t';
		text += rows[i].kind;
		text += L'\n';
	}
	return text;
}

} // anonymous namespace

//========================================================================================
// KESCMExportChangedPagesTSV(KESCMChangedPagesTSV.h で宣言)
//========================================================================================
void KESCMExportChangedPagesTSV()
{
	IDataBase* targetDB = KESCMDrawEventHandler::sDB;
	IDataBase* sourceDB = KESCMDrawEventHandler::sSrcDB;
	if (targetDB == nil || sourceDB == nil)
	{
		// フライアウト項目は比較中(sDB≠nil)のみ有効化しているので、通常ここへは来ない(belt and braces)。
		ShowStatus("Start a comparison first.");
		return;
	}

	std::vector<KESCMChangeRow> rows;
	if (!CollectRows(targetDB, sourceDB, rows))
	{
		ShowStatus("No changed pages to export.");
		return;
	}

	// 保存先(sdksamples/common の共通チューザ)。タイトル/初期名は KESCM の他 UI と同じく英語固定。
	// 'TEXT'/'CWIE' は SDK のテキスト書き出しが渡す古典的な Mac type/creator(Windows では無意味だが API が要求)。
	SDKFileSaveChooser chooser;
	PMString title("Export Changed Pages");
	title.SetTranslatable(kFalse);
	chooser.SetTitle(title);
	chooser.SetFilename(BuildSuggestedFileName(targetDB));
	PMString filterName("Text file(txt)");
	filterName.SetTranslatable(kFalse);
	chooser.AddFilter('CWIE', 'TEXT', "txt", filterName);
	chooser.ShowDialog();
	if (!chooser.IsChosen())
		return;	// キャンセルは無音

	// wide テキスト → PMString → UTF-8。
	const std::wstring wtext = BuildReportText(rows);
	PMString report;
	report.SetTranslatable(kFalse);
	report.AppendW(reinterpret_cast<const UTF16TextChar*>(wtext.c_str()));

	// UTF-8 + BOM、'\n' -> '\r\n'(KESCL と同一。BOM 無しの日本語テキストは Excel/メモ帳が推測を誤る)。
	const std::string utf8 = report.GetUTF8String();
	std::string bytes;
	bytes.reserve(utf8.size() + utf8.size() / 8 + 3);
	bytes += "\xEF\xBB\xBF";
	for (std::string::const_iterator it = utf8.begin(); it != utf8.end(); ++it)
	{
		if (*it == '\n')
			bytes += "\r\n";
		else
			bytes += *it;
	}

	InterfacePtr<IPMStream> stream(StreamUtil::CreateFileStreamWrite(
		chooser.GetIDFile(), kOpenOut | kOpenTrunc, 'TEXT', 'CWIE'));
	if (stream == nil)
	{
		ShowStatus("Could not create the file. Is the folder writable?");
		return;
	}
	stream->XferByte(reinterpret_cast<uchar*>(&bytes[0]), static_cast<int32>(bytes.size()));
	// 状態は Flush の後で読む(KESCL と同じ: XferByte はバッファのみのことがあり、書き込み失敗が
	// Flush で初めて表面化し得るため。前に読むと「保存できた」と誤報告する)。
	stream->Flush();
	const bool failed = (stream->GetStreamState() == kStreamStateFailure);
	stream->Close();

	// 成功時は無言(ファイルが指定場所にできる=ステータス行に足すことは無い)。失敗のみ出す(KESCL の流儀)。
	if (failed)
		ShowStatus("Could not write the file.");
}

// End, KESCMChangedPagesTSV.cpp.
