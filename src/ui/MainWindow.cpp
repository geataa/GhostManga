#include "ui/MainWindow.h"
#include "core/FileIo.h"
#include "core/Startup.h"
#include "core/I18n.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Utf8.h"
#include "engine/AmrImport.h"
#include "engine/LocalSource.h"
#include "engine/MirrorEngine.h"
#include "engine/ChapterLoader.h"
#include "engine/ArchiveReader.h"
#include "engine/CbzWriter.h"
#include "engine/Scheduler.h"
#include "engine/CoverManager.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "net/Fetch.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

namespace gm {
namespace {

std::wstring SanitizeForPath(const std::wstring& in) {
    std::wstring out = in;
    for (wchar_t& c : out) {
        if (c == L'<' || c == L'>' || c == L':' || c == L'"' || c == L'/' || c == L'\\' || c == L'|' || c == L'?' || c == L'*') {
            c = L'_';
        }
    }
    return out;
}

constexpr UINT WM_TRAY             = WM_APP + 1;
constexpr UINT WM_APP_DECODED      = WM_APP + 2;
constexpr UINT WM_APP_CMD          = WM_APP + 3;
constexpr UINT WM_APP_ONLINE_READY = WM_APP + 4;
constexpr UINT WM_APP_ONLINE_FAIL  = WM_APP + 5;
constexpr UINT WM_APP_NEW_CHAPTERS = WM_APP + 6;
constexpr UINT WM_APP_CHAPTERS_SCANNED = WM_APP + 7;
constexpr UINT WM_APP_PAGE_READY   = WM_APP + 8;
constexpr UINT WM_APP_DISPATCH_UI  = WM_APP + 9;
constexpr UINT WM_APP_COVER_READY  = WM_APP + 10;
constexpr UINT WM_APP_SCAN_FINISHED = WM_APP + 11;
constexpr UINT kTrayId             = 1;

struct OnlineReadyInfo {
    int tabIndex = 0;
    std::wstring cacheDir;
    std::wstring seriesTitle;
    std::wstring chapterTitle;
};

struct PageReadyInfo {
    int tabIndex = 0;
    int pageIndex = 0;
    int totalPages = 0;
    std::wstring pagePath;
};

struct ChapterScanResult {
    int tabIndex = 0;
    std::string mangaUrl;
    std::vector<OnlineChapter> chapters;
    int targetIndex = 0;
};

enum TrayCmd : UINT { kCmdShow = 1, kCmdSettings = 2, kCmdQuit = 3 };

UINT DpiFor(HWND hwnd) {
    const UINT d = hwnd ? GetDpiForWindow(hwnd) : GetDpiForSystem();
    return d ? d : 96;
}

void SetCornerPreference(HWND hwnd, bool round) {
    const DWORD attr = 33;              // DWMWA_WINDOW_CORNER_PREFERENCE
    const DWORD pref = round ? 2u : 1u; // DWMWCP_ROUND : DWMWCP_DONOTROUND
    DwmSetWindowAttribute(hwnd, attr, &pref, sizeof(pref));
}

void SetDarkFrame(HWND hwnd) {
    const BOOL on = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &on, sizeof(on));
}

float EaseOut(float t) {
    const float u = 1.0f - std::clamp(t, 0.0f, 1.0f);
    return 1.0f - u * u * u;
}

double ParseChapterNumber(const std::string& str) {
    return ChapterNumber(str);
}

std::wstring CleanChapterTitle(const std::wstring& rawTitle, int fallbackIdx = -1) {
    std::wstring t = rawTitle;
    while (!t.empty() && (t.front() == L' ' || t.front() == L'\t' || t.front() == L'\r' || t.front() == L'\n')) t.erase(t.begin());
    while (!t.empty() && (t.back() == L' ' || t.back() == L'\t' || t.back() == L'\r' || t.back() == L'\n')) t.pop_back();

    while (!t.empty() && (t.back() == L'-' || t.back() == L':' || t.back() == L'—' || t.back() == L' ' || t.back() == L'|')) {
        t.pop_back();
        while (!t.empty() && t.back() == L' ') t.pop_back();
    }

    if (t.empty()) {
        if (fallbackIdx >= 0) return i18n::Literal(L"Bölüm ") + std::to_wstring(fallbackIdx + 1);
        return L"";
    }

    bool onlyNum = true;
    for (wchar_t c : t) {
        if (!iswdigit(c) && c != L'.' && c != L' ' && c != L'#') { onlyNum = false; break; }
    }
    if (onlyNum) {
        while (!t.empty() && t.front() == L'#') t.erase(t.begin());
        while (!t.empty() && t.front() == L' ') t.erase(t.begin());
        return i18n::Literal(L"Bölüm ") + t;
    }

    if (t.rfind(L"Chapter ", 0) == 0) t = i18n::Literal(L"Bölüm ") + t.substr(8);
    else if (t.rfind(L"chapter ", 0) == 0) t = i18n::Literal(L"Bölüm ") + t.substr(8);
    else if (t.rfind(L"Ch. ", 0) == 0) t = i18n::Literal(L"Bölüm ") + t.substr(4);
    else if (t.rfind(L"Ch.", 0) == 0) t = i18n::Literal(L"Bölüm ") + t.substr(3);

    // Eger bolum basliginda tekrarlanan bolum numarasi varsa veya ozel bir baslik yoksa (or. "Bolum 14 : Bolum 14" veya "Bolum 14 - 14"):
    size_t sep = t.find_first_of(L":-—|");
    if (sep != std::wstring::npos) {
        std::wstring leftPart = t.substr(0, sep);
        std::wstring rightPart = t.substr(sep + 1);
        while (!leftPart.empty() && leftPart.back() == L' ') leftPart.pop_back();
        while (!rightPart.empty() && rightPart.front() == L' ') rightPart.erase(rightPart.begin());
        while (!rightPart.empty() && rightPart.back() == L' ') rightPart.pop_back();

        if (rightPart.empty()) {
            t = leftPart;
        } else {
            std::wstring normRight = rightPart;
            if (normRight.rfind(L"Chapter ", 0) == 0) normRight = i18n::Literal(L"Bölüm ") + normRight.substr(8);
            else if (normRight.rfind(L"Ch. ", 0) == 0) normRight = i18n::Literal(L"Bölüm ") + normRight.substr(4);
            else if (normRight.rfind(L"Ch.", 0) == 0) normRight = i18n::Literal(L"Bölüm ") + normRight.substr(3);

            bool rightOnlyNum = true;
            for (wchar_t c : rightPart) {
                if (!iswdigit(c) && c != L'.' && c != L' ') { rightOnlyNum = false; break; }
            }

            if (normRight == leftPart || rightOnlyNum) {
                t = leftPart;
            }
        }
    }

    return t;
}

std::wstring FormatTabTitle(const std::string& mirror, const std::wstring& mangaTitle, const std::wstring& rawChapterTitle, int chapterIdx = -1) {
    std::wstring mPrefix = mirror.empty() ? L"" : L"[" + FromUtf8(mirror) + L"] ";
    std::wstring cleanCh = CleanChapterTitle(rawChapterTitle, chapterIdx);
    std::wstring mTitle = mangaTitle.empty() ? L"Manga" : mangaTitle;
    if (cleanCh.empty()) return mPrefix + mTitle;
    return mPrefix + mTitle + L" - " + cleanCh;
}

} // namespace


UINT MainWindow::ActivateMessage() {
    static const UINT msg = RegisterWindowMessageW(L"GhostManga.Activate");
    return msg;
}

double MainWindow::Now() const {
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)m_qpcFreq.QuadPart;
}

// ------------------------------------------------------------ olusturma -----

bool MainWindow::Create(HINSTANCE inst, const std::wstring& openPath, std::wstring* err) {
    m_inst = inst;
    QueryPerformanceFrequency(&m_qpcFreq);

    m_set.Load(paths::Join(paths::DataDir(), L"settings.ini"));
    if (m_set.filePath.empty()) m_set.filePath = paths::Join(paths::DataDir(), L"settings.ini");
    m_set.startOnLogin = startup::Enabled();
    i18n::Set(m_set.lang);
    m_lib.Load(paths::Join(paths::DataDir(), L"library.json"));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &MainWindow::WndProcStatic;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) {
        if (err) *err = L"Pencere sinifi kaydedilemedi.";
        return false;
    }

    const UINT sysDpi = DpiFor(nullptr);
    const float s = sysDpi / 96.0f;
    // WS_EX_NOREDIRECTIONBITMAP: GDI yonlendirme yuzeyi yok, tek icerik
    // DirectComposition gorseli. Okuyucunun yari saydam zemini ancak boyle
    // gercekten masaustunu gosterir (GhostView ile ayni).
    m_hwnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP, kClassName, T(S::AppName), WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, (int)(1280 * s), (int)(840 * s),
                             nullptr, nullptr, inst, this);
    if (!m_hwnd) {
        if (err) *err = L"Pencere olusturulamadi.";
        return false;
    }
    m_dpi = DpiFor(m_hwnd);
    SetPropW(m_hwnd, kInstanceProp, (HANDLE)(uintptr_t)paths::InstanceHash());
    SetCornerPreference(m_hwnd, true);
    SetDarkFrame(m_hwnd);
    UpdateIcons();
    // Not: ChangeWindowMessageFilterEx ile WM_COPYDATA/WM_DROPFILES'i dusuk
    // butunluklu sureclere acmiyoruz; ayni yetkideki Gezgin ve ikinci ornek
    // zaten ulasabiliyor.
    DragAcceptFiles(m_hwnd, TRUE);

    // Cerceve olculeri olusturma aninda onbelleklenir; WM_NCCALCSIZE'in etkili
    // olmasi icin SWP_FRAMECHANGED sart (FullTerminal dersi).
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (!m_r.Init(m_hwnd, m_dpi, err)) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return false;
    }

    m_reader.Init(this, &m_set);
    m_browserView.Init(m_hwnd, this);
    BrowserView::CleanOldBrowserCache(3);

    // Sekme 0 her zaman Kütüphanedir (pinli, kapatılamaz)
    AppTab libTab;
    libTab.type = TabType::Library;
    libTab.title = T(S::Library);
    libTab.canClose = false;
    m_tabs.push_back(std::move(libTab));
    m_activeTab = 0;

    LoadSessionTabs();

    m_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    AddTray();
    ApplySavedPlacement();

    m_pendingOpen = openPath;
    m_lastTick = Now();
    m_alive = true;

    // Arka plan guncelleme tarayicisi (M7)
    if (m_set.bgScanInterval > 0) {
        engine::Scheduler::Instance().Start(
            [this](const engine::UpdateNotification& notif) {
                if (m_hwnd) {
                    auto* p = new engine::UpdateNotification(notif);
                    PostMessageW(m_hwnd, WM_APP_NEW_CHAPTERS, 0, reinterpret_cast<LPARAM>(p));
                }
            },
            [this]() {
                if (m_hwnd) {
                    PostMessageW(m_hwnd, WM_APP_SCAN_FINISHED, 0, 0);
                }
            },
            m_set.bgScanInterval
        );
    }

    // Kapak indirme ve onbellek yoneticisi
    engine::CoverManager::Instance().Init([this]() {
        if (m_hwnd) {
            PostMessageW(m_hwnd, WM_APP_COVER_READY, 0, 0);
        }
    });

    std::vector<std::string> coverKeys, coverUrls, coverMirrors;
    coverKeys.reserve(m_lib.All().size());
    coverUrls.reserve(m_lib.All().size());
    coverMirrors.reserve(m_lib.All().size());
    for (const auto& s : m_lib.All()) {
        coverKeys.push_back(s.key);
        coverUrls.push_back(s.url);
        coverMirrors.push_back(s.mirror);
    }
    engine::CoverManager::Instance().QueueBatch(coverKeys, coverUrls, coverMirrors);

    log::Write(L"Pencere hazir. DPI %u, veri: %s", m_dpi, paths::DataDir().c_str());
    return true;
}

void MainWindow::Show(int nCmdShow) {
    if (nCmdShow == SW_HIDE && !m_trayAdded) nCmdShow = SW_SHOWNORMAL;
    if (m_set.winValid && m_set.winMax && nCmdShow != SW_HIDE && nCmdShow != SW_SHOWMINIMIZED && nCmdShow != SW_MINIMIZE)
        nCmdShow = SW_SHOWMAXIMIZED;
    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
    // Kutuphane dosyasinda sorun vardiysa sessizce bos liste gosterme: soyle.
    switch (m_lib.Note()) {
    case Library::LoadNote::RestoredFromBackup: ToastFor(T(S::LibRestoredFromBak), 8.0f); break;
    case Library::LoadNote::ResetCorrupt: ToastFor(T(S::LibResetCorrupt), 8.0f); break;
    case Library::LoadNote::Blocked: ToastFor(T(S::LibBlocked), 8.0f); break;
    default: break;
    }
    if (!m_pendingOpen.empty()) {
        const std::wstring p = m_pendingOpen;
        m_pendingOpen.clear();
        OpenPath(p);
    }
}

void MainWindow::UpdateIcons() {
    // Not: "small" rpcndr.h'de makro, degisken adi olamaz.
    const int bigPx = GetSystemMetricsForDpi(SM_CXICON, m_dpi);
    const int smallPx = GetSystemMetricsForDpi(SM_CXSMICON, m_dpi);
    HICON nb = MakeAppIcon(bigPx), ns = MakeAppIcon(smallPx);
    if (nb) SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)nb);
    if (ns) SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)ns);
    if (m_iconBig) DestroyIcon(m_iconBig);
    if (m_iconSmall) DestroyIcon(m_iconSmall);
    m_iconBig = nb;
    m_iconSmall = ns;
    if (m_trayAdded) {
        NOTIFYICONDATAW nid{ sizeof(nid) };
        nid.hWnd = m_hwnd;
        nid.uID = kTrayId;
        nid.uFlags = NIF_ICON;
        nid.hIcon = m_iconSmall;
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
}

// ------------------------------------------------------------ yerlesim ------

void MainWindow::ApplySavedPlacement() {
    if (!m_set.winValid || m_set.winW < 320 || m_set.winH < 240) return;
    RECT rc{ m_set.winX, m_set.winY, m_set.winX + m_set.winW, m_set.winY + m_set.winH };
    // Kaydedilen yer artik hicbir monitorde degilse (monitor cikarildi) kullanma.
    if (!MonitorFromRect(&rc, MONITOR_DEFAULTTONULL)) return;
    WINDOWPLACEMENT wp{ sizeof(wp) };
    GetWindowPlacement(m_hwnd, &wp);
    wp.rcNormalPosition = rc;
    wp.showCmd = SW_HIDE;
    SetWindowPlacement(m_hwnd, &wp);
}

void MainWindow::RememberPlacement() {
    if (!m_hwnd) return;
    WINDOWPLACEMENT wp{ sizeof(wp) };
    if (m_fullscreen) wp = m_prevPlacement;
    else if (!GetWindowPlacement(m_hwnd, &wp)) return;
    const RECT& r = wp.rcNormalPosition;
    m_set.winValid = true;
    m_set.winX = r.left;
    m_set.winY = r.top;
    m_set.winW = r.right - r.left;
    m_set.winH = r.bottom - r.top;
    m_set.winMax = wp.showCmd == SW_SHOWMAXIMIZED || (wp.flags & WPF_RESTORETOMAXIMIZED) != 0;
    if (!m_fullscreen && IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd)) m_set.winMax = IsZoomed(m_hwnd) != FALSE;
}

void MainWindow::ToggleFullscreen() { Post(Cmd::ToggleFullscreen); }

void MainWindow::Post(Cmd c) {
    if (m_hwnd) PostMessageW(m_hwnd, WM_APP_CMD, (WPARAM)c, 0);
}

void MainWindow::RunCommand(Cmd c) {
    if (!m_alive) return;
    switch (c) {
    case Cmd::OpenFolder: RunFolderDialog(); break;
    case Cmd::OpenArchive: RunArchiveDialog(); break;
    case Cmd::ImportAmr: RunImportDialog(); break;
    case Cmd::ExportBackup: RunExportDialog(); break;
    case Cmd::OpenDataFolder:
        ShellExecuteW(m_hwnd, L"open", paths::DataDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case Cmd::OpenDownloadsFolder: {
        std::wstring d = m_set.downloadDir.empty() ? paths::Join(paths::DataDir(), L"downloads") : m_set.downloadDir;
        paths::EnsureDir(d);
        ShellExecuteW(m_hwnd, L"open", d.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    }
    case Cmd::OpenUrl: {
        std::wstring url;
        url.swap(m_pendingUrl);
        if (!url.empty()) ShellExecuteW(m_hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    }
    case Cmd::ToggleFullscreen: DoToggleFullscreen(); break;
    case Cmd::Minimize:
        if (m_fullscreen) DoToggleFullscreen();
        ShowWindow(m_hwnd, SW_MINIMIZE);
        break;
    case Cmd::Maximize:
        if (m_fullscreen) DoToggleFullscreen();
        ShowWindow(m_hwnd, IsZoomed(m_hwnd) ? SW_RESTORE : SW_MAXIMIZE);
        m_dirty = true;
        break;
    case Cmd::HideToTray: HideToTray(); break;
    }
}

void MainWindow::DoToggleFullscreen() {
    if (!m_fullscreen) {
        m_prevPlacement.length = sizeof(m_prevPlacement);
        GetWindowPlacement(m_hwnd, &m_prevPlacement);
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        m_prevStyle = GetWindowLongPtrW(m_hwnd, GWL_STYLE);
        m_fullscreen = true;
        SetCornerPreference(m_hwnd, false);
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, (m_prevStyle & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
        SetWindowPos(m_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
    } else {
        m_fullscreen = false;
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, m_prevStyle ? m_prevStyle : (WS_OVERLAPPEDWINDOW | WS_VISIBLE));
        SetCornerPreference(m_hwnd, true);
        SetWindowPlacement(m_hwnd, &m_prevPlacement);
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }
    m_dirty = true;
}

// ------------------------------------------------------------ tepsi ---------

void MainWindow::AddTray() {
    NOTIFYICONDATAW nid{ sizeof(nid) };
    nid.hWnd = m_hwnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = m_iconSmall;
    wcsncpy_s(nid.szTip, T(S::AppName), _TRUNCATE);
    m_trayAdded = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
}

void MainWindow::RemoveTray() {
    if (!m_trayAdded) return;
    NOTIFYICONDATAW nid{ sizeof(nid) };
    nid.hWnd = m_hwnd;
    nid.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    m_trayAdded = false;
}

void MainWindow::HideToTray() {
    RememberPlacement();
    SaveSettings();
    if (!m_trayAdded) AddTray();
    if (!m_trayAdded) {
        // Tepsi simgesi yoksa gizlenen pencereye bir daha ulasilamaz: kucult.
        ShowWindow(m_hwnd, SW_MINIMIZE);
        return;
    }
    ShowWindow(m_hwnd, SW_HIDE);
    if (!m_set.trayHintShown && m_trayAdded) {
        NOTIFYICONDATAW nid{ sizeof(nid) };
        nid.hWnd = m_hwnd;
        nid.uID = kTrayId;
        nid.uFlags = NIF_INFO;
        nid.dwInfoFlags = NIIF_INFO;
        wcsncpy_s(nid.szInfoTitle, T(S::AppName), _TRUNCATE);
        wcsncpy_s(nid.szInfo, T(S::TrayStillRunning), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &nid);
        m_set.trayHintShown = true;
        SaveSettings();
    }
    log::Write(L"Tepsiye indi.");
}

void MainWindow::RestoreFromTray() {
    if (IsIconic(m_hwnd)) ShowWindow(m_hwnd, SW_RESTORE);
    else ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);
    m_lastTick = Now();
    m_dirty = true;
}

void MainWindow::ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, kCmdShow, T(S::TrayShow));
    AppendMenuW(menu, MF_STRING, kCmdSettings, T(S::TraySettings));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdQuit, T(S::TrayQuit));
    SetMenuDefaultItem(menu, kCmdShow, FALSE);
    POINT pt{};
    GetCursorPos(&pt);
    // Menunun disina tiklaninca kapanmasi icin pencere on planda olmali.
    SetForegroundWindow(m_hwnd);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, m_hwnd, nullptr);
    PostMessageW(m_hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
    switch (cmd) {
    case kCmdShow: RestoreFromTray(); break;
    case kCmdSettings:
        RestoreFromTray();
        SetView(View::Settings);
        break;
    case kCmdQuit: QuitApp(); break;
    default: break;
    }
}

void MainWindow::QuitApp() {
    m_quitting = true;
    for (auto& tab : m_tabs) if (tab.downloadCancel) tab.downloadCancel->store(true);
    m_library.StopWorkers();
    for (auto& worker : m_ownedWorkers) if (worker.joinable()) worker.join();
    m_ownedWorkers.clear();
    engine::Scheduler::Instance().Stop();
    engine::CoverManager::Instance().Shutdown();
    RememberPlacement();
    SaveSettings();
    SaveSessionTabs();
    DestroyWindow(m_hwnd);
}

// ------------------------------------------------------------ gorunumler ----

void MainWindow::SetView(View v) {
    if (v == View::Settings && m_view != View::Settings) m_viewBeforeSettings = m_view;
    if (v == View::Browser && m_view != View::Browser) m_viewBeforeSettings = m_view;
    if (v == View::Library && m_view != View::Library) m_library.RecheckRecent();
    m_browserView.Show(v == View::Browser);
    m_view = v;
    m_drawerOpen = false;
    m_dirty = true;
}

void MainWindow::Back() {
    if (m_drawerOpen) { m_drawerOpen = false; m_dirty = true; return; }
    if (m_view == View::Browser) { CloseBrowser(); return; }
    if (m_view == View::Reader && ActiveReader()->ClosePopups()) { m_dirty = true; return; }
    if (m_fullscreen) { DoToggleFullscreen(); return; }
    if (m_view == View::Reader) { ActivateTab(0); return; }
    if (m_view == View::Settings) {
        SetView(m_viewBeforeSettings == View::Reader && ActiveReader()->IsOpen() ? View::Reader : View::Library);
    }
}

void MainWindow::OpenFolderDialog() { Post(Cmd::OpenFolder); }
void MainWindow::OpenArchiveDialog() { Post(Cmd::OpenArchive); }
void MainWindow::ImportAmrDialog() { Post(Cmd::ImportAmr); }
void MainWindow::ExportBackupDialog() { Post(Cmd::ExportBackup); }
void MainWindow::OpenDataFolder() { Post(Cmd::OpenDataFolder); }
void MainWindow::OpenDownloadsFolder() { Post(Cmd::OpenDownloadsFolder); }

void MainWindow::PurgeCache() {
    const std::wstring cache = paths::CacheDir();
    uint64_t bytes = paths::DirectorySize(cache);
    uint32_t mb = static_cast<uint32_t>(bytes / (1024 * 1024));
    paths::ClearDirectoryContents(cache);
    wchar_t buf[128];
    swprintf_s(buf, T(S::CachePurged), mb);
    ToastFor(buf, 4.0f);
    m_dirty = true;
}

void MainWindow::CheckUpdatesNow() {
    if (engine::Scheduler::Instance().IsChecking()) {
        ToastFor(i18n::Literal(L"Mangalar zaten denetleniyor..."), 2.5f);
        return;
    }
    if (!engine::Scheduler::Instance().IsRunning()) {
        engine::Scheduler::Instance().Start(
            [this](const engine::UpdateNotification& notif) {
                if (m_hwnd) {
                    auto* p = new engine::UpdateNotification(notif);
                    PostMessageW(m_hwnd, WM_APP_NEW_CHAPTERS, 0, reinterpret_cast<LPARAM>(p));
                }
            },
            [this]() {
                if (m_hwnd) {
                    PostMessageW(m_hwnd, WM_APP_SCAN_FINISHED, 0, 0);
                }
            },
            m_set.bgScanInterval
        );
    }
    ToastFor(T(S::ManualScanTriggered), 3.0f);
    engine::Scheduler::Instance().TriggerCheckNow();
}

void MainWindow::OpenSitesCatalog() {
    SetView(View::Library);
    m_library.SetTab(3); // kSites
    m_dirty = true;
}

void MainWindow::OpenUrl(const std::wstring& url) {
    // Yalniz web adresi: ice aktarilan dosyadaki bir deger ShellExecute ile
    // program ya da yerel dosya calistirmasin.
    if (_wcsnicmp(url.c_str(), L"https://", 8) != 0 && _wcsnicmp(url.c_str(), L"http://", 7) != 0) {
        Toast(T(S::BadUrl));
        return;
    }
    // Cift tiklamanin ikinci tiklamasi ayni bolumu ikinci sekmede acmasin.
    const ULONGLONG now = GetTickCount64();
    if (url == m_lastUrl && now - m_lastUrlAt < GetDoubleClickTime()) return;
    m_lastUrl = url;
    m_lastUrlAt = now;
    m_pendingUrl = url;
    Post(Cmd::OpenUrl);
}

bool MainWindow::LibraryVisible() const {
    return m_view == View::Library || (m_view == View::Reader && !ActiveReader()->IsOpen());
}

void MainWindow::RunImportDialog() {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
        return;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
    const COMDLG_FILTERSPEC types[] = {
        { T(S::AmrFileFilter), L"*.json" },
        { T(S::AllFilesFilter), L"*.*" },
    };
    dlg->SetFileTypes((UINT)std::size(types), types);
    dlg->SetFileTypeIndex(1);
    dlg->SetTitle(T(S::PickAmrTitle));
    // AMR dosyayi tarayicinin Indirilenler klasorune kaydeder. Varsayilan
    // klasor yalniz ilk acilista kullanilir; sonra Windows son klasoru hatirlar.
    IShellItem* downloads = nullptr;
    if (SUCCEEDED(SHGetKnownFolderItem(FOLDERID_Downloads, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&downloads)))) {
        dlg->SetDefaultFolder(downloads);
        downloads->Release();
    }
    std::wstring picked;
    if (SUCCEEDED(dlg->Show(m_hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                picked = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    // Diyalog acikken tepsiden Cik secilmis olabilir: pencere yok edildiyse dur.
    if (!m_alive) return;
    m_lastTick = Now();
    if (!picked.empty()) ImportAmrFile(picked);
}

void MainWindow::RunExportDialog() {
    IFileSaveDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
        return;
    dlg->SetTitle(T(S::SaveBackupTitle));
    const COMDLG_FILTERSPEC spec[] = {
        { T(S::AmrFileFilter), L"*.json" },
        { T(S::AllFilesFilter), L"*.*" },
    };
    dlg->SetFileTypes((UINT)std::size(spec), spec);
    dlg->SetFileTypeIndex(1);
    dlg->SetDefaultExtension(L"json");

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t defName[64];
    swprintf_s(defName, L"AMR_%04d-%02d-%02d.json", st.wYear, st.wMonth, st.wDay);
    dlg->SetFileName(defName);

    std::wstring picked;
    if (SUCCEEDED(dlg->Show(m_hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                picked = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    if (!m_alive || picked.empty()) return;

    if (amr::ExportAmrFile(picked, m_lib)) {
        ToastFor(T(S::ExportSuccess), 4.0f);
    } else {
        ToastFor(T(S::ExportFailed), 4.0f);
    }
}

void MainWindow::ImportAmrFile(const std::wstring& path) {
    constexpr float kImportToast = 6.0f;   // ozet ve hata metni uzun: okunacak kadar kalsin
    amr::ImportData data;
    const amr::ImportError e = amr::LoadExportFile(path, data);
    if (e != amr::ImportError::None) {
        log::Write(L"AMR ice aktarma basarisiz (%d): %s %s", (int)e, path.c_str(), FromUtf8(data.detail).c_str());
        switch (e) {
        case amr::ImportError::TooBig: ToastFor(T(S::ImportErrTooBig), kImportToast); break;
        case amr::ImportError::BadJson: {
            // Ayrinti (Turkce, ASCII) yalniz gunlukte; kullaniciya dilinde konum.
            wchar_t buf[200];
            swprintf_s(buf, T(S::ImportErrJsonFmt), data.errLine, data.errCol);
            ToastFor(buf, kImportToast);
            break;
        }
        case amr::ImportError::NotAmr: ToastFor(T(S::ImportErrNotAmr), kImportToast); break;
        case amr::ImportError::Empty: ToastFor(T(S::ImportErrEmpty), kImportToast); break;
        default: ToastFor(T(S::ImportErrRead), kImportToast); break;
        }
        return;
    }

    const size_t total = data.series.size();
    const int skipped = data.skipped;
    const bool v1 = data.v1;
    // Once kopyada birlestir ve kaydet; yalniz kayit basarirsa asil listeyi
    // degistir. Yoksa ekrandaki liste diskte olmayan serileri gosterir ve
    // uygulama kapaninca sessizce kaybolurdu.
    Library::MergeResult res;
    bool saved = false;
    try {
        Library next = m_lib;
        res = next.Merge(std::move(data.series), std::move(data.bookmarks));
        saved = next.Save();
        if (saved) m_lib = std::move(next);
    } catch (const std::bad_alloc&) {
        saved = false;
    }
    log::Write(L"AMR ice aktarma: %s -> %zu seri (%d yeni, %d guncellendi, %d ayni), %d yeni yer imi, %d atlandi%s%s",
               path.c_str(), total, res.added, res.updated, res.unchanged, res.bookmarksAdded, skipped,
               v1 ? L", V1 bicimi" : L"", saved ? L"" : L", KAYDEDILEMEDI, uygulanmadi");

    if (!saved) {
        ToastFor(T(S::ImportSaveFailed), kImportToast);
        return;
    }
    wchar_t buf[160];
    swprintf_s(buf, T(S::ImportDoneFmt), res.added, res.updated, res.unchanged);
    std::wstring msg = buf;
    if (res.bookmarksAdded > 0) {
        swprintf_s(buf, T(S::ImportBookmarksFmt), res.bookmarksAdded);
        msg += buf;
    }
    if (skipped > 0) {
        swprintf_s(buf, T(S::ImportSkippedFmt), skipped);
        msg += buf;
    }
    SetView(View::Library);
    m_library.ShowReadingList();
    ToastFor(msg, kImportToast);
}

void MainWindow::RunFolderDialog() {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
        return;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(T(S::PickFolderTitle));
    std::wstring picked;
    if (SUCCEEDED(dlg->Show(m_hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                picked = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    // Diyalog acikken tepsiden Cik secilmis olabilir: pencere yok edildiyse dur.
    if (!m_alive) return;
    m_lastTick = Now();
    if (!picked.empty()) OpenPath(picked);
}

void MainWindow::RunArchiveDialog() {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
        return;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
    const COMDLG_FILTERSPEC types[] = {
        { i18n::Literal(L"Çizgi Roman Arşivleri (*.cbz; *.zip)"), L"*.cbz;*.zip" },
        { i18n::Literal(L"Tüm Dosyalar (*.*)"), L"*.*" },
    };
    dlg->SetFileTypes(static_cast<UINT>(std::size(types)), types);
    dlg->SetFileTypeIndex(1);
    dlg->SetTitle(i18n::Literal(L"Çizgi Roman Arşivi Aç"));
    std::wstring picked;
    if (SUCCEEDED(dlg->Show(m_hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                picked = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    if (!m_alive) return;
    m_lastTick = Now();
    if (!picked.empty()) OpenPath(picked);
}

ReaderView* MainWindow::ActiveReader() {
    if (m_activeTab >= 0 && m_activeTab < (int)m_tabs.size()) {
        if (m_tabs[m_activeTab].reader) return m_tabs[m_activeTab].reader.get();
    }
    return &m_reader;
}

const ReaderView* MainWindow::ActiveReader() const {
    if (m_activeTab >= 0 && m_activeTab < (int)m_tabs.size()) {
        if (m_tabs[m_activeTab].reader) return m_tabs[m_activeTab].reader.get();
    }
    return &m_reader;
}

void MainWindow::ActivateTab(int index) {
    if (index < 0 || index >= (int)m_tabs.size()) return;
    m_activeTab = index;
    const auto& tab = m_tabs[index];
    if (tab.type == TabType::Library) {
        m_browserView.Show(false);
        SetView(View::Library);
    } else if (tab.type == TabType::Reader) {
        m_browserView.Show(false);
        SetView(View::Reader);
        if (tab.reader) {
            tab.reader->WakeChrome(Now());
        }
    } else if (tab.type == TabType::Browser) {
        SetView(View::Browser);
        m_browserView.Show(true);
    }
    SaveSessionTabs();
    m_dirty = true;
}

void MainWindow::CloseTab(int index) {
    if (index <= 0 || index >= (int)m_tabs.size()) return; // Sekme 0 Kütüphanedir, kapatılamaz
    if (!m_tabs[index].canClose) return;

    if (m_tabs[index].downloadCancel) {
        m_tabs[index].downloadCancel->store(true);
    }

    if (m_tabs[index].type == TabType::Browser) {
        m_browserView.Show(false);
    }
    if (m_tabs[index].reader) {
        m_tabs[index].reader->Shutdown();
    }

    m_tabs.erase(m_tabs.begin() + index);

    if (m_activeTab >= (int)m_tabs.size()) {
        m_activeTab = (int)m_tabs.size() - 1;
    } else if (m_activeTab == index) {
        m_activeTab = std::max(0, index - 1);
    } else if (m_activeTab > index) {
        m_activeTab--;
    }

    ActivateTab(m_activeTab);
    SaveSessionTabs();
    m_dirty = true;
}

void MainWindow::SaveSessionTabs() {
    json::Value root = json::Value::Obj();
    root.Add("activeTab", json::Value::Num(m_activeTab));

    json::Value tabsArr = json::Value::Arr();
    for (size_t i = 1; i < m_tabs.size(); ++i) {
        const auto& tab = m_tabs[i];
        json::Value t = json::Value::Obj();
        t.Add("type", json::Value::Str(tab.type == TabType::Reader ? "reader" : "browser"));
        t.Add("title", json::Value::Str(ToUtf8(tab.title)));
        t.Add("mangaTitle", json::Value::Str(ToUtf8(tab.mangaTitle)));
        t.Add("chapterTitle", json::Value::Str(ToUtf8(tab.chapterTitle)));
        t.Add("mirror", json::Value::Str(tab.mirror));
        t.Add("mangaUrl", json::Value::Str(tab.mangaUrl));
        t.Add("chapterUrl", json::Value::Str(tab.chapterUrl));
        t.Add("chapterDir", json::Value::Str(ToUtf8(tab.chapterDir)));
        t.Add("chapterIndex", json::Value::Num(tab.chapterIndex));
        int curPage = tab.reader ? tab.reader->CurrentPageIndex() : 0;
        t.Add("page", json::Value::Num(curPage));
        tabsArr.Push(std::move(t));
    }
    root.Add("tabs", std::move(tabsArr));

    std::string out = json::Dump(root);
    fileio::WriteAtomic(paths::Join(paths::DataDir(), L"session_tabs.json"), out);
}

void MainWindow::LoadSessionTabs() {
    std::wstring path = paths::Join(paths::DataDir(), L"session_tabs.json");
    std::string content;
    if (fileio::ReadText(path, 4 * 1024 * 1024, content) != fileio::ReadResult::Ok || content.empty()) return;

    json::Value root;
    std::string err;
    if (!json::Parse(content, root, &err) || !root.IsObject()) return;

    int savedActive = (int)(root.Get("activeTab") ? root.Get("activeTab")->AsInt(0) : 0);

    const auto* tabsArr = root.Get("tabs");
    if (tabsArr && tabsArr->IsArray()) {
        for (const auto& item : tabsArr->arr) {
            if (!item.IsObject()) continue;
            std::string tType = item.Get("type") ? item.Get("type")->AsString() : "reader";
            if (tType == "browser") continue; // Web gorunumu sekmeleri kullaniciya acilmaz

            AppTab tab;
            tab.type = TabType::Reader;
            tab.title = FromUtf8(item.Get("title") ? item.Get("title")->AsString() : "");
            tab.mangaTitle = FromUtf8(item.Get("mangaTitle") ? item.Get("mangaTitle")->AsString() : "");
            tab.chapterTitle = FromUtf8(item.Get("chapterTitle") ? item.Get("chapterTitle")->AsString() : "");
            tab.mirror = item.Get("mirror") ? item.Get("mirror")->AsString() : "";
            tab.mangaUrl = item.Get("mangaUrl") ? item.Get("mangaUrl")->AsString() : "";
            tab.chapterUrl = item.Get("chapterUrl") ? item.Get("chapterUrl")->AsString() : "";
            tab.chapterDir = FromUtf8(item.Get("chapterDir") ? item.Get("chapterDir")->AsString() : "");
            tab.chapterIndex = (int)(item.Get("chapterIndex") ? item.Get("chapterIndex")->AsInt(-1) : -1);
            int page = (int)(item.Get("page") ? item.Get("page")->AsInt(0) : 0);
            tab.canClose = true;

            tab.reader = std::make_unique<ReaderView>();
            tab.reader->Init(this, &m_set);
            tab.reader->SetSourceMirror(FromUtf8(tab.mirror));
            tab.title = FormatTabTitle(tab.mirror, tab.mangaTitle, tab.chapterTitle, tab.chapterIndex);

            std::string sKey = amr::MangaKey(tab.mirror, tab.mangaUrl, "en");
            const Series* foundSeries = m_lib.FindSeries(sKey);
            if (!foundSeries && !tab.mangaUrl.empty()) foundSeries = m_lib.FindSeries(tab.mangaUrl);
            if (foundSeries) {
                bool hasCustom = foundSeries->hasCustomReaderSettings || (foundSeries->webtoon != 0) || (foundSeries->zoom > 0.05);
                tab.reader->SetMangaSettings(foundSeries->key, hasCustom, foundSeries->webtoon != 0,
                                            (FitMode)foundSeries->fitMode, foundSeries->doublePage != 0, foundSeries->rtl != 0,
                                            (float)foundSeries->zoom);
            } else {
                tab.reader->SetMangaSettings(sKey, false, m_set.webtoon, m_set.fit, m_set.doublePage, m_set.rtl, 1.0f);
            }

            if (!tab.chapterDir.empty() && GetFileAttributesW(tab.chapterDir.c_str()) != INVALID_FILE_ATTRIBUTES) {
                if (tab.reader->Open(tab.chapterDir)) {
                    tab.reader->SetCustomTitle(tab.mangaTitle, CleanChapterTitle(tab.chapterTitle, tab.chapterIndex));
                    if (page > 0) tab.reader->GoToPage(page);
                }
            }

            if (!tab.mirror.empty() && !tab.mangaUrl.empty()) {
                int tidx = (int)m_tabs.size();
                ScanChaptersForTab(tidx, tab.mirror, tab.mangaUrl, tab.chapterUrl);
                if (!tab.reader->IsOpen() || !engine::ChapterLoader::Instance().IsCached(tab.chapterDir)) {
                    tab.loading = true;
                    tab.statusMessage = i18n::Literal(L"⚡ Bölüm hazırlanıyor...");
                    std::string mirror = tab.mirror;
                    std::string mangaUrl = tab.mangaUrl;
                    std::string chapUrl = tab.chapterUrl;
                    std::wstring title = tab.mangaTitle;
                    std::wstring chTitle = tab.chapterTitle.empty() ? paths::FileName(FromUtf8(chapUrl)) : tab.chapterTitle;

                    std::wstring cacheDir = tab.chapterDir;
                    if (cacheDir.empty() && !chapUrl.empty()) {
                        cacheDir = engine::ChapterLoader::Instance().GetCacheDir(mirror, mangaUrl, chapUrl);
                        tab.chapterDir = cacheDir;
                    }
                    if (!tab.reader->IsOpen()) {
                        tab.reader->OpenProgressive(cacheDir, title, chTitle, 1);
                    }

                    if (!chapUrl.empty()) {
                        tab.downloadCancel = std::make_shared<std::atomic<bool>>(false);
                        auto cancelToken = tab.downloadCancel;
                        m_ownedWorkers.emplace_back([this, tidx, mirror, mangaUrl, chapUrl, title, chTitle, cacheDir, cancelToken]() {
                            auto onPageReady = [this, tidx](int pageIndex, int totalPages, const std::wstring& pagePath) {
                                auto* pProg = new PageReadyInfo{ tidx, pageIndex, totalPages, pagePath };
                                PostMessageW(m_hwnd, WM_APP_PAGE_READY, 0, reinterpret_cast<LPARAM>(pProg));
                            };
                            bool ok = engine::ChapterLoader::Instance().DownloadChapter(mirror, chapUrl, cacheDir, onPageReady,
                                [cancelToken]() { return cancelToken ? cancelToken->load() : false; });
                            if (ok) {
                                auto* pInfo = new OnlineReadyInfo{ tidx, cacheDir, title, chTitle };
                                PostMessageW(m_hwnd, WM_APP_ONLINE_READY, 0, reinterpret_cast<LPARAM>(pInfo));
                            } else {
                                if (cancelToken && cancelToken->load()) return;
                                m_browserView.CrawlChapterImages(tidx, chapUrl, title, chTitle, [this, tidx, title, chTitle](const BrowserView::ImageCrawlResult& cRes) {
                                    if (cRes.success) {
                                        RunOnUIThread([this, tidx, cRes, title, chTitle]() {
                                            if (tidx >= 0 && tidx < (int)m_tabs.size() && m_tabs[tidx].reader) {
                                                m_tabs[tidx].chapterDir = cRes.cacheDir;
                                                m_tabs[tidx].reader->OpenProgressive(cRes.cacheDir, title, chTitle, cRes.pageCount);
                                            }
                                            m_dirty = true;
                                        });
                                    } else {
                                        PostMessageW(m_hwnd, WM_APP_ONLINE_FAIL, (WPARAM)tidx, 0);
                                    }
                                });
                            }
                        });
                    }
                }
            }
            m_tabs.push_back(std::move(tab));
        }
    }

    if (savedActive >= 0 && savedActive < (int)m_tabs.size()) {
        m_activeTab = savedActive;
    } else {
        m_activeTab = 0;
    }
    ActivateTab(m_activeTab);
}

void MainWindow::ScanChaptersForTab(int tabIndex, const std::string& mirror, const std::string& mangaUrl, const std::string& targetChapterUrl) {
    if (mangaUrl.empty()) return;
    std::string catalogueKey = amr::MangaKey(mirror, mangaUrl, "en");
    std::string catalogueLanguage = "en";
    if (const auto* series = m_lib.FindSeries(catalogueKey)) { catalogueKey = series->key; catalogueLanguage = series->language; }
    else if (const auto* series = m_lib.FindSeries(mangaUrl)) { catalogueKey = series->key; catalogueLanguage = series->language; }
    m_ownedWorkers.emplace_back([this, tabIndex, mirror, mangaUrl, targetChapterUrl, catalogueKey, catalogueLanguage]() {
        std::string key = catalogueKey;

        // 1. Once varsa diskteki onbellekten aninda yukle
        std::vector<std::pair<std::string, std::string>> cached;
        if (amr::LoadCachedChapterList(key, cached) && !cached.empty()) {
            std::vector<OnlineChapter> converted;
            converted.reserve(cached.size());
            int targetIdx = 0;
            for (size_t i = 0; i < cached.size(); ++i) {
                converted.push_back({ cached[i].first, cached[i].second });
                if (!targetChapterUrl.empty() && cached[i].second == targetChapterUrl) {
                    targetIdx = (int)i;
                }
            }
            auto* res = new ChapterScanResult{ tabIndex, mangaUrl, std::move(converted), targetIdx };
            PostMessageW(m_hwnd, WM_APP_CHAPTERS_SCANNED, 0, reinterpret_cast<LPARAM>(res));
        }

        std::vector<engine::ChapterInfo> chapters;
        if (!mirror.empty()) {
            chapters = engine::MirrorEngine::Instance().GetChapters(mirror, mangaUrl, catalogueLanguage);
        }
        if (!chapters.empty()) {
            std::vector<OnlineChapter> converted;
            converted.reserve(chapters.size());
            std::vector<std::pair<std::string, std::string>> pairs;
            pairs.reserve(chapters.size());
            int targetIdx = 0;
            for (size_t i = 0; i < chapters.size(); ++i) {
                converted.push_back({ chapters[i].title, chapters[i].url });
                pairs.push_back({ chapters[i].title, chapters[i].url });
                if (!targetChapterUrl.empty() && chapters[i].url == targetChapterUrl) {
                    targetIdx = (int)i;
                }
            }
            amr::SaveCachedChapterList(key, pairs);
            auto* res = new ChapterScanResult{ tabIndex, mangaUrl, std::move(converted), targetIdx };
            PostMessageW(m_hwnd, WM_APP_CHAPTERS_SCANNED, 0, reinterpret_cast<LPARAM>(res));
            return;
        }

        log::Write(L"[ScanChaptersForTab] MirrorEngine returned empty, falling back to crawler: %s", FromUtf8(mangaUrl).c_str());
        m_browserView.CrawlMangaChapters(mangaUrl, [this, tabIndex, key, mangaUrl, targetChapterUrl](const BrowserView::ChapterCrawlResult& cRes) {
            if (cRes.success && !cRes.chapters.empty()) {
                std::vector<OnlineChapter> converted;
                converted.reserve(cRes.chapters.size());
                std::vector<std::pair<std::string, std::string>> pairs;
                pairs.reserve(cRes.chapters.size());
                int targetIdx = 0;
                for (size_t i = 0; i < cRes.chapters.size(); ++i) {
                    converted.push_back({ cRes.chapters[i].title, cRes.chapters[i].url });
                    pairs.push_back({ cRes.chapters[i].title, cRes.chapters[i].url });
                    if (!targetChapterUrl.empty() && cRes.chapters[i].url == targetChapterUrl) {
                        targetIdx = (int)i;
                    }
                }
                amr::SaveCachedChapterList(key, pairs);
                auto* res = new ChapterScanResult{ tabIndex, mangaUrl, std::move(converted), targetIdx };
                PostMessageW(m_hwnd, WM_APP_CHAPTERS_SCANNED, 0, reinterpret_cast<LPARAM>(res));
            }
        });
    });
}

void MainWindow::OpenPath(const std::wstring& input) {
    std::wstring dir = input;
    const DWORD attr = GetFileAttributesW(dir.c_str());
    const bool isDir = attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
    if (!isDir && _wcsicmp(PathFindExtensionW(dir.c_str()), L".json") == 0) {
        ImportAmrFile(dir);
        return;
    }
    if (!isDir && engine::ArchiveReader::IsArchive(dir)) {
        ToastFor(i18n::Literal(L"Arşiv açılıyor..."), 2.0f);
        std::wstring cacheDir = engine::ArchiveReader::PrepareArchiveForReading(dir);
        if (cacheDir.empty()) {
            Toast(T(S::NoImages));
            return;
        }
        std::wstring baseName = paths::FileName(dir);
        size_t dot = baseName.rfind(L'.');
        if (dot != std::wstring::npos) baseName = baseName.substr(0, dot);

        auto archiveInfo=local::Describe(dir,m_lib);
        archiveInfo.originalPath=dir;archiveInfo.path=cacheDir;archiveInfo.archive=false;
        local::StoreInfo(archiveInfo);
        AppTab newTab;
        newTab.type = TabType::Reader;
        newTab.title = archiveInfo.seriesTitle + L" · " + archiveInfo.chapterTitle;
        newTab.mangaTitle = archiveInfo.seriesTitle;
        newTab.chapterTitle = archiveInfo.chapterTitle;
        newTab.chapterDir = cacheDir;
        newTab.canClose = true;
        newTab.reader = std::make_unique<ReaderView>();
        newTab.reader->Init(this, &m_set);
        if (newTab.reader->Open(cacheDir)) {
            newTab.reader->SetCustomTitle(archiveInfo.seriesTitle, archiveInfo.chapterTitle);
            if(!archiveInfo.seriesKey.empty() && !archiveInfo.chapterUrl.empty())m_lib.MarkLastRead(archiveInfo.seriesKey,archiveInfo.chapterUrl,ToUtf8(archiveInfo.chapterTitle));
            m_tabs.push_back(std::move(newTab));
            ActivateTab((int)m_tabs.size() - 1);
            SaveSessionTabs();
        } else {
            Toast(T(S::NoImages));
        }
        return;
    }
    if (attr == INVALID_FILE_ATTRIBUTES) {
        Toast(T(S::NoImages));
        return;
    }
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) dir = paths::ParentDir(dir);
    const std::wstring chapter = local::ResolveChapterDir(dir);
    if (chapter.empty()) {
        Toast(T(S::NoImages));
        return;
    }

    for (size_t i = 1; i < m_tabs.size(); ++i) {
        if (m_tabs[i].type == TabType::Reader && m_tabs[i].chapterDir == chapter) {
            ActivateTab((int)i);
            return;
        }
    }

    const auto localInfo = local::Describe(chapter, m_lib);
    std::wstring folderName = localInfo.seriesTitle + L" · " + localInfo.chapterTitle;
    AppTab newTab;
    newTab.type = TabType::Reader;
    newTab.title = folderName.empty() ? i18n::Literal(L"Yerel Manga") : folderName;
    newTab.mangaTitle = localInfo.seriesTitle;
    newTab.chapterTitle = localInfo.chapterTitle;
    newTab.chapterDir = chapter;
    newTab.canClose = true;
    newTab.reader = std::make_unique<ReaderView>();
    newTab.reader->Init(this, &m_set);
    if (newTab.reader->Open(chapter)) {
        newTab.reader->SetCustomTitle(localInfo.seriesTitle, localInfo.chapterTitle);
        newTab.reader->SetSourceMirror(FromUtf8(localInfo.mirror));
        if(!localInfo.seriesKey.empty() && !localInfo.chapterUrl.empty())m_lib.MarkLastRead(localInfo.seriesKey,localInfo.chapterUrl,ToUtf8(localInfo.chapterTitle));
        m_set.PushRecent(chapter);
        m_tabs.push_back(std::move(newTab));
        ActivateTab((int)m_tabs.size() - 1);
        SaveSessionTabs();
    } else {
        Toast(T(S::NoImages));
    }
}

void MainWindow::OpenOnlineManga(const std::string& mirrorName, const std::string& mangaUrl, const std::wstring& title, const std::string& chapterUrl) {
    std::string safeMangaUrl = net::NormalizeMirrorUrl(mangaUrl);
    std::string safeChapterUrl = net::NormalizeMirrorUrl(chapterUrl);
    if (safeMangaUrl.rfind("http://fanfox.net", 0) == 0) safeMangaUrl.replace(0, 4, "https");
    else if (safeMangaUrl.rfind("http://www.mangahere.cc", 0) == 0) safeMangaUrl.replace(0, 4, "https");
    else if (safeMangaUrl.rfind("http://", 0) == 0 && (safeMangaUrl.find("mangahere") != std::string::npos || safeMangaUrl.find("fanfox") != std::string::npos)) safeMangaUrl.replace(0, 4, "https");

    if (safeChapterUrl.rfind("http://fanfox.net", 0) == 0) safeChapterUrl.replace(0, 4, "https");
    else if (safeChapterUrl.rfind("http://www.mangahere.cc", 0) == 0) safeChapterUrl.replace(0, 4, "https");
    else if (safeChapterUrl.rfind("http://", 0) == 0 && (safeChapterUrl.find("mangahere") != std::string::npos || safeChapterUrl.find("fanfox") != std::string::npos)) safeChapterUrl.replace(0, 4, "https");

    // 1. Zaten bu seriye ait bir sekme acik mi kontrol et
    for (size_t i = 1; i < m_tabs.size(); ++i) {
        if (m_tabs[i].type == TabType::Reader && m_tabs[i].mangaUrl == safeMangaUrl && !safeMangaUrl.empty()) {
            ActivateTab((int)i);
            bool needsReload = m_tabs[i].failed || m_tabs[i].loading || (m_tabs[i].reader && !m_tabs[i].reader->IsOpen()) || (m_tabs[i].chapterUrl != safeChapterUrl);
            if (!safeChapterUrl.empty() && needsReload) {
                if (safeChapterUrl == "__FIRST__") {
                    if (!m_tabs[i].chapters.empty()) {
                        int targetIdx = (int)m_tabs[i].chapters.size() - 1;
                        for (size_t ci = 0; ci < m_tabs[i].chapters.size(); ++ci) {
                            double num = ParseChapterNumber(m_tabs[i].chapters[ci].title);
                            if (num >= 0.95 && num <= 1.05) {
                                targetIdx = (int)ci;
                                break;
                            }
                        }
                        OpenChapterByIndex(targetIdx);
                    }
                    return;
                }
                for (size_t ci = 0; ci < m_tabs[i].chapters.size(); ++ci) {
                    if (m_tabs[i].chapters[ci].url == safeChapterUrl) {
                        OpenChapterByIndex((int)ci);
                        return;
                    }
                }
                // Bolum listesinde yoksa dogrudan indir
                if (m_tabs[i].downloadCancel) {
                    m_tabs[i].downloadCancel->store(true);
                }
                m_tabs[i].downloadCancel = std::make_shared<std::atomic<bool>>(false);
                auto cancelToken = m_tabs[i].downloadCancel;

                m_tabs[i].chapterUrl = safeChapterUrl;
                m_tabs[i].loading = true;
                m_tabs[i].failed = false;
                m_tabs[i].statusMessage = i18n::Literal(L"⚡ Bölüm indiriliyor...");
                std::wstring cacheDir = engine::ChapterLoader::Instance().GetCacheDir(mirrorName, safeMangaUrl, safeChapterUrl);
                m_tabs[i].chapterDir = cacheDir;
                if (m_tabs[i].reader) {
                    m_tabs[i].reader->OpenProgressive(cacheDir, title, paths::FileName(FromUtf8(safeChapterUrl)), 0);
                }
                if (!mirrorName.empty() && !safeMangaUrl.empty()) {
                    std::string key = amr::MangaKey(mirrorName, safeMangaUrl, "en");

                    m_library.SetDisplayDirty();
                }
                int tidx = (int)i;
                m_ownedWorkers.emplace_back([this, tidx, mirrorName, safeMangaUrl, safeChapterUrl, title, cacheDir, cancelToken]() {
                    auto onPageReady = [this, tidx](int pageIndex, int totalPages, const std::wstring& pagePath) {
                        auto* pProg = new PageReadyInfo{ tidx, pageIndex, totalPages, pagePath };
                        PostMessageW(m_hwnd, WM_APP_PAGE_READY, 0, reinterpret_cast<LPARAM>(pProg));
                    };
                    bool ok = engine::ChapterLoader::Instance().DownloadChapter(mirrorName, safeChapterUrl, cacheDir, onPageReady,
                        [cancelToken]() { return cancelToken ? cancelToken->load() : false; });
                    if (ok) {
                        auto* pInfo = new OnlineReadyInfo{ tidx, cacheDir, title, L"" };
                        PostMessageW(m_hwnd, WM_APP_ONLINE_READY, 0, reinterpret_cast<LPARAM>(pInfo));
                    } else {
                        if (cancelToken && cancelToken->load()) return;
                        m_browserView.CrawlChapterImages(tidx, safeChapterUrl, title, L"", [this, tidx, title](const BrowserView::ImageCrawlResult& cRes) {
                            if (cRes.success) {
                                RunOnUIThread([this, tidx, cRes, title]() {
                                    if (tidx >= 0 && tidx < (int)m_tabs.size() && m_tabs[tidx].reader) {
                                        m_tabs[tidx].chapterDir = cRes.cacheDir;
                                        m_tabs[tidx].reader->OpenProgressive(cRes.cacheDir, title, L"", cRes.pageCount);
                                    }
                                    m_dirty = true;
                                });
                            } else {
                                PostMessageW(m_hwnd, WM_APP_ONLINE_FAIL, (WPARAM)tidx, 0);
                            }
                        });
                    }
                });
            }
            return;
        }
    }

    // 2. Yeni Okuyucu Sekmesi olustur
    AppTab newTab;
    newTab.type = TabType::Reader;
    newTab.mirror = mirrorName;
    newTab.mangaUrl = safeMangaUrl;
    newTab.chapterUrl = safeChapterUrl;
    newTab.mangaTitle = title;
    newTab.title = FormatTabTitle(mirrorName, title, L"");
    newTab.canClose = true;
    newTab.loading = true;
    newTab.failed = false;
    newTab.statusMessage = i18n::Literal(L"⚡ Bölüm taranıyor ve hazırlanıyor...");
    newTab.downloadCancel = std::make_shared<std::atomic<bool>>(false);
    auto cancelToken = newTab.downloadCancel;
    newTab.reader = std::make_unique<ReaderView>();
    newTab.reader->Init(this, &m_set);
    newTab.reader->SetSourceMirror(FromUtf8(mirrorName));

    std::string sKey = amr::MangaKey(mirrorName, safeMangaUrl, "en");
    const Series* foundSeries = m_lib.FindSeries(sKey);
    if (!foundSeries && !safeMangaUrl.empty()) foundSeries = m_lib.FindSeries(safeMangaUrl);
    if (foundSeries) {
        bool hasCustom = foundSeries->hasCustomReaderSettings || (foundSeries->webtoon != 0) || (foundSeries->zoom > 0.05);
        newTab.reader->SetMangaSettings(foundSeries->key, hasCustom, foundSeries->webtoon != 0,
                                       (FitMode)foundSeries->fitMode, foundSeries->doublePage != 0, foundSeries->rtl != 0,
                                       (float)foundSeries->zoom);
    } else {
        newTab.reader->SetMangaSettings(sKey, false, m_set.webtoon, m_set.fit, m_set.doublePage, m_set.rtl, 1.0f);
    }

    std::wstring initCacheDir;
    if (!safeChapterUrl.empty() && safeChapterUrl != "__FIRST__" && safeChapterUrl != "__LAST__") {
        initCacheDir = engine::ChapterLoader::Instance().GetCacheDir(mirrorName, safeMangaUrl, safeChapterUrl);
        newTab.chapterDir = initCacheDir;
        newTab.reader->OpenProgressive(initCacheDir, title, CleanChapterTitle(paths::FileName(FromUtf8(safeChapterUrl))), 0);
    } else {
        newTab.reader->OpenProgressive(L"", title, i18n::Literal(L"Bölüm taranıyor..."), 1);
    }

    m_tabs.push_back(std::move(newTab));
    int tabIdx = (int)m_tabs.size() - 1;
    ActivateTab(tabIdx);

    ToastFor(i18n::Literal(L"Bölüm yükleniyor..."), 2.5f);

    // 3. Hizli yol: Belirli bir bolum adresi zaten varsa HEMEN indirmeye basla!
    if (!safeChapterUrl.empty() && safeChapterUrl != "__FIRST__" && safeChapterUrl != "__LAST__") {
        m_ownedWorkers.emplace_back([this, tabIdx, mirrorName, safeMangaUrl, safeChapterUrl, title, initCacheDir, cancelToken]() {
            auto onPageReady = [this, tabIdx](int pageIndex, int totalPages, const std::wstring& pagePath) {
                auto* pProg = new PageReadyInfo{ tabIdx, pageIndex, totalPages, pagePath };
                PostMessageW(m_hwnd, WM_APP_PAGE_READY, 0, reinterpret_cast<LPARAM>(pProg));
            };
            bool ok = engine::ChapterLoader::Instance().DownloadChapter(mirrorName, safeChapterUrl, initCacheDir, onPageReady,
                [cancelToken]() { return cancelToken ? cancelToken->load() : false; });
            if (ok) {
                auto* pInfo = new OnlineReadyInfo{ tabIdx, initCacheDir, title, L"" };
                PostMessageW(m_hwnd, WM_APP_ONLINE_READY, 0, reinterpret_cast<LPARAM>(pInfo));
            } else {
                if (cancelToken && cancelToken->load()) return;
                log::Write(L"[OnlineManga] Doğrudan indirme başarısız (Cloudflare?), WebView2 Crawler devrede: %s", FromUtf8(safeChapterUrl).c_str());
                m_browserView.CrawlChapterImages(tabIdx, safeChapterUrl, title, L"", [this, tabIdx, title](const BrowserView::ImageCrawlResult& cRes) {
                    if (cRes.success) {
                        RunOnUIThread([this, tabIdx, cRes, title]() {
                            if (tabIdx >= 0 && tabIdx < (int)m_tabs.size() && m_tabs[tabIdx].reader) {
                                m_tabs[tabIdx].chapterDir = cRes.cacheDir;
                                m_tabs[tabIdx].reader->OpenProgressive(cRes.cacheDir, title, L"", cRes.pageCount);
                            }
                            m_dirty = true;
                        });
                    } else {
                        PostMessageW(m_hwnd, WM_APP_ONLINE_FAIL, (WPARAM)tabIdx, 0);
                    }
                });
            }
        });

        // Paralel olarak tum bolumleri arka planda tara (navigasyon ve dropdown icin)
        ScanChaptersForTab(tabIdx, mirrorName, safeMangaUrl, safeChapterUrl);
        return;
    }

    // 4. Bolum adresi belirtilmediyse veya FIRST/LAST ise: bolum listesini cekip sec
    std::string savedLastChapUrl;
    {
        std::string sKey = amr::MangaKey(mirrorName, safeMangaUrl, "en");
        const Series* ser = m_lib.FindSeries(sKey);
        if (!ser && !safeMangaUrl.empty()) ser = m_lib.FindSeries(safeMangaUrl);
        if (ser) savedLastChapUrl = ser->lastChapterUrl;
    }

    auto proceedWithChapters = [this, tabIdx, mirrorName, safeMangaUrl, title, safeChapterUrl, savedLastChapUrl](std::vector<OnlineChapter> chapters) {
        if (chapters.empty()) {
            log::Write(L"[OnlineManga] No chapters found for %s", FromUtf8(safeMangaUrl).c_str());
            PostMessageW(m_hwnd, WM_APP_ONLINE_FAIL, (WPARAM)tabIdx, 0);
            return;
        }

        size_t targetIdx = 0;
        if (safeChapterUrl == "__FIRST__") {
            targetIdx = chapters.size() - 1;
            for (size_t i = 0; i < chapters.size(); ++i) {
                double num = ParseChapterNumber(chapters[i].title);
                if (num >= 0.95 && num <= 1.05) {
                    targetIdx = i;
                    break;
                }
            }
        } else if (!safeChapterUrl.empty() && safeChapterUrl != "__LAST__") {
            for (size_t i = 0; i < chapters.size(); ++i) {
                if (chapters[i].url == safeChapterUrl) {
                    targetIdx = i;
                    break;
                }
            }
        } else {
            // safeChapterUrl bos ise veya son okunan araniyorsa:
            int foundCur = -1;
            if (!savedLastChapUrl.empty() && safeChapterUrl != "__LAST__") {
                std::string normLast = net::NormalizeMirrorUrl(savedLastChapUrl);
                for (size_t i = 0; i < chapters.size(); ++i) {
                    if (chapters[i].url == savedLastChapUrl || net::NormalizeMirrorUrl(chapters[i].url) == normLast) {
                        foundCur = (int)i;
                        break;
                    }
                }
            }

            if (foundCur >= 0) {
                targetIdx = (size_t)foundCur;
            } else if (safeChapterUrl == "__LAST__") {
                targetIdx = 0;
            } else {
                // Okunmamis serilerde dogrudan Bolum 1'i ac (__FIRST__), ASLA en son bolumu acma!
                targetIdx = chapters.size() - 1;
                for (size_t i = 0; i < chapters.size(); ++i) {
                    double num = ParseChapterNumber(chapters[i].title);
                    if (num >= 0.95 && num <= 1.05) {
                        targetIdx = i;
                        break;
                    }
                }
            }
        }

        const auto chap = chapters[targetIdx];
        auto* scanRes = new ChapterScanResult{ tabIdx, safeMangaUrl, std::move(chapters), (int)targetIdx };
        PostMessageW(m_hwnd, WM_APP_CHAPTERS_SCANNED, 0, reinterpret_cast<LPARAM>(scanRes));

        std::wstring cacheDir = engine::ChapterLoader::Instance().GetCacheDir(mirrorName, safeMangaUrl, chap.url);
        auto cancelToken = std::make_shared<std::atomic<bool>>(false);
        std::wstring cleanCh = CleanChapterTitle(FromUtf8(chap.title), (int)targetIdx);
        RunOnUIThread([this, tabIdx, cacheDir, title, cleanCh, chapUrl = chap.url, mirrorName, safeMangaUrl, cancelToken, targetIdx]() {
            if (tabIdx >= 0 && tabIdx < (int)m_tabs.size() && m_tabs[tabIdx].reader) {
                if (m_tabs[tabIdx].downloadCancel) {
                    m_tabs[tabIdx].downloadCancel->store(true);
                }
                m_tabs[tabIdx].downloadCancel = cancelToken;
                m_tabs[tabIdx].chapterDir = cacheDir;
                m_tabs[tabIdx].chapterUrl = chapUrl;
                m_tabs[tabIdx].chapterIndex = (int)targetIdx;
                m_tabs[tabIdx].chapterTitle = cleanCh;
                m_tabs[tabIdx].title = FormatTabTitle(mirrorName, title, cleanCh, (int)targetIdx);
                m_tabs[tabIdx].reader->SetSourceMirror(FromUtf8(mirrorName));
                m_tabs[tabIdx].reader->OpenProgressive(cacheDir, title, cleanCh, 0);
            }
            if (!mirrorName.empty() && !safeMangaUrl.empty()) {
                std::string key = amr::MangaKey(mirrorName, safeMangaUrl, "en");

                m_library.SetDisplayDirty();
            }
            m_dirty = true;
        });

        auto onPageReady = [this, tabIdx](int pageIndex, int totalPages, const std::wstring& pagePath) {
            auto* pProg = new PageReadyInfo{ tabIdx, pageIndex, totalPages, pagePath };
            PostMessageW(m_hwnd, WM_APP_PAGE_READY, 0, reinterpret_cast<LPARAM>(pProg));
        };
        bool ok = engine::ChapterLoader::Instance().DownloadChapter(mirrorName, chap.url, cacheDir, onPageReady,
            [cancelToken]() { return cancelToken ? cancelToken->load() : false; });
        if (ok) {
            auto* pInfo = new OnlineReadyInfo{ tabIdx, cacheDir, title, cleanCh };
            PostMessageW(m_hwnd, WM_APP_ONLINE_READY, 0, reinterpret_cast<LPARAM>(pInfo));
        } else {
            if (cancelToken && cancelToken->load()) return;
            log::Write(L"[OnlineManga] Doğrudan indirme başarısız (Cloudflare?), WebView2 Crawler devrede: %s", FromUtf8(chap.url).c_str());
            m_browserView.CrawlChapterImages(tabIdx, chap.url, title, cleanCh, [this, tabIdx, title, cleanCh](const BrowserView::ImageCrawlResult& cRes) {
                if (cRes.success) {
                    RunOnUIThread([this, tabIdx, cRes, title, cleanCh]() {
                        if (tabIdx >= 0 && tabIdx < (int)m_tabs.size() && m_tabs[tabIdx].reader) {
                            m_tabs[tabIdx].chapterDir = cRes.cacheDir;
                            m_tabs[tabIdx].reader->OpenProgressive(cRes.cacheDir, title, cleanCh, cRes.pageCount);
                        }
                        m_dirty = true;
                    });
                } else {
                    PostMessageW(m_hwnd, WM_APP_ONLINE_FAIL, (WPARAM)tabIdx, 0);
                }
            });
        }
    };

    std::string catalogueKey = amr::MangaKey(mirrorName, safeMangaUrl, "en");
    std::string catalogueLanguage = "en";
    if (const auto* series = m_lib.FindSeries(catalogueKey)) { catalogueKey = series->key; catalogueLanguage = series->language; }
    else if (const auto* series = m_lib.FindSeries(safeMangaUrl)) { catalogueKey = series->key; catalogueLanguage = series->language; }
    m_ownedWorkers.emplace_back([this, tabIdx, mirrorName, safeMangaUrl, title, safeChapterUrl, proceedWithChapters, catalogueKey, catalogueLanguage]() {
        std::string key = catalogueKey;

        // 1. Once varsa diskteki onbellekten aninda yukle
        std::vector<std::pair<std::string, std::string>> cached;
        if (amr::LoadCachedChapterList(key, cached) && !cached.empty()) {
            std::vector<OnlineChapter> converted;
            converted.reserve(cached.size());
            for (const auto& c : cached) {
                converted.push_back({ c.first, c.second });
            }
            proceedWithChapters(std::move(converted));
            DispatchUI([this, tabIdx, mirrorName, safeMangaUrl, safeChapterUrl]() { ScanChaptersForTab(tabIdx, mirrorName, safeMangaUrl, safeChapterUrl); });
            return;
        }

        log::Write(L"[OnlineManga] Fetching chapters for: %s (%s)", title.c_str(), FromUtf8(safeMangaUrl).c_str());
        auto rawChapters = engine::MirrorEngine::Instance().GetChapters(mirrorName, safeMangaUrl, catalogueLanguage);
        if (rawChapters.empty()) {
            log::Write(L"[OnlineManga] MirrorEngine boş döndü, WebView2 Crawler devreye giriyor: %s", FromUtf8(safeMangaUrl).c_str());
            m_browserView.CrawlMangaChapters(safeMangaUrl, [key, proceedWithChapters](const BrowserView::ChapterCrawlResult& cRes) {
                std::vector<OnlineChapter> converted;
                if (cRes.success && !cRes.chapters.empty()) {
                    converted.reserve(cRes.chapters.size());
                    std::vector<std::pair<std::string, std::string>> pairs;
                    pairs.reserve(cRes.chapters.size());
                    for (const auto& c : cRes.chapters) {
                        converted.push_back({ c.title, c.url });
                        pairs.push_back({ c.title, c.url });
                    }
                    amr::SaveCachedChapterList(key, pairs);
                }
                proceedWithChapters(std::move(converted));
            });
            return;
        }

        std::vector<OnlineChapter> converted;
        converted.reserve(rawChapters.size());
        std::vector<std::pair<std::string, std::string>> pairs;
        pairs.reserve(rawChapters.size());
        for (const auto& c : rawChapters) {
            converted.push_back({ c.title, c.url });
            pairs.push_back({ c.title, c.url });
        }
        amr::SaveCachedChapterList(key, pairs);
        proceedWithChapters(std::move(converted));
    });
}

bool MainWindow::NavigateChapter(int delta) {
    if (m_activeTab < 0 || m_activeTab >= (int)m_tabs.size()) return false;
    auto& tab = m_tabs[m_activeTab];
    if (tab.chapters.empty()) return false;

    // Eger chapterIndex bozulmussa veya gecersizse mevcut chapterUrl ile kurtar
    if ((tab.chapterIndex < 0 || tab.chapterIndex >= (int)tab.chapters.size()) && !tab.chapterUrl.empty()) {
        for (size_t i = 0; i < tab.chapters.size(); ++i) {
            if (tab.chapters[i].url == tab.chapterUrl) {
                tab.chapterIndex = (int)i;
                break;
            }
        }
    }

    if (tab.chapterIndex < 0 || tab.chapterIndex >= (int)tab.chapters.size()) {
        return false;
    }

    // Bolum listesinin siralanis yonunu tespit et (Azalan: index 0 en yeni; Artan: index 0 Bolum 1)
    bool isDescending = true;
    if (tab.chapters.size() >= 2) {
        double numFirst = ParseChapterNumber(tab.chapters.front().title);
        double numLast = ParseChapterNumber(tab.chapters.back().title);
        if (numFirst >= 0 && numLast >= 0 && numFirst < numLast) {
            isDescending = false; // 1, 2, 3...
        }
    }

    int step = isDescending ? -1 : 1;
    int newIdx = tab.chapterIndex + (delta * step);
    if (newIdx < 0 || newIdx >= (int)tab.chapters.size()) {
        Toast(T(delta > 0 ? S::LastChapter : S::FirstChapter));
        return true;
    }
    return OpenChapterByIndex(newIdx);
}

bool MainWindow::OpenChapterByIndex(int index) {
    if (m_activeTab < 0 || m_activeTab >= (int)m_tabs.size()) return false;
    auto& tab = m_tabs[m_activeTab];
    if (tab.chapters.empty() || index < 0 || index >= (int)tab.chapters.size()) return false;

    if (tab.downloadCancel) {
        tab.downloadCancel->store(true);
    }
    tab.downloadCancel = std::make_shared<std::atomic<bool>>(false);
    auto cancelToken = tab.downloadCancel;

    tab.chapterIndex = index;
    const auto& chap = tab.chapters[(size_t)index];
    tab.chapterUrl = chap.url;
    std::wstring cleanCh = CleanChapterTitle(FromUtf8(chap.title), index);
    tab.chapterTitle = cleanCh;
    tab.title = FormatTabTitle(tab.mirror, tab.mangaTitle, cleanCh, index);
    tab.loading = true;
    tab.failed = false;
    tab.statusMessage = L"⚡ " + cleanCh + i18n::Literal(L" indiriliyor...");

    int tabIdx = m_activeTab;
    std::string mirrorName = tab.mirror;
    std::string mangaUrl = tab.mangaUrl;
    std::string chapUrl = chap.url;
    std::wstring title = tab.mangaTitle;
    std::wstring chapTitle = cleanCh;

    std::wstring cacheDir = engine::ChapterLoader::Instance().GetCacheDir(mirrorName, mangaUrl, chapUrl);
    tab.chapterDir = cacheDir;
    if (tab.reader) {
        tab.reader->SetSourceMirror(FromUtf8(mirrorName));
        tab.reader->OpenProgressive(cacheDir, title, chapTitle, 0);
    }

    if (!mirrorName.empty() && !mangaUrl.empty()) {
        std::string key = amr::MangaKey(mirrorName, mangaUrl, "en");

        m_library.SetDisplayDirty();
    }

    ToastFor(std::wstring(T(S::OpeningChapter)) + L" " + chapTitle, 3.0f);

    m_ownedWorkers.emplace_back([this, tabIdx, mirrorName, mangaUrl, chapUrl, title, chapTitle, cacheDir, cancelToken]() {
        auto onPageReady = [this, tabIdx](int pageIndex, int totalPages, const std::wstring& pagePath) {
            auto* pProg = new PageReadyInfo{ tabIdx, pageIndex, totalPages, pagePath };
            PostMessageW(m_hwnd, WM_APP_PAGE_READY, 0, reinterpret_cast<LPARAM>(pProg));
        };
        bool ok = engine::ChapterLoader::Instance().DownloadChapter(mirrorName, chapUrl, cacheDir, onPageReady,
            [cancelToken]() { return cancelToken ? cancelToken->load() : false; });
        if (ok) {
            auto* pInfo = new OnlineReadyInfo{ tabIdx, cacheDir, title, chapTitle };
            PostMessageW(m_hwnd, WM_APP_ONLINE_READY, 0, reinterpret_cast<LPARAM>(pInfo));
        } else {
            if (cancelToken && cancelToken->load()) return;
            log::Write(L"[OpenChapterByIndex] Doğrudan indirme başarısız (Cloudflare?), WebView2 Crawler devrede: %s", FromUtf8(chapUrl).c_str());
            m_browserView.CrawlChapterImages(tabIdx, chapUrl, title, chapTitle, [this, tabIdx, title, chapTitle](const BrowserView::ImageCrawlResult& cRes) {
                if (cRes.success) {
                    RunOnUIThread([this, tabIdx, cRes, title, chapTitle]() {
                        if (tabIdx >= 0 && tabIdx < (int)m_tabs.size() && m_tabs[tabIdx].reader) {
                            m_tabs[tabIdx].chapterDir = cRes.cacheDir;
                            m_tabs[tabIdx].reader->OpenProgressive(cRes.cacheDir, title, chapTitle, cRes.pageCount);
                        }
                        m_dirty = true;
                    });
                } else {
                    PostMessageW(m_hwnd, WM_APP_ONLINE_FAIL, (WPARAM)tabIdx, 0);
                }
            });
        }
    });

    SaveSessionTabs();
    m_dirty = true;
    return true;
}

void MainWindow::CrawlChaptersForManga(const std::string& mangaUrl, std::function<void(const std::vector<std::pair<std::string, std::string>>&)> callback) {
    m_browserView.CrawlMangaChapters(mangaUrl, [callback](const BrowserView::ChapterCrawlResult& res) {
        std::vector<std::pair<std::string, std::string>> list;
        if (res.success) {
            list.reserve(res.chapters.size());
            for (const auto& ch : res.chapters) {
                list.push_back({ ch.title, ch.url });
            }
        }
        if (callback) callback(list);
    });
}


const std::vector<OnlineChapter>& MainWindow::GetCurrentChapters() const {
    if (m_activeTab >= 0 && m_activeTab < (int)m_tabs.size()) {
        return m_tabs[m_activeTab].chapters;
    }
    static const std::vector<OnlineChapter> empty;
    return empty;
}

int MainWindow::GetCurrentChapterIndex() const {
    if (m_activeTab >= 0 && m_activeTab < (int)m_tabs.size()) {
        return m_tabs[m_activeTab].chapterIndex;
    }
    return -1;
}

bool MainWindow::HasNextChapter() const {
    if (m_activeTab < 0 || m_activeTab >= (int)m_tabs.size()) return false;
    const auto& tab = m_tabs[m_activeTab];
    if (tab.chapters.empty()) {
        if (tab.reader) {
            return !tab.reader->NextDir().empty();
        }
        return false;
    }

    int curIdx = tab.chapterIndex;
    if ((curIdx < 0 || curIdx >= (int)tab.chapters.size()) && !tab.chapterUrl.empty()) {
        for (size_t i = 0; i < tab.chapters.size(); ++i) {
            if (tab.chapters[i].url == tab.chapterUrl) {
                curIdx = (int)i;
                break;
            }
        }
    }
    if (curIdx < 0 || curIdx >= (int)tab.chapters.size()) return false;

    bool isDescending = true;
    if (tab.chapters.size() >= 2) {
        double numFirst = ParseChapterNumber(tab.chapters.front().title);
        double numLast = ParseChapterNumber(tab.chapters.back().title);
        if (numFirst >= 0 && numLast >= 0 && numFirst < numLast) {
            isDescending = false;
        }
    }

    int step = isDescending ? -1 : 1;
    int nextIdx = curIdx + step;
    return (nextIdx >= 0 && nextIdx < (int)tab.chapters.size());
}

bool MainWindow::HasPrevChapter() const {
    if (m_activeTab < 0 || m_activeTab >= (int)m_tabs.size()) return false;
    const auto& tab = m_tabs[m_activeTab];
    if (tab.chapters.empty()) {
        if (tab.reader) {
            return !tab.reader->PrevDir().empty();
        }
        return false;
    }

    int curIdx = tab.chapterIndex;
    if ((curIdx < 0 || curIdx >= (int)tab.chapters.size()) && !tab.chapterUrl.empty()) {
        for (size_t i = 0; i < tab.chapters.size(); ++i) {
            if (tab.chapters[i].url == tab.chapterUrl) {
                curIdx = (int)i;
                break;
            }
        }
    }
    if (curIdx < 0 || curIdx >= (int)tab.chapters.size()) return false;

    bool isDescending = true;
    if (tab.chapters.size() >= 2) {
        double numFirst = ParseChapterNumber(tab.chapters.front().title);
        double numLast = ParseChapterNumber(tab.chapters.back().title);
        if (numFirst >= 0 && numLast >= 0 && numFirst < numLast) {
            isDescending = false;
        }
    }

    int step = isDescending ? -1 : 1;
    int prevIdx = curIdx - step;
    return (prevIdx >= 0 && prevIdx < (int)tab.chapters.size());
}

void MainWindow::AddBookmark(const std::wstring& seriesName, const std::wstring& chapterName, int pageIndex) {
    Bookmark bm;
    std::string mirrorName = "Yerel";
    std::string mangaUrl;
    std::string chapUrl;
    if (m_activeTab >= 0 && m_activeTab < (int)m_tabs.size()) {
        const auto& tab = m_tabs[m_activeTab];
        if (!tab.mirror.empty()) mirrorName = tab.mirror;
        mangaUrl = tab.mangaUrl;
        if (tab.chapterIndex >= 0 && tab.chapterIndex < (int)tab.chapters.size()) {
            chapUrl = tab.chapters[(size_t)tab.chapterIndex].url;
        } else if (!tab.chapterUrl.empty()) {
            chapUrl = tab.chapterUrl;
        } else if (tab.reader) {
            chapUrl = ToUtf8(tab.reader->Dir());
        }
    }
    bm.mirror = mirrorName;
    bm.name = ToUtf8(seriesName);
    bm.chapName = ToUtf8(chapterName);
    bm.url = mangaUrl;
    bm.chapUrl = chapUrl;
    bm.note = "Sayfa " + std::to_string(pageIndex + 1);
    if (m_lib.AddBookmark(bm)) {
        Toast(i18n::Literal(L"Yer imi eklendi: ") + (chapterName.empty() ? seriesName : chapterName));
    } else {
        Toast(i18n::Literal(L"Yer imi güncellendi."));
    }
    m_dirty = true;
}

void MainWindow::RemoveBookmark(size_t index) {
    if (m_lib.RemoveBookmark(index)) {
        Toast(i18n::Literal(L"Yer imi silindi."));
        m_dirty = true;
    }
}

void MainWindow::AddSeriesToLibrary(const std::string& mirrorName, const std::string& mangaUrl, const std::wstring& title) {
    if (m_lib.FindSource(mirrorName, mangaUrl)) { Toast(i18n::Literal(L"Bu kaynak zaten kütüphanede.")); return; }
    Series s;
    s.mirror = mirrorName;
    s.url = mangaUrl;
    s.name = ToUtf8(title);
    s.language = "en";
    s.key = amr::MangaKey(mirrorName, mangaUrl, s.language);
    if (m_lib.AddSeries(s)) {
        Toast(i18n::Literal(L"Kütüphaneye eklendi: ") + title);
    } else {
        Toast(i18n::Literal(L"Kaynak kütüphaneye kaydedilemedi."));
    }
    m_dirty = true;
}

void MainWindow::OpenEmbeddedBrowser(const std::string& url, const std::string& mirrorHint) {
    if (url.empty()) return;
    std::wstring cleanTitle = !mirrorHint.empty() ? FromUtf8(mirrorHint) : i18n::Literal(L"Web Görünümü");

    for (size_t i = 1; i < m_tabs.size(); ++i) {
        if (m_tabs[i].type == TabType::Browser) {
            ActivateTab((int)i);
            m_browserView.Navigate(FromUtf8(url), cleanTitle);
            return;
        }
    }

    AppTab bTab;
    bTab.type = TabType::Browser;
    bTab.title = cleanTitle;
    bTab.mangaTitle = cleanTitle;
    bTab.mirror = mirrorHint;
    bTab.canClose = true;
    m_tabs.push_back(std::move(bTab));
    ActivateTab((int)m_tabs.size() - 1);
    m_browserView.Navigate(FromUtf8(url), cleanTitle);
    SaveSessionTabs();
}

void MainWindow::CloseBrowser() {
    for (size_t i = 1; i < m_tabs.size(); ++i) {
        if (m_tabs[i].type == TabType::Browser) {
            CloseTab((int)i);
            return;
        }
    }
    SetView(View::Library);
    m_dirty = true;
}

void MainWindow::OpenExtractedChapter(const std::wstring& chapterDir, const std::wstring& mangaTitle, const std::wstring& chapterTitle) {
    RunOnUIThread([this, chapterDir, mangaTitle, chapterTitle]() {
        AppTab newTab;
        newTab.type = TabType::Reader;
        newTab.mangaTitle = mangaTitle.empty() ? L"Manga" : mangaTitle;
        std::wstring cleanCh = CleanChapterTitle(chapterTitle);
        newTab.chapterTitle = cleanCh;
        newTab.title = FormatTabTitle("", newTab.mangaTitle, cleanCh);
        newTab.chapterDir = chapterDir;
        newTab.canClose = true;
        newTab.reader = std::make_unique<ReaderView>();
        newTab.reader->Init(this, &m_set);
        if (newTab.reader->Open(chapterDir)) {
            newTab.reader->SetCustomTitle(newTab.mangaTitle, cleanCh);
            m_tabs.push_back(std::move(newTab));
            ActivateTab((int)m_tabs.size() - 1);
            SaveSessionTabs();
        }
    });
}

void MainWindow::DispatchUI(std::function<void()> fn) {
    if (!fn || !m_hwnd || m_quitting) return;
    {
        std::lock_guard<std::mutex> lock(m_uiDispatchMutex);
        m_uiDispatchQueue.push_back(std::move(fn));
    }
    PostMessageW(m_hwnd, WM_APP_DISPATCH_UI, 0, 0);
}

void MainWindow::StoreChapterCatalogue(const std::string& key, const std::vector<Chapter>& chapters) {
    DispatchUI([this, key, chapters]() {
        if (chapters.empty()) {
            const auto* series=m_lib.FindSeries(key);
            const auto error=series && IsMangaDexSource(series->url)
                ? "MangaDex: kayıtlı dilde okunabilir bölüm listesi alınamadı ("+series->language+")"
                : std::string("Bölüm listesi alınamadı");
            m_lib.StoreUpdateError(key, error);
            m_library.SetDisplayDirty();
            if (m_pendingReadToLatest.erase(key)) Toast(i18n::Literal(L"Son bölüm alınamadı; okuma ilerlemesi değiştirilmedi."));
            return;
        }
        const auto* series = m_lib.FindSeries(key);
        if (!series) return; // A deleted series must not be resurrected by a late response.
        if (!m_lib.StoreChapters(key, chapters)) {
            Toast(i18n::Literal(L"Bölüm listesi diske kaydedilemedi."));
            m_pendingReadToLatest.erase(key);
            return;
        }
        std::vector<std::pair<std::string, std::string>> pairs;
        for (const auto& ch : chapters) pairs.emplace_back(ch.title, ch.url);
        amr::SaveCachedChapterList(key, pairs);
        m_library.CatalogueChanged(key);
        m_library.ClearUpdateError(key);
        if (m_pendingReadToLatest.erase(key)) SetReadingProgress(key, true);
        m_dirty = true;
    });
}

void MainWindow::SetReadingProgress(const std::string& key, bool readToLatest) {
    DispatchUI([this, key, readToLatest]() {
        const auto* series = m_lib.FindSeries(key);
        if (!series) { Toast(i18n::Literal(L"Seri kütüphanede bulunamadı.")); return; }
        const std::wstring title = FromUtf8(series->displayName.empty() ? series->name : series->displayName);
        if (readToLatest && series->latestChapterUrl.empty()) {
            m_pendingReadToLatest.insert(key);
            m_library.RequestCatalogue(*series, *this);
            Toast(i18n::Literal(L"Son bölüm kaynaktan alınıyor: ") + title);
            return;
        }
        if (!readToLatest) m_pendingReadToLatest.erase(key);
        const bool ok = readToLatest ? m_lib.MarkReadUpToLatest(key) : m_lib.ResetReadingProgress(key);
        if (!ok) { Toast(i18n::Literal(L"Okuma ilerlemesi kaydedilemedi; önceki durum korundu.")); return; }
        m_library.CatalogueChanged(key);
        Toast((readToLatest ? i18n::Literal(L"Son bölüme kadar okundu: ") : i18n::Literal(L"Okuma ilerlemesi sıfırlandı: ")) + title);
        m_dirty = true;
    });
}

void MainWindow::RunOnUIThread(std::function<void()> fn) {
    if (!fn || m_quitting) return;
    if (m_hwnd && GetCurrentThreadId() == GetWindowThreadProcessId(m_hwnd, nullptr)) {
        fn();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_uiDispatchMutex);
        m_uiDispatchQueue.push_back(std::move(fn));
    }
    if (m_hwnd) PostMessageW(m_hwnd, WM_APP_DISPATCH_UI, 0, 0);
}

void MainWindow::ShowChallengeVerification(const std::string& url, const std::wstring& mangaTitle, const std::string& mode) {
    RunOnUIThread([this, url, mangaTitle, mode]() {
        std::string cleanUrl = url;
        size_t f = cleanUrl.find_first_not_of(" \t\r\n");
        if (f != std::string::npos) {
            size_t l = cleanUrl.find_last_not_of(" \t\r\n");
            cleanUrl = cleanUrl.substr(f, l - f + 1);
        }
        if (cleanUrl.empty() || cleanUrl == "__FIRST__" || cleanUrl == "__LAST__") {
            if (m_activeTab > 0 && m_activeTab < (int)m_tabs.size()) {
                const auto& tab = m_tabs[m_activeTab];
                if (!tab.chapters.empty()) {
                    if (cleanUrl == "__FIRST__") cleanUrl = tab.chapters.back().url;
                    else cleanUrl = tab.chapters.front().url;
                } else if (!tab.mangaUrl.empty()) {
                    cleanUrl = tab.mangaUrl;
                }
            }
        }
        // If relative URL, prepend origin from active tab
        if (cleanUrl.rfind("/", 0) == 0 && m_activeTab > 0 && m_activeTab < (int)m_tabs.size()) {
            const auto& tab = m_tabs[m_activeTab];
            size_t p = tab.mangaUrl.find("://");
            if (p != std::string::npos) {
                size_t p2 = tab.mangaUrl.find('/', p + 3);
                std::string origin = (p2 != std::string::npos) ? tab.mangaUrl.substr(0, p2) : tab.mangaUrl;
                cleanUrl = origin + cleanUrl;
            }
        }

        m_challengeModalOpen = true;
        m_challengeUrl = cleanUrl;
        m_challengeMangaTitle = mangaTitle;
        m_challengeMode = mode;
        m_browserView.StartChallengeVerification(cleanUrl, mangaTitle);
        ToastFor(i18n::Literal(L"🛡️ Cloudflare robot doğrulaması algılandı! Lütfen kutuyu işaretleyin."), 4.5f);
        m_dirty = true;
        Invalidate();
    });
}

void MainWindow::OnChallengeSolved() {
    RunOnUIThread([this]() {
        m_challengeModalOpen = false;
        m_browserView.Show(false);
        ToastFor(i18n::Literal(L"✅ Robot doğrulaması başarılı! Bölüm hazırlanıyor..."), 3.0f);

        m_browserView.SyncCookiesToJar();

        bool wasCrawling = m_browserView.IsCrawling();
        m_browserView.FinishChallengeVerification();

        if (!wasCrawling && m_activeTab > 0 && m_activeTab < (int)m_tabs.size()) {
            auto& tab = m_tabs[m_activeTab];
            tab.failed = false;
            tab.loading = true;
            tab.statusMessage = i18n::Literal(L"⚡ Doğrulama tamamlandı, bölüm yükleniyor...");
            std::string mirror = tab.mirror;
            std::string mangaUrl = tab.mangaUrl;
            std::wstring title = tab.mangaTitle;
            std::string chapUrl = tab.chapterUrl;
            OpenOnlineManga(mirror, mangaUrl, title, chapUrl);
        }
        m_dirty = true;
        Invalidate();
    });
}

void MainWindow::OnCrawlerPageReady(int tabIndex, int pageIndex, int totalPages, const std::wstring& pagePath) {
    if (!m_hwnd) return;
    auto* pProg = new PageReadyInfo{ tabIndex, pageIndex, totalPages, pagePath };
    PostMessageW(m_hwnd, WM_APP_PAGE_READY, 0, reinterpret_cast<LPARAM>(pProg));
}

void MainWindow::OnCrawlerChapterReady(int tabIndex, const std::wstring& cacheDir, const std::wstring& mangaTitle, const std::wstring& chapterTitle) {
    if (!m_hwnd) return;
    auto* pInfo = new OnlineReadyInfo{ tabIndex, cacheDir, mangaTitle, chapterTitle };
    PostMessageW(m_hwnd, WM_APP_ONLINE_READY, 0, reinterpret_cast<LPARAM>(pInfo));
}

void MainWindow::DrawChallengeModal(const D2D1_RECT_F& content, double now) {
    (void)now;
    if (!m_challengeModalOpen) return;

    const float s = m_r.Scale();

    // Modal kart (geniş ve ferah)
    const float cardW = std::min(860.0f * s, RcW(content) - 40.0f * s);
    const float cardH = std::min(640.0f * s, RcH(content) - 40.0f * s);
    const D2D1_RECT_F card = RcWH(content.left + (RcW(content) - cardW) * 0.5f,
                                  content.top + (RcH(content) - cardH) * 0.5f,
                                  cardW, cardH);
    m_challengeCardRc = card;

    // Arka plan karartma ve kart dışına tıklama ile iptal etme
    m_r.Fill(content, theme::Black, 0.75f);
    m_ui.Occlude(card);
    if (m_ui.Button(Id("cf.dim"), content)) {
        m_challengeModalOpen = false;
        m_browserView.CancelChallengeVerification();
        m_browserView.Show(false);
        if (m_activeTab > 0 && m_activeTab < (int)m_tabs.size()) {
            auto& tab = m_tabs[m_activeTab];
            if (tab.loading) {
                tab.loading = false;
                tab.failed = true;
                tab.statusMessage = i18n::Literal(L"❌ Robot doğrulaması iptal edildi.");
            }
        }
        m_dirty = true;
        Invalidate();
    }
    m_ui.ClearOcclusion();

    m_r.FillRound(card, 14.0f * s, theme::Elevated, 1.0f);
    m_r.StrokeRound(card, 14.0f * s, theme::Accent, 1.5f * s, 0.7f);

    // Başlık şeridi
    const float headerH = 48.0f * s;
    const D2D1_RECT_F headerRc = RcWH(card.left, card.top, cardW, headerH);
    m_r.FillRound(headerRc, 14.0f * s, theme::Panel, 1.0f);
    m_r.Line(card.left, headerRc.bottom, card.right, headerRc.bottom, theme::Border, 1.0f * s, 0.5f);

    // Başlık ve ikonu
    const float iconSz = 22.0f * s;
    const D2D1_RECT_F iconRc = RcWH(card.left + 16.0f * s, card.top + (headerH - iconSz) * 0.5f, iconSz, iconSz);
    DrawIcon(m_r, Icon::Ghost, iconRc, iconSz, theme::Accent);

    const D2D1_RECT_F titleRc = Rc(iconRc.right + 10.0f * s, card.top + 6.0f * s, card.right - 210.0f * s, card.top + 26.0f * s);
    m_r.Text(i18n::Literal(L"🛡️ ROBOT VE GÜVENLİK DOĞRULAMASI (CLOUDFLARE)"), titleRc, theme::Text, 13.5f * s,
             Renderer::Align::Left, Renderer::Weight::Bold);

    const D2D1_RECT_F subRc = Rc(iconRc.right + 10.0f * s, card.top + 26.0f * s, card.right - 210.0f * s, card.top + 43.0f * s);
    m_r.Text(i18n::Literal(L"Lütfen aşağıdaki sayfada kutuyu işaretleyin. Tamamlanınca otomatik algılanıp kapanır."),
             subRc, theme::TextMuted, 11.5f * s, Renderer::Align::Left);

    // Yenile ve İptal butonları
    const float btnH = 28.0f * s;
    const float btnW = 90.0f * s;
    const D2D1_RECT_F reloadRc = RcWH(card.right - btnW * 2.0f - 18.0f * s, card.top + (headerH - btnH) * 0.5f, btnW, btnH);
    const D2D1_RECT_F closeRc = RcWH(card.right - btnW - 10.0f * s, card.top + (headerH - btnH) * 0.5f, btnW, btnH);

    if (w::TextButton(m_r, m_ui, Id("cf.reload"), reloadRc, i18n::Literal(L"🔄 Yenile"), false)) {
        m_browserView.ReloadCurrentPage();
        ToastFor(L"Sayfa yenileniyor...", 2.0f);
        m_dirty = true;
        Invalidate();
    }
    m_ui.Tip(Id("cf.reload"), i18n::Literal(L"Doğrulama sayfasını baştan yükle"), reloadRc);

    if (w::TextButton(m_r, m_ui, Id("cf.cancel"), closeRc, i18n::Literal(L"✕ İptal"), false)) {
        m_challengeModalOpen = false;
        m_browserView.CancelChallengeVerification();
        m_browserView.Show(false);
        if (m_activeTab > 0 && m_activeTab < (int)m_tabs.size()) {
            auto& tab = m_tabs[m_activeTab];
            if (tab.loading) {
                tab.loading = false;
                tab.failed = true;
                tab.statusMessage = i18n::Literal(L"❌ Robot doğrulaması iptal edildi.");
            }
        }
        m_dirty = true;
        Invalidate();
    }
    m_ui.Tip(Id("cf.cancel"), i18n::Literal(L"Doğrulamayı kapat"), closeRc);

    // WebView2 gömülü tarayıcı alanı
    const D2D1_RECT_F webRc = Rc(card.left + 2.0f * s, headerRc.bottom + 1.0f * s, card.right - 2.0f * s, card.bottom - 2.0f * s);
    m_r.Fill(webRc, theme::Black, 1.0f);
    m_r.Text(i18n::Literal(L"🌐 Doğrulama sayfası yükleniyor..."), webRc, theme::TextMuted, 12.0f * s, Renderer::Align::Center);

    m_browserView.UpdateBounds(webRc);
    m_browserView.Show(true);
}

void MainWindow::ConvertAndOptimizeLibrary() {
    int fixed = 0;
    if (m_lib.ConvertAndOptimize(fixed)) {
        wchar_t buf[128];
        swprintf_s(buf, i18n::Literal(L"Kütüphane optimize edildi: %d kayıt/alan düzeltildi."), fixed);
        ToastFor(buf, 4.0f);
    } else {
        Toast(i18n::Literal(L"Kütüphane optimize edilemedi."));
    }
    m_dirty = true;
}

void MainWindow::DeleteSeriesFromLibrary(const std::string& key) {
    if (m_lib.RemoveSeries(key)) {
        Toast(i18n::Literal(L"Seri kütüphaneden silindi."));
        m_dirty = true;
    }
}

void MainWindow::DeleteLocalChapter(const std::wstring& path) {
    // Dispatch after rendering: closing readers releases image decoder handles.
    DispatchUI([this,path](){
        const auto info=local::Describe(path,m_lib);
        const auto downloads=m_set.downloadDir.empty()?paths::Join(paths::DataDir(),L"downloads"):m_set.downloadDir;
        if(!local::CanDelete(info,downloads)){Toast(i18n::Literal(L"Bu konum silinemiyor; yalnız yerel bölüm klasörü veya arşivi seçin."));return;}
        for(int i=(int)m_tabs.size()-1;i>0;--i) {
            if(m_tabs[i].chapterDir==path || (!m_tabs[i].chapterDir.empty() && local::Describe(m_tabs[i].chapterDir,m_lib).originalPath==path)) {
                if(m_tabs[i].downloadCancel)m_tabs[i].downloadCancel->store(true);CloseTab(i);
            }
        }
        m_ownedWorkers.emplace_back([this,info,downloads](){
            std::wstring error;const bool ok=local::Recycle(info,downloads,error);
            DispatchUI([this,info,ok,error](){
                if(ok){m_set.recent.erase(std::remove_if(m_set.recent.begin(),m_set.recent.end(),[&](const auto& item){return item==info.path || GetFileAttributesW(item.c_str())==INVALID_FILE_ATTRIBUTES;}),m_set.recent.end());SaveSettings();}
                m_library.RecheckRecent();Toast(ok?i18n::Literal(L"Yerel bölüm Geri Dönüşüm Kutusu'na taşındı."):error);Invalidate();
            });
        });
    });
}

void MainWindow::DownloadChapter(const std::wstring& chapterDir, const std::wstring& seriesName, const std::wstring& chapterName) {
    auto info=local::Describe(chapterDir,m_lib);
    if(!seriesName.empty())info.seriesTitle=seriesName;
    if(!chapterName.empty())info.chapterTitle=chapterName;
    const auto dlDir=m_set.downloadDir.empty()?paths::Join(paths::DataDir(),L"downloads"):m_set.downloadDir;
    const int format=m_set.downloadFormat;
    info.originalPath.clear();
    Toast(i18n::Literal(L"Bölüm indiriliyor..."));
    m_ownedWorkers.emplace_back([this,chapterDir,info,dlDir,format]() mutable {
        const auto seriesFolder=paths::Join(dlDir,SanitizeForPath(info.seriesTitle.empty()?L"Manga":info.seriesTitle));
        paths::EnsureDir(seriesFolder);
        const auto name=SanitizeForPath(info.chapterTitle.empty()?paths::FileName(chapterDir):info.chapterTitle);
        bool ok=false;
        if(format==0){info.path=paths::Join(seriesFolder,name+L".cbz");info.archive=true;ok=engine::CbzWriter::PackFolder(chapterDir,info.path);}
        else {
            info.path=paths::Join(seriesFolder,name);info.archive=false;paths::EnsureDir(info.path);
            const auto images=local::ListImages(chapterDir);ok=!images.empty();
            for(const auto& image:images) if(!CopyFileW(image.c_str(),paths::Join(info.path,paths::FileName(image)).c_str(),FALSE))ok=false;
        }
        if(ok) {
            const bool metadata=local::StoreInfo(info,true);
            DispatchUI([this,name,metadata](){m_library.RecheckRecent();Toast(metadata?i18n::Literal(L"Bölüm indirildi: ")+name:i18n::Literal(L"Bölüm indirildi; manga bilgisi kaydedilemedi: ")+name);Invalidate();});
        } else Toast(i18n::Literal(L"Bölüm indirilemedi; dosyaların tamamı kopyalanamadı veya paketlenemedi."));
    });
}

void MainWindow::ReloadCurrentChapter() {
    if (m_activeTab < 0 || m_activeTab >= (int)m_tabs.size()) return;
    auto& tab = m_tabs[(size_t)m_activeTab];
    if (tab.type == TabType::Reader && !tab.mangaUrl.empty()) {
        std::wstring dir = tab.chapterDir;
        if (!dir.empty()) {
            WIN32_FIND_DATAW fd;
            HANDLE hFind = FindFirstFileW((dir + L"\\*").c_str(), &fd);
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                        DeleteFileW(paths::Join(dir, fd.cFileName).c_str());
                    }
                } while (FindNextFileW(hFind, &fd));
                FindClose(hFind);
            }
        }
        Toast(i18n::Literal(L"Resimler baştan yükleniyor..."));
        if (tab.chapterIndex >= 0 && tab.chapterIndex < (int)tab.chapters.size()) {
            OpenChapterByIndex(tab.chapterIndex);
        } else if (!tab.chapterUrl.empty()) {
            OpenOnlineManga(tab.mirror, tab.mangaUrl, tab.mangaTitle, tab.chapterUrl);
        }
    } else if (tab.reader) {
        std::wstring dir = tab.reader->Dir();
        if (!dir.empty()) {
            tab.reader->Open(dir);
            Toast(i18n::Literal(L"Bölüm baştan yüklendi."));
        }
    }
}

void MainWindow::ChapterOpened(const std::wstring& dir) {
    auto info=local::Describe(dir,m_lib);
    for(const auto& tab:m_tabs) if(tab.chapterDir==dir && !tab.mangaUrl.empty()) {
        info.seriesTitle=tab.mangaTitle;info.chapterTitle=tab.chapterTitle;
        info.mirror=tab.mirror;info.mangaUrl=tab.mangaUrl;info.chapterUrl=tab.chapterUrl;
        if(const auto* series=m_lib.FindSource(tab.mirror,tab.mangaUrl))info.seriesKey=series->key;
        break;
    }
    if(info.seriesTitle!=i18n::Literal(L"Kimliği bulunamayan yerel manga")) local::StoreInfo(info);
    m_library.RecheckRecent();
    m_set.PushRecent(dir);
    SaveSettings();
    m_dirty = true;
}

void MainWindow::SaveSettings() {
    if (m_set.startOnLogin != startup::Enabled() && !startup::SetEnabled(m_set.startOnLogin)) {
        m_set.startOnLogin = startup::Enabled();
        Toast(i18n::Literal(L"Başlangıç ayarı değiştirilemedi."));
    }
    m_set.Save();
    for (auto& tab : m_tabs) {
        if (tab.reader) tab.reader->SettingsChanged();
    }
    if (!m_quitting) {
        if (m_set.bgScanInterval <= 0) {
            engine::Scheduler::Instance().Stop();
        } else {
            engine::Scheduler::Instance().Start(
                [this](const engine::UpdateNotification& notif) {
                    if (m_hwnd) {
                        auto* p = new engine::UpdateNotification(notif);
                        PostMessageW(m_hwnd, WM_APP_NEW_CHAPTERS, 0, reinterpret_cast<LPARAM>(p));
                    }
                },
                [this]() {
                    if (m_hwnd) {
                        PostMessageW(m_hwnd, WM_APP_SCAN_FINISHED, 0, 0);
                    }
                },
                m_set.bgScanInterval
            );
        }
    }
    m_dirty = true;
}

void MainWindow::SaveMangaReadingSettings(const std::string& seriesKey, bool webtoon, FitMode fit, bool doublePage, bool rtl, float zoom) {
    if (!seriesKey.empty()) {
        m_lib.UpdateReadingSettings(seriesKey, webtoon, (int)fit, doublePage, rtl, (double)zoom);
        m_library.SetDisplayDirty();
    }
}

void MainWindow::LanguageChanged() {
    i18n::Set(m_set.lang);
    SetWindowTextW(m_hwnd, T(S::AppName));
    if (m_trayAdded) {
        NOTIFYICONDATAW nid{ sizeof(nid) };
        nid.hWnd = m_hwnd;
        nid.uID = kTrayId;
        nid.uFlags = NIF_TIP | NIF_SHOWTIP;
        wcsncpy_s(nid.szTip, T(S::AppName), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
    Toast(T(S::LanguageChanged));
    m_dirty = true;
}

void MainWindow::Toast(const std::wstring& msg) { ToastFor(msg, theme::ToastTime); }

void MainWindow::ToastFor(const std::wstring& msg, float seconds) {
    RunOnUIThread([this, msg, seconds]() {
        m_toastText = msg;
        m_toastT = m_toastTotal = seconds;
        m_dirty = true;
    });
}

void MainWindow::DecoderSignaled() {
    // Is parcacigindan. Kuyruga ust uste mesaj yigmayalim.
    bool expected = false;
    if (m_decodePosted.compare_exchange_strong(expected, true)) {
        HWND h = m_hwnd;
        if (h) PostMessageW(h, WM_APP_DECODED, 0, 0);
    }
}

// ------------------------------------------------------------ kare dongusu --

DWORD MainWindow::Tick() {
    if (!m_alive) return INFINITE;
    const double now = Now();
    const float dt = (float)std::clamp(now - m_lastTick, 0.0, 0.1);
    m_lastTick = now;

    // Gizliyken cizim yok; yalniz mesajlar ve arka plan isleri (FR-BG-009).
    if (!IsWindowVisible(m_hwnd) || IsIconic(m_hwnd)) {
        m_dirty = false;
        return INFINITE;
    }

    bool anim = false;
    const float drawerTarget = m_drawerOpen ? 1.0f : 0.0f;
    if (m_drawerT != drawerTarget) {
        const float step = dt / theme::DrawerAnim;
        m_drawerT = drawerTarget > m_drawerT ? std::min(1.0f, m_drawerT + step) : std::max(0.0f, m_drawerT - step);
        anim = true;
    }
    if (m_toastT > 0.0f) {
        m_toastT = std::max(0.0f, m_toastT - dt);
        anim = true;
    }
    if (m_view == View::Reader && ActiveReader()->IsOpen()) anim |= ActiveReader()->Animate(dt, now);
    else if (m_view == View::Reader && m_activeTab > 0 && m_activeTab < (int)m_tabs.size() && m_tabs[m_activeTab].loading) anim = true;

    // Aygit yeniden kurulduysa (surucu guncellemesi, GPU sifirlama) bos kalan
    // pencereyi hemen ciz.
    if (m_seenDevGen != m_r.DeviceGeneration()) {
        m_seenDevGen = m_r.DeviceGeneration();
        m_dirty = true;
    }

    // Ipucu yalniz metni olan bir oge icin beklenir. TipText bir onceki karenin
    // degeridir; metinsiz ogenin ustunde beklemek sonsuz kare dongusu olurdu.
    const bool tipPending = !m_ui.TipText().empty() && m_ui.HotStable() && !m_tipShown && !m_in.down;
    const bool tipDue = tipPending && (now - m_ui.HotSince()) >= theme::TipDelay;
    if (anim || m_dirty || tipDue) {
        m_dirty = false;
        if (Render(now)) return 0;
        return 250; // cizilemedi (aygit yok): kisa aralarla yeniden dene
    }

    double wait = -1.0;
    auto consider = [&](double w) {
        if (w < 0.0) return;
        wait = wait < 0.0 ? w : std::min(wait, w);
    };
    if (m_view == View::Reader) consider(ActiveReader()->NextWake(now));
    if (tipPending) consider(m_ui.HotSince() + theme::TipDelay - now);
    if (wait < 0.0) return INFINITE;
    return (DWORD)std::max(1.0, std::ceil(wait * 1000.0));
}

void MainWindow::ApplyPendingResize() {
    if (!m_resizePending) return;
    m_resizePending = false;
    m_r.Resize(m_pendingW, m_pendingH);
}

void MainWindow::ComputeTitleRects() {
    const float W = (float)m_r.Width();
    const float h = Dp(theme::TitleH);
    const float bw = Dp(theme::TitleBtnW);
    const float gap = Dp(4);
    m_rcClose = Rc(W - bw, 0, W, h);
    m_rcMax = Rc(m_rcClose.left - gap - bw, 0, m_rcClose.left - gap, h);
    m_rcMin = Rc(m_rcMax.left - gap - bw, 0, m_rcMax.left - gap, h);
    m_rcFull = Rc(m_rcMin.left - gap - bw, 0, m_rcMin.left - gap, h);
    m_rcMenu = Rc(0, 0, Dp(theme::MenuBtnW), h);
    m_rcTitle = Rc(0, 0, W, h);
    m_rcTabsArea = Rc(m_rcMenu.right + Dp(6), 0, m_rcFull.left - Dp(8), h);
}

bool MainWindow::Render(double now) {
    if (m_inRender) {           // ic ice cagri: siradaki kareye birak
        m_dirty = true;
        return true;
    }
    ApplyPendingResize();
    if (!m_r.Begin()) return false;
    m_inRender = true;
    const float s = m_r.Scale();
    w::SetScale(s);
    m_ui.Begin(m_in, now);
    ComputeTitleRects();

    const float W = (float)m_r.Width(), H = (float)m_r.Height();
    const float titleH = Dp(theme::TitleH);
    const D2D1_RECT_F content = m_fullscreen ? Rc(0, 0, W, H) : Rc(0, titleH, W, H);
    const float chrome = (m_view == View::Reader && ActiveReader()->IsOpen()) ? ActiveReader()->ChromeAlpha() : 1.0f;
    const float titleAlpha = m_fullscreen ? (m_drawerT > 0.0f ? 1.0f : chrome) : 1.0f;

    if (m_drawerT > 0.0f || m_challengeModalOpen) m_ui.Occlude(content);
    if ((m_fullscreen && titleAlpha > 0.05f) || m_challengeModalOpen) m_ui.Occlude(m_rcTitle);

    m_library.SetCategoryFilter(m_catState[0], m_catState[1], m_catState[2]);
    switch (m_view) {
    case View::Library:
        m_library.Draw(m_r, m_ui, content, m_set, m_lib, *this, now);
        break;
    case View::Settings:
        m_settingsView.Draw(m_r, m_ui, content, m_set, m_lib, *this);
        break;
    case View::Reader:
        if (m_activeTab > 0 && m_activeTab < (int)m_tabs.size() && m_tabs[m_activeTab].failed) {
            DrawReaderTabLoading(content, m_tabs[m_activeTab], now);
        } else if (ActiveReader() && ActiveReader()->IsOpen()) {
            ActiveReader()->Draw(m_r, m_ui, content, now);
        } else if (m_activeTab > 0 && m_activeTab < (int)m_tabs.size()) {
            DrawReaderTabLoading(content, m_tabs[m_activeTab], now);
        } else {
            m_library.Draw(m_r, m_ui, content, m_set, m_lib, *this, now);
        }
        break;
    case View::Browser: {
        const float barH = Dp(42.0f);
        const D2D1_RECT_F barRc = Rc(content.left, content.top, content.right, content.top + barH);
        m_browserView.DrawBar(m_r, m_ui, barRc, now);
        const D2D1_RECT_F webRc = Rc(content.left, barRc.bottom, content.right, content.bottom);
        m_browserView.UpdateBounds(webRc);
        break;
    }
    }
    m_ui.ClearOcclusion();

    if (m_drawerT > 0.0f) DrawDrawer(content, now);
    if (!m_fullscreen || titleAlpha > 0.01f) DrawTitleBar(m_rcTitle, titleAlpha);
    if (m_challengeModalOpen) {
        DrawChallengeModal(content, now);
    } else if (m_view != View::Browser) {
        m_browserView.Show(false);
    }
    DrawToast();
    DrawTooltip(now);

    m_ui.End();
    m_r.End();
    m_in.pressed = false;
    m_in.released = false;
    m_inRender = false;
    return true;
}

void MainWindow::DrawTitleBar(const D2D1_RECT_F& bar, float a) {
    const float s = m_r.Scale();
    m_r.Fill(bar, theme::Panel, (m_fullscreen ? 0.92f : 1.0f) * a);
    m_r.Line(bar.left, bar.bottom - 0.5f, bar.right, bar.bottom - 0.5f, theme::Border, 1.0f, 0.30f * a);

    // Menu butonu
    if (w::IconButton(m_r, m_ui, Id("title.menu"), RcInset(m_rcMenu, Dp(4), Dp(4)), Icon::Menu, Dp(20), T(S::Menu),
                      m_drawerOpen, true, a, Dp(8))) {
        m_drawerOpen = !m_drawerOpen;
    }

    // Sekmeler Alani (Tabs Area)
    m_rcTabButtons.clear();
    m_rcTabCloses.clear();

    const float tabH = bar.bottom - bar.top - Dp(6);
    const float tabY = bar.top + Dp(3);
    float curX = m_rcTabsArea.left;
    const float availW = m_rcTabsArea.right - m_rcTabsArea.left;

    const size_t numTabs = m_tabs.size();
    const float tab0W = Dp(146.0f);
    float otherW = Dp(165.0f);
    if (numTabs > 1) {
        float remainingW = availW - tab0W - Dp(4) * (float)numTabs;
        float idealW = remainingW / (float)(numTabs - 1);
        if (idealW < otherW) otherW = std::max(Dp(75.0f), idealW);
    }

    for (size_t i = 0; i < numTabs; ++i) {
        const bool isTab0 = (i == 0);
        const float tw = isTab0 ? tab0W : otherW;
        if (curX + tw > m_rcTabsArea.right + Dp(2)) break;

        const D2D1_RECT_F tabRc = RcWH(curX, tabY, tw, tabH);
        m_rcTabButtons.push_back(tabRc);

        const bool isActive = ((int)i == m_activeTab && m_view != View::Settings);
        const uint32_t tid = Id("tab.btn", (uint32_t)i);
        const bool tabClicked = m_ui.Button(tid, tabRc);

        if (isActive) {
            m_r.FillRound(tabRc, Dp(6), theme::Elevated, 1.0f * a);
            m_r.FillRound(Rc(tabRc.left + Dp(8), tabRc.bottom - Dp(2.5f), tabRc.right - Dp(8), tabRc.bottom), Dp(1), theme::Accent, 1.0f * a);
        } else if (m_ui.Pressed(tid)) {
            m_r.FillRound(tabRc, Dp(6), theme::Press, theme::PressAlpha * a);
        } else if (m_ui.Hot(tid)) {
            m_r.FillRound(tabRc, Dp(6), theme::White, 0.05f * a);
        }

        if(isTab0){
            m_r.FillRound(tabRc,Dp(9),theme::ReadTone,(isActive?1.0f:0.65f)*a);
            m_r.StrokeRound(tabRc,Dp(9),theme::Accent,Dp(1),isActive?0.8f*a:0.3f*a);
        }
        Icon tabIcon = isTab0 ? Icon::Bookshelf : Icon::Library;
        if (m_tabs[i].type == TabType::Reader) tabIcon = Icon::Library;
        else if (m_tabs[i].type == TabType::Browser) tabIcon = Icon::Globe;

        const float iconSz = Dp(15.0f);
        const D2D1_RECT_F iconRc = RcWH(tabRc.left + Dp(7), tabRc.top + (tabH - iconSz) * 0.5f, iconSz, iconSz);
        DrawIcon(m_r, tabIcon, iconRc, iconSz, isActive ? theme::Accent : theme::TextDim, a);

        D2D1_RECT_F closeRc{};
        bool closeClicked = false;
        if (!isTab0 && m_tabs[i].canClose) {
            const float closeSz = Dp(16.0f);
            closeRc = RcWH(tabRc.right - closeSz - Dp(5), tabRc.top + (tabH - closeSz) * 0.5f, closeSz, closeSz);
            m_rcTabCloses.push_back(closeRc);

            const uint32_t cid = Id("tab.close", (uint32_t)i);
            closeClicked = m_ui.Button(cid, closeRc);
            if (m_ui.Hot(cid)) {
                m_r.FillRound(closeRc, Dp(4), theme::Danger, 0.85f * a);
                DrawIcon(m_r, Icon::Close, closeRc, Dp(11.0f), theme::White, a);
            } else {
                DrawIcon(m_r, Icon::Close, closeRc, Dp(11.0f), theme::TextDim, 0.8f * a);
            }
        } else {
            m_rcTabCloses.push_back(D2D1_RECT_F{});
        }

        const float textLeft = tabRc.left + Dp(26.0f);
        const float textRight = (!isTab0 && m_tabs[i].canClose) ? (closeRc.left - Dp(3.0f)) : (tabRc.right - Dp(6.0f));
        if (textRight > textLeft + Dp(10.0f)) {
            const D2D1_RECT_F textRc = Rc(textLeft, tabRc.top, textRight, tabRc.bottom);
            m_r.Text(m_tabs[i].title, textRc, isActive ? theme::Text : theme::TextDim, Dp(12.0f),
                     Renderer::Align::Left, isActive ? Renderer::Weight::Semibold : Renderer::Weight::Normal, a);
        }

        if (closeClicked) {
            CloseTab((int)i);
            break;
        } else if (tabClicked) {
            ActivateTab((int)i);
        }

        curX += tw + Dp(3.0f);
    }

    if (w::IconButton(m_r, m_ui, Id("title.full"), m_rcFull, Icon::FitWindow, Dp(18), m_fullscreen ? i18n::Literal(L"Tam ekrandan çık (F11)") : i18n::Literal(L"Tam ekran (F11)"), m_fullscreen, true, a, Dp(6))) ToggleFullscreen();
    // Kucult / gizle / kapat
    if (w::IconButton(m_r, m_ui, Id("title.min"), m_rcMin, Icon::Minimize, Dp(18), T(S::Minimize), false, true, a, Dp(6))) {
        Post(Cmd::Minimize);
    }
    if (w::IconButton(m_r, m_ui, Id("title.max"), m_rcMax, IsZoomed(m_hwnd) ? Icon::Restore : Icon::Maximize, Dp(19), IsZoomed(m_hwnd) ? i18n::Literal(L"Önceki boyuta dön") : i18n::Literal(L"Pencereyi büyüt"), false, true, a, Dp(6))) {
        Post(Cmd::Maximize);
    }
    const uint32_t closeId = Id("title.close");
    const bool closeClicked = m_ui.Button(closeId, m_rcClose);
    if (m_ui.Pressed(closeId)) m_r.FillRound(m_rcClose, Dp(6), theme::Danger, 0.75f * a);
    else if (m_ui.Hot(closeId)) m_r.FillRound(m_rcClose, Dp(6), theme::Danger, 0.95f * a);
    DrawIcon(m_r, Icon::Ghost, m_rcClose, Dp(23), theme::Text, a);
    m_ui.Tip(closeId, T(S::Close), m_rcClose);
    if (closeClicked) PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
    (void)s;
}

void MainWindow::DrawDrawer(const D2D1_RECT_F& content, double now) {
    (void)now;
    const float t = EaseOut(m_drawerT);
    const float dw = std::min(Dp(theme::DrawerW), RcW(content) - Dp(40));
    const float x0 = content.left - dw * (1.0f - t);
    const D2D1_RECT_F panel = Rc(x0, content.top, x0 + dw, content.bottom);

    // Karartma: disina tiklayinca kapanir.
    m_r.Fill(content, theme::Black, 0.40f * t);
    m_ui.Occlude(panel);
    if (m_ui.Button(Id("drawer.dim"), content)) m_drawerOpen = false;
    m_ui.ClearOcclusion();

    m_r.Fill(panel, theme::Panel, 1.0f);
    m_r.Line(panel.right - 0.5f, panel.top, panel.right - 0.5f, panel.bottom, theme::Border, 1.0f, 0.35f);

    struct Item { Icon icon; S label; int action; S soon; };
    const Item items[] = {
        { Icon::Library,  S::Library,       0, S::Count_ },
        { Icon::Search,   S::Search,        1, S::Count_ },
        { Icon::Download, S::Downloads,     2, S::Count_ },
        { Icon::Star,     S::Bookmarks,     3, S::Count_ },
        { Icon::Folder,   S::OpenLocal,     4, S::Count_ },
        { Icon::Globe,    S::Sites,         5, S::Count_ },
        { Icon::Sliders,  S::SettingsTitle, 6, S::Count_ },
        { Icon::Info,     S::About,         7, S::Count_ },
    };
    const float rowH = Dp(theme::DrawerRowH);
    float y = panel.top + Dp(10);
    for (const Item& it : items) {
        const D2D1_RECT_F row = Rc(panel.left + Dp(8), y, panel.right - Dp(8), y + rowH);
        const uint32_t id = Id("drawer.item", (uint32_t)it.action);
        const bool clicked = m_ui.Button(id, row);
        const bool selected = (it.action == 0 && (m_view == View::Library || m_view == View::Reader)) ||
                              (it.action == 6 && m_view == View::Settings);
        if (selected) {
            m_r.FillRound(row, Dp(8), theme::Elevated, 1.0f);
            m_r.FillRound(Rc(row.left, row.top + Dp(10), row.left + Dp(3), row.bottom - Dp(10)), Dp(1.5f), theme::Accent, 1.0f);
        } else if (m_ui.Pressed(id)) {
            m_r.FillRound(row, Dp(8), theme::Press, theme::PressAlpha);
        } else if (m_ui.Hot(id)) {
            m_r.FillRound(row, Dp(8), theme::White, 0.06f);
        }
        const bool soon = it.soon != S::Count_;
        DrawIcon(m_r, it.icon, RcWH(row.left + Dp(10), row.top, Dp(24), rowH), Dp(20),
                 soon ? theme::TextMuted : theme::TextDim);
        m_r.Text(T(it.label), Rc(row.left + Dp(44), row.top, row.right - Dp(70), row.bottom),
                 soon ? theme::TextMuted : theme::Text, Dp(14), Renderer::Align::Left,
                 selected ? Renderer::Weight::Semibold : Renderer::Weight::Normal);
        if (soon) {
            const std::wstring tag = T(S::SoonTag);
            const float tw = m_r.MeasureText(tag, Dp(11)) + Dp(14);
            const D2D1_RECT_F pill = RcWH(row.right - Dp(8) - tw, row.top + (rowH - Dp(20)) * 0.5f, tw, Dp(20));
            m_r.FillRound(pill, Dp(10), theme::Elevated, 1.0f);
            m_r.Text(tag, pill, theme::TextMuted, Dp(11), Renderer::Align::Center);
        }
        if (clicked) {
            switch (it.action) {
            case 0: SetView(View::Library); m_library.SetTab(0); break;
            case 1: SetView(View::Library); m_library.SetTab(0); m_library.FocusSearch(); break;
            case 2: m_drawerOpen = false; OpenDownloadsFolder(); break;
            case 3: SetView(View::Library); m_library.SetTab(2); break;
            case 4: m_drawerOpen = false; OpenFolderDialog(); break;
            case 5: SetView(View::Library); m_library.SetTab(3); break;
            case 6: m_settingsView.SetTab(0); SetView(View::Settings); break;
            case 7: m_settingsView.SetTab(8); SetView(View::Settings); break;
            default: Toast(T(it.soon)); break;
            }
        }
        y += rowH + Dp(2);
    }

    // Kategoriler
    y += Dp(14);
    m_r.Line(panel.left + Dp(16), y, panel.right - Dp(16), y, theme::Border, 1.0f, 0.25f);
    y += Dp(14);
    m_r.Text(T(S::Categories), Rc(panel.left + Dp(18), y, panel.right - Dp(16), y + Dp(18)), theme::TextMuted, Dp(11),
             Renderer::Align::Left, Renderer::Weight::Semibold);
    y += Dp(24);
    const S cats[] = { S::CatNew, S::CatUnread, S::CatOneshots };
    if (m_drawerCatRev != m_lib.Revision()) {
        m_lib.GetStandardCategoryCounts(m_cachedCatCounts[0], m_cachedCatCounts[1], m_cachedCatCounts[2]);
        m_drawerCatRev = m_lib.Revision();
    }
    for (int i = 0; i < 3; ++i) {
        const D2D1_RECT_F row = Rc(panel.left + Dp(8), y, panel.right - Dp(8), y + Dp(34));
        const uint32_t id = Id("drawer.cat", (uint32_t)i);
        if (m_ui.Button(id, row)) {
            m_catState[i] = (m_catState[i] + 1) % 3;
            m_dirty = true;
        }
        if (m_ui.Hot(id)) m_r.FillRound(row, Dp(8), theme::White, 0.06f);
        const float cx = row.left + Dp(22), cy = (row.top + row.bottom) * 0.5f;
        // Uc durum: dahil (dolu), haric (carpili halka), umursama (bos halka)
        if (m_catState[i] == 0) m_r.Disc(cx, cy, Dp(5), theme::Accent);
        else {
            m_r.Ring(cx, cy, Dp(5), theme::TextMuted, Dp(1.4f));
            if (m_catState[i] == 1) m_r.Line(cx - Dp(3), cy + Dp(3), cx + Dp(3), cy - Dp(3), theme::TextMuted, Dp(1.4f));
        }
        m_r.Text(T(cats[i]), Rc(row.left + Dp(44), row.top, row.right - Dp(40), row.bottom), theme::TextDim, Dp(13.5f));
        wchar_t cntBuf[32];
        swprintf_s(cntBuf, L"%d", m_cachedCatCounts[i]);
        m_r.Text(cntBuf, Rc(row.right - Dp(40), row.top, row.right - Dp(12), row.bottom), theme::TextMuted, Dp(12.5f),
                 Renderer::Align::Right, Renderer::Weight::Normal, 1.0f, false, false, true);
        y += Dp(36);
    }
    {
        const D2D1_RECT_F row = Rc(panel.left + Dp(8), y, panel.right - Dp(8), y + Dp(34));
        const uint32_t id = Id("drawer.addcat");
        if (m_ui.Button(id, row)) Toast(i18n::Literal(L"Kategori filtreleri çekmeceden ayarlanabilir."));
        if (m_ui.Hot(id)) m_r.FillRound(row, Dp(8), theme::White, 0.06f);
        DrawIcon(m_r, Icon::Plus, RcWH(row.left + Dp(10), row.top, Dp(24), Dp(34)), Dp(16), theme::TextMuted);
        m_r.Text(T(S::AddCategory), Rc(row.left + Dp(44), row.top, row.right - Dp(12), row.bottom), theme::TextMuted, Dp(13.5f));
    }

    // Alt: son tarama
    const float by = panel.bottom - Dp(96);
    if (by > y + Dp(40)) {
        m_r.Line(panel.left + Dp(16), by, panel.right - Dp(16), by, theme::Border, 1.0f, 0.25f);
        m_r.Text(T(S::LastScanNever), Rc(panel.left + Dp(18), by + Dp(12), panel.right - Dp(16), by + Dp(34)),
                 theme::TextMuted, Dp(12.5f));
        if (w::TextButton(m_r, m_ui, Id("drawer.scan"), Rc(panel.left + Dp(16), by + Dp(42), panel.left + Dp(150), by + Dp(76)),
                          T(S::ScanNow), false)) {
            CheckUpdatesNow();
        }
    }
}

void MainWindow::DrawReaderTabLoading(const D2D1_RECT_F& area, const AppTab& tab, double now) {
    const float s = m_r.Scale();
    m_r.Fill(area, theme::Ground, 1.0f);

    const float cardW = std::min(520.0f * s, RcW(area) - 40.0f * s);
    const float cardH = 260.0f * s;
    const D2D1_RECT_F card = RcWH((RcW(area) - cardW) * 0.5f, (RcH(area) - cardH) * 0.5f, cardW, cardH);

    m_r.FillRound(card, 16.0f * s, theme::Elevated, 1.0f);
    m_r.StrokeRound(card, 16.0f * s, theme::Border, 1.5f * s, 0.7f);

    // Manga ikonu kutusu
    const float iconSz = 50.0f * s;
    const D2D1_RECT_F iconBox = RcWH(card.left + (cardW - iconSz) * 0.5f, card.top + 22.0f * s, iconSz, iconSz);
    m_r.FillRound(iconBox, 14.0f * s, tab.failed ? theme::Danger : theme::Active, 0.16f);
    m_r.StrokeRound(iconBox, 14.0f * s, tab.failed ? theme::Danger : theme::Active, 1.0f * s, 0.40f);
    DrawIcon(m_r, tab.failed ? Icon::Close : Icon::Library, iconBox, 26.0f * s, tab.failed ? theme::Danger : theme::Accent);

    // Manga Basligi
    float y = iconBox.bottom + 12.0f * s;
    std::wstring mTitle = tab.mangaTitle.empty() ? L"Manga" : tab.mangaTitle;
    m_r.Text(mTitle, Rc(card.left + 20.0f * s, y, card.right - 20.0f * s, y + 26.0f * s), theme::Text, 17.0f * s,
             Renderer::Align::Center, Renderer::Weight::Bold);
    y += 28.0f * s;

    // Alt baslik (Bolum ve Kaynak)
    std::wstring sub = tab.chapterTitle.empty() ? i18n::Literal(L"Bölüm Hazırlanıyor") : tab.chapterTitle;
    if (!tab.mirror.empty()) sub += L"  ·  " + FromUtf8(tab.mirror);
    m_r.Text(sub, Rc(card.left + 20.0f * s, y, card.right - 20.0f * s, y + 20.0f * s), theme::TextMuted, 13.0f * s,
             Renderer::Align::Center);
    y += 26.0f * s;

    if (tab.failed) {
        // Hata Durumu
        std::wstring errMsg = tab.statusMessage.empty() ? i18n::Literal(L"Bölüm yüklenemedi. Sunucu yanıt vermiyor veya koruma devrede.") : tab.statusMessage;
        m_r.Text(errMsg, Rc(card.left + 20.0f * s, y, card.right - 20.0f * s, y + 22.0f * s), theme::Danger, 12.5f * s,
                 Renderer::Align::Center);
        y += 30.0f * s;

        const float btnW = 125.0f * s;
        const float btnH = 34.0f * s;
        const float gap = 8.0f * s;
        const float totalBtnW = btnW * 3.0f + gap * 2.0f;
        const float btnX0 = card.left + (cardW - totalBtnW) * 0.5f;

        const D2D1_RECT_F retryRc = RcWH(btnX0, y, btnW, btnH);
        const D2D1_RECT_F cfRc = RcWH(retryRc.right + gap, y, btnW, btnH);
        const D2D1_RECT_F closeRc = RcWH(cfRc.right + gap, y, btnW, btnH);

        if (w::TextButton(m_r, m_ui, Id("tab.load.retry"), retryRc, i18n::Literal(L"🔄 Yeniden Dene"), true)) {
            std::string mirror = tab.mirror;
            std::string mangaUrl = tab.mangaUrl;
            std::wstring title = tab.mangaTitle;
            std::string chapUrl = tab.chapterUrl;
            int curActive = m_activeTab;
            if (curActive > 0 && curActive < (int)m_tabs.size()) {
                m_tabs[curActive].failed = false;
                m_tabs[curActive].loading = true;
                m_tabs[curActive].statusMessage = i18n::Literal(L"Yeniden bağlanılıyor...");
                OpenOnlineManga(mirror, mangaUrl, title, chapUrl);
            }
        }
        if (w::TextButton(m_r, m_ui, Id("tab.load.cf"), cfRc, i18n::Literal(L"🛡️ Doğrula"), false)) {
            std::string targetUrl = tab.chapterUrl;
            if (targetUrl.empty() || targetUrl == "__FIRST__" || targetUrl == "__LAST__") {
                if (!tab.chapters.empty()) {
                    if (targetUrl == "__FIRST__") targetUrl = tab.chapters.back().url;
                    else targetUrl = tab.chapters.front().url;
                } else {
                    targetUrl = tab.mangaUrl;
                }
            }
            if (targetUrl.rfind("/", 0) == 0 && !tab.mangaUrl.empty()) {
                size_t p = tab.mangaUrl.find("://");
                if (p != std::string::npos) {
                    size_t p2 = tab.mangaUrl.find('/', p + 3);
                    std::string origin = (p2 != std::string::npos) ? tab.mangaUrl.substr(0, p2) : tab.mangaUrl;
                    targetUrl = origin + targetUrl;
                }
            }
            ShowChallengeVerification(targetUrl, tab.mangaTitle, "images");
        }
        m_ui.Tip(Id("tab.load.cf"), i18n::Literal(L"Cloudflare Robot Doğrulamasını Aç"), cfRc);

        if (w::TextButton(m_r, m_ui, Id("tab.load.close"), closeRc, i18n::Literal(L"✕ Kapat"), false)) {
            CloseTab(m_activeTab);
        }
    } else {
        // Yukleniyor Durumu
        std::wstring stMsg = tab.statusMessage.empty() ? i18n::Literal(L"⚡ Bölüm taranıyor ve sayfalar hazırlanıyor...") : tab.statusMessage;
        m_r.Text(stMsg, Rc(card.left + 20.0f * s, y, card.right - 20.0f * s, y + 20.0f * s), theme::Accent, 13.0f * s,
                 Renderer::Align::Center, Renderer::Weight::Semibold);
        y += 24.0f * s;

        // Animasyonlu pulse bar
        const float barW = 260.0f * s;
        const float barH = 5.0f * s;
        const D2D1_RECT_F barRc = RcWH(card.left + (cardW - barW) * 0.5f, y, barW, barH);
        m_r.FillRound(barRc, barH * 0.5f, theme::Panel, 1.0f);

        float pulsePos = (float)(0.5 + 0.5 * sin(now * 3.5));
        const float pulseW = 70.0f * s;
        const float px = barRc.left + (barW - pulseW) * pulsePos;
        const D2D1_RECT_F pulseRc = RcWH(px, barRc.top, pulseW, barH);
        m_r.FillRound(pulseRc, barH * 0.5f, theme::Accent, 0.9f);
        y += 16.0f * s;

        m_r.Text(i18n::Literal(L"İnternet bağlantınıza göre birkaç saniye sürebilir..."), Rc(card.left + 20.0f * s, y, card.right - 20.0f * s, y + 18.0f * s),
                 theme::TextMuted, 11.5f * s, Renderer::Align::Center);
        y += 20.0f * s;

        const float cfBtnW = 150.0f * s;
        const float closeBtnW = 110.0f * s;
        const float btnH = 30.0f * s;
        const float gap = 10.0f * s;
        const float totalBtnW = cfBtnW + closeBtnW + gap;
        const float btnX0 = card.left + (cardW - totalBtnW) * 0.5f;

        const D2D1_RECT_F cfRc = RcWH(btnX0, y, cfBtnW, btnH);
        const D2D1_RECT_F closeRc = RcWH(cfRc.right + gap, y, closeBtnW, btnH);

        if (w::TextButton(m_r, m_ui, Id("tab.load.cf"), cfRc, i18n::Literal(L"🛡️ Doğrulamayı Aç"), false)) {
            std::string targetUrl = tab.chapterUrl;
            if (targetUrl.empty() || targetUrl == "__FIRST__" || targetUrl == "__LAST__") {
                if (!tab.chapters.empty()) {
                    if (targetUrl == "__FIRST__") targetUrl = tab.chapters.back().url;
                    else targetUrl = tab.chapters.front().url;
                } else {
                    targetUrl = tab.mangaUrl;
                }
            }
            if (targetUrl.rfind("/", 0) == 0 && !tab.mangaUrl.empty()) {
                size_t p = tab.mangaUrl.find("://");
                if (p != std::string::npos) {
                    size_t p2 = tab.mangaUrl.find('/', p + 3);
                    std::string origin = (p2 != std::string::npos) ? tab.mangaUrl.substr(0, p2) : tab.mangaUrl;
                    targetUrl = origin + targetUrl;
                }
            }
            ShowChallengeVerification(targetUrl, tab.mangaTitle, "images");
        }
        m_ui.Tip(Id("tab.load.cf"), i18n::Literal(L"Sayfa Cloudflare robot kontrolüne takıldıysa doğrulamayı aç"), cfRc);

        if (w::TextButton(m_r, m_ui, Id("tab.load.cancel"), closeRc, i18n::Literal(L"✕ Kapat"), false)) {
            CloseTab(m_activeTab);
        }
    }
}

void MainWindow::DrawToast() {
    if (m_toastT <= 0.0f || m_toastText.empty()) return;
    const float total = m_toastTotal > 0.0f ? m_toastTotal : theme::ToastTime;
    const float age = total - m_toastT;
    float a = 1.0f;
    if (age < 0.15f) a = age / 0.15f;
    if (m_toastT < 0.3f) a = m_toastT / 0.3f;

    const float W = (float)m_r.Width(), H = (float)m_r.Height();
    // +2 dp pay: olculen genislikle cizimdeki satir genisligi kesirde ayrisinca
    // DirectWrite metni "..." ile kesmesin.
    const float tw = std::min(W - Dp(40), m_r.MeasureText(m_toastText, Dp(13.5f)) + Dp(38));
    const float th = Dp(38);
    float bottom = H - Dp(40);
    if (m_view == View::Reader && ActiveReader()->IsOpen()) bottom = H - Dp(theme::HudMargin + theme::HudH + 18);
    const D2D1_RECT_F rc = RcWH((W - tw) * 0.5f, bottom - th, tw, th);
    m_r.FillRound(rc, th * 0.5f, theme::Panel, theme::HudAlpha * a);
    m_r.StrokeRound(rc, th * 0.5f, theme::Border, 1.0f, theme::BorderAlpha * a);
    m_r.Text(m_toastText, RcInset(rc, Dp(18), 0), theme::Text, Dp(13.5f), Renderer::Align::Center,
             Renderer::Weight::Normal, a);
}

void MainWindow::DrawTooltip(double now) {
    m_tipShown = false;
    const std::wstring& text = m_ui.TipText();
    if (text.empty() || !m_ui.HotStable() || (now - m_ui.HotSince()) < theme::TipDelay || m_in.down) return;
    const D2D1_RECT_F& an = m_ui.TipAnchor();
    const float W = (float)m_r.Width(), H = (float)m_r.Height();
    const float tw = std::min(W - Dp(16), m_r.MeasureText(text, Dp(12.5f)) + Dp(20));
    const float th = Dp(28);
    const bool above = an.bottom > H * 0.5f;
    const float y = above ? an.top - th - Dp(8) : an.bottom + Dp(6);
    const float x = std::clamp((an.left + an.right) * 0.5f - tw * 0.5f, Dp(8), W - tw - Dp(8));
    const D2D1_RECT_F rc = RcWH(x, y, tw, th);
    m_r.FillRound(rc, Dp(7), theme::Elevated, 0.97f);
    m_r.StrokeRound(rc, Dp(7), theme::Border, 1.0f, theme::BorderAlpha);
    m_r.Text(text, RcInset(rc, Dp(10), 0), theme::Text, Dp(12.5f), Renderer::Align::Center);
    m_tipShown = true;
}

// ------------------------------------------------------------ girdi ---------

LRESULT MainWindow::HitTest(int x, int y) {
    if (m_fullscreen) return HTCLIENT;
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    const int b = (int)Dp(theme::ResizeGrip);
    if (!IsZoomed(m_hwnd)) {
        const bool L = x < b, R = x >= rc.right - b;
        const bool T = y < b, B = y >= rc.bottom - b;
        if (T && L) return HTTOPLEFT;
        if (T && R) return HTTOPRIGHT;
        if (B && L) return HTBOTTOMLEFT;
        if (B && R) return HTBOTTOMRIGHT;
        if (L) return HTLEFT;
        if (R) return HTRIGHT;
        if (T) return HTTOP;
        if (B) return HTBOTTOM;
    }
    if (y < (int)Dp(theme::TitleH)) {
        const float fx = (float)x, fy = (float)y;
        if (RcHit(m_rcMenu, fx, fy) || RcHit(m_rcFull, fx, fy) || RcHit(m_rcMin, fx, fy) || RcHit(m_rcMax, fx, fy) || RcHit(m_rcClose, fx, fy))
            return HTCLIENT;
        for (const auto& btn : m_rcTabButtons) {
            if (RcHit(btn, fx, fy)) return HTCLIENT;
        }
        for (const auto& btn : m_rcTabCloses) {
            if (RcHit(btn, fx, fy)) return HTCLIENT;
        }
        return HTCAPTION;
    }
    return HTCLIENT;
}

void MainWindow::ResetPointer() {
    ActiveReader()->OnMouseUp();
    ActiveReader()->OnMouseLeave();
    m_in.down = false;
    m_in.pressed = false;
    m_in.released = false;
    m_ui.CancelActive();
    m_dirty = true;
}

bool MainWindow::OnKey(WPARAM vk, bool repeat) {
    const bool ctrl = GetKeyState(VK_CONTROL) < 0;
    const bool shift = GetKeyState(VK_SHIFT) < 0;
    const double now = Now();
    if (m_view == View::Reader) ActiveReader()->WakeChrome(now);

    if (m_challengeModalOpen) {
        if (vk == VK_ESCAPE) {
            m_challengeModalOpen = false;
            m_browserView.CancelChallengeVerification();
            m_browserView.Show(false);
            if (m_activeTab > 0 && m_activeTab < (int)m_tabs.size()) {
                auto& tab = m_tabs[m_activeTab];
                if (tab.loading) {
                    tab.loading = false;
                    tab.failed = true;
                    tab.statusMessage = i18n::Literal(L"❌ Robot doğrulaması iptal edildi.");
                }
            }
            m_dirty = true;
            Invalidate();
            return true;
        }
    }

    // Focused editors receive text commands before app/reader letter shortcuts.
    if (!m_drawerOpen && !m_challengeModalOpen) {
        bool handled = false;
        if (m_view == View::Reader && ActiveReader()->IsOpen() && ActiveReader()->HasFocusedTextInput())
            handled = ActiveReader()->OnKey((UINT)vk, ctrl, shift, repeat);
        else if (m_view == View::Browser && m_browserView.HasFocusedTextInput())
            handled = m_browserView.OnKey((UINT)vk, ctrl, shift);
        else if (LibraryVisible() && m_library.HasFocusedTextInput())
            handled = m_library.OnKey((UINT)vk, ctrl, shift);
        if (handled) { m_dirty = true; return true; }
    }

    switch (vk) {
    case VK_F11: if (!repeat) DoToggleFullscreen(); return true;
    case VK_ESCAPE: if (!repeat) Back(); return true;
    case 'W':
        if (ctrl) {
            if (!repeat && m_activeTab > 0) {
                CloseTab(m_activeTab);
            }
            return true;
        }
        break;
    case VK_TAB:
        if (ctrl) {
            if (!repeat && m_tabs.size() > 1) {
                if (shift) {
                    int prev = (m_activeTab + (int)m_tabs.size() - 1) % (int)m_tabs.size();
                    ActivateTab(prev);
                } else {
                    int next = (m_activeTab + 1) % (int)m_tabs.size();
                    ActivateTab(next);
                }
            }
            return true;
        }
        break;
    case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
        if (ctrl) {
            if (!repeat) {
                int targetTab = (int)(vk - '1');
                if (targetTab >= 0 && targetTab < (int)m_tabs.size()) {
                    ActivateTab(targetTab);
                }
            }
            return true;
        }
        break;
    case 'O':
        if (ctrl) {
            if (!repeat) {
                if (shift) OpenArchiveDialog();
                else OpenFolderDialog();
            }
            return true;
        }
        break;
    case 'I': if (ctrl) { if (!repeat) ImportAmrDialog(); return true; } break;
    case 'Q': if (ctrl) { if (!repeat) QuitApp(); return true; } break;
    case VK_OEM_COMMA:
        if (ctrl) { if (!repeat) SetView(View::Settings); return true; }
        break;
    case 'L':
        if (!ctrl) {
            if (repeat) return true;
            m_set.lang = i18n::Next();
            LanguageChanged();
            SaveSettings();
            return true;
        }
        break;
    case 'F':
        if (ctrl) {
            if (!repeat && LibraryVisible()) {
                m_library.FocusSearch();
                m_dirty = true;
                return true;
            }
        } else if (m_view == View::Reader) {
            if (!repeat) DoToggleFullscreen();
            return true;
        }
        break;
    default: break;
    }
    if (m_view == View::Reader && ActiveReader()->IsOpen() && !m_drawerOpen) {
        if (ActiveReader()->OnKey((UINT)vk, ctrl, shift, repeat)) {
            m_dirty = true;
            return true;
        }
    }
    if (m_view == View::Browser && !m_drawerOpen) {
        if (m_browserView.OnKey((UINT)vk, ctrl, shift)) {
            m_dirty = true;
            return true;
        }
    }
    if (LibraryVisible() && !m_drawerOpen && m_library.OnKey((UINT)vk, ctrl, shift)) {
        m_dirty = true;
        return true;
    }
    return false;
}

LRESULT CALLBACK MainWindow::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = hwnd;
    } else if (msg == WM_NCDESTROY) {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        if (self) self->m_hwnd = nullptr;
        return DefWindowProcW(hwnd, msg, wp, lp);
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->WndProc(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MainWindow::WndProc(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == ActivateMessage()) {
        RestoreFromTray();
        return 0;
    }
    if (m_msgTaskbarCreated && msg == m_msgTaskbarCreated) {
        // Gezgin yeniden basladi: tepsi simgesi kayboldu, yeniden ekle.
        m_trayAdded = false;
        AddTray();
        return 0;
    }

    switch (msg) {
    case WM_NCCALCSIZE:
        if (wp == TRUE) {
            if (IsZoomed(m_hwnd) && !m_fullscreen) {
                auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
                const int cx = GetSystemMetricsForDpi(SM_CXSIZEFRAME, m_dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, m_dpi);
                const int cy = GetSystemMetricsForDpi(SM_CYSIZEFRAME, m_dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, m_dpi);
                p->rgrc[0].left += cx;
                p->rgrc[0].right -= cx;
                p->rgrc[0].top += cy;
                p->rgrc[0].bottom -= cy;
                // Otomatik gizlenen gorev cubugu: buyutulmus pencere o kenari
                // tamamen kaplarsa cubuk bir daha acilmaz. O kenardan 1 px birak
                // (Windows Terminal ve Chromium ile ayni yaklasim).
                APPBARDATA abd{ sizeof(abd) };
                if (SHAppBarMessage(ABM_GETSTATE, &abd) & ABS_AUTOHIDE) {
                    MONITORINFO mi{ sizeof(mi) };
                    GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
                    const UINT edges[] = { ABE_TOP, ABE_BOTTOM, ABE_LEFT, ABE_RIGHT };
                    for (UINT e : edges) {
                        APPBARDATA q{ sizeof(q) };
                        q.uEdge = e;
                        q.rc = mi.rcMonitor;
                        if (!SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &q)) continue;
                        if (e == ABE_TOP) p->rgrc[0].top += 1;
                        else if (e == ABE_BOTTOM) p->rgrc[0].bottom -= 1;
                        else if (e == ABE_LEFT) p->rgrc[0].left += 1;
                        else p->rgrc[0].right -= 1;
                    }
                }
            }
            return 0;
        }
        break;

    case WM_APP_CMD:
        RunCommand((Cmd)wp);
        return 0;

    case WM_ENTERSIZEMOVE:
        m_inSizeMove = true;
        return 0;

    case WM_EXITSIZEMOVE:
        m_inSizeMove = false;
        m_dirty = true;
        return 0;

    case WM_CAPTURECHANGED:
        if ((HWND)lp != m_hwnd && m_in.down) ResetPointer();
        return 0;

    case WM_NCHITTEST: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(m_hwnd, &pt);
        return HitTest(pt.x, pt.y);
    }

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = (LONG)Dp(720);
        mmi->ptMinTrackSize.y = (LONG)Dp(480);
        return 0;
    }

    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) {
            m_pendingW = LOWORD(lp);
            m_pendingH = HIWORD(lp);
            m_resizePending = true;
            m_dirty = true;
            // Cizim ortasinda (ic ice) gelen boyut degisimi bir sonraki kareye kalir.
            if (!m_inRender) ApplyPendingResize();
            // Kullanici kenardan surukleyip boyutlarken ana dongu donmez:
            // yalniz o durumda burada ciz.
            if (m_inSizeMove && !m_inRender && IsWindowVisible(m_hwnd)) {
                m_dirty = false;
                Render(Now());
            }
        }
        return 0;

    case WM_DPICHANGED: {
        m_dpi = HIWORD(wp);
        m_r.SetDpi(m_dpi);
        const RECT* r = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(m_hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        UpdateIcons();
        m_dirty = true;
        return 0;
    }

    case WM_TRAY:
        switch (LOWORD(lp)) {
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            RestoreFromTray();
            break;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowTrayMenu();
            break;
        default: break;
        }
        return 0;

    case WM_APP_DECODED:
        m_decodePosted = false;
        for (auto& tab : m_tabs) {
            if (tab.reader) tab.reader->OnDecoderSignal(m_r);
        }
        m_reader.OnDecoderSignal(m_r);
        m_dirty = true;
        return 0;

    case WM_APP_ONLINE_READY: {
        auto* pInfo = reinterpret_cast<OnlineReadyInfo*>(lp);
        if (pInfo) {
            int tidx = pInfo->tabIndex;
            if (tidx >= 0 && tidx < (int)m_tabs.size() && m_tabs[tidx].reader) {
                // Sadece indirme bu sekmeye aitse guncelle
                if (m_tabs[tidx].chapterDir.empty() || m_tabs[tidx].chapterDir == pInfo->cacheDir) {
                    m_tabs[tidx].loading = false;
                    m_tabs[tidx].failed = false;
                    m_tabs[tidx].statusMessage.clear();
                    m_tabs[tidx].chapterDir = pInfo->cacheDir;
                    if (!pInfo->chapterTitle.empty()) {
                        m_tabs[tidx].chapterTitle = CleanChapterTitle(pInfo->chapterTitle, m_tabs[tidx].chapterIndex);
                    }
                    m_tabs[tidx].title = FormatTabTitle(m_tabs[tidx].mirror, m_tabs[tidx].mangaTitle.empty() ? pInfo->seriesTitle : m_tabs[tidx].mangaTitle, m_tabs[tidx].chapterTitle, m_tabs[tidx].chapterIndex);
                    m_tabs[tidx].reader->SetSourceMirror(FromUtf8(m_tabs[tidx].mirror));
                    m_tabs[tidx].reader->Open(pInfo->cacheDir);
                    m_tabs[tidx].reader->SetCustomTitle(pInfo->seriesTitle, m_tabs[tidx].chapterTitle);
                    if (!m_tabs[tidx].mirror.empty() && !m_tabs[tidx].mangaUrl.empty()) {
                        std::string key = amr::MangaKey(m_tabs[tidx].mirror, m_tabs[tidx].mangaUrl, "en");
                        m_lib.MarkLastRead(key, m_tabs[tidx].chapterUrl, ToUtf8(m_tabs[tidx].chapterTitle));
                        m_library.SetDisplayDirty();
                    }
                    SaveSessionTabs();
                }
            }
            delete pInfo;
            m_dirty = true;
        }
        return 0;
    }

    case WM_APP_PAGE_READY: {
        auto* pProg = reinterpret_cast<PageReadyInfo*>(lp);
        if (pProg) {
            int tidx = pProg->tabIndex;
            if (tidx >= 0 && tidx < (int)m_tabs.size() && m_tabs[tidx].reader) {
                if (m_tabs[tidx].chapterDir.empty() || pProg->pagePath.rfind(m_tabs[tidx].chapterDir, 0) == 0) {
                    m_tabs[tidx].reader->OnPageDownloaded(pProg->pageIndex, pProg->totalPages, pProg->pagePath);
                    if (pProg->pageIndex == 0) {
                        if (!m_tabs[tidx].mirror.empty() && !m_tabs[tidx].mangaUrl.empty() && !m_tabs[tidx].chapterUrl.empty()) {
                            std::string key = amr::MangaKey(m_tabs[tidx].mirror, m_tabs[tidx].mangaUrl, "en");
                            m_lib.MarkLastRead(key, m_tabs[tidx].chapterUrl, ToUtf8(m_tabs[tidx].chapterTitle));
                            m_library.SetDisplayDirty();
                        }
                    }
                    DecoderSignaled();
                    m_dirty = true;
                }
            }
            delete pProg;
        }
        return 0;
    }

    case WM_APP_DISPATCH_UI: {
        std::vector<std::function<void()>> batch;
        {
            std::lock_guard<std::mutex> lock(m_uiDispatchMutex);
            batch.swap(m_uiDispatchQueue);
        }
        for (auto& fn : batch) {
            if (fn) fn();
        }
        return 0;
    }

    case WM_APP_ONLINE_FAIL: {
        int tidx = (int)wp;
        if (tidx <= 0 || tidx >= (int)m_tabs.size()) tidx = m_activeTab;
        if (tidx >= 0 && tidx < (int)m_tabs.size()) {
            m_tabs[tidx].loading = false;
            m_tabs[tidx].failed = true;
            m_tabs[tidx].statusMessage = i18n::Literal(L"Bölüm indirilemedi. Sunucu yanıt vermiyor veya koruma devrede.");
        }
        ToastFor(i18n::Literal(L"Bölüm yüklenemedi. Yeniden deneyin veya sekmeyi kapatın."), 3.5f);
        m_dirty = true;
        return 0;
    }

    case WM_APP_CHAPTERS_SCANNED: {
        auto* res = reinterpret_cast<ChapterScanResult*>(lp);
        if (res) {
            int tidx = res->tabIndex;
            if (tidx < 0 || tidx >= (int)m_tabs.size() || m_tabs[tidx].mangaUrl != res->mangaUrl) {
                for (size_t i = 0; i < m_tabs.size(); ++i) {
                    if (m_tabs[i].mangaUrl == res->mangaUrl) {
                        tidx = (int)i;
                        break;
                    }
                }
            }
            if (tidx >= 0 && tidx < (int)m_tabs.size()) {
                std::vector<Chapter> catalogue;
                for (const auto& ch : res->chapters) catalogue.push_back({ch.title, ch.url});
                const auto* series = m_lib.FindSeries(amr::MangaKey(m_tabs[tidx].mirror, res->mangaUrl, "en"));
                if (!series) series = m_lib.FindSeries(res->mangaUrl);
                if (series && !catalogue.empty()) StoreChapterCatalogue(series->key, catalogue);
                m_tabs[tidx].chapters = std::move(res->chapters);
                int foundIdx = -1;
                if (!m_tabs[tidx].chapterUrl.empty()) {
                    for (size_t ci = 0; ci < m_tabs[tidx].chapters.size(); ++ci) {
                        if (m_tabs[tidx].chapters[ci].url == m_tabs[tidx].chapterUrl) {
                            foundIdx = (int)ci;
                            break;
                        }
                    }
                }
                if (foundIdx >= 0) {
                    m_tabs[tidx].chapterIndex = foundIdx;
                } else if (res->targetIndex >= 0 && res->targetIndex < (int)m_tabs[tidx].chapters.size()) {
                    m_tabs[tidx].chapterIndex = res->targetIndex;
                }
                if (m_tabs[tidx].chapterIndex >= 0 && m_tabs[tidx].chapterIndex < (int)m_tabs[tidx].chapters.size()) {
                    std::wstring cleanCh = CleanChapterTitle(FromUtf8(m_tabs[tidx].chapters[m_tabs[tidx].chapterIndex].title), m_tabs[tidx].chapterIndex);
                    m_tabs[tidx].chapterTitle = cleanCh;
                    m_tabs[tidx].title = FormatTabTitle(m_tabs[tidx].mirror, m_tabs[tidx].mangaTitle, cleanCh, m_tabs[tidx].chapterIndex);
                    if (m_tabs[tidx].reader) {
                        m_tabs[tidx].reader->SetCustomTitle(m_tabs[tidx].mangaTitle, cleanCh);
                        m_tabs[tidx].reader->SetSourceMirror(FromUtf8(m_tabs[tidx].mirror));
                    }
                }
                SaveSessionTabs();
            }
            delete res;
            m_dirty = true;
        }
        return 0;
    }

    case WM_APP_NEW_CHAPTERS: {
        auto* notif = reinterpret_cast<engine::UpdateNotification*>(lp);
        if (notif) {
            FILETIME ft;
            GetSystemTimeAsFileTime(&ft);
            ULARGE_INTEGER uli;
            uli.LowPart = ft.dwLowDateTime;
            uli.HighPart = ft.dwHighDateTime;
            int64_t nowMs = (int64_t)((uli.QuadPart - 116444736000000000ull) / 10000ull);

            if (!notif->error.empty()) {
                m_lib.StoreUpdateError(notif->seriesKey, notif->error);
                m_library.SetUpdateError(notif->seriesKey, FromUtf8(notif->error));
            } else if (!notif->chapters.empty()) {
                if (m_lib.StoreChapters(notif->seriesKey, notif->chapters, nowMs)) {
                    m_library.CatalogueChanged(notif->seriesKey);
                    m_library.ClearUpdateError(notif->seriesKey);
                } else {
                    m_library.SetUpdateError(notif->seriesKey, i18n::Literal(L"Bölüm listesi kaydedilemedi"));
                }
            }
            m_library.SetDisplayDirty();
            if (notif->newChapterCount > 0) {
                ToastFor(notif->seriesTitle + i18n::Literal(L": Yeni bölüm! (") + FromUtf8(notif->latestChapName) + L")", 6.0f);

                if (m_trayAdded && m_set.bgNotify) {
                    NOTIFYICONDATAW nid{ sizeof(nid) };
                    nid.hWnd = m_hwnd;
                    nid.uID = kTrayId;
                    nid.uFlags = NIF_INFO;
                    nid.dwInfoFlags = NIIF_INFO;
                    wcsncpy_s(nid.szInfoTitle, T(S::AppName), _TRUNCATE);
                    std::wstring info = notif->seriesTitle + L": " + FromUtf8(notif->latestChapName);
                    wcsncpy_s(nid.szInfo, info.c_str(), _TRUNCATE);
                    Shell_NotifyIconW(NIM_MODIFY, &nid);
                }
            }
            delete notif;
            m_dirty = true;
        }
        return 0;
    }

    case WM_APP_SCAN_FINISHED: {
        ToastFor(i18n::Literal(L"✅ Manga denetimi tamamlandı."), 3.0f);
        m_library.SetDisplayDirty();
        m_dirty = true;
        return 0;
    }

    case WM_APP_COVER_READY: {
        m_library.SetDisplayDirty();
        m_dirty = true;
        return 0;
    }

    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) ActiveReader()->OnMouseLeave();
        else m_library.RecheckRecent();
        m_dirty = true;
        break;

    case WM_DEVICECHANGE:
        if (wp == 0x8000 /* DBT_DEVICEARRIVAL */ || wp == 0x8004 /* DBT_DEVICEREMOVECOMPLETE */) {
            m_library.RecheckRecent();
            m_dirty = true;
        }
        return TRUE;

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        m_dirty = true;
        break;

    case WM_MOUSEMOVE: {
        const float x = (float)GET_X_LPARAM(lp), y = (float)GET_Y_LPARAM(lp);
        m_in.mx = x;
        m_in.my = y;
        if (m_view == View::Reader && ActiveReader()->IsOpen()) ActiveReader()->OnMouseMove(x, y, Now());
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hwnd, 0 };
        TrackMouseEvent(&tme);
        m_dirty = true;
        return 0;
    }

    case WM_MOUSELEAVE:
        if (!m_in.down) {
            m_in.mx = m_in.my = -1.0f;
            ActiveReader()->OnMouseLeave();
            m_dirty = true;
        }
        return 0;

    case WM_MBUTTONUP: {
        const float x = (float)GET_X_LPARAM(lp), y = (float)GET_Y_LPARAM(lp);
        if (y < Dp(theme::TitleH)) {
            for (size_t i = 1; i < m_rcTabButtons.size(); ++i) {
                if (RcHit(m_rcTabButtons[i], x, y)) {
                    CloseTab((int)i);
                    m_dirty = true;
                    return 0;
                }
            }
        }
        break;
    }

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        const float x = (float)GET_X_LPARAM(lp), y = (float)GET_Y_LPARAM(lp);
        SetFocus(m_hwnd);
        m_in.mx = x;
        m_in.my = y;
        m_dirty = true;
        const bool overTitle = !m_fullscreen ? y < Dp(theme::TitleH)
                                             : (y < Dp(theme::TitleH) && ActiveReader()->ChromeAlpha() > 0.05f);
        const bool canvas = m_view == View::Reader && ActiveReader()->IsOpen() && !m_drawerOpen && !overTitle;
        if (msg == WM_LBUTTONDBLCLK && canvas) {
            if (!m_lastDownCanvas) {
                m_lastDownCanvas = true;
                return 0;
            }
            if (ActiveReader()->OnDoubleClick(x, y)) {
                m_ui.CancelActive();
                return 0;
            }
        }
        m_lastDownCanvas = canvas;
        SetCapture(m_hwnd);
        m_in.down = true;
        m_in.pressed = true;
        if (canvas) ActiveReader()->OnMouseDown(x, y, Now());
        return 0;
    }

    case WM_LBUTTONUP: {
        m_in.mx = (float)GET_X_LPARAM(lp);
        m_in.my = (float)GET_Y_LPARAM(lp);
        const bool wasDown = m_in.down;
        const bool dragged = ActiveReader()->Dragging() && ActiveReader()->DragMoved();
        ActiveReader()->OnMouseUp();
        m_in.down = false;
        m_in.released = wasDown;
        if (GetCapture() == m_hwnd) ReleaseCapture();
        if (dragged) m_ui.CancelActive();
        m_dirty = true;
        return 0;
    }

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && m_view == View::Reader && ActiveReader()->Dragging() && ActiveReader()->DragMoved()) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            return TRUE;
        }
        break;

    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(m_hwnd, &pt);
        const float notches = (float)GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA;
        const bool ctrl = (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) != 0;
        const bool shift = (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) != 0 || msg == WM_MOUSEHWHEEL;
        if (m_view == View::Reader && ActiveReader()->IsOpen() && !m_drawerOpen) {
            ActiveReader()->WakeChrome(Now());
            ActiveReader()->OnWheel(msg == WM_MOUSEHWHEEL ? -notches : notches, ctrl, shift, (float)pt.x, (float)pt.y);
        } else if (LibraryVisible() && !m_drawerOpen && msg == WM_MOUSEWHEEL && !ctrl) {
            m_library.OnWheel(notches);
        }
        m_dirty = true;
        return 0;
    }

    case WM_KEYDOWN:
        if (OnKey(wp, (lp & (1 << 30)) != 0)) return 0;
        break;

    case WM_CHAR:
        if (m_view == View::Reader && ActiveReader() && ActiveReader()->IsOpen() && !m_drawerOpen) {
            if (ActiveReader()->OnChar((wchar_t)wp)) {
                m_dirty = true;
                return 0;
            }
        }
        if (m_view == View::Browser && !m_drawerOpen) {
            if (m_browserView.OnChar((wchar_t)wp)) {
                m_dirty = true;
                return 0;
            }
        }
        if (LibraryVisible() && !m_drawerOpen) {
            if (m_library.OnChar((wchar_t)wp)) {
                m_dirty = true;
                return 0;
            }
        }
        break;

    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp;
        wchar_t path[MAX_PATH * 4]{};
        if (DragQueryFileW(drop, 0, path, (UINT)std::size(path))) {
            DragFinish(drop);
            OpenPath(path);
        } else {
            DragFinish(drop);
        }
        return 0;
    }

    case WM_COPYDATA: {
        const auto* cds = reinterpret_cast<const COPYDATASTRUCT*>(lp);
        if (cds && cds->dwData == kCopyOpenPath && cds->lpData && cds->cbData >= sizeof(wchar_t)) {
            std::wstring p(static_cast<const wchar_t*>(cds->lpData), cds->cbData / sizeof(wchar_t));
            while (!p.empty() && p.back() == L'\0') p.pop_back();
            RestoreFromTray();
            if (!p.empty()) OpenPath(p);
            return TRUE;
        }
        break;
    }

    case WM_CLOSE:
        if (m_set.closeToTray && !m_quitting) {
            HideToTray();
            return 0;
        }
        QuitApp();
        return 0;

    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_ENDSESSION:
        if (wp) {
            RememberPlacement();
            m_set.Save();
            SaveSessionTabs();
        }
        return 0;

    case WM_DESTROY: {
        const bool wasAlive = m_alive;
        m_alive = false;
        if (wasAlive) {
            RememberPlacement();
            m_set.Save();
            SaveSessionTabs();
        }
        RemoveTray();
        for (auto& tab : m_tabs) {
            if (tab.reader) tab.reader->Shutdown();
        }
        m_reader.Shutdown();
        m_browserView.Shutdown();
        m_r.Shutdown();
        RemovePropW(m_hwnd, kInstanceProp);
        if (m_iconBig) DestroyIcon(m_iconBig);
        if (m_iconSmall) DestroyIcon(m_iconSmall);
        m_iconBig = m_iconSmall = nullptr;
        if (wasAlive) {
            log::Write(L"Cikis.");
            PostQuitMessage(0);
        }
        return 0;
    }

    default:
        break;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

} // namespace gm
