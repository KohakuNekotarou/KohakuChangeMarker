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

// Company:
#define kKESCMCompanyKey	"KohakuNekotarou"	// Company name used internally for menu paths and the like. Must be globally unique, only A-Z, 0-9, space and "_".
#define kKESCMCompanyValue	"KohakuNekotarou"	// Company name displayed externally.

// Plug-in:
#define kKESCMPluginName	"KohakuExtendScriptChangeMarker"			// Name of this plug-in. 内部名(ID系・.rc の InternalName)。互換のため据え置き。
#define kKESCMDisplayName	"Kohaku Change Marker"			// 表示名(About メニュー項目・About ボックス本文・パネル/ツール名)。KBS の "Kohaku Search Panel" に合わせ、単語間をスペースで区切る(2026-07-25)。
#define kKESCMFileName		"KohakuChangeMarker"			// 出力ファイル名の基底(.rc の OriginalFilename)。vcxproj の TargetName と一致させること。表示名と違いスペースは入れない。
#define kKESCMPrefixNumber	0x205515 		// Unique prefix number for this plug-in(*Must* be obtained from Adobe Developer Support).
#define kKESCMVersion		"1.4.0"						// Version of this plug-in。About ボックス本文・.rc の FileVersion・PluginVersion リソースの3か所に出る。1.0.1 → 1.1.0(2026-07-25) → 1.1.1(2026-07-26) → 1.2.0(2026-07-30) → 1.2.1(2026-08-06) → 1.3.0(2026-08-07) → 1.3.1(2026-08-07) → 1.4.0(2026-08-09)。
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

// Plug-in Prefix: (please change kKESCMPrefixNumber above to modify the prefix.)
#define kKESCMPrefix		RezLong(kKESCMPrefixNumber)				// The unique numeric prefix for all object model IDs for this plug-in.
#define kKESCMStringPrefix	SDK_DEF_STRINGIZE(kKESCMPrefixNumber)	// The string equivalent of the unique prefix number for  this plug-in.

// Missing plug-in: (see ExtraPluginInfo resource)
#define kKESCMMissingPluginURLValue		kSDKDefPartnersStandardValue_enUS // URL displayed in Missing Plug-in dialog
#define kKESCMMissingPluginAlertValue	kSDKDefMissingPluginAlertValue // Message displayed in Missing Plug-in dialog - provide a string that instructs user how to solve their missing plug-in problem

// PluginID:
DECLARE_PMID(kPlugInIDSpace, kKESCMPluginID, kKESCMPrefix + 0)

// ClassIDs:
DECLARE_PMID(kClassIDSpace, kKESCMScriptProviderBoss, kKESCMPrefix + 3)	// app.kcmStatus を返す ScriptProvider(2026-08-06 復活。旧スクリプトAPI(kescmToast 等)は撤去済みで、公開するのは読み取り専用の1プロパティだけ)
DECLARE_PMID(kClassIDSpace, kKESCMDrawEventServiceBoss, kKESCMPrefix + 4)
// kKESCMPeekWatcherBoss (kKESCMPrefix + 5) は中ボタンウォッチャ撤去(2026-07-13)により廃止。スロットは予約のまま。
DECLARE_PMID(kClassIDSpace, kKESCMPeekStartupBoss, kKESCMPrefix + 6)	// IStartupShutdown: アプリ起動時に peek ウォッチャを開始
DECLARE_PMID(kClassIDSpace, kKESCMThumbIdleTaskBoss, kKESCMPrefix + 7)	// IIdleTask: クローズ後の Pages サムネイル再生成を次のidleに遅延(旧 kKESCMToastIdleTaskBoss のスロット転用)
DECLARE_PMID(kClassIDSpace, kKESCMPanelWidgetBoss, kKESCMPrefix + 8)	// ChangeMarker 操作パネル(パレット)
DECLARE_PMID(kClassIDSpace, kKESCMActionComponentBoss, kKESCMPrefix + 9)	// About メニューのアクションコンポーネント
DECLARE_PMID(kClassIDSpace, kKESCMDocResponderServiceBoss, kKESCMPrefix + 10)	// IK2ServiceProvider+IResponder: ドキュメントクローズ監視(閉じた文書の追跡状態を確定クリーンアップ)
DECLARE_PMID(kClassIDSpace, kKESCMIconWidgetBoss, kKESCMPrefix + 11)	// kRollOverIconButtonBoss を継承し IID_ITIP を追加(パネルイラストのツールチップ)
DECLARE_PMID(kClassIDSpace, kKESCMScrollMapWidgetBoss, kKESCMPrefix + 12)	// kGenericPanelWidgetBoss+自前IControlView: 縦スクロールバー脇の枠ページ地図strip(旧 kKESCMLayoutSyncObserverBoss のスロット転用)
DECLARE_PMID(kClassIDSpace, kKESCMToolBoss, kKESCMPrefix + 13)	// kGenericToolBoss継承: ツールボックスの peek 専用ツール(KESCMTool.cpp)
DECLARE_PMID(kClassIDSpace, kKESCMTrackerBoss, kKESCMPrefix + 14)	// ツールのキャプチャ型トラッカー(IID_ITRACKER+IID_IEVENTHANDLER)。左ボタン hold 中だけ reveal。KESCMTracker.cpp
DECLARE_PMID(kClassIDSpace, kKESCMTrackerRegisterBoss, kKESCMPrefix + 15)	// トラッカー登録(kLayoutWidgetBoss×ツール→トラッカー)。KESCMTrackerRegister.cpp
// (unused-slot placeholders below start at +16; +6..+15 are declared above. 2026-08-05 audit)
DECLARE_PMID(kClassIDSpace, kKESCMStorySectionToggleBoss, kKESCMPrefix + 16)	// kRollOverIconButtonBoss継承+IID_IOBSERVER: パネル下部「Story Edits」セクションの開閉ボタン(三角)。絵は本体の kTreeBranchCollapsed/Expanded を借りる
DECLARE_PMID(kClassIDSpace, kKESCMStorySectionPanelBoss, kKESCMPrefix + 17)	// kGenericPanelWidgetBoss継承+IID_IKESCMSAVEDSECTIONHEIGHT(kPersistIntDataImpl): 下ペイン本体。閉じる直前の高さをここに覚える(手本=製品 linksui の kLinkInfoPanelWidgetBoss)
DECLARE_PMID(kClassIDSpace, kKESCMStoryTreeWidgetBoss, kKESCMPrefix + 18)	// kTreeViewWidgetBoss継承: Story Edits の一覧(平坦1階層)。載せるのは adapter と widget mgr の2つだけ＝コントローラーは kTreeViewWidgetBoss が既に持っている
DECLARE_PMID(kClassIDSpace, kKESCMStoryRowWidgetBoss, kKESCMPrefix + 19)	// kTreeNodeWidgetBoss継承: 一覧の1行。載せるのは IID_IEVENTHANDLER(kKESCMStoryRowEHImpl)の1つだけ＝単クリックでジャンプ・ダブルクリックで全文選択。⚠2026-08-11 まで空の Class だった(段階4 で足した)
DECLARE_PMID(kClassIDSpace, kKESCMStoryRowCellBoss, kKESCMPrefix + 20)	// kInfoStaticTextWidgetBoss継承+IID_ITIP(kKESCMNoTipImpl): 一覧の行のセル。★狙いはツールチップを**消す**こと＝素の静的テキストは省略表示すると全文をポップアップで出す(実機ダンプ: kStaticTextWidgetBoss が IID_ITIP=kTextWidgetTipImpl を持つ)。行に出るのは邪魔なので空の tip を返す実装で上書きする(2026-08-10 ユーザー指定)
// ブック比較のダイアログ(2026-08-11)。★モードレス＝開いたまま文書を触れる。だから未決の
// 「行クリックでその章を開く」を後から足せる(モーダルだとその道が閉じる)。stock の kDialogBoss に
// 自前の IDialogController を載せるだけ＝KESCL の Jump Offset ダイアログと同じ形。
DECLARE_PMID(kClassIDSpace, kKESCMBookDialogBoss, kKESCMPrefix + 21)
// ブック比較ダイアログの中の章一覧(2026-08-11)。★Story Edits の一覧と**同じ3点セット**＝
// ツリー本体(adapter+widget mgr)／行／行のセル。あちらとの違いは住む場所だけで、
// パレットではなくダイアログに載る＝テーマが kIDDialogTheme・フォントがダイアログ用になる。
DECLARE_PMID(kClassIDSpace, kKESCMBookTreeWidgetBoss, kKESCMPrefix + 22)	// kTreeViewWidgetBoss継承: 章の一覧(平坦1階層)。載せるのは adapter と widget mgr の2つだけ
DECLARE_PMID(kClassIDSpace, kKESCMBookRowWidgetBoss, kKESCMPrefix + 23)	// kTreeNodeWidgetBoss継承: 一覧の1行。★今は何も足していない空の Class＝行クリック(段階4「その章を開く」)で IID_IEVENTHANDLER を載せる場所として先に採ってある。Story Edits の行 boss がたどったのと同じ順序
DECLARE_PMID(kClassIDSpace, kKESCMBookRowCellBoss, kKESCMPrefix + 24)	// kInfoStaticTextWidgetBoss継承+IID_ITIP(kKESCMNoTipImpl): 行のセル。素の静的テキストは省略表示すると全文をポップアップで出すので、一覧の行では黙らせる(Story Edits と同じ判断=2026-08-10 ユーザー指定)
// パネルを上下に割る分割バー(2026-08-12)。★中身は素の kSplitterPanelWidgetBoss と同じで、
// IID_IEVENTHANDLER だけを「何もしない」実装に差し替えてある＝**バーをドラッグして動かせなくする**
// (ユーザー指定 2026-08-12)。継承した boss からインターフェイスを**取り除く道は無い**ので、消し方は
// 「別の答えを返す実装で上書きする」になる ---- kKESCMStoryRowCellBoss(+20)がツールチップを黙らせたのと同じ形。
DECLARE_PMID(kClassIDSpace, kKESCMSplitterPanelBoss, kKESCMPrefix + 25)	// kSplitterPanelWidgetBoss継承+IID_IEVENTHANDLER(kKESCMSplitterEHImpl): 分割バーを掴めない SplitterPanelWidget
DECLARE_PMID(kClassIDSpace, kKESCMUIDrawEventServiceBoss, kKESCMPrefix + 26)	// IK2ServiceProvider+IDrwEvtHandler: **UI 専用**の描画サービス(2026-08-13・model/UI 分割 第1段 Task 6)。押下中 HUD だけを持つ。★上の kKESCMDrawEventServiceBoss(比較マーク)と役割が違う＝あちらは印刷と PDF 書き出しに出なければならないので model 側、こちらは画面専用。kDrawEventService は複数プロバイダ登録が前提(本体だけで20以上)。第2段でこの Class ごと KCMUI へ移る


// InterfaceIDs:
DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMLAYOUTSYNCOBSERVER, kKESCMPrefix + 0)	// レイアウトビュー同期オブザーバのアタッチ識別ID(AttachObserver の observerIID)
DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMDOCSCLOSEDOBSERVER, kKESCMPrefix + 1)	// 一括クローズ完了(kPendingDocumentsClosedMsg)を受けるオブザーバのアタッチ識別ID
DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMPANELVISIBILITYOBSERVER, kKESCMPrefix + 2)	// パネルの表示状態変化(kPaletteVisibilityChangedMessage)を受けるオブザーバのアタッチ識別ID。半透明トグルをドッキング切り替え/開き直しに追随させるために使う
DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMSAVEDSECTIONHEIGHT, kKESCMPrefix + 3)	// IIntData として扱う: Story Edits セクションを閉じた瞬間の高さ(px)。次に開くときこの高さで開く。実装は SDK 標準の kPersistIntDataImpl(手本=linksui の IID_ISAVEDINFOPANESIZE)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 4)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 5)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 6)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 7)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 8)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKESCMINTERFACE, kKESCMPrefix + 9)
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
DECLARE_PMID(kImplementationIDSpace, kKESCMScriptProviderImpl, kKESCMPrefix + 0)	// CScriptProvider 実装(app.kcmStatus を返す。KESCMScriptProvider.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMDrawEventSrvcImpl, kKESCMPrefix + 1)
DECLARE_PMID(kImplementationIDSpace, kKESCMDrawEventHandlerImpl, kKESCMPrefix + 2)
// kKESCMPeekWatcherImpl (kKESCMPrefix + 3) は中ボタンウォッチャ撤去(2026-07-13)により廃止。スロットは予約のまま。
DECLARE_PMID(kImplementationIDSpace, kKESCMPeekStartupImpl, kKESCMPrefix + 4)	// IStartupShutdown 実装(peek ウォッチャを開始)
DECLARE_PMID(kImplementationIDSpace, kKESCMThumbIdleTaskImpl, kKESCMPrefix + 5)	// IIdleTask 実装(クローズ後の Pages サムネイル再生成を遅延実行)
DECLARE_PMID(kImplementationIDSpace, kKESCMPanelObserverImpl, kKESCMPrefix + 6)	// IObserver 実装(パネルのウィジェットオブザーバ)
DECLARE_PMID(kImplementationIDSpace, kKESCMActionComponentImpl, kKESCMPrefix + 7)	// IActionComponent 実装(About)
// kKESCMDocServiceProviderImpl (kKESCMPrefix + 8) は自前 ServiceProvider の撤去(2026-08-06)により廃止。
// 1シグナルだけの responder は API 提供の kAfterCloseDocSignalRespServiceImpl(DocumentID.h)を .fr で
// 名指しすれば登録される(KESCM.fr の kKESCMDocResponderServiceBoss 参照)。スロットは予約のまま。
DECLARE_PMID(kImplementationIDSpace, kKESCMDocResponderImpl, kKESCMPrefix + 9)	// IResponder 実装(クローズ確定時の追跡状態クリーンアップ)
DECLARE_PMID(kImplementationIDSpace, kKESCMIconTipImpl, kKESCMPrefix + 10)	// ITip 実装(パネルイラストにURLをツールチップ表示)
DECLARE_PMID(kImplementationIDSpace, kKESCMLayoutSyncObserverImpl, kKESCMPrefix + 11)	// IObserver 実装(レイアウトビュー同期)
DECLARE_PMID(kImplementationIDSpace, kKESCMScrollMapViewImpl, kKESCMPrefix + 12)	// IControlView 実装(スクロールバー地図stripの自前描画; KESCMScrollMap.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMToolImpl, kKESCMPrefix + 13)	// ITool 実装(KESCMTool.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMTrackerImpl, kKESCMPrefix + 14)	// ITracker 実装(CTracker派生; KESCMTracker.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMTrackerRegisterImpl, kKESCMPrefix + 15)	// ITrackerRegister 実装(KESCMTrackerRegister.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMTrackerEHImpl, kKESCMPrefix + 16)	// IEventHandler 実装(CTrackerEventHandler派生; 押下中のボタン解放を EndTracking へ転送。KESCMTracker.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMCursorProviderImpl, kKESCMPrefix + 17)	// ICursorProvider 実装(CToolCursorProvider派生; ツール選択中は常時✓カーソル。KESCMCursorProvider.cpp)
// (+6..+17 are all declared above - stale placeholders for them removed 2026-08-05 audit)
// (+18 = kKESCMSpriteImpl は 2026-08-06 に押下中 HUD ごと撤去。★**この番号は再利用しない**。
//  トラッカー boss の IID_ISPRITE は SDK 標準実装 kNoHandleSpriteImpl に戻してある=公式サンプル
//  wavetool の boss 構成(WavTl.fr:151,155)と同じ形)
DECLARE_PMID(kImplementationIDSpace, kKESCMDocsClosedObserverImpl, kKESCMPrefix + 19)	// IObserver 実装(一括クローズ完了で、保留した後片付けを1回だけ流す。KESCMPeek.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMPanelVisibilityObserverImpl, kKESCMPrefix + 20)	// IObserver 実装(パネルの表示状態が変わったら半透明を貼り直す。KESCMPanelAlpha.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMPanelRollOverImpl, kKESCMPrefix + 21)	// IMouseRollOver 実装(パネルにカーソルが乗っている間だけ半透明を解除。KESCMPanelAlpha.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMStorySectionToggleObserverImpl, kKESCMPrefix + 22)	// IObserver 実装(開閉ボタンの押下を受けて Story Edits セクションを開閉。KESCMStorySectionObserver.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMStoryTreeAdapterImpl, kKESCMPrefix + 23)	// ITreeViewHierarchyAdapter 実装(ListTreeViewAdapter派生。KESCMStoryTreeAdapter.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMStoryTreeWidgetMgrImpl, kKESCMPrefix + 24)	// ITreeViewWidgetMgr 実装(CTreeViewWidgetMgr派生。KESCMStoryTreeWidgetMgr.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMNoTipImpl, kKESCMPrefix + 25)	// ITip 実装(常に空を返す＝ツールチップを出さない。KESCMNoTip.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMPanelViewImpl, kKESCMPrefix + 26)	// IControlView 実装(PalettePanelView派生。ConstrainDimensions でパネルの最小サイズを守る。KESCMPanelView.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMStoryRowEHImpl, kKESCMPrefix + 27)	// IEventHandler 実装(TreeNodeEventHandler派生。Story Edits の行=単クリックでジャンプ・ダブルクリックでストーリー全文を選択。KESCMStoryRowEH.cpp)
// (kKESCMPrefix + 28 は一度 kKESCMStoryRowViewImpl=行の間の区切り線を消す IControlView に使い、同日
//  撤去した跡地。線を残すユーザー判断なので実装ごと消えている＝経緯は KESCM.fr の行 boss のコメント。
//  ActionID と違い Impl 番号は外部保存が参照しないので、下記のとおり再利用した。)
DECLARE_PMID(kImplementationIDSpace, kKESCMStoryTreeEHImpl, kKESCMPrefix + 28)	// IEventHandler 実装(TreeViewEventHandler派生)。★一覧**そのもの**のキー操作＝↑↓で行を移動し、着いた行へジャンプする(KESCMStoryTreeEH.cpp)。行側の kKESCMStoryRowEHImpl とは別物＝あちらはクリック
DECLARE_PMID(kImplementationIDSpace, kKESCMBookDialogControllerImpl, kKESCMPrefix + 29)	// IDialogController 実装(CDialogController派生)。ブック比較のモードレスダイアログ＝開いたとき対象の2ブック名を埋める(KESCMBookDialog.cpp)
// (退役 2026-08-12)kKESCMBookDialogObserverImpl(kKESCMPrefix + 30)＝ブック比較ダイアログの Compare ボタンの押下を受けていた IObserver。
//   ★ボタンごと撤去した(確認アラート→OK で比較する流れへ変更)ので実装ファイルごと削除し、IID_IOBSERVER は kDialogBoss の stock(kCDialogObserverImpl)へ戻した。スロットは予約のまま再利用しない。
DECLARE_PMID(kImplementationIDSpace, kKESCMBookTreeAdapterImpl, kKESCMPrefix + 31)	// ITreeViewHierarchyAdapter 実装(ListTreeViewAdapter派生。KESCMBookTreeAdapter.cpp)。ブック比較ダイアログの章一覧＝行数を答えるだけ
DECLARE_PMID(kImplementationIDSpace, kKESCMBookTreeWidgetMgrImpl, kKESCMPrefix + 32)	// ITreeViewWidgetMgr 実装(CTreeViewWidgetMgr派生。KESCMBookTreeWidgetMgr.cpp)。章一覧の行の生成と流し込み(章名 / 状態の2列)
DECLARE_PMID(kImplementationIDSpace, kKESCMBookRowEHImpl, kKESCMPrefix + 33)	// IEventHandler 実装(TreeNodeEventHandler派生。KESCMBookRowEH.cpp)。ブック比較の章行＝**ダブルクリックでその章を開く**・**右クリックで行メニュー**(Start Change Marker)。★単クリックは何もしない(Story Edits の行と違う＝あちらは開いている文書の中を移動するだけだが、こちらは文書を開いてしまうため)。実際の動作は KESCMBookOpen.cpp
DECLARE_PMID(kImplementationIDSpace, kKESCMUIDrawEventSrvcImpl, kKESCMPrefix + 35)	// CServiceProvider 実装(kDrawEventService。UI 専用の描画サービス。KESCMUIDrawEvent.cpp)。★GetThreadingPolicy は手書きしない＝CServiceProvider がプラグインの型から既定を返す
DECLARE_PMID(kImplementationIDSpace, kKESCMUIDrawEventHandlerImpl, kKESCMPrefix + 36)	// IDrwEvtHandler 実装(押下中 HUD の描画だけ。画面専用＝PDF 書き出しに出なくてよい。KESCMUIDrawEvent.cpp)
DECLARE_PMID(kImplementationIDSpace, kKESCMSplitterEHImpl, kKESCMPrefix + 34)	// IEventHandler 実装(CEventHandler派生＝全メソッドが kFalse を返すだけの基底をそのまま使う)。パネルの分割バーが押下を受け取らなくなる＝ドラッグで動かせない(KESCMSplitterEH.cpp)


// ActionIDs:
DECLARE_PMID(kActionIDSpace, kKESCMAboutActionID, kKESCMPrefix + 0)
DECLARE_PMID(kActionIDSpace, kKESCMPanelWidgetActionID, kKESCMPrefix + 1)	// パネルの表示/非表示(ウィンドウメニュー)
DECLARE_PMID(kActionIDSpace, kKESCMPopupAboutThisActionID, kKESCMPrefix + 2)	// パネルのフライアウトの「このプラグインについて」
DECLARE_PMID(kActionIDSpace, kKESCMPopupAboutScriptActionID, kKESCMPrefix + 3)	// (撤去・予約)旧「About Scripting」フライアウト項目。2026-07-25 削除(ユーザー指定)。ID スロットは予約のまま
DECLARE_PMID(kActionIDSpace, kKESCMPopupUsageActionID, kKESCMPrefix + 4)	// パネルのフライアウトの「使い方」
// kActionIDSpace +5 は現在空き(旧 kKESCMPopupTestSplitActionID; Split Test 検証メニューは撤去済み)
// kActionIDSpace +6 は現在空き(旧 kKESCMPopupSplitTargetActionID; Split Target on Start は 2026-07-04 撤去。
//   仕組みは docs/ai-notes/kescm-split-target-mechanism.md に保存)
DECLARE_PMID(kActionIDSpace, kKESCMPopupHideUnchangedActionID, kKESCMPrefix + 7)	// パネルのフライアウトの「Hide Unchanged Spreads」チェック式トグル(ON=変更なしスプレッドを隠す)
DECLARE_PMID(kActionIDSpace, kKESCMPopupShowOldNumsActionID, kKESCMPrefix + 8)	// パネルのフライアウトの「Show Original Page Numbers」チェック式トグル(枠表示中/印刷ON時に隠す前の元番号バッジ)
DECLARE_PMID(kActionIDSpace, kKESCMPopupSyncViewsActionID, kKESCMPrefix + 9)	// パネルのフライアウトの「Sync Layout Views」チェック式トグル(他文書のビューへ座標+拡大率を自動同期)
DECLARE_PMID(kActionIDSpace, kKESCMPopupShowSrcMarksActionID, kKESCMPrefix + 10)	// パネルのフライアウトの「Show Marks on Source」チェック式トグル(Source側にも枠を常時表示。OPPでも表示・印刷にも出す。Startで既定ON)
DECLARE_PMID(kActionIDSpace, kKESCMPageMapToggleActionID, kKESCMPrefix + 11)	// ページパネルのページ右クリック(RtMenuPagesPanel)のトグル「KCM: Register as Added/Removed Pages」(選択ページを「比較相手なし」として登録/解除。チェック/動的ラベルは kCustomEnabling。KESCMPageMap.cpp)
DECLARE_PMID(kActionIDSpace, kKESCMPopupIgnorePageNumActionID, kKESCMPrefix + 12)	// パネルのフライアウトの「Ignore Page Number Marker」チェック式トグル(ON=ノンブル(自動ページ番号)マーカーを含むフレームを比較から除外。★既定OFF=sIgnorePageNumberMarker の初期値。KESCMPageNumberMarker.cpp)
DECLARE_PMID(kActionIDSpace, kKESCMPopupStartStopActionID, kKESCMPrefix + 13)	// パネルのフライアウト先頭の「Start / Stop」(比較の開始/解除。旧トグルボタンをメニュー化。arm 状態で名前が Start↔Stop に動的変化=kCustomEnabling+SetNthActionName。KESCMPanelObserver.cpp の KESCMToggleStartStop)
DECLARE_PMID(kActionIDSpace, kKESCMPopupPrintMarksActionID, kKESCMPrefix + 14)	// パネルのフライアウトの「Print comparison marks」チェック式トグル(旧パネルのチェックボックスをメニュー化。ON=マークを印刷し画面にも常時表示。KESCMPanelObserver.cpp の KESCMTogglePrintMarks)
DECLARE_PMID(kActionIDSpace, kKESCMPopupOpacity25ActionID, kKESCMPrefix + 15)	// パネルのフライアウトの「Marks opacity 25%」(旧パネルの opacity ラジオをメニュー化。75% と相互排他=選択中の方に✓。KESCMPanelObserver.cpp の KESCMSetMarkOpacity25)
DECLARE_PMID(kActionIDSpace, kKESCMPopupOpacity75ActionID, kKESCMPrefix + 16)	// パネルのフライアウトの「Marks opacity 75%」(25% と相互排他)
DECLARE_PMID(kActionIDSpace, kKESCMPopupSep1ActionID, kKESCMPrefix + 17)	// フライアウト: Start の下の区切り線(MenuDef のパス末尾 ":-"。ActionDef 不要・DoAction 不要=一意なIDだけ要る)
DECLARE_PMID(kActionIDSpace, kKESCMPopupSep2ActionID, kKESCMPrefix + 18)	// フライアウト: How to Use の上の区切り線
DECLARE_PMID(kActionIDSpace, kKESCMPopupHoldToHideMarksActionID, kKESCMPrefix + 19)	// パネルのフライアウトの「Hold to Hide Marks」チェック式トグル(ON=枠を画面に常時表示し、ツール左hold中だけ隠す=極性反転。画面のみ・印刷は Print comparison marks が別管理。KESCMActionComponent.cpp)
// kKESCMPopupPanelShortcutActionID (kKESCMPrefix + 20) は中ボタン撤去(2026-07-13)に伴い廃止。スロットを 2026-07-24 に再利用:
DECLARE_PMID(kActionIDSpace, kKESCMPopupAlignViewsActionID, kKESCMPrefix + 20)	// パネルのフライアウトの「Align Other Views to Active」(実行アクション)。アクティブ(最前面)文書のビューの位置+拡大率を他文書のビューへ1回そろえる。Start中はページのAdd/Remove補正あり。ショートカット割当可(kKESCMPanelMenuActionArea+VisibleInKBSC)。実体 KESCMPeek.cpp の KESCMAlignOtherViewsToActiveNow
DECLARE_PMID(kActionIDSpace, kKESCMPopupScrollMapActionID, kKESCMPrefix + 21)	// パネルのフライアウトの「Show Scrollbar Map」チェック式トグル(ON=文書窓の縦スクロールバー脇に変更位置地図stripを表示。既定ON。実体 KESCMScrollMap.cpp の sScrollMapOn)
DECLARE_PMID(kActionIDSpace, kKESCMPopupSavePanelStateActionID, kKESCMPrefix + 22)	// パネルのフライアウトの「Save Panel Settings」(チェックではなく実行アクション)。現在の設定系トグルを独自JSONでローカルへ保存し保存先パスを表示。読込は起動時(KESCMPeekStartup::Startup。2026-07-15 前倒し)。実体 KESCMPanelState.cpp
DECLARE_PMID(kActionIDSpace, kKESCMPopupSep3ActionID, kKESCMPrefix + 23)	// フライアウト: Refresh Overset の下(9.50)の区切り線(MenuDef のパス末尾 ":-"。ActionDef 不要)。現配置は下の位置一覧(9.50)が正(2026-07-25 コメント現行化)
DECLARE_PMID(kActionIDSpace, kKESCMPageCheckToggleActionID, kKESCMPrefix + 24)	// ページパネルのページ右クリック(RtMenuPagesPanel)のトグル「KCM: Check」(選択ページに✓印を付け外し。Start中限定・Stopで消去。チェック/有効無効は kCustomEnabling。実体 KESCMPageCheck.cpp、✓描画は KESCMDrawEventHandler の isThumb 分岐)
DECLARE_PMID(kActionIDSpace, kKESCMPopupSaveChecksActionID, kKESCMPrefix + 25)	// パネルのフライアウトの「Save Check & Register」(実行アクション)。Start中の Target/Source の現在の Check(✓)+ Register(Added/Removed)を独自JSON(KESCM\KESCMPageChecks.json, v2)へマージ保存し保存先パスを表示。実体 KESCMPageCheck.cpp
DECLARE_PMID(kActionIDSpace, kKESCMPopupLoadChecksActionID, kKESCMPrefix + 26)	// パネルのフライアウトの「Load Check & Register」(実行アクション)。Start中だけ有効。上記JSONから Register を両文書へ適用→再比較→Check(今もマーク付きのみ)を復元。実体 KESCMPageCheck.cpp
// kKESCMPopupPagesPanelShortcutActionID (kKESCMPrefix + 27) は中ボタン撤去(2026-07-13)に伴い「Invoke Pages Panel Shortcut」トグルごと廃止。スロットは予約のまま。
DECLARE_PMID(kActionIDSpace, kKESCMPageMapSepActionID, kKESCMPrefix + 28)	// ページパネルのページ右クリック(RtMenuPagesPanel): KESCM 追加項目(Register / Check)の上の区切り線(MenuDef のパス末尾 ":-"。ActionDef 不要・DoAction 不要=一意なIDだけ要る)。本家メニューと視覚的に分けるため
DECLARE_PMID(kActionIDSpace, kKESCMToolActionID, kKESCMPrefix + 29)	// ツールボックスのツール選択ショートカット用の ActionID(ToolDef が参照。ActionDef 不要=ツール枠が自動生成)
DECLARE_PMID(kActionIDSpace, kKESCMPageRefreshCompareActionID, kKESCMPrefix + 30)	// ページパネルのページ右クリック(RtMenuPagesPanel)の実行アクション「KCM: Refresh Page Comparison」(選択ページの比較を再検出して枠/サムネイルを更新。旧 Ctrl+ミドルのスプレッド再比較を移設。Start中限定・kCustomEnabling。実体 KESCMPeek.cpp の KESCMRefreshComparisonForSelectedPages)
DECLARE_PMID(kActionIDSpace, kKESCMPopupFindOversetActionID, kKESCMPrefix + 31)	// パネルのフライアウトの「Find Overset」チェック式トグル(ON=アクティブ文書を走査し overset のあるページに大きな十字を表示。比較と独立・単独点検。kCustomEnabling。実体 KESCMActionComponent.cpp/KESCMOversetScan.cpp)
DECLARE_PMID(kActionIDSpace, kKESCMPopupRefreshOversetActionID, kKESCMPrefix + 32)	// パネルのフライアウトの「Refresh Overset」(実行アクション)。Find Overset が ON のときだけ有効(OFF時は灰色)=アクティブ文書を再走査して十字を貼り直す。kCustomEnabling
DECLARE_PMID(kActionIDSpace, kKESCMPopupOversetSepActionID, kKESCMPrefix + 33)	// フライアウト: Find Overset 群の上の区切り線(MenuDef のパス末尾 ":-"。ActionDef 不要・DoAction 不要=一意なIDだけ要る)
DECLARE_PMID(kActionIDSpace, kKESCMPopupExportChangedPagesActionID, kKESCMPrefix + 34)	// パネルのフライアウトの「Export Changed Pages...」(実行アクション)。比較中(sDB≠nil)のみ有効=現在の比較の変更ページ一覧をTSV(新/旧/種別)で保存。実体 KESCMChangedPagesTSV.cpp
// (+35 = kKESCMPopupHudActionID「Show HUD」は 2026-08-06 に機能ごと撤去。★**この番号は再利用しない**
//  ＝ショートカット割当は .indk に ActionID の数値で保存されるので、別機能に振り直すと古い割当が
//  その機能を叩いてしまう)
DECLARE_PMID(kActionIDSpace, kKESCMPopupTranslucentPanelActionID, kKESCMPrefix + 36)	// パネルのフライアウトの「Translucent Panel」チェック式トグル(ON=フローティング中のこのパネルを半透明にする。★Windows 専用・★ドッキング中は選べるが効かない(フラグだけ立ちフローティングに戻すと効く)。既定 OFF。実体 KESCMPanelAlpha.cpp)
DECLARE_PMID(kActionIDSpace, kKESCMPopupTranslucentPagesActionID, kKESCMPrefix + 37)	// パネルのフライアウトの「Translucent Pages Panel」チェック式トグル(ON=フローティング中の**本体のページパネル**を半透明にする。上の +36 と同じ仕組みで対象だけが違う=WidgetID(kPagesPanelWidgetID)で狙い撃ちする。★Windows 専用・★ドッキング中は選べるが効かない。既定 OFF。実体 KESCMPanelAlpha.cpp)
// ★kKESCMPrefix + 38 は「Translucent Toolbox」トグルの跡地(2026-08-07 に追加し、同日ユーザー判断で撤去)。
//   **番号は再利用しない** = ショートカット設定(.indk)はアクションを数値の ActionID で保存するので、
//   割り当て済みの番号を別機能へ回すと、そのショートカットが無関係な機能を叩く。押下中 HUD を撤去した
//   ときの +35 とまったく同じ扱い。
DECLARE_PMID(kActionIDSpace, kKESCMPopupCompareBooksActionID, kKESCMPrefix + 39)	// パネルのフライアウトの「Compare Books」(実行アクション)。★ブックパネルで前面タブのブック=Target / それ以外で最初に開いているブック=Source として、章(ドキュメント)単位で「変更あり/なし」を判定する。★既存の文書比較(Start)とは完全に独立=arm しない・枠を作らない・KESCMDrawEventHandler の static を触らない。kCustomEnabling(2ブックそろい、かつ前面タブが特定できるときだけ有効)。実体 KESCMBookCompare.cpp / 対象の解決 KESCMBookPair.cpp
// ブック比較ダイアログの章行の右クリックメニュー(2026-08-12)。★パネルのフライアウトではなく
// **行の上に出るポップアップ**なので、置き場所は MenuDef のサブツリー kKESCMBookRowMenuName。
// 押されたときに「どの行か」を知る手段はこのアクション自身には無い(ActionID しか渡らない)ので、
// 行は右クリックの時点で KESCMBookSetMenuRow が控える＝KBS の結果行と同じ作り。
DECLARE_PMID(kActionIDSpace, kKESCMBookRowStartActionID, kKESCMPrefix + 40)	// 章行の右クリック「Start Change Marker」= その章の Target/Source 2文書を窓付きで開き、比較中なら一度 Stop してから比較を開始する(KESCMBookOpen.cpp)。★両側のファイルが揃っていない行(ChapterAdded/ChapterDeleted・ファイル無し)では灰色(kCustomEnabling → KESCMBookRowCanStart)
DECLARE_PMID(kActionIDSpace, kKESCMPopupTranslucentBookDialogActionID, kKESCMPrefix + 41)	// ★パネルのフライアウトの「Translucent Book Dialog」チェック式トグル(2026-08-13 ユーザー要望「ダイアログも半透明に出来る様に」)。上の +36/+37 と**同じ実体**(KESCMPanelAlpha.cpp)で対象だけが違う。⚠ただし対象がパネルではないので**窓の出所だけが別**＝ダイアログ自身が KESCMSetBookDialogWindow で教える(パネルマネージャは WidgetID で引けるが、ダイアログはそこに載っていない)。★パネル側と違い**ドッキングの概念が無いので「押しても効かない状態」が無い**。既定 OFF
// (+15..+23 are all declared above - stale placeholders for them removed 2026-08-05 audit. Next free: +42)
//DECLARE_PMID(kActionIDSpace, kKESCMActionID, kKESCMPrefix + 41)
// kKESCMPrefix + 24/25/26/28 は使用中(KCM: Check / Save Check & Register / Load Check & Register / RtMenuPagesPanel の区切り線)。+27 は廃止・予約(上記)


// WidgetIDs:
DECLARE_PMID(kWidgetIDSpace, kKESCMPanelWidgetID, kKESCMPrefix + 0)
DECLARE_PMID(kWidgetIDSpace, kKESCMTargetTextWidgetID, kKESCMPrefix + 1)
DECLARE_PMID(kWidgetIDSpace, kKESCMSourceTextWidgetID, kKESCMPrefix + 26)
// kWidgetIDSpace +27 は現在空き(旧 kKESCMStartButtonWidgetID; 開始/解除を kKESCMToggleButtonWidgetID に統合)
// kWidgetIDSpace +28 は現在空き(旧 kKESCMClearButtonWidgetID; 同上)
// kWidgetIDSpace +29 は現在未使用(旧 kKESCMPrintCheckWidgetID; 印刷ON/OFF チェックボックスは 2026-07-10 に
//   フライアウト「Print comparison marks」メニュー項目=kKESCMPopupPrintMarksActionID へ移行しパネルから撤去)
// kWidgetIDSpace +30〜+32 は現在未使用(旧 kKESCMOpacityClusterWidgetID / kKESCMOpacity25RadioWidgetID /
//   kKESCMOpacity75RadioWidgetID; 不透明度 25%/75% ラジオは 2026-07-10 にフライアウト
//   kKESCMPopupOpacity25ActionID / kKESCMPopupOpacity75ActionID へ移行しパネルから撤去)
// kWidgetIDSpace +33 は現在空き(旧 kKESCMHintTextWidgetID; 説明文はパネルから撤去しフライアウト「使い方」へ移動)
DECLARE_PMID(kWidgetIDSpace, kKESCMIconOnWidgetID, kKESCMPrefix + 34)
DECLARE_PMID(kWidgetIDSpace, kKESCMIconOffWidgetID, kKESCMPrefix + 35)
DECLARE_PMID(kWidgetIDSpace, kKESCMStatusTextWidgetID, kKESCMPrefix + 36)
// kWidgetIDSpace +37 は 2026-07-15 に kKESCMNavPosTextWidgetID として再利用(旧 kKESCMToggleButtonWidgetID;
//   開始/解除ボタンは 2026-07-10 にフライアウト「Start / Stop」項目=kKESCMPopupStartStopActionID へ移行済み)
DECLARE_PMID(kWidgetIDSpace, kKESCMNavPosTextWidgetID, kKESCMPrefix + 37)	// Prev/Next の間に出す現在位置「3/12」(中央揃え StaticText。KESCMChangeNav.cpp が KESCMSetNavPosition で更新)
DECLARE_PMID(kWidgetIDSpace, kKESCMPrevChangeButtonWidgetID, kKESCMPrefix + 38)	// 「◀ Prev」= 前の見るべきページへスクロール(KESCMChangeNav.cpp)
DECLARE_PMID(kWidgetIDSpace, kKESCMNextChangeButtonWidgetID, kKESCMPrefix + 39)	// 「Next ▶」= 次の見るべきページへスクロール(KESCMChangeNav.cpp)
DECLARE_PMID(kWidgetIDSpace, kKESCMScrollMapWidgetID, kKESCMPrefix + 40)	// スクロールバー地図strip(文書窓の縦スクロールバー左隣に実行時注入; KESCMScrollMap.cpp)
DECLARE_PMID(kWidgetIDSpace, kKESCMToolWidgetID, kKESCMPrefix + 41)	// ツールボックスのツールボタンのウィジェットID(KESCMTool::InitWidget)
DECLARE_PMID(kWidgetIDSpace, kKESCMToolButtonWidgetID, kKESCMPrefix + 42)	// ★パネル内のツール切替ボタン(2026-08-07 追加。Prev の左・32x22)。押すと kKESCMToolBoss をアクティブツールにする(KESCMActivateOwnTool)。上の +41 とは別物＝あちらはツールボックス側のツール枠
// ★「Story Edits」セクション(2026-08-09 追加)。パネルを SplitterPanelWidget で上下に割り、下ペインに
//   「テキストが編集されたストーリー」の一覧を出す(段階3)。手本は製品 linksui の「リンク情報」セクション。
DECLARE_PMID(kWidgetIDSpace, kKESCMSplitterWidgetID, kKESCMPrefix + 43)			// パネルを上下に割る SplitterPanelWidget(Widgets.fh:462 / kSplitterPanelWidgetBoss)
DECLARE_PMID(kWidgetIDSpace, kKESCMTopPaneWidgetID, kKESCMPrefix + 44)			// 上ペイン=従来のパネル内容一式を丸ごと収めた GenericPanelWidget。★splitter の「伸縮させない方」に指定する
DECLARE_PMID(kWidgetIDSpace, kKESCMStorySectionWidgetID, kKESCMPrefix + 45)		// 下ペイン=Story Edits 本体(初期は非表示。中身＝列見出しの帯＋罫線＋一覧ツリー)
DECLARE_PMID(kWidgetIDSpace, kKESCMStorySectionToggleWidgetID, kKESCMPrefix + 46)	// 開閉ボタン(三角)。★上ペインの中に置く=下ペインに置くと閉じたときボタンごと消えて開けなくなる
DECLARE_PMID(kWidgetIDSpace, kKESCMStoryTreeWidgetID, kKESCMPrefix + 47)		// Story Edits の一覧ツリー本体(下ペインいっぱい)
DECLARE_PMID(kWidgetIDSpace, kKESCMStoryRowTextWidgetID, kKESCMPrefix + 48)	// 行の左=本文の先頭テキスト
DECLARE_PMID(kWidgetIDSpace, kKESCMStoryRowKindWidgetID, kKESCMPrefix + 49)	// 行の右=変わった種類(Text / Attr / Other / Added)
DECLARE_PMID(kWidgetIDSpace, kKESCMStorySectionLabelWidgetID, kKESCMPrefix + 50)	// 上ペインの三角の隣=「Story Edits (3)」。件数は C++ が実行時に付ける
DECLARE_PMID(kWidgetIDSpace, kKESCMStoryRowWidgetID, kKESCMPrefix + 51)		// 行テンプレート自身。★GetWidgetTypeForNode が返すのはこれ
DECLARE_PMID(kWidgetIDSpace, kKESCMStoryRowUIDWidgetID, kKESCMPrefix + 52)	// ★行の左端=ストーリーの UID(10進。2026-08-10 ユーザー要望「UID・テキスト・変更部分」)。行の同一性を目で追える識別子＝本文が同じ文言でも別のストーリーだと分かる
// ★一覧の列見出し(2026-08-10 ユーザー要望「一番上の列に UID / Text / 変更のようなのを付けて欲しい」)。
//   ツリーの中ではなく**下ペインの中でツリーの上**に置く固定の帯＝行をスクロールしても動かない。
//   ★3つとも行のセルと**同じ x 座標・同じ binding**を与えてある(KESCM.fr)。それが列が揃い続ける唯一の
//   保証で、片方だけ動かすと可変幅パネルでずれる。
DECLARE_PMID(kWidgetIDSpace, kKESCMStoryHeaderUIDWidgetID, kKESCMPrefix + 53)	// 見出しの左「UID」(行の kKESCMStoryRowUIDWidgetID と同じ 8〜48・kBindLeft)
DECLARE_PMID(kWidgetIDSpace, kKESCMStoryHeaderTextWidgetID, kKESCMPrefix + 54)	// 見出しの中「Story」(行の kKESCMStoryRowTextWidgetID と同じ 52〜154・kBindLeft|kBindRight＝広げるとここだけ伸びる)
DECLARE_PMID(kWidgetIDSpace, kKESCMStoryHeaderKindWidgetID, kKESCMPrefix + 55)	// 見出しの右「Change」(行の kKESCMStoryRowKindWidgetID と同じ 154〜216・kBindRight・右寄せ)
DECLARE_PMID(kWidgetIDSpace, kKESCMStoryHeaderRuleWidgetID, kKESCMPrefix + 56)	// 見出しと一覧を分ける 1px の罫線＝**ErasablePrimaryResourcePanelWidget を高さ1pxで置き kInterfaceSeparatorColor で erase**(その塗りが線)。⚠stock の RuleWidget(Widgets.fh:887 / kRuleWidgetBoss)を先に試したが**パースもビルドも通って何も描かなかった**ので差し替えた(.fr 側に全文)

// ブック比較ダイアログ(2026-08-11)。★OK/Cancel は stock の WidgetID(kOKButtonWidgetID /
// kCancelButton_WidgetID)を使うので、ここに要るのはダイアログ本体だけ。
DECLARE_PMID(kWidgetIDSpace, kKESCMBookDialogWidgetID, kKESCMPrefix + 57)
DECLARE_PMID(kWidgetIDSpace, kKESCMBookTargetTextWidgetID, kKESCMPrefix + 58)	// 「Target: new.indb」(前面タブのブック)
DECLARE_PMID(kWidgetIDSpace, kKESCMBookSourceTextWidgetID, kKESCMPrefix + 59)	// 「Source: old.indb」(それ以外で最初に開いているブック)
DECLARE_PMID(kWidgetIDSpace, kKESCMBookCompareButtonWidgetID, kKESCMPrefix + 60)	// 「Compare」ボタン。★押す前に上の2行が目に入るのが要点
DECLARE_PMID(kWidgetIDSpace, kKESCMBookStatusTextWidgetID, kKESCMPrefix + 61)	// ステータス行(比較の要約。章数を必ず含む)
DECLARE_PMID(kWidgetIDSpace, kKESCMBookTreeWidgetID, kKESCMPrefix + 62)		// 章一覧のツリー本体(ダイアログの中で一番大きい部品)
// ★★**番号が +2 なのは書き間違いではない**(2026-08-13)。ダイアログのメッセージを2行にする(2行目＝行の
//   右クリックの案内)ために widget を1つ足したが、**+63 から先は KESCL の prefix 領域**(0x205554)なので
//   後ろへは伸ばせない。一方 **+2〜+25 は宣言ごと空いていた**(下のプレースホルダ群。+1 の次は +26)＝
//   予約済み 256 枠の内側の未使用番号なので、**ID 空間の消費はゼロで衝突も無い**。
//   ⇒ 逃げ道は「借用(同じ親の子孫でだけ一意)」だけではなく、**前方の穴**もある＝[[id-prefix-256-slot-budget]]
DECLARE_PMID(kWidgetIDSpace, kKESCMBookHintTextWidgetID, kKESCMPrefix + 2)	// ステータスの2行目=「Right-click a changed chapter to start Change Marker.」(固定文。.fr が初期テキストとして持ち、C++ は触らない)
// ★★★下の3つは「Story Edits」の行と **WidgetID を共有する**(2026-08-12。新しい番号を1つも使わない)。
//   根拠 = **widget ID がアプリ全体で一意である必要は無い**。公式ガイド vol2-12:71 が、
//   グローバルに一意でなければならない文字列キーと**対比して**こう書いている:
//     "widget identifiers need be unique only within the list of descendants of a given widget,
//      so ... you can reuse a widget identifier (for example, across different dialog boxes or
//      panels you own)"
//   機構の裏づけ = IPanelControlData::FindWidget(WidgetID, searchLevels) は**自分の部分木を再帰探索
//   するだけ**で、グローバルな「widget ID → widget」レジストリではない(IPanelControlData.h:89)。
//   実例 = stock の kOKButtonWidgetID / kCancelButton_WidgetID を source/sdksamples の **39 ファイル**が
//   共有している(上の +57 のブロックで既にそれを使っている)。
//   ここでは「パネルの Story Edits 一覧」と「このダイアログの章一覧」が**別の部分木**に属するので衝突しない。
//   ⇒ これで **+63〜+65 の KESCL 領域(0x205554/55/56)への食い込みが消えた**(下の採番メモ参照)。
//   ⚠ 共有してよいのは**子 widget だけ**。パネル/ダイアログの**ルート**の WidgetID は
//     IPanelMgr::GetPanelFromWidgetID がアプリ全体から引くので一意でなければならない。
//   ⚠ 対応関係は役割まで一致させてある(行テンプレ↔行テンプレ / 左セル↔左セル / 右セル↔右セル)。
//     ずらすと読む側が混乱するだけで、得は何も無い。
DECLARE_PMID(kWidgetIDSpace, kKESCMBookRowWidgetID, kKESCMPrefix + 51)		// ＝kKESCMStoryRowWidgetID と同値。行テンプレート自身。★GetWidgetTypeForNode が返すのはこれ
DECLARE_PMID(kWidgetIDSpace, kKESCMBookRowNameWidgetID, kKESCMPrefix + 48)	// ＝kKESCMStoryRowTextWidgetID と同値。行の左=章のファイル名(Failed のときだけ「 - 理由」が後ろに付く)
DECLARE_PMID(kWidgetIDSpace, kKESCMBookRowStateWidgetID, kKESCMPrefix + 49)	// ＝kKESCMStoryRowKindWidgetID と同値。行の右=判定(Changed / NoChange / ChapterAdded / ChapterDeleted / Failed)。固定幅・右寄せ
//====================================================================================
// ★★採番の上限に注意 — この prefix で安全に使えるのは「+0 〜 +62」まで
//
//  prefix は 32bit ID の上位ビットで、本来は 1 プラグインにつき各 ID 空間 256 個(+0〜+255)
//  ぶんの枠が与えられる。ところが KESCM(0x205515) と KESCL(0x205554) の間隔は 0x3F = 63 しかない。
//  ∴ **+63 以降は KESCL の領域**で、そこへ両者が届いた瞬間に「2つのプラグインが同じ ID を主張」する
//     状態になり、起動時のオブジェクトモデル構築("Completing object model" 段)で **弾かれる**
//     (＝静かな誤動作ではなく、読み込まれない)。
//
//  ★2026-08-12: **食い込みは解消済み**。ブック比較ダイアログの行 widget 3つが +63/+64/+65 を
//    使っていたが、下記 (a) の方法で Story Edits の行と ID を共有する形に変えた(上のブロック参照)。
//    現在の最大は **+62**(kKESCMBookTreeWidgetID)。
//
//  widget を足すときは、この順に検討する:
//
//    (a) ★★★**そもそも新しい番号を採らない** — **widget ID がアプリ全体で一意である必要は無い**。
//        「**同じ親の子孫の中でだけ**一意」ならよく、**別のダイアログ/パネルでは再利用してよい**
//        (公式ガイド vol2-12:71 が、グローバル一意が必須の文字列キーと**対比して**明言)。
//        機構 = IPanelControlData::FindWidget は**自分の部分木を再帰探索するだけ**(:89)。
//        実例 = stock の kOKButtonWidgetID を source/sdksamples の **39 ファイル**が共有。
//        ⇒ **ID 空間を1つも消費しない**ので、まずこれを検討する。
//        ⚠ ただし**ルート**の WidgetID は IPanelMgr::GetPanelFromWidgetID がアプリ全体から引くので
//          一意でなければならない(パネル本体・ダイアログ本体は再利用不可)。
//        ⚠ widget は状態を saved data に永続するので、**初めて再利用する形を入れたら実機で確認する**。
//
//    (b) **+27 〜 +33 の穴(7個)** — 現在どこからも使われていない。
//        ⚠ 過去に使って消した番号なので、再利用の前に git 履歴を見ること
//        (widget ID はワークスペース＝パネル配置の永続データに現れうる)
//
//    (c) それでも足りなくなったら **セカンダリ prefix を1本用意する**(既存 ID は1つも動かさない。
//        公開済みバージョンとの互換を保ったまま 256 枠を新規に確保できる)。正攻法は
//        wwds@adobe.com に prefix ID の発行を依頼すること。
//
//  調査の全記録 = docs/ai-notes/guide-gs-04-object-model-read-2026-08-12.md §1
//                 docs/ai-notes/guide-vol2-12-ui-fundamentals-read-2026-08-12.md §0 ((a) の根拠)
//====================================================================================
// (+2 is IN USE since 2026-08-13 - kKESCMBookHintTextWidgetID, declared with the dialog's widgets above.)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 3)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 4)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 5)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 6)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 7)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 8)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 9)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 10)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 11)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 12)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 13)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 14)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 15)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 16)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 17)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 18)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 19)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 20)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 21)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 22)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 23)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 24)
//DECLARE_PMID(kWidgetIDSpace, kKESCMWidgetID, kKESCMPrefix + 25)

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

// "About Plug-ins" sub-menu:
#define kKESCMAboutMenuKey			kKESCMStringPrefix "kKESCMAboutMenuKey"
#define kKESCMAboutMenuPath		kSDKDefStandardAboutMenuPath kKESCMCompanyKey

// (旧 "Plug-ins" sub-menu 用の kKESCMPluginsMenuKey/Path は未使用のため撤去。パネルのメニュー配置は
//  下の kKESCMPanelPluginsMenuPath が担う)

// パネルを Plug-Ins メニューへ出すためのパスと位置。
// Plug-Ins ▸ KohakuNekotarou ▸ Kohaku Change Marker（リーフはパネル名キー）。
#define kKESCMPanelPluginsMenuPath		kSDKDefPlugInsStandardMenuPath kKESCMCompanyKey kSDKDefDelimitMenuPath kKESCMPanelTitleKey
#define kKESCMPanelPluginsMenuPosition	100.0	// 大きいほど下に並ぶ。

// Menu item keys:

// ★プラットフォーム別の修飾キー表記(2026-07-25 追補 Mac 対応)。
//   実装側は SDK の IEvent が差を吸収する(OptionAltKeyDown = Win の Alt / Mac の Option、
//   CmdKeyDown = Win の Ctrl / Mac の Command)ので、切り替えるのは「ユーザーに見せる名前」だけ。
//   この定数は文字列リテラルなので、.fr の StringTable でも C++ でも隣接連結でそのまま埋め込める
//   (例: "Hold Left + " kKESCMAltKeyName "=")。MACINTOSH は Mac ビルドの xcconfig
//   (GCC_PREPROCESSOR_DEFINITIONS)と odfrc の双方で定義される(SDK サンプル snapshot/linksui に
//   .fr 内 #ifdef MACINTOSH の実例あり)。
//   ※現在この表記が要るのは How to Use 本文(kKESCMHintKey)だけ。KESCM のジェスチャは Ctrl/Command を
//     使わないので Cmd 側の名前定数は用意していない(必要になったら同じ形で足す)。
#ifdef MACINTOSH
#define kKESCMAltKeyName	"Option"
#else
#define kKESCMAltKeyName	"Alt"
#endif

// Other StringKeys:
#define kKESCMAboutBoxStringKey	kKESCMStringPrefix "kKESCMAboutBoxStringKey"
#define kKESCMRepoURL			"https://github.com/KohakuNekotarou/KohakuChangeMarker"// 配布元URL。「このプラグインについて」本文とパネルのイラストクリックの飛び先で共通
// (kKESCMAboutScriptMenuKey / kKESCMScriptHelpStringKey は「About Scripting」撤去(2026-07-25)に伴い削除)
#define kKESCMUsageMenuKey		kKESCMStringPrefix "kKESCMUsageMenuKey"	// パネルのフライアウト「使い方」のメニュー名(本文は kKESCMHintKey を再利用)
#define kKESCMHideUnchangedMenuKey	kKESCMStringPrefix "kKESCMHideUnchangedMenuKey"	// パネルのフライアウト「Hide Unchanged Spreads」トグルのメニュー名
#define kKESCMHideConfirmKey		kKESCMStringPrefix "kKESCMHideConfirmKey"	// その確認ダイアログ本文(enUS=英語。日本語UIは KESCMLoc.h の実行時切替 2026-08-05)
#define kKESCMShowOldNumsMenuKey	kKESCMStringPrefix "kKESCMShowOldNumsMenuKey"	// パネルのフライアウト「Show Original Page Numbers」トグルのメニュー名
#define kKESCMSyncViewsMenuKey		kKESCMStringPrefix "kKESCMSyncViewsMenuKey"	// パネルのフライアウト「Sync Layout Views」トグルのメニュー名
#define kKESCMAlignViewsMenuKey		kKESCMStringPrefix "kKESCMAlignViewsMenuKey"	// パネルのフライアウト「Align Other Views to Active」(実行アクション)のメニュー名
#define kKESCMShowSrcMarksMenuKey	kKESCMStringPrefix "kKESCMShowSrcMarksMenuKey"	// パネルのフライアウト「Show Marks on Source」トグルのメニュー名
#define kKESCMPageMapToggleMenuKey	kKESCMStringPrefix "kKESCMPageMapToggleMenuKey"	// ページパネル右クリックのトグル「KCM: Register as Added/Removed Pages」の既定メニュー名(表示時は UpdateActionStates が Target=Added/Source=Removed に動的差し替え)
#define kKESCMPageCheckMenuKey		kKESCMStringPrefix "kKESCMPageCheckMenuKey"	// ページパネル右クリックのトグル「KCM: Check」のメニュー名
#define kKESCMPageRefreshCompareMenuKey	kKESCMStringPrefix "kKESCMPageRefreshCompareMenuKey"	// ページパネル右クリックの「KCM: Refresh Page Comparison」のメニュー名(選択ページの比較を再検出して更新)
#define kKESCMIgnorePageNumMenuKey	kKESCMStringPrefix "kKESCMIgnorePageNumMenuKey"	// パネルのフライアウト「Ignore Page Number Marker」トグルのメニュー名
#define kKESCMHoldToHideMarksMenuKey	kKESCMStringPrefix "kKESCMHoldToHideMarksMenuKey"	// パネルのフライアウト「Hold to Hide Marks」トグルのメニュー名
#define kKESCMScrollMapMenuKey		kKESCMStringPrefix "kKESCMScrollMapMenuKey"	// パネルのフライアウト「Show Scrollbar Map」トグルのメニュー名
#define kKESCMSavePanelStateMenuKey	kKESCMStringPrefix "kKESCMSavePanelStateMenuKey"	// パネルのフライアウト「Save Panel Settings」項目のメニュー名
#define kKESCMSaveChecksMenuKey		kKESCMStringPrefix "kKESCMSaveChecksMenuKey"	// パネルのフライアウト「Save Check & Register」項目のメニュー名
#define kKESCMLoadChecksMenuKey		kKESCMStringPrefix "kKESCMLoadChecksMenuKey"	// パネルのフライアウト「Load Check & Register」項目のメニュー名
#define kKESCMFindOversetMenuKey	kKESCMStringPrefix "kKESCMFindOversetMenuKey"	// パネルのフライアウト「Find Overset」トグルのメニュー名
#define kKESCMRefreshOversetMenuKey	kKESCMStringPrefix "kKESCMRefreshOversetMenuKey"	// パネルのフライアウト「Refresh Overset」項目のメニュー名
#define kKESCMExportChangedPagesMenuKey	kKESCMStringPrefix "kKESCMExportChangedPagesMenuKey"	// パネルのフライアウト「Export Changed Pages...」項目のメニュー名
#define kKESCMCompareBooksMenuKey	kKESCMStringPrefix "kKESCMCompareBooksMenuKey"	// パネルのフライアウト「Compare Books」項目のメニュー名(ブック同士を章単位で比較)
#define kKESCMBookDialogTitleKey	kKESCMStringPrefix "kKESCMBookDialogTitleKey"	// ブック比較ダイアログのタイトル
#define kKESCMBookCompareKey		kKESCMStringPrefix "kKESCMBookCompareKey"		// (退役 2026-08-12)旧「Compare」ボタンのラベル。ボタンごと撤去したので参照は無いが、enUS テーブルの行とともに残してある＝復活させるとき対で戻せる
#define kKESCMBookReadyKey			kKESCMStringPrefix "kKESCMBookReadyKey"			// 比較前のステータス文。★2026-08-12 以降ここに来るのは「比較を1度もしていないのにダイアログが開いた」場合だけ(通常は結果の要約で上書きされる)
#define kKESCMBookHintKey			kKESCMStringPrefix "kKESCMBookHintKey"			// ★ステータスの2行目(2026-08-13 ユーザー指示)=行の右クリックで比較を始められることの案内。**固定文**なので .fr が初期テキストとして持ち、C++ は一度も書き換えない(要約と違い run ごとに変わらないため)
// ★ブック比較の確認アラート(2026-08-12 ユーザー指示「Compare... をするとアラートを出し、OK が押されると比較する」)。
//   ⚠★**2026-08-13 訂正**＝旧記述「ここは日本語 UI では日本語で出す＝KESCMLoc の対象が4箇所へ増えた」は
//     **下の確認キーについては失効**。ユーザー指示「英語で良いです」で確認アラートは全ロケール英語に戻り、
//     KESCMLoc の対象は3箇所になった。**日本語のままなのは次の kKESCMBookNoPairKey だけ**。
#define kKESCMBookCompareConfirmKey	kKESCMStringPrefix "kKESCMBookCompareConfirmKey"	// 「これから比較する」確認アラートの1行目(この下に target: / source: のフルパスが続く)
#define kKESCMBookNoPairKey			kKESCMStringPrefix "kKESCMBookNoPairKey"			// 2ブックを解決できなかったときの警告アラート(通常はメニューが灰色なので到達しない)
#define kKESCMBookRowStartMenuKey	kKESCMStringPrefix "kKESCMBookRowStartMenuKey"	// 章行の右クリックメニューの「Start Change Marker」項目名
// 章行の右クリックメニュー(2026-08-12)。この名前の MenuDef サブツリーを KESCMBookRowEH::RButtonDn が
// IMenuManager::HandlePopupMenu でカーソル位置に出す＝製品の Links / Layers パネルの行メニューと
// 同じ機構で、KBS の結果行(kKBSResultRowMenuName)・KESCL のレポート行と同じ作り。
// ★この根の名前は画面に出ないので、翻訳キーではなく素のリテラルでよい。
#define kKESCMBookRowMenuName		"KESCMRtMenuBookRow"
#define kKESCMTranslucentPanelMenuKey	kKESCMStringPrefix "kKESCMTranslucentPanelMenuKey"	// パネルのフライアウト「Translucent Panel」トグルのメニュー名
#define kKESCMTranslucentPagesPanelMenuKey	kKESCMStringPrefix "kKESCMTranslucentPagesPanelMenuKey"	// パネルのフライアウト「Translucent Pages Panel」トグルのメニュー名(対象は本体のページパネル)
#define kKESCMTranslucentBookDialogMenuKey	kKESCMStringPrefix "kKESCMTranslucentBookDialogMenuKey"	// パネルのフライアウト「Translucent Book Dialog」トグルのメニュー名(対象はブック比較ダイアログ。2026-08-13 追加)
// (kKESCMTranslucentToolboxMenuKey は 2026-08-07 に機能ごと撤去。文字列キーは ActionID と違って
//  外部に保存されないので、跡地を残す必要は無い＝行ごと削除してある。)

// ショートカット割当可アクション用のアクションエリア(KESCL と同型。2026-07-24)。ActionDef の area 欄に
// kKESCMPanelMenuActionArea を渡し、StringTable でその値 kKESCMPanelMenuActionAreaValue に解決させると、
// Edit > キーボードショートカット の Product Area「Palette Menus」に「Kohaku Change Marker: <名前>」として並ぶ。
// 先頭の "KBSCE " は KBSC エディタ用エリアキーの規約プレフィックス。既定ショートカットは同梱しない(ユーザーが割当)。
// ※この表記は表示ラベルにすぎない。ショートカット自体は IShortcutManager が ActionID + コンテキスト文字列で
//   保持する(IShortcutManager.h の AddShortcut/GetActionIDOfShortcut)ので、ここを改名しても既存の割当は外れない。
#define kKESCMPanelMenuActionArea		"KBSCE Palette Menus: Kohaku Change Marker: "
#define kKESCMPanelMenuActionAreaValue	"Palette Menus:Kohaku Change Marker"

// パネル: 内部フライアウト(ポップアップ)メニュー名＋そのメニューパス。
#define kKESCMInternalPopupMenuNameKey	kKESCMStringPrefix "kKESCMInternalPopupMenuNameKey"
#define kKESCMPopupMenuPath				kKESCMInternalPopupMenuNameKey

// フライアウトの「Marks opacity」サブメニュー(中に 25% / 75%)。名前は英語リテラル(KESCM のメニュー名は
// 全ロケール英語で統一)。子項目(25%/75%)はこの kKESCMOpacitySubmenuPath を親メニューパスとして指す。
// 親ノードは MenuDef で actionID 0・パス末尾に区切り(kSDKDefDelimitMenuPath)を付けて宣言する
// (Adobe 実例 open/components/buttonui FormFieldUIMenu.fr / incopyexportui と同じ流儀)。
#define kKESCMOpacitySubmenuName		"Marks opacity"
#define kKESCMOpacitySubmenuPath		kKESCMPopupMenuPath kSDKDefDelimitMenuPath kKESCMOpacitySubmenuName

// パネルの文字列キー(値は KESCM_enUS.fr の StringTable。全ロケールがこの1枚を引く=
// LocaleIndex の全行が index_enUS を指す。KESCM_jaJP.fr は 2026-08-05 に撤去し、日本語で出す
// 2箇所だけ KESCMLoc.h の実行時切替に移した)。
#define kKESCMPanelTitleKey		kKESCMStringPrefix "kKESCMPanelTitleKey"
#define kKESCMTargetLabelKey	kKESCMStringPrefix "kKESCMTargetLabelKey"	// パネルの "Target:" ラベル。リテラルだとシステム訳と衝突するため自前キーで持つ
#define kKESCMSourceLabelKey	kKESCMStringPrefix "kKESCMSourceLabelKey"	// パネルの "Source:" ラベル。リテラル "Source:" は日本語ロケールで「スタイルソース :」に化けるため自前キーで持つ
#define kKESCMStartButtonKey	kKESCMStringPrefix "kKESCMStartButtonKey"	// フライアウト「Start / Stop」項目の既定メニュー名(未開始=Start)。表示時は UpdateActionStates が arm 状態で Start↔Stop に動的差し替え(旧トグルボタンのキャプションキーを流用)
#define kKESCMPrintCheckKey		kKESCMStringPrefix "kKESCMPrintCheckKey"	// フライアウト「Print comparison marks」トグルのメニュー名(旧パネルチェックボックスのキャプションキーを流用)
#define kKESCMOpacity25Key		kKESCMStringPrefix "kKESCMOpacity25Key"	// サブメニュー「Marks opacity」内の子項目名(="25%")
#define kKESCMOpacity75Key		kKESCMStringPrefix "kKESCMOpacity75Key"	// サブメニュー「Marks opacity」内の子項目名(="75%")
#define kKESCMPrevChangeKey		kKESCMStringPrefix "kKESCMPrevChangeKey"	// パネルの「◀ Prev」ボタンのキャプション(英語固定)
#define kKESCMNextChangeKey		kKESCMStringPrefix "kKESCMNextChangeKey"	// パネルの「Next ▶」ボタンのキャプション(英語固定)
#define kKESCMHintKey			kKESCMStringPrefix "kKESCMHintKey"
#define kKESCMToolStringKey		kKESCMStringPrefix "kKESCMToolStringKey"	// ツールボックスのツール名(ツールチップ)。全ロケール英語で統一

// Story Edits セクションの文字列。⚠文言に "text" を使わない——この一覧はテキスト以外の変更も載せる
// ので "No text edits" のような言い方は事実と食い違う(設計書 §3-5)。
#define kKESCMStorySectionLabelKey	kKESCMStringPrefix "kKESCMStorySectionLabelKey"	// セクション見出し(件数は C++ が付ける)
#define kKESCMStoryNoEditsKey		kKESCMStringPrefix "kKESCMStoryNoEditsKey"		// 変更0件のときに出す1行
#define kKESCMStoryKindTextKey		kKESCMStringPrefix "kKESCMStoryKindTextKey"		// 行の右=文字が変わった
#define kKESCMStoryKindAttrKey		kKESCMStringPrefix "kKESCMStoryKindAttrKey"		// 行の右=属性が変わった(適用スタイル・オーバーライド・表の罫線を含む)
#define kKESCMStoryKindOtherKey		kKESCMStringPrefix "kKESCMStoryKindOtherKey"	// 行の右=上記以外(実測では出にくい。KESCMStoryStamp.h 参照)
#define kKESCMStoryKindAddedKey		kKESCMStringPrefix "kKESCMStoryKindAddedKey"	// 行の右=Source 側に相手が無い

// 一覧の列見出し(2026-08-10)。★中の語をそのまま使わない: 2列目の見出しは "Text" ではなく "Story"、
// 3列目は "Kind" ではなく "Change"(ユーザー指定)。理由は語の衝突——3列目に出る**値**が "Text" なので、
// 2列目の見出しを "Text" にすると同じ語が1行の中で別の意味で2回出る。
#define kKESCMStoryColUIDKey		kKESCMStringPrefix "kKESCMStoryColUIDKey"		// 見出し左=ストーリーの UID
#define kKESCMStoryColTextKey		kKESCMStringPrefix "kKESCMStoryColTextKey"		// 見出し中=本文の書き出し
#define kKESCMStoryColKindKey		kKESCMStringPrefix "kKESCMStoryColKindKey"		// 見出し右=変わった種類

// PNG アイコンリソース(プラグインに埋め込み; .pln とは別ファイルでは出荷しない)。
#define kKESCMIconOnResID	1001
#define kKESCMIconOffResID	1002
#define kKESCMPaletteIconResID	1003	// パネルが折りたたまれた時に出る小さいドックタブアイコン

// スクロールバー地図stripのビューリソースID(kViewRsrcType; ::CreateObject で実行時生成する。KESCMScrollMap.cpp)
#define kKESCMScrollMapRsrcID	1010

// Story Edits の行テンプレートのビューリソースID(kViewRsrcType; CreateObjectNoInit で1行ずつ生成する。
// KESCMStoryTreeWidgetMgr.cpp)。
#define kKESCMStoryRowRsrcID	1011

// ブック比較ダイアログのビューリソースID(kViewRsrcType)。KESCMBookDialog.cpp が RsrcSpec で指す。
// ★1010/1011 と同じ採番の続き。手本=KESCL の Jump Offset ダイアログ(あちらは kSDKDefDialogResourceID)。
#define kKESCMBookDialogRsrcID	1012

// ブック比較ダイアログの章一覧の、行テンプレートのビューリソースID(kViewRsrcType;
// CreateObjectNoInit で1行ずつ生成する。KESCMBookTreeWidgetMgr.cpp)。★1011(Story Edits の行)と
// 同じ作りで、違うのは中身が2列であることとダイアログ用のフォントを使うことだけ。
#define kKESCMBookRowRsrcID	1013

// 章一覧の行の高さ。★Story Edits の kKESCMStoryRowHeight と同じく .fr と C++ の両方がこの1つの定数を
// 読む(行リソースの Frame・ツリーのスクロール増分・GetNodeWidgetHeight)。
// ★下の 19 と違って、こちらは SDK 標準の kCC2016PanelTreeNodeHeight と同じ 22
// (StdHeightWidthConstants.h:50)。パレットの一覧が 19 なのは**パレットフォントを実測して決めた値**
// (2026-08-11)で、ダイアログのフォントは測っていない＝測っていない側では標準に従う。製品の
// AutoCorrect 環境設定のリストも、ダイアログの中の一覧をこの定数で組んでいる
// (AutoCorrectPrefsPanel_enUS.fr:288)。
#define kKESCMBookRowHeight	22

// 一覧の行の高さ。★.fr と C++ の両方がこの1つの定数を読む(Adobe の StdHeightWidthConstants.h と同じ形)
// ＝行リソースの Frame・ツリーのスクロール増分・GetNodeWidgetHeight が同じ事実を語る。値は KBS の
// kKBSResultRowHeight と同じ 19＝パレットフォントでの実測値で、SDK の kCC2016PanelTreeNodeHeight(=22)ではない。
#define kKESCMStoryRowHeight	19

// 一覧の列見出しの帯の高さ(ラベル 14px ＋ 罫線 1px ＋ 上下の余白 3px。2026-08-10)。
// ★行高と同じく .fr と C++ の両方がこの1つの定数を読む＝帯を厚くすれば、ツリーの位置も
//   セクションの最小・既定の高さも同時に動く(KESCM.fr / KESCMStorySection.cpp)。
#define kKESCMStoryHeaderHeight	18

// ★★パネルの最小サイズ(2026-08-10 ユーザー指定「今のを最小の設定で、パネルの大きさは固定ではなく」)。
//   PanelList を kIsResizable にしたので、下限を守るのは KESCMPanelView::ConstrainDimensions。
// ・幅 = これまでの固定幅そのまま。中の widget はすべて縁に束縛してあるので広げる方向は自由に伸びるが、
//   これより狭めるとステータス欄が読めなくなり、一覧の行は省略記号だけになる。
// ・高さ = 上ペインの設計高。★Story Edits を**閉じている間はこれが上限でもある**——閉じているときの
//   パネルは固定座標のコントロール群だけなので、伸ばしても下に空白の帯ができるだけになる。
//   開いている間の下限は「上ペイン + セクションの最小(= .fr の Bottom snap)」で、C++ 側は
//   分割バーに実際の snap 値を聞く(数字を2か所に書かない)。
// ★2026-08-10: 173 → 185。Story Edits の帯に猫イラストを下ろした分(20px の帯では 32×32 の絵が
//   収まらない)。帯が 12px 高くなり、代わりにステータス欄が右端まで(180→216)伸びた。
#define kKESCMPanelMinWidth		224
#define kKESCMPanelTopPaneHeight	185

// ✓チェックマークカーソルのリソースID。CursorSpec の CursorID として使い、HOTC(このID)でホットスポット
// (✓の折れ点=座標取得点)を指定する。★2026-07-25: 画像はコールバック描画から PNGC リソースへ変更
// (KESCM_Check_10_18.png ＋ @2x=+kHIDPICrsrOffset / @3to2x=+kHIDPI150CrsrOffset)。押下時のゴミの発生源
// =基底のモーダルカーソル取得によるコールバック再実行 を断つため。KESCMCursorProvider.cpp / KESCM.fr。
#define kKESCMCheckCursorResID	1020

// Alt+左「色比較」の CMYK 情報カーソルのリソースID。✓カーソルと HOTC は同じ(10,18)だが CursorID は
// 分ける。同一 CursorID を使い回すとカーソルキャッシュが ✓(24×24)と CMYK情報(150×60)を取り違え、
// 色比較の初回フレームに「ゴミ」が一瞬見えた(ユーザー報告 2026-07-13)。別IDにして解消。KESCMPeek.cpp。
#define kKESCMCmykCursorResID	1021

// CMYK 情報カーソルの交互切替用の第2リソースID。ドラッグ中の数値更新は kFalse スペックの「入れ直し」で
// 行うが(動的 kTrue スペックは設定の瞬間に未初期化バッファが見える=初回ゴミの真因のため全廃。2026-07-14)、
// 同一スペックの再設定が no-op 扱いされても確実に描き直しが起きるよう 1021 と 1022 を交互に使う
// (KESCMTracker.cpp の InstallCmykCursor)。HOTC は 1021 と同じ (10,18)=切替でカーソル位置は動かない。
#define kKESCMCmykCursor2ResID	1022

// ✓カーソルの非アクティブ版(白抜き=黒フチ+白本体)のリソースID。ツール選択中、「Start 中かつマウス下が
// Target 文書」のときだけ黒✓、それ以外(Source・第3の文書・未 Start)は白抜き✓=「ここではツールは
// 効かない」の明示(ユーザー指定 2026-07-15。灰色本体は判別しづらく反転式に変更)。CursorID を分けるのは
// キャッシュの取り違え防止(1021/1022 と同じ理由)+ClearCache 不要で切り替えるため。
// HOTC は ✓ と同じ (10,18)。画像は黒✓と同様 PNGC リソース(KESCM_CheckOff_10_18.png ＋ @2x / @3to2x。
// 2026-07-25)。KESCMCursorProvider.cpp / KESCM.fr。
#define kKESCMCheckCursorInactiveResID	1023

// ツールボックスの KESCM ツール専用アイコン(32×32 通常 / 64×64 = +kHIDPIIconOffset)。従来はパネル用
// アイコン(kKESCMIconOnResID)を流用していたが、専用画像(KESCM_Tool_32.png/_64.png)を用意したため差し替え
// (ユーザー提供 2026-07-14)。ダーク版は用意していないため PNGAD もライト版と同じ画像を指す(=流用)。
#define kKESCMToolIconResID	1030

// Menu item positions (flyout order, 2026-07-24 に大幅入れ替え):
//   Start/Stop(9.0) → Compare Books(9.05) → ─線Sep1(9.1) →
//   [表示系トグル群] Hold to Hide Marks(9.20) → Ignore Page Number Marker(9.22) → Marks opacity ▸(9.24) →
//     Print comparison marks(9.26) → Show Original Page Numbers(9.28) →
//     Show Marks on Source(9.30) → Show Scrollbar Map(9.32) → Sync Layout Views(9.34) →
//     Translucent Panel(9.36) →
//   ─線OversetSep(9.40) → Find Overset(9.42) → Refresh Overset(9.44) →
//   ─線Sep3(9.50) → [実行アクション群] Align Other Views to Active(9.52) → Export Changed Pages...(9.53) →	★Compare Books はここに居たが 2026-08-12 に Start の直下へ移した
//     Hide Unchanged Spreads(9.54) → Save Panel Settings(9.56) → Save Check & Register(9.58) →
//     Load Check & Register(9.60) →
//   ─線Sep2(9.95) → How to Use(10) → About this plug-in(12)。 ※About Scripting(11)は 2026-07-25 撤去
// ※メニュー名は日本語ロケールでも英語で統一(2026-07-04)。区切り線は Sep1/OversetSep/Sep3/Sep2 の4本を再利用。
#define kKESCMStartStopMenuItemPosition		9.0	// 「Start / Stop」(比較開始/解除)をフライアウト先頭に。名前は arm 状態で動的に Start↔Stop
#define kKESCMSep1MenuItemPosition			9.1	// Start の下の区切り線(パス末尾 ":-")
// ── 表示系トグル群 ──
#define kKESCMHoldToHideMarksMenuItemPosition	9.20	// チェック式トグル「Hold to Hide Marks」(枠表示の極性反転)。Sep1 の直後(群の先頭)
#define kKESCMIgnorePageNumMenuItemPosition	9.22	// チェック式トグル「Ignore Page Number Marker」
#define kKESCMOpacitySubmenuMenuItemPosition	9.24	// 「Marks opacity」サブメニュー(中に 25% / 75%)。Print の上へ入れ替え(2026-07-24)
#define kKESCMPrintMarksMenuItemPosition	9.26	// チェック式トグル「Print comparison marks」。Marks opacity の下へ入れ替え(2026-07-24)
#define kKESCMOpacity25SubMenuItemPosition	1.0	// サブメニュー「Marks opacity」内: 25%(選択中に✓)
#define kKESCMOpacity75SubMenuItemPosition	2.0	// サブメニュー「Marks opacity」内: 75%(25% と相互排他)
// (9.27 = 「Show HUD」は 2026-08-06 に機能ごと撤去。位置番号は空き=別項目に使ってよい)
#define kKESCMShowOldNumsMenuItemPosition	9.28	// チェック式トグル「Show Original Page Numbers」
#define kKESCMShowSrcMarksMenuItemPosition	9.30	// チェック式トグル「Show Marks on Source」
#define kKESCMScrollMapMenuItemPosition		9.32	// チェック式トグル「Show Scrollbar Map」
#define kKESCMSyncViewsMenuItemPosition		9.34	// チェック式トグル「Sync Layout Views」
#define kKESCMTranslucentPagesPanelMenuItemPosition	9.36	// チェック式トグル「Translucent Pages Panel」(★Windows 専用=フローティング中の**本体のページパネル**を半透明に。2026-08-06 追加。★同日ユーザー指定で Translucent Panel より上へ)
// (9.37 は「Translucent Toolbox」の跡地。2026-08-07 に機能ごと撤去したので空き番。)
#define kKESCMTranslucentPanelMenuItemPosition	9.38	// チェック式トグル「Translucent Panel」(表示系トグル群の末尾。★Windows 専用=フローティング中のパネル自身を半透明に。2026-08-06 に Pages 側と上下を入れ替え)
#define kKESCMTranslucentBookDialogMenuItemPosition	9.39	// チェック式トグル「Translucent Book Dialog」(★Windows 専用=ブック比較ダイアログを半透明に。2026-08-13 追加。Translucent 3兄弟の末尾＝9.36 Pages / 9.38 Panel / 9.39 ここ)
// ── Overset 群 ──
#define kKESCMOversetSepMenuItemPosition	9.40	// Find Overset 群の上の区切り線(パス末尾 ":-")
#define kKESCMFindOversetMenuItemPosition	9.42	// チェック式トグル「Find Overset」(アクティブ文書の overset ページに十字)
#define kKESCMRefreshOversetMenuItemPosition	9.44	// 実行アクション「Refresh Overset」(ON時のみ有効=再走査)
// ── 実行アクション群 ──
#define kKESCMSep3MenuItemPosition			9.50	// Refresh Overset の下の区切り線(パス末尾 ":-")。この下に実行アクション群を置く
#define kKESCMAlignViewsMenuItemPosition	9.52	// 実行アクション「Align Other Views to Active」を実行アクション群の先頭に(2026-07-24)
#define kKESCMHideUnchangedMenuItemPosition	9.54	// チェック式トグル「Hide Unchanged Spreads」。⚠2026-08-12 まで下の Compare Books と**同じ 9.54 で重複していた**(同値だと並びを決めるのは MenuDef の登録順だけになる)。Compare Books が Start の直下へ抜けたので重複は解消済み
#define kKESCMSavePanelStateMenuItemPosition	9.56	// 実行アクション「Save Panel Settings」
#define kKESCMSaveChecksMenuItemPosition	9.58	// 実行アクション「Save Check & Register」
#define kKESCMLoadChecksMenuItemPosition	9.60	// 実行アクション「Load Check & Register」
#define kKESCMExportChangedPagesMenuItemPosition	9.53	// 実行アクション「Export Changed Pages...」(変更ページ一覧をTSVで保存)。Align の直下(2026-07-25 ユーザー指定)
// ★★Compare Books の位置は 9.54(実行アクション群) → 9.53 → **9.05** と 2026-08-12 に二度動いた(ユーザー指定
//   「一つ上へ」→「Start のすぐ下に」)。9.05 は **Start(9.0) と 区切り線 Sep1(9.1) の間**＝Start との間に線が
//   入らない位置で、比較を**始める**2つの項目が1つの群になる。⚠**旧コメントの「文書比較(Start)とは独立した
//   経路なので Start 群ではなく実行アクション群に置く」は撤回**(2026-08-12 ユーザー判断)。
#define kKESCMCompareBooksMenuItemPosition	9.05	// 実行アクション「Compare Books」(ブック同士を章単位で比較)。Start の直下
// ブック比較ダイアログの章行の右クリックメニュー内の位置。★このメニューは項目が1つしかないので
// 値そのものに意味は無い(パネルのフライアウトとは別の木＝kKESCMBookRowMenuName の下)。
#define kKESCMBookRowStartMenuItemPosition	1.0		// 章行の右クリック「Start Change Marker」
// ── 情報系(末尾) ──
#define kKESCMSep2MenuItemPosition			9.95	// How to Use の上の区切り線(パス末尾 ":-")
#define kKESCMUsageMenuItemPosition			10.0	// 「使い方」
// ページパネルのページ右クリックメニュー(内部名 RtMenuPagesPanel、2026-07-05 実機確定)内の位置。
// 本家項目の後ろ(末尾)に付ける。内部名は非翻訳キーなので全ロケール共通で効く。
#define kKESCMPageMapSepMenuItemPosition	2999.0	// KESCM 追加項目(Register 3000.0 / Check 3001.0)の直上の区切り線。本家メニュー項目と KESCM 項目を視覚的に分ける
#define kKESCMPageMapToggleMenuItemPosition	3000.0
#define kKESCMPageCheckMenuItemPosition		3001.0	// 「KCM: Check」を Register の直後(ページパネル右クリック末尾)に
#define kKESCMPageRefreshCompareMenuItemPosition	3002.0	// 「KCM: Refresh Page Comparison」を Check の直後(ページパネル右クリック末尾)に
#define kKESCMAboutThisMenuItemPosition		12.0	// 末尾に「このプラグインについて」(11.0=旧 About Scripting は撤去済み 2026-07-25)


// Initial data format version numbers
#define kKESCMFirstMajorFormatNumber  RezLong(1)
#define kKESCMFirstMinorFormatNumber  RezLong(0)

// Data format version numbers for the PluginVersion resource 
#define kKESCMCurrentMajorFormatNumber kKESCMFirstMajorFormatNumber
#define kKESCMCurrentMinorFormatNumber kKESCMFirstMinorFormatNumber

#endif // __KESCMID_h__
