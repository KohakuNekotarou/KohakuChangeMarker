import io, sys
TAB = chr(9)
p = 'source/KCMID.h'
raw = open(p, 'rb').read()
had_bom = raw.startswith(b'\xef\xbb\xbf')
t = raw.decode('utf-8-sig').replace('\r\n', '\n')
orig = t

def sub(old, new):
    global t
    if t.count(old) != 1:
        print('FAIL count=%d for: %s' % (t.count(old), old[:80]))
        sys.exit(1)
    t = t.replace(old, new)

# --- A. ClassID: the +33 comment had been left on the +34 line by 7c96cda; split them apart,
#        then add the new command boss.
sub('DECLARE_PMID(kClassIDSpace, kKCMStoryMarkerExpiryBoss, kKCMPrefix + 33)\n'
    'DECLARE_PMID(kClassIDSpace, kKCMAfterOpenResponderServiceBoss, kKCMPrefix + 34)' + TAB +
    '// IResponder for kAfterOpenDoc: puts back the ticks and paws the document carries as script labels (KCMDocResponder.cpp / KCMPageMarksDoc.h, 2026-09-07)' + TAB +
    '// IIdleTask: withdraws just the jump flash of the marker above after about a second (KCMStoryMarkerExpiry.cpp). It is on this side because the adornment starts and stops it; leaving it in the UI would invert the dependency.\n',

    'DECLARE_PMID(kClassIDSpace, kKCMStoryMarkerExpiryBoss, kKCMPrefix + 33)' + TAB +
    '// IIdleTask: withdraws just the jump flash of the marker above after about a second (KCMStoryMarkerExpiry.cpp). It is on this side because the adornment starts and stops it; leaving it in the UI would invert the dependency.\n'
    'DECLARE_PMID(kClassIDSpace, kKCMAfterOpenResponderServiceBoss, kKCMPrefix + 34)' + TAB +
    '// IResponder for kAfterOpenDoc: puts back the ticks and paws the document carries as script labels (KCMDocResponder.cpp / KCMPageMarksDoc.h, 2026-09-07)\n'
    'DECLARE_PMID(kClassIDSpace, kKCMSetPageMarksCmdBoss, kKCMPrefix + 35)' + TAB +
    '// ICommand (Command) + IKCMPageMarksCmdData: the ONE way a tick or a cat paw is written (KCMPageMarksCmd.cpp, 2026-09-07). It writes the pages script labels through IScriptUtils and then notifies on the DOCUMENTS subject, which is what makes the change undoable AND what puts the session store back on undo and redo. Nothing else may write those labels.\n')

# --- B. the stale "next free" note. It said +34 while +34 was already taken.
sub('Next new boss: +34.',
    'Next new boss: +36. WARNING: this line went stale the moment +34 was taken and was still saying +34 on 2026-09-07. COUNT, do not read: grep "DECLARE_PMID(kClassIDSpace" in this file and take the largest, then check the retirement notes above and below it.')

# --- C. InterfaceIDs: the free-range note was wrong (+10 is IID_IKCMSTORYMARKFACADE in
#        KCMBoundaryID.h), and the three new model-only IIDs go in.
sub('// +10..+25 are free.',
    '// +10 is NOT free: IID_IKCMSTORYMARKFACADE took it in KCMBoundaryID.h (this line said "+10..+25\n'
    '//   are free" until 2026-09-07, which would have handed out a colliding number).\n'
    '// +14..+25 are free. COUNT before taking one: the facade IIDs live in KCMBoundaryID.h, not here,\n'
    '//   so the largest number in THIS file is not the largest number in use.\n'
    '//\n'
    '// The three below are MODEL-ONLY, which is why they are here and not in KCMBoundaryID.h: the UI\n'
    '// neither sends nor receives any of them. Keeping them out of the boundary header is what stops\n'
    '// the two copies of that file having to be edited in step for a change the UI cannot see.\n'
    'DECLARE_PMID(kInterfaceIDSpace, IID_IKCMPAGEMARKSCMDDATA, kKCMPrefix + 11)' + TAB + '// what kKCMSetPageMarksCmdBoss is told to write (KCMPageMarksCmd.h). Non-persistent: it is a parameter, not document data.\n'
    'DECLARE_PMID(kInterfaceIDSpace, IID_IKCMPAGEMARKS, kKCMPrefix + 12)' + TAB + '// the PROTOCOL of the notification the command raises on the documents subject. It names no interface -- a protocol IID is a filter, and this one means "the ticks or the paws of this document changed".\n'
    'DECLARE_PMID(kInterfaceIDSpace, IID_IKCMMARKSOBSERVER, kKCMPrefix + 13)' + TAB + '// the observer that listens for it, AddIn on kDocBoss (KCMMarksObserver.cpp). It has an IID of its own because kDocBoss already carries somebody elses IID_IOBSERVER.\n')

# --- D. ImplementationIDs: same comment mixup at +52/+53, then the three new ones.
sub('DECLARE_PMID(kImplementationIDSpace, kKCMStoryMarkerExpiryImpl, kKCMPrefix + 52)\n'
    'DECLARE_PMID(kImplementationIDSpace, kKCMAfterOpenResponderImpl, kKCMPrefix + 53)' + TAB +
    '// IResponder implementation (KCMDocResponder.cpp)' + TAB +
    '// IIdleTask implementation (KCMStoryMarkerExpiry.cpp). Withdraws the jump flash after about a second.\n',

    'DECLARE_PMID(kImplementationIDSpace, kKCMStoryMarkerExpiryImpl, kKCMPrefix + 52)' + TAB +
    '// IIdleTask implementation (KCMStoryMarkerExpiry.cpp). Withdraws the jump flash after about a second.\n'
    'DECLARE_PMID(kImplementationIDSpace, kKCMAfterOpenResponderImpl, kKCMPrefix + 53)' + TAB +
    '// IResponder implementation (KCMDocResponder.cpp)\n'
    'DECLARE_PMID(kImplementationIDSpace, kKCMSetPageMarksCmdImpl, kKCMPrefix + 54)' + TAB + '// the Command itself (KCMPageMarksCmd.cpp).\n'
    'DECLARE_PMID(kImplementationIDSpace, kKCMPageMarksCmdDataImpl, kKCMPrefix + 55)' + TAB + '// its parameter interface, on the same boss (same file). Non-persistent.\n'
    'DECLARE_PMID(kImplementationIDSpace, kKCMMarksObserverImpl, kKCMPrefix + 56)' + TAB + '// the lazy observer AddIn on kDocBoss (KCMMarksObserver.cpp).\n')

sub('Next new implementation: +53.',
    'Next new implementation: +57.')

if t == orig:
    print('FAIL: nothing changed'); sys.exit(1)
out = t.replace('\n', '\r\n').encode('utf-8')
if had_bom:
    out = b'\xef\xbb\xbf' + out
open(p, 'wb').write(out)
print('KCMID.h patched OK')
