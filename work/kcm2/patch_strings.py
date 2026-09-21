import sys; sys.path.insert(0,'work/kcm2')
from ed import load, save
BS = chr(92)          # a backslash, never written literally: the heredoc eats doubled ones
Q  = BS + '"'         # what an escaped quote looks like inside the .fr string
NL = BS + 'n'         # what a line break looks like inside the .fr string

p = 'ui/KCMUI_enUS.fr'
t, bom = load(p)

if '"Kohaku Paw Stamp",' in t:
    t = t.replace('"Kohaku Paw Stamp",', '"Kohaku Cat Paw",')
    print('  ok: tool name -> Kohaku Cat Paw')
elif '"Kohaku Cat Paw",' in t:
    print('  already: tool name')
else:
    print('FAIL: tool name not found'); sys.exit(1)

start = t.find('[Cat paw stamps')
end   = t.find(NL + NL + 'DISCLAIMER')
if start < 0 or end < 0 or end <= start:
    print('FAIL: paw section not found (start=%d end=%d)' % (start, end)); sys.exit(1)

new = ('[Cat paw stamps (Kohaku Cat Paw)]' + NL +
  'Put a paw-print landmark anywhere on a page. Hold the Kohaku Change Marker tool in the toolbox '
  '(or the tool button on the panel) to open its flyout, then pick the stamp tool.' + NL +
  '- Click to place; Shift+click to lift' + NL +
  '- Alt+click asks for a note first: type one line or several, and the words are drawn beside the paw' + NL +
  '- Shift+Alt+click SWAPS the colour between red and blue. It places nothing - it only changes what the next click puts down' + NL +
  '- A paw cannot be stacked where one already is' + NL +
  '- Paws need no comparison and survive Stop, exactly like checks' + NL +
  '- On screen always; in print and PDF only with ' + Q + 'Print comparison marks' + Q + ' on' + NL +
  '- PAWS AND CHECKS ARE WRITTEN INTO THE DOCUMENT, as script labels on the page that carries them. '
  'They go in the moment you make them, Ctrl+Z takes them back, and they come home when the document '
  'is opened again - so the file carries them to anyone else who has this plug-in' + NL +
  '- ' + Q + 'Clear Cat Paws in This Document' + Q + ' and ' + Q + 'Clear Checks in This Document' + Q +
  ' clear the active document of one kind each; ' + Q + 'Clear Marks from Document' + Q + ' clears both')

t = t[:start] + new + t[end:]
save(p, t, bom)
print('  ok: how to use (paw section) rewritten')
