#pragma once

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include "ChapterState.h"

namespace gm::engine {

struct UpdateNotification {
    std::string seriesKey;
    std::wstring seriesTitle;
    std::string latestChapName;
    std::string latestChapUrl;
    int newChapterCount = 0;
    std::vector<Chapter> chapters;
    std::string error;
};

class Scheduler {
public:
    using NotifyCallback = std::function<void(const UpdateNotification&)>;
    using FinishedCallback = std::function<void()>;

    static Scheduler& Instance();

    // Start background scheduler worker thread
    void Start(NotifyCallback onNewChapters, int intervalMinutes = 60);
    void Start(NotifyCallback onNewChapters, FinishedCallback onCheckFinished, int intervalMinutes = 60);

    static int64_t DelaySeconds(int64_t now, int64_t lastScan, int intervalMinutes, bool startup) {
        if (intervalMinutes <= 0) return -1;
        const int64_t interval = (intervalMinutes > 1440 ? 1440 : intervalMinutes) * 60LL;
        const int64_t elapsed = now >= lastScan ? now - lastScan : 0;
        const int64_t remaining = lastScan > 0 && elapsed < interval ? interval - elapsed : 0;
        return startup && remaining < 30 ? 30 : remaining;
    }

    // Stop background worker cleanly
    void Stop();

    // Trigger an immediate manual check of all followed manga
    void TriggerCheckNow();

    // Is the scheduler currently running
    bool IsRunning() const { return m_running; }

    // Is an update scan currently in progress
    bool IsChecking() const { return m_isChecking; }

    // Scan a single series and detect if it has updates compared to known lastChapterUrl
    bool CheckSeries(const std::string& mirror, const std::string& url,
                     const std::string& lastKnownChapUrl, const std::string& lastKnownChapName,
                     UpdateNotification& outNotif, const std::string& language = "en");
    bool CheckSeries(const std::string& mirror, const std::string& url,
                     const std::string& lastKnownChapUrl,
                     UpdateNotification& outNotif) {
        return CheckSeries(mirror, url, lastKnownChapUrl, "", outNotif);
    }

private:
    Scheduler() = default;
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    void WorkerLoop();
    void CheckForUpdates();

    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_stopRequested{ false };
    std::atomic<bool> m_triggerNow{ false };
    std::atomic<bool> m_isChecking{ false };
    std::atomic<int> m_intervalMinutes{60};
    std::atomic<bool> m_scheduleChanged{false};
    int64_t m_lastScan = 0;

    NotifyCallback m_onNewChapters;
    FinishedCallback m_onCheckFinished;
    std::thread m_worker;
    std::mutex m_cvMutex;
    std::condition_variable m_cv;
};

} // namespace gm::engine
