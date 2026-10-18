#pragma once
//
// GhostView paleti. Degerler PhotoViewer\src\HudRenderer.cpp'den birebir;
// yeni sayi icat edilmez (04-UI-SPEC.md bolum 1).
// Renkler 0xRRGGBB, alfa ayri verilir.
//
#include <cstdint>

namespace gm::theme {

inline constexpr uint32_t Ground     = 0x111116; // kutuphane ve ayarlar zemini
inline constexpr uint32_t Panel      = 0x17171C; // GhostView HUD zemini (0.09, 0.09, 0.11)
inline constexpr uint32_t Panel2     = 0x1E1E25;
inline constexpr uint32_t Elevated   = 0x272730;
inline constexpr uint32_t Border     = 0x737380; // (0.45, 0.45, 0.50)
inline constexpr uint32_t Text       = 0xF5F5FA; // (0.96, 0.96, 0.98)
inline constexpr uint32_t TextDim    = 0xCCD1DB; // (0.80, 0.82, 0.86)
inline constexpr uint32_t TextMuted  = 0x8A8F9C;
inline constexpr uint32_t Accent     = 0x00A2FF; // ev tarzi vurgu
inline constexpr uint32_t Press      = 0x00A6FF; // (0.00, 0.65, 1.00)
inline constexpr uint32_t Active     = 0x008CF2; // (0.00, 0.55, 0.95)
inline constexpr uint32_t ActiveEdge = 0x00D9FF; // (0.00, 0.85, 1.00)
inline constexpr uint32_t ReadTone = 0x17243A;
inline constexpr uint32_t UnreadTone = 0x383327;
inline constexpr uint32_t UpdateErrorTone = 0x382327;
inline constexpr uint32_t Danger     = 0xE81123;
inline constexpr uint32_t White      = 0xFFFFFF;
inline constexpr uint32_t Black      = 0x000000;

inline constexpr float HudAlpha        = 0.92f;
inline constexpr float BorderAlpha     = 0.45f;
inline constexpr float HoverAlpha      = 0.22f;
inline constexpr float PressAlpha      = 0.45f;
inline constexpr float ActiveAlpha     = 0.45f;
inline constexpr float ActiveEdgeAlpha = 0.90f;

// Olculer (dp)
inline constexpr float TitleH      = 38.0f;
inline constexpr float TitleBtnW   = 42.0f;
inline constexpr float MenuBtnW    = 44.0f;
inline constexpr float DrawerW     = 300.0f;
inline constexpr float DrawerRowH  = 40.0f;
inline constexpr float HudH        = 56.0f;
inline constexpr float HudMargin   = 30.0f;
inline constexpr float HudSidePad  = 14.0f;
inline constexpr float HudGap      = 4.0f;
inline constexpr float ResizeGrip  = 6.0f;

// Sure (saniye)
inline constexpr float DrawerAnim  = 0.18f;
inline constexpr float HudIdle     = 2.5f;
inline constexpr float HudFade     = 0.30f;
inline constexpr float ToastTime   = 2.5f;
inline constexpr float TipDelay    = 0.55f;

inline const wchar_t* const UiFontCandidates[]   = { L"Segoe UI Variable Text", L"Segoe UI" };
inline const wchar_t* const MonoFontCandidates[] = { L"Cascadia Mono", L"Consolas" };

} // namespace gm::theme
