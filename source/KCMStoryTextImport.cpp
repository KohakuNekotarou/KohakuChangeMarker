//========================================================================================
//
//  KCMStoryTextImport.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <windows.h>				// FindFirstFileW - Windows only, like the rest of KCM's file work
#include <cstdio>
#include <string>
#include <vector>

#include "FileUtils.h"

#include "KCMStoryTextImport.h"

namespace
{

/** The set held beside the origin. ⚠A file-static holding PMStrings and std::strings, so it has a
	line in the model's shutdown (KCMStoryList.h says what forgetting that costs). */
KCMStoryTextSet		sHeld;
bool16				sHolding = kFalse;

/** The path of an IDFile as Windows spells it. (KCMStoryTextExport.cpp has the same four lines,
	and for the same reason: the two files share nothing else.) */
std::wstring WidePath(const IDFile& file)
{
	PMString s;
	FileUtils::IDFileToPMString(file, s);
	int32 n = 0;
	const UTF16TextChar* b = s.GrabUTF16Buffer(&n);
	return (b != nil && n > 0)
		   ? std::wstring(reinterpret_cast<const wchar_t*>(b), static_cast<size_t>(n))
		   : std::wstring();
}

/** "<folder>\<leaf>" as an IDFile. */
IDFile FileInFolder(const std::wstring& folder, const std::wstring& leaf)
{
	std::wstring path = folder;
	if (!path.empty() && path[path.size() - 1] != L'\\' && path[path.size() - 1] != L'/')
		path += L"\\";
	path += leaf;

	PMString s;
	s.SetTranslatable(kFalse);
	s.AppendW(reinterpret_cast<const UTF16TextChar*>(path.c_str()));
	return FileUtils::PMStringToSysFile(s);
}

/** The whole file, as bytes. kFalse when it could not be opened.

	⚠stdio rather than IPMStream, which is the road KCM already took for its settings file and
	 states the reason for there (KCMPageCheck.cpp): IPMStream::Close and Flush both return void. */
bool16 ReadWholeFile(const IDFile& file, std::string& out)
{
	out.clear();

	FILE* fp = FileUtils::OpenFile(file, "rb");
	if (fp == nil)
		return kFalse;

	char buf[4096];
	size_t n = 0;
	while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0)
		out.append(buf, n);

	std::fclose(fp);
	return kTrue;
}

/** "269.html" -> 269. kFalse for a name that is not one of ours.

	★**THE FILE NAME IS THE PAIRING**, so the test is exact: every character before ".html" has to
	be a decimal digit. A reader's own notes in that folder ("notes.html", "269 copy.html") are not
	ours and are passed over rather than guessed at. */
bool16 UidOfLeaf(const std::wstring& leaf, uint32& outUid)
{
	const size_t dot = leaf.find_last_of(L'.');
	if (dot == std::wstring::npos || dot == 0)
		return kFalse;

	std::wstring ext = leaf.substr(dot + 1);
	for (size_t i = 0; i < ext.size(); ++i)
	{
		if (ext[i] >= L'A' && ext[i] <= L'Z')
			ext[i] = static_cast<wchar_t>(ext[i] - L'A' + L'a');
	}
	if (ext != L"html")
		return kFalse;

	const std::wstring stem = leaf.substr(0, dot);
	if (stem.empty() || stem.size() > 10)
		return kFalse;

	uint32 value = 0;
	for (size_t i = 0; i < stem.size(); ++i)
	{
		if (stem[i] < L'0' || stem[i] > L'9')
			return kFalse;
		value = value * 10 + static_cast<uint32>(stem[i] - L'0');
	}
	if (value == 0)
		return kFalse;

	outUid = value;
	return kTrue;
}

void AppendCount(PMString& out, const char* before, int32 n, const char* after)
{
	out.Append(before);
	out.AppendNumber(n);
	out.Append(after);
}

}	// anonymous namespace

bool16 KCMReadStoryTextFolder(const IDFile& folder, KCMStoryTextSet& out, PMString& whyNot)
{
	out = KCMStoryTextSet();
	whyNot.Clear();
	whyNot.SetTranslatable(kFalse);

	const std::wstring folderPath = WidePath(folder);
	if (folderPath.empty())
	{
		whyNot = "the chosen folder could not be read";
		whyNot.SetTranslatable(kFalse);
		return kFalse;
	}

	// The folder's own name, for the panel's Source: line.
	{
		const size_t slash = folderPath.find_last_of(L"\\/");
		const std::wstring leaf = (slash == std::wstring::npos) ? folderPath
																: folderPath.substr(slash + 1);
		out.fFolderName.SetTranslatable(kFalse);
		out.fFolderName.AppendW(reinterpret_cast<const UTF16TextChar*>(leaf.c_str()));
	}

	std::wstring pattern = folderPath;
	if (!pattern.empty() && pattern[pattern.size() - 1] != L'\\' && pattern[pattern.size() - 1] != L'/')
		pattern += L"\\";
	pattern += L"*.html";

	WIN32_FIND_DATAW found;
	HANDLE search = ::FindFirstFileW(pattern.c_str(), &found);
	if (search == INVALID_HANDLE_VALUE)
	{
		whyNot = "there is no .html file in that folder";
		whyNot.SetTranslatable(kFalse);
		return kFalse;
	}

	int32 skippedName = 0;		// not one of ours: a name that is not a decimal number
	int32 refused = 0;			// ours, but the markup could not be read
	PMString firstReason;
	firstReason.SetTranslatable(kFalse);

	do
	{
		if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			continue;

		const std::wstring leaf(found.cFileName);
		uint32 uid = 0;
		if (!UidOfLeaf(leaf, uid))
		{
			++skippedName;
			continue;
		}

		std::string bytes;
		if (!ReadWholeFile(FileInFolder(folderPath, leaf), bytes))
		{
			++refused;
			continue;
		}

		KCMStoryHtml::Story story;
		std::string why;
		if (!KCMStoryHtml::Read(bytes.c_str(), bytes.size(), story, why))
		{
			// ⚠ONE BAD FILE MUST NOT COST THE OTHERS. It is counted and the first reason is kept,
			//   so the reader is told what to fix rather than left with nothing.
			++refused;
			if (firstReason.IsEmpty())
			{
				firstReason.AppendW(reinterpret_cast<const UTF16TextChar*>(leaf.c_str()));
				firstReason.Append(": ");
				firstReason.Append(why.c_str());
			}
			continue;
		}

		out.fUids.push_back(UID(uid));
		out.fStories.push_back(story);
	}
	while (::FindNextFileW(search, &found) != 0);

	::FindClose(search);

	const int32 read = static_cast<int32>(out.fUids.size());
	AppendCount(whyNot, "", read, " story file(s) read");
	if (refused > 0)
	{
		AppendCount(whyNot, ", ", refused, " refused");
		if (!firstReason.IsEmpty())
		{
			whyNot.Append(" (");
			whyNot.Append(firstReason);
			whyNot.Append(")");
		}
	}
	if (skippedName > 0)
		AppendCount(whyNot, ", ", skippedName, " not named after a story");

	return (read > 0) ? kTrue : kFalse;
}

const KCMStoryTextSet* KCMHeldStoryText()
{
	return sHolding ? &sHeld : nil;
}

void KCMHoldStoryText(const KCMStoryTextSet& set)
{
	sHeld = set;
	sHolding = kTrue;
}

void KCMReleaseStoryText()
{
	sHeld = KCMStoryTextSet();
	sHolding = kFalse;
}

// End, KCMStoryTextImport.cpp.
