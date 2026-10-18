#pragma once
//
// Kabuk gorunumlerinin (kutuphane, ayarlar) ana pencereden istedikleri.
//
#include <string>
#include <vector>
#include <functional>
#include <utility>
#include "engine/ChapterState.h"

namespace gm {

// Kural: bu arayuzun cizim sirasinda cagrilan her yontemi ya aninda biter ya
// da isi mesaj kuyruguna erteler. Mesaj dongusu donduren hicbir sey (modal
// diyalog, ShellExecute, DestroyWindow, pencere boyutu degisimi) cizimin
// ortasinda calismaz; yoksa ic ice Render ve silinmis Renderer'a donus olur.
class ShellHost {
public:
    virtual ~ShellHost() = default;
    virtual void Toast(const std::wstring& msg) = 0;
    virtual void Invalidate() = 0;
    virtual void OpenFolderDialog() = 0;   // ertelenir
    virtual void OpenArchiveDialog() = 0;  // ertelenir
    virtual void ImportAmrDialog() = 0;    // ertelenir
    virtual void ExportBackupDialog() = 0; // ertelenir
    virtual void OpenDataFolder() = 0;     // ertelenir
    virtual void OpenDownloadsFolder() = 0;// ertelenir
    virtual void PurgeCache() = 0;
    virtual void CheckUpdatesNow() = 0;
    virtual void OpenSitesCatalog() = 0;
    virtual void OpenUrl(const std::wstring& url) = 0;  // ertelenir; yalniz http(s)
    virtual void OpenPath(const std::wstring& dir) = 0;
    virtual void OpenOnlineManga(const std::string& mirrorName, const std::string& mangaUrl, const std::wstring& title, const std::string& chapterUrl = "") = 0;
    virtual void OpenEmbeddedBrowser(const std::string& url, const std::string& mirrorHint = "") = 0;
    virtual void ConvertAndOptimizeLibrary() = 0;
    virtual void DeleteSeriesFromLibrary(const std::string& key) = 0;
    virtual void DeleteLocalChapter(const std::wstring& path) {}
    virtual void AddSeriesToLibrary(const std::string& mirrorName, const std::string& mangaUrl, const std::wstring& title) = 0;
    virtual void RemoveBookmark(size_t index) = 0;
    virtual void SetReadingProgress(const std::string& key, bool readToLatest) = 0;
    virtual void StoreChapterCatalogue(const std::string& key, const std::vector<Chapter>& chapters) = 0;
    virtual void DispatchUI(std::function<void()> fn) = 0;
    virtual void CrawlChaptersForManga(const std::string& mangaUrl, std::function<void(const std::vector<std::pair<std::string, std::string>>&)> callback) = 0;
    virtual void SaveSettings() = 0;
    virtual void LanguageChanged() = 0;
};

} // namespace gm
