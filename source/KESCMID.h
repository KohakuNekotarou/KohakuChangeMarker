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


#ifndef __KESCMID_h__
#define __KESCMID_h__

#include "SDKDef.h"

// ★★2026-08-15（第2段 Task 6B-2）: **会社名・表示名・版数・Alt キー表記は KESCMBoundaryID.h へ移した**
//   ＝model と UI の2つの .pln が同じ値を名乗らなければならないため（[[one-question-one-place]]）。
//   ここに残るのは **model 側だけの名前** 2つ。UI 側の名前は KCMUIID.h が持つ。

// Plug-in:
// ★2026-08-15（第2段 Task 11）: `kKESCMPluginName` は KESCMBoundaryID.h へ移した
//   ＝UI 側が `PluginDependency` で依存先として名乗るため（両側が同じ値を知る必要がある）。
#define kKESCMFileName		"KohakuChangeMarker"			// 出力ファイル名の基底(.rc の OriginalFilename)。vcxproj の TargetName と一致させること。表示名と違いスペースは入れない。
// ★★★**Adobe から受け取った原文（2026-08-13。忘れないようにここへ残す＝ユーザー指示）**:
//
//     "Following Prefix ID has been registered as per your request below : 0x1EA500 - 0x1EA5FF ."
//
//   ⇒ **登録されたのは 1 個の値ではなく `0x1EA500`〜`0x1EA5FF` の 256 枠**。この帯の中でどう割るかは
//     こちらの自由で、**model と UI で分け合ってよい**:
//         `0x1EA500` … このプラグイン（model / KohakuExtendScriptChangeMarker）
//         `0x1EA580` … KohakuChangeMarkerUI（KCMUI・model/UI 分割 第2段で使う）
//   ★1本の帯を model と UI で分けるのは **Adobe 自身のやり方**。SDK 実測: `customdatalink`(0xb3300) と
//     `customdatalinkui`(0xb3380) はオフセット +0..37 と +0..17 ＝ **両方とも 0xb33xx に収まっている**。
//     ほかに xdocbookworkflow 対は 16 刻み、0x572xx は4本のサンプルが共有している。
//   ★**この 1 帯で自作5本ぜんぶ賄える**（最大オフセットの合計は ActionID で 122/256。widget ID は
//     グローバルに一意である必要が無いので予算に数えない）。割り当て案は
//     docs/superpowers/specs/2026-08-13-kescm-model-ui-split-stage1-design.md §1.3-2。
// ★★2026-08-15（第2段 Task 6B）: **prefix の定義そのものは KESCMBoundaryID.h へ移した**
//   ＝UI 側（KCMUI）のコピーと値を1つにするため。下に続くコメントは経緯の記録としてここに残す。
													// ★★2026-08-13: Adobe が発行した正規の prefix に差し替えた。wwds@adobe.com へメールで依頼し
													// 「0x1EA500 - 0x1EA5FF」を受領＝**各 ID 空間 256 枠が予約された**(自作プラグインで唯一)。
													// ⚠旧値 0x205515 は Adobe Developer Console のプラグイン ID(10進 205515)に 0x を付けて
													// 16進として読み替えただけの数値で、**Adobe の採番体系とは無関係＝1枠も予約されていなかった**。
													// 実害として KESCL(0x205554) と 0x3F=63 しか離れておらず、widget 枠が残り6個まで逼迫していた。
													// ★引っ越しても既存の .indd は壊れない(実測): KESCM は文書に永続データを一切書いていない
													// ——KESCM.fr の AddIn は 1 か所だけで相手は kActiveContextBoss(セッション常駐=実行時のみ)、
													// Class は全て UI か実行時オブジェクト、唯一の永続 IID_IKESCMSAVEDSECTIONHEIGHT は
													// パネル下ペインの高さ(ワークスペース側)。失うのはショートカット割り当てとパネル配置だけ。
													// ★★2026-08-15 更新: UI 側 KCMUI の prefix は**確定済み＝0x1EA580**(この帯の後半)。
													// 旧記述「2 本目を発行依頼中(暫定 0x205792)」は**失効**——2 本目は発行されず、
													// **同じ帯 0x1EA500-0x1EA5FF を model と UI で前半・後半に割る**形に決まった:
													//   model(KESCM) = 0x1EA500 (+0..127) / UI(KCMUI) = 0x1EA580 (+128..255)
													// 1 本の帯を 2 プラグインで分けるのは Adobe 自身のやり方
													// (customdatalink 0xb3300 / customdatalinkui 0xb3380 が両方 0xb33xx に収まっている)
													// ＝**ID の一意性はプラグイン単位ではなく「値」で決まる**。
													// ⚠**ID スペースごとに 128 枠**(widget と action は別勘定)。現状の最大オフセットは +43 で余裕がある。
													// ★実機確認済み(2026-08-15): `app.menuActions.itemByID(2008320)` = KESCM の About /
													//   `itemByID(2008448=0x1EA580)` = KCMUI の About ＝**両方ロードされ衝突なし**。
													// なお model/UI で prefix を分けること自体はガイド vol1-07:113 の指示。
// ★★2026-08-15（第2段 Task 6B-2）: **値の定義は KESCMBoundaryID.h へ移した**（model と UI の
//   2つの .pln が同じ版数を名乗るため）。⚠**版数の履歴と「次に提出する分」の増分は、引き続き
//   下のコメントがこのプロジェクトの正本**。動いたのは値の置き場所だけ。
//#define kKESCMVersion		"1.5.0"						// Version of this plug-in。About ボックス本文・.rc の FileVersion・PluginVersion リソースの3か所に出る。1.0.1 → 1.1.0(2026-07-25) → 1.1.1(2026-07-26) → 1.2.0(2026-07-30) → 1.2.1(2026-08-06) → 1.3.0(2026-08-07) → 1.3.1(2026-08-07) → 1.4.0(2026-08-09) → 1.5.0(2026-08-15) → 1.6.0(2026-08-19) → **2.0.0(2026-08-21)**。
														// ★1.4.0 で minor を上げた理由 = **新機能 Story Edits**(パネル下部に開閉セクションを設け、変更のあったストーリーを一覧する)。1.2.1→1.3.0 のときと同じ基準＝機能追加が入るなら patch では足りない。★**2026-08-10 に段階4(ジャンプ)まで完成**(下記③)。
														// ★★**1.5.0 で minor を上げた理由(2026-08-15 ユーザー決定)** = **model/UI 分割の完了**で **PDF 書き出しに比較マークが出るようになった**(下記⑧)＋**Story の変更カウンターをスクリプトに公開**(下記⑨)。
														//   ⚠**1.4.0 は提出しないまま 1.5.0 へ繰り上げた**(1.2.1→1.3.0 と同じ形)。∴ 下の増分リストは**公開版 1.3.0 から見た全部**で、①〜⑦は 1.4.0 のときのまま据え置き。
														//   ★このとき下の「■■【1.4.0 = 次に提出する分】」の見出しも 1.5.0 へ書き換えてある＝**見出しの版数と kKESCMVersion は必ず一致させること**(ずれると「どれを提出説明に使うか」が読めなくなる)。
														// ★★★**1.6.0 で minor を上げた理由(2026-08-19)** = **比較マークが PDF 1.3(Acrobat 4)でも半透明で出るようになった**(下記⑩)。⚠**日本の入稿プリセットはほぼ 1.3**(PDF/X-1a・X-3・雑誌広告送稿用＝すべて Acrobat 4)なので、実務では 1.4 より効く。
														//   ⚠**1.5.0 も提出しないまま 1.6.0 へ繰り上げた**(1.4.0→1.5.0 と同じ形)。∴ 下の増分リストは**引き続き公開版 1.3.0 から見た全部**で、①〜⑨は据え置き。
														// ★★★**2.0.0 で major を上げた理由(2026-08-21 ユーザー判断)** = **2つ目の比較モード「Story Changes」**(下記⑬)。1.x は「ページを画素で比べる道具」の一本道だったが、**本文そのものを比べる道具が並んだ**＝製品の性格が変わったので minor では足りない。
														//   ⚠**1.6.0 も提出しないまま 2.0.0 へ繰り上げた**(1.5.0→1.6.0 と同じ形)。∴ 下の増分リストは**引き続き公開版 1.3.0 から見た全部**で、①〜⑫は据え置き。
														//   ★★**提出の時期＝「この機能が完成した時点」**(2026-08-21 ユーザー明言)＝Story Changes は**まだ第1段**なので、**今すぐ出さない**。⇒ 第2段(Scrollbar Map / ページパネル / Export Changed Pages への供給。設計書 §13)と、旧側の窓を対応する文字まで寄せる件が片付いてから。
														// ★Adobe Exchange の公開版は **1.3.0**(2026-08-07 承認・公開)。1.2.1 は提出しないまま 1.3.0 へ繰り上げた(機能追加が入ったので patch では足りない)。
														// ★★**「版数が◯◯だった時期にコードへ入れた」ことと「提出した◯◯のビルドに入っている」ことは別物**。取り違えると提出説明を誤る(2026-08-07 に実際に踏んだ)ので、増分は**提出したビルドを境に**2段へ分けてある。★提出文を起こすときは【次に提出する分】だけを読むこと。
														//
														// ■■【2.0.0 = 次に提出する分】★公開版 1.3.0 から見た増分。**提出説明はここだけを使う**。(版数は 1.3.1→1.4.0→1.5.0→1.6.0→2.0.0 と繰り上げ。①〜⑦は 1.4.0 のとき、⑧⑨は 1.5.0 のときと同一で、2026-08-19 に⑪・2026-08-20 に⑫・2026-08-21 に⑬が加わった。⚠**⑩は「設定が初期化される」という注意書きで、機能の増分ではない**＝番号を振るときに数え間違えやすい。★**major を上げた理由は⑬**＝2026-08-21 ユーザー判断)
														//   ①パネルにツール切替ボタン(kKESCMToolButtonWidgetID) = 押すとツールボックスの琥珀のツールがアクティブになる。絵はツールボックスと同じリソースを参照。★押下表示はツールボックスと双方向に同期する(状態を書くのは ITool::Select/Deselect の1か所だけなので、どちらから選んでも食い違わない)。How to Use の冒頭も「ツールボックス、またはパネルのツールボタン」へ追随済み。
														//   ②半透明パネルの「不透明に戻す」判定を変更 = カーソルがパネルの矩形の中にある限り、その上にフライアウト・子メニュー・ツールチップが出ていても不透明のまま。公開版 1.3.0 は自分の窓が上に出ると薄くなった(KBS と同じ判定へ揃えたもの)。
														//   ③★★**Story Edits(パネル下部の開閉セクション) = minor を上げた理由。★2026-08-10 に完成(段階1〜4)＝提出説明に書いてよい。**
														//     何をするものか: 画素比較は「このページは違って見える」までしか言えない。Story Edits は Target と Source の各ストーリーの変更カウンター(ITextModel の4本)を突き合わせ、**変更のあったストーリーを一覧**して「テキストが変わったのか・書式だけか・表などが変わったのか」を区別する。行はページ順で、左に本文の先頭、右に種類(Text / Attr / Other / Added)。
														//     ⚠**数字(4->6 のようなカウンター値)は出さない** = カウンターは編集回数ではなく状態のバージョン番号なので、差の大きさに人向けの意味が無い(2026-08-08 実測)。
														//     行の操作: **単クリック=そのストーリーの先頭フレームを画面中央に出す**(Source 窓と Pages パネルは Prev/Next と同じ流儀で連動)。**ダブルクリック=そのストーリーの全文を選択する**(2026-08-10 ユーザー指示で「先頭にキャレット」から変更＝行は「このストーリーが変わった」という報告なので、次にやりたいのはコピー・書式変更・差し替えのいずれかで、そのどれもが選択で足りる)。⚠ダブルクリックは**アクティブツールを文字ツールに変える**(琥珀のツールは外れる)＝選択しても操作できなければ意味が無いため。How to Use にも明記済み。
														//     一覧の見出し: **UID / Story / Change の3列**(2026-08-10 ユーザー要望)。★行のセルと同じ Frame・同じ binding を .fr で与えることだけが列の揃いを保証している。行の間の区切り線は**フレームワークが描くものをそのまま残している**(DVTreeNodeControlView は行高14px以上なら自動で描く)。⚠**2026-08-11 に一度消す実装(kKESCMStoryRowViewImpl)を入れ、同日ユーザー判断で撤去した＝線は「ある」**——旧記述の「消してある」は誤り(2026-08-11 ブロック15 監査 D-1 で訂正)。経緯は下の Impl +28 のコメントと KESCM.fr の行 boss。
//     現況: **段階1〜4 すべて完了**(2026-08-10)。計画=docs/superpowers/plans/2026-08-09-kescm-story-edits-stage3.md と ...-2026-08-10-kescm-story-edits-stage4.md／設計=docs/superpowers/specs/2026-08-09-kescm-story-edit-section-design.md
														//     ⚠**パネルが 153→185px 高くなっている**(開閉ボタンの帯 20px ＋ 猫イラストを収めるための 12px。2026-08-10)。これは公開版 1.3.0 から見て目に見える変更なので、提出説明に書く価値がある。★同時に**ステータス欄が右端まで広がった**(180→216)＝猫が下の帯へ移った分。
//     ★★2026-08-11 の仕上げ2件(どちらも公開版 1.3.0 から見て目に見える): ①**一覧の本文が途中で切れなくなった** = モデル側にあった30文字上限を撤去し、省略はセルの kEllipsizeMiddle に一任した(パネルを広げた分だけ本文が伸びる)。旧実装では "STORY A bottom right of page 1" でちょうど切れ、続く ", EDITED." が出ていなかった。②**ステータス欄が5px高くなった**(下端 145→150) = 日本語UIのパレットフォントは1行 17.9px(実測)で4行に 74px 要るのに枠が 69px しかなく、**4行目が約5px 切れていた**。どちらも「表示が欠けていたのを直した」ので、提出説明では機能追加ではなく修正として書く。
//   ④★★**マスターページも比較するようになった**(2026-08-11。公開版 1.3.0 から見て新機能)。従来は通常スプレッドだけが比較対象で、マスターページの変更は枠に出なかった(あふれ「+」だけは出ていた)。マスタースプレッドは**名前**(A-親ページ 等)で Target/Source を対応付ける＝順番で組むと、片方にマスターが1つ増えただけで別のマスターどうしを比べてしまうため。片方にしか無い名前のマスターは比較しない。あわせて**スクロールバー地図が表示中のスプレッドに追従**するようになり(マスター表示中はそのマスターの内容だけを出す。マークが無ければ空)、**Prev/Next の巡回にもマスターページの枠が入る**。★**「Sync Layout Views」と「Align Other Views to Active」もマスターに対応**(同日追加)＝一方の窓でマスタースプレッドを表示すると、もう一方の窓も**同じ名前のマスターへ移動して同じ位置を映す**(スクロールだけでは別スプレッドへ届かないので、追従側のスプレッドも切り替える)。**相手側にそのマスターが無ければ追従側は動かさない**(Added ページと同じ扱い)。★**TSV 出力(Export Changed Pages)もマスターページを出す**(2026-08-11 に対応。⚠旧記述「TSV だけは通常ページのみ」は失効)＝Page 列にマスタースプレッド名(「A-親ページ」。見開きマスターは「A-親ページ (2)」のようにスプレッド内の位置を添える)を出し、種別は Changed。⚠**挿入/削除(Inserted/Deleted)は通常ページだけ**＝マスターは名前で対応付けるので、片方にしか無いマスターはそもそも比較対象にならず「増えた/減った」という状態を持たない。★★**2026-08-16: 旧版べた載せ(Shift+左ドラッグ)と CMYK 色サンプリング(Alt+左)もマスタースプレッドで使えるようになった**＝マスター対応から漏れていた最後の2つ。比較・Refresh・Sync・TSV・Prev/Next は 2026-08-11 に対応したが、この2つだけは**マウスの下のページを探す経路**が通常スプレッドしか見ていなかった(マスターを表示していると、同じ座標にある通常ページの方が先に当たる)。⇒ **表示中のスプレッドの中だけを見る**形にして解消。実機確認済み。
//   ⑤★★**ブック比較(Compare Books) = パネルのフライアウトに追加した新機能**(2026-08-11。公開版 1.3.0 から見て新機能)。開いている2つのブックを**章単位**で比較し、章ごとに「変わった/変わらない」を出す。Target は**ブックパネルで前面タブのブック**、Source はそれ以外で最初に開いているブック——ダイアログの上2行に両方の名前が出るので、押す前に対象を確かめられる。結果は章名と判定(Changed / NoChange / ChapterAdded / ChapterDeleted / Failed)の一覧。★判定は**章の中で最初の違いが見つかった時点で打ち切る**(ページ数が違えばページを1枚も開かずに Changed)。章は**裏で1つずつ開いて閉じる**ので、文書を開いたままにしないし dirty にもしない。ダイアログは**モードレス**＝開いたまま文書を触れる。
														//     ★**進捗バーとキャンセルを入れた(2026-08-12。段階3 完了)＝この機能は提出説明に載せてよい。** 比較の間は進捗バーが出て、いま見ている章の名前を表示し、いつでもキャンセルできる(実測: 15章のブックで押した時点から 0.2〜0.5 秒で止まる)。**キャンセルしても、そこまでに判定できた章の結果は残る**——見ていない章は一覧に「NotCompared」と出て、要約の末尾が「- cancelled」になる。⚠**「NotCompared」と「NoChange」を混ぜないこと**が眼目で、中断した章を「変更なし」と報告すると、確かめていないものを確かめたと言うことになる。★あわせて**要約の文言を短くした**(「book compare: 15 chapters, ...」→「15 chapters: ...」・0件の項目は出さない)＝ステータス欄は中央で省略されるので、長いと **"book co...5 chapters" のように数字が壊れて別の数に見える**(2026-08-12 実測)。
														//     (旧記述「進捗バーとキャンセルはまだ無い。押すと固まると受け取られるので提出説明に載せない」は解消済み。実測では 100ページのブックに約3秒、変更の無い200ページで十数秒かかっていた。)
														//     ⚠**版数は 1.4.0 のまま**でよい。1.4.0 は**まだ提出していない**ので、公開版 1.3.0 から見た増分がこのリストに増えるだけ(1.2.1→1.3.0 のときと同じ考え方＝提出していない版数は繰り上げずに中身を足す)。
//   ⑥内部の安全修正だけ(2026-08-12)。★**提出説明には書かない** ＝ 画面にも操作にも見える変化が無い。ここに残すのは、次に差分を洗う人が「これは説明に要る変更か」を毎回考え直さずに済むようにするため。中身は2つ＝①**終了時に半透明トグルの購読を外す**(KESCMDetachPanelVisibilityObserver 新設。購読している間セッションが握るのはこの .pln の中へのポインタで、終了処理中のパネル破棄は実際に通知を飛ばす) ②**Win32 フックを外せなかったときハンドルを捨てない**(UnhookWinEvent が失敗する条件は3つあり、うち1つではフックが生きたまま残る＝捨てると二度と外せない)。★どちらも **KBS が先に直していて、こちらへ歩いてこなかった分**(KBS ブロック14 の3周目が兄弟報告として検出し、同日ユーザー指示で移植)。
//   ⑦UI の細部2件(2026-08-12 ユーザー指定。どちらも公開版 1.3.0 から見て目に見えるが、**提出説明に書くほどではない**)。①**Story Edits の分割バーをドラッグで動かせなくした** ＝ 上ペインは固定座標のコントロールの塊で正しい高さが1つしか無く(Top snap がその高さ)、下げられる方向だけが残っていて、下げると下に何も無い帯ができた。セクションの高さはパネルの縁のドラッグで決まる。②フライアウトの「Compare Books」を**Start の直下**へ(9.54→9.05)＝比較を始める項目を1つの群にまとめた。あわせて Hide Unchanged Spreads と**同じ位置番号 9.54 で重複していた**のも解消。
														//   ⑧★★★**PDF 書き出しに比較マークが出るようになった**(2026-08-15。公開版 1.3.0 から見て**いちばん大きな変化**)。従来は「Print comparison marks」を ON にしても、**File ▸ Export ▸ Adobe PDF で書き出した PDF には1画素も出なかった**(印刷には出ていた)。⇒ プラグインを **model と UI の2本に分割**し(KohakuChangeMarker.pln ＋ KohakuChangeMarkerUI.pln)、比較マークの描画をバックグラウンドスレッドから届く側へ移した。★**利用者から見た使い方は何も変わらない**——インストールは従来どおり両方を入れるだけで、パネルもメニューも同じ。
														//     ⚠**同時に PDF 書き出し用の描き方も作り直した**(2026-08-16 に完成)＝**印刷と PDF 書き出しを同じ処理に統一**し(アルファサーバ＋透明グループ)、**マークを CMYK で塗る**ようにした ⇒ **[PDF/X-1a] で書き出しても「非 CMYK カラー」の警告が出ない**。**印刷経路は従来どおり**。⚠★★**旧「提出説明に必ず書く要点＝書き出しの互換性は Acrobat 5(PDF 1.4)以上にすること」は 2026-08-20 に撤回した**＝下の⑪⑫で PDF 1.3 でも正しく半透明になったため。How to Use の但し書きも日英とも撤去済み。⚠★**旧記述「PDF 書き出しのポートは透明を一切通さないので、リングをベクターで塗り不透明度を色に溶かす」は 2026-08-16 に撤回した**(欠けていたのは公式が要求する追加初期化＝透明グループだけだった)＝**2026-08-17 の不具合再検査 B3 でここへ反映**。
														//   ⑨★★**ストーリーの変更カウンターをスクリプトから読めるようにした**(2026-08-15 ユーザー要望)＝`app.documents[0].stories[2].kcmChangeCount` ほか3本(`kcmTextChangeCount` / `kcmAttrChangeCount` / `kcmOtherChangeCount`)。すべて**読み取り専用の整数**。
														//     何の役に立つか: **Story Edits に行が出るかどうかを決めているのが、この集約カウンターの一致/不一致**。読めるようにしたことで「一覧が空なのは変更が無いからか、それとも見落としか」を**利用者自身が確かめられる**。⚠**カウンターは編集回数ではなく状態のバージョン番号**なので、**別々に作った2つの文書は、中身が違っても同じ値を持つことがある**——Story Edits が意味を持つのは「元の文書を別名保存して片方を編集した」対に対してであり、この4本はそれを外から確認する手段でもある。
														//     ⚠**KESCM の「メソッドは公開しない」方針は変えていない**(足したのは読み取り専用プロパティだけ)。既に DOM へ公開している `app.kcmStatus` / `app.kcmBookResult` と同じ性格。⚠**ただし `app.kcmBookResult` は「公開済み」ではない**＝2026-08-11 追加で、**提出した 1.3.0 のビルドには入っていない**(このファイル冒頭が警告している取り違えを、この行自身がしていた＝2026-08-18 の再検査 B11 で訂正)。
//     ⑨-b ★★**`app.kcmBookResult`(2026-08-11)も 1.3.0 から見た増分**＝⑤のブック比較の結果を**章ごと1行のタブ区切り**でスクリプトから読める読み取り専用プロパティ。⚠**旧記述はこれを増分に数え落としていた**（⑤にも⑨にも無かった）＝**機能を列挙する説明文は、対応を1つ足すたびに列挙そのものを数え直す**(④の TSV で 2026-08-11 に踏んだのと同じ抜け方)。
//     ⑨-c ★★**ストーリーの4カウンターは IDML に書き出されない**(2026-08-18)＝スクリプトからは読めるまま、**利用者が書き出す IDML には一切現れない**。プロパティは既定では IDML の属性になる(実測で全 `<Story>` に載っていた)ので、INX 専用のスクリプトリソースで明示的に除外した。**提出説明に書く必要は無い**(利用者から見て何も変わらない)が、**IDML を扱う利用者にとっては「自作プラグインがファイルを汚さない」保証**なので、訊かれたら答えられるようにここに残す。
														//   ⑩⚠★★**利用者の設定が初期化される**(1.3.0 からの更新時に1回だけ)。**リリースノートに必ず書く**——書かないと不具合として報告される種類の変化: ①**キーボードショートカットの割り当て** ②**パネルの配置**(表示されない場合はウィンドウメニューから開き直す) ③**Story Edits セクションの高さ** ④**ツールの選択状態**。いずれも UI 側が別プラグインになり ID が振り替わったため。**比較の結果や保存した Check/Register のデータには影響しない。**
														//   ⑪★★★**比較マークが PDF 1.3(Acrobat 4)でも半透明で出るようになった**(2026-08-19。**1.6.0 で minor を上げた理由**)。
														//     何が変わったか: 従来は **PDF 1.3 で書き出すと、透明効果を1つも含まないページだけマークが「真っ赤なベタ塗り」になり、下の誌面が見えなくなっていた**(透明を含むページでは正しく半透明だった)。1.6.0 ではどのページでも半透明で出る。★**日本の入稿プリセットはほぼ 1.3**(PDF/X-1a・PDF/X-3・雑誌広告送稿用＝すべて Acrobat 4)なので、実務ではここが効く。
														//     ⚠**利用者から見た操作は何も変わらない**(トグルも設定も増えていない)。「1.3 で書き出しても枠が正しく出る」とだけ書けばよい。
														//     ★実測(同一文書・同一プリセット `[雑誌広告送稿用]`・透明を含まないページ)＝**全面ベタ 850,175 画素 → 半透明のリング 74,503 画素**。同期書き出しと UI の書き出し(バックグラウンド)の両方で確認。
														//     ⚠★★★**2026-08-20 訂正＝⑪だけでは半分しか直っていなかった。** 上の実測は「**透明を含まないページ**」で採ったが、その**文書には別のページに透明があった**。**文書全体で透明が1つも無い**と、⑪を入れても全面ベタのまま出る(手でも再現)。⇒ 下の⑫で解消。**「ページに透明が無い」と「文書に透明が無い」を測り分けていなかったのが誤りの正体。**
														//   ⑫★★★**透明を1つも含まない文書でも PDF 1.3 で半透明になった**(2026-08-20。⑪の残り半分)。**入稿用の文書はむしろ透明を1つも使わないのが普通**なので、実務ではここまで揃って初めて効く。
														//     原因: フラットナ(透明の平坦化)は「文書に透明がある」ときしか動かず、その判定は **`IXPManager` の「透明を持つページアイテムの一覧」** だけで決まる。KESCM のアドーンメントは**セッションに登録するだけでどのアイテムにも属さない**ので、その一覧に自力では入れなかった ⇒ 申告口(⑪)を持っていても**誰も聞きに来ない**。
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
														//   ■1.3.1 で撤去したもの: 「Translucent Toolbox」トグル(フローティング中の**ツールボックス**を半透明にする)。★★**提出説明に「機能を削除した」と書かないこと** ＝ **提出した 1.3.0 のビルドに最初から入っていない**(2026-08-07 ユーザー明言)ので、公開版から見れば存在しなかった機能。ActionID +38 は欠番のまま再利用しない。
														//   ⚠**①②とも「版数が 1.3.0 だった時期にコードへ入れた」もの**だが、提出した 1.3.0 のビルド(commit 5ff22c5 時点)には入っていない。**版数コメントが載っている位置で「提出済みか」を判断しない**。
														//
														// ■■【1.3.0 = 公開済み】公開版 1.2.0 から見た増分＝**1.3.0 の提出説明に使用済み。次の提出では繰り返さない**。
														// ■機能追加(minor を上げた理由):
														//   ①app.kcmStatus = パネルのステータス行の最後の1行をスクリプト/COM から読む読み取り専用プロパティ(KESCMScriptProvider.cpp)。パネルを閉じていても答える。実機検証の自動化用。
														//   ②Translucent Pages Panel = **本体のページパネル**もホバーで不透明に戻る半透明にできる(フライアウト。従来は自分のパネルだけ)。窓の特定を WidgetID 狙い撃ちへ全面入れ替えたことで可能になった(本体パネルの窓タイトルは UI 言語で変わるため)。
														//   ③ツールに ScriptID を与えた = app.toolBoxTools.currentTool から KESCM ツールを読み書きできる(UITools.KOHAKU_CHANGE_MARKER_TOOL)。従来は en_None で「何も選ばれていない」と区別できなかった。
														// ■機能変更(1.3.0 の提出説明で記載済み): 押下中 HUD を作り直した。公開版 1.2.0 は sprite 層で**相手の文書名**を出す作りで、押下を抜けた後に one-shot タイマーで描くため枠より遅れて出ていた。新版は比較マークの枠と同じ Draw Event で描くので**枠と同時に出る**。出す内容も文書名ではなく**押した窓の役割**(Target / Source / Not in comparison / Not comparing)に変えた(2026-07-27 ユーザー指示「相手の文書名は出さない」)。位置・見た目は旧版と同じ(ビュー左上・文字20px・不透明度0.6)。フライアウトの「Show HUD」トグルと設定キー hudOn は廃止＝**常に出る**。★2026-08-06 に一度全廃し、2026-08-07 に作り直した経緯があるので、「削除した機能」として説明しないこと。
														// ■表示・操作の整理: About を「名前 版数」1行に(英語のみ)／日本語で出すのは How to Use と Hide Unchanged の確認アラートの2箇所だけに整理(メニュー・パネル・ステータス行は全ロケール英語)／パネル幅 +10px／Hide Unchanged と Start(文書2つ未満)と Find Overset(走査対象なし)を条件付きで灰色化／ページパネル右クリックのメニュー接頭辞を KESCM:→KCM: に短縮／Prev/Next のラベル整理。
														// ■不具合修正・内部改善: ジャンプ前にスプレッド切替(マスターページへ飛べるように)＋マスターページのあふれを巡回一覧に載せる／Find Overset で押し出された表のセルを報告しない／あふれを聞く前にリコンポーズ／表の列挙を ITextStoryThreadDictHier へ(入れ子の表が入る)／描画エンジンの見直し(greek 無効・除外領域の緑は画面限定・除外矩形のキャッシュ化)／LocaleIndex に k_Wild 追補(列挙外 UI 言語での生キー表示を防ぐ)／比較失敗ページの可視化(failed=N・Refresh で既存枠を消さない)／半透明の細部(はみ出しメニュー・AutoAttach の OFF ガード・Shutdown 後の再武装禁止)。
														// ■全14ブロックの API 監査(2026-08-05〜08-07)とバグ特化の全コード再点検(08-06)を実施済み。全文=docs/ai-notes/kescm-file-map.md の各ブロックノート／kescm-bug-recheck-2026-08-06.md。
// (kKESCMAuthor はテンプレート残骸(どこからも未参照)のため削除 2026-07-25)

// Plug-in Prefix ＋ 境界の ID:
//
// ★★2026-08-15（第2段 Task 6B）: `kKESCMPrefixNumber` / `kKESCMPrefix` / `kKESCMStringPrefix` と、
//   **model と UI の両方が同じ値で知っていなければならない ID**（Facade 5本の IID・通知の
//   protocol IID・MessageID 7本）は **KESCMBoundaryID.h** へ移した。
//   ⚠**あちらは KCMUI 側にも同じ内容のコピーがある。片方だけ直すと黙ってずれる。**
//   このファイルに残るのは **model 専用の ID** だけ。
#include "KESCMBoundaryID.h"

// Missing plug-in: (see ExtraPluginInfo resource)
#define kKESCMMissingPluginURLValue		kSDKDefPartnersStandardValue_enUS // URL displayed in Missing Plug-in dialog
#define kKESCMMissingPluginAlertValue	kSDKDefMissingPluginAlertValue // Message displayed in Missing Plug-in dialog - provide a string that instructs user how to solve their missing plug-in problem

// PluginID: ★2026-08-15（第2段 Task 11）に KESCMBoundaryID.h へ移した
//   ＝UI 側が `PluginDependency` で依存先として名指しするので、両側が同じ値を知る必要がある。

// ClassIDs:
// ★★2026-08-15（第2段 Task 6B-2）: **UI 側の boss 20 個は ui/KCMUIID.h へ移した**（+7 +8 +9 +11〜+27）。
//   オフセットは動かしていない＝あちらで `kKCMUIPrefix + 同じ数字` になっているだけ。
// ★★2026-08-17（不具合再検査 B1・D-2）に全数を実測して訂正: **UI 側の ClassID は今 21 個で +28 まで伸びている**
//   （+7 +8 +9 +11〜+28）。増えた1つは **+28 `kKESCMBookPathTextWidgetBoss`**（ui/KCMUIID.h:141＝ブック比較
//   ダイアログの Target:/Source: 行）で、**移動ではなく同じ 2026-08-15 の新設**。⇒ 上の「移した 20 個」は
//   その時点として正しく、**その後に増えた分がこの台帳へ来ていなかった**だけ。
//   ⚠ よって **この帯の +7 +8 +9 +11〜+28 は「空き」ではない**（UI 側の同じ数字と対応が付いている）。
//   ★これは「値が衝突する」という意味ではない（model は 0x1EA5 0x〜 / UI は 0x1EA5 8x〜で別物）。
//     **番号を読み替えるための手がかり**として空けてある、という意味。
//   ★ここに残るのが「窓が無くても成り立つ仕事」＝比較マークの描画・起動終了・文書クローズ・ScriptProvider。
DECLARE_PMID(kClassIDSpace, kKESCMScriptProviderBoss, kKESCMPrefix + 3)	// ★このプラグインが公開する ScriptProvider は**これ1つだけ**＝app の2プロパティ(kcmStatus/kcmBookResult)と story の4カウンターを両方この boss が serve する(2026-08-15 に集約)。.fr は**同じ boss に Provider ブロックを2つ**書いて Object ごとに Property を分けている(KESCM.fr の末尾2ブロック)。旧スクリプトAPI(kescmToast 等)は撤去済みで、公開するのは読み取り専用プロパティだけ
// kKESCMDrawEventServiceBoss (kKESCMPrefix + 4) は 2026-08-20 に廃止＝マークの描画を kKESCMRingAdornmentBoss(グローバルページアイテムアドーンメント)へ**一本化**し、Draw Event の受け口を撤去した。スロットは予約のまま。
// kKESCMPeekWatcherBoss (kKESCMPrefix + 5) は中ボタンウォッチャ撤去(2026-07-13)により廃止。スロットは予約のまま。
DECLARE_PMID(kClassIDSpace, kKESCMPeekStartupBoss, kKESCMPrefix + 6)	// IStartupShutdown: アプリ起動時に peek ウォッチャを開始
DECLARE_PMID(kClassIDSpace, kKESCMDocResponderServiceBoss, kKESCMPrefix + 10)	// IK2ServiceProvider+IResponder: ドキュメントクローズ監視(閉じた文書の追跡状態を確定クリーンアップ)
DECLARE_PMID(kClassIDSpace, kKESCMBeforeSaveResponderServiceBoss, kKESCMPrefix + 11)	// IK2ServiceProvider+IResponder: ★**保存の「前」**の監視。Hide Unchanged で隠したスプレッドを、ファイルへ書かれる前に戻す(2026-08-19＝致命性再検査 軸①)。⚠**「閉じる前(kBeforeCloseDoc)」では遅い**＝実測で保存のほうが先だった(理由は KESCMDocResponder.cpp)
// kKESCMStoryScriptProviderBoss (kKESCMPrefix + 11) は 2026-08-15 に廃止。スロットは予約のまま(再利用しない)。
//   ★経緯＝story の4カウンターを公開したとき「1つの boss では app と story を分けられない」と考えて2つ目の
//   boss を作ったが、**それが誤り**だった。ガイドの Provider element の定義が答え＝Property は「**直前の**
//   Object が指すオブジェクトに載る」(vol1-11:1302。Object 側は「**後続の**フィールドが載る」:1300)、かつ
//   provider は「**複数の場所で定義できる**」(vol1-11:1237)。⇒ **分けるのは boss ではなく Object/ブロック**。
//   公式の証拠＝basicshape が Adobe 自身の kPageItemScriptProviderBoss に**3ブロック**を与えている
//   (BscShp.fr:370-404。Contexts は :317 の1件だけ)。⇒ ブロックを2つに保ったまま boss を一本化した。
//   ★実機で確認済み(2026-08-15)＝`'kcmChangeCount' in app` も `'kcmStatus' in story` も **false**（混ざらない）。
DECLARE_PMID(kClassIDSpace, kKESCMRingAdornmentBoss, kKESCMPrefix + 29)	// IAdornmentShape + IAdornmentFlattenerUsage: 比較マークをアドーンメントとして描く boss(KESCMRingAdornment.cpp)。★セッションの**グローバル**ページアイテムアドーンメントリスト(IID_IGLOBALPAGEITEMADORNMENTLIST)へ登録するので、文書には一切付けない=.indd を1バイトも変えない。★★番号を +7〜+28 から採らないのは上のとおり(あの帯は UI 側の同じ数字と対応が付いている)＝**新規は +29 以降から採る**
DECLARE_PMID(kClassIDSpace, kKESCMRingAdornmentStartupBoss, kKESCMPrefix + 30)	// IStartupShutdown: 上のアドーンメントを**実行コンテキストごとに**登録/解除するだけの boss。★★**kKESCMPeekStartupBoss とは別にする必要がある**＝あちらは Shutdown で比較状態を捨てるのでメインスレッド限定(kCMainThreadStartupShutdownProviderImpl)だが、こちらは**BG スレッドでも呼ばれなければ意味が無い**(セッションへの登録はスレッドをまたがない)
// ★★★2026-08-20: 透明の申告を「書き出し／印刷のあいだだけ」立てるための2 boss。
//   ⚠**なぜ要るか**＝`IXPManager` の「透明を持つページアイテムの一覧」は**文書側のデータで、`.indd` に永続する**
//     (2026-08-20 実測＝開き直しても再検証されない)。比較中ずっと載せておくと、ユーザーが保存した瞬間に
//     **根拠のない記録が文書へ焼き付く**(KESCM を持たない人が開いても残る)。⇒ **要る瞬間だけ載せて、すぐ降ろす。**
//   ★フラットナが要るのは**書き出しと印刷のときだけ**で、画面描画にもサムネイルにも一覧は要らない。
//   ★手本＝`customconditionaltext`(PDF と印刷の両方で「前に変えて後で戻す」を実装している唯一のサンプル)。
//   ⚠**保存の前後(kBeforeSaveDoc)には置かない**＝そこで落ちると文書を失う。書き出しなら失敗してもやり直せる
//     (2026-08-20 ユーザー判断＝「どこで失敗しても許される場所に置く」)。
DECLARE_PMID(kClassIDSpace, kKESCMPDFExportSetupBoss, kKESCMPrefix + 31)	// IK2ServiceProvider(Adobe 提供の kPDFExportSetupServiceImpl)+IPDFExportSetupProvider: PDF 書き出しの BeginExport で透明の一覧に載せ、EndExport で降ろす。★★★**非同期書き出しではここに「書き出し用のクローン db」が渡る**＝元の文書を一度も触らずに出力だけ変えられる(2026-08-20 実測)。⚠**旧 kKESCMExportXPResponderServiceBoss(同じ +31)の後継**＝あちらは kBeforeExport/kAfterExport/kFailedExport の3シグナルで**元の文書**に載せていたので、書き出し中に保存されると一覧が .indd に焼き付いた
														// ⚠**印刷側の対(kPrintSetupService+IPrintSetupProvider)は無い**＝公式に倣って一度書いたが(旧 +32/+50)、**2026-08-20 のユーザー判断で外した**。⚠**効かないからではない**＝載せれば印刷でもマークは濃くなる(実測 16,076 ⇔ 8,407 画素。どちらもベタにはならない)が、**印刷にそこまでの厳密性は要らない・印刷会社へ出すのは PDF** という判断。★A/B と復活手順は KESCMRingAdornment.cpp の節5。**次の新規 boss は +32 から採ってよい**

// InterfaceIDs:
// ★★+0〜+3（Observer 3本のアタッチ識別 ID ＋ Story Edits セクション高さ）は 2026-08-15（第2段
//   Task 6B-2）に **ui/KCMUIID.h へ移した**＝どれも UI 側の boss だけが使う。オフセットは据え置き。
// ★★+4〜+9（Facade 5本の IID ＋ 通知の protocol IID）は 2026-08-15（第2段 Task 6B）に
//   **KESCMBoundaryID.h へ移した**＝UI 側と同じ値でなければ意味を成さない ID だから。
//   ⚠ 番号は動いていない（+4..+9 のまま）。スロットとしては使用中なので再利用しないこと。
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 10)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 11)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 12)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 13)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 14)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 15)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 16)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 17)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 18)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 19)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 20)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 21)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 22)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 23)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 24)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 25)


// ImplementationIDs:
// ★★2026-08-15（第2段 Task 6B-2）: **UI 側の実装 26 本は ui/KCMUIID.h へ移した**（+5 +6 +7 +10〜+38 の UI 分）。
//   オフセットは据え置き。⚠ここに残る 10 本は ui/KCMUIFactoryList.h ではなく source/KESCMFactoryList.h に載る。
DECLARE_PMID(kImplementationIDSpace, kKESCMScriptProviderImpl, kKESCMPrefix + 0)	// CScriptProvider 実装(KESCMScriptProvider.cpp)。★★2026-08-17 訂正＝旧「app.kcmStatus を返す」は 2026-08-15 の統合前の姿。**この1本が公開6プロパティ全部を serve する**(app の2本＋story の4本)＝上の kKESCMScriptProviderBoss の説明が正
// kKESCMDrawEventSrvcImpl (kKESCMPrefix + 1) / kKESCMDrawEventHandlerImpl (kKESCMPrefix + 2) は 2026-08-20 に廃止(上の kKESCMDrawEventServiceBoss と同じ理由＝アドーンメントへの一本化)。スロットは予約のまま。
// kKESCMPeekWatcherImpl (kKESCMPrefix + 3) は中ボタンウォッチャ撤去(2026-07-13)により廃止。スロットは予約のまま。
DECLARE_PMID(kImplementationIDSpace, kKESCMPeekStartupImpl, kKESCMPrefix + 4)	// IStartupShutdown 実装(peek ウォッチャを開始)
// kKESCMDocServiceProviderImpl (kKESCMPrefix + 8) は自前 ServiceProvider の撤去(2026-08-06)により廃止。
// 1シグナルだけの responder は API 提供の kAfterCloseDocSignalRespServiceImpl(DocumentID.h)を .fr で
// 名指しすれば登録される(KESCM.fr の kKESCMDocResponderServiceBoss 参照)。スロットは予約のまま。
DECLARE_PMID(kImplementationIDSpace, kKESCMDocResponderImpl, kKESCMPrefix + 9)	// IResponder 実装(クローズ確定時の追跡状態クリーンアップ)
DECLARE_PMID(kImplementationIDSpace, kKESCMCompareFacadeImpl, kKESCMPrefix + 39)	// IKESCMCompareFacade 実装(KESCMFacades.cpp)。★kUtilsBoss へ AddIn する＝**必ず自作の実装**(SDK 提供の実装を既存 boss に足すと他社と衝突して起動に失敗する。衝突の単位は IID ではなく ImplementationID)
DECLARE_PMID(kImplementationIDSpace, kKESCMMarkDataImpl, kKESCMPrefix + 40)	// IKESCMMarkData 実装(KESCMFacades.cpp)。上と同じ kUtilsBoss への AddIn で、こちらは**読み取り専用**
DECLARE_PMID(kImplementationIDSpace, kKESCMPageFlagsFacadeImpl, kKESCMPrefix + 41)	// IKESCMPageFlagsFacade 実装(KESCMFacades.cpp)。同じく kUtilsBoss へ AddIn
DECLARE_PMID(kImplementationIDSpace, kKESCMStoryEditsFacadeImpl, kKESCMPrefix + 42)	// IKESCMStoryEditsFacade 実装(KESCMFacades.cpp)。同じく kUtilsBoss へ AddIn。★読み取り専用
DECLARE_PMID(kImplementationIDSpace, kKESCMBookFacadeImpl, kKESCMPrefix + 43)	// IKESCMBookFacade 実装(KESCMFacades.cpp)。同じく kUtilsBoss へ AddIn
DECLARE_PMID(kImplementationIDSpace, kKESCMBeforeSaveResponderImpl, kKESCMPrefix + 44)	// IResponder 実装(保存の前に Hide Unchanged の隠しスプレッドを戻す。KESCMDocResponder.cpp)
// kKESCMStoryScriptProviderImpl (kKESCMPrefix + 44) は 2026-08-15 に廃止(実装ごと kKESCMScriptProviderImpl へ
//   統合)。スロットは予約のまま(再利用しない)。理由は上の kKESCMStoryScriptProviderBoss の欠番コメント。
DECLARE_PMID(kImplementationIDSpace, kKESCMRingAdornmentImpl, kKESCMPrefix + 45)	// IAdornmentShape 実装(KESCMRingAdornment.cpp)。描画本体は持たず、スプレッドに対して KESCMDrawEventHandler::DrawSpreadMarks() をそのまま呼ぶ=描画ロジックは Draw Event 経路と1本のまま
DECLARE_PMID(kImplementationIDSpace, kKESCMRingFlattenerUsageImpl, kKESCMPrefix + 46)	// IAdornmentFlattenerUsage 実装(同上)。★★これが本命＝**透明マネージャに「このアドーンメントは透明を使う」と申告する唯一の口**。PDF 1.3(透明を含まないページ)でリングが全面ベタになる既知の制限を解くために足した。手本=transparencyeffect/TranFxFlattenerUsage.cpp
DECLARE_PMID(kImplementationIDSpace, kKESCMRingAdornmentStartupImpl, kKESCMPrefix + 47)	// IStartupShutdownService 実装(KESCMRingAdornment.cpp の末尾)。中身は Register/Unregister を呼ぶだけ。★**実行コンテキストごとに**呼ばれる必要があるので kKESCMPeekStartupImpl とは別の boss に載せる
// ★2026-08-20: 上の2 boss の実装(いずれも KESCMRingAdornment.cpp の末尾)。透明の申告と同じ関心事なので同居させる。
DECLARE_PMID(kImplementationIDSpace, kKESCMPDFExportSetupImpl, kKESCMPrefix + 48)		// IPDFExportSetupProvider 実装(KESCMRingAdornment.cpp)。★ServiceProvider 側は Adobe 提供の kPDFExportSetupServiceImpl をそのまま .fr で名指しするので、自作はこの1本だけ。手本=sdksamples/pdfvt。⚠**旧 kKESCMExportXPResponderImpl(同じ +48)の後継**
// kKESCMExportXPServiceProviderImpl (kKESCMPrefix + 49) は 2026-08-20 に廃止＝書き出しシグナル3本を1つの boss で受けるための自作 ServiceProvider だったが、PDF 書き出しサービスへ移して不要になった(あちらは ServiceProvider が Adobe 提供)。スロットは予約のまま。
														// ⚠**印刷側の IPrintSetupProvider 実装は無い**(旧 +50。理由は上の Class 側の注記と KESCMRingAdornment.cpp の節5)。**次の新規 Impl は +50 から採ってよい**

// MessageIDs: model が UI へ「何が変わったか」を知らせる通知(2026-08-13・model/UI 分割 第1段 Task 9)。
//   ★★2026-08-15（第2段 Task 6B）に **7本すべて KESCMBoundaryID.h へ移した**
//     ＝送り手（model）と受け手（UI）が同じ値を見ていなければ、ビルドは通るのに**黙って何も起きない**。
//   ⚠ 番号は動いていない（+0..+6）。以下は跡地の記録:
//     +0 kKESCMMarksRebuiltMessage / +1 kKESCMMarksClearedMessage / +2 kKESCMPageFlagsChangedMessage /
//     +3 kKESCMStoryEditsRebuiltMessage / +4 kKESCMStatusTextMessage / +5 kKESCMOversetRescannedMessage /
//     +6 kKESCMComparisonDocsClosedMessage
//   （下に残しているのは +6 を Stop と別建てにした理由の説明。移動先のヘッダーにも同じ説明がある。）
//   ★比較していた文書が閉じられ、Stop 相当の後片付けが済んだ(2026-08-13・Task 10)。
																					// ⚠**Stop(kKESCMMarksClearedMessage)とは別**にした理由＝UI 側の後始末が3点違う:
																					//   ①サムネイルの作り直しは**次の idle へ遅延**させる(前面切替の過渡で ForceRedraw が
																					//     効かず枠が残る＝2026-07-08 実機で確認)②**一括クローズ中は保留**して全部閉じ終えて
																					//     から1回だけ流す ③Find Overset が単独 ON 中なら strip は**残す**(赤帯だけ描き直す)。
																					//   ★付随データ＝**生存している側**の db を最大3つ(Target/旧版/Source側枠)。閉じた db は
																					//     決して渡さない(通知の受け手が deref するため)。


// スクリプト要素 ID。★**現役は +13〜+18 の6本**(下記。app の2本 + story の4本＝すべて読み取り専用プロパティ)。
// 旧スクリプトAPI(メソッド)は全撤去済みで **+1〜+12 がその跡地**＝再利用時は旧用途との衝突に注意
//   ⚠2026-08-17 訂正: 旧見出しは「スクリプトAPIは全撤去済み=+1〜+12 はすべて空き」だけで、**見出しだけ読むと
//     このスペース全体が空きに見えた**(実際は +13〜+18 が現役)。2026-08-15 に story の4本が加わった分。
// kScriptInfoIDSpace +1 は現在空き(ページ単位 kescmMarkChanges は廃止; kescmMarkChangesDoc を使う)
// kScriptInfoIDSpace +2 は現在空き(旧 kKESCMClearMarksMethodScriptElement; スクリプトAPI撤去)
// kScriptInfoIDSpace +3 は現在空き(旧 kKESCMMarkChangesDocMethodScriptElement; 同上)
// kScriptInfoIDSpace +4 は現在空き(kescmShowPageX 廃止; 対角線のページ × は撤去)
// kScriptInfoIDSpace +5 は現在空き(kescmShowOverset 廃止); 再利用時は衝突に注意
// kScriptInfoIDSpace +6 は現在空き(kescmShowOriginal 廃止; peek に統合)
// kScriptInfoIDSpace +7 は現在空き(kescmHideOriginal 廃止; kescmShowOriginal と対)
// kScriptInfoIDSpace +8 は現在空き(kescmShowOriginalUnderMouse 廃止; peek を使う)
// kScriptInfoIDSpace +9 は現在空き(旧 kKESCMArmMousePeekMethodScriptElement; スクリプトAPI撤去)
// kScriptInfoIDSpace +10 は現在空き(旧 kKESCMDisarmMousePeekMethodScriptElement; 同上)
// kScriptInfoIDSpace +11 は現在空き(旧 kKESCMToastMethodScriptElement; kescmToast はスクリプトAPIごと撤去)
// kScriptInfoIDSpace +12 は現在空き(旧 kKESCMSetPrintMarksMethodScriptElement; スクリプトAPI撤去)
// ★+1〜+12 は「メソッド」の跡地なので再利用せず、新しいプロパティは +13 から採る(旧用途と混同しないため)。
DECLARE_PMID(kScriptInfoIDSpace, kKESCMStatusPropertyScriptElement, kKESCMPrefix + 13)	// app.kcmStatus(読み取り専用。パネルのステータス行の最後の1行)
DECLARE_PMID(kScriptInfoIDSpace, kKESCMBookResultPropertyScriptElement, kKESCMPrefix + 14)	// app.kcmBookResult(読み取り専用。直近のブック比較の結果を章ごと1行の TSV「章名<TAB>状態」で返す)。★ステータス行は1行しか出せないので、章ごとの一覧を人手ゼロで検証するにはこの口が要る(狙いは kcmStatus と同じ)
// ★2026-08-15: story の変更カウンター4本(読み取り専用)。app.documents[0].stories[2].kcmChangeCount のように**ストーリーそのもの**から読む。
//   ⚠これは application ではなく **story オブジェクト**に載る＝.fr では**同じ kKESCMScriptProviderBoss の2つ目の
//     Provider ブロック**に置く(2026-08-15 に別 boss から集約。分けるのは boss ではなくブロック＝BscShp.fr:370-404)。
//   ★狙い＝**Story Edits が行を出すかどうかを決めている当の数値**(集約カウンター)を外から読めるようにすること。
//     これが読めないと「一覧が空」のとき、不具合なのか2文書が本当に同じ読みなのかを**ソースを読む以外に確かめる術がない**(2026-08-15 に実際に困った)。
//   ★★★2026-08-18(再検査 B11): **この4本は IDML/INX の DOM から明示的に外してある**＝`KESCM.fr` の
//     2つ目の `VersionedScriptElementInfo`(Contexts が `kINXScriptManagerBoss`／`Provider{kNotSupported}`)。
//     **プロパティは既定で IDML の属性になる**(実測＝全 `<Story>` と `<XmlStory>` に `KcmChangeCount` 等が
//     書き出されていた)。⇒ **要素 ID をこの4つと同じにして INX 側で打ち消す**形なので、
//     **この4行の ID を変えたら、あちらの4行も一緒に変える**(片方だけ直すと黙って IDML に出る)。
DECLARE_PMID(kScriptInfoIDSpace, kKESCMChangeCountPropertyScriptElement, kKESCMPrefix + 15)	// stories[n].kcmChangeCount(集約。ITextModel::GetChangeCount)
DECLARE_PMID(kScriptInfoIDSpace, kKESCMTextChangeCountPropertyScriptElement, kKESCMPrefix + 16)	// stories[n].kcmTextChangeCount(本文。GetTextChangeCount)
DECLARE_PMID(kScriptInfoIDSpace, kKESCMAttrChangeCountPropertyScriptElement, kKESCMPrefix + 17)	// stories[n].kcmAttrChangeCount(書式。GetAttrChangeCount)
DECLARE_PMID(kScriptInfoIDSpace, kKESCMOtherChangeCountPropertyScriptElement, kKESCMPrefix + 18)	// stories[n].kcmOtherChangeCount(その他。GetOtherChangeCount)
// ★★2026-08-20 追加。⚠**上の4本と同じく、KESCM.fr の「2つ目の VersionedScriptElementInfo」にも同じ ID を書く**(片方だけ直すと黙って IDML に出る)。
DECLARE_PMID(kScriptInfoIDSpace, kKESCMTransparencyItemCountPropertyScriptElement, kKESCMPrefix + 19)	// document.kcmTransparencyItemCount(読み取り専用。IXPManager の「透明を持つページアイテムの一覧」の件数＝**載せたまま保存していないかを外から確かめる口**。一覧は .indd に永続するので、保存→閉じる→開き直して読めば「書き込まれたか」が判る)
// (ツールの列挙子は本体の kToolBoxEnumScriptElement に載せるので、こちら側の ID は要らない。)

// StringKeys:
// ★★2026-08-15（第2段 Task 6B-2）: **model 側に残る文字列キーはこの1本だけ**。他は全部 ui/KCMUIID.h。
//   ⚠ キーの**値**は `kKESCMStringPrefix`（＝model の prefix "2008320"）のまま両側で使う
//     ＝文字列キーはグローバルに一意でなければならず、widget ID のように借用できないため
//     （ガイド vol2-12:71）。UI 側へ移したキーも値は1文字も変わっていない。
// ⚠ この1本のためだけに **model 側 .fr にも StringTable が要る**（KESCM_enUS.fr）。
#define kKESCMHideConfirmKey		kKESCMStringPrefix "kKESCMHideConfirmKey"	// その確認ダイアログ本文(enUS=英語。日本語UIは KESCMLoc.h の実行時切替 2026-08-05)

// Initial data format version numbers
#define kKESCMFirstMajorFormatNumber  RezLong(1)
#define kKESCMFirstMinorFormatNumber  RezLong(0)

// Data format version numbers for the PluginVersion resource 
#define kKESCMCurrentMajorFormatNumber kKESCMFirstMajorFormatNumber
#define kKESCMCurrentMinorFormatNumber kKESCMFirstMinorFormatNumber

#endif // __KESCMID_h__
