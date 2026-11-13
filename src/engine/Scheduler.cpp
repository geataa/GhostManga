#include "Scheduler.h"
#include "MirrorEngine.h"
#include "AmrImport.h"
#include "core/Paths.h"
#include "core/Log.h"
#include "core/Utf8.h"
#include "core/FileIo.h"
#include <algorithm>
#include "ChapterLoader.h"

#include <chrono>

namespace gm::engine {

Scheduler& Scheduler::Instance() {
    static Scheduler s_instance;
    return s_instance;
}

Scheduler::~Scheduler() {
    Stop();
}

void Scheduler::Start(NotifyCallback onNewChapters, int intervalMinutes) {
    Start(std::move(onNewChapters), nullptr, intervalMinutes);
}

void Scheduler::Start(NotifyCallback onNewChapters, FinishedCallback onCheckFinished, int intervalMinutes) {
    intervalMinutes = std::clamp(intervalMinutes, 0, 1440);
    if (m_running.exchange(true)) {
        {
            std::lock_guard<std::mutex> lock(m_cvMutex);
            if (m_intervalMinutes.exchange(intervalMinutes) != intervalMinutes) m_scheduleChanged = true;
        }
        m_cv.notify_all();
        return;
    }

    m_onNewChapters = std::move(onNewChapters);
    m_onCheckFinished = std::move(onCheckFinished);
    m_intervalMinutes = intervalMinutes;
    m_scheduleChanged = false;
    std::string stamp; m_lastScan = 0;
    if (fileio::ReadText(paths::Join(paths::DataDir(), L"scan.timestamp"), 64, stamp) == fileio::ReadResult::Ok) {
        try { m_lastScan = std::max<int64_t>(0, std::stoll(stamp)); } catch (...) {}
    }
    m_stopRequested = false;
    m_triggerNow = false;

    m_worker = std::thread(&Scheduler::WorkerLoop, this);
    log::Write(L"[Scheduler] Started background update scheduler (interval: %d min)", m_intervalMinutes.load());
}

void Scheduler::Stop() {
    if (!m_running.exchange(false)) return;

    {
        std::lock_guard<std::mutex> lock(m_cvMutex);
        m_stopRequested = true;
    }
    m_cv.notify_all();

    if (m_worker.joinable()) {
        m_worker.join();
    }
    log::Write(L"[Scheduler] Stopped background update scheduler");
}

void Scheduler::TriggerCheckNow() {
    if (!m_running) return;
    if (m_isChecking.load()) {
        log::Write(L"[Scheduler] Update check already in progress, ignoring duplicate trigger");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_cvMutex);
        m_pendingTargetKeys.clear();
        m_triggerNow = true;
    }
    m_cv.notify_all();
    log::Write(L"[Scheduler] Manual update check triggered");
}

void Scheduler::TriggerCheckKeys(const std::vector<std::string>& keys) {
    if (!m_running || keys.empty()) return;
    if (m_isChecking.load()) {
        log::Write(L"[Scheduler] Update check already in progress, ignoring targeted trigger");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_cvMutex);
        m_pendingTargetKeys = keys;
        m_triggerNow = true;
    }
    m_cv.notify_all();
    log::Write(L"[Scheduler] Targeted update check triggered (%zu series)", keys.size());
}

bool Scheduler::IsCheckingKey(const std::string& key) const {
    if (!m_isChecking.load() || key.empty()) return false;
    std::lock_guard<std::mutex> lock(m_cvMutex);
    if (m_activeCheckKey == key) return true;
    if (m_activeTargetKeys.empty()) return true;
    return std::find(m_activeTargetKeys.begin(), m_activeTargetKeys.end(), key) != m_activeTargetKeys.end();
}

bool Scheduler::CheckSeries(const std::string& mirror, const std::string& url,
                            const std::string& lastKnownChapUrl, const std::string& lastKnownChapName,
                            UpdateNotification& outNotif, const std::string& language) {
    outNotif.chapters.clear();
    outNotif.error.clear();
    outNotif.newChapterCount = 0;
    if (mirror.empty() || url.empty()) return false;

    auto raw = MirrorEngine::Instance().GetChapters(mirror, url, language);
    for (const auto& ch : raw) outNotif.chapters.push_back({ch.title, ch.url});
    NormalizeChapters(outNotif.chapters);
    if (outNotif.chapters.empty()) {
        const auto statuses = MirrorEngine::Instance().GetSourceStatuses();
        const SourceStatus* latestSt = nullptr;
        for (const auto& st : statuses) {
            if (st.mirror == mirror && st.operation != "Erişim") {
                if (!latestSt || st.checkedAt > latestSt->checkedAt) latestSt = &st;
            }
        }
        if (latestSt) {
            std::string lowerErr = latestSt->error;
            std::transform(lowerErr.begin(), lowerErr.end(), lowerErr.begin(),
                           [](unsigned char c) { return (char)::tolower(c); });
            const bool isProtection = (latestSt->httpStatus == 403 || latestSt->httpStatus == 503 || latestSt->httpStatus == 429 ||
                                       lowerErr.find("cloudflare") != std::string::npos ||
                                       lowerErr.find("turnstile") != std::string::npos ||
                                       lowerErr.find("captcha") != std::string::npos ||
                                       lowerErr.find("challenge") != std::string::npos ||
                                       lowerErr.find("koruma") != std::string::npos ||
                                       lowerErr.find("403") != std::string::npos ||
                                       lowerErr.find("503") != std::string::npos);
            if (isProtection) {
                outNotif.error = latestSt->httpStatus > 0
                    ? ("Dogrulama / Cloudflare korumasi (HTTP " + std::to_string(latestSt->httpStatus) + ") — Elle Dogrula ile cozun")
                    : "Dogrulama / Cloudflare korumasi algilandi — Elle Dogrula ile cozun";
                return false;
            }
            if (!latestSt->error.empty()) {
                outNotif.error = "Bolumler alinamadi: " + latestSt->error;
                return false;
            }
        }
        outNotif.error = "Bolumler alinamadi; Elle Dogrula ile sayfayi kontrol edebilirsiniz";
        return false;
    }
    const auto& latest = outNotif.chapters.front();
    outNotif.latestChapUrl = latest.url;
    outNotif.latestChapName = latest.title;
    const int oldIndex = ChapterIndex(outNotif.chapters, lastKnownChapUrl);
    if (oldIndex > 0) outNotif.newChapterCount = oldIndex;
    else if (!lastKnownChapUrl.empty() && ChapterNumber(latest.title, latest.url) > ChapterNumber(lastKnownChapName, lastKnownChapUrl) && ChapterNumber(lastKnownChapName, lastKnownChapUrl) >= 0)
        outNotif.newChapterCount = 1;
    return true; // Successful scans publish the catalogue even without a release.
}

bool Scheduler::CheckForUpdates() {
    m_isChecking = true;
    std::vector<std::string> targetKeys;
    {
        std::lock_guard<std::mutex> lock(m_cvMutex);
        targetKeys.swap(m_pendingTargetKeys);
        m_activeTargetKeys = targetKeys;
        m_activeCheckKey.clear();
    }
    const bool wasFullScan = targetKeys.empty();
    m_checkedCount = 0;
    m_totalCheckCount = 0;

    std::wstring libPath = paths::Join(paths::DataDir(), L"library.json");

    amr::ImportData data;
    amr::ImportError err = amr::LoadExportFile(libPath, data);
    if (err != amr::ImportError::None && err != amr::ImportError::Empty) {
        log::Write(L"[Scheduler] Failed to load library file %s (err %d)", libPath.c_str(), (int)err);
        {
            std::lock_guard<std::mutex> lock(m_cvMutex);
            m_activeTargetKeys.clear();
            m_activeCheckKey.clear();
        }
        m_isChecking = false;
        if (m_onCheckFinished) m_onCheckFinished();
        return wasFullScan;
    }

    std::vector<const Series*> toCheck;
    toCheck.reserve(data.series.size());
    for (const auto& s : data.series) {
        if (s.mirror.empty() || s.url.empty()) continue;
        if (!wasFullScan) {
            if (std::find(targetKeys.begin(), targetKeys.end(), s.key) != targetKeys.end()) {
                toCheck.push_back(&s);
            }
        } else if (s.update == 1) {
            toCheck.push_back(&s);
        }
    }

    m_totalCheckCount = (int)toCheck.size();
    log::Write(L"[Scheduler] Checking updates (%zu target series, fullScan=%d)", toCheck.size(), wasFullScan ? 1 : 0);

    for (const Series* sp : toCheck) {
        if (m_stopRequested) break;
        const Series& s = *sp;

        // If user is actively downloading/reading a chapter, pause background checks
        while (ChapterLoader::Instance().IsDownloading() && !m_stopRequested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        if (m_stopRequested) break;

        {
            std::lock_guard<std::mutex> lock(m_cvMutex);
            m_activeCheckKey = s.key;
        }

        UpdateNotification notif;
        notif.seriesKey = s.key;
        notif.seriesTitle = FromUtf8(s.displayName.empty() ? s.name : s.displayName);

        // Sitedeki en son bilinen bolumle karsilastir (kullanicinin okuma ilerlemesiyle degil!)
        std::string lastKnownUrl = !s.latestChapterUrl.empty() ? s.latestChapterUrl : s.lastChapterUrl;
        std::string lastKnownName = !s.latestChapterName.empty() ? s.latestChapterName : s.lastChapterName;

        try {
            CheckSeries(s.mirror, s.url, lastKnownUrl, lastKnownName, notif, s.language);
            if (s.latestChapterUrl.empty()) notif.newChapterCount = 0;
            if (!notif.chapters.empty()) {
                std::vector<std::pair<std::string, std::string>> pairs;
                for (const auto& ch : notif.chapters) pairs.emplace_back(ch.title, ch.url);
                amr::SaveCachedChapterList(s.key, pairs);
            }
        } catch (const std::exception& e) {
            notif.error = e.what();
        } catch (...) {
            notif.error = "Kaynak denetimi basarisiz";
        }
        m_checkedCount.fetch_add(1);
        if (m_onNewChapters && !m_stopRequested) m_onNewChapters(notif);

        // Hizli ve nazik gecikme (150ms), kullanicinin durdurma istegini dinler
        for (int i = 0; i < 2 && !m_stopRequested; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(75));
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_cvMutex);
        m_activeTargetKeys.clear();
        m_activeCheckKey.clear();
    }
    m_isChecking = false;
    log::Write(L"[Scheduler] Update check finished");
    if (m_onCheckFinished) {
        m_onCheckFinished();
    }
    return wasFullScan;
}

void Scheduler::WorkerLoop() {
    using Clock = std::chrono::system_clock;
    const auto started = std::chrono::steady_clock::now();
    bool first = true;
    while (!m_stopRequested) {
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(Clock::now().time_since_epoch()).count();
        auto delay = DelaySeconds(now, m_lastScan, m_intervalMinutes.load(), false);
        if (first && delay >= 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-started).count();
            delay = std::max<int64_t>(delay, std::max<int64_t>(0,30-elapsed));
        }
        {
            std::unique_lock<std::mutex> lock(m_cvMutex);
            auto wake = [this] { return m_stopRequested.load() || m_triggerNow.load() || m_scheduleChanged.load(); };
            if (delay < 0) m_cv.wait(lock,wake);
            else m_cv.wait_for(lock,std::chrono::seconds(delay),wake);
        }
        if (m_stopRequested) break;
        if (m_scheduleChanged.exchange(false) && !m_triggerNow) continue;
        first = false;
        m_triggerNow = false;
        const bool wasFullScan = CheckForUpdates();
        if (wasFullScan && !m_stopRequested) {
            m_lastScan = std::chrono::duration_cast<std::chrono::seconds>(Clock::now().time_since_epoch()).count();
            fileio::WriteAtomic(paths::Join(paths::DataDir(),L"scan.timestamp"),std::to_string(m_lastScan));
        }
    }
}

} // namespace gm::engine
