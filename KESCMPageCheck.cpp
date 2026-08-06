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

#include "IDataBase.h"			// GetSysFile(保存キー=文書ファイルパス)
#include "IActionStateList.h"	// メニューの有効/チェック
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
#include "KESCMDocUidSet.h"		// 「文書DB→ページUID集合」の共通の入れ物(登録側と共有。2026-08-06 監査 C-1)
#include "KESCMThumbnailRefresh.h"	// KESCMRefreshThumbnailsForPages(トグルページの明示サムネイル更新)

// チェック済みページ: 文書DB → ページUIDの集合。セッション内のみ。
// 空になった文書のエントリは即座に消える(KESCMDocUidSet の規約)。
static KESCMDocUidSet sChecked;

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

	bool16 anyUnchecked = kFalse;
	for (size_t i = 0; i < pages.size(); ++i)
	{
		if (!sChecked.Contains(db, pages[i]))
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
			sChecked.Insert(db, pages[i]);
		msg.Append("check +");
		msg.AppendNumber((int32)pages.size());
	}
	else
	{
		for (size_t i = 0; i < pages.size(); ++i)
			sChecked.Erase(db, pages[i]);
		msg.Append("check -");
		msg.AppendNumber((int32)pages.size());
	}

	// 合計は付け外しの後に数える(解除で空になった文書のエントリは Erase が捨てているので 0 が返る)。
	msg.Append(", total ");
	msg.AppendNumber(sChecked.CountIn(db));

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
	for (size_t i = 0; i < marked.size(); ++i)
	{
		if (sChecked.Contains(db, marked[i]))
			++chkCount;
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
	sChecked.SweepClosedDocs();	// 終了中の nil ガードも deref 回避も入れ物側の責務(KESCMDocUidSet.cpp)
}

//========================================================================================
// KESCMPageCheckClearAllDocs(KESCMPageCheck.h で宣言)
//========================================================================================
void KESCMPageCheckClearAllDocs()
{
	sChecked.ClearAllDocs();
}

//========================================================================================
// KESCMPageCheckPruneToMarked(KESCMPageCheck.h で宣言)
//   再比較後、各文書のチェックを「今もマーク付き」のページだけに絞る(マークが消えたページの
//   チェックは忘れる)。KESCMCollectChangedPageUIDs は db が sDB/sSrcDB のときだけ現在のマーク集合を
//   返す(それ以外は空=その db の全チェックが外れる)。ポインタは deref しない。
//========================================================================================
void KESCMPageCheckPruneToMarked()
{
	if (sChecked.IsEmpty())
		return;
	// ★文書ごとにマーク集合を1回だけ作って絞るので、入れ物の集合を直接いじる口(GetMap)を使う。
	//   空になった文書のエントリは最後に PruneEmptyDocs() で捨てる(KESCMDocUidSet.h の規約)。
	KESCMDocUidSet::Map& m = sChecked.GetMap();
	for (KESCMDocUidSet::Map::iterator it = m.begin(); it != m.end(); ++it)
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
	}
	sChecked.PruneEmptyDocs();
}

//========================================================================================
// KESCMPageCheckIsChecked(KESCMPageCheck.h で宣言)
//========================================================================================
bool16 KESCMPageCheckIsChecked(IDataBase* db, UID pageUID)
{
	return sChecked.Contains(db, pageUID);
}

//========================================================================================
// KESCMPageCheckHasAny(KESCMPageCheck.h で宣言)
//========================================================================================
bool16 KESCMPageCheckHasAny(IDataBase* db)
{
	return sChecked.HasAny(db);
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
	// ★途中で読込エラーなら失敗扱い(2026-07-25 監査で追加): 部分読みのまま Save のマージ元になると、
	//   読めなかった他文書分が上書きで黙って消えるため。
	const bool16 ok = ferror(fp) ? kFalse : kTrue;
	fclose(fp);
	if (!ok)
		outText.clear();
	return ok;
}

// pos 以降で「JSON のキーとしての key」(例 "\"checks\"")の位置を探す。無ければ npos。
// ★素の find だと、値の文字列(=文書のフルパス)の中にエスケープされた \"checks\" が入っている場合に
//   その内側へ一致してしまう(エスケープ後のバイト列に "checks" がそのまま現れるため)。JSON 文法上、
//   キーの直前の非空白文字は必ず '{' か ',' なので、そこまで確かめて誤一致を捨てる。
//   ⚠Windows はファイル名に '"' を使えないので現状は無害だが、Mac では使える(2026-08-06 ブロック9 監査 C-3)。
static size_t KESCMFindJsonKey(const std::string& text, size_t pos, const std::string& key)
{
	while (pos < text.size())
	{
		const size_t hit = text.find(key, pos);
		if (hit == std::string::npos)
			return std::string::npos;
		size_t b = hit;
		while (b > 0 && (text[b - 1] == ' ' || text[b - 1] == '\t' || text[b - 1] == '\n' || text[b - 1] == '\r'))
			--b;
		if (b > 0 && (text[b - 1] == '{' || text[b - 1] == ','))
			return hit;		// 直前が '{' か ',' = 本物のキー
		pos = hit + 1;		// 値の文字列に紛れ込んだ一致 = 次を探す
	}
	return std::string::npos;
}

// [regionBegin, regionEnd) の範囲内で key(例 "\"checks\"")の直後の [ ... ] を探し、中の符号なし整数を
// out に拾う。key が region 内に見つかれば(配列が空でも)kTrue、無ければ kFalse。region 境界を跨がないよう
// 見つけた '[' / ']' が regionEnd を越えるものは無効扱い。
static bool16 KESCMParseUintArray(const std::string& text, size_t regionBegin, size_t regionEnd,
	const char* key, std::set<uint32>& out)
{
	out.clear();
	const std::string k(key);
	const size_t kk = KESCMFindJsonKey(text, regionBegin, k);
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
static bool16 KESCMReadSetsMap(std::map<std::string, KESCMDocSets>& out, bool16* outReadError = nil)
{
	out.clear();
	if (outReadError)
		*outReadError = kFalse;

	IDFile file;
	if (!KESCMPageChecksFile(file))
		return kFalse;
	if (!FileUtils::DoesFileExist(file))
		return kFalse;	// ファイル無し=正常な「保存なし」(読込エラーではない)
	std::string text;
	if (!KESCMReadWholeFile(file, text))
	{
		// ★在るのに読めない(open/fread 失敗)。Save のマージ元にすると既存の保存分が全て消えるため、
		//   呼び出し側(Save)が中止できるよう区別して返す(2026-07-25 監査で追加)。
		if (outReadError)
			*outReadError = kTrue;
		return kFalse;
	}
	if (text.empty())
		return kFalse;	// 空ファイル=温存すべきデータ無し(上書きしても失うものが無い)

	const std::string kPathKey = "\"path\"";
	size_t p = 0;
	while (true)
	{
		const size_t kpath = KESCMFindJsonKey(text, p, kPathKey);
		if (kpath == std::string::npos)
			break;

		// この doc の領域末尾 = 次の "path"(無ければ末尾)。配列探索がこの境界を跨がないようにする。
		const size_t next = KESCMFindJsonKey(text, kpath + kPathKey.size(), kPathKey);
		const size_t regionEnd = (next == std::string::npos) ? text.size() : next;

		// "path" の後の ':' → 開き '"' → 文字列本体。
		// ★読解失敗は break ではなく「この doc だけスキップ」(2026-07-25 監査で変更): 1エントリの破損で
		//   残りの全 doc を放棄すると、その状態の Save マージで後続の保存分が消えるため。
		size_t q = text.find(':', kpath + kPathKey.size());
		if (q == std::string::npos || q >= regionEnd)
		{
			p = regionEnd;
			continue;
		}
		++q;
		while (q < text.size() && (text[q] == ' ' || text[q] == '\t' || text[q] == '\n' || text[q] == '\r'))
			++q;
		std::string pathStr;
		if (!KESCMJsonReadString(text, q, pathStr))
		{
			p = regionEnd;
			continue;
		}

		KESCMDocSets sets;
		KESCMParseUintArray(text, q, regionEnd, "\"registered\"", sets.registered);
		// v2 の "checks"。無ければ旧 v1 の "pages" を checks とみなす。
		if (!KESCMParseUintArray(text, q, regionEnd, "\"checks\"", sets.checks))
			KESCMParseUintArray(text, q, regionEnd, "\"pages\"", sets.checks);

		if (!sets.checks.empty() || !sets.registered.empty())
			out[pathStr] = sets;

		p = regionEnd;	// 次の doc を探す
	}

	// ★本文があるのに構造の目印("docs" キー)すら無く1件も読めなかった=壊れたファイル。マージ元にすると
	//   全消えするため読込エラー扱いにする(正規の「空の docs 配列」ファイルはエラーにしない)(2026-07-25)。
	if (out.empty() && text.find("\"docs\"") == std::string::npos && outReadError)
		*outReadError = kTrue;

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
	// ★書込バイト数と fclose の成否を確認(2026-07-25 監査で追加): ディスクフル等の部分書込を
	//   「保存できた」と誤報告しない(TSV 側の Flush 後 GetStreamState 確認と同じ流儀)。
	const size_t wrote = fwrite(json.data(), 1, json.size(), fp);
	const int closed = fclose(fp);
	return (wrote == json.size() && closed == 0) ? kTrue : kFalse;
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
	bool16 readError = kFalse;
	KESCMReadSetsMap(merged, &readError);	// 無ければ空
	if (readError)
	{
		// ★既存ファイルが在るのに読めない/壊れている。このまま上書きすると他文書の保存分が消えるため中止
		//   (2026-07-25 監査で追加)。
		PMString err("Save failed (read old)");
		err.SetTranslatable(kFalse);
		KESCMSetStatus(err, kTrue /*forceRedrawNow*/);
		return;
	}

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
		std::set<UID> chk;
		sChecked.CollectInto(db, chk);
		for (std::set<UID>::const_iterator u = chk.begin(); u != chk.end(); ++u)
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
		PMString err("Save failed (write)");	// 短い状態表示(ステータス行)。open/書込/close いずれの失敗も含む
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
	// ★再比較をキャンセルされたら(登録の変更が多いと進捗バーに Cancel が出る)、マークは
	//   KESCMDoMarkChangesDoc 側で全部破棄されている。そのままフェーズ3へ進むと「今もマーク付き」が
	//   空集合になり、保存してあった ✓ を1つも復元しないまま「load chk0」と報告してしまう。
	//   → 復元は行わず、Start 経路と同じ考え方で Stop まで戻す(枠が1つも無い Start 中を残さない)。
	//   保存ファイル自体は無傷なので、Start し直して Load を実行すればやり直せる。2026-07-29 の自己レビューで発見。
	if (tgt != nil && src != nil)
	{
		PMString report;
		if (KESCMDoMarkChangesDoc(tgt, src, report, kTrue /*allowIncremental*/) != kSuccess)
		{
			KESCMToggleStartStop();		// arm 中なので Stop 分岐(strip 撤去・disarm・Check/Register 破棄)
			PMString msg("Load cancelled");
			msg.SetTranslatable(kFalse);
			KESCMSetStatus(msg, kTrue /*forceRedrawNow*/);
			return;
		}
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
		// ★CollectInto は out をクリアしないので、newSet に旧チェックを足し込む形で和集合になる。
		std::set<UID> affected = newSet;
		sChecked.CollectInto(db, affected);

		// この文書のチェックを復元セットで置き換える(空ならエントリごと消える)。
		sChecked.Replace(db, newSet);
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
