#pragma once
//
// GhostManga Gömülü Tarayıcı Görünümü (BrowserView).
// Microsoft Edge WebView2 Evergreen runtime tabanlı, tam özellikli,
// Cloudflare ve bot korumalarını kullanıcı etkileşimiyle aşabilen,
// All Manga Reader (AMR) tarzı sayfa içi görsel ayıklama yetenekli tarayıcı.
//
#include "ui/Renderer.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"

#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace gm {

class BrowserHost {
public:
    virtual ~BrowserHost() = default;
    virtual void Toast(const std::wstring& msg) = 0;
    virtual void Invalidate() = 0;
    virtual void CloseBrowser() = 0;
    virtual void OpenExtractedChapter(const std::wstring& chapterDir, const std::wstring& mangaTitle, const std::wstring& chapterTitle) = 0;
    virtual void DecoderSignaled() = 0;
    virtual void RunOnUIThread(std::function<void()> fn) = 0;
    virtual void ShowChallengeVerification(const std::string& url, const std::wstring& mangaTitle, const std::string& mode) = 0;
    virtual void OnChallengeSolved() = 0;
    virtual void OnCrawlerPageReady(int tabIndex, int pageIndex, int totalPages, const std::wstring& pagePath) = 0;
    virtual void OnCrawlerChapterReady(int tabIndex, const std::wstring& cacheDir, const std::wstring& mangaTitle, const std::wstring& chapterTitle) = 0;
};

class BrowserView {
public:
    BrowserView();
    ~BrowserView();

    bool Init(HWND parentHwnd, BrowserHost* host);
    void Shutdown();

    void Navigate(const std::wstring& url, const std::wstring& mangaTitle = L"");
    void Show(bool visible);
    bool IsVisible() const { return m_visible; }
    void UpdateBounds(const D2D1_RECT_F& area);

    void DrawBar(Renderer& r, Ui& ui, const D2D1_RECT_F& bar, double now);
    void ExtractAndRead();
    void NavigateChapterInBrowser(int delta);
    void ReloadCurrentPage();
    void CancelChallengeVerification();
    void FinishChallengeVerification();
    void SyncCookiesToJar(const std::string& url = "");
    void StartChallengeVerification(const std::string& url, const std::wstring& mangaTitle = L"");
    bool IsChallengeVerificationActive() const;
    bool IsCrawling() const;
    void ApplyLastBounds();

    bool OnKey(UINT vk, bool ctrl, bool shift = false);
    bool HasFocusedTextInput() const { return m_urlInput.focused; }
    bool OnChar(wchar_t c);

    static void CleanOldBrowserCache(int maxAgeDays = 3);

    // Otomatik Arka Plan WebCrawler (Cloudflare ve Dinamik JS Aşıcı)
    struct CrawledChapter {
        std::string title;
        std::string url;
    };
    struct ChapterCrawlResult {
        bool success = false;
        std::string mangaTitle;
        std::vector<CrawledChapter> chapters;
        std::string errorMessage;
    };
    struct ImageCrawlResult {
        bool success = false;
        std::wstring cacheDir;
        std::wstring mangaTitle;
        std::wstring chapterTitle;
        int pageCount = 0;
        std::string errorMessage;
    };

    void CrawlMangaChapters(const std::string& mangaUrl, std::function<void(const ChapterCrawlResult&)> onComplete);
    void CrawlChapterImages(int tabIndex, const std::string& chapterUrl, const std::wstring& mangaTitle, const std::wstring& chapterTitle, std::function<void(const ImageCrawlResult&)> onComplete);
    void CrawlChapterImages(const std::string& chapterUrl, const std::wstring& mangaTitle, const std::wstring& chapterTitle, std::function<void(const ImageCrawlResult&)> onComplete) {
        CrawlChapterImages(-1, chapterUrl, mangaTitle, chapterTitle, std::move(onComplete));
    }

    const std::wstring& CurrentUrl() const { return m_currentUrl; }
    const std::wstring& MangaTitle() const { return m_mangaTitle; }

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
    BrowserHost* m_host = nullptr;
    HWND m_parent = nullptr;
    bool m_visible = false;
    std::wstring m_currentUrl;
    std::wstring m_mangaTitle;
    w::TextInput m_urlInput;
    bool m_isExtracting = false;
    float m_lastScale = 1.0f;
    D2D1_RECT_F m_lastBounds{};
};

} // namespace gm
