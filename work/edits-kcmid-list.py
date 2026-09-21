#!/usr/bin/env python3
"""Add the removal to KCMID.h's submission increment list, and correct entry ㊼."""

PATH = r"C:\Users\user\Desktop\plugin_sdk_21.0.0.192\source\sdksamples\KCM\source\KCMID.h"

NEW = (
    "//   ㊽⛔★★★**Story の「戻す」系を全廃止**(2026-09-21・ユーザー指示「戻す、やり直しを全て無くす、"
    "コードも無くす、Story 単位のも」)。廃止したのは**「Restore Source Text」「Undo the Restore」"
    "「Restore All in This Story」の3項目**と、そのためだけにあった仕組み一式。"
    "理由はユーザーの言葉で「**ソースの文書が目の前にあるので、戻したければそこから取ればいい**」"
    "――㊼で Task Start がファイルの写しになり、Start がそれを窓で開くようになった日の決定。"
    "⇒ ★**KCM がメニューから読み手の本文を書き換える機能はゼロ**になり、書き込みは Import だけ。"
    "⚠**説明文に影響**――Import で入ったものを**1件だけ戻す道が無くなった**(Ctrl+Z で Import 全体を戻すか、"
    "Source の文書から取る)・**変更行の右クリックメニューは Story モードでは出ない**(2026-09-12 以前と同じ・"
    "Resources モードの「Edit...」は残る)・取り込み済みの行に付いていた**「=」の印が消えた**。"
    "★**コードは 4 ファイルが丸ごと消え**(KCMTableRestore / KCMStorySnapshot / KCMStoryRowMerge / KCMStoryUndoObserver)、"
    "KCMStoryRestore は 1,817行→302行(残したのは Import と PDF レポートが使う**書き込みの部品**だけ)。"
    "⚠**facade の5スロットと Change の6フィールドは抜け殻で据え置き**(KIDMCP が vtable で呼ぶ・構造体は値で渡す)。"
    "引退番号＝ActionID **+66 / +77 / +81**、ScriptID **'eKGk' / 'eKGb'**(どちらも Adobe 未登録)、"
    "PMID **+31 / +34**、IID **+16**、Impl **+60**。全文＝`docs/ai-notes/kcm-restore-retired-2026-09-21.md`。"
    "退避タグ＝`backup/2026-09-21-before-restore-removal`。"
)

OLD47 = ("★**Story の「戻す／やり直す」が2文書比較でも効くようになり**(2026-09-16 の「2文書では戻せない」を撤回)、"
         "★**「Restore All in This Story」が復活**(2026-09-20 に撤去したもの。ActionID は引退番号 +73 を避けて **+81**／"
         "「Restore All Stories」は撤去のまま)。")
NEW47 = ("⛔**「戻す／やり直す」は同日のうちに全廃止された**(→㊽。一度は2文書比較でも効くようにし、"
         "「Restore All in This Story」を +81 で復活させたが、どちらもその日のうちに撤去されている"
         "＝**提出説明には書かない**)。")


def main():
    with open(PATH, 'rb') as f:
        raw = f.read()
    bom = raw.startswith(b'\xef\xbb\xbf')
    text = (raw[3:] if bom else raw).decode('utf-8')

    if OLD47 not in text:
        print('FAIL: entry 47 not found as expected')
        return 1
    text = text.replace(OLD47, NEW47)

    lines = text.replace('\r\n', '\n').split('\n')
    hits = [i for i, ln in enumerate(lines) if ln.startswith('//   ㊼')]
    if len(hits) != 1:
        print('FAIL: entry 47 line not unique (%d)' % len(hits))
        return 1

    at = hits[0] + 1
    while at < len(lines) and lines[at].startswith('//'):
        at += 1
    lines[at:at] = [NEW]

    out = '\n'.join(lines).replace('\n', '\r\n').encode('utf-8')
    with open(PATH, 'wb') as f:
        f.write((b'\xef\xbb\xbf' if bom else b'') + out)
    print('ok: the new entry went in at line %d' % (at + 1))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
