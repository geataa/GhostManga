#pragma once
//
// Ayarlar (E9). Sekmeli sayfa: Gorunum, Okuyucu, Kutuphane, Siteler, Indirme,
// Arka plan, Senkron, Gelismis, Hakkinda (FR-SET-001). Degisiklik aninda
// uygulanir, yeniden baslatma istemez (FR-SET-006).
//
#include "core/Settings.h"
#include "engine/Library.h"
#include "ui/Renderer.h"
#include "ui/ShellHost.h"
#include "ui/Ui.h"

#include <string>

namespace gm {

class SettingsView {
public:
    void Draw(Renderer& r, Ui& ui, const D2D1_RECT_F& area, Settings& set, const Library& lib, ShellHost& host);
    void SetTab(int t) { m_tab = t; }

private:
    int m_tab = 0;
    std::wstring m_webview2;
    bool m_webview2Read = false;
};

} // namespace gm
