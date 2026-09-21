//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - a small XML document as a tree, with nothing underneath it
//
//  WHAT THIS IS FOR. The reading half of KCMStoryDocx has to walk the parts of a .docx -
//  word/document.xml, word/footnotes.xml, word/styles.xml, the customXml tag - and answer, for
//  every element, "which one is this, in which namespace, with which attributes, holding what".
//  Word's own files are that shape exactly: small, well-formed, namespace-prefixed XML.
//
//  *** A PURE FUNCTION. No SDK type, no file. *** Built and run outside InDesign in
//  work/kcm-storydocx-test, for the reason KCMStoryShape.h gives: the writer and the reader of a
//  format are tested AGAINST EACH OTHER, in one second, on a plain struct - and that is only
//  possible when neither needs InDesign to run. The SDK's own SAX parser (ISAXServices) would tie
//  the reader to a running application for a job that is a few hundred lines of plain C++.
//
//  *** THE TREE IS AN ARENA OF INDICES, NOT A RECURSIVE STRUCT. *** A node holds the indices of its
//  children in one flat vector, so the header needs no type that contains itself and asks nothing
//  of the language standard the plug-in happens to build with.
//
//  *** NAMESPACES ARE RESOLVED, PREFIXES ARE NOT TRUSTED. *** Word writes "w:" for its main
//  namespace and always has, but a file is matched on the URI a prefix was bound to, so a file
//  that bound another prefix reads the same. Attributes are the exception: they are kept as
//  written and looked up by their LOCAL name ("val" finds w:val), because every attribute the
//  reader asks for lives on an element whose namespace has already been settled.
//
//  WHAT IT REFUSES, with a reason: bytes that are not one well-formed document (an end tag that
//  does not match, a document that does not close, an entity it does not know). What it ignores:
//  the declaration, processing instructions, comments, a DOCTYPE. What it keeps: every text node,
//  whitespace-only ones included - whether whitespace between elements means anything is the
//  reader's question, not the tree's.
//
//========================================================================================
#ifndef __KCMXmlTree_h__
#define __KCMXmlTree_h__

#include "BaseType.h"

#include <string>
#include <vector>

/** One attribute, as written. fName keeps its prefix ("w:val"); fValue has its entities decoded. */
struct KCMXmlAttr
{
	std::string	fName;
	std::string	fValue;
};

/** One node: an element (fName is its LOCAL name, fNs the namespace URI its prefix resolved to)
	or a text node (fName empty, the characters in fText, entities decoded). */
struct KCMXmlNode
{
	std::string				fName;
	std::string				fNs;
	std::string				fText;
	std::vector<KCMXmlAttr>	fAttrs;
	std::vector<int32>		fChildren;	// indices into the tree, in document order

	bool16 IsText() const { return fName.empty() ? kTrue : kFalse; }
};

class KCMXmlTree
{
public:
	/** The bytes of one XML document (a BOM in front is skipped) -> this tree. Whatever was here
		before is dropped first. kFalse with a reason when the bytes are not one well-formed
		document; the tree is then not to be read. */
	bool16 Parse(const char* xml, size_t size, std::string& whyNot);

	/** The document element, or -1 when nothing has been parsed. */
	int32 Root() const;
	int32 Count() const;
	const KCMXmlNode& At(int32 node) const;

	/** The first attribute of `node` whose LOCAL name is `localName`, whatever its prefix
		("val" finds w:val, "space" finds xml:space), or nil. */
	const std::string* Attr(int32 node, const char* localName) const;

	/** The first element child of `node` in namespace `ns` named `localName`, or -1. */
	int32 Child(int32 node, const char* ns, const char* localName) const;

	/** Whether `node` is an element in namespace `ns` named `localName`. */
	bool16 Is(int32 node, const char* ns, const char* localName) const;

	/** The characters of every text node under `node`, at any depth, in document order. */
	std::string TextBelow(int32 node) const;

	static const char* const kWordNs;	// http://schemas.openxmlformats.org/wordprocessingml/2006/main
	static const char* const kMcNs;		// http://schemas.openxmlformats.org/markup-compatibility/2006

private:
	std::vector<KCMXmlNode>	fNodes;		// [0] is the document; its one element child is the root
};

#endif // __KCMXmlTree_h__

// End, KCMXmlTree.h.
