#pragma once
//
// Kutuphane (E1, M2.5). Okuma listesi ve son acilan yerel klasorler.
// Arama, siralama, isme gore gruplama ve goreli zaman damgalari.
//
#include "core/Settings.h"
#include "engine/Library.h"
#include "engine/LocalSource.h"
#include "ui/Renderer.h"
#include "ui/ShellHost.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"

#include <string>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>

namespace gm {

class LibraryView {
    friend struct LibraryViewTestAccess;
public:
    ~LibraryView() { StopWorkers(); }
    void StopWorkers();
    void RequestCatalogue(const Series& series, ShellHost& host) {
        if (!series.chapters.empty()) { host.StoreChapterCatalogue(series.key, series.chapters); return; }
        FetchChaptersForManga(series.key, series.mirror, series.url, series.lastChapterUrl, host);
    }
    void CatalogueChanged(const std::string& key) {
        std::lock_guard<std::mutex> lock(m_chapterMutex);
        m_chapterCache.erase(key);
        m_displayDirty = true;
    }
    void Draw(Renderer& r, Ui& ui, const D2D1_RECT_F& area, Settings& set, Library& lib, ShellHost& host, double now);
    void OnWheel(float notches);
    bool OnKey(UINT vk, bool ctrl = false, bool shift = false);
    bool OnChar(wchar_t c);
    void ShowReadingList();
    void FocusSearch();
    void SearchOtherSources(const std::wstring& title, ShellHost& host);
    bool IsSearchFocused() const { return m_tab == kLocal ? m_localSearch.focused : m_tab == kSearch ? (m_activeSearch && m_activeSearch->input.focused) : m_search.focused; }
    bool HasFocusedTextInput() const {
        if (m_resetConfirmStep > 0 || (m_tab == kLocal && !m_localSelected.empty())) return false;
        if (m_chDropdownOpen) return m_chDropdownSearch.focused;
        if (m_selectedRowIndex >= 0) return m_cardMode == 1 && m_chapterSearch.focused;
        return IsSearchFocused();
    }
    void RecheckRecent() { m_checkedOnce = false; m_localDataDirty = true; }
    void SetTab(int tab) { m_tab = std::clamp(tab, 0, 5); }
    int GetTab() const { return m_tab; }
    void SetUpdateError(const std::string& key, const std::wstring& err) {
        std::lock_guard<std::mutex> lock(m_updateErrMutex);
        if (!key.empty()) m_updateErrors[key] = err;
        ++m_updateErrorRevision;
    }
    void ClearUpdateError(const std::string& key) {
        std::lock_guard<std::mutex> lock(m_updateErrMutex);
        m_updateErrors.erase(key);
        ++m_updateErrorRevision;
    }
    bool HasUpdateError(const std::string& key) const {
        std::lock_guard<std::mutex> lock(m_updateErrMutex);
        return m_updateErrors.find(key) != m_updateErrors.end();
    }
    std::wstring GetUpdateError(const std::string& key) const {
        std::lock_guard<std::mutex> lock(m_updateErrMutex);
        auto it = m_updateErrors.find(key);
        return (it != m_updateErrors.end()) ? it->second : L"";
    }
    void SetCategoryFilter(int filterNew, int filterUnread, int filterOneshot) {
        if (m_filterNew != filterNew || m_filterUnread != filterUnread || m_filterOneshot != filterOneshot) {
            m_filterNew = filterNew;
            m_filterUnread = filterUnread;
            m_filterOneshot = filterOneshot;
            m_displayDirty = true;
        }
    }
    void SetDisplayDirty() { m_displayDirty = true; }
    int FilterNew() const { return m_filterNew; }
    int FilterUnread() const { return m_filterUnread; }
    int FilterOneshot() const { return m_filterOneshot; }

private:
    enum Tab { kSeries = 0, kLocal = 1, kBookmarks = 2, kSites = 3, kSearch = 4, kHealth = 5 };
    void DrawSourceHealth(Renderer& r, Ui& ui, const D2D1_RECT_F& list, ShellHost& host);
    bool m_onlyFailedSources = true;
    std::atomic<bool> m_stopping{false};
    std::vector<std::thread> m_workers;

    struct OnlineSearchResult {
        std::string mirror;
        std::string title;
        std::string url;
    };

    struct Row {
        size_t index = 0;        // lib.All() icindeki yeri
        std::string key;
        std::string mirror;
        std::string mangaUrl;
        std::string lastChapterUrl;
        std::string latestChapterUrl;
        std::string latestChapterName;
        std::wstring chapterLabel; // "c912" veya "Bölüm 1" gibi (son okunan)
        std::wstring latestChapterLabel; // sitedeki en yeni bolum etiketi
        std::wstring title;
        std::wstring normTitle;  // gruplama ve coklu kaynak esleme icin normalize edilmis baslik
        std::wstring searchStr;  // title + sub + mirror + cats (kucuk harf)
        std::wstring sub;        // ayna · son okunan · kategoriler
        std::wstring lang;       // "EN" gibi
        std::wstring initial;
        std::wstring openUrl;
        std::wstring relUpts;    // onceden hesaplanmis goreli zaman (UI takilmasini onler)
        std::wstring relLastRead;// onceden hesaplanmis son okuma zamani
        int64_t upts = 0;
        int64_t lastReadTs = 0;
        int64_t addedTs = 0;
        bool stopped = false;
        bool caughtUp = false;
        bool updateFailed = false;
    };

    struct DisplayItem {
        bool isGroupHeader = false;
        bool isChild = false;
        size_t rowIndex = 0;     // m_rows icindeki index
        std::wstring groupTitle;
        std::wstring groupInitial;
        std::wstring relTime;    // onceden hesaplanmis grup zamani
        size_t childCount = 0;
        int64_t maxTime = 0;
        bool expanded = false;
        bool caughtUp = false;
        bool updateFailed = false;
        std::string groupKey;
        std::vector<std::string> groupMirrors; // grup icindeki ayna listesi
    };

    void DrawEmpty(Renderer& r, Ui& ui, const D2D1_RECT_F& area, ShellHost& host);
    void DrawToolbar(Renderer& r, Ui& ui, const D2D1_RECT_F& rc, Settings& set, Library& lib, ShellHost& host, double now);
    void DrawSeriesList(Renderer& r, Ui& ui, const D2D1_RECT_F& list, Settings& set, Library& lib, ShellHost& host);
    void DrawCardGrid(Renderer& r, Ui& ui, const D2D1_RECT_F& list, Settings& set, Library& lib, ShellHost& host);
    void DrawLocalList(Renderer& r, Ui& ui, const D2D1_RECT_F& list, const std::vector<std::wstring>& recent,
                       ShellHost& host, Settings& set, Library& lib);
    void RebuildLocal(const Settings& set, const Library& lib);
    void DrawLocalDetail(Renderer& r, Ui& ui, const D2D1_RECT_F& area, ShellHost& host);
    void DrawBookmarkList(Renderer& r, Ui& ui, const D2D1_RECT_F& list, const std::vector<Bookmark>& bookmarks,
                          ShellHost& host);
    void DrawSitesList(Renderer& r, Ui& ui, const D2D1_RECT_F& list, ShellHost& host, Settings& set);
    void DrawSearchTab(Renderer& r, Ui& ui, const D2D1_RECT_F& list, ShellHost& host, Settings& set, Library& lib, double now);
    void DrawResetConfirmModal(Renderer& r, Ui& ui, const D2D1_RECT_F& area, Library& lib, Settings& set, ShellHost& host);
    void DrawSortMenu(Renderer& r, Ui& ui, Settings& set, ShellHost& host);
    void DrawScrollbar(Renderer& r, Ui& ui, const D2D1_RECT_F& list, float contentH, ShellHost& host);
    float ClampScroll(float listH, float contentH);
    void RebuildRows(const Library& lib);
    void RebuildDisplayItems(const Settings& set);
    const std::vector<std::wstring>& ExistingRecent(const std::vector<std::wstring>& recent);

    struct LocalGroup { std::wstring key, title; std::vector<local::Entry> chapters; int64_t downloadedAt=0; bool recordedDownload=false; };
    std::vector<local::Entry> m_localEntries;
    std::vector<LocalGroup> m_localGroups;
    bool m_localDataDirty=true;
    std::wstring m_localRoot, m_localSelected, m_localDeletePath;
    std::vector<std::wstring> m_localRecent;
    uint32_t m_localRevision=0xFFFFFFFF;
    std::wstring m_localLayoutQuery;
    int m_localLayoutSort=-1;
    bool m_localLayoutGroup=false, m_localLayoutDesc=false;
    w::TextInput m_localSearch;
    int m_localSort=0;
    bool m_localSortDesc=true;
    float m_localDetailScroll=0, m_localDetailMaxScroll=0;
    int m_tab = kSeries;
    float m_scroll[6] = {};
    float m_maxScroll[6] = {};
    float m_pitch = 72.0f;
    float m_listH = 0.0f;
    float m_lastScale = 0.0f;

    w::TextInput m_search;
    bool m_sortMenuOpen = false;
    D2D1_RECT_F m_sortBtnRc{};
    std::unordered_set<std::string> m_expandedGroups;

    std::vector<Row> m_rows;
    std::vector<DisplayItem> m_items; // ekranda cizilecek sirali ogeler
    uint32_t m_rowsRev = 0;
    Lang m_rowsLanguage=Lang::Auto;
    int m_lastSort = -1;
    bool m_lastSortDesc = true;
    bool m_lastGroup = false;
    std::wstring m_lastQuery;

    std::vector<std::wstring> m_checkedFor;
    std::vector<std::wstring> m_existing;
    bool m_checkedOnce = false;

    std::vector<OnlineSearchResult> m_onlineResults;
    bool m_searchingOnline = false;
    std::wstring m_searchStatusText;

    int m_filterNew = 2; // 0: dahil, 1: haric, 2: umursama
    int m_filterUnread = 2;
    int m_filterOneshot = 2;

    struct MirrorDisplayItem {
        size_t index = 0;
        std::wstring nameW;
        std::wstring domainStr;
        std::wstring langUpper;
        std::vector<std::wstring> languages;
        std::wstring searchLower;
    };
    std::vector<MirrorDisplayItem> m_mirrorDisplayList;
    std::vector<size_t> m_filteredMirrors;
    std::wstring m_lastMirrorSearch;
    bool m_mirrorsCacheBuilt = false;
    void EnsureMirrorsCache();
    void FilterSites(const Settings& set);
    void StartSiteChecks(ShellHost& host, const std::vector<size_t>& indices);
    bool m_showHiddenSites=false, m_siteLanguagesOpen=false, m_sitesAutoChecked=false;
    std::vector<std::wstring> m_siteLanguages;
    std::atomic<bool> m_siteChecking{false}, m_siteCancel{false};
    std::atomic<size_t> m_siteCompleted{0}, m_siteTotal{0};

    bool m_displayDirty = true;
    uint32_t m_itemsRowsRev = 0xFFFFFFFF;
    int m_selectedRowIndex = -1;
    void DrawDetailCard(Renderer& r, Ui& ui, const D2D1_RECT_F& area, Library& lib, ShellHost& host, Settings& set);

    // Bolum Listesi (Gecmis ve Okunmamis Bolumler)
    struct ChapterItem {
        std::string title;
        std::string url;
        bool isRead = false;
        bool isCurrent = false;
    };
    struct MangaChapterCache {
        std::string mirror;
        std::string mangaUrl;
        std::vector<ChapterItem> chapters;
        bool loading = false;
        bool failed = false;
    };
    mutable std::mutex m_chapterMutex;
    std::unordered_map<std::string, MangaChapterCache> m_chapterCache;
    int m_cardMode = 0; // 0: Detay/Kaynaklar, 1: Bolum Listesi
    float m_cardSourcesScroll=0, m_cardSourcesMaxScroll=0;
    w::TextInput m_chapterSearch;
    int m_chapterFilterTab = 0; // 0: Tumu, 1: Okunmamis, 2: Okunanlar
    bool m_chapterSortAsc = false; // false: En yeni, true: En eski (Bolum 1)
    float m_chapterScroll = 0.0f;
    float m_chapterMaxScroll = 0.0f;
    void FetchChaptersForManga(const std::string& key, const std::string& mirror, const std::string& mangaUrl, const std::string& lastChapUrl, ShellHost& host);

    // Web tarzi asagi acilir bolum secim dropdown'u
    bool m_chDropdownOpen = false;
    std::string m_chDropdownRowKey;
    D2D1_RECT_F m_chDropdownAnchorRc{};
    D2D1_RECT_F m_chDropdownRc{};
    Row m_chDropdownRow;
    bool m_chDropdownSortAsc = false;
    float m_chDropdownScroll = 0.0f;
    float m_chDropdownMaxScroll = 0.0f;
    w::TextInput m_chDropdownSearch;
    void DrawRowChapterDropdown(Renderer& r, Ui& ui, const D2D1_RECT_F& area, ShellHost& host, double now);

    // Kaydirma cubugu surukleme durumu
    bool m_scrollDragging = false;
    float m_scrollDragStartY = 0.0f;
    float m_scrollDragStartVal = 0.0f;

    // Cift Onayli Kutuphane Sifirlama Modali (1. Onay -> 2. Onay)
    int m_resetConfirmStep = 0; // 0: Kapali, 1: Ilk Onay, 2: Ikinci ve Son Onay

    // Guncelleme Hatasi Alan Seriler (Key -> Hata Mesaji)
    mutable std::mutex m_updateErrMutex;
    std::unordered_map<std::string, std::wstring> m_updateErrors;
    std::atomic<uint32_t> m_updateErrorRevision{0};
    uint32_t m_displayErrorRevision = 0;

    // Bolum Dropdown Pozisyonlama Bayraklari
    bool m_chDropdownNeedsScrollToCurrent = false;
    bool m_chapterNeedsScrollToCurrent = false;

    // AMR Tarzi Coklu Kaynak Manga Arama
    struct SearchMirrorItem {
        std::string mirror;
        std::string url;
    };
    struct GroupedSearchItem {
        std::string title;
        std::wstring titleW;
        std::wstring initial;
        int relevanceScore = 0;
        std::vector<SearchMirrorItem> mirrors;
        size_t selectedMirrorIdx = 0;
    };
    struct SearchSession {
        uint32_t id = 0;
        w::TextInput input;
        std::vector<GroupedSearchItem> results;
        std::atomic<bool> searching{false};
        std::atomic<uint64_t> generation{0};
        std::wstring query, status;
        std::mutex mutex;
        float scroll = 0, maxScroll = 0;
    };
    std::vector<std::shared_ptr<SearchSession>> m_searchTabs;
    std::shared_ptr<SearchSession> m_activeSearch;
    uint32_t m_nextSearchId = 1;
    bool NewSearchTab(ShellHost& host);
    void SelectSearchTab(size_t index);
    void CloseSearchTab(size_t index);
    void CancelSearch(const std::shared_ptr<SearchSession>& session);
    void EnsureSearchTab();
    void LaunchGlobalMangaSearch(const std::wstring& query, ShellHost& host);
};

} // namespace gm
