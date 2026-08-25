//========================================================================================
//
//  KCMThumbIdleTask.h
//
//  Pages パネルサムネイルの再生成を「次の idle」に遅延させる仕組み(KCMThumbIdleTask.cpp)。
//
//  ★なぜ遅延が要るか(2026-07-08 実機で切り分け): ターゲット文書を閉じてソースが新たに
//    アクティブ化する場合、クローズ responder(kAfterCloseDoc)のその場で
//    KCMTryRefreshPagesPanelThumbnails を呼んでも、Pages パネルがソースへ切り替わる過渡で
//    ForceRedraw が再生成を起こしきれず、ソースのサムネイルに枠が残る。切替が落ち着いた後
//    (次の idle)に purge＋ForceRedraw すれば、アクティブが安定した状態=確実に消える。
//
//  使い方: 生存している db を KCMScheduleThumbRefresh に渡す。
//  ⚠2026-08-17 訂正(API 監査 B-U8): ここは「呼び出し元は2つとも KCMPeek.cpp」と書いていたが、
//    2026-08-13 の model/UI 分割で**呼び手は両方とも UI 側へ移っている**(model からこの UI 関数は
//    呼べない)。全数 Grep での現状は2ファイル4箇所:
//    ①KCMModelChangeObserver.cpp … model からのクローズ通知を受けて、生き残った文書を渡す(3本)
//    ②KCMPeekGesture.cpp … 一括クローズ完了(kPendingDocumentsClosedMsg)で保留した UI 仕事を
//      流す経路(こちらは「今開いている文書」をその場で列挙し直す)。
//    約 150ms 後に一度だけ走り(RunTask 末尾の UninstallTask で自分をキューから外す。
//    ★kEndOfTime 返しは CIdleTask の fCurrentlyInstalled が残る契約違反=cpp 側の説明が正)、生存して
//    いる db にだけ KCMTryRefreshPagesPanelThumbnails を呼ぶ。終了時は KCMShutdownThumbIdleTask で解放。
//
//========================================================================================

#ifndef __KCMThumbIdleTask_h__
#define __KCMThumbIdleTask_h__

class IDataBase;

// db の Pages パネルサムネイル再生成を「次の idle(既定約150ms後)」に予約する。前面切替が
// 落ち着いてから走るので、クローズで survivor が新たにアクティブ化するケースでも確実に消える。
// 何度呼んでも安全(db を集約し、idle task は1個を再利用。二重 AddTask はしない)。
void KCMScheduleThumbRefresh(IDataBase* db);

// 共有 idle task インスタンスを解放する(アプリ終了時に呼ぶ)。予約中なら RemoveTask してから解放。
void KCMShutdownThumbIdleTask();

#endif // __KCMThumbIdleTask_h__
