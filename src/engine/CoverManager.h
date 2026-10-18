#pragma once

#include "ui/Renderer.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>

namespace gm::engine {

class CoverManager {
public:
    static CoverManager& Instance();

    void Init(std::function<void()> onCoverReady);
    void Shutdown();

    // Key icin diskteki kapak dosyasi yolu
    std::wstring GetCoverPath(const std::string& key);

    // Kapak dosyasi diskte var mi
    bool HasCover(const std::string& key);

    // D2D Bitmap al (yoksa diski dener, diskte de yoksa arka planda indirmeyi baslatir)
    ID2D1Bitmap1* GetBitmap(Renderer& r, const std::string& key,
                            const std::string& mangaUrl = "", const std::string& mirror = "");

    ID2D1Bitmap1* GetLocalBitmap(Renderer& r, const std::wstring& imagePath);

    // Tekli indirme istegi ekle
    void RequestCover(const std::string& key, const std::string& mangaUrl, const std::string& mirror = "");

    // Kutuphanedeki tum seriler icin eksik kapaklari arka planda siraya koy
    void QueueBatch(const std::vector<std::string>& keys, const std::vector<std::string>& urls, const std::vector<std::string>& mirrors);

private:
    CoverManager();
    ~CoverManager();

    struct Task {
        std::string key;
        std::string url;
        std::string mirror;
    };

    void WorkerLoop();
    bool DownloadCover(const Task& task);

    std::function<void()> m_notifyCb;
    std::wstring m_coversDir;

    std::mutex m_queueMutex;
    std::condition_variable m_cv;
    std::deque<Task> m_queue;
    std::unordered_set<std::string> m_queuedKeys;
    std::unordered_set<std::string> m_failedKeys;

    std::vector<std::thread> m_workers;
    std::atomic<bool> m_stop{ false };

    // UI Thread Bitmap Cache
    uint32_t m_lastDevGen = 0;
    std::unordered_map<std::string, ComPtr<ID2D1Bitmap1>> m_bitmaps;
};

} // namespace gm::engine
