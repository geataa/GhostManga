#include "engine/PageDecoder.h"
#include "core/Log.h"

#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace gm {
namespace {

constexpr uint64_t kMaxPixels = kMaxDecodePixels;

bool Probe(IWICImagingFactory* f, const std::wstring& path, UINT& w, UINT& h) {
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                            WICDecodeMetadataCacheOnDemand, &dec)))
        return false;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, &frame))) return false;
    return SUCCEEDED(frame->GetSize(&w, &h)) && w > 0 && h > 0;
}

bool Decode(IWICImagingFactory* f, const std::wstring& path, UINT targetW, DecodeResult& out) {
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                            WICDecodeMetadataCacheOnDemand, &dec)))
        return false;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, &frame))) return false;
    UINT w = 0, h = 0;
    if (FAILED(frame->GetSize(&w, &h)) || !w || !h) return false;

    ComPtr<IWICFormatConverter> conv;
    if (FAILED(f->CreateFormatConverter(&conv))) return false;
    if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                nullptr, 0.0, WICBitmapPaletteTypeCustom)))
        return false;

    UINT outW = (targetW && targetW < w) ? targetW : w;
    UINT outH = std::max<UINT>(1, (UINT)std::lround((double)h * outW / w));
    if ((uint64_t)outW * outH > kMaxPixels) {
        const double k = std::sqrt((double)kMaxPixels / ((double)outW * outH));
        outW = std::max<UINT>(1, (UINT)(outW * k));
        outH = std::max<UINT>(1, (UINT)(outH * k));
    }

    IWICBitmapSource* src = conv.Get();
    ComPtr<IWICBitmapScaler> scaler;
    if (outW != w || outH != h) {
        if (FAILED(f->CreateBitmapScaler(&scaler))) return false;
        if (FAILED(scaler->Initialize(conv.Get(), outW, outH, WICBitmapInterpolationModeHighQualityCubic)) &&
            FAILED(scaler->Initialize(conv.Get(), outW, outH, WICBitmapInterpolationModeFant)))
            return false;
        src = scaler.Get();
    }

    const UINT stride = outW * 4;
    try {
        out.bgra.resize((size_t)stride * outH);
    } catch (...) {
        return false;
    }
    if (FAILED(src->CopyPixels(nullptr, stride, (UINT)out.bgra.size(), out.bgra.data()))) {
        out.bgra.clear();
        return false;
    }
    out.w = outW;
    out.h = outH;
    return true;
}

} // namespace

void PageDecoder::Start() {
    if (m_thread.joinable()) return;
    m_stop = false;
    m_thread = std::thread([this] { Loop(); });
}

void PageDecoder::Stop() {
    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_stop = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

uint64_t PageDecoder::SetSource(std::vector<std::wstring> paths) {
    uint64_t gen;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        gen = ++m_gen;
        m_paths = std::move(paths);
        m_probeNext = 0;
        m_reqs.clear();
        m_probes.clear();
        m_decodes.clear();
    }
    m_cv.notify_all();
    return gen;
}

void PageDecoder::UpdatePath(int index, const std::wstring& path) {
    {
        std::lock_guard<std::mutex> lk(m_mu);
        if (index < 0) return;
        if ((size_t)index >= m_paths.size()) {
            m_paths.resize((size_t)index + 1);
        }
        m_paths[(size_t)index] = path;
        if ((size_t)index < m_probeNext) {
            m_probeNext = (size_t)index;
        }
    }
    m_cv.notify_all();
}

void PageDecoder::Request(uint64_t gen, int index, UINT targetW, float priority) {
    {
        std::lock_guard<std::mutex> lk(m_mu);
        if (gen != m_gen || index < 0 || index >= (int)m_paths.size()) return;
        for (auto& r : m_reqs) {
            if (r.index == index) {
                r.w = targetW;
                r.prio = priority;
                return;
            }
        }
        m_reqs.push_back({ index, targetW, priority });
    }
    m_cv.notify_all();
}

void PageDecoder::CancelRequest(uint64_t gen, int index) {
    std::lock_guard<std::mutex> lk(m_mu);
    if (gen != m_gen) return;
    m_reqs.erase(std::remove_if(m_reqs.begin(), m_reqs.end(), [&](const Req& r) { return r.index == index; }),
                 m_reqs.end());
}

void PageDecoder::Take(std::vector<ProbeResult>& probes, std::vector<DecodeResult>& decodes) {
    std::lock_guard<std::mutex> lk(m_mu);
    probes.swap(m_probes);
    decodes.swap(m_decodes);
    m_probes.clear();
    m_decodes.clear();
}

void PageDecoder::Loop() {
    const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
        log::Write(L"PageDecoder: WIC fabrikasi olusturulamadi.");
    }

    for (;;) {
        std::unique_lock<std::mutex> lk(m_mu);
        m_cv.wait(lk, [&] { return m_stop || m_probeNext < m_paths.size() || !m_reqs.empty(); });
        if (m_stop) break;
        const uint64_t gen = m_gen;

        // En acil cozum istegi
        int best = -1;
        for (size_t i = 0; i < m_reqs.size(); ++i) {
            if (best < 0 || m_reqs[i].prio < m_reqs[(size_t)best].prio) best = (int)i;
        }
        const bool urgent = best >= 0 && m_reqs[(size_t)best].prio < 1.0f;

        if (!urgent && m_probeNext < m_paths.size()) {
            // Olcumleri kucuk partilerle yap; araya gorunen sayfa cozumu girebilsin.
            std::vector<std::pair<int, std::wstring>> batch;
            while (m_probeNext < m_paths.size() && batch.size() < 8) {
                batch.emplace_back((int)m_probeNext, m_paths[m_probeNext]);
                ++m_probeNext;
            }
            lk.unlock();
            std::vector<ProbeResult> out;
            for (auto& [idx, path] : batch) {
                if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
                ProbeResult pr;
                pr.gen = gen;
                pr.index = idx;
                pr.ok = wic && Probe(wic.Get(), path, pr.w, pr.h);
                if (pr.ok) out.push_back(pr);
            }
            lk.lock();
            if (gen == m_gen && !out.empty()) {
                for (auto& p : out) m_probes.push_back(p);
            }
            lk.unlock();
            if (!out.empty() && m_notify) m_notify();
            continue;
        }

        if (best < 0) continue;
        const Req req = m_reqs[(size_t)best];
        m_reqs.erase(m_reqs.begin() + best);
        const std::wstring path = m_paths[(size_t)req.index];
        lk.unlock();

        DecodeResult dr;
        dr.gen = gen;
        dr.index = req.index;
        dr.ok = wic && Decode(wic.Get(), path, req.w, dr);
        if (!dr.ok) log::Write(L"Cozulemedi: %s", path.c_str());

        lk.lock();
        if (gen == m_gen) m_decodes.push_back(std::move(dr));
        lk.unlock();
        if (m_notify) m_notify();
    }

    wic.Reset();
    if (SUCCEEDED(hrCo)) CoUninitialize();
}

} // namespace gm
