//========================================================================================
//
//  KESCMChangedPagesTSV.cpp
//
//  「Export Changed Pages...」(パネルのフライアウト)の実体。現在の比較(Start 後)の変更ページ
//  一覧を、KESCL の KESCLReportSave(KESCLReportPanel::SaveReportAsText)と同じ流儀のタブ区切り
//  テキストで保存する。2列 = Page / Type(Changed / Inserted / Deleted)。ラベルは全て英語で統一
//  (ユーザー指定 2026-07-25)。Page はそのページ自身の表示名(変更・挿入=Target / 削除=Source)。
//
//  ・ページ番号は IPageList::GetPageString(★セクション込み・**ページパネルの番号**)で取る。
//    ★★★2026-08-18(不具合再検査 B10 の2周目)＝**InDesign のページ番号は2つある**(実機で実測):
//      ①ページパネル / ページ番号フィールド / DOM page.name / GetPageString(…,kTrue) = 隠しスプレッドも数える
//      ②ページに刷られる実ノンブル / GetPageString(…,kFalse)                          = 隠しスプレッドを飛ばす
//    この一覧は「どのページを見に行くか」を人に渡す表なので①(kTrue)で書く。詳細は PageDisplay の注記。
//    ⚠KESCMPageNumberMarker とは**引数が2つ違う**: あちらは bIncludeSectionName=kFalse(実ノンブルに
//    セクション名は付かない。2026-08-06 のブロック5 監査)＋ bIncludePagesOfHiddenSpread=kFalse(②の側)。
//    **どちらの違いも「実際に刷られる字を測る」という用途から来ている**。同じ関数だが問いが違う。
//  ・★隠れているスプレッドのページも**元のページ番号で出し、"(Hide)" を添える**("2 (Hide)"。
//    ユーザー指定 2026-08-18)。判定は KESCMIsPageOnHiddenSpread、添えるのは PageDisplay の中だけ。
//    ⚠一度は「隠れているページは出さない」で作ったが、それだと**変更があったのに一覧に載らない
//    ページ**が生まれる。番号と状態を同じ列で渡すほうが、受け取った人が迷わない。
//  ・出力は UTF-8 + BOM + CRLF(KESCL と同一)。日本語ページ名でも Excel/メモ帳が化けない。
//  ・成功時は無言、失敗のみステータス行。未 Start / 変更ゼロは短くステータス行に出して戻る。
//  ・★文字列は最初から最後まで PMString で持つ(2026-07-25 追補 Mac 対応)。旧実装は std::wstring と
//    reinterpret_cast<const wchar_t*>(UTF16TextChar*) で組んでいたが、これは wchar_t が 16bit の
//    Windows でしか成立しない。macOS/clang の wchar_t は 32bit なので、同じキャストは文字化けに
//    加えて元バッファの 2 倍を読む(バッファ外読み取り)。PMString なら UTF-16 のまま扱えて
//    プラットフォームに依存しない。ラベルは全て ASCII なので Append(const char*) で足りる。
//  ・★マスタースプレッドのページも出す(2026-08-11)。ただし出すのは **変更(Changed)だけ**——
//    マスターどうしは KESCMBuildMasterPairing が「名前で」対応付け、相手のいないマスターは組まずに
//    飛ばすので、通常ページの Inserted/Deleted に相当する状態(=対応表からあふれる)が存在しない
//    (ユーザー指定 2026-08-11)。Page 列はマスタースプレッド名("A-親ページ")で、見開きマスターの
//    左右は末尾の " (1)" / " (2)" で分ける。
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
#include "IHierarchy.h"			// GetSpreadUID(マスターページ→そのマスタースプレッド)
#include "IMasterSpread.h"		// GetName("A-親ページ")
#include "ISpread.h"			// GetNumPages / GetNthPageUID(見開きマスターの左右を分ける)

// General includes:
#include "PMString.h"
#include "StreamUtil.h"
#include "SDKFileHelper.h"		// SDKFileSaveChooser (sdksamples/common)

#include <string>
#include <vector>
#include <set>

// Project includes:
#include "KESCMCore.h"				// KESCMSetStatus / KESCMCollectPageUIDs / KESCMCollectMasterPageUIDs
									// / KESCMIsPageOnHiddenSpread(隠れたスプレッドのページを一覧から外す。2026-08-18)
// (★KESCMUIShared.h は 2026-08-13 Task 10 で外した＝ステータス行への出力は Task 9 で戻り値へ変わり、
//  このファイルから UI を呼ぶ経路は1つも残っていない。保存先パスは呼び手のフライアウトが表示する)
#include "KESCMDrawEventHandler.h"	// sEntries / sDB / sSrcDB / sOverflowT / sOverflowS(★どれも「比較した時点」の
									// 控え＝画面・サムネイル・地図が見ているものと同じ。2026-08-18 の B10)
#include "KESCMPageMap.h"			// KESCMPageMapCollectRegistered
									// (★KESCMBuildPairing の呼びは B10 で無くなった＝あふれ集合は上の控えから読む)
#include "KESCMChangedPagesTSV.h"

namespace
{

// 出力は2列(Page / Type)。ラベルは全て英語で統一(ユーザー指定 2026-07-25。KESCM の他 UI と同じ)。
// Type の値 = Changed / Inserted / Deleted。旧ページ列は持たない(ページ名はそのページ自身の
// 文書での表示名: 変更・挿入=Target 側 / 削除=Source 側)。全て ASCII なので char リテラルでよい。
const char* const kKindChanged  = "Changed";
const char* const kKindInserted = "Inserted";
const char* const kKindDeleted  = "Deleted";
const char* const kHeaderCol1   = "Page";
const char* const kHeaderCol2   = "Type";

// 区切り文字は AppendW(UTF32TextChar) で明示的に入れる(KESCM の他所と同じ流儀。char リテラルの
// "\t" / "\n" を PMString へ渡す経路のエンコーディング解釈に依存しない)。値は使う場所でその都度
// 組む(名前空間スコープの静的オブジェクトにして初期化順序に依存させない)。
const int kTabCode = 0x09;
const int kLfCode  = 0x0A;

// 1行分(ページ表示名 + 種別)。page はドキュメント由来なので PMString(UTF-16)、kind は上の
// 固定 ASCII リテラルを指すだけ(コピー不要)。
struct KESCMChangeRow
{
	PMString    page;
	const char* kind;
	KESCMChangeRow() : kind(kKindChanged) { page.SetTranslatable(kFalse); }
};

// ステータス行(KESCL/KESCM 共通の非翻訳・英語の流儀)。
// ★★2026-08-13(Task 9): **ステータス行へ直接書かず、呼び手へ返す文字列を覚えるだけにした。**
//   理由＝TSV 書き出しは「成功/失敗と保存先パス」を**返すのが自然**で、通知を投げる理由が無い
//   (設計書 §3.3)。ここは model 側で、表示は UI 側の呼び手(フライアウトの Export Changed Pages)が行う。
//   ⚠関数名と呼び所は元のまま＝本体のどの経路(早期 return を含む)からでも今までどおり呼べる。
PMString gExportMessage;

void ShowStatus(const char* text)
{
	gExportMessage = PMString(text);
	gExportMessage.SetTranslatable(kFalse);
}

// pageUID(db 内)の「ページパネルに出ている表示ページ番号」を返す。取れなければ空。
// 引数(IPageList.h:141-146): 第3 bIncludeSectionName=kTrue … セクション名込み("A:12")。人が読む一覧なので
//   どのセクションの 12 かが分かる方が良い。⚠KESCMPageNumberMarker は kFalse(用途が違う。下記)。
// 第4 bUseIntegerStyle=kFalse … セクションの番号スタイルそのまま(ローマ数字等も画面どおり)。
//
// ★★★第7 bIncludePagesOfHiddenSpread=**kTrue**(既定) ---- 2026-08-18(不具合再検査 B10 の2周目)に
//   kFalse から変更。**InDesign はページ番号を2つ持っている**(同日 実機で実測):
//     ・ページパネル / ステータスバーのページ番号フィールド / スクリプト DOM の page.name /
//       GetPageString(…,kTrue) …… **隠しスプレッドのページも数える**(隠しても元の番号のまま)
//     ・ページに組版される実ノンブル(自動ページ番号マーカー) / GetPageString(…,kFalse)
//       …… **隠しスプレッドを飛ばす**(先頭スプレッドを隠すと2ページ目に "1" が刷られる。撮影で確認)
//   旧コメントは kFalse を「(=画面に出ている番号)」と書いていたが、**画面のほうは kTrue**だった。
//   ⇒ この一覧は「どのページを見に行くか」を人に渡す表で、受け取った人はページパネルで探す。
//     ∴ ページパネルと同じ番号(kTrue)で書く。
//   ★これを kFalse のままにすると、Hide Unchanged で変更なしスプレッドを隠したまま書き出したとき、
//     変更ページ 2,3 が "1,2" と1つずつずれた一覧になっていた(実測)。KESCM の標準的な使い方
//     ---- 変更なしを隠す → 一覧を書き出す ---- がそのまま当たる。
//   ⚠**KESCMPageNumberMarker.cpp は kFalse のままで正しい**: あちらは「誌面に実際に刷られる数字」の
//     インク範囲を測る用途なので、実ノンブルと同じ数え方でなければならない。非対称は意図的。
PMString PageDisplay(IDataBase* db, UID pageUID)
{
	PMString out;
	out.SetTranslatable(kFalse);
	if (db == nil || pageUID == kInvalidUID)
		return out;
	InterfacePtr<IPageList> pageList(db, db->GetRootUID(), UseDefaultIID());
	if (pageList == nil)
		return out;
	pageList->GetPageString(pageUID, &out, kTrue, kFalse, kDefaultPageType, kTrue, kTrue);
	out.SetTranslatable(kFalse);	// GetPageString が付け直す可能性に備えて再設定

	// ★★2026-08-18(不具合再検査 B10 の2周目・ユーザー指定): **隠れているスプレッドのページも
	//   「元のページ番号」で出し、隠れていることが分かる印を添える**("2 (Hide)")。
	//   一覧を受け取った人が「2ページ目を見に行ったのに無い」で止まらないように、番号と状態を
	//   同じ列で渡す。⚠**列が数値でなくなる心配は無い**——マスタースプレッドの行は元から
	//   "A-親ページ (2)" のような文字列で、この列は最初から数値列ではない。
	//   ★印の綴りはパネルの Prev/Next のステータス行と同じ "(Hide)"(綴りを2つにしない)。
	if (KESCMIsPageOnHiddenSpread(db, pageUID))
		out.Append(" (Hide)");
	return out;
}

// マスターページ(db 内)の表示名。"A-親ページ"(IMasterSpread::GetName)を返す。同じマスタースプレッドに
// 2ページ以上あるときだけ、スプレッド内の位置を " (1)" / " (2)" で添える——見開きマスターの左右は
// InDesign では同じ1つの名前しか持たないので、両ページが変わると同じ文字列が2行並んでしまう
// (ユーザー指定 2026-08-11)。マスターページでない/名前が引けないときは空を返す(呼び手が
// PageDisplay へ落とす)。
//
// ★通常ページの PageDisplay(GetPageString)と分けてあるのは、マスターページに GetPageString を
//   渡すと prefix("A")しか返らず、どのマスターかは分かってもマスターだと分からないため。
//   IPageList.h:138 の bAbbreviate の説明が長形("A-Master")に触れているのは **spreadUID を
//   渡したとき**の話で、ページ UID には効かない。
PMString MasterPageDisplay(IDataBase* db, UID pageUID)
{
	PMString out;
	out.SetTranslatable(kFalse);
	if (db == nil || pageUID == kInvalidUID)
		return out;

	// ページ → そのページが載っているスプレッド。IHierarchy::GetSpreadUID は「この階層ノードの
	// スプレッド」を返す契約でページ限定ではない(KESCMPeek / KESCMChangeNav と同じ聞き方)。
	InterfacePtr<IHierarchy> pageHier(db, pageUID, UseDefaultIID());
	if (pageHier == nil)
		return out;
	const UID spreadUID = pageHier->GetSpreadUID();
	if (spreadUID == kInvalidUID)
		return out;

	// 通常スプレッドは IID_IMASTERSPREAD を持たないので、この Query が「マスターか」の判定を兼ねる。
	InterfacePtr<IMasterSpread> master(db, spreadUID, UseDefaultIID());
	if (master == nil)
		return out;
	master->GetName(&out);
	// ★文書側の実データ(ユーザーが付けた名前)なので翻訳キーとして扱わせない。GetName が
	//   translatable を立てて返す可能性に備えて取った後に落とす(PageDisplay と同じ流儀)。
	out.SetTranslatable(kFalse);
	if (out.NumUTF16TextChars() == 0)
		return out;

	const int32 posBase = 1;	// 人に見せる番号は 1 始まり
	InterfacePtr<ISpread> spread(db, spreadUID, UseDefaultIID());
	if (spread != nil)
	{
		const int32 np = spread->GetNumPages();
		if (np > 1)
		{
			for (int32 p = 0; p < np; ++p)
			{
				if (spread->GetNthPageUID(p) == pageUID)
				{
					out.Append(" (");
					out.AppendNumber(p + posBase);
					out.Append(")");
					break;
				}
			}
		}
	}
	return out;
}

// ファイル名に使えない文字を '-' に(KESCL の SanitizeForFileName と同じ9文字)。Windows の禁止文字を
// 基準にする(Mac で禁止なのは '/' と ':' だけなので、この集合はどちらのOSでも安全側)。
// PMString を UTF-16 のまま1文字ずつ写す=サロゲートペアも壊さない。
PMString SanitizeForFileName(const PMString& part)
{
	PMString out;
	out.SetTranslatable(kFalse);
	const int32 n = part.NumUTF16TextChars();
	const UTF16TextChar* b = part.GrabUTF16Buffer(nil);
	if (b == nil)
		return out;
	for (int32 i = 0; i < n; ++i)
	{
		const UTF16TextChar c = b[i];
		const bool16 bad = (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?'
			|| c == '"' || c == '<' || c == '>' || c == '|') ? kTrue : kFalse;
		out.AppendW(UTF32TextChar(bad ? (UTF16TextChar)'-' : c));
	}
	return out;
}

// db を所有する文書の表示名(拡張子は残す。suggested filename 用)。取れなければ空。
// KESCMPanelObserver::KESCMDocNameFromDB と同じ解決経路(セッション→app→docList→FindDocByDataBase)。
PMString DocNameFromDB(IDataBase* db)
{
	PMString out;
	out.SetTranslatable(kFalse);
	if (db == nil)
		return out;
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app != nil ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return out;
	IDocument* d = docList->FindDocByDataBase(db);	// no ref(deref せず名前を取るだけ)
	if (d == nil)
		return out;
	d->GetName(out);
	out.SetTranslatable(kFalse);
	return out;
}

// suggested filename: "KohakuChangeMarker_ChangedPages_<targetDoc>.txt"。target 名が取れなければ
// "KohakuChangeMarker_ChangedPages.txt"。拡張子(.indd 等)は落とす。
PMString BuildSuggestedFileName(IDataBase* targetDB)
{
	PMString result;
	result.SetTranslatable(kFalse);
	result.Append("KohakuChangeMarker_ChangedPages");

	// 文書名から拡張子(最後の '.' 以降)を落とす。'.' が先頭のときは落とさない(隠しファイル名扱い)。
	const PMString doc = DocNameFromDB(targetDB);
	const int32 n = doc.NumUTF16TextChars();
	const UTF16TextChar* b = doc.GrabUTF16Buffer(nil);
	if (n > 0 && b != nil)
	{
		int32 cut = n;
		for (int32 i = n - 1; i > 0; --i)
			if (b[i] == '.') { cut = i; break; }
		PMString stem;
		stem.SetTranslatable(kFalse);
		for (int32 i = 0; i < cut; ++i)
			stem.AppendW(UTF32TextChar(b[i]));
		if (stem.NumUTF16TextChars() > 0)
		{
			result.Append("_");
			result.Append(SanitizeForFileName(stem));
		}
	}
	result.Append(".txt");
	return result;
}

// 変更ページ行を集める。行が1つでもあれば kTrue。順序: Target をドキュメント順(変更+挿入)、
// 続けて Source をドキュメント順(削除)。
bool16 CollectRows(IDataBase* targetDB, IDataBase* sourceDB, std::vector<KESCMChangeRow>& rows)
{
	// 挿入/削除の分類に使う「あふれ集合」。
	// ★★2026-08-18(不具合再検査 B10) = **描画が使っているキャッシュそのものを読む。**
	//   このファイルのヘッダーは元から「挿入 = sOverflowT / 削除 = sOverflowS」と宣言していたのに、
	//   実装はここで KESCMBuildPairing を呼び直し、**今の文書構成から**あふれを計算していた。
	//   キャッシュのほうは比較した時点で固定される(KESCMDrawEventHandler.h の "生のページ挿入/削除
	//   (Start無し)には追従しない=次の Start/再比較まで固定(枠=リングと同じ挙動)")ので、Start の後に
	//   ページを足して再比較していないと、**画面に「/」が出ていないページを Inserted として書いていた**。
	//   一覧は画面の写しであるべきなので、同じ集合を読むようにした。
	//   ★副産物: KESCMBuildPairing の戻り値 tPairs/sPairs は**一度も使っていなかった**(旧ページ列を
	//     廃した 2026-07-25 以降)。あふれ集合のためだけに両文書の全ページ走査を回していたのが丸ごと消える。
	//   ⚠EnsureOverflowCache は控えた (sDB,sSrcDB) が現在と一致していれば何もしない。呼び手
	//     (KESCMExportChangedPagesTSVRun)が targetDB/sourceDB をその2つから取っているので必ず一致する。
	KESCMDrawEventHandler::EnsureOverflowCache();
	const std::set<UID>& overflowT = KESCMDrawEventHandler::sOverflowT;
	const std::set<UID>& overflowS = KESCMDrawEventHandler::sOverflowS;

	// 手動登録(Added=Target 側 / Removed=Source 側)。
	std::set<UID> registeredT, registeredS;
	KESCMPageMapCollectRegistered(targetDB, registeredT);
	KESCMPageMapCollectRegistered(sourceDB, registeredS);

	// ---- Target をドキュメント順に: 変更 → 挿入 ----
	// ★★2026-08-18(不具合再検査 B10 の2周目・ユーザー指定): **隠れているスプレッドのページも出す。**
	//   一度は「隠したのはユーザー自身の操作だから一覧から外す」で作ったが、**外すと『変更があった
	//   のに一覧に載らないページ』が生まれる**ので、外さずに **PageDisplay が "(Hide)" を添える**形に
	//   変えた（番号は隠す前と同じ＝ページパネルの番号）。⇒ 判定の場所は PageDisplay ただ1つ。
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

	// ---- Target のマスタースプレッド: 変更のみ(2026-08-11) ----
	// ★マスタースプレッドは IMasterSpreadList の別管理で、上の KESCMCollectPageUIDs には一度も現れない
	//   (2026-08-16 に中身が IPageList へ移ったが、**マスターを含まないのは契約**＝`IPageList.h:81`
	//    "does not include master pages"。根拠が ISpreadList からヘッダーの明文へ移っただけ)。並び順は Prev/Next(KESCMBuildStops)と peek が使うのと同じ
	//   KESCMCollectMasterPageUIDs に借りる(マスターの列挙順を決める場所を2つにしない)。
	//   位置は「Target の通常ページを全部出した後・Source の削除の前」= Prev/Next の巡回順と同じ並び。
	// ★Inserted/Deleted は扱わない(冒頭の注記): 相手のいないマスターは KESCMBuildMasterPairing が
	//   組まないので比較されず、sEntries にも対応表のあふれにも現れない。
	std::vector<UID> masterOrder;
	KESCMCollectMasterPageUIDs(targetDB, masterOrder);
	for (size_t i = 0; i < masterOrder.size(); ++i)
	{
		const UID m = masterOrder[i];
		if (KESCMDrawEventHandler::sEntries.count(m) == 0)
			continue;
		KESCMChangeRow row;
		row.page = MasterPageDisplay(targetDB, m);
		if (row.page.NumUTF16TextChars() == 0)
			row.page = PageDisplay(targetDB, m);	// 名前が引けないときの保険(prefix だけでも出す)
		row.kind = kKindChanged;
		rows.push_back(row);
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

// 集めた行を1本のテキスト(ヘッダー + 各行、行末は '\n')に組む。区切りの TAB / LF は ASCII なので
// Append(const char*) でそのまま置ける(PMString は UTF-16 で保持する)。
PMString BuildReportText(const std::vector<KESCMChangeRow>& rows)
{
	PMString text;
	text.SetTranslatable(kFalse);
	text.Append(kHeaderCol1);  text.AppendW(UTF32TextChar(kTabCode));
	text.Append(kHeaderCol2);  text.AppendW(UTF32TextChar(kLfCode));
	for (size_t i = 0; i < rows.size(); ++i)
	{
		text.Append(rows[i].page);
		text.AppendW(UTF32TextChar(kTabCode));
		text.Append(rows[i].kind);
		text.AppendW(UTF32TextChar(kLfCode));
	}
	return text;
}

} // anonymous namespace

//========================================================================================
// 本体。★2026-08-13(Task 9)に static 化し、公開関数は下のラッパにした。中身は1行も変えていない
//   ＝早期 return がいくつもあるので、**最後の1か所で out へ書ける形**にするために包んだ。
//========================================================================================
static void KESCMExportChangedPagesTSVRun()
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
	// ★★2026-08-15（第2段 Task 10）＝**model 側から出しているファイルダイアログ**。残す判断の根拠は
	//   KESCMHideUnchanged.cpp の CAlert と同じ:
	//   ・`SDKFileSaveChooser` は `sdksamples/common` のヘルパーで、**UI プラグイン由来の boss ではない**。
	//   ・この経路（Export Changed Pages...）は Facade 越しに**フライアウトの操作**からしか入らず、
	//     **BG で走る描画パスからは到達しない**（2026-08-15 に呼び出し全数を Grep して確認）。
	//   ⚠BG から書き出させたくなったら、**保存先を UI が決めて `IDFile` を引数で渡す**形へ変える
	//     （Task 9B の KESCMGetPanelBookFile とまったく同じ形）。チューザは BG では開けない。
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

	// UTF-8 + BOM、'\n' -> '\r\n'(KESCL と同一。BOM 無しの日本語テキストは Excel/メモ帳が推測を誤る)。
	const PMString report = BuildReportText(rows);
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

//========================================================================================
// KESCMExportChangedPagesTSV(KESCMChangedPagesTSV.h で宣言)
//   ★2026-08-13(Task 9): **ステータス行へ書かず、出したい文字列を out で返す。**
//   TSV 書き出しは「成功/失敗と保存先パス」を返すのが自然で、通知を投げる理由が無い(設計書 §3.3)。
//   ⇒ ここは model 側、表示は呼び手(フライアウトの Export Changed Pages＝UI)。
//   ★出すことが無ければ out は空で返る(成功時は無言、が従来の仕様)。
//========================================================================================
void KESCMExportChangedPagesTSV(PMString& outMessage)
{
	gExportMessage.Clear();
	KESCMExportChangedPagesTSVRun();
	outMessage = gExportMessage;
}

//========================================================================================
// KESCMClearExportMessage(KESCMChangedPagesTSV.h で宣言)
//   ★2026-08-18(不具合再検査 B8)。終了時に file-static PMString を空にする1行。文書にも UI にも
//   触らないので、終了処理中のどのタイミングで呼ばれても安全。
//========================================================================================
void KESCMClearExportMessage()
{
	gExportMessage.Clear();
}

// End, KESCMChangedPagesTSV.cpp.
