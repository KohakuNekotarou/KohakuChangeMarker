//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMStoryXml.h for what this is for and what must not be passed to it.
//
//  Ported from KohakuTest's KTStoryXml::ExportToBytes on 2026-08-20 - the export itself, with
//  the reporting and the script-facing wrapper left behind. The one behavioural choice that
//  came with it: ExportInCopyInterchange rather than ExportPageitems. KT could do either
//  (its ExportMode picked between them); the InCopy form is the one that gives a story's text
//  as <Content> elements, which is what the diff reads.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IPMStream.h"
#include "ISnippetExport.h"

// General includes:
#include "StreamUtil.h"
#include "UIDList.h"
#include "UIDRef.h"
#include "Utils.h"

#include <string>

// Project includes:
#include "KCMMemXferBytes.h"
#include "KCMStoryXml.h"

/* ExportStory
*/
bool16 KCMStoryXml::ExportStory(const UIDRef& storyRef, std::string& utf8Out)
{
	utf8Out.clear();

	KCMMemXferBytes xferBytes;
	ErrorCode status = kFailure;
	{
		// ★takeOwnership and recycleBoss BOTH kFalse. The IXferBytes lives on this stack frame,
		//   and StreamUtil.h:248-250 warns that a recycled boss may hold on to it past the point
		//   where it has gone away.
		InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&xferBytes, kFalse, kFalse));
		if (stream == nil)
			return kFalse;

		status = Utils<ISnippetExport>()->ExportInCopyInterchange(stream, UIDList(storyRef));

		stream->Flush();
	}	// ★The stream is released HERE, before the bytes are read. Reading xferBytes while the
		//   stream still holds it would be reading something that is still being written to.

	if (xferBytes.GetData() != nil && xferBytes.GetSize() > 0)
		utf8Out.assign(xferBytes.GetData(), xferBytes.GetSize());

	return status == kSuccess;
}

// End, KCMStoryXml.cpp.
