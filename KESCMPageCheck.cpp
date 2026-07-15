//========================================================================================
//
//  KESCMPageCheck.cpp
//
//  「KESCM: Check」機能(KESCMPageCheck.h 参照)。ページパネルでページを選択→右クリックの
//  トグル「KESCM: Check」で、そのページに「チェック済み」印を付け外しする。チェックしたページには
//  Pages パネルのサムネイル中央に青い ✓(ベクター線)を描く(描画は KESCMDrawEventHandler の
//  isThumb 分岐)。登録(KESCMPageMap)とは独立した別集合。セッション内のみ・Stop で全消去。
//
//  構造は KESCMPageMap.cpp を踏襲(選択取得=共通リーダー KESCMPageMapReadSelection、状態=
//  文書DBごとの UID セット、クローズスイープは deref なしのポインタ比較のみ)。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "ISession.h"
#include "IApplication.h"
#include "IDocument.h"
#include "IDataBase.h"			// GetSysFile(保存キー=文書ファイルパス)
#include "IDocumentList.h"		// 生存スイープ(FindDocByDataBase へのポインタ比較のみ)
#include "IActionStateList.h"	// メニューの有効/チェック
#include "Utils.h"
#include "PersistUtils.h"		// ::GetDataBase(IDocument→IDataBase)
#include "PMString.h"
#include "FileUtils.h"			// GetAppRoamingDataFolder / AppendPath / OpenFile / DoesFileExist / SysFileToPMString
#include "IDFile.h"

#include <map>
#include <set>
#include <vector>
#include <string>
#include <cstdio>				// FILE / fread / fwrite / fclose

#include "KESCMCore.h"			// KESCMCollectPageUIDs / KESCMIsArmed / KESCMArmedTargetDB / KESCMArmedSourceDB / KESCMSetStatus / KESCMDoMarkChangesDoc
#include "KESCMPageCheck.h"
#include "KESCMPageMap.h"		// KESCMPageMapCollectRegistered(保存) / KESCMPageMapReplaceRegistered(読込)
#include "KESCMThumbnailRefresh.h"	// KESCMRefreshThumbnailsForPages(トグルページの明示サムネイル更新)

// チェック済みページ: 文書DB → ページUIDの集合。セッション内のみ。空になった文書のエントリは即消す。
static std::map<IDataBase*, std::set<UID> > sChecked;

// db の「マーク付きページ」集合を1回だけ作る。マーク付き = KESCM の変更リング(sEntries)/登録「/」/
// overflow「/」のいずれか。実体は KESCMCollectChangedPageUIDs(KESCMThumbnailRefresh.h。db が sDB/sSrcDB の
// ときだけ true+集合を返す)。★複数ページを判定するときは、これを1回呼んでから outMarked.count() で引くこと
// (KESCMCollectChangedPageUIDs は毎回 sEntries を全走査するため、ページごとに呼ぶと O(ページ数×変更数)になる)。
static bool16 KESCMCollectMarked(IDataBase* db, std::set<UID>& outMarked)
{
	outMarked.clear();
	return KESCMCollectChangedPageUIDs(db, outMarked);
}

// ★チェック(✓)は「マーク付きページ限定」にする(ユーザー指定 2026-07-11): マークの無いページは
//   サムネイルが作り直されず✓が乗らないため、そもそもチェックさせない/覚えない。判定は上の
//   KESCMCollectMarked で集合を作り count() で引く(ページごとに引き直さない)。

// 選択ページのうち「マーク付き」のものだけを outMarked に残す。マーク集合は1回だけ作る(上記参照)。
static void KESCMFilterToMarked(IDataBase* db, const std::vector<UID>& pages, std::vector<UID>& outMarked)
{
	outMarked.clear();
	std::set<UID> marked;
	if (!KESCMCollectMarked(db, marked))
		return;		// db が比較対象でない=マーク無し
	for (size_t i = 0; i < pages.size(); ++i)
		if (marked.count(pages[i]) > 0)
			outMarked.push_back(pages[i]);
}

//========================================================================================
// KESCMPageCheckToggleSelectedPages(KESCMPageCheck.h で宣言)
//========================================================================================
void KESCMPageCheckToggleSelectedPages()
{
	IDataBase* db = nil;
	std::vector<UID> selPages;
	if (!KESCMPageMapReadSelection(db, selPages))
		return;		// メニューは kCustomEnabling で無効化済みのはずだが保険

	// チェックは「比較を Start 中(arm 済み)」かつ「選択文書が Target/Source」のときだけ可能。
	if (!KESCMIsArmed() || (db != KESCMArmedTargetDB() && db != KESCMArmedSourceDB()))
		return;

	// ★マーク付きページだけを対象にする(マークの無いページは✓が乗らないので無視)。
	std::vector<UID> pages;
	KESCMFilterToMarked(db, selPages, pages);
	if (pages.empty())
		return;		// 選択にマーク付きページが無い=何もしない(メニューも無効のはず)

	std::set<UID>& chk = sChecked[db];

	bool16 anyUnchecked = kFalse;
	for (size_t i = 0; i < pages.size(); ++i)
	{
		if (chk.count(pages[i]) == 0)
		{
			anyUnchecked = kTrue;
			break;
		}
	}

	PMString msg;
	msg.SetTranslatable(kFalse);
	if (anyUnchecked)
	{
		for (size_t i = 0; i < pages.size(); ++i)
			chk.insert(pages[i]);
		msg.Append("check +");
		msg.AppendNumber((int32)pages.size());
	}
	else
	{
		for (size_t i = 0; i < pages.size(); ++i)
			chk.erase(pages[i]);
		msg.Append("check -");
		msg.AppendNumber((int32)pages.size());
	}

	// 空になったらエントリごと捨てる。合計はその後に数える。
	if (chk.empty())
		sChecked.erase(db);
	std::map<IDataBase*, std::set<UID> >::const_iterator it = sChecked.find(db);
	msg.Append(", total ");
	msg.AppendNumber(it != sChecked.end() ? (int32)it->second.size() : 0);

	// トグルしたページ(マーク付きに限定済み=サムネイルが確実に作り直される)のサムネイルを即更新して
	// ✓ を反映する(比較には影響しないので再比較は不要)。
	KESCMRefreshThumbnailsForPages(db, pages);

	// ★レイアウトビュー版の ✓(2026-07-12 追加)も即反映する。✓ は常時表示なので、トグルした文書の
	// レイアウトビューを InvalidateViews で再描画しないと、次の再描画機会(スクロール等)まで
	// 付け外しが画面に出ない(ユーザー報告: OFF にしても ✓ が残る)。サムネイル更新とは別経路。
	KESCMInvalidateDB(db);

	KESCMSetStatus(msg);
}

//========================================================================================
// KESCMPageCheckUpdateToggleState(KESCMPageCheck.h で宣言)
//========================================================================================
void KESCMPageCheckUpdateToggleState(IActionStateList* listToUpdate, int32 index)
{
	IDataBase* db = nil;
	std::vector<UID> pages;
	if (!KESCMPageMapReadSelection(db, pages))
	{
		listToUpdate->SetNthActionState(index, kDisabled_Unselected);
		return;
	}

	// Start 中かつ選択文書が Target/Source のときだけ有効。それ以外はグレーアウト。
	if (!KESCMIsArmed() || (db != KESCMArmedTargetDB() && db != KESCMArmedSourceDB()))
	{
		listToUpdate->SetNthActionState(index, kDisabled_Unselected);
		return;
	}

	// ★選択にマーク付き(枠/「/」の付く=サムネイルが作り直される)ページが無ければ無効化する
	//   (枠の無いページでは「チェック」を出さない=ユーザー指定 2026-07-11)。
	std::vector<UID> marked;
	KESCMFilterToMarked(db, pages, marked);
	if (marked.empty())
	{
		listToUpdate->SetNthActionState(index, kDisabled_Unselected);
		return;
	}

	int32 chkCount = 0;
	std::map<IDataBase*, std::set<UID> >::const_iterator it = sChecked.find(db);
	if (it != sChecked.end())
	{
		for (size_t i = 0; i < marked.size(); ++i)
		{
			if (it->second.count(marked[i]) > 0)
				++chkCount;
		}
	}

	int16 state = kEnabledAction;
	if (chkCount == (int32)marked.size())
		state |= kSelectedAction;			// マーク付き選択が全部チェック済み=✓
	else if (chkCount > 0)
		state |= kMultiSelectedAction;		// 一部だけチェック済み=中間チェック
	listToUpdate->SetNthActionState(index, state);
}

//========================================================================================
// KESCMPageCheckSweepClosedDocs(KESCMPageCheck.h で宣言)
//========================================================================================
void KESCMPageCheckSweepClosedDocs()
{
	if (sChecked.empty())
		return;

	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IDocumentList> docList(app != nil ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return;

	std::map<IDataBase*, std::set<UID> >::iterator it = sChecked.begin();
	while (it != sChecked.end())
	{
		if (docList->FindDocByDataBase(it->first) == nil)
			sChecked.erase(it++);	// 閉じた文書: 状態だけ捨てる(deref なし)
		else
			++it;
	}
}

//========================================================================================
// KESCMPageCheckClearAllDocs(KESCMPageCheck.h で宣言)
//========================================================================================
void KESCMPageCheckClearAllDocs()
{
	sChecked.clear();
}

//========================================================================================
// KESCMPageCheckPruneToMarked(KESCMPageCheck.h で宣言)
//   再比較後、各文書のチェックを「今もマーク付き」のページだけに絞る(マークが消えたページの
//   チェックは忘れる)。KESCMCollectChangedPageUIDs は db が sDB/sSrcDB のときだけ現在のマーク集合を
//   返す(それ以外は空=その db の全チェックが外れる)。ポインタは deref しない。
//========================================================================================
void KESCMPageCheckPruneToMarked()
{
	if (sChecked.empty())
		return;
	std::map<IDataBase*, std::set<UID> >::iterator it = sChecked.begin();
	while (it != sChecked.end())
	{
		std::set<UID> marked;
		KESCMCollectChangedPageUIDs(it->first, marked);		// db が比較対象でなければ空
		std::set<UID>& chk = it->second;
		for (std::set<UID>::iterator c = chk.begin(); c != chk.end(); )
		{
			if (marked.count(*c) == 0)
				chk.erase(c++);		// もうマークが無いページ=チェックを忘れる
			else
				++c;
		}
		if (chk.empty())
			sChecked.erase(it++);
		else
			++it;
	}
}

//========================================================================================
// KESCMPageCheckIsChecked(KESCMPageCheck.h で宣言)
//========================================================================================
bool16 KESCMPageCheckIsChecked(IDataBase* db, UID pageUID)
{
	if (db == nil)
		return kFalse;
	std::map<IDataBase*, std::set<UID> >::const_iterator it = sChecked.find(db);
	return (it != sChecked.end() && it->second.count(pageUID) > 0) ? kTrue : kFalse;
}

//========================================================================================
// KESCMPageCheckHasAny(KESCMPageCheck.h で宣言)
//========================================================================================
bool16 KESCMPageCheckHasAny(IDataBase* db)
{
	if (db == nil)
		return kFalse;
	std::map<IDataBase*, std::set<UID> >::const_iterator it = sChecked.find(db);
	return (it != sChecked.end() && !it->second.empty()) ? kTrue : kFalse;
}

//========================================================================================
// チェック/登録状態の保存/読み込み(フライアウト「Save Check & Register」「Load Check & Register」)
//
//   パネル設定(KESCMPanelState.cpp)と同じローミング環境設定フォルダー直下に、独自 JSON ファイル
//   KESCMPageChecks.json として保存する(サブフォルダーは作らない)。InDesign 本体のデータには一切書かない。
//   キー = 文書ファイルのフルパス(GetSysFile→SysFileToPMString。★ファイル名のみ案を検討したが、Target と
//     Source が同名だとキー衝突するためフルパスへ戻した=2026-07-12。保存済み文書内でページ UID は永続なので
//     再オープンしても一致する。トレードオフ: 文書を移動/別名保存してパスが変わると一致しない)。
//   値 = チェック済み(✓)ページUID + 登録済み(Added/Removed=緑「/」)ページUID の2集合。パスは UTF-8 で
//   格納(日本語パス/Shift-JIS の 0x5C 問題を回避)。
//
//   形式(version 2):
//     {
//       "version": 2,
//       "docs": [
//         { "path": "<utf8 path>", "checks": [12, 45], "registered": [3, 7] }
//       ]
//     }
//   ★読み込みは寛容: version は見ない。旧 v1 の "pages" 配列は "checks" として受理する
//     (registered が無い旧ファイルは登録なしとして扱う)。
//========================================================================================

// 1文書ぶんの保存単位: チェック済み(✓)と登録済み(Added/Removed)の生 UID(uint32)集合。
struct KESCMDocSets
{
	std::set<uint32> checks;
	std::set<uint32> registered;
};

static const char* const kKESCMPageChecksFileName = "KESCMPageChecks.json";

// ローミング環境設定フォルダー(locale 付き)直下の KESCMPageChecks.json への IDFile を返す。
// ★サブフォルダーは作らない(ユーザー指定 2026-07-12)。GetAppRoamingDataFolder の subFolderName に
//   ファイル名をそのまま渡すと、そのフォルダー直下の「ファイルの」IDFile が返る(SDK 実例:
//   SnpShareAppResources.cpp / SuppUISysFileData.cpp)。親フォルダーは InDesign が環境設定用に既に
//   作っているので CreateFolderIfNeeded は不要。取得できなければ kFalse。
static bool16 KESCMPageChecksFile(IDFile& outFile)
{
	return FileUtils::GetAppRoamingDataFolder(&outFile, PMString(kKESCMPageChecksFileName));
}

// UTF-8 文字列を JSON 文字列リテラル用にエスケープ(\\ と \" と制御文字)。UTF-8 なので継続バイトに
// 0x5C/0x22 は現れず、バイト単位の走査で安全。
static void KESCMJsonEscape(const std::string& in, std::string& out)
{
	out.clear();
	out.reserve(in.size() + 8);
	for (size_t i = 0; i < in.size(); ++i)
	{
		const char c = in[i];
		switch (c)
		{
			case '\\': out += "\\\\"; break;
			case '\"': out += "\\\""; break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			case '\t': out += "\\t";  break;
			default:   out += c;      break;
		}
	}
}

// text[pos] が開き '"' を指している前提で、閉じ '"' までを非エスケープして out へ返す。pos は閉じ '"' の
// 次へ進める。開始が '"' でなければ kFalse。
static bool16 KESCMJsonReadString(const std::string& text, size_t& pos, std::string& out)
{
	out.clear();
	if (pos >= text.size() || text[pos] != '\"')
		return kFalse;
	++pos;	// 開き " をスキップ
	while (pos < text.size())
	{
		const char c = text[pos++];
		if (c == '\"')
			return kTrue;	// 閉じ "
		if (c == '\\' && pos < text.size())
		{
			const char e = text[pos++];
			switch (e)
			{
				case 'n':  out += '\n'; break;
				case 'r':  out += '\r'; break;
				case 't':  out += '\t'; break;
				case '\\': out += '\\'; break;
				case '\"': out += '\"'; break;
				case '/':  out += '/';  break;
				default:   out += e;    break;	// 未知エスケープはそのまま
			}
		}
		else
		{
			out += c;
		}
	}
	return kFalse;	// 閉じ " が無い=壊れ
}

// ファイル全体を std::string に読む。無ければ/開けなければ空で kFalse。
static bool16 KESCMReadWholeFile(const IDFile& file, std::string& outText)
{
	outText.clear();
	if (!FileUtils::DoesFileExist(file))
		return kFalse;
	FILE* fp = FileUtils::OpenFile(file, "rb");
	if (fp == nil)
		return kFalse;
	char buf[2048];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
		outText.append(buf, n);
	fclose(fp);
	return kTrue;
}

// [regionBegin, regionEnd) の範囲内で key(例 "\"checks\"")の直後の [ ... ] を探し、中の符号なし整数を
// out に拾う。key が region 内に見つかれば(配列が空でも)kTrue、無ければ kFalse。region 境界を跨がないよう
// 見つけた '[' / ']' が regionEnd を越えるものは無効扱い。
static bool16 KESCMParseUintArray(const std::string& text, size_t regionBegin, size_t regionEnd,
	const char* key, std::set<uint32>& out)
{
	out.clear();
	const std::string k(key);
	const size_t kk = text.find(k, regionBegin);
	if (kk == std::string::npos || kk >= regionEnd)
		return kFalse;
	const size_t lb = text.find('[', kk + k.size());
	if (lb == std::string::npos || lb >= regionEnd)
		return kFalse;
	const size_t rb = text.find(']', lb + 1);
	if (rb == std::string::npos || rb > regionEnd)
		return kFalse;

	size_t r = lb + 1;
	while (r < rb)
	{
		while (r < rb && (text[r] < '0' || text[r] > '9'))
			++r;
		if (r >= rb)
			break;
		uint32 val = 0;
		bool16 any = kFalse;
		while (r < rb && text[r] >= '0' && text[r] <= '9')
		{
			val = val * 10u + (uint32)(text[r] - '0');
			any = kTrue;
			++r;
		}
		if (any)
			out.insert(val);
	}
	return kTrue;	// key は在った(配列が空でも成功)
}

// KESCMPageChecks.json を読み、パス(UTF-8)→(checks/registered)集合に展開する。無ければ空 map で kFalse。
// 寛容パース: "path" を順に探し、その doc の領域([この "path" 位置, 次の "path" 位置))内で "registered" と
// "checks" を読む。"checks" が無い旧 v1 ファイルは "pages" を checks として受理する。
static bool16 KESCMReadSetsMap(std::map<std::string, KESCMDocSets>& out)
{
	out.clear();

	IDFile file;
	if (!KESCMPageChecksFile(file))
		return kFalse;
	std::string text;
	if (!KESCMReadWholeFile(file, text) || text.empty())
		return kFalse;

	const std::string kPathKey = "\"path\"";
	size_t p = 0;
	while (true)
	{
		const size_t kpath = text.find(kPathKey, p);
		if (kpath == std::string::npos)
			break;

		// この doc の領域末尾 = 次の "path"(無ければ末尾)。配列探索がこの境界を跨がないようにする。
		const size_t next = text.find(kPathKey, kpath + kPathKey.size());
		const size_t regionEnd = (next == std::string::npos) ? text.size() : next;

		// "path" の後の ':' → 開き '"' → 文字列本体
		size_t q = text.find(':', kpath + kPathKey.size());
		if (q == std::string::npos || q >= regionEnd)
			break;
		++q;
		while (q < text.size() && (text[q] == ' ' || text[q] == '\t' || text[q] == '\n' || text[q] == '\r'))
			++q;
		std::string pathStr;
		if (!KESCMJsonReadString(text, q, pathStr))
			break;

		KESCMDocSets sets;
		KESCMParseUintArray(text, q, regionEnd, "\"registered\"", sets.registered);
		// v2 の "checks"。無ければ旧 v1 の "pages" を checks とみなす。
		if (!KESCMParseUintArray(text, q, regionEnd, "\"checks\"", sets.checks))
			KESCMParseUintArray(text, q, regionEnd, "\"pages\"", sets.checks);

		if (!sets.checks.empty() || !sets.registered.empty())
			out[pathStr] = sets;

		p = regionEnd;	// 次の doc を探す
	}

	return !out.empty();
}

// uint32 集合を "1, 2, 3" 形式で json に追記する(角括弧は呼び出し側)。
static void KESCMAppendUintList(std::string& json, const std::set<uint32>& s)
{
	bool16 first = kTrue;
	for (std::set<uint32>::const_iterator u = s.begin(); u != s.end(); ++u)
	{
		if (!first)
			json += ", ";
		first = kFalse;
		char num[16];
		std::snprintf(num, sizeof(num), "%lu", (unsigned long)(*u));	// 境界チェック付き(sprintf を避け静的解析の指摘を回避)
		json += num;
	}
}

// パス(UTF-8)→(checks/registered)集合を KESCMPageChecks.json(version 2)へ書く。書けた IDFile を outFile に返す。
static bool16 KESCMWriteSetsMap(const std::map<std::string, KESCMDocSets>& in, IDFile& outFile)
{
	if (!KESCMPageChecksFile(outFile))
		return kFalse;

	std::string json;
	json += "{\n";
	json += "  \"version\": 2,\n";
	json += "  \"docs\": [\n";
	bool16 firstDoc = kTrue;
	for (std::map<std::string, KESCMDocSets>::const_iterator d = in.begin(); d != in.end(); ++d)
	{
		if (d->second.checks.empty() && d->second.registered.empty())
			continue;	// 空エントリは書かない
		if (!firstDoc)
			json += ",\n";
		firstDoc = kFalse;

		std::string esc;
		KESCMJsonEscape(d->first, esc);
		json += "    { \"path\": \"";
		json += esc;
		json += "\", \"checks\": [";
		KESCMAppendUintList(json, d->second.checks);
		json += "], \"registered\": [";
		KESCMAppendUintList(json, d->second.registered);
		json += "] }";
	}
	json += "\n  ]\n}\n";

	FILE* fp = FileUtils::OpenFile(outFile, "wb");
	if (fp == nil)
		return kFalse;
	fwrite(json.data(), 1, json.size(), fp);
	fclose(fp);
	return kTrue;
}

// db の文書ファイルパス(フルパス)を UTF-8 で返す(保存/読込の判別キー)。未保存(パス無し)なら kFalse。
// ★フルパスを使う: Target と Source が同じファイル名でも(別フォルダーにある新旧版など)キーが衝突しない
//   ようにするため(2026-07-12: ファイル名のみ案を検討したが同名衝突を避けフルパスへ戻した)。
//   トレードオフ: 文書を移動/別名保存してパスが変わると一致しない。
static bool16 KESCMDocUtf8Path(IDataBase* db, std::string& outUtf8)
{
	outUtf8.clear();
	if (db == nil)
		return kFalse;
	const IDFile* f = db->GetSysFile();
	if (f == nil)
		return kFalse;
	PMString p = FileUtils::SysFileToPMString(*f);
	if (p.IsEmpty())
		return kFalse;
	outUtf8 = p.GetUTF8String();
	return !outUtf8.empty();
}

//----------------------------------------------------------------------------------------
// KESCMPageCheckSaveToFile(KESCMPageCheck.h で宣言)
//----------------------------------------------------------------------------------------
void KESCMPageCheckSaveToFile()
{
	if (!KESCMIsArmed())
	{
		PMString msg("Save: start first");	// ステータス行は幅が狭い(約152px×4行)ので短く
		msg.SetTranslatable(kFalse);
		KESCMSetStatus(msg, kTrue /*forceRedrawNow*/);
		return;
	}

	// 既存ファイルを読み(他文書の保存済み分を温存するため)、今 Start 中の Target/Source ぶんだけ
	// 現在の Check(✓)+ Register(Added/Removed)で上書き/削除する。
	std::map<std::string, KESCMDocSets> merged;
	KESCMReadSetsMap(merged);	// 無ければ空

	IDataBase* dbs[2] = { KESCMArmedTargetDB(), KESCMArmedSourceDB() };
	int32 savedDocs = 0;
	int32 skippedUnsaved = 0;
	for (int i = 0; i < 2; ++i)
	{
		IDataBase* db = dbs[i];
		if (db == nil)
			continue;
		std::string path;
		if (!KESCMDocUtf8Path(db, path))
		{
			++skippedUnsaved;	// 未保存文書=キーに出来ない
			continue;
		}

		KESCMDocSets sets;
		// Check(✓)
		std::map<IDataBase*, std::set<UID> >::const_iterator it = sChecked.find(db);
		if (it != sChecked.end())
			for (std::set<UID>::const_iterator u = it->second.begin(); u != it->second.end(); ++u)
				sets.checks.insert((uint32)u->Get());
		// Register(Added/Removed=緑「/」)。別モジュール管理なので facade 経由で集める。
		std::set<UID> reg;
		KESCMPageMapCollectRegistered(db, reg);
		for (std::set<UID>::const_iterator u = reg.begin(); u != reg.end(); ++u)
			sets.registered.insert((uint32)u->Get());

		if (!sets.checks.empty() || !sets.registered.empty())
		{
			merged[path] = sets;
			++savedDocs;
		}
		else
		{
			merged.erase(path);	// この文書は今 Check も Register も無し=保存からも消す
		}
	}

	IDFile outFile;
	if (!KESCMWriteSetsMap(merged, outFile))
	{
		PMString err("Save failed (open)");	// 短い状態表示(ステータス行)
		err.SetTranslatable(kFalse);
		KESCMSetStatus(err, kTrue /*forceRedrawNow*/);
		return;
	}

	PMString msg;
	msg.SetTranslatable(kFalse);
	if (savedDocs == 0)
		msg.Append(skippedUnsaved > 0 ? "Save doc first" : "Nothing to save");
	else
		msg.Append(FileUtils::SysFileToPMString(outFile));	// パスのみ(ラベル/件数を付けるとステータス行から溢れるため)
	KESCMSetStatus(msg, kTrue /*forceRedrawNow*/);
}

//----------------------------------------------------------------------------------------
// KESCMPageCheckLoadFromFile(KESCMPageCheck.h で宣言)
//----------------------------------------------------------------------------------------
void KESCMPageCheckLoadFromFile()
{
	if (!KESCMIsArmed())
	{
		PMString msg("Load: start first");	// ステータス行は狭いので短く
		msg.SetTranslatable(kFalse);
		KESCMSetStatus(msg, kTrue /*forceRedrawNow*/);
		return;
	}

	std::map<std::string, KESCMDocSets> saved;
	if (!KESCMReadSetsMap(saved))
	{
		PMString msg("No saved data");
		msg.SetTranslatable(kFalse);
		KESCMSetStatus(msg, kTrue /*forceRedrawNow*/);
		return;
	}

	IDataBase* tgt = KESCMArmedTargetDB();
	IDataBase* src = KESCMArmedSourceDB();
	IDataBase* dbs[2] = { tgt, src };

	// この Start 中の2文書のうち、保存データを持つものを覚えておく(そのペアのパス s も保持)。
	std::map<std::string, KESCMDocSets>::const_iterator saveIt[2] = { saved.end(), saved.end() };
	bool16 anyDocFound = kFalse;

	// 各文書の平坦ページ列は Phase1 で集めて Phase3 でも使い回す(再比較でページ構造は増減しない前提。
	// 保存データを持つ文書=saveIt[i]!=end のときだけ埋まる。二重収集を避けるためのキャッシュ)。
	std::vector<UID> flatCache[2];

	//--- フェーズ1: Register を両文書へ適用する(再比較の前=除外対応表に効かせるため)。--------------
	// 保存 registered UID のうち、この文書に実在するページだけを登録集合に置き換える(setter)。
	// 保存データを持つ文書は、たとえ registered が空でも空集合で置き換える(=保存時の状態に合わせる)。
	int32 regApplied = 0;
	for (int i = 0; i < 2; ++i)
	{
		IDataBase* db = dbs[i];
		if (db == nil)
			continue;
		std::string path;
		if (!KESCMDocUtf8Path(db, path))
			continue;
		std::map<std::string, KESCMDocSets>::const_iterator s = saved.find(path);
		if (s == saved.end())
			continue;	// この文書は保存データ無し=現在の Check/Register はそのまま
		saveIt[i] = s;
		anyDocFound = kTrue;

		std::vector<UID> regPages;
		std::vector<UID>& flat = flatCache[i];
		KESCMCollectPageUIDs(db, flat);		// Phase3 でも同じ flat を使い回す
		for (size_t k = 0; k < flat.size(); ++k)
		{
			const UID u = flat[k];
			if (s->second.registered.count((uint32)u.Get()) > 0)
				regPages.push_back(u);
		}
		KESCMPageMapReplaceRegistered(db, regPages);	// 空なら登録を消す
		regApplied += (int32)regPages.size();
	}

	if (!anyDocFound)
	{
		PMString msg("No saved data for docs");
		msg.SetTranslatable(kFalse);
		KESCMSetStatus(msg, kTrue /*forceRedrawNow*/);
		return;
	}

	//--- フェーズ2: 一度だけ再比較する。--------------------------------------------------------------
	// Register が変わったので除外対応表(ペアリング)を Start と同様に張り直す。差分再比較でペア不変ページは
	// 前回結果を再利用。これにより Added/Removed の緑「/」サムネイルも更新される(登録ページ込みで Purge)。
	// 副作用として現在の Check も KESCMPageCheckPruneToMarked でマーク付きに剪定されるが、フェーズ3で
	// 保存 Check に置き換えるので問題ない。
	if (tgt != nil && src != nil)
	{
		PMString report;
		KESCMDoMarkChangesDoc(tgt, src, report, kTrue /*allowIncremental*/);
	}

	//--- フェーズ3: Check(✓)を復元する(再比較後に「今もマーク付き」のページだけ)。-----------------
	int32 checksRestored = 0;
	for (int i = 0; i < 2; ++i)
	{
		IDataBase* db = dbs[i];
		if (db == nil || saveIt[i] == saved.end())
			continue;

		const std::set<uint32>& savedChecks = saveIt[i]->second.checks;

		// 再比較後の「今マーク付き」集合を1文書1回だけ作る(ページごとに引くと O(ページ数×変更数)になる)。
		std::set<UID> marked;
		KESCMCollectMarked(db, marked);

		std::set<UID> newSet;
		const std::vector<UID>& flat = flatCache[i];	// Phase1 で収集済み(再収集しない)
		for (size_t k = 0; k < flat.size(); ++k)
		{
			const UID u = flat[k];
			if (savedChecks.count((uint32)u.Get()) > 0 && marked.count(u) > 0)
				newSet.insert(u);
		}

		// 影響ページ(旧チェック ∪ 新チェック)のサムネイルを更新する(✓の付与/消去を反映)。
		std::set<UID> affected = newSet;
		std::map<IDataBase*, std::set<UID> >::iterator cur = sChecked.find(db);
		if (cur != sChecked.end())
			for (std::set<UID>::const_iterator o = cur->second.begin(); o != cur->second.end(); ++o)
				affected.insert(*o);

		// この文書のチェックを復元セットで置き換える。
		if (newSet.empty())
			sChecked.erase(db);
		else
			sChecked[db] = newSet;
		checksRestored += (int32)newSet.size();

		if (!affected.empty())
		{
			std::vector<UID> pages(affected.begin(), affected.end());
			KESCMRefreshThumbnailsForPages(db, pages);
			// ★レイアウトビュー版の ✓(2026-07-12 追加)も即反映する。フェーズ2の再比較(KESCMDoMarkChangesDoc)
			// が両文書を Invalidate するのは「復元前の ✓ 状態」に対してなので、ここで復元後の状態で
			// もう一度 Invalidate しないと、復元/消去された ✓ がレイアウト画面に出ない(トグルと同じ理屈)。
			KESCMInvalidateDB(db);
		}
	}

	// 結果をステータス行に短く出す(幅が狭いので略記)。
	PMString msg;
	msg.SetTranslatable(kFalse);
	msg.Append("load chk");
	msg.AppendNumber(checksRestored);
	msg.Append(" reg");
	msg.AppendNumber(regApplied);
	KESCMSetStatus(msg, kTrue /*forceRedrawNow*/);
}

// KESCMPageCheck.cpp 終わり。
