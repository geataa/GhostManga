#pragma once

#include "MirrorEngine.h"
#include <string>
#include <vector>
#include <functional>
#include <future>

namespace gm::engine {

class ChapterLoader {
public:
    static ChapterLoader& Instance();

    // Computes safe persistent cache directory for a given chapter
    std::wstring GetCacheDir(const std::string& mirrorName, const std::string& mangaUrl, const std::string& chapterUrl);

    // Checks if chapter is fully cached on disk
    bool IsCached(const std::wstring& cacheDir);

    // Synchronously downloads all images of a chapter into cacheDir
    bool DownloadChapter(const std::string& mirrorName,
                         const std::string& chapterUrl,
                         const std::wstring& cacheDir,
                         std::function<void(int pageIndex, int totalPages, const std::wstring& pagePath)> onPageReady = nullptr,
                         std::function<bool()> shouldCancel = nullptr);

    // Asynchronously retrieves chapter list
    std::future<std::vector<ChapterInfo>> GetChaptersAsync(const std::string& mirrorName, const std::string& mangaUrl);

    // Asynchronously downloads chapter
    std::future<bool> DownloadChapterAsync(const std::string& mirrorName,
                                          const std::string& chapterUrl,
                                          const std::wstring& cacheDir,
                                          std::function<void(int pageIndex, int totalPages, const std::wstring& pagePath)> onPageReady = nullptr);

    bool IsDownloading() const { return m_activeDownloads.load() > 0; }

private:
    ChapterLoader() = default;
    ~ChapterLoader() = default;

    std::atomic<int> m_activeDownloads{ 0 };
};

} // namespace gm::engine
