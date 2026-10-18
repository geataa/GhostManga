#pragma once
//
// Okuyucu (E5). Sayfalar tek kaydirilabilir yuzeyde dikey liste halinde,
// GhostView tuvali gibi karartilmis zeminin ustunde asili durur.
// Ayar cubugu altta, GhostView HUD geometrisiyle (04-UI-SPEC.md bolum 5).
//
// Bellek: yalniz gorunen ve yakindaki sayfalarin bitmapleri tutulur,
// uzaklasanlar birakilir (FR-RDR-010). Webtoon seritleri D2D'nin azami
// bitmap boyutunu asmasin diye dilimlere bolunur.
//
#include "core/Settings.h"
#include "engine/PageDecoder.h"
#include "ui/Renderer.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"

#include <memory>
#include <string>
#include <vector>

namespace gm {

struct OnlineChapter {
    std::string title;
    std::string url;
};

class ReaderHost {
public:
    virtual ~ReaderHost() = default;
    virtual void Toast(const std::wstring& msg) = 0;
    virtual void Invalidate() = 0;
    virtual void ToggleFullscreen() = 0;
    virtual void SaveSettings() = 0;
    virtual void DecoderSignaled() = 0;                     // is parcacigindan cagrilir
    virtual void ChapterOpened(const std::wstring& dir) = 0;
    virtual void AddBookmark(const std::wstring& seriesName, const std::wstring& chapterName, int pageIndex) = 0;
    virtual void DownloadChapter(const std::wstring& chapterDir, const std::wstring& seriesName, const std::wstring& chapterName) = 0;
    virtual void ReloadCurrentChapter() = 0;
    virtual bool NavigateChapter(int delta) = 0;
    virtual bool OpenChapterByIndex(int index) = 0;
    virtual const std::vector<OnlineChapter>& GetCurrentChapters() const = 0;
    virtual int GetCurrentChapterIndex() const = 0;
    virtual bool HasNextChapter() const = 0;
    virtual bool HasPrevChapter() const = 0;
    virtual void SaveMangaReadingSettings(const std::string& seriesKey, bool webtoon, FitMode fit, bool doublePage, bool rtl, float zoom) = 0;
};

class ReaderView {
    friend struct ReaderViewTestAccess;
public:
    void Init(ReaderHost* host, Settings* settings);
    void Shutdown();

    // Manga bazli okuma ayarlari
    void SetMangaSettings(const std::string& seriesKey, bool hasCustom, bool webtoon, FitMode fit, bool doublePage, bool rtl, float zoom = 1.0f);
    bool IsWebtoon() const { return m_hasCustomSettings ? m_webtoon : (m_set ? m_set->webtoon : false); }
    FitMode CurrentFit() const { return m_hasCustomSettings ? m_fit : (m_set ? m_set->fit : FitMode::Width); }
    bool IsDoublePage() const { return m_hasCustomSettings ? m_doublePage : (m_set ? m_set->doublePage : false); }
    bool IsRtl() const { return m_hasCustomSettings ? m_rtl : (m_set ? m_set->rtl : false); }

    bool Open(const std::wstring& chapterDir);
    bool OpenProgressive(const std::wstring& chapterDir, const std::wstring& series, const std::wstring& chapter, int totalPages = 0);
    void SetProgressiveCount(int totalPages);
    void Close();
    bool IsOpen() const { return m_isOpen || !m_pages.empty(); }
    const std::wstring& Dir() const { return m_dir; }
    const std::wstring& SeriesName() const { return m_series; }
    const std::wstring& ChapterName() const { return m_chapter; }
    bool HasNextChapter() const;
    bool HasPrevChapter() const;
    const std::wstring& NextDir() const { return m_nextDir; }
    const std::wstring& PrevDir() const { return m_prevDir; }
    void SetCustomTitle(const std::wstring& series, const std::wstring& chapter) {
        if (!series.empty()) m_series = series;
        if (!chapter.empty()) m_chapter = chapter;
    }
    void SetSourceMirror(const std::wstring& mirror) { m_mirror = mirror; }
    const std::wstring& SourceMirror() const { return m_mirror; }
    int CurrentPageIndex() const;
    void GoToPage(int page);
    void CheckProgressiveOnDisk();
    void OnPageDownloaded(int index, int totalPages, const std::wstring& pagePath);

    // Kare
    bool Animate(float dt, double now);
    double NextWake(double now) const;
    void Draw(Renderer& r, Ui& ui, const D2D1_RECT_F& area, double now);

    // Girdi
    bool OnKey(UINT vk, bool ctrl, bool shift, bool repeat);
    bool HasFocusedTextInput() const { return m_sidebarChDropdownOpen && m_sidebarChSearch.focused; }
    bool OnChar(wchar_t ch);
    void OnWheel(float notches, bool ctrl, bool shift, float mx, float my);
    void OnMouseMove(float mx, float my, double now);
    bool OnMouseDown(float mx, float my, double now);
    void OnMouseUp();
    void OnMouseLeave();
    bool OnDoubleClick(float mx, float my);
    void OnDecoderSignal(Renderer& r);
    bool OverChrome(float mx, float my) const;
    bool ClosePopups();
    bool Dragging() const { return m_dragging || m_scrollDragging; }
    bool DragMoved() const { return m_dragMoved || m_scrollDragging; }

    float ChromeAlpha() const { return m_chrome; }
    void WakeChrome(double now);
    void SettingsChanged();

private:
    struct Tile {
        ComPtr<ID2D1Bitmap1> bmp;
        UINT y0 = 0, h = 0;
    };
    struct Page {
        std::wstring path;
        UINT natW = 0, natH = 0;
        bool probed = false;
        bool failed = false;
        std::vector<Tile> tiles;
        UINT decW = 0, decH = 0;
        UINT pendingW = 0;
        // D2D bitmap olusturma bu aygit neslinde basarisiz oldu (VRAM dolu,
        // aygit kaybi). Dosya bozuk degil: aygit degisince yeniden denenir.
        uint32_t bmpFailGen = 0;
    };
    struct Row {
        int a = -1, b = -1;       // b: cift sayfada ikinci sayfa
        bool endCard = false;
        float y = 0, h = 0;
        D2D1_RECT_F ra{}, rb{};   // belge koordinati
    };
    struct Anchor {
        int row = 0;
        float frac = 0.0f;
        float fracX = 0.5f;
        float screenY = 0.0f;
    };

    void Layout();
    Anchor CaptureAnchor(float screenY) const;
    void ApplyAnchor(const Anchor& a);
    void RelayoutKeepingPosition(float screenY);
    void NatSize(int i, float& w, float& h) const;
    void FitSingle(int i, float availW, float availH, float& w, float& h) const;
    bool Wide(int i) const;

    void Schedule(Renderer& r);
    void ReleasePage(Page& p);
    void DropAllBitmaps();

    void ScrollTo(float y, bool animate = true);
    void ScrollBy(float dy);
    float MaxScrollY() const;
    float MaxScrollX() const;
    int CurrentRow() const;
    int RowAt(float docY) const;
    float AlignPad() const { return 8.0f * m_scale; } // sayfaya atlarken ustte birakilan pay
    UINT CapWidth(const Page& p) const;
    void NextPage();
    void PrevPage();
    void OpenChapter(int delta);
    void SetFit(FitMode f);
    void ZoomBy(float factor, float mx, float my);
    void ToggleWebtoon();
    void ToggleDouble();
    void ToggleDirection();

    D2D1_RECT_F ToScreen(const D2D1_RECT_F& doc) const;
    void DrawPages(Renderer& r, Ui& ui);
    void DrawHud(Renderer& r, Ui& ui, double now);
    void DrawPopover(Renderer& r, Ui& ui);
    void DrawHelp(Renderer& r);
    void DrawSidebarHandle(Renderer& r, Ui& ui, const D2D1_RECT_F& area);
    void DrawRightSidebar(Renderer& r, Ui& ui, const D2D1_RECT_F& sbRc, double now);
    void DrawSidebarChapterDropdown(Renderer& r, Ui& ui, const D2D1_RECT_F& anchorRc, double now);
    void DrawThumbnailStrip(Renderer& r, Ui& ui, const D2D1_RECT_F& dockRc, double now);

    ReaderHost* m_host = nullptr;
    Settings* m_set = nullptr;
    std::unique_ptr<PageDecoder> m_decoder;
    uint64_t m_gen = 0;
    uint32_t m_devGen = 0;

    // Manga bazli okuma ayarlari
    std::string m_seriesKey;
    bool m_hasCustomSettings = false;
    bool m_webtoon = false;
    FitMode m_fit = FitMode::Width;
    bool m_doublePage = false;
    bool m_rtl = false;

    std::wstring m_dir, m_series, m_chapter, m_mirror, m_nextDir, m_prevDir;
    std::vector<Page> m_pages;
    std::vector<Row> m_rows;
    int m_probedCount = 0;
    bool m_isOpen = false;
    int m_expectedTotalPages = 0;

    D2D1_RECT_F m_area{};
    float m_scale = 1.0f;
    float m_layoutW = 0, m_layoutH = 0, m_layoutScale = 0;
    bool m_layoutDirty = true;
    float m_docW = 0, m_docH = 0;

    float m_scrollY = 0, m_targetY = 0;
    float m_scrollX = 0, m_targetX = 0;
    float m_zoom = 1.0f;

    bool m_scrollDragging = false;
    float m_scrollGrab = 0;
    D2D1_RECT_F m_scrollTrack{}, m_scrollThumb{};
    void DrawScrollbar(Renderer& r);
    void DragScrollbar(float y);

    bool m_dragging = false, m_dragMoved = false;
    float m_dragMx = 0, m_dragMy = 0, m_dragSx = 0, m_dragSy = 0;

    float m_overscroll = 0.0f;
    double m_overscrollAt = 0.0;
    int m_pendingChapter = 0;

    // Sag Okuma Menusu (Right Sidebar)
    bool m_sidebarOpen = true;
    bool m_sidebarChDropdownOpen = false;
    w::TextInput m_sidebarChSearch;
    float m_sidebarChScroll = 0.0f;
    float m_sidebarChMaxScroll = 0.0f;
    bool m_sidebarChSortAsc = false;
    bool m_sidebarChNeedsScrollToCurrent = false;
    D2D1_RECT_F m_sidebarRc{}, m_sidebarReopenRc{};
    D2D1_RECT_F m_sidebarChSelectRc{};
    D2D1_RECT_F m_sidebarChDropdownRc{};

    // Alt Kucuk Resim (Thumbnail Strip) Seridi
    D2D1_RECT_F m_thumbStripRc{};
    float m_thumbScrollX = 0.0f;
    float m_thumbTargetScrollX = 0.0f;
    float m_thumbMaxScrollX = 0.0f;
    bool m_thumbDragging = false;
    bool m_thumbDragMoved = false;
    float m_thumbDragStartX = 0.0f;
    float m_thumbDragStartScrollX = 0.0f;
    int m_thumbClickCandidate = -1;
    int m_lastAutoScrolledPage = -1;

    // Krom (HUD) gorunurlugu
    float m_chrome = 1.0f;
    double m_lastActivity = 0.0;
    float m_mx = -1, m_my = -1;
    D2D1_RECT_F m_hudRect{};
    D2D1_RECT_F m_popRect{};
    D2D1_RECT_F m_gearRect{};
    bool m_popOpen = false;
    bool m_helpOpen = false;
};

} // namespace gm
