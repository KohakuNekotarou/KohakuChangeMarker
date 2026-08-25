//========================================================================================
//
//  KCMTrackerHud.h
//
//  KCM ツールで**左ボタンを押している間だけ**、押したレイアウトビューの**左上**に
//    「その窓が比較の何なのか」を1行で出す(押下中 HUD。2026-08-07 ユーザー指示)。
//
//  出す文言は4通り(判定の本体は KCMTrackerHud.cpp の KCMTrackerHudLabel):
//      比較中 + Target の窓  → "Target"
//      比較中 + Source の窓  → "Source"
//      比較中 + それ以外     → "Not in comparison"
//      Stop 中               → "Not comparing"
//    ★相手の文書名は出さない(2026-07-27 ユーザー指示)。理由は上記関数のコメント。
//
//  表示条件はこれだけ = 「KCM ツールが選ばれていて、左ボタンが押されている」。
//    ・印刷/PDF には出ない(描画側で printing を弾く)。Print comparison marks が ON でも出ない。
//    ・Hold to Hide Marks の状態も見ない(あちらは比較マークの極性トグル。この HUD とは無関係)。
//    ・押した窓にだけ出る(他の文書窓・他のビューには出ない)。
//
//  ★★描画は **Draw Event 経路**。比較マークの枠とまったく同じ描画パスなので、**枠と同時に出る**
//    (旧 HUD は sprite 層で、押下を抜けた後に one-shot タイマーで描いていたため枠より遅れていた
//     ＝ユーザー報告「押して表示されるまで時間がかかる/枠とずれる/目につく」で 2026-08-06 に全廃)。
//
//  ★★★「Draw Event ではビューの隅に描けない」は誤りだった(2026-08-07 に判明)。
//    memory/layout-screen-overlay.md は「描画はペーストボードに clip される」と書いていたが、
//    ①それは **kEndSpreadMessage(スプレッド単位)** の話で、②本当の制約は clip ではなく **Z 順**。
//    正しくはこう:
//      kEndSpreadMessage           … spread 座標。帯(スプレッド/ペーストボード)に clip されるが**前面**
//      kAfterLastSpreadDrawMessage … pasteboard 座標。ウィンドウに1回だが**背面**なので、
//                                    何も被さらないカンバス部分にだけ見える
//    ∴ **2つを併用すると各画素はどちらか一方だけが担当**し、二重描きなしでビュー全域を覆える。
//    これは KCM 自身が 2026-07-04 まで「トースト」で実際にやっていた(git 068d8fb^ の
//    KCMDrawEventHandler.cpp:551-563, :900-909, :951-963)。撤去したときに知見ごと失われていた。
//
//========================================================================================

#ifndef __KCMTrackerHud_h__
#define __KCMTrackerHud_h__

#include "BaseType.h"
#include "PMPoint.h"

class IControlView;
class IGraphicsPort;

// 押下開始(トラッカーの BeginTracking から)。view = 押されたレイアウトビュー。
// ★押した窓の再描画も**ここが自分で要求する**(Draw Event で描くので再描画が無いと1度も描かれない。
//   reveal 側の再描画は Target 窓でしか走らない = KCMTrackerHud.cpp の Invalidate のコメント)。
void KCMTrackerHudBegin(IControlView* view);

// 押下解除/中断(EndTracking / AbortTracking から)。二重に呼んでも安全。
// ★消すための再描画もここが要求する(旗を落としてから。順序は上と対称)。
void KCMTrackerHudEnd();

// この描画で HUD を描くか = 「押下中」かつ「押した窓のビュー」。描画ハンドラの早期 return 判定にも使う。
bool16 KCMTrackerHudWantsDraw(IControlView* view);

// HUD を1回描く。
//   gPort         … 描画ポート
//   view          … gd->GetView()(ズーム倍率と可視範囲を取る)
//   spreadOffset  … このポートの座標系が pasteboard からどれだけずれているか
//                   ・kAfterLastSpreadDrawMessage(pasteboard 座標) → (0,0)
//                   ・kEndSpreadMessage(spread 座標)               → そのスプレッドのオフセット
void KCMTrackerHudDraw(IGraphicsPort* gPort, IControlView* view, const PMPoint& spreadOffset);

// プラグイン終了時。持っているフォント参照を返す(状態は静的変数だけなのでこれで足りる)。
void KCMTrackerHudShutdown();

#endif // __KCMTrackerHud_h__

// End, KCMTrackerHud.h.
