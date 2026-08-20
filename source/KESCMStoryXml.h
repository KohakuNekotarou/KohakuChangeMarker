//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  One story, exported to XML in memory. Nothing is written to disk.
//
//  This is how the Story Changes mode gets at the text: comparing two versions means reading
//  both of them out in a form that can be diffed, and the snippet API is the only route that
//  hands over a whole story - text, paragraph breaks and all - without going through the file
//  system. What comes out is INX rather than IDML, but the two are images of the same scripting
//  DOM, so the <Content> elements the diff reads are identical either way.
//
//  *** ONLY ExportStory CAME ACROSS FROM KohakuTest. *** KTStoryXml also has Capture/GetLast,
//  which exist to expose the export to a script (app.ktStoryXml) so the experiment could be
//  driven from outside. KESCM calls this from C++ and has no such need, and a function nobody
//  calls is a promise nobody keeps - so they were left behind rather than carried over unused.
//
//  ⚠THE XML BACKING STORE MUST NOT BE PASSED HERE. Asking the snippet API to export it crashed
//  InDesign outright during KohakuTest's first run. ISnippetExport.h:71-77 says it "returns
//  kFailure" for that case, which is gentler than what actually happens. Callers walk stories
//  with IStoryList::GetNthUserAccessibleStoryUID, which does not include it.
//
//========================================================================================

#ifndef __KESCMStoryXml_h__
#define __KESCMStoryXml_h__

#include <string>

class UIDRef;

/** Reading one story out as XML, in memory.
	@ingroup KESCM
*/
namespace KESCMStoryXml
{
	/** Exports one story to XML, in memory. Nothing is written to disk.

		Works on a story in ANY open document, not just the front one - which is the whole point
		here, since a comparison always has one story from each of two documents.

		@param storyRef IN the story to export. ⚠Not the XML backing store (see the file comment).
		@param utf8Out OUT receives the XML as UTF-8. Emptied first.
		@return kTrue when the export succeeded. On kFalse, utf8Out holds whatever arrived before
			the failure, so a caller that wants to report the partial result still can.
	*/
	bool16 ExportStory(const UIDRef& storyRef, std::string& utf8Out);
}

#endif // __KESCMStoryXml_h__

// End, KESCMStoryXml.h.
