#pragma once
//
// Ana pencere (E8). Cercevesiz, kendi baslik seridi; sag ustte kucult,
// gizle, kapat (GhostView davranisi); solda menu dugmesi ve cekmece.
// Gizle her zaman tepsiye iner; Kapat ayara bagli (FR-BG-001, FR-UI-002).
//
#include "core/Settings.h"
#include "engine/Library.h"
#include "ui/BrowserView.h"
#include "ui/LibraryView.h"
#include "ui/ReaderView.h"
#include "ui/Renderer.h"
#include "ui/SettingsView.h"
#include "ui/ShellHost.h"
#include "ui/Ui.h"

#include <windows.h>
#include <atomic>
#include <string>

namespace gm {

enum class TabType {
    Library = 0,
    Reader,
    Browser
};

struct AppTab {
    TabType type = TabType::Library;
    std::wstring title;
    std::wstring mangaTitle;
    std::wstring chapterTitle;
    std::string mirror;
    std::string mangaUrl;
    std::string chapterUrl;
    std::wstring chapterDir;
    int chapterIndex = -1;
    std::vector<OnlineChapter> chapters;
    std::unique_ptr<ReaderView> reader;
    bool canClose = true;
    bool loading = false;
    bool failed = false;
    std::wstring statusMessage;
    std::shared_ptr<std::atomic<bool>> downloadCancel = std::make_shared<std::atomic<bool>>(false);
};

class MainWindow : public ReaderHost, public ShellHost, public BrowserHost {
public:
    static constexpr const wchar_t* kClassName = L"GhostManga.MainWindow";
    static constexpr const wchar_t* kInstanceProp = L"GhostManga.Instance";
    static constexpr ULONG_PTR kCopyOpenPath = 0x474D4F50; // 'GMOP'
    static UINT ActivateMessage();

    bool Create(HINSTANCE inst, const std::wstring& openPath, std::wstring* err);
    void Show(int nCmdShow);
    DWORD Tick();                 // 0: kare cizildi; aksi halde beklenecek ms
    bool Alive() const { return m_alive; }

    // ReaderHost + ShellHost
    void Toast(const std::wstring& msg) override;
    void Invalidate() override { DispatchUI([this]() { m_dirty = true; }); }
    void ToggleFullscreen() override;          // ertelenir
    void SaveSettings() override;
    void DecoderSignaled() override;
    void ChapterOpened(const std::wstring& dir) override;
    void OpenFolderDialog() override;          // ertelenir
    void OpenArchiveDialog() override;         // ertelenir
    void ImportAmrDialog() override;           // ertelenir
    void ExportBackupDialog() override;        // ertelenir
    void OpenDataFolder() override;            // ertelenir
    void OpenDownloadsFolder() override;       // ertelenir
    void PurgeCache() override;
    void CheckUpdatesNow() override;
    void OpenSitesCatalog() override;
    void OpenUrl(const std::wstring& url) override;  // ertelenir
    void OpenPath(const std::wstring& path) override; // klasor okunur, .json ice aktarilir
    void OpenOnlineManga(const std::string& mirrorName, const std::string& mangaUrl, const std::wstring& title, const std::string& chapterUrl = "") override;
    void OpenEmbeddedBrowser(const std::string& url, const std::string& mirrorHint = "") override;
    void ConvertAndOptimizeLibrary() override;
    void DeleteSeriesFromLibrary(const std::string& key) override;
    void DeleteLocalChapter(const std::wstring& path) override;
    void AddSeriesToLibrary(const std::string& mirrorName, const std::string& mangaUrl, const std::wstring& title) override;
    void RemoveBookmark(size_t index) override;
    void SetReadingProgress(const std::string& key, bool readToLatest) override;
    void StoreChapterCatalogue(const std::string& key, const std::vector<Chapter>& chapters) override;
    void DispatchUI(std::function<void()> fn) override;
    void AddBookmark(const std::wstring& seriesName, const std::wstring& chapterName, int pageIndex) override;
    void DownloadChapter(const std::wstring& chapterDir, const std::wstring& seriesName, const std::wstring& chapterName) override;
    void ReloadCurrentChapter() override;
    bool NavigateChapter(int delta) override;
    bool OpenChapterByIndex(int index) override;
    const std::vector<OnlineChapter>& GetCurrentChapters() const override;
    int GetCurrentChapterIndex() const override;
    bool HasNextChapter() const override;
    bool HasPrevChapter() const override;
    void SaveMangaReadingSettings(const std::string& seriesKey, bool webtoon, FitMode fit, bool doublePage, bool rtl, float zoom) override;
    void LanguageChanged() override;
    void CrawlChaptersForManga(const std::string& mangaUrl, std::function<void(const std::vector<std::pair<std::string, std::string>>&)> callback) override;

    // BrowserHost
    void CloseBrowser() override;
    void OpenExtractedChapter(const std::wstring& chapterDir, const std::wstring& mangaTitle, const std::wstring& chapterTitle) override;
    void RunOnUIThread(std::function<void()> fn) override;
    void ShowChallengeVerification(const std::string& url, const std::wstring& mangaTitle, const std::string& mode) override;
    void OnChallengeSolved() override;
    void OnCrawlerPageReady(int tabIndex, int pageIndex, int totalPages, const std::wstring& pagePath) override;
    void OnCrawlerChapterReady(int tabIndex, const std::wstring& cacheDir, const std::wstring& mangaTitle, const std::wstring& chapterTitle) override;

private:
    enum class View { Library, Reader, Settings, Browser };

    // Cizim sirasinda istenen ama cizim disinda calismasi gereken eylemler.
    enum class Cmd : WPARAM { OpenFolder = 1, OpenArchive, ImportAmr, ExportBackup, OpenDataFolder, OpenDownloadsFolder, OpenUrl, ToggleFullscreen, Minimize, Maximize, HideToTray };
    void Post(Cmd c);
    void RunCommand(Cmd c);
    void RunFolderDialog();
    void RunArchiveDialog();
    void RunImportDialog();
    void RunExportDialog();
    void ImportAmrFile(const std::wstring& path);
    void DoToggleFullscreen();
    bool LibraryVisible() const;
    void ToastFor(const std::wstring& msg, float seconds);   // uzun mesajlar icin

    static LRESULT CALLBACK WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);

    bool Render(double now);                   // false: cizilemedi (aygit yok)
    void ApplyPendingResize();
    void DrawTitleBar(const D2D1_RECT_F& bar, float alpha);
    void DrawDrawer(const D2D1_RECT_F& content, double now);
    void DrawReaderTabLoading(const D2D1_RECT_F& area, const AppTab& tab, double now);
    void DrawChallengeModal(const D2D1_RECT_F& content, double now);
    void DrawToast();
    void DrawTooltip(double now);
    void ComputeTitleRects();
    LRESULT HitTest(int x, int y);
    bool OnKey(WPARAM vk, bool repeat);
    void ResetPointer();
    void SetView(View v);
    void Back();

    // Sekme (Tab) Yonetimi
    void ActivateTab(int index);
    void CloseTab(int index);
    ReaderView* ActiveReader();
    const ReaderView* ActiveReader() const;
    void SaveSessionTabs();
    void LoadSessionTabs();
    void ScanChaptersForTab(int tabIndex, const std::string& mirror, const std::string& mangaUrl, const std::string& targetChapterUrl);

    void AddTray();
    void RemoveTray();
    void HideToTray();
    void RestoreFromTray();
    void ShowTrayMenu();
    void QuitApp();
    void UpdateIcons();

    void RememberPlacement();
    void ApplySavedPlacement();

    double Now() const;
    float Dp(float v) const { return v * m_dpi / 96.0f; }

    HINSTANCE m_inst = nullptr;
    HWND m_hwnd = nullptr;          // WM_NCDESTROY'a kadar gecerli
    bool m_alive = false;           // WM_DESTROY'da duser
    UINT m_dpi = 96;
    Renderer m_r;
    Ui m_ui;
    Input m_in;
    Settings m_set;
    Library m_lib;

    LibraryView m_library;
    SettingsView m_settingsView;
    ReaderView m_reader;
    BrowserView m_browserView;
    View m_view = View::Library;
    View m_viewBeforeSettings = View::Library;

    bool m_dirty = true;
    std::unordered_set<std::string> m_pendingReadToLatest;
    double m_lastTick = 0.0;
    LARGE_INTEGER m_qpcFreq{};
    bool m_inRender = false;        // Render yeniden girise kapali
    bool m_inSizeMove = false;      // kullanici pencereyi surukleyip boyutluyor
    bool m_resizePending = false;
    UINT m_pendingW = 0, m_pendingH = 0;
    uint32_t m_seenDevGen = 0;

    // Cekmece
    bool m_drawerOpen = false;
    float m_drawerT = 0.0f;
    int m_catState[3] = { 2, 2, 2 };  // 0 dahil, 1 haric, 2 umursama (varsayilan: 2)
    uint32_t m_drawerCatRev = 0xFFFFFFFF;
    int m_cachedCatCounts[3] = { 0, 0, 0 };

    // Baslik dugmeleri (WM_NCHITTEST icin son yerlesim)
    D2D1_RECT_F m_rcMenu{}, m_rcFull{}, m_rcMin{}, m_rcMax{}, m_rcClose{}, m_rcTitle{};

    // Bildirim balonu
    std::wstring m_toastText;
    float m_toastT = 0.0f;
    float m_toastTotal = 0.0f;

    // Tam ekran
    bool m_fullscreen = false;
    WINDOWPLACEMENT m_prevPlacement{ sizeof(WINDOWPLACEMENT) };
    LONG_PTR m_prevStyle = 0;

    // Tepsi
    bool m_trayAdded = false;
    std::atomic<bool> m_quitting{false};
    std::vector<std::thread> m_ownedWorkers;
    UINT m_msgTaskbarCreated = 0;
    HICON m_iconBig = nullptr, m_iconSmall = nullptr;

    std::atomic<bool> m_decodePosted{ false };
    bool m_tipShown = false;
    std::wstring m_pendingOpen;
    std::wstring m_pendingUrl;      // Cmd::OpenUrl icin; son istek kazanir
    std::wstring m_lastUrl;         // cift tiklama ayni bolumu iki kez acmasin
    ULONGLONG m_lastUrlAt = 0;
    bool m_lastDownCanvas = false;  // onceki basis okuyucu tuvalinde miydi

    // Sekmeler (Tabs)
    std::vector<AppTab> m_tabs;
    int m_activeTab = 0;
    D2D1_RECT_F m_rcTabsArea{};
    std::vector<D2D1_RECT_F> m_rcTabButtons;
    std::vector<D2D1_RECT_F> m_rcTabCloses;

    // UI Thread Dispatch Queue
    std::mutex m_uiDispatchMutex;
    std::vector<std::function<void()>> m_uiDispatchQueue;

    // Cloudflare / Robot Verification Modal
    bool m_challengeModalOpen = false;
    std::string m_challengeUrl;
    std::wstring m_challengeMangaTitle;
    std::string m_challengeMode;
    D2D1_RECT_F m_challengeCardRc{};
};

} // namespace gm
