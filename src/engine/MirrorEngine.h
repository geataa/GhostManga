#pragma once

#include "JsEngine.h"
#include "JsBridge.h"
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <atomic>

namespace gm::engine {

struct MirrorInfo {
    std::string name;
    std::string language;
    std::vector<std::string> domains;
    std::string home;
    bool canListFullMangas = false;
    bool disabled = false;
    std::string iconDataUrl;
    std::string imageReferer;
};

struct SearchResult {
    std::string title;
    std::string url;
};

struct ChapterInfo {
    std::string title;
    std::string url;
};

struct SourceStatus {
    std::string mirror, operation, error;
    int resultCount = 0;
    int64_t checkedAt = 0;
    bool success = false;
    int httpStatus = 0;
};

class MirrorEngine {
public:
    static MirrorEngine& Instance();

    bool Init();
    void Shutdown();

    // Enumerate all active mirrors
    const std::vector<MirrorInfo>& GetMirrors();
    size_t GetMirrorsCount() const { return m_cachedMirrorsCount.load(std::memory_order_acquire); }

    // Query a mirror for manga matching search term
    std::vector<SearchResult> SearchManga(const std::string& mirrorName, const std::string& query,
                                        const std::function<bool()>& cancelled = {});

    // Get list of chapters for a manga URL
    std::vector<ChapterInfo> GetChapters(const std::string& mirrorName, const std::string& mangaUrl, const std::string& language = "en");

    // Get list of page image URLs for a chapter URL
    std::vector<std::string> GetChapterImages(const std::string& mirrorName, const std::string& chapterUrl);
    std::string GetImageReferer(const std::string& mirrorName, const std::string& chapterUrl);

    JsEngine& Engine() { return m_engine; }
    std::vector<SourceStatus> GetSourceStatuses() const;
    SourceStatus CheckAccess(const MirrorInfo& mirror, int timeoutMs = 10000);

private:
    MirrorEngine() = default;
    ~MirrorEngine();

    std::string LoadBundleSource();
    void RecordStatus(const std::string& mirror, const std::string& operation, int count, JSValueConst promise);
    void StoreStatus(SourceStatus status);
    mutable std::mutex m_statusMutex;
    std::vector<SourceStatus> m_statuses;

    std::recursive_mutex m_mutex;
    bool                 m_initialized = false;
    JsEngine    m_engine;
    JsBridge    m_bridge;
    std::vector<MirrorInfo> m_cachedMirrors;
    std::atomic<size_t> m_cachedMirrorsCount{ 0 };
};

} // namespace gm::engine
