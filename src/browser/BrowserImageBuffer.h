#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace gm::browser {
// A crawl owns its buffer. Async browser responses cannot outlive its storage,
// and downloads consume entries instead of keeping a second chapter in memory.
class BrowserImageBuffer {
public:
    static constexpr size_t MaxImageBytes = 8 * 1024 * 1024;
    static constexpr size_t MaxTotalBytes = 32 * 1024 * 1024;
    bool Put(const std::string& url, std::string bytes) {
        if(bytes.empty() || bytes.size()>MaxImageBytes) return false;
        std::lock_guard<std::mutex> lock(m_mutex);
        if(m_images.count(url) || bytes.size()>MaxTotalBytes-m_bytes) return false;
        m_bytes+=bytes.size();
        m_images.emplace(url,std::move(bytes));
        return true;
    }
    std::string Take(const std::string& url) {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it=m_images.find(url);
        if(it==m_images.end()) return {};
        m_bytes-=it->second.size();
        auto bytes=std::move(it->second);m_images.erase(it);return bytes;
    }
private:
    std::mutex m_mutex;
    size_t m_bytes=0;
    std::unordered_map<std::string,std::string> m_images;
};
}
