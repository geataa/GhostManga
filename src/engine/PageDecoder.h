#pragma once
//
// Sayfa goruntulerini arka plan is parcaciginda WIC ile cozer.
//
// Iki is turu var:
//   Olcum: tum sayfalarin piksel boyutu, sirayla. Duzen bunlara dayanir.
//   Cozum: gorunen ve yakindaki sayfalar, hedef genislige olceklenmis
//          premultiplied BGRA piksel. Oncelik = gorunume uzaklik.
//
// Direct2D bitmap'e cevirme UI is parcaciginda yapilir (tek is parcacikli
// D2D fabrikasi). Sonuclar hazir olunca notify() cagrilir; cagiran taraf
// bunu PostMessage ile UI'ya tasir.
//
#include <windows.h>

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gm {

// Tek sayfa icin bellek tavani: 48 milyon piksel (~192 MB). Webtoon seritleri
// 800x60000 olabiliyor; tavani asan sayfa orantili kucultulur. Okuyucu da
// istedigi genisligi buna gore sinirlar, yoksa "daha keskin" istegi hic
// karsilanmaz ve sonsuz yeniden cozme dongusu olur.
inline constexpr uint64_t kMaxDecodePixels = 48ull * 1024 * 1024;

struct ProbeResult {
    uint64_t gen = 0;
    int index = 0;
    UINT w = 0, h = 0;
    bool ok = false;
};

struct DecodeResult {
    uint64_t gen = 0;
    int index = 0;
    UINT w = 0, h = 0;          // cozulen boyut
    std::vector<uint8_t> bgra;  // stride = w * 4
    bool ok = false;
};

class PageDecoder {
public:
    explicit PageDecoder(std::function<void()> notify) : m_notify(std::move(notify)) {}
    ~PageDecoder() { Stop(); }

    void Start();
    void Stop();

    // Yeni kaynak. Onceki tum isler duser. Donen nesil numarasi sonuclarda gelir.
    uint64_t SetSource(std::vector<std::wstring> paths);

    // Kademeli indirmede yeni inen sayfanin yolunu gunceller (nesil degismez, onceki cozumler korunur)
    void UpdatePath(int index, const std::wstring& path);

    // Ayni sayfa icin bekleyen istek varsa yenisi onun yerine gecer.
    void Request(uint64_t gen, int index, UINT targetW, float priority);
    void CancelRequest(uint64_t gen, int index);

    void Take(std::vector<ProbeResult>& probes, std::vector<DecodeResult>& decodes);

private:
    struct Req { int index; UINT w; float prio; };

    void Loop();

    std::function<void()> m_notify;
    std::thread m_thread;
    std::mutex m_mu;
    std::condition_variable m_cv;
    bool m_stop = false;

    uint64_t m_gen = 0;
    std::vector<std::wstring> m_paths;
    size_t m_probeNext = 0;
    std::vector<Req> m_reqs;

    std::vector<ProbeResult> m_probes;
    std::vector<DecodeResult> m_decodes;
};

} // namespace gm
