#include "engine/CoverManager.h"
#include "core/Paths.h"
#include "core/Log.h"
#include "core/FileIo.h"
#include "core/Utf8.h"
#include "net/Fetch.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cmath>
#include <algorithm>
#include <chrono>

using Microsoft::WRL::ComPtr;

namespace gm::engine {

static uint64_t Fnv1a64(std::string_view str) {
    uint64_t hash = 14695981039346656037ull;
    for (char c : str) {
        hash ^= (uint64_t)(unsigned char)c;
        hash *= 1099511628211ull;
    }
    return hash;
}

static bool DecodeImageFile(const std::wstring& path, UINT maxW, UINT& outW, UINT& outH, std::vector<uint8_t>& outBgra) {
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapDecoder> dec;
    hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = dec->GetFrame(0, &frame);
    if (FAILED(hr)) return false;

    UINT origW = 0, origH = 0;
    hr = frame->GetSize(&origW, &origH);
    if (FAILED(hr) || origW == 0 || origH == 0) return false;

    ComPtr<IWICFormatConverter> conv;
    hr = factory->CreateFormatConverter(&conv);
    if (FAILED(hr)) return false;
    hr = conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return false;

    const double ratio = std::min({1.0, (double)(maxW ? maxW : origW) / origW, 800.0 / origH});
    UINT targetW = std::max<UINT>(1, (UINT)(origW * ratio));
    UINT targetH = (UINT)std::max<UINT>(1, (UINT)std::lround((double)origH * targetW / origW));

    IWICBitmapSource* src = conv.Get();
    ComPtr<IWICBitmapScaler> scaler;
    if (targetW != origW || targetH != origH) {
        if (SUCCEEDED(factory->CreateBitmapScaler(&scaler))) {
            if (SUCCEEDED(scaler->Initialize(conv.Get(), targetW, targetH, WICBitmapInterpolationModeHighQualityCubic))) {
                src = scaler.Get();
            }
        }
    }

    const UINT stride = targetW * 4;
    try {
        outBgra.resize((size_t)stride * targetH);
    } catch (...) {
        return false;
    }

    hr = src->CopyPixels(nullptr, stride, (UINT)outBgra.size(), outBgra.data());
    if (FAILED(hr)) {
        outBgra.clear();
        return false;
    }

    outW = targetW;
    outH = targetH;
    return true;
}

static std::string ExtractCoverUrlFromHtml(const std::string& html, const std::string& pageUrl) {
    if (html.empty()) return "";

    auto searchMeta = [&](std::string_view propName) -> std::string {
        size_t pos = 0;
        while ((pos = html.find(propName, pos)) != std::string::npos) {
            size_t tagStart = html.rfind('<', pos);
            size_t tagEnd = html.find('>', pos);
            if (tagStart != std::string::npos && tagEnd != std::string::npos && tagEnd > tagStart) {
                std::string metaTag = html.substr(tagStart, tagEnd - tagStart + 1);
                size_t cPos = metaTag.find("content=");
                if (cPos != std::string::npos && cPos + 8 < metaTag.size()) {
                    char quote = metaTag[cPos + 8];
                    if (quote == '"' || quote == '\'') {
                        size_t endQuote = metaTag.find(quote, cPos + 9);
                        if (endQuote != std::string::npos) {
                            return metaTag.substr(cPos + 9, endQuote - (cPos + 9));
                        }
                    }
                }
            }
            pos += propName.size();
        }
        return "";
    };

    // 1. OpenGraph & Twitter Cards
    std::string cand = searchMeta("og:image");
    if (!cand.empty()) return cand;

    cand = searchMeta("twitter:image");
    if (!cand.empty()) return cand;

    // 2. Site-specific cover class patterns
    const char* classKeywords[] = {
        "detail-info-cover-img", "detail-info-cover", "img-cover", "wp-manga-cover",
        "summary_image", "attr-cover", "novel-cover", "manga-poster", "featured-image", "cover"
    };

    for (const char* kw : classKeywords) {
        size_t pos = 0;
        while ((pos = html.find(kw, pos)) != std::string::npos) {
            size_t tagStart = html.rfind('<', pos);
            if (tagStart != std::string::npos) {
                // Examine up to 600 chars around this element
                size_t snippetLen = std::min<size_t>(600, html.size() - tagStart);
                std::string snippet = html.substr(tagStart, snippetLen);

                const char* srcAttrs[] = { "src=", "data-src=", "data-original=", "data-lazy-src=" };
                for (const char* attr : srcAttrs) {
                    size_t sPos = snippet.find(attr);
                    if (sPos != std::string::npos) {
                        size_t valStart = sPos + strlen(attr);
                        if (valStart < snippet.size()) {
                            char q = snippet[valStart];
                            if (q == '"' || q == '\'') {
                                size_t eq = snippet.find(q, valStart + 1);
                                if (eq != std::string::npos) {
                                    std::string u = snippet.substr(valStart + 1, eq - (valStart + 1));
                                    if (!u.empty() && u.find(".gif") == std::string::npos &&
                                        u.find("avatar") == std::string::npos && u.find("icon") == std::string::npos) {
                                        return u;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            pos += strlen(kw);
        }
    }

    return "";
}

static std::string ResolveUrl(std::string url, const std::string& pageUrl) {
    if (url.empty()) return url;
    while (!url.empty() && isspace((unsigned char)url.front())) url.erase(0, 1);
    while (!url.empty() && isspace((unsigned char)url.back())) url.pop_back();

    if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
        return url;
    }
    if (url.rfind("//", 0) == 0) {
        return "https:" + url;
    }
    if (url.rfind("/", 0) == 0) {
        size_t pos = pageUrl.find("://");
        if (pos != std::string::npos) {
            size_t nextSlash = pageUrl.find('/', pos + 3);
            if (nextSlash != std::string::npos) {
                return pageUrl.substr(0, nextSlash) + url;
            } else {
                return pageUrl + url;
            }
        }
    }
    return url;
}

CoverManager& CoverManager::Instance() {
    static CoverManager s_instance;
    return s_instance;
}

CoverManager::CoverManager() {
    m_coversDir = paths::Join(paths::CacheDir(), L"covers");
    paths::EnsureDir(m_coversDir);
}

CoverManager::~CoverManager() {
    Shutdown();
}

void CoverManager::Init(std::function<void()> onCoverReady) {
    m_notifyCb = std::move(onCoverReady);
    if (m_workers.empty()) {
        m_stop = false;
        const int numWorkers = 3;
        for (int i = 0; i < numWorkers; ++i) {
            m_workers.emplace_back(&CoverManager::WorkerLoop, this);
        }
    }
}

void CoverManager::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_stop = true;
    }
    m_cv.notify_all();
    for (auto& w : m_workers) {
        if (w.joinable()) {
            w.join();
        }
    }
    m_workers.clear();
}

ID2D1Bitmap1* CoverManager::GetLocalBitmap(Renderer& r, const std::wstring& imagePath) {
    if (imagePath.empty()) return nullptr;
    if (r.DeviceGeneration() != m_lastDevGen) { m_bitmaps.clear();m_lastDevGen=r.DeviceGeneration(); }
    const std::string key="local:"+ToUtf8(imagePath);
    auto it=m_bitmaps.find(key);if(it!=m_bitmaps.end())return it->second.Get();
    UINT w=0,h=0;std::vector<uint8_t> pixels;
    if(DecodeImageFile(imagePath,180,w,h,pixels)) m_bitmaps[key]=r.CreateBitmap(w,h,pixels.data(),w*4);
    else m_bitmaps[key]=nullptr;
    return m_bitmaps[key].Get();
}

std::wstring CoverManager::GetCoverPath(const std::string& key) {
    wchar_t buf[64];
    swprintf_s(buf, L"%016llx.jpg", (unsigned long long)Fnv1a64(key));
    return paths::Join(m_coversDir, buf);
}

bool CoverManager::HasCover(const std::string& key) {
    std::wstring p = GetCoverPath(key);
    DWORD attr = GetFileAttributesW(p.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

ID2D1Bitmap1* CoverManager::GetBitmap(Renderer& r, const std::string& key,
                                      const std::string& mangaUrl, const std::string& mirror) {
    if (key.empty()) return nullptr;

    if (r.DeviceGeneration() != m_lastDevGen) {
        m_bitmaps.clear();
        m_lastDevGen = r.DeviceGeneration();
    }

    auto it = m_bitmaps.find(key);
    if (it != m_bitmaps.end()) {
        return it->second.Get();
    }

    std::wstring path = GetCoverPath(key);
    if (HasCover(key)) {
        UINT outW = 0, outH = 0;
        std::vector<uint8_t> bgra;
        // Scale cover down to max 350px width for fast rendering and low VRAM
        if (DecodeImageFile(path, 350, outW, outH, bgra) && !bgra.empty()) {
            ComPtr<ID2D1Bitmap1> bmp = r.CreateBitmap(outW, outH, bgra.data(), outW * 4);
            if (bmp) {
                ID2D1Bitmap1* raw = bmp.Get();
                m_bitmaps[key] = std::move(bmp);
                return raw;
            }
        }
    } else {
        // Enqueue background fetch if not already done
        if (!mangaUrl.empty()) {
            RequestCover(key, mangaUrl, mirror);
        }
    }

    return nullptr;
}

void CoverManager::RequestCover(const std::string& key, const std::string& mangaUrl, const std::string& mirror) {
    if (key.empty() || mangaUrl.empty()) return;
    if (HasCover(key)) return;

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_queuedKeys.count(key) || m_failedKeys.count(key)) return;
        m_queuedKeys.insert(key);
        m_queue.push_back({ key, mangaUrl, mirror });
    }
    m_cv.notify_all();
}

void CoverManager::QueueBatch(const std::vector<std::string>& keys, const std::vector<std::string>& urls, const std::vector<std::string>& mirrors) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    for (size_t i = 0; i < keys.size(); ++i) {
        const std::string& k = keys[i];
        if (k.empty() || m_queuedKeys.count(k) || m_failedKeys.count(k) || HasCover(k)) continue;
        const std::string& u = (i < urls.size()) ? urls[i] : "";
        const std::string& m = (i < mirrors.size()) ? mirrors[i] : "";
        if (u.empty()) continue;
        m_queuedKeys.insert(k);
        m_queue.push_back({ k, u, m });
    }
    m_cv.notify_all();
}

void CoverManager::WorkerLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    while (!m_stop) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_cv.wait(lock, [this]() { return m_stop || !m_queue.empty(); });
            if (m_stop) break;
            task = m_queue.front();
            m_queue.pop_front();
            if (!m_queue.empty()) {
                m_cv.notify_one();
            }
        }

        if (HasCover(task.key)) {
            continue;
        }

        bool ok = DownloadCover(task);
        if (!ok) {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_failedKeys.insert(task.key);
        }

        // Polite delay between downloads
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }

    CoUninitialize();
}

static bool IsValidCoverBuffer(const std::string& buf) {
    if (buf.size() < 12) return false;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(buf.data());
    // JPEG: FF D8 FF
    if (p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF) return true;
    // PNG: 89 50 4E 47
    if (p[0] == 0x89 && p[1] == 0x50 && p[2] == 0x4E && p[3] == 0x47) return true;
    // GIF: GIF8
    if (p[0] == 'G' && p[1] == 'I' && p[2] == 'F' && p[3] == '8') return true;
    // WebP: RIFF....WEBP
    if (p[0] == 'R' && p[1] == 'I' && p[2] == 'F' && p[3] == 'F' &&
        p[8] == 'W' && p[9] == 'E' && p[10] == 'B' && p[11] == 'P') return true;
    // BMP: BM
    if (p[0] == 'B' && p[1] == 'M') return true;
    return false;
}

bool CoverManager::DownloadCover(const Task& task) {
    if (task.url.empty()) return false;
    if (task.url.find("bato.to") != std::string::npos || task.url.find("batocomic.com") != std::string::npos) return false;

    // 1. Fetch the manga series HTML page
    net::HttpRequest req;
    req.url = task.url;
    req.timeoutMs = 5000;
    req.allowBrowserFallback = false; // Background cover scraping should never block or escalate to WebView2
    req.headers.push_back({ "User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36" });

    net::HttpResponse resp = net::Fetch(req, 80);
    if (!resp.success || resp.body.empty()) {
        return false;
    }

    // 2. Extract image URL
    std::string coverUrl = ExtractCoverUrlFromHtml(resp.body, task.url);
    if (coverUrl.empty()) {
        return false;
    }

    coverUrl = ResolveUrl(coverUrl, task.url);

    // 3. Download the cover image
    net::HttpRequest imgReq;
    imgReq.url = coverUrl;
    imgReq.timeoutMs = 7000;
    imgReq.allowBrowserFallback = false;
    imgReq.headers.push_back({ "Referer", task.url });
    imgReq.headers.push_back({ "User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36" });

    net::HttpResponse imgResp = net::Fetch(imgReq, 50);
    if (!imgResp.success || imgResp.body.size() < 400 || !IsValidCoverBuffer(imgResp.body)) {
        return false;
    }

    // 4. Save to disk
    std::wstring outPath = GetCoverPath(task.key);
    if (fileio::WriteAtomic(outPath, imgResp.body)) {
        log::Write(L"[CoverManager] Cover cached for key: %s (size: %zu bytes)",
                   FromUtf8(task.key).c_str(), imgResp.body.size());
        if (m_notifyCb) {
            m_notifyCb();
        }
        return true;
    }

    return false;
}

} // namespace gm::engine
