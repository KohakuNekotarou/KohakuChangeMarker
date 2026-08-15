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
//#define kKESCMVersion		"1.4.0"						// Version of this plug-in。About ボックス本文・.rc の FileVersion・PluginVersion リソースの3か所に出る。1.0.1 → 1.1.0(2026-07-25) → 1.1.1(2026-07-26) → 1.2.0(2026-07-30) → 1.2.1(2026-08-06) → 1.3.0(2026-08-07) → 1.3.1(2026-08-07) → 1.4.0(2026-08-09)。
														// ★1.4.0 で minor を上げた理由 = **新機能 Story Edits**(パネル下部に開閉セクションを設け、変更のあったストーリーを一覧する)。1.2.1→1.3.0 のときと同じ基準＝機能追加が入るなら patch では足りない。★**2026-08-10 に段階4(ジャンプ)まで完成**(下記③)。
														// ★Adobe Exchange の公開版は **1.3.0**(2026-08-07 承認・公開)。1.2.1 は提出しないまま 1.3.0 へ繰り上げた(機能追加が入ったので patch では足りない)。
														// ★★**「版数が◯◯だった時期にコードへ入れた」ことと「提出した◯◯のビルドに入っている」ことは別物**。取り違えると提出説明を誤る(2026-08-07 に実際に踏んだ)ので、増分は**提出したビルドを境に**2段へ分けてある。★提出文を起こすときは【次に提出する分】だけを読むこと。
														//
														// ■■【1.4.0 = 次に提出する分】★公開版 1.3.0 から見た増分。**提出説明はここだけを使う**。(版数は 1.3.1 から繰り上げ。中身は①②に③が加わっただけで、①②は 1.3.1 のときと同一)
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
//   ④★★**マスターページも比較するようになった**(2026-08-11。公開版 1.3.0 から見て新機能)。従来は通常スプレッドだけが比較対象で、マスターページの変更は枠に出なかった(あふれ「+」だけは出ていた)。マスタースプレッドは**名前**(A-親ページ 等)で Target/Source を対応付ける＝順番で組むと、片方にマスターが1つ増えただけで別のマスターどうしを比べてしまうため。片方にしか無い名前のマスターは比較しない。あわせて**スクロールバー地図が表示中のスプレッドに追従**するようになり(マスター表示中はそのマスターの内容だけを出す。マークが無ければ空)、**Prev/Next の巡回にもマスターページの枠が入る**。★**「Sync Layout Views」と「Align Other Views to Active」もマスターに対応**(同日追加)＝一方の窓でマスタースプレッドを表示すると、もう一方の窓も**同じ名前のマスターへ移動して同じ位置を映す**(スクロールだけでは別スプレッドへ届かないので、追従側のスプレッドも切り替える)。**相手側にそのマスターが無ければ追従側は動かさない**(Added ページと同じ扱い)。★**TSV 出力(Export Changed Pages)もマスターページを出す**(2026-08-11 に対応。⚠旧記述「TSV だけは通常ページのみ」は失効)＝Page 列にマスタースプレッド名(「A-親ページ」。見開きマスターは「A-親ページ (2)」のようにスプレッド内の位置を添える)を出し、種別は Changed。⚠**挿入/削除(Inserted/Deleted)は通常ページだけ**＝マスターは名前で対応付けるので、片方にしか無いマスターはそもそも比較対象にならず「増えた/減った」という状態を持たない。
//   ⑤★★**ブック比較(Compare Books) = パネルのフライアウトに追加した新機能**(2026-08-11。公開版 1.3.0 から見て新機能)。開いている2つのブックを**章単位**で比較し、章ごとに「変わった/変わらない」を出す。Target は**ブックパネルで前面タブのブック**、Source はそれ以外で最初に開いているブック——ダイアログの上2行に両方の名前が出るので、押す前に対象を確かめられる。結果は章名と判定(Changed / NoChange / ChapterAdded / ChapterDeleted / Failed)の一覧。★判定は**章の中で最初の違いが見つかった時点で打ち切る**(ページ数が違えばページを1枚も開かずに Changed)。章は**裏で1つずつ開いて閉じる**ので、文書を開いたままにしないし dirty にもしない。ダイアログは**モードレス**＝開いたまま文書を触れる。
														//     ★**進捗バーとキャンセルを入れた(2026-08-12。段階3 完了)＝この機能は提出説明に載せてよい。** 比較の間は進捗バーが出て、いま見ている章の名前を表示し、いつでもキャンセルできる(実測: 15章のブックで押した時点から 0.2〜0.5 秒で止まる)。**キャンセルしても、そこまでに判定できた章の結果は残る**——見ていない章は一覧に「NotCompared」と出て、要約の末尾が「- cancelled」になる。⚠**「NotCompared」と「NoChange」を混ぜないこと**が眼目で、中断した章を「変更なし」と報告すると、確かめていないものを確かめたと言うことになる。★あわせて**要約の文言を短くした**(「book compare: 15 chapters, ...」→「15 chapters: ...」・0件の項目は出さない)＝ステータス欄は中央で省略されるので、長いと **"book co...5 chapters" のように数字が壊れて別の数に見える**(2026-08-12 実測)。
														//     (旧記述「進捗バーとキャンセルはまだ無い。押すと固まると受け取られるので提出説明に載せない」は解消済み。実測では 100ページのブックに約3秒、変更の無い200ページで十数秒かかっていた。)
														//     ⚠**版数は 1.4.0 のまま**でよい。1.4.0 は**まだ提出していない**ので、公開版 1.3.0 から見た増分がこのリストに増えるだけ(1.2.1→1.3.0 のときと同じ考え方＝提出していない版数は繰り上げずに中身を足す)。
//   ⑥内部の安全修正だけ(2026-08-12)。★**提出説明には書かない** ＝ 画面にも操作にも見える変化が無い。ここに残すのは、次に差分を洗う人が「これは説明に要る変更か」を毎回考え直さずに済むようにするため。中身は2つ＝①**終了時に半透明トグルの購読を外す**(KESCMDetachPanelVisibilityObserver 新設。購読している間セッションが握るのはこの .pln の中へのポインタで、終了処理中のパネル破棄は実際に通知を飛ばす) ②**Win32 フックを外せなかったときハンドルを捨てない**(UnhookWinEvent が失敗する条件は3つあり、うち1つではフックが生きたまま残る＝捨てると二度と外せない)。★どちらも **KBS が先に直していて、こちらへ歩いてこなかった分**(KBS ブロック14 の3周目が兄弟報告として検出し、同日ユーザー指示で移植)。
//   ⑦UI の細部2件(2026-08-12 ユーザー指定。どちらも公開版 1.3.0 から見て目に見えるが、**提出説明に書くほどではない**)。①**Story Edits の分割バーをドラッグで動かせなくした** ＝ 上ペインは固定座標のコントロールの塊で正しい高さが1つしか無く(Top snap がその高さ)、下げられる方向だけが残っていて、下げると下に何も無い帯ができた。セクションの高さはパネルの縁のドラッグで決まる。②フライアウトの「Compare Books」を**Start の直下**へ(9.54→9.05)＝比較を始める項目を1つの群にまとめた。あわせて Hide Unchanged Spreads と**同じ位置番号 9.54 で重複していた**のも解消。
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
//   ⚠ よって **この帯の +7 +8 +9 +11〜+27 は「空き」ではない**（UI 側の同じ数字と対応が付いている）。
//   ★ここに残る4つが「窓が無くても成り立つ仕事」＝比較マークの描画・起動終了・文書クローズ・ScriptProvider。
DECLARE_PMID(kClassIDSpace, kKESCMScriptProviderBoss, kKESCMPrefix + 3)	// app.kcmStatus を返す ScriptProvider(2026-08-06 復活。旧スクリプトAPI(kescmToast 等)は撤去済みで、公開するのは読み取り専用の1プロパティだけ)
DECLARE_PMID(kClassIDSpace, kKESCMDrawEventServiceBoss, kKESCMPrefix + 4)
// kKESCMPeekWatcherBoss (kKESCMPrefix + 5) は中ボタンウォッチャ撤去(2026-07-13)により廃止。スロットは予約のまま。
DECLARE_PMID(kClassIDSpace, kKESCMPeekStartupBoss, kKESCMPrefix + 6)	// IStartupShutdown: アプリ起動時に peek ウォッチャを開始
DECLARE_PMID(kClassIDSpace, kKESCMDocResponderServiceBoss, kKESCMPrefix + 10)	// IK2ServiceProvider+IResponder: ドキュメントクローズ監視(閉じた文書の追跡状態を確定クリーンアップ)


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
DECLARE_PMID(kImplementationIDSpace, kKESCMScriptProviderImpl, kKESCMPrefix + 0)	// CScriptProvider 実装(app.kcmStatus を返す。KESCMScriptProvider.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMDrawEventSrvcImpl, kKESCMPrefix + 1)
DECLARE_PMID(kImplementationIDSpace, kKESCMDrawEventHandlerImpl, kKESCMPrefix + 2)
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


// スクリプト要素 ID(スクリプトAPIは全撤去済み=+1〜+12 はすべて空き。再利用時は旧用途との衝突に注意)
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
