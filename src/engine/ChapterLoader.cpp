#include "ChapterLoader.h"
#include "net/Fetch.h"
#include "engine/ImageValidation.h"
#include "core/Paths.h"
#include "core/FileIo.h"
#include "core/Log.h"
#include "core/Utf8.h"
#include "engine/LocalSource.h"

#include <windows.h>
#include <shlwapi.h>
#include <iomanip>
#include <sstream>

namespace gm::engine {

static uint64_t HashString(const std::string& str) {
    uint64_t hash = 14695981039346656037ull; // FNV-1a
    for (char c : str) {
        hash ^= (uint8_t)c;
        hash *= 1099511628211ull;
    }
    return hash;
}

static std::wstring SanitizeName(const std::string& s) {
    std::wstring w = FromUtf8(s);
    for (wchar_t& c : w) {
        if (c == L'<' || c == L'>' || c == L':' || c == L'"' || c == L'/' ||
            c == L'\\' || c == L'|' || c == L'?' || c == L'*') {
            c = L'_';
        }
    }
    return w;
}

ChapterLoader& ChapterLoader::Instance() {
    static ChapterLoader s_instance;
    return s_instance;
}

std::wstring ChapterLoader::GetCacheDir(const std::string& mirrorName, const std::string& mangaUrl, const std::string& chapterUrl) {
    std::wstring root = paths::DataDir() + L"\\cache";
    std::wstring mirrorDir = root + L"\\" + SanitizeName(mirrorName);

    std::wstringstream ssManga;
    ssManga << std::hex << std::setfill(L'0') << std::setw(16) << HashString(mangaUrl);
    std::wstring mangaDir = mirrorDir + L"\\" + ssManga.str();

    std::wstringstream ssChap;
    ssChap << std::hex << std::setfill(L'0') << std::setw(16) << HashString(chapterUrl);
    std::wstring chapDir = mangaDir + L"\\" + ssChap.str();

    paths::EnsureDir(chapDir);
    return chapDir;
}


static bool IsValidImageBuffer(const std::string& bytes) {return gm::engine::ValidImage(bytes);}
static bool IsValidImageFile(const std::wstring& path) {return gm::engine::ValidImageFile(path);}

bool ChapterLoader::IsCached(const std::wstring& cacheDir) {
    std::string text;
    if (fileio::ReadText(cacheDir+L"\\.complete",64,text)!=fileio::ReadResult::Ok) return false;
    int total=0; try { size_t end; total=std::stoi(text,&end); if(end!=text.size()) return false; } catch(...) { return false; }
    if(total<1 || total>20000) return false;
    for(int i=1;i<=total;++i) {
        wchar_t pattern[32]; swprintf_s(pattern,L"%04d.*",i);
        WIN32_FIND_DATAW data{};
        HANDLE search=FindFirstFileW((cacheDir+L"\\"+pattern).c_str(),&data);
        bool valid=false;
        if(search!=INVALID_HANDLE_VALUE) {
            do { if(!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) && local::IsImageFile(data.cFileName) && IsValidImageFile(cacheDir+L"\\"+data.cFileName)) {valid=true;break;} } while(FindNextFileW(search,&data));
            FindClose(search);
        }
        if(!valid) return false;
    }
    return true;
}

static bool RetryPause(int milliseconds, const std::function<bool()>& cancelled) {
    for(int elapsed=0;elapsed<milliseconds;elapsed+=50) {
        if(cancelled && cancelled()) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return !(cancelled && cancelled());
}

bool ChapterLoader::DownloadChapter(const std::string& mirrorName,
                                   const std::string& chapterUrl,
                                   const std::wstring& cacheDir,
                                   std::function<void(int pageIndex, int totalPages, const std::wstring& pagePath)> onPageReady,
                                   std::function<bool()> shouldCancel) {
    const auto imageReferer = MirrorEngine::Instance().GetImageReferer(mirrorName, chapterUrl);
    CreateDirectoryW(cacheDir.c_str(), nullptr);

    std::string safeChapterUrl = net::NormalizeMirrorUrl(chapterUrl);

    struct ActiveGuard {
        std::atomic<int>& count;
        ActiveGuard(std::atomic<int>& c) : count(c) { count.fetch_add(1); }
        ~ActiveGuard() { count.fetch_sub(1); }
    } guard(m_activeDownloads);
    int previousTotal=0;
    std::string expected;
    if(fileio::ReadText(cacheDir+L"\\.expected",64,expected)==fileio::ReadResult::Ok) {
        try {previousTotal=std::clamp(std::stoi(expected),0,20000);} catch(...) {}
    }
    std::vector<std::string> images;
    for(int attempt=0;attempt<3;++attempt) {
        if(shouldCancel && shouldCancel()) return false;
        auto candidate=MirrorEngine::Instance().GetChapterImages(mirrorName,safeChapterUrl);
        candidate.erase(std::remove_if(candidate.begin(),candidate.end(),[](const auto& url){return url.find_first_not_of(" \t\r\n")==std::string::npos;}),candidate.end());
        if(candidate.size()>images.size()) images=std::move(candidate);
        if(images.size()>2 && (int)images.size()>=previousTotal) break;
        if(attempt<2 && !RetryPause(400*(attempt+1),shouldCancel)) return false;
    }
    if(images.empty()) { log::Write(L"[ChapterLoader] Page list failed after 3 attempts"); return false; }
    if((int)images.size()<previousTotal) {
        log::Write(L"[ChapterLoader] Incomplete page list after 3 attempts (%zu/%d)",images.size(),previousTotal); return false;
    }
    fileio::WriteAtomic(cacheDir+L"\\.expected",std::to_string(images.size()));
    DeleteFileW((cacheDir+L"\\.complete").c_str());

    const int total = (int)images.size();
    log::Write(L"[ChapterLoader] Downloading %d pages for chapter %s into %s (fast concurrent mode)",
               total, FromUtf8(chapterUrl).c_str(), cacheDir.c_str());

    std::atomic<int> nextIndex{ 0 };
    std::atomic<int> downloadedCount{ 0 };
    std::atomic<bool> cancelled{ false };

    auto workerFn = [&]() {
        while (!cancelled.load()) {
            if (shouldCancel && shouldCancel()) {
                cancelled.store(true);
                break;
            }

            int i = nextIndex.fetch_add(1);
            if (i >= total) break;

            std::string rawImg = images[(size_t)i];
            size_t first = rawImg.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) continue;
            size_t last = rawImg.find_last_not_of(" \t\r\n");
            std::string imgUrl = rawImg.substr(first, (last - first + 1));

            if (imgUrl.rfind("//", 0) == 0) {
                imgUrl = "https:" + imgUrl;
            } else if (imgUrl.rfind("/", 0) == 0) {
                size_t p = chapterUrl.find("://");
                if (p != std::string::npos) {
                    size_t p2 = chapterUrl.find('/', p + 3);
                    std::string origin = (p2 != std::string::npos) ? chapterUrl.substr(0, p2) : chapterUrl;
                    imgUrl = origin + imgUrl;
                }
            }

            // Determine extension
            std::string ext = ".jpg";
            size_t queryPos = imgUrl.find('?');
            std::string pathOnly = (queryPos != std::string::npos) ? imgUrl.substr(0, queryPos) : imgUrl;
            size_t dotPos = pathOnly.rfind('.');
            if (dotPos != std::string::npos && dotPos + 1 < pathOnly.size()) {
                std::string possibleExt = pathOnly.substr(dotPos);
                for (char& c : possibleExt) c = (char)tolower((unsigned char)c);
                if (possibleExt == ".png" || possibleExt == ".webp" || possibleExt == ".jpeg" ||
                    possibleExt == ".jpg" || possibleExt == ".gif" || possibleExt == ".avif") {
                    ext = (possibleExt == ".jpeg") ? ".jpg" : possibleExt;
                }
            }

            wchar_t pageFileName[64];
            swprintf_s(pageFileName, L"%04d%s", i + 1, FromUtf8(ext).c_str());
            std::wstring pagePath = cacheDir + L"\\" + pageFileName;

            // If file already exists and non-empty, verify it's a real image
            WIN32_FILE_ATTRIBUTE_DATA fad;
            if (GetFileAttributesExW(pagePath.c_str(), GetFileExInfoStandard, &fad) && fad.nFileSizeLow > 0) {
                if (IsValidImageFile(pagePath)) {
                    downloadedCount.fetch_add(1);
                    if (onPageReady && !cancelled.load()) onPageReady(i, total, pagePath);
                    continue;
                } else {
                    DeleteFileW(pagePath.c_str());
                }
            }

            // Fetch image bytes (no artificial delay on CDN images)
            net::HttpRequest req;
            req.url = imgUrl;
            req.timeoutMs = 15000;
            req.allowBrowserFallback = false;
            req.headers.push_back({ "Referer", imageReferer });
            req.headers.push_back({ "Accept", "image/avif,image/webp,image/apng,image/svg+xml,image/*,*/*;q=0.8" });

            net::HttpResponse resp;
            bool valid=false;
            for(int attempt=0;attempt<4;++attempt) {
                if(cancelled.load() || (shouldCancel && shouldCancel())) {cancelled=true;break;}
                resp=net::Fetch(req,0);
                if(resp.success && IsValidImageBuffer(resp.body)) {valid=true;break;}
                log::Write(L"[ChapterLoader] Page %d attempt %d/4 failed (HTTP %d)",i+1,attempt+1,resp.status);
                if(attempt<3 && !RetryPause(250*(1<<attempt),[&]{return cancelled.load() || (shouldCancel && shouldCancel());})) {cancelled=true;break;}
            }
            if(!valid) continue;

            // Write safely with unique temp file per thread to avoid sharing violations
            std::wstring tmpPath = pagePath + L"." + std::to_wstring(GetCurrentThreadId()) + L".tmp";
            HANDLE h = CreateFileW(tmpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                DWORD written = 0;
                const bool writtenOk=WriteFile(h,resp.body.data(),(DWORD)resp.body.size(),&written,nullptr) && written==resp.body.size();
                CloseHandle(h);
                if(writtenOk && MoveFileExW(tmpPath.c_str(),pagePath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
                    downloadedCount.fetch_add(1);
                    if(onPageReady && !cancelled.load()) onPageReady(i,total,pagePath);
                } else DeleteFileW(tmpPath.c_str());
            }
        }
    };

    // 4 concurrent workers for fast parallel throughput
    int numWorkers = std::min(4, total);
    if (numWorkers < 1) numWorkers = 1;

    std::vector<std::thread> workers;
    workers.reserve((size_t)numWorkers);
    for (int w = 0; w < numWorkers; ++w) {
        workers.emplace_back(workerFn);
    }
    for (auto& t : workers) {
        if (t.joinable()) t.join();
    }

    if (cancelled.load()) {
        log::Write(L"[ChapterLoader] Download cancelled by user");
        return false;
    }

    // Only write completion marker if all pages successfully downloaded
    if (downloadedCount.load() >= total && total > 0) {
        std::wstring marker = cacheDir + L"\\.complete";
        fileio::WriteAtomic(marker, std::to_string(total));
    }

    return downloadedCount.load() == total;
}

std::future<std::vector<ChapterInfo>> ChapterLoader::GetChaptersAsync(const std::string& mirrorName, const std::string& mangaUrl) {
    return std::async(std::launch::async, [mirrorName, mangaUrl]() {
        return MirrorEngine::Instance().GetChapters(mirrorName, mangaUrl);
    });
}

std::future<bool> ChapterLoader::DownloadChapterAsync(const std::string& mirrorName,
                                                     const std::string& chapterUrl,
                                                     const std::wstring& cacheDir,
                                                     std::function<void(int, int, const std::wstring&)> onPageReady) {
    return std::async(std::launch::async, [this, mirrorName, chapterUrl, cacheDir, onPageReady]() {
        return DownloadChapter(mirrorName, chapterUrl, cacheDir, onPageReady);
    });
}

} // namespace gm::engine
