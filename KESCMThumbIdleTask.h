//========================================================================================
//
//  KESCMThumbIdleTask.h
//
//  Pages パネルサムネイルの再生成を「次の idle」に遅延させる仕組み(KESCMThumbIdleTask.cpp)。
//
//  ★なぜ遅延が要るか(2026-07-08 実機で切り分け): ターゲット文書を閉じてソースが新たに
//    アクティブ化する場合、クローズ responder(kAfterCloseDoc)のその場で
//    KESCMTryRefreshPagesPanelThumbnails を呼んでも、Pages パネルがソースへ切り替わる過渡で
//    ForceRedraw が再生成を起こしきれず、ソースのサムネイルに枠が残る。切替が落ち着いた後
//    (次の idle)に purge＋ForceRedraw すれば、アクティブが安定した状態=確実に消える。
//
//  使い方: クローズ後片付け(KESCMHandleDocsClosed)で survivor db を KESCMScheduleThumbRefresh に
//    渡す。約 150ms 後に一度だけ走り(kEndOfTime で自分をキューから外す)、生存している db にだけ
//    KESCMTryRefreshPagesPanelThumbnails を呼ぶ。終了時は KESCMShutdownThumbIdleTask で解放。
//
//========================================================================================

#ifndef __KESCMThumbIdleTask_h__
#define __KESCMThumbIdleTask_h__

class IDataBase;

// db の Pages パネルサムネイル再生成を「次の idle(既定約150ms後)」に予約する。前面切替が
// 落ち着いてから走るので、クローズで survivor が新たにアクティブ化するケースでも確実に消える。
// 何度呼んでも安全(db を集約し、idle task は1個を再利用。二重 AddTask はしない)。
void KESCMScheduleThumbRefresh(IDataBase* db);

// 共有 idle task インスタンスを解放する(アプリ終了時に呼ぶ)。予約中なら RemoveTask してから解放。
void KESCMShutdownThumbIdleTask();

#endif // __KESCMThumbIdleTask_h__
