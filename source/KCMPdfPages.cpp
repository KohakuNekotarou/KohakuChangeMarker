//========================================================================================
//
//  KCMPdfPages.cpp -- see the header.
//
//========================================================================================

// ⚠FIRST AND UNGUARDED: the plug-in builds with /Yu, which discards everything up to and including this line. The
//  offline harness answers it with a stub of its own (work/kcm-pdf-test).
#include "VCPlugInHeaders.h"

#include "KCMPdfPages.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <set>
#include <utility>

namespace
{
	const int kMaxTreeDepth = 64;
	const size_t kMaxPages = 1000000;

	bool NumberAt(KCMPdf::Document& doc, const KCMPdf::Object& arr, size_t i, double& out)
	{
		if (i >= arr.items.size())
			return false;
		KCMPdf::Object v;
		if (!doc.Resolve(arr.items[i], v) || !v.IsNumber())
			return false;
		out = v.Number();
		return true;
	}

	/** A rectangle (ISO 32000-1 7.9.5), its corners in either order. kFalse when it is not one, or has no area. */
	bool ReadBox(KCMPdf::Document& doc, const KCMPdf::Object* value, KCMPdf::Box& out)
	{
		if (value == nullptr)
			return false;
		KCMPdf::Object arr;
		if (!doc.Resolve(*value, arr) || arr.type != KCMPdf::kObjArray || arr.items.size() < 4)
			return false;
		double v[4];
		for (size_t i = 0; i < 4; ++i)
			if (!NumberAt(doc, arr, i, v[i]))
				return false;
		out = KCMPdf::Box((std::min)(v[0], v[2]), (std::min)(v[1], v[3]), (std::max)(v[0], v[2]), (std::max)(v[1], v[3]));
		return out.Width() > 0 && out.Height() > 0;
	}

	/** inner bounded by outer; outer itself when the two do not overlap (14.11.2). */
	KCMPdf::Box Clip(const KCMPdf::Box& inner, const KCMPdf::Box& outer)
	{
		const KCMPdf::Box b((std::max)(inner.x0, outer.x0), (std::max)(inner.y0, outer.y0),
							(std::min)(inner.x1, outer.x1), (std::min)(inner.y1, outer.y1));
		return (b.Width() > 0 && b.Height() > 0) ? b : outer;
	}

	/** A quarter turn as 0, 90, 180 or 270; anything that is not a multiple of 90 reads as 0. */
	int NormalRotate(int64_t r)
	{
		if (r % 90 != 0)
			return 0;
		int64_t n = r % 360;
		if (n < 0)
			n += 360;
		return static_cast<int>(n);
	}

	/** What a page inherits from the /Pages nodes above it (7.7.3.4). */
	struct Inherited
	{
		bool hasMedia;
		bool hasCrop;
		KCMPdf::Box media;
		KCMPdf::Box crop;
		int rotate;
		Inherited() : hasMedia(false), hasCrop(false), rotate(0) {}
	};

	void TakeInherited(KCMPdf::Document& doc, const KCMPdf::Object& node, Inherited& in)
	{
		KCMPdf::Box b;
		if (ReadBox(doc, node.Get("MediaBox"), b))
		{
			in.media = b;
			in.hasMedia = true;
		}
		if (ReadBox(doc, node.Get("CropBox"), b))
		{
			in.crop = b;
			in.hasCrop = true;
		}
		const KCMPdf::Object* r = node.Get("Rotate");
		KCMPdf::Object rv;
		if (r != nullptr && doc.Resolve(*r, rv) && rv.type == KCMPdf::kObjInt)
			in.rotate = NormalRotate(rv.intValue);
	}

	/** /PieceInfo /InDesign: the uids in key order (/0, /1, ...) and the document's id. Absent pieces leave both empty. */
	void ReadPieceInfo(KCMPdf::Document& doc, const KCMPdf::Object& page, KCMPdf::PageInfo& out)
	{
		KCMPdf::Object piece;
		const KCMPdf::Object* p = page.Get("PieceInfo");
		if (p == nullptr || !doc.Resolve(*p, piece) || piece.type != KCMPdf::kObjDict)
			return;
		KCMPdf::Object indesign;
		const KCMPdf::Object* d = piece.Get("InDesign");
		if (d == nullptr || !doc.Resolve(*d, indesign) || indesign.type != KCMPdf::kObjDict)
			return;
		KCMPdf::Object list;
		const KCMPdf::Object* l = indesign.Get("PageUIDList");
		if (l != nullptr && doc.Resolve(*l, list) && list.type == KCMPdf::kObjDict)
		{
			std::vector<std::pair<long long, uint32_t> > keyed;
			for (size_t i = 0; i < list.keys.size() && i < list.items.size(); ++i)
			{
				const char* const key = list.keys[i].c_str();
				char* end = nullptr;
				const long long k = std::strtoll(key, &end, 10);
				if (end == key || *end != 0)
					continue;
				KCMPdf::Object v;
				if (!doc.Resolve(list.items[i], v) || v.type != KCMPdf::kObjInt || v.intValue <= 0 || v.intValue > 0xFFFFFFFFll)
					continue;
				keyed.push_back(std::make_pair(k, static_cast<uint32_t>(v.intValue)));
			}
			std::sort(keyed.begin(), keyed.end());
			for (size_t i = 0; i < keyed.size(); ++i)
				out.uids.push_back(keyed[i].second);
		}
		KCMPdf::Object idValue;
		const KCMPdf::Object* id = indesign.Get("DocumentID");
		if (id != nullptr && doc.Resolve(*id, idValue) && idValue.type == KCMPdf::kObjString)
			out.documentID = KCMPdf::TextStringToUtf8(idValue.text);
	}
}	// namespace

bool KCMPdf::Info::HasPageIds() const
{
	if (pages.empty())
		return false;
	for (size_t i = 0; i < pages.size(); ++i)
		if (pages[i].uids.empty() || pages[i].documentID != pages[0].documentID)
			return false;
	return true;
}

bool KCMPdf::Info::HasMixedPageIds() const
{
	if (HasPageIds())
		return false;
	for (size_t i = 0; i < pages.size(); ++i)
		if (!pages[i].uids.empty())
			return true;
	return false;
}

bool KCMPdf::LooksLikePdf(const wchar_t* path)
{
	try
	{
		FileSource src(path);
		if (!src.IsOpen())
			return false;
		uint8_t head[1024];
		const size_t n = src.ReadAt(0, head, sizeof(head));
		for (size_t i = 0; i + 5 <= n; ++i)
			if (std::memcmp(head + i, "%PDF-", 5) == 0)
				return true;
		return false;
	}
	catch (...)
	{
		return false;
	}
}

bool KCMPdf::ReadInfo(const wchar_t* path, Info& out, std::string& why)
{
	out.pages.clear();
	try
	{
		FileSource src(path);
		if (!src.IsOpen())
		{
			why = "the file could not be opened";
			return false;
		}
		return ReadInfoFrom(src, out, why);
	}
	catch (...)
	{
		out.pages.clear();
		why = "out of memory reading the PDF";
		return false;
	}
}

bool KCMPdf::ReadInfoFrom(Source& source, Info& out, std::string& why)
{
	out.pages.clear();
	try
	{
		Document doc;
		if (!doc.Open(source, why))
			return false;
		if (doc.IsEncrypted())
		{
			why = "the PDF is encrypted";
			return false;
		}
		Object catalog;
		const Object* root = doc.Trailer().Get("Root");
		if (root == nullptr || !doc.Resolve(*root, catalog) || catalog.type != kObjDict)
		{
			why = "the PDF has no catalog";
			return false;
		}
		Object pagesRoot;
		const Object* pagesRef = catalog.Get("Pages");
		if (pagesRef == nullptr || !doc.Resolve(*pagesRef, pagesRoot) || pagesRoot.type != kObjDict)
		{
			why = "the PDF has no page tree";
			return false;
		}

		// Depth first, in document order: a node's kids go onto the stack last-first.
		struct Frame
		{
			Object node;
			Inherited in;
			int depth;
			Frame() : depth(0) {}
		};
		std::vector<Frame> stack;
		std::set<uint32_t> seen;
		if (pagesRef->type == kObjRef)
			seen.insert(pagesRef->refNum);
		Frame top;
		top.node = pagesRoot;
		stack.push_back(top);
		while (!stack.empty())
		{
			const Frame f = stack.back();
			stack.pop_back();
			Inherited in = f.in;
			TakeInherited(doc, f.node, in);
			const Object* kids = f.node.Get("Kids");
			const Object* type = f.node.Get("Type");
			const bool typedPages = (type != nullptr && type->IsName("Pages"));
			const bool typedPage = (type != nullptr && type->IsName("Page"));
			if (typedPages || (!typedPage && kids != nullptr))
			{
				if (f.depth >= kMaxTreeDepth)
				{
					out.pages.clear();
					why = "the page tree is too deep";
					return false;
				}
				Object kidArray;
				if (kids == nullptr || !doc.Resolve(*kids, kidArray) || kidArray.type != kObjArray)
					continue;
				for (size_t i = kidArray.items.size(); i-- > 0; )
				{
					const Object& k = kidArray.items[i];
					Frame child;
					child.in = in;
					child.depth = f.depth + 1;
					if (k.type == kObjRef)
					{
						if (seen.count(k.refNum) != 0)
							continue;				// a node met twice: a cycle, or a page listed twice
						seen.insert(k.refNum);
						if (!doc.Fetch(k.refNum, child.node))
							continue;
					}
					else
						child.node = k;				// a kid written in place (not allowed, but met)
					if (child.node.type != kObjDict)
						continue;
					stack.push_back(child);
				}
				continue;
			}
			if (out.pages.size() >= kMaxPages)
			{
				out.pages.clear();
				why = "the PDF has too many pages";
				return false;
			}
			PageInfo page;
			page.media = in.hasMedia ? in.media : Box(0, 0, 612, 792);	// /MediaBox is required; Letter for a page with none
			page.crop = in.hasCrop ? Clip(in.crop, page.media) : page.media;
			Box trim;
			page.trim = ReadBox(doc, f.node.Get("TrimBox"), trim) ? Clip(trim, page.crop) : page.crop;
			page.rotate = in.rotate;
			ReadPieceInfo(doc, f.node, page);
			out.pages.push_back(page);
		}
		if (out.pages.empty())
		{
			why = "the PDF has no pages";
			return false;
		}
		return true;
	}
	catch (...)
	{
		out.pages.clear();
		why = "out of memory reading the PDF";
		return false;
	}
}

// End, KCMPdfPages.cpp.
