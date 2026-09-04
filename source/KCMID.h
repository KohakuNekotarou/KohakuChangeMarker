//========================================================================================
//  
//  $File: $
//  
//  Owner: 
//  
//  $Author: $
//  
//  $DateTime: $
//  
//  $Revision: $
//  
//  $Change: $
//  
//  Copyright 1997-2012 Adobe Systems Incorporated. All rights reserved.
//  
//  NOTICE:  Adobe permits you to use, modify, and distribute this file in accordance 
//  with the terms of the Adobe license agreement accompanying it.  If you have received
//  this file from a source other than Adobe, then your use, modification, or 
//  distribution of it requires the prior written permission of Adobe.
//  
//========================================================================================


#ifndef __KCMID_h__
#define __KCMID_h__

#include "SDKDef.h"

// Company name, display name, version and the Alt-key label are in KCMBoundaryID.h, because both
// .pln files have to report the same values. What is left here are the names only this half owns;
// the UI half keeps its own in KCMUIID.h.

// Plug-in:
#define kKCMFileName		"KohakuChangeMarker"			// Base name of the built file (OriginalFilename in the .rc). Must match TargetName in the vcxproj. Unlike the display name it carries no spaces.

// ID PREFIX. Adobe registered a range, not a single value, and quoted the request back verbatim:
//
//     "Following Prefix ID has been registered as per your request below : 0x1EA500 - 0x1EA5FF ."
//
// So there are 256 slots, and how they are divided is up to us. They are split between the two
// halves of this product:
//     0x1EA500   model (KohakuExtendScriptChangeMarker)   offsets +0..127
//     0x1EA580   KohakuChangeMarkerUI (KCMUI)             offsets +128..255
// Splitting one range across a model and a UI plug-in is Adobe's own practice: customdatalink
// (0xb3300) and customdatalinkui (0xb3380) both sit inside 0xb33xx. Uniqueness is decided per
// value, not per plug-in.
// The 256 slots are counted PER ID SPACE - widgets and actions have separate budgets.
// The macros themselves (kKCMPrefixNumber / kKCMPrefix / kKCMStringPrefix) are in
// KCMBoundaryID.h, so the UI half's copy cannot drift away from this one.
// The version number is defined in KCMBoundaryID.h so that both .pln files report the same one.
// What stays here is the block below: the version history and the increment list for the next
// submission. It is the master copy of the Adobe Exchange submission notes and is kept in
// Japanese, because that is the language they are submitted in.
//#define kKCMVersion		"1.5.0"						// Version of this plug-in。About ボックス本文・.rc の FileVersion・PluginVersion リソースの3か所に出る。1.0.1 → 1.1.0(2026-07-25) → 1.1.1(2026-07-26) → 1.2.0(2026-07-30) → 1.2.1(2026-08-06) → 1.3.0(2026-08-07) → 1.3.1(2026-08-07) → 1.4.0(2026-08-09) → 1.5.0(2026-08-15) → 1.6.0(2026-08-19) → **2.0.0(2026-08-21)**。
														// ★1.4.0 で minor を上げた理由 = **新機能 Story Edits**(パネル下部に開閉セクションを設け、変更のあったストーリーを一覧する)。1.2.1→1.3.0 のときと同じ基準＝機能追加が入るなら patch では足りない。★**2026-08-10 に段階4(ジャンプ)まで完成**(下記③)。
														// ★★**1.5.0 で minor を上げた理由(2026-08-15 ユーザー決定)** = **model/UI 分割の完了**で **PDF 書き出しに比較マークが出るようになった**(下記⑧)＋**Story の変更カウンターをスクリプトに公開**(下記⑨)。
														//   ⚠**1.4.0 は提出しないまま 1.5.0 へ繰り上げた**(1.2.1→1.3.0 と同じ形)。∴ 下の増分リストは**公開版 1.3.0 から見た全部**で、①〜⑦は 1.4.0 のときのまま据え置き。
														//   ★このとき下の「■■【1.4.0 = 次に提出する分】」の見出しも 1.5.0 へ書き換えてある＝**見出しの版数と kKCMVersion は必ず一致させること**(ずれると「どれを提出説明に使うか」が読めなくなる)。
														// ★★★**1.6.0 で minor を上げた理由(2026-08-19)** = **比較マークが PDF 1.3(Acrobat 4)でも半透明で出るようになった**(下記⑩)。⚠**日本の入稿プリセットはほぼ 1.3**(PDF/X-1a・X-3・雑誌広告送稿用＝すべて Acrobat 4)なので、実務では 1.4 より効く。
														//   ⚠**1.5.0 も提出しないまま 1.6.0 へ繰り上げた**(1.4.0→1.5.0 と同じ形)。∴ 下の増分リストは**引き続き公開版 1.3.0 から見た全部**で、①〜⑨は据え置き。
														// ★★★**2.0.0 で major を上げた理由(2026-08-21 ユーザー判断)** = **2つ目の比較モード「Story Changes」**(下記⑬)。1.x は「ページを画素で比べる道具」の一本道だったが、**本文そのものを比べる道具が並んだ**＝製品の性格が変わったので minor では足りない。
														//   ⚠**1.6.0 も提出しないまま 2.0.0 へ繰り上げた**(1.5.0→1.6.0 と同じ形)。∴ 下の増分リストは**引き続き公開版 1.3.0 から見た全部**で、①〜⑫は据え置き。
														//   ★★**提出の時期＝「この機能が完成した時点」**(2026-08-21 ユーザー明言)＝Story Changes は**まだ第1段**なので、**今すぐ出さない**。⇒ 第2段(Scrollbar Map / ページパネル / Export Changed Pages への供給。設計書 §13)と、旧側の窓を対応する文字まで寄せる件が片付いてから。
														// ★Adobe Exchange の公開版は **1.3.0**(2026-08-07 承認・公開)。1.2.1 は提出しないまま 1.3.0 へ繰り上げた(機能追加が入ったので patch では足りない)。
														// ★★**「版数が◯◯だった時期にコードへ入れた」ことと「提出した◯◯のビルドに入っている」ことは別物**。取り違えると提出説明を誤る(2026-08-07 に実際に踏んだ)ので、増分は**提出したビルドを境に**2段へ分けてある。★提出文を起こすときは【次に提出する分】だけを読むこと。
														//
														// ■■【2.0.0 = 次に提出する分】★公開版 1.3.0 から見た増分。**提出説明はここだけを使う**。(版数は 1.3.1→1.4.0→1.5.0→1.6.0→2.0.0 と繰り上げ。①〜⑦は 1.4.0 のとき、⑧⑨は 1.5.0 のときと同一で、2026-08-19 に⑪・2026-08-20 に⑫・2026-08-21 に⑬が加わった。⚠**⑩は「設定が初期化される」という注意書きで、機能の増分ではない**＝番号を振るときに数え間違えやすい。★**major を上げた理由は⑬**＝2026-08-21 ユーザー判断)
														//   ①パネルにツール切替ボタン(kKCMToolButtonWidgetID) = 押すとツールボックスの琥珀のツールがアクティブになる。絵はツールボックスと同じリソースを参照。★押下表示はツールボックスと双方向に同期する(状態を書くのは ITool::Select/Deselect の1か所だけなので、どちらから選んでも食い違わない)。How to Use の冒頭も「ツールボックス、またはパネルのツールボタン」へ追随済み。
														//   ②半透明パネルの「不透明に戻す」判定を変更 = カーソルがパネルの矩形の中にある限り、その上にフライアウト・子メニュー・ツールチップが出ていても不透明のまま。公開版 1.3.0 は自分の窓が上に出ると薄くなった(KBS と同じ判定へ揃えたもの)。
														//   ③★★**Story Edits(パネル下部の開閉セクション) = minor を上げた理由。★2026-08-10 に完成(段階1〜4)＝提出説明に書いてよい。**
														//     何をするものか: 画素比較は「このページは違って見える」までしか言えない。Story Edits は Target と Source の各ストーリーの変更カウンター(ITextModel の4本)を突き合わせ、**変更のあったストーリーを一覧**して「テキストが変わったのか・書式だけか・表などが変わったのか」を区別する。行はページ順で、左に本文の先頭、右に種類(Text / Attr / Other / Added)。
														//     ⚠**数字(4->6 のようなカウンター値)は出さない** = カウンターは編集回数ではなく状態のバージョン番号なので、差の大きさに人向けの意味が無い(2026-08-08 実測)。
														//     行の操作: **単クリック=そのストーリーの先頭フレームを画面中央に出す**(Source 窓と Pages パネルは Prev/Next と同じ流儀で連動)。**ダブルクリック=そのストーリーの全文を選択する**(2026-08-10 ユーザー指示で「先頭にキャレット」から変更＝行は「このストーリーが変わった」という報告なので、次にやりたいのはコピー・書式変更・差し替えのいずれかで、そのどれもが選択で足りる)。⚠ダブルクリックは**アクティブツールを文字ツールに変える**(琥珀のツールは外れる)＝選択しても操作できなければ意味が無いため。How to Use にも明記済み。
														//     一覧の見出し: **UID / Story / Change の3列**(2026-08-10 ユーザー要望)。★行のセルと同じ Frame・同じ binding を .fr で与えることだけが列の揃いを保証している。行の間の区切り線は**フレームワークが描くものをそのまま残している**(DVTreeNodeControlView は行高14px以上なら自動で描く)。⚠**2026-08-11 に一度消す実装(kKCMStoryRowViewImpl)を入れ、同日ユーザー判断で撤去した＝線は「ある」**——旧記述の「消してある」は誤り(2026-08-11 ブロック15 監査 D-1 で訂正)。経緯は下の Impl +28 のコメントと KCM.fr の行 boss。
//     現況: **段階1〜4 すべて完了**(2026-08-10)。計画=docs/superpowers/plans/2026-08-09-kescm-story-edits-stage3.md と ...-2026-08-10-kescm-story-edits-stage4.md／設計=docs/superpowers/specs/2026-08-09-kescm-story-edit-section-design.md
														//     ⚠**パネルが 153→185px 高くなっている**(開閉ボタンの帯 20px ＋ 猫イラストを収めるための 12px。2026-08-10)。これは公開版 1.3.0 から見て目に見える変更なので、提出説明に書く価値がある。★同時に**ステータス欄が右端まで広がった**(180→216)＝猫が下の帯へ移った分。
//     ★★2026-08-11 の仕上げ2件(どちらも公開版 1.3.0 から見て目に見える): ①**一覧の本文が途中で切れなくなった** = モデル側にあった30文字上限を撤去し、省略はセルの kEllipsizeMiddle に一任した(パネルを広げた分だけ本文が伸びる)。旧実装では "STORY A bottom right of page 1" でちょうど切れ、続く ", EDITED." が出ていなかった。②**ステータス欄が5px高くなった**(下端 145→150) = 日本語UIのパレットフォントは1行 17.9px(実測)で4行に 74px 要るのに枠が 69px しかなく、**4行目が約5px 切れていた**。どちらも「表示が欠けていたのを直した」ので、提出説明では機能追加ではなく修正として書く。
														//     ★★★2026-08-22 の絞り込み＝**書式だけが動いたストーリーは一覧に出さない**(ユーザー指定「属性の変更は無視」)。フォント・色・段落スタイル・表の罫線を変えただけの行は、本文を読みに来た人の前ではノイズになるため。⇒ **一覧に出るのは3種**＝①本文が変わったストーリー ②ルビが変わったストーリー ③ストーリーごと増えた・消えたもの(Added / Deleted)。⚠**圏点(けんてん)は 2026-08-22 に一度入れて 08-23 に取りやめた**(ユーザー決定「ストーリーモードの StoryEdit にでるのは、テキストの変更と、ルビだけで」)＝**提出説明には書かない**(公開版には一度も出ていないので、増えても減ってもいない)。⚠**Pixel Changes モードではルビだけの変更は出ない**＝ルビは「属性」なので、本文を突き合わせない Pixel モードではカウンター上「書式が動いた」としか読めず、**フォントを変えただけの行と見分けがつかない**(2026-08-22 ユーザー判断＝Pixel では諦めて Text の変更だけを出す)。★この絞り込みは Story Edits と同じく**公開版 1.3.0 には存在しない機能の仕様**なので、リリースノートに「挙動を変えた」とは書かない ＝ ③の説明としてこう書く。
//   ④★★**マスターページも比較するようになった**(2026-08-11。公開版 1.3.0 から見て新機能)。従来は通常スプレッドだけが比較対象で、マスターページの変更は枠に出なかった(あふれ「+」だけは出ていた)。マスタースプレッドは**名前**(A-親ページ 等)で Target/Source を対応付ける＝順番で組むと、片方にマスターが1つ増えただけで別のマスターどうしを比べてしまうため。片方にしか無い名前のマスターは比較しない。あわせて**スクロールバー地図が表示中のスプレッドに追従**するようになり(マスター表示中はそのマスターの内容だけを出す。マークが無ければ空)、**Prev/Next の巡回にもマスターページの枠が入る**。★**「Sync Layout Views」と「Align Other Views to Active」もマスターに対応**(同日追加)＝一方の窓でマスタースプレッドを表示すると、もう一方の窓も**同じ名前のマスターへ移動して同じ位置を映す**(スクロールだけでは別スプレッドへ届かないので、追従側のスプレッドも切り替える)。**相手側にそのマスターが無ければ追従側は動かさない**(Added ページと同じ扱い)。★**TSV 出力(Export Changed Pages)もマスターページを出す**(2026-08-11 に対応。⚠旧記述「TSV だけは通常ページのみ」は失効)＝Page 列にマスタースプレッド名(「A-親ページ」。見開きマスターは「A-親ページ (2)」のようにスプレッド内の位置を添える)を出し、種別は Changed。⚠**挿入/削除(Inserted/Deleted)は通常ページだけ**＝マスターは名前で対応付けるので、片方にしか無いマスターはそもそも比較対象にならず「増えた/減った」という状態を持たない。★★**2026-08-16: 旧版べた載せ(Shift+左ドラッグ)と CMYK 色サンプリング(Alt+左)もマスタースプレッドで使えるようになった**＝マスター対応から漏れていた最後の2つ。比較・Refresh・Sync・TSV・Prev/Next は 2026-08-11 に対応したが、この2つだけは**マウスの下のページを探す経路**が通常スプレッドしか見ていなかった(マスターを表示していると、同じ座標にある通常ページの方が先に当たる)。⇒ **表示中のスプレッドの中だけを見る**形にして解消。実機確認済み。
//   ⑤★★**ブック比較(Compare Books) = パネルのフライアウトに追加した新機能**(2026-08-11。公開版 1.3.0 から見て新機能)。開いている2つのブックを**章単位**で比較し、章ごとに「変わった/変わらない」を出す。Target は**ブックパネルで前面タブのブック**、Source はそれ以外で最初に開いているブック——ダイアログの上2行に両方の名前が出るので、押す前に対象を確かめられる。結果は章名と判定(Changed / NoChange / ChapterAdded / ChapterDeleted / Failed)の一覧。★判定は**章の中で最初の違いが見つかった時点で打ち切る**(ページ数が違えばページを1枚も開かずに Changed)。章は**裏で1つずつ開いて閉じる**ので、文書を開いたままにしないし dirty にもしない。ダイアログは**モードレス**＝開いたまま文書を触れる。
														//     ★**進捗バーとキャンセルを入れた(2026-08-12。段階3 完了)＝この機能は提出説明に載せてよい。** 比較の間は進捗バーが出て、いま見ている章の名前を表示し、いつでもキャンセルできる(実測: 15章のブックで押した時点から 0.2〜0.5 秒で止まる)。**キャンセルしても、そこまでに判定できた章の結果は残る**——見ていない章は一覧に「NotCompared」と出て、要約の末尾が「- cancelled」になる。⚠**「NotCompared」と「NoChange」を混ぜないこと**が眼目で、中断した章を「変更なし」と報告すると、確かめていないものを確かめたと言うことになる。★あわせて**要約の文言を短くした**(「book compare: 15 chapters, ...」→「15 chapters: ...」・0件の項目は出さない)＝ステータス欄は中央で省略されるので、長いと **"book co...5 chapters" のように数字が壊れて別の数に見える**(2026-08-12 実測)。
														//     (旧記述「進捗バーとキャンセルはまだ無い。押すと固まると受け取られるので提出説明に載せない」は解消済み。実測では 100ページのブックに約3秒、変更の無い200ページで十数秒かかっていた。)
														//     ⚠**版数は 1.4.0 のまま**でよい。1.4.0 は**まだ提出していない**ので、公開版 1.3.0 から見た増分がこのリストに増えるだけ(1.2.1→1.3.0 のときと同じ考え方＝提出していない版数は繰り上げずに中身を足す)。
//   ⑥内部の安全修正だけ(2026-08-12)。★**提出説明には書かない** ＝ 画面にも操作にも見える変化が無い。ここに残すのは、次に差分を洗う人が「これは説明に要る変更か」を毎回考え直さずに済むようにするため。中身は2つ＝①**終了時に半透明トグルの購読を外す**(KCMDetachPanelVisibilityObserver 新設。購読している間セッションが握るのはこの .pln の中へのポインタで、終了処理中のパネル破棄は実際に通知を飛ばす) ②**Win32 フックを外せなかったときハンドルを捨てない**(UnhookWinEvent が失敗する条件は3つあり、うち1つではフックが生きたまま残る＝捨てると二度と外せない)。★どちらも **KBS が先に直していて、こちらへ歩いてこなかった分**(KBS ブロック14 の3周目が兄弟報告として検出し、同日ユーザー指示で移植)。
//   ⑦UI の細部2件(2026-08-12 ユーザー指定。どちらも公開版 1.3.0 から見て目に見えるが、**提出説明に書くほどではない**)。①**Story Edits の分割バーをドラッグで動かせなくした** ＝ 上ペインは固定座標のコントロールの塊で正しい高さが1つしか無く(Top snap がその高さ)、下げられる方向だけが残っていて、下げると下に何も無い帯ができた。セクションの高さはパネルの縁のドラッグで決まる。②フライアウトの「Compare Books」を**Start の直下**へ(9.54→9.05)＝比較を始める項目を1つの群にまとめた。あわせて Hide Unchanged Spreads と**同じ位置番号 9.54 で重複していた**のも解消。
														//   ⑧★★★**PDF 書き出しに比較マークが出るようになった**(2026-08-15。公開版 1.3.0 から見て**いちばん大きな変化**)。従来は「Print comparison marks」を ON にしても、**File ▸ Export ▸ Adobe PDF で書き出した PDF には1画素も出なかった**(印刷には出ていた)。⇒ プラグインを **model と UI の2本に分割**し(KohakuChangeMarker.pln ＋ KohakuChangeMarkerUI.pln)、比較マークの描画をバックグラウンドスレッドから届く側へ移した。★**利用者から見た使い方は何も変わらない**——インストールは従来どおり両方を入れるだけで、パネルもメニューも同じ。
														//     ⚠**同時に PDF 書き出し用の描き方も作り直した**(2026-08-16 に完成)＝**印刷と PDF 書き出しを同じ処理に統一**し(アルファサーバ＋透明グループ)、**マークを CMYK で塗る**ようにした ⇒ **[PDF/X-1a] で書き出しても「非 CMYK カラー」の警告が出ない**。**印刷経路は従来どおり**。⚠★★**旧「提出説明に必ず書く要点＝書き出しの互換性は Acrobat 5(PDF 1.4)以上にすること」は 2026-08-20 に撤回した**＝下の⑪⑫で PDF 1.3 でも正しく半透明になったため。How to Use の但し書きも日英とも撤去済み。⚠★**旧記述「PDF 書き出しのポートは透明を一切通さないので、リングをベクターで塗り不透明度を色に溶かす」は 2026-08-16 に撤回した**(欠けていたのは公式が要求する追加初期化＝透明グループだけだった)＝**2026-08-17 の不具合再検査 B3 でここへ反映**。
														//   ⑨★★**ストーリーの変更カウンターをスクリプトから読めるようにした**(2026-08-15 ユーザー要望)＝`app.documents[0].stories[2].kcmChangeCount` ほか3本(`kcmTextChangeCount` / `kcmAttrChangeCount` / `kcmOtherChangeCount`)。すべて**読み取り専用の整数**。
														//     何の役に立つか: **Story Edits に行が出るかどうかを決めているのが、この集約カウンターの一致/不一致**。読めるようにしたことで「一覧が空なのは変更が無いからか、それとも見落としか」を**利用者自身が確かめられる**。⚠**カウンターは編集回数ではなく状態のバージョン番号**なので、**別々に作った2つの文書は、中身が違っても同じ値を持つことがある**——Story Edits が意味を持つのは「元の文書を別名保存して片方を編集した」対に対してであり、この4本はそれを外から確認する手段でもある。
														//     ⚠**KCM の「メソッドは公開しない」方針は変えていない**(足したのは読み取り専用プロパティだけ)。既に DOM へ公開している `app.kcmStatus` / `app.kcmBookResult` と同じ性格。⚠**ただし `app.kcmBookResult` は「公開済み」ではない**＝2026-08-11 追加で、**提出した 1.3.0 のビルドには入っていない**(このファイル冒頭が警告している取り違えを、この行自身がしていた＝2026-08-18 の再検査 B11 で訂正)。
//     ⑨-b ★★**`app.kcmBookResult`(2026-08-11)も 1.3.0 から見た増分**＝⑤のブック比較の結果を**章ごと1行のタブ区切り**でスクリプトから読める読み取り専用プロパティ。⚠**旧記述はこれを増分に数え落としていた**（⑤にも⑨にも無かった）＝**機能を列挙する説明文は、対応を1つ足すたびに列挙そのものを数え直す**(④の TSV で 2026-08-11 に踏んだのと同じ抜け方)。
//     ⑨-c ★★**ストーリーの4カウンターは IDML に書き出されない**(2026-08-18)＝スクリプトからは読めるまま、**利用者が書き出す IDML には一切現れない**。プロパティは既定では IDML の属性になる(実測で全 `<Story>` に載っていた)ので、INX 専用のスクリプトリソースで明示的に除外した。**提出説明に書く必要は無い**(利用者から見て何も変わらない)が、**IDML を扱う利用者にとっては「自作プラグインがファイルを汚さない」保証**なので、訊かれたら答えられるようにここに残す。
														//   ⑩⚠★★**利用者の設定が初期化される**(1.3.0 からの更新時に1回だけ)。**リリースノートに必ず書く**——書かないと不具合として報告される種類の変化: ①**キーボードショートカットの割り当て** ②**パネルの配置**(表示されない場合はウィンドウメニューから開き直す) ③**Story Edits セクションの高さ** ④**ツールの選択状態**。いずれも UI 側が別プラグインになり ID が振り替わったため。**比較の結果や保存した Check/Register のデータには影響しない。**
														//   ⑪★★★**比較マークが PDF 1.3(Acrobat 4)でも半透明で出るようになった**(2026-08-19。**1.6.0 で minor を上げた理由**)。
														//     何が変わったか: 従来は **PDF 1.3 で書き出すと、透明効果を1つも含まないページだけマークが「真っ赤なベタ塗り」になり、下の誌面が見えなくなっていた**(透明を含むページでは正しく半透明だった)。1.6.0 ではどのページでも半透明で出る。★**日本の入稿プリセットはほぼ 1.3**(PDF/X-1a・PDF/X-3・雑誌広告送稿用＝すべて Acrobat 4)なので、実務ではここが効く。
														//     ⚠**利用者から見た操作は何も変わらない**(トグルも設定も増えていない)。「1.3 で書き出しても枠が正しく出る」とだけ書けばよい。
														//     ★実測(同一文書・同一プリセット `[雑誌広告送稿用]`・透明を含まないページ)＝**全面ベタ 850,175 画素 → 半透明のリング 74,503 画素**。同期書き出しと UI の書き出し(バックグラウンド)の両方で確認。
														//     ⚠★★★**2026-08-20 訂正＝⑪だけでは半分しか直っていなかった。** 上の実測は「**透明を含まないページ**」で採ったが、その**文書には別のページに透明があった**。**文書全体で透明が1つも無い**と、⑪を入れても全面ベタのまま出る(手でも再現)。⇒ 下の⑫で解消。**「ページに透明が無い」と「文書に透明が無い」を測り分けていなかったのが誤りの正体。**
														//   ⑫★★★**透明を1つも含まない文書でも PDF 1.3 で半透明になった**(2026-08-20。⑪の残り半分)。**入稿用の文書はむしろ透明を1つも使わないのが普通**なので、実務ではここまで揃って初めて効く。
														//     原因: フラットナ(透明の平坦化)は「文書に透明がある」ときしか動かず、その判定は **`IXPManager` の「透明を持つページアイテムの一覧」** だけで決まる。KCM のアドーンメントは**セッションに登録するだけでどのアイテムにも属さない**ので、その一覧に自力では入れなかった ⇒ 申告口(⑪)を持っていても**誰も聞きに来ない**。
														//     直し: 公式サンプル `transparencyeffect` と同じ通知(`ItemXPChanged`)を、**文書を1バイトも変えずに**出す(`IDataBase::SaveRestoreModifiedState` で dirty も戻す)。★**利用者から見た操作は何も変わらない**。
														//     ★実測＝`red 400,404 画素(全面ベタ) → 0 画素・淡赤 30,155 画素`。害の対照も測った＝**一度も比較していない書き出しと Stop 後の書き出しが4ページとも差分0画素・PNG の SHA256 も一致**(申告を上げっぱなしにして誌面の色を変える逆方向の事故は無い)。
														//     ⚠残る制限＝**ページアイテムが1つも無いスプレッド**(一覧に載せる代表が取れない)。
														//     ⇒ ★**How to Use の「互換性は Acrobat 5(PDF 1.4)以上に」という但し書きは撤去した**(日英とも)。⑪⑫の後は 1.3 でも正しく出るため。
														//   ⑬★★★**2つ目の比較モード「Story Changes」を足した**(2026-08-21。**2.0.0 で major を上げた理由**。フライアウトの Compare mode > Pixel Changes / Story Changes で相互排他)。
														//     何をするものか: 従来の比較(Pixel Changes)は**ページをラスタ化して画素を比べる**ので「このページは違って見える」までしか言えない。Story Changes は**本文そのものを差分**し、Story Edits を**親＝ストーリー / 子＝変更箇所**の2階層ツリーで出す。子の行には**変更された文字だけを通常色**で、前後の文脈を薄く出し、右端に **`+`(追加) / `-`(削除) / `≠`(置換)** を添える。
														//     行の操作: **シングルクリック＝新旧どちらの窓もその箇所へ移動し、変わった文字に一時マーカーを出す**(選択もツールも奪わない。マーカーは数秒で消える)。**ダブルクリック＝その文字を選択する**。↑↓ でも変更箇所へ飛ぶ。旧側の本文はパネルのメッセージ欄に `Source Text:` / `Target Text:` として出る。
														//     ★★**ストーリーの行を右クリックすると「Refresh Story Comparison」**(2026-08-21)＝**その1本のストーリーだけ比較を取り直す**。比較は走った瞬間の写真なので、パネルを開いたまま本文を直していくと行は直す前の姿を出し続ける——これは**文書全体を比較し直さずに1行だけ最新にする**ための項目。★**行に出ている本文の書き出しも、クリックで飛ぶ先も一緒に取り直す**（⚠一覧の**並び順**だけは次の比較まで元のまま＝1行だけの更新で全体を並べ替えないため）。★**直し終えた行は子(変更箇所)が消え、行自体は残って Change 列が `None` になる**(＝「本文には差が無い」。⚠「変更なし」ではない＝カウンターは動いている)。★**`None` は初回の比較でも出る**＝書式や表だけが変わって本文は同じだった行。⚠**Story モードのときだけ**の項目(Pixel モードでは灰色＝項目が1つなのでメニュー自体が出ない)／**子の行では右クリックしても何も出さない**(1つの差分を指しているのにストーリー全体への操作を出すのは筋が違うため)。
														//     ★**今どちらのモードかはパネルのタブに出る**＝「Kohaku Change Marker - Pixel」/「- Story」。★**モードは Save Panel Settings に載る**ので次の起動でも同じモードで始まる。
														//     ⚠**Story モードでは比較の枠(リング)を描かない**(本文の差分にページの枠は対応しないため)。**灰色になるのは `Hide Unchanged Spreads` の1つだけ**＝スクロールバー地図・Export Changed Pages・Print marks・opacity・Find Overset・ページの✓・登録ページは**両モードで生きている**。
														//     ★★**大きい文書ほど速い**＝40ページ/120ストーリーで1本だけ変更した対の実測が **Story 216〜368ms ⇔ Pixel 2,126〜2,749ms**(約10倍)。変更の無いストーリーは突き合わせる前に落とすため。
														//     ★**編集数に上限は無い**(2026-08-21 に差分エンジンを線形空間 Myers へ入れ替えた＝メモリが O(D²)→O(N+M))。実測＝2,500段落を全部書き換えた対で `edits=2500` を **796ms**・メモリ増加なし。⚠**旧実装は 2,000 編集で詳細を諦めていた**。
														//     ⚠**まだ第1段**＝**旧側の窓は「同じストーリー」までで、対応する文字までは寄せていない**。★総合回帰は全項目 PASS＝`docs/ai-notes/kescm-story-change-mode-task9-regression-2026-08-21.md`。
														//     ★★**提出は「この機能が完成した時点」**(2026-08-21 ユーザー明言)＝**第1段のままでは出さない**。⇒ 第2段(設計書 §13)を済ませてから 2.0.0 を Exchange へ。
														//   ⑭★★**マークを「押している間」だけでなく「常に」出せるようにした**(2026-08-22)。フライアウトに **「Always Show Marks on Target」** を新設(「Always Show Marks on Source」の直上)。★**この2項目は 2026-08-25 に改名した**＝旧 "Show Marks on Target"/"Show Marks on Source"。旧名は**既定の挙動(ツールの左ボタンを押している間だけ出る)との違いが読めなかった**ので、"Always" を足して「常に出す」ことを名前に出した。⚠**"Hold" は名前に入れていない**＝押下の身振りは**このトグルと無関係に常に効き、しかもトグルの状態を反転する**(⑮)ため、名前に入れると「このトグルの機能＝Hold」と読めてしまう。⚠**公開版 1.3.0 にはこの2項目自体が無い**ので、提出説明では新名だけを書けばよい。ON の間は、KCM ツールで左ボタンを押さなくても Target 文書のマークが出たままになる。★**Pixel では比較の枠、Story では変更した文字に敷く色地**＝**同じトグルが両モードで意味を持つ**(描く機構は別々だが、ユーザーから見た意味は1つ)。⚠**画面だけ**＝Target 文書が印刷/PDF に何を出すかは従来どおり「Print comparison marks」が単独で決める(Source 版が印刷にも出るのとは非対称・意図的)。
														//     ★★★**Story Changes モードのマークを新設**＝**変更・追加された文字そのものに色地を敷いて**見せる(蛍光ペンと同じ見え方。★2026-08-24 に反転から色地へ変えた＝反転は紙に出せないため)。**枠ではない**ので **拡大率で大きさが変わらず、ページへの外枠も要らない**(2026-08-22 ユーザー指定)。見え方はジャンプ時の一時マーカーと同じ(同じグローバルテキストアドーンメントが描く)なので、**縦組み・回転フレーム・連結した段にもそのまま乗る**。出し方は2通り＝**トグルで常時**、または**ツールで左ボタンを押している間だけ**(その場合は押した窓の側だけ)。不透明度はパネルの「Marks opacity 25% / 75%」に連動する。
														//     ★**Added / Removed のストーリーは全文に色地が付く**(相手の版に無いので本文の差分が取れず、全部が新しい／全部が消えたことになるため)。⚠**削除された文字は旧版にしか残っていない**ので、旧版の窓でしか見えない。
														//     ★★**「Always Show Marks on Source」が Story モードでも効くようになった**＝Target と Source を**両方 ON にすると新旧の窓に同時に**マークが出る。
														//     ⚠★★**公開版からの挙動変更が1つ**＝**「Always Show Marks on Source」が Start のたびに ON へ戻らなくなった**(2026-08-22 ユーザー判断)。**両トグルとも既定 OFF で、Start は触らない。** 理由＝この設定は Save Panel Settings に保存され**起動時に自動で復元される**ので、Start が上書きすると**保存した選択が比較のたびに消える**。⇒ リリースノートに書く価値がある(公開版 1.3.0 を使っている人には「Start したら Source にも枠が出ていた」が既定だったため)。
														//   ⑮★★★**ツールの左ボタンの意味を1つの規則にまとめ、「Hold to Hide Marks」トグルを撤去した**(2026-08-22 ユーザー決定)。新しい規則＝**押している間は、その窓のマークが反対になる**＝**出ていなければ出る／出ていれば隠れる**(隠れる側は、マークをどけて素の紙面を確かめるため)。両方の比較モードと、Target/Source どちらの窓でも同じ。
														//     ⚠★★**公開版からの挙動変更**＝1.3.0 の「Hold to Hide Marks」は「枠を常時表示し、押している間だけ隠す」トグルだった。**前半は今の「Always Show Marks on Target」と同じ**なので、**あのトグルを ON にして使っていた人は「Always Show Marks on Target」を ON にすれば同じ挙動になる**(押している間だけ隠れるところまで含めて)。⇒ **機能は1つも失われていない**が、**メニュー項目が1つ消えるのでリリースノートに書く**。
														//     ★撤去できた理由＝**「常時表示」を2つのトグルが別々に持っていた**(描画側が `sAlwaysShowMarks || sTgtMarksOn` という OR になっていたのが、重複そのものの証拠)。固有だったのは「押している間だけ隠す」だけなので、それを両トグルの標準の挙動に畳んだ。
														//   ⑯★★★**Story Changes モードのマークを印刷・PDF に出せるようにした**(2026-08-23〜24)。規則は**Pixel の枠とまったく同じ**＝新版(Target)は「Print comparison marks」が単独で決め、旧版(Source)は「Always Show Marks on Source」が決める(旧版側が印刷トグルを見ないのは従来からの仕様)。⚠**同時に見え方も作り直した**＝**変更箇所に色地を敷く**(蛍光ペンと同じ見え方。それまでは画面専用の反転で、紙では文字が読めなくなるため出せなかった)。★**透明を1つも使わない**ので、**PDF 1.3 でも 1.4 でも印刷でも同じに出て、画面と紙が同じ絵になる**(日本の入稿で主流の PDF/X-1a=1.3 で実機確認済み)。濃さはこれまでどおり「Marks opacity 25% / 75%」に連動する。
														//   ⑰★★**マークの色を選べるようにした**(2026-08-24)。フライアウトに **「Mark colour > Red / Cyan」**(相互排他・選択中に✓)。**Pixel の枠と Story の色地の両方**が同じ選択から色を取る。⚠**公開版からの挙動変更**＝1.3.0 までは「下地が赤っぽい画素の上だけ自動でシアンに変わる」背景適応だった。**自動判定は廃止**＝Story の色地は下地の画素を読めないので同じ芸当ができず、2つのモードで色の決まり方が食い違うため。⇒**赤い下地でマークが埋もれるならシアンを選ぶ**、という形にした。
														//   ⑱★**「Print comparison marks」を ON にしたときに、印刷と PDF に出ることを知らせるようにした**(2026-08-24 ユーザー要望)。OK ボタン1つのアラートで、**ON にしたときだけ**出す(OFF は元に戻すだけで、出力に何かが増えることは無いので出さない)。文面は全ロケール英語＝"Comparison marks will now appear in print and in exported PDF files."。★入れた理由＝このトグルが**画面だけでなく紙と PDF にも出る**ことは How to Use の中でしか説明しておらず、読まずに ON にした人には伝わらなかった。⚠**起動時のパネル設定の復元では出ない**(メニューを押した経路にだけ置いてある)。
														//     ⚠★★**公開版からの挙動変更**(2026-08-25 ユーザー指定)＝**この「Print comparison marks」は Save Panel Settings に保存されなくなった**。⇒ **InDesign を起動し直すたびに必ず OFF から始まる**(公開版 1.3.0 は ON のまま保存・復元していた)。理由＝このトグルだけは**画面の見え方ではなく、紙と PDF に何が出るかを変える**ので、前に使った回の設定が黙って残っていると、**気づかないままマーク入りで出力してしまう**。⇒ 出力に載せたい回だけ、その場で ON にする形にした。★**「Marks opacity 25% / 75%」は従来どおり保存される**(あちらは「出るときにどう見えるか」の設定で、出力に何かが増えるわけではないため)。⇒ リリースノートに書く価値がある。														//   ⑲★**ページパネルの右クリックの2項目を、比較モードに合わせた**(2026-08-24 ユーザー指摘)。(a)**「Refresh Page Comparison」を Story Changes モードでは出さない**＝この項目が作り直すのは画素比較の結果で、Story モードはページを1枚もラスタ化しないため、押しても時間がかかるだけで画面が変わらなかった。Story モードの更新は Story Edits の行の右クリック「Refresh Story Comparison」が持つ(⇒ どちらのモードにも更新の口がちょうど1つずつ在る)。(b)**「Check」を Story Changes モードでは全ページに付けられるようにした**＝Pixel モードでは「比較枠や斜線の付いたページだけ」に限っており、Story モードは枠を作らないので**項目そのものがメニューから消えていた**(無効な項目はコンテキストメニューに出ないため)。⇒ Story モードでは確認の済んだページを自由にチェックできる。★Pixel モードの挙動は従来どおり(枠のあるページだけ／枠が消えたらチェックも外れる)。
														//   ⑳★★**Story Changes モードでも ◀ Prev / Next ▶ が使えるようになった**(2026-08-24 ユーザー要望)。巡回するのは **Story Edits 一覧の「変更そのもの」**＝**変更のぶら下がっている行はその変更を1つずつ**、**ぶら下がっていない行はその行を1つ**(同じ場所を二度案内しないため)。押すと**一覧の行をクリックしたときとまったく同じことが起きる**＝新旧2つの窓がその箇所へ動き、**変わった文字の上に印が一瞬光り**、パネルのメッセージ欄に**もう一方の版の本文**が出る。★ボタンの間の表示は「1/4」のように**変更の件数**を数える(ページ数ではない)。★**一覧の選択も一緒に動く**ので、いまどの変更を見ているのかが一覧でも分かり、そのまま矢印キーで続きを歩ける。⚠Pixel モードの巡回は従来どおり(変更のあったページを1ページずつ)。Find Overset が ON のときのあふれ箇所は、どちらのモードでも従来どおり末尾に続く。
														//   ㉑★**「Mark colour」の選択が Save Panel Settings に保存されるようになった**(2026-08-25)。⑰でこのメニューを足したときに保存の口へ入れ忘れており、**Cyan を選んで保存しても InDesign を起動し直すと赤へ戻っていた**(しかも保存そのものは成功と表示されるので「保存したのに効かない」と見える)。⇒ 他の設定系トグルと同じ扱いになった。⚠**公開版 1.3.0 にはこのメニュー自体が無い**ので、提出説明では⑰の一部として書けばよく、独立した「修正」として説明する必要は無い。
														//   ㉓★★★**Story Changes の本文の読み取りを、スニペット XML 経由から ITextModel の直読へ替えた**(2026-08-31〜09-03・`feature/story-direct-read`)。**利用者から見える変化＝比較できなかった形が比較できるようになった**：(a)**脚注を1つでも含むストーリーは、本文を変えても脚注を変えても変更が1件も出なかった**(実測 `edits=0`)→出る (b)**段落の途中に表が立つ形**も同じく出るようになった (c)**圏点を読んで報告する**(㉔) (d)ルビの mono/group は属性の有無から推論せず `kTAMojiRubyBoss` を読む。⚠**行の分かれ方が1点だけ変わる**＝表のセルは本文の後ろに並ぶので、本文と表の変更が隣り合っても1行にまとまらず、本文の行が連続する。★同時に**「比較できなかった行」が2種類だけになり理由を持つ**(読めない／差が大きすぎる)。2026-09-03 に旧経路・並行運転・一時プロパティ `app.kcmStoryReadCompare`(一度も提出していない)を撤去。回帰＝`docs/ai-notes/kcm-story-direct-read-regression-2026-09-01.md`。
//   ㉔★★**Story Edits が圏点(けんてん)の変更も報告する**(2026-09-01 ユーザー決定「見つけられるなら見つけたい」)＝**③の「ルビと本文だけ」の絞り込みを一部撤回**。上段に種類のマーク(11種)を**図として描く**(文字だと CP932 とフォントの都合で化けうるため)。Custom は名前。★**本文が変わった段落のルビ・圏点も報告**する(親文字が変わっても属性が残っていれば2件、文字ごと消えたら本文の1件だけ＝「テキストが主、ルビ・圏点は従」)。★**結果の行は常に新版**(削除の行だけ旧版だったのを統一)。★★**本文が変わらずルビと圏点の両方が動いた行は Change 列が `Ruby+`/`Kenten+`**(2026-09-03)。
//   ㉕★★★**猫の手スタンプ(Kohaku Paw Stamp) = 2つ目のツール**(2026-09-04)。**KCM ツールを長押しすると出るフライアウト**に入るので、**ツールボックスのマスは増えない**(親のマスに小さな三角が付く)。ページの上をクリックすると、その点に肉球の印を置く。★**Shift＋クリックで剥がす**／★**Alt＋クリックで通常の10倍(ページ短辺の半分)の大きい印**。★**同じ場所へ重ねては置けない**(既に印のある所を普通に押しても何も起きず、ステータス行がそう言う)。★★**文書は1バイトも変えない**＝印はプラグインが持つ(比較マークと同じ約束)ので、**比較していない文書にも置ける**。★画面には常に出る。**印刷と PDF には「Print comparison marks」が ON のときだけ**出る(他のマークと同じ規則)。濃さは「Marks opacity 25% / 75%」に連動。★**パネルのツールボタンが2つのツールを持つようになった**＝**押しっぱなしでもう一方に切り替わる**(ツールボックスのフライアウトと同じ操作)。ボタンには**フライアウトの▽**が付き、**いま選ばれているほうのツールの絵**を表示する。★ScriptID **`nKGp`**(`UITools.KOHAKU_PAW_STAMP_TOOL`)を採番＝スクリプトから選べる。⚠**このコードは Adobe に未登録**なので、**次の提出のときに申請する**(登録済みの7件＝`pKGm` `pKGb` `pKGC` `pKGT` `pKGA` `pKGO` `nKGt`)。⚠**まだ保存されない**(InDesign を終了すると消える)＝JSON への保存は実装中。
//   ㉒★**プラグイン内部の名前(識別子・ファイル名・フォルダー名)を KCM に統一した**(2026-08-25 ユーザー指定)。★**利用者から見える変更は2つだけ**＝(a)**ページパネルの右クリックに出る3項目から接頭辞が消えた**(「KCM: Check」→「Check」ほか。直前に区切り線があるので、自作分がひとまとまりに見えることは変わらない)。(b)**パネル設定とチェック印の保存先ファイル名が変わった**。⚠**引き継ぎ処理は入れていない**ので、**更新した人はパネル設定が既定に戻り、保存済みのチェック印と Add/Remove 登録は読めなくなる**⇒**リリースノートに書く**。★**変えていないもの**＝ID の数値(ショートカット割当は .indk が数値 ID を保存するので外れない)／ScriptID の4文字コードと DOM 名(app.kcmStatus ほか。Adobe に登録済みのペアなので改名は再申請になる)／.pln 名・表示名・内部名。
															//   ■1.3.1 で撤去したもの: 「Translucent Toolbox」トグル(フローティング中の**ツールボックス**を半透明にする)。★★**提出説明に「機能を削除した」と書かないこと** ＝ **提出した 1.3.0 のビルドに最初から入っていない**(2026-08-07 ユーザー明言)ので、公開版から見れば存在しなかった機能。ActionID +38 は欠番のまま再利用しない。
														//   ⚠**①②とも「版数が 1.3.0 だった時期にコードへ入れた」もの**だが、提出した 1.3.0 のビルド(commit 5ff22c5 時点)には入っていない。**版数コメントが載っている位置で「提出済みか」を判断しない**。
														//
														// ■■【1.3.0 = 公開済み】公開版 1.2.0 から見た増分＝**1.3.0 の提出説明に使用済み。次の提出では繰り返さない**。
														// ■機能追加(minor を上げた理由):
														//   ①app.kcmStatus = パネルのステータス行の最後の1行をスクリプト/COM から読む読み取り専用プロパティ(KCMScriptProvider.cpp)。パネルを閉じていても答える。実機検証の自動化用。
														//   ②Translucent Pages Panel = **本体のページパネル**もホバーで不透明に戻る半透明にできる(フライアウト。従来は自分のパネルだけ)。窓の特定を WidgetID 狙い撃ちへ全面入れ替えたことで可能になった(本体パネルの窓タイトルは UI 言語で変わるため)。
														//   ③ツールに ScriptID を与えた = app.toolBoxTools.currentTool から KCM ツールを読み書きできる(UITools.KOHAKU_CHANGE_MARKER_TOOL)。従来は en_None で「何も選ばれていない」と区別できなかった。
														// ■機能変更(1.3.0 の提出説明で記載済み): 押下中 HUD を作り直した。公開版 1.2.0 は sprite 層で**相手の文書名**を出す作りで、押下を抜けた後に one-shot タイマーで描くため枠より遅れて出ていた。新版は比較マークの枠と同じ Draw Event で描くので**枠と同時に出る**。出す内容も文書名ではなく**押した窓の役割**(Target / Source / Not in comparison / Not comparing)に変えた(2026-07-27 ユーザー指示「相手の文書名は出さない」)。位置・見た目は旧版と同じ(ビュー左上・文字20px・不透明度0.6)。フライアウトの「Show HUD」トグルと設定キー hudOn は廃止＝**常に出る**。★2026-08-06 に一度全廃し、2026-08-07 に作り直した経緯があるので、「削除した機能」として説明しないこと。
														// ■表示・操作の整理: About を「名前 版数」1行に(英語のみ)／日本語で出すのは How to Use と Hide Unchanged の確認アラートの2箇所だけに整理(メニュー・パネル・ステータス行は全ロケール英語)／パネル幅 +10px／Hide Unchanged と Start(文書2つ未満)と Find Overset(走査対象なし)を条件付きで灰色化／ページパネル右クリックのメニュー接頭辞を短縮／Prev/Next のラベル整理。
														// ■不具合修正・内部改善: ジャンプ前にスプレッド切替(マスターページへ飛べるように)＋マスターページのあふれを巡回一覧に載せる／Find Overset で押し出された表のセルを報告しない／あふれを聞く前にリコンポーズ／表の列挙を ITextStoryThreadDictHier へ(入れ子の表が入る)／描画エンジンの見直し(greek 無効・除外領域の緑は画面限定・除外矩形のキャッシュ化)／LocaleIndex に k_Wild 追補(列挙外 UI 言語での生キー表示を防ぐ)／比較失敗ページの可視化(failed=N・Refresh で既存枠を消さない)／半透明の細部(はみ出しメニュー・AutoAttach の OFF ガード・Shutdown 後の再武装禁止)。
														// ■全14ブロックの API 監査(2026-08-05〜08-07)とバグ特化の全コード再点検(08-06)を実施済み。全文=docs/ai-notes/kescm-file-map.md の各ブロックノート／kescm-bug-recheck-2026-08-06.md。
// Plug-in prefix and the IDs that sit on the boundary:
// kKCMPrefixNumber / kKCMPrefix / kKCMStringPrefix, together with every ID both halves must know
// by the same value (the Facade IIDs, the notification protocol IID, the seven MessageIDs), are in
// KCMBoundaryID.h. KCMUI carries a copy of that file with identical content: edit one and the
// other drifts in silence. What remains here are the model-only IDs.
#include "KCMBoundaryID.h"

// Missing plug-in: (see ExtraPluginInfo resource)
#define kKCMMissingPluginURLValue		kSDKDefPartnersStandardValue_enUS // URL displayed in Missing Plug-in dialog
#define kKCMMissingPluginAlertValue	kSDKDefMissingPluginAlertValue // Message displayed in Missing Plug-in dialog - provide a string that instructs user how to solve their missing plug-in problem

// PluginID is in KCMBoundaryID.h as well: the UI half names this plug-in as its PluginDependency,
// so both sides need the same value.

// ClassIDs:
// Offsets +7, +8, +9 and +11..+28 are NOT free. They belong to the UI half's bosses, which kept
// their numbers when they moved to ui/KCMUIID.h - the same offset, read against kKCMUIPrefix. The
// values cannot collide (model is 0x1EA50x, UI is 0x1EA58x); keeping the numbers aligned is what
// makes an offset readable across the two files. New model bosses start at +34.
// Reusing a retired ClassID slot is safe here - this half writes no persistent data into a
// document - and +11 was in fact taken over below. The space where reuse is NOT safe is the
// ActionID space, because an .indk stores action IDs as plain numbers.
// What lives on this side is the work that stands up without a window: drawing the comparison
// marks, startup and shutdown, document close, and the ScriptProvider.
DECLARE_PMID(kClassIDSpace, kKCMScriptProviderBoss, kKCMPrefix + 3)	// The only ScriptProvider this plug-in has. It serves every property this half publishes - on the application, the story and the document objects: the .fr gives this same boss one Provider block per object (the last blocks of KCM.fr), and that is the list; do not repeat it here. There are no methods. All are read-only except app.kcmStoryReadCompare, a temporary read-write one (see the ScriptInfoIDs below).
// +4 retired: kKCMDrawEventServiceBoss, when mark drawing was unified on kKCMRingAdornmentBoss.
// +5 retired: kKCMPeekWatcherBoss, when the middle-button watcher was dropped.
DECLARE_PMID(kClassIDSpace, kKCMPeekStartupBoss, kKCMPrefix + 6)	// IStartupShutdown: starts the peek watcher when the application starts.
DECLARE_PMID(kClassIDSpace, kKCMDocResponderServiceBoss, kKCMPrefix + 10)	// IK2ServiceProvider + IResponder: watches for a document actually closing, to clean up the tracking state that referred to it.
DECLARE_PMID(kClassIDSpace, kKCMBeforeSaveResponderServiceBoss, kKCMPrefix + 11)	// IK2ServiceProvider + IResponder: watches BEFORE a save and puts back the spreads Hide Unchanged hid, so they never reach the file. kBeforeCloseDoc is too late - measured, the save happens first (KCMDocResponder.cpp says why). This offset previously held kKCMStoryScriptProviderBoss.
// kKCMStoryScriptProviderBoss also stood at +11 until the story counters were folded into the one
// ScriptProvider above. It existed on the belief that a single boss could not keep app and story
// properties apart, which was wrong: a Property attaches to the PRECEDING Object (vol1-11:1302)
// and a provider "can be defined (used) in multiple places" (vol1-11:1237). basicshape does
// exactly that with Adobe's own kPageItemScriptProviderBoss (BscShp.fr:370-404, one Contexts entry
// at :317). Verified on device: 'kcmChangeCount' in app and 'kcmStatus' in story are both false.
DECLARE_PMID(kClassIDSpace, kKCMRingAdornmentBoss, kKCMPrefix + 29)	// IAdornmentShape + IAdornmentFlattenerUsage: draws the comparison marks as an adornment (KCMRingAdornment.cpp). It is registered on the session's GLOBAL page item adornment list (IID_IGLOBALPAGEITEMADORNMENTLIST), so it is attached to nothing in the document and the .indd is never touched.
DECLARE_PMID(kClassIDSpace, kKCMRingAdornmentStartupBoss, kKCMPrefix + 30)	// IStartupShutdown: registers and unregisters the adornment above, once per execution context. It has to be separate from kKCMPeekStartupBoss: that one drops comparison state on shutdown and is therefore main-thread only (kCMainThreadStartupShutdownProviderImpl), while this one is pointless unless it also runs on background threads.
// The next two bosses declare the adornment's transparency, but only while an export runs.
// Why it has to be temporary: IXPManager's list of page items that have transparency is document
// data and persists into the .indd (measured - reopening does not revalidate it). Left on for the
// whole comparison, it is baked into the file the moment the user saves, and stays there for
// people who do not have KCM. So it goes on only when it is needed and comes straight back off.
// The flattener is only needed for export and print; screen drawing and thumbnails never ask.
// Modelled on customconditionaltext, the one sample that changes something before an operation and
// restores it afterwards, for both PDF and print.
// Deliberately not hooked to saving (kBeforeSaveDoc): failing there costs the document, whereas
// failing an export only costs the export.
DECLARE_PMID(kClassIDSpace, kKCMPDFExportSetupBoss, kKCMPrefix + 31)	// IK2ServiceProvider (Adobe's kPDFExportSetupServiceImpl) + IPDFExportSetupProvider: joins the transparency list in BeginExport and leaves it in EndExport. An asynchronous export hands this the CLONE db it exports from, which is what makes it possible to change the output without touching the original document. It replaces kKCMExportXPResponderServiceBoss, which used the kBeforeExport / kAfterExport / kFailedExport signals against the ORIGINAL db and therefore baked the list into the .indd if the user saved mid-export.
DECLARE_PMID(kClassIDSpace, kKCMStoryMarkerBoss, kKCMPrefix + 32)	// IK2ServiceProvider (the API's own kGlobalTextAdornmentServiceImpl) + IID_IGLOBALTEXTADORNMENT: the global text adornment that lays a colored ground under changed characters in Story mode (KCMStoryMarker.cpp). It lives on the model side because File > Export > PDF runs on a background thread and a kUIPlugIn is never handed a single draw there. Unlike the page item adornment at +29 it needs no manual per-context registration: it is a service, so the registry resolves it for background threads too.
DECLARE_PMID(kClassIDSpace, kKCMStoryMarkerExpiryBoss, kKCMPrefix + 33)	// IIdleTask: withdraws just the jump flash of the marker above after about a second (KCMStoryMarkerExpiry.cpp). It is on this side because the adornment starts and stops it; leaving it in the UI would invert the dependency.
										// There is no print-side counterpart (kPrintSetupService + IPrintSetupProvider). Not because it would not work: with it the marks come out denser in print too (measured 16,076 against 8,407 colored pixels, and neither case turns solid). It was left out because print does not need that precision - what goes to the printer is the PDF. The A/B and the way back are in section 5 of KCMRingAdornment.cpp.
										// Next new boss: +34.

// InterfaceIDs:
// +0..+3 (three observer attachment IDs and the Story Edits section height) moved to
//   ui/KCMUIID.h - all of them are used only by UI bosses. The offsets did not change.
// +4..+9 (the Facade IIDs and the notification protocol IID) moved to KCMBoundaryID.h, because
//   they mean nothing unless the UI half sees the same value. The offsets did not change either,
//   so all of +0..+9 are in use and none of them may be reused here.
// +10..+25 are free.

// ImplementationIDs:
// The UI half's implementations are in ui/KCMUIID.h, keeping their original offsets. Whatever is
// declared here belongs in source/KCMFactoryList.h, not in ui/KCMUIFactoryList.h.
DECLARE_PMID(kImplementationIDSpace, kKCMScriptProviderImpl, kKCMPrefix + 0)	// CScriptProvider implementation (KCMScriptProvider.cpp). This one implementation serves every published property, on app, story and document alike (the list is KCM.fr's Provider blocks).
// +1 / +2 retired: kKCMDrawEventSrvcImpl / kKCMDrawEventHandlerImpl, with the draw event route.
// +3 retired: kKCMPeekWatcherImpl, with the middle-button watcher.
DECLARE_PMID(kImplementationIDSpace, kKCMPeekStartupImpl, kKCMPrefix + 4)	// IStartupShutdown implementation (starts the peek watcher).
// +8 retired: kKCMDocServiceProviderImpl, a hand-written ServiceProvider. A responder that handles
//   a single signal does not need one - naming the API's kAfterCloseDocSignalRespServiceImpl
//   (DocumentID.h) in the .fr registers it (see kKCMDocResponderServiceBoss in KCM.fr).
DECLARE_PMID(kImplementationIDSpace, kKCMDocResponderImpl, kKCMPrefix + 9)	// IResponder implementation (clears tracking state once a close is final).
DECLARE_PMID(kImplementationIDSpace, kKCMCompareFacadeImpl, kKCMPrefix + 39)	// IKCMCompareFacade implementation (KCMFacades.cpp). AddIn on kUtilsBoss, which is why it MUST be our own implementation: adding an SDK-supplied implementation to an existing boss collides with other vendors and the collision is per ImplementationID, not per IID.
DECLARE_PMID(kImplementationIDSpace, kKCMMarkDataImpl, kKCMPrefix + 40)	// IKCMMarkData implementation (KCMFacades.cpp). Same AddIn on kUtilsBoss; this one is read-only.
DECLARE_PMID(kImplementationIDSpace, kKCMPageFlagsFacadeImpl, kKCMPrefix + 41)	// IKCMPageFlagsFacade implementation (KCMFacades.cpp). Same AddIn.
DECLARE_PMID(kImplementationIDSpace, kKCMStoryEditsFacadeImpl, kKCMPrefix + 42)	// IKCMStoryEditsFacade implementation (KCMFacades.cpp). Same AddIn; read-only apart from RefreshRow.
DECLARE_PMID(kImplementationIDSpace, kKCMBookFacadeImpl, kKCMPrefix + 43)	// IKCMBookFacade implementation (KCMFacades.cpp). Same AddIn.
DECLARE_PMID(kImplementationIDSpace, kKCMBeforeSaveResponderImpl, kKCMPrefix + 44)	// IResponder implementation (puts back the spreads Hide Unchanged hid, before the save. KCMDocResponder.cpp). This offset previously held kKCMStoryScriptProviderImpl, which was folded into kKCMScriptProviderImpl.
DECLARE_PMID(kImplementationIDSpace, kKCMRingAdornmentImpl, kKCMPrefix + 45)	// IAdornmentShape implementation (KCMRingAdornment.cpp). It holds no drawing code: for a spread it calls KCMDrawEventHandler::DrawSpreadMarks(), so there is exactly one place that paints the marks.
DECLARE_PMID(kImplementationIDSpace, kKCMRingFlattenerUsageImpl, kKCMPrefix + 46)	// IAdornmentFlattenerUsage implementation (same file). This is the one that matters: it is the ONLY way to tell the transparency manager that this adornment uses transparency, which is what stops the ring coming out solid in PDF 1.3. Modelled on transparencyeffect/TranFxFlattenerUsage.cpp.
DECLARE_PMID(kImplementationIDSpace, kKCMRingAdornmentStartupImpl, kKCMPrefix + 47)	// IStartupShutdownService implementation (end of KCMRingAdornment.cpp). It only calls Register/Unregister. It needs to run once per execution context, which is why it is on a different boss from kKCMPeekStartupImpl.
DECLARE_PMID(kImplementationIDSpace, kKCMPDFExportSetupImpl, kKCMPrefix + 48)		// IPDFExportSetupProvider implementation (KCMRingAdornment.cpp - same concern as the transparency declaration, so they live together). The ServiceProvider side names Adobe's kPDFExportSetupServiceImpl in the .fr, so this is the only custom implementation. Modelled on sdksamples/pdfvt. Successor to kKCMExportXPResponderImpl, which held the same offset.
// +49 retired: kKCMExportXPServiceProviderImpl, a hand-written ServiceProvider that caught the
//   three export signals on one boss. The PDF export service replaced it and supplies its own.
DECLARE_PMID(kImplementationIDSpace, kKCMStoryMarkFacadeImpl, kKCMPrefix + 50)	// IKCMStoryMarkFacade implementation (KCMFacades.cpp). How the UI reports that a toggle moved, a press started, or a jump happened.
DECLARE_PMID(kImplementationIDSpace, kKCMStoryMarkerAdornmentImpl, kKCMPrefix + 51)	// IGlobalTextAdornment implementation (KCMStoryMarker.cpp). Lays a colored ground under changed characters.
DECLARE_PMID(kImplementationIDSpace, kKCMStoryMarkerExpiryImpl, kKCMPrefix + 52)	// IIdleTask implementation (KCMStoryMarkerExpiry.cpp). Withdraws the jump flash after about a second.
										// There is no IPrintSetupProvider implementation (the old +50); see the note on the Class side.
										// Next new implementation: +53. Read this line before picking a number - the retirement notes are BELOW the DECLAREs, so deciding from the last line alone picks a slot that is already spoken for.

// MessageIDs: how the model tells the UI what changed. All seven moved to KCMBoundaryID.h - sender
//   and receiver must see the same value, or the build succeeds and nothing happens at run time.
//   The offsets are unchanged (+0..+6) and must not be reused here:
//     +0 kKCMMarksRebuiltMessage / +1 kKCMMarksClearedMessage / +2 kKCMPageFlagsChangedMessage /
//     +3 kKCMStoryEditsRebuiltMessage / +4 kKCMStatusTextMessage / +5 kKCMOversetRescannedMessage /
//     +6 kKCMComparisonDocsClosedMessage
//   Why +6 is separate from Stop, and what it carries, is documented where it now lives.

// Script element IDs. The live ones are the DECLAREs below, +13 onwards; what object each hangs
// off and whether it is read-only is in KCM.fr's Provider blocks - count them THERE. (This line
// used to say "six, all read-only", and was wrong on both counts by 2026-08-31: a document
// property came on 2026-08-20 and a read-write application property on 2026-08-31, and neither
// author re-counted here.) +1..+12 are the graves of the old scripting METHODS, which were removed
// wholesale; new properties are taken from +13 so that nothing is confused with a retired method.
DECLARE_PMID(kScriptInfoIDSpace, kKCMStatusPropertyScriptElement, kKCMPrefix + 13)	// app.kcmStatus (read-only; the last line shown in the panel's status area)
DECLARE_PMID(kScriptInfoIDSpace, kKCMBookResultPropertyScriptElement, kKCMPrefix + 14)	// app.kcmBookResult (read-only; the last book comparison as one TSV line per chapter, "name<TAB>state"). The status line can only show one line, so this is what makes a chapter-by-chapter result checkable without a human reading it.
// The four story change counters, read straight off the story - app.documents[0].stories[2].kcmChangeCount.
// They hang off the STORY object, not the application, which in the .fr means the SECOND Provider
// block on the same kKCMScriptProviderBoss (what separates them is the block, not the boss).
// What they are for: the aggregate counter is the number that decides whether Story Edits shows a
// row at all, so being able to read it from outside is the difference between diagnosing "the list
// is empty" and having to read the source to find out whether that is a bug or two genuinely
// identical documents.
// These four are deliberately kept out of the IDML/INX DOM by the SECOND VersionedScriptElementInfo
// in KCM.fr (Contexts kINXScriptManagerBoss, Provider{kNotSupported}). Properties become IDML
// ATTRIBUTES by default - measured, every <Story> and <XmlStory> carried KcmChangeCount and the
// rest. That resource cancels them by naming the SAME element IDs, so changing an ID here means
// changing it there in the same edit: fix one side only and they silently reappear in the IDML.
DECLARE_PMID(kScriptInfoIDSpace, kKCMChangeCountPropertyScriptElement, kKCMPrefix + 15)	// stories[n].kcmChangeCount (aggregate; ITextModel::GetChangeCount)
DECLARE_PMID(kScriptInfoIDSpace, kKCMTextChangeCountPropertyScriptElement, kKCMPrefix + 16)	// stories[n].kcmTextChangeCount (body text; GetTextChangeCount)
DECLARE_PMID(kScriptInfoIDSpace, kKCMAttrChangeCountPropertyScriptElement, kKCMPrefix + 17)	// stories[n].kcmAttrChangeCount (formatting; GetAttrChangeCount)
DECLARE_PMID(kScriptInfoIDSpace, kKCMOtherChangeCountPropertyScriptElement, kKCMPrefix + 18)	// stories[n].kcmOtherChangeCount (everything else; GetOtherChangeCount)
// Same rule as the four above: this ID is repeated in KCM.fr's second VersionedScriptElementInfo.
DECLARE_PMID(kScriptInfoIDSpace, kKCMTransparencyItemCountPropertyScriptElement, kKCMPrefix + 19)	// document.kcmTransparencyItemCount (read-only; the size of IXPManager's list of page items that have transparency). It is how we check from outside that nothing was left on the list and saved: the list persists into the .indd, so save, close, reopen and read.
// (The line above lost this comment on 2026-08-31, when +20 was inserted and the two trailing
//  comments ended up on one line. Put back by the API re-audit of 2026-09-03.)
// +20 retired: kKCMStoryReadComparePropertyScriptElement (app.kcmStoryReadCompare, the direct-read
//   migration's parallel run, READ-WRITE and temporary; 2026-08-31 to 2026-09-03). Never shipped,
//   so the slot could be reused - it is left empty anyway, the way +1..+12 are.
// (The tool's enumerator goes on the application's own kToolBoxEnumScriptElement, so this side
//  needs no ID for it.)

// StringKeys:
// This is the only string key left on the model side; the rest are in ui/KCMUIID.h. The VALUE of
// every key keeps the model prefix (kKCMStringPrefix = "2008320") on both sides, because string
// keys have to be globally unique and cannot be borrowed the way widget IDs can (vol2-12:71) - the
// keys that moved to the UI half did not change value.
// This single key is the reason the model half needs a StringTable of its own (KCM_enUS.fr).
#define kKCMHideConfirmKey		kKCMStringPrefix "kKCMHideConfirmKey"	// Body of that confirmation dialog (enUS; a Japanese UI gets the runtime swap in KCMLoc.h)

// Initial data format version numbers
#define kKCMFirstMajorFormatNumber  RezLong(1)
#define kKCMFirstMinorFormatNumber  RezLong(0)

// Data format version numbers for the PluginVersion resource
#define kKCMCurrentMajorFormatNumber kKCMFirstMajorFormatNumber
#define kKCMCurrentMinorFormatNumber kKCMFirstMinorFormatNumber

#endif // __KCMID_h__
