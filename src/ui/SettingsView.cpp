#include "ui/SettingsView.h"
#include "core/I18n.h"
#include "core/Paths.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <shellapi.h>
#include <algorithm>

namespace gm {
namespace {

// Edge WebView2 Evergreen surumu. Yalniz okunur; hicbir yere yazilmaz.
std::wstring ReadWebView2Version() {
    static const wchar_t* kClient = L"{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";
    const struct { HKEY root; const wchar_t* base; } places[] = {
        { HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\" },
        { HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\" },
        { HKEY_CURRENT_USER,  L"Software\\Microsoft\\EdgeUpdate\\Clients\\" },
    };
    for (const auto& p : places) {
        const std::wstring key = std::wstring(p.base) + kClient;
        wchar_t buf[64]{};
        DWORD size = sizeof(buf);
        if (RegGetValueW(p.root, key.c_str(), L"pv", RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS &&
            buf[0] && wcscmp(buf, L"0.0.0.0") != 0)
            return buf;
    }
    return std::wstring();
}

} // namespace

void SettingsView::Draw(Renderer& r, Ui& ui, const D2D1_RECT_F& area, Settings& set, const Library& lib,
                        ShellHost& host) {
    const float s = r.Scale();
    w::SetScale(s);
    r.Fill(area, theme::Ground, 1.0f);

    if (!m_webview2Read) {
        m_webview2 = ReadWebView2Version();
        m_webview2Read = true;
    }

    // Sol sekme listesi
    const S tabs[] = { S::TabAppearance, S::TabReader, S::TabLibrary, S::TabSites, S::TabDownloads,
                       S::TabBackground, S::TabSync, S::TabAdvanced, S::TabAbout };
    const float tabW = 200.0f * s;
    const D2D1_RECT_F side = Rc(area.left, area.top, area.left + tabW, area.bottom);
    r.Fill(side, theme::Panel, 1.0f);
    r.Line(side.right - 0.5f, side.top, side.right - 0.5f, side.bottom, theme::Border, 1.0f, 0.25f);
    float ty = area.top + 20.0f * s;
    r.Text(T(S::SettingsTitle), Rc(side.left + 20 * s, ty, side.right - 12 * s, ty + 28 * s), theme::Text, 18.0f * s,
           Renderer::Align::Left, Renderer::Weight::Semibold);
    ty += 44.0f * s;
    for (int i = 0; i < (int)std::size(tabs); ++i) {
        const D2D1_RECT_F row = Rc(side.left + 8 * s, ty, side.right - 8 * s, ty + 36 * s);
        const uint32_t id = Id("set.tab", (uint32_t)i);
        // Ustteki sekmeler bu karede eski secimle cizildi: bir kare daha iste,
        // yoksa fare kipirdayana kadar iki sekme secili gorunur.
        if (ui.Button(id, row) && m_tab != i) {
            m_tab = i;
            host.Invalidate();
        }
        const bool on = m_tab == i;
        if (on) {
            r.FillRound(row, 8.0f * s, theme::Elevated, 1.0f);
            r.FillRound(Rc(row.left, row.top + 9 * s, row.left + 3 * s, row.bottom - 9 * s), 1.5f * s, theme::Accent, 1.0f);
        } else if (ui.Hot(id)) {
            r.FillRound(row, 8.0f * s, theme::White, 0.06f);
        }
        r.Text(T(tabs[i]), Rc(row.left + 14 * s, row.top, row.right - 8 * s, row.bottom),
               on ? theme::Text : theme::TextDim, 14.0f * s, Renderer::Align::Left,
               on ? Renderer::Weight::Semibold : Renderer::Weight::Normal);
        ty += 38.0f * s;
    }

    // Icerik
    const float cl = side.right + 32.0f * s;
    const float cr = std::min(area.right - 32.0f * s, cl + 680.0f * s);
    float y = area.top + 24.0f * s;
    r.Text(T(tabs[m_tab]), Rc(cl, y, cr, y + 32 * s), theme::Text, 22.0f * s, Renderer::Align::Left,
           Renderer::Weight::Semibold);
    y += 52.0f * s;
    r.PushClip(Rc(side.right, y - 4 * s, area.right, area.bottom));

    auto label = [&](const std::wstring& t) {
        r.Text(t, Rc(cl, y, cr, y + 22 * s), theme::TextDim, 12.5f * s, Renderer::Align::Left, Renderer::Weight::Semibold);
        y += 28.0f * s;
    };
    auto note = [&](const std::wstring& t) {
        const float h = r.MeasureWrappedHeight(t, 13.5f * s, cr - cl);
        r.Text(t, Rc(cl, y, cr, y + h + 4 * s), theme::TextMuted, 13.5f * s, Renderer::Align::Left,
               Renderer::Weight::Normal, 1.0f, true);
        y += h + 16.0f * s;
    };
    auto toggle = [&](uint32_t id, const std::wstring& t, bool& v, const std::wstring& hint = std::wstring()) {
        const float h = hint.empty() ? 48.0f * s : 72.0f * s;
        const bool ch = w::Toggle(r, ui, id, Rc(cl - 12 * s, y, cr, y + h), t, v, hint);
        y += h + 4.0f * s;
        if (ch) host.SaveSettings();
        return ch;
    };
    auto kv = [&](const std::wstring& k, const std::wstring& v) {
        r.Text(k, Rc(cl, y, cl + 190 * s, y + 24 * s), theme::TextMuted, 13.5f * s);
        r.Text(v, Rc(cl + 196 * s, y, cr, y + 24 * s), theme::Text, 13.5f * s);
        y += 32.0f * s;
    };

    switch (m_tab) {
    case 0: { // Gorunum
        label(T(S::Language));
        const float cell=(cr-cl-18*s)/4;
        for(int i=0;i<(int)Lang::Count_;++i){const auto& option=Languages[i];
            const auto box=RcWH(cl+(i%4)*(cell+6*s),y+(i/4)*44*s,cell,38*s);
            const std::wstring name=option.value==Lang::Auto?T(S::LangAuto):option.name;
            if(w::TextButton(r,ui,Id("set.lang",i),box,name,set.lang==option.value)) {
                set.lang=option.value;host.LanguageChanged();host.SaveSettings();
            }
        }
        y+=192*s;
        break;
    }
    case 1: { // Okuyucu
        label(T(S::FitModeLabel));
        const int sel = (int)set.fit;
        const int pick = w::Segmented(r, ui, Id("set.fit"), Rc(cl, y, cl + 440 * s, y + 38 * s),
                                      { T(S::FitW), T(S::FitH), T(S::FitWin), T(S::Fit11) }, sel);
        y += 58.0f * s;
        if (pick >= 0 && pick != sel) {
            set.fit = (FitMode)pick;
            host.SaveSettings();
        }
        toggle(Id("set.webtoon"), T(S::WebtoonDefault), set.webtoon);
        toggle(Id("set.double"), T(S::DoublePageLabel), set.doublePage);
        toggle(Id("set.rtl"), T(S::RtlLabel), set.rtl);
        toggle(Id("set.scaleup"), T(S::ScaleUp), set.scaleUp);
        toggle(Id("set.magic"), T(S::MagicScroll), set.magicScroll);
        toggle(Id("set.cover"), T(S::CoverAlone), set.coverAlone);
        toggle(Id("set.autohide"), T(S::HudAutoHide), set.hudAutoHide);
        break;
    }
    case 2: { // Kutuphane
        wchar_t count[96];
        swprintf_s(count, T(S::LibraryCountFmt), (int)lib.All().size(), (int)lib.Bookmarks().size());
        kv(T(S::ReadingList), count);
        y += 4.0f * s;
        if (w::TextButton(r, ui, Id("set.import"), Rc(cl, y, cl + 220 * s, y + 38 * s), T(S::ImportAmr), true)) {
            host.ImportAmrDialog();
        }
        if (w::TextButton(r, ui, Id("set.export"), Rc(cl + 230 * s, y, cl + 470 * s, y + 38 * s), T(S::ExportBackupBtn), false)) {
            host.ExportBackupDialog();
        }
        y += 54.0f * s;
        note(T(S::ExportHowTo));
        note(T(S::TabComingLibrary));
        break;
    }
    case 3: { // Siteler
        wchar_t mirrorBuf[64];
        swprintf_s(mirrorBuf, T(S::SitesLoadedFmt), 236);
        kv(T(S::Sites), mirrorBuf);
        kv(L"JavaScript Motoru", L"QuickJS-ng C11 Embedded");
        kv(L"HTML5 / CSS Motoru", L"Lexbor HTML5 Fast Parser");
        kv(T(S::WebView2Runtime), m_webview2.empty() ? std::wstring(T(S::NotInstalled)) : m_webview2);
        y += 8.0f * s;
        if (w::TextButton(r, ui, Id("set.opensites"), Rc(cl, y, cl + 220 * s, y + 38 * s), T(S::OpenSitesCatalog), true)) {
            host.OpenSitesCatalog();
        }
        y += 54.0f * s;
        note(T(S::TabComingSites));
        break;
    }
    case 4: { // Indirme
        label(T(S::DownloadFormatLabel));
        const int pickFormat = w::Segmented(r, ui, Id("set.dlformat"), Rc(cl, y, cl + 340 * s, y + 38 * s),
                                            { T(S::DownloadFormatCbz), T(S::DownloadFormatFolder) }, set.downloadFormat);
        y += 54.0f * s;
        if (pickFormat >= 0 && pickFormat != set.downloadFormat) {
            set.downloadFormat = pickFormat;
            host.SaveSettings();
        }
        label(T(S::DownloadDirLabel));
        const std::wstring curDl = set.downloadDir.empty() ? paths::Join(paths::DataDir(), L"downloads") : set.downloadDir;
        const D2D1_RECT_F dlBox = Rc(cl, y, cr, y + 40 * s);
        r.FillRound(dlBox, 8.0f * s, theme::Panel, 1.0f);
        r.Text(curDl, RcInset(dlBox, 12 * s, 0), theme::Text, 13.0f * s, Renderer::Align::Left,
               Renderer::Weight::Normal, 1.0f, false, true, true);
        y += 52.0f * s;
        if (w::TextButton(r, ui, Id("set.opendl"), Rc(cl, y, cl + 160 * s, y + 36 * s), T(S::OpenFolderBtn), false)) {
            host.OpenDownloadsFolder();
        }
        y += 52.0f * s;
        note(T(S::TabComingDownloads));
        break;
    }
    case 5: { // Arka plan
        toggle(Id("set.startup"), i18n::Literal(L"Windows başlangıcında çalıştır"), set.startOnLogin, i18n::Literal(L"Oturum açıldığında sistem tepsisinde başlar."));
        toggle(Id("set.tray"), T(S::CloseToTray), set.closeToTray, T(S::CloseToTrayHint));
        toggle(Id("set.bgnotify"), T(S::NotifyNewChapters), set.bgNotify);
        label(T(S::ScanIntervalLabel));
        const int intervals[] = {0,15,30,60,120,180,240,300,360,480,720,1440};
        const float cell = (cr - cl - 12*s) / 4;
        for (int i = 0; i < 12; ++i) {
            auto box = RcWH(cl + (i%4)*(cell+4*s), y+(i/4)*42*s, cell, 36*s);
            const std::wstring label=intervals[i]==0?T(S::Off):intervals[i]==1440?i18n::Literal(L"Günde bir"):intervals[i]<60?std::to_wstring(intervals[i])+i18n::Literal(L" dk"):std::to_wstring(intervals[i]/60)+i18n::Literal(L" saat");
            if (w::TextButton(r,ui,Id("set.scanint",i),box,label,set.bgScanInterval==intervals[i])) {
                set.bgScanInterval=intervals[i]; host.SaveSettings();
            }
        }
        y += 140*s;
        note(i18n::Literal(L"Denetim zamanı saklanır. Süresi geçmişse açılıştan 30 saniye sonra kontrol edilir."));
        if (w::TextButton(r, ui, Id("set.scannow"), Rc(cl, y, cl + 260 * s, y + 38 * s), T(S::ManualScanNow), true)) {
            host.CheckUpdatesNow();
        }
        y += 52.0f * s;
        note(T(S::BackgroundScanNote));
        break;
    }
    case 6: { // Senkron & Yedek
        label(T(S::TabSync));
        if (w::TextButton(r, ui, Id("set.export2"), Rc(cl, y, cl + 240 * s, y + 38 * s), T(S::ExportBackupBtn), true)) {
            host.ExportBackupDialog();
        }
        y += 48.0f * s;
        if (w::TextButton(r, ui, Id("set.import2"), Rc(cl, y, cl + 240 * s, y + 38 * s), T(S::ImportAmr), false)) {
            host.ImportAmrDialog();
        }
        y += 54.0f * s;
        note(T(S::TabComingSync));
        note(T(S::ExportHowTo));
        break;
    }
    case 7: { // Gelismis
        label(T(S::DataFolder));
        const D2D1_RECT_F box = Rc(cl, y, cr, y + 40 * s);
        r.FillRound(box, 8.0f * s, theme::Panel, 1.0f);
        r.Text(paths::DataDir(), RcInset(box, 12 * s, 0), theme::Text, 13.0f * s, Renderer::Align::Left,
               Renderer::Weight::Normal, 1.0f, false, true, true);
        y += 52.0f * s;
        if (w::TextButton(r, ui, Id("set.opendata"), Rc(cl, y, cl + 150 * s, y + 36 * s), T(S::OpenFolderBtn), false)) {
            host.OpenDataFolder();
        }
        y += 52.0f * s;

        // Onbellek yonetimi
        label(T(S::CacheSizeLabel));
        const uint64_t cacheBytes = paths::DirectorySize(paths::CacheDir());
        wchar_t cacheBuf[64];
        swprintf_s(cacheBuf, L"%.1f MB", (double)cacheBytes / (1024.0 * 1024.0));
        kv(T(S::CacheSizeLabel), cacheBuf);
        if (w::TextButton(r, ui, Id("set.purgecache"), Rc(cl, y, cl + 170 * s, y + 36 * s), T(S::PurgeCacheBtn), false)) {
            host.PurgeCache();
        }
        y += 52.0f * s;
        note(paths::IsPortable() ? T(S::PortableOn) : T(S::PortableOff));
        break;
    }
    case 8: { // Hakkinda
        const D2D1_RECT_F ghost = RcWH(cl, y, 56 * s, 56 * s);
        r.FillRound(ghost, 14.0f * s, theme::Accent, 1.0f);
        DrawIcon(r, Icon::Ghost, ghost, 38.0f * s, theme::White);
        r.Text(T(S::AppName), Rc(ghost.right + 16 * s, y + 4 * s, cr, y + 30 * s), theme::Text, 20.0f * s,
               Renderer::Align::Left, Renderer::Weight::Semibold);
        r.Text(T(S::VersionValue), Rc(ghost.right + 16 * s, y + 30 * s, cr, y + 52 * s), theme::TextMuted, 13.5f * s);
        y += 80.0f * s;
        kv(T(S::StackLabel), T(S::StackValue));
        kv(T(S::WebView2Runtime), m_webview2.empty() ? std::wstring(T(S::NotInstalled)) : m_webview2);
        note(T(S::WebView2Note));
        label(T(S::PrivacyLabel));
        note(T(S::PrivacyValue));
        break;
    }
    default: break;
    }
    r.PopClip();
}

} // namespace gm
