#pragma once
//
// Anlik kip (immediate mode) etkilesim yardimcisi. Cizim cagiran tarafta;
// burasi yalnizca "hangi oge uzerinde, hangisine basildi, tiklandi mi"
// sorularini cevaplar.
//
// Kurallar (FullTerminal'den tasinan dersler):
//  - Eylem fare BIRAKILINCA calisir, basilinca degil.
//  - Ust katmanlar (cekmece, baslik ortusu) alttakilerin tiklamasini
//    Occlude() ile keser.
//
#include "ui/Renderer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gm {

struct Input {
    float mx = -1.0f, my = -1.0f;
    bool down = false;      // sol tus basili
    bool pressed = false;   // bu karede basildi
    bool released = false; // bu karede birakildi
};

class Ui {
public:
    void Begin(const Input& in, double now) {
        m_in = in;
        m_now = now;
        m_hot = 0;
        m_tipText.clear();
        m_occ.clear();
    }

    void End() {
        if (m_hot != m_hotPrev) {
            m_hotPrev = m_hot;
            m_hotSince = m_now;
        }
        if (m_in.released) m_active = 0;
    }

    void Occlude(const D2D1_RECT_F& r) { m_occ.push_back(r); }
    void ClearOcclusion() { m_occ.clear(); }

    bool Over(const D2D1_RECT_F& r) const {
        if (!RcHit(r, m_in.mx, m_in.my)) return false;
        for (const auto& o : m_occ) if (RcHit(o, m_in.mx, m_in.my)) return false;
        return true;
    }

    // Tiklandiysa true.
    bool Button(uint32_t id, const D2D1_RECT_F& r, bool enabled = true) {
        const bool over = Over(r);
        if (over) m_hot = id;
        if (!enabled) return false;
        if (over && m_in.pressed) m_active = id;
        return m_in.released && m_active == id && over;
    }

    bool Hot(uint32_t id) const { return m_hot == id; }
    bool Pressed(uint32_t id) const { return m_active == id && m_in.down; }
    uint32_t HotId() const { return m_hot; }
    void CancelActive() { m_active = 0; }

    void Tip(uint32_t id, const std::wstring& text, const D2D1_RECT_F& anchor) {
        if (m_hot == id && !text.empty()) {
            m_tipText = text;
            m_tipAnchor = anchor;
        }
    }
    const std::wstring& TipText() const { return m_tipText; }
    const D2D1_RECT_F& TipAnchor() const { return m_tipAnchor; }
    double HotSince() const { return m_hotSince; }
    bool HotStable() const { return m_hot != 0 && m_hot == m_hotPrev; }

    const Input& In() const { return m_in; }

private:
    Input m_in;
    double m_now = 0.0;
    uint32_t m_hot = 0, m_hotPrev = 0, m_active = 0;
    double m_hotSince = 0.0;
    std::wstring m_tipText;
    D2D1_RECT_F m_tipAnchor{};
    std::vector<D2D1_RECT_F> m_occ;
};

// Ogeler icin kararli kimlik: FNV-1a
constexpr uint32_t Id(const char* s, uint32_t salt = 0) {
    uint32_t h = 2166136261u ^ salt;
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 16777619u;
    }
    return h ? h : 1u;
}

} // namespace gm
