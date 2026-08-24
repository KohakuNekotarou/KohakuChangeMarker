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
//
// ★★2026-08-15・model/UI 分割 第2段 Task 6: **UI 側の実装 26 本を ui/KCMUIFactoryList.h へ移した。**
//   ここに残るのは model 側の 10 本だけ ---- 比較マークの描画2本、model 側の起動/終了、
//   文書レスポンダ、ScriptProvider、そして境界の Facade 5本。
//
//   ⚠**登録漏れは完全に無言で失敗する**（`CREATE_PMINTERFACE` を書いてあっても、この表に載って
//     いなければ実体は作られない。ビルドも起動も通り、その機能だけが黙って動かない）。
//     ∴**この表は記憶で足さない**——`CREATE_PMINTERFACE` と `CREATE_PERSIST_PMINTERFACE` の
//     両方を Grep し、その結果と1対1で突き合わせて作ること。
//     ★実際 2026-08-15 の分割では、`CREATE_PMINTERFACE` だけを Grep して 3 本
//     （ScrollMapView / Tool / PanelView）を落としかけた＝**tool と自前 view は PERSIST 版を使う。**
//
//   ⚠**Impl ID の番号はまだ `kKESCMPrefix + N` のまま**。移した実装を名指ししている `.fr` の
//     Class / AddIn ブロックが KESCM.fr 側に残っているため（振り替えは Task 6B）。
//
// (★2026-08-20: KESCMDrawEventSrvc / KESCMDrawEventHandler の2本はここにあったが撤去した＝マークの描画を
//  下のアドーンメントへ一本化したため。⚠**KESCMDrawEventHandler クラス自体は今も在る**が、
//  IDrwEvtHandler を継承しない「描画本体 DrawSpreadMarks と static 群の入れ物」になったので、
//  boss にも FactoryList にも載らない。)
REGISTER_PMINTERFACE(KESCMRingAdornmentShape, kKESCMRingAdornmentImpl)	// ★**マークを描く唯一の経路**(KESCMRingAdornment.cpp)。★描画本体は持たず KESCMDrawEventHandler::DrawSpreadMarks を呼ぶだけ
REGISTER_PMINTERFACE(KESCMRingFlattenerUsage, kKESCMRingFlattenerUsageImpl)	// ★★上と対になる透明の申告口。**これが無いと PDF 1.3 でリングが全面ベタになる**(足した理由そのもの)
REGISTER_PMINTERFACE(KESCMRingAdornmentStartup, kKESCMRingAdornmentStartupImpl)	// ★上の2つを**実行コンテキストごとに**セッションへ登録する(BG スレッドを含む)。テキスト版のサービス宣言に相当するものを手で書いた形
REGISTER_PMINTERFACE(KESCMPeekStartup, kKESCMPeekStartupImpl)	// model 側の起動/終了(KESCMPeek.cpp)。UI 側の対は KCMUI の KESCMUIStartup
REGISTER_PMINTERFACE(KESCMDocResponder, kKESCMDocResponderImpl)	// ServiceProvider は API 提供の実装を .fr で名指し(2026-08-06)
REGISTER_PMINTERFACE(KESCMBeforeSaveDocResponder, kKESCMBeforeSaveResponderImpl)	// 保存の前に Hide Unchanged を戻す(2026-08-19。同じく ServiceProvider は API 提供)
REGISTER_PMINTERFACE(KESCMPDFExportSetup, kKESCMPDFExportSetupImpl)	// ★★★PDF 書き出しの BeginExport で透明の一覧に載せ、EndExport で降ろす(KESCMRingAdornment.cpp)。**非同期ではクローン db が渡るので元の文書を一度も触らない**。⚠**印刷側の対は無い**＝効かないからではなく「印刷にそこまでの厳密性は要らない」というユーザー判断で外した(2026-08-20。載せれば印刷も濃くなる＝A/B と復活手順は同ファイルの節5)
REGISTER_PMINTERFACE(KESCMScriptProvider, kKESCMScriptProviderImpl)	// ★この1本が**公開する6プロパティ全部**を serve する(app.kcmStatus / app.kcmBookResult ＋ stories[n] の変更カウンター4本。2026-08-15 に2つの boss を1つへ統合)。すべて読み取り専用でメソッドは0本。★ScriptProvider は UI ではない(設計書 §4.1)
REGISTER_PMINTERFACE(KESCMCompareFacade, kKESCMCompareFacadeImpl)	// UI が比較エンジンに頼む窓口(kUtilsBoss へ AddIn。KESCMFacades.cpp)
REGISTER_PMINTERFACE(KESCMMarkData, kKESCMMarkDataImpl)	// UI が比較結果を読む窓口(同上。★読み取り専用)
REGISTER_PMINTERFACE(KESCMPageFlagsFacade, kKESCMPageFlagsFacadeImpl)	// UI が Register/Check を書き換える窓口(同上)
REGISTER_PMINTERFACE(KESCMStoryEditsFacade, kKESCMStoryEditsFacadeImpl)	// UI が Story Edits の一覧を読む窓口(同上。★読み取り専用)
REGISTER_PMINTERFACE(KESCMBookFacade, kKESCMBookFacadeImpl)	// UI がブック比較を頼む窓口(同上。★第1段の5本目)
REGISTER_PMINTERFACE(KESCMStoryMarkFacade, kKESCMStoryMarkFacadeImpl)	// ★境界の6本目(2026-08-23)＝UI が Story モードのマークを出す/消す窓口(同上。KESCMFacades.cpp)
REGISTER_PMINTERFACE(KESCMStoryMarkerAdornment, kKESCMStoryMarkerAdornmentImpl)	// ★★変わった文字の下に色地を敷くグローバルテキストアドーンメント(KESCMStoryMarker.cpp。⚠2026-08-24 までは Difference 合成の反転だった)。**2026-08-23 に UI 側から移設**＝UI の File>Export>PDF は BG で走り kUIPlugIn には配られないので、紙・PDF に出すには model 側に居るしかない
REGISTER_PMINTERFACE(KESCMStoryMarkerExpiryTask, kKESCMStoryMarkerExpiryImpl)	// 上のうち**ジャンプの点滅だけ**を1秒ほどで引っ込める IIdleTask(KESCMStoryMarkerExpiry.cpp)。★同じ回に移設(マーカーが Start/Stop を呼ぶので UI に残すと逆依存になる)
