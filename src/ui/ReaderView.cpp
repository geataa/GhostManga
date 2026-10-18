#include "ui/ReaderView.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Utf8.h"
#include "engine/LocalSource.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <cmath>

namespace gm {
namespace {

constexpr float kPlaceholderW = 1000.0f;
constexpr float kPlaceholderH = 1440.0f;
constexpr float kMinZoom = 0.25f, kMaxZoom = 5.0f;
constexpr int   kKeepBehind = 3, kKeepAhead = 6;
constexpr UINT  kTileH = 4096;

std::wstring Fmt(const wchar_t* fmt, int v) {
    wchar_t buf[128];
    swprintf_s(buf, fmt, v);
    return buf;
}

std::wstring CleanChapterTitle(const std::wstring& rawTitle, int fallbackIdx = -1) {
    std::wstring t = rawTitle;
    while (!t.empty() && (t.front() == L' ' || t.front() == L'\t' || t.front() == L'\r' || t.front() == L'\n')) t.erase(t.begin());
    while (!t.empty() && (t.back() == L' ' || t.back() == L'\t' || t.back() == L'\r' || t.back() == L'\n')) t.pop_back();

    while (!t.empty() && (t.back() == L'-' || t.back() == L':' || t.back() == L'—' || t.back() == L' ' || t.back() == L'|')) {
        t.pop_back();
        while (!t.empty() && t.back() == L' ') t.pop_back();
    }

    if (t.empty()) {
        if (fallbackIdx >= 0) return i18n::Literal(L"Bölüm ") + std::to_wstring(fallbackIdx + 1);
        return L"";
    }

    bool onlyNum = true;
    for (wchar_t c : t) {
        if (!iswdigit(c) && c != L'.' && c != L' ' && c != L'#') { onlyNum = false; break; }
    }
    if (onlyNum) {
        while (!t.empty() && t.front() == L'#') t.erase(t.begin());
        while (!t.empty() && t.front() == L' ') t.erase(t.begin());
        return i18n::Literal(L"Bölüm ") + t;
    }

    // Parse the source prefix, not its translation: localized prefixes can
    // be shorter than eight characters and may already come from MainWindow.
    if (t.rfind(L"Chapter ", 0) == 0) t = i18n::Literal(L"Bölüm ") + t.substr(8);
    else if (t.rfind(L"chapter ", 0) == 0) t = i18n::Literal(L"Bölüm ") + t.substr(8);
    else if (t.rfind(L"Ch. ", 0) == 0) t = i18n::Literal(L"Bölüm ") + t.substr(4);
    else if (t.rfind(L"Ch.", 0) == 0) t = i18n::Literal(L"Bölüm ") + t.substr(3);

    size_t sep = t.find_first_of(L":-—|");
    if (sep != std::wstring::npos) {
        std::wstring leftPart = t.substr(0, sep);
        std::wstring rightPart = t.substr(sep + 1);
        while (!leftPart.empty() && leftPart.back() == L' ') leftPart.pop_back();
        while (!rightPart.empty() && rightPart.front() == L' ') rightPart.erase(rightPart.begin());
        while (!rightPart.empty() && rightPart.back() == L' ') rightPart.pop_back();

        if (rightPart.empty()) {
            t = leftPart;
        } else {
            std::wstring normRight = rightPart;
            if (normRight.rfind(L"Chapter ", 0) == 0) normRight = i18n::Literal(L"Bölüm ") + normRight.substr(8);
            else if (normRight.rfind(L"Ch. ", 0) == 0) normRight = i18n::Literal(L"Bölüm ") + normRight.substr(4);
            else if (normRight.rfind(L"Ch.", 0) == 0) normRight = i18n::Literal(L"Bölüm ") + normRight.substr(3);

            bool rightOnlyNum = true;
            for (wchar_t c : rightPart) {
                if (!iswdigit(c) && c != L'.' && c != L' ') { rightOnlyNum = false; break; }
            }

            if (normRight == leftPart || rightOnlyNum) {
                t = leftPart;
            }
        }
    }

    return t;
}

} // namespace

// ------------------------------------------------------------- yasam --------

void ReaderView::Init(ReaderHost* host, Settings* settings) {
    m_host = host;
    m_set = settings;
    m_decoder = std::make_unique<PageDecoder>([this] {
        if (m_host) m_host->DecoderSignaled();
    });
    m_decoder->Start();
}

void ReaderView::SetMangaSettings(const std::string& seriesKey, bool hasCustom, bool webtoon, FitMode fit, bool doublePage, bool rtl, float zoom) {
    m_seriesKey = seriesKey;
    m_hasCustomSettings = hasCustom;
    m_webtoon = webtoon;
    m_fit = fit;
    m_doublePage = doublePage;
    m_rtl = rtl;
    if (hasCustom && zoom > 0.05f) {
        m_zoom = zoom;
    }
    m_layoutDirty = true;
}

void ReaderView::Shutdown() {
    if (m_decoder) m_decoder->Stop();
    m_decoder.reset();
    m_pages.clear();
    m_rows.clear();
}

bool ReaderView::Open(const std::wstring& chapterDir) {
    std::vector<std::wstring> files = local::ListImages(chapterDir);
    if (files.empty()) return false;

    m_isOpen = true;
    m_pages.clear();
    m_pages.resize(files.size());
    for (size_t i = 0; i < files.size(); ++i) m_pages[i].path = files[i];
    m_probedCount = 0;
    m_gen = m_decoder->SetSource(std::move(files));

    // Eski bolumun satirlari yeni bolumun ilk duzenine capa olmasin.
    m_rows.clear();
    m_docW = m_docH = 0.0f;

    m_dir = chapterDir;
    m_chapter = paths::FileName(chapterDir);
    m_series = paths::FileName(paths::ParentDir(chapterDir));
    m_nextDir = local::SiblingChapter(chapterDir, +1);
    m_prevDir = local::SiblingChapter(chapterDir, -1);

    m_scrollY = m_targetY = 0.0f;
    m_scrollX = m_targetX = 0.0f;
    m_thumbScrollX = m_thumbTargetScrollX = 0.0f;
    m_thumbMaxScrollX = 0.0f;
    m_thumbDragging = false;
    m_thumbDragMoved = false;
    m_thumbClickCandidate = -1;
    m_lastAutoScrolledPage = -1;
    if (!m_hasCustomSettings || m_zoom < 0.05f) {
        m_zoom = 1.0f;
    }
    m_overscroll = 0.0f;
    m_popOpen = false;
    m_helpOpen = false;
    m_layoutDirty = true;
    m_chrome = 1.0f;
    m_lastActivity = 0.0; // ilk Draw'da simdiki zamana cekilir
    log::Write(L"Okuyucu: %s (%d sayfa)", chapterDir.c_str(), (int)m_pages.size());
    if (m_host) m_host->ChapterOpened(chapterDir);
    return true;
}

bool ReaderView::OpenProgressive(const std::wstring& chapterDir, const std::wstring& series, const std::wstring& chapter, int totalPages) {
    m_dir = chapterDir;
    if (!series.empty()) m_series = series;
    if (!chapter.empty()) m_chapter = chapter;
    m_isOpen = true;
    m_expectedTotalPages = totalPages;

    std::vector<std::wstring> existingFiles = chapterDir.empty() ? std::vector<std::wstring>{} : local::ListImages(chapterDir);
    int count = std::max(totalPages, (int)existingFiles.size());
    if (count <= 0) count = 1; // En az 1 bekleme kutusu

    m_pages.clear();
    m_pages.resize(count);

    std::vector<std::wstring> decoderSource(count);
    for (int i = 0; i < count; ++i) {
        wchar_t buf[64];
        swprintf_s(buf, L"%04d.jpg", i + 1);
        std::wstring p = chapterDir.empty() ? L"" : (chapterDir + L"\\" + buf);
        if (!chapterDir.empty() && GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::wstring pPng = chapterDir + L"\\" + Fmt(L"%04d.png", i + 1);
            if (GetFileAttributesW(pPng.c_str()) != INVALID_FILE_ATTRIBUTES) p = pPng;
            else {
                std::wstring pWebp = chapterDir + L"\\" + Fmt(L"%04d.webp", i + 1);
                if (GetFileAttributesW(pWebp.c_str()) != INVALID_FILE_ATTRIBUTES) p = pWebp;
            }
        }
        m_pages[i].path = p;
        m_pages[i].probed = false;
        m_pages[i].natW = 800;
        m_pages[i].natH = 200;
        if (!p.empty() && GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
            decoderSource[i] = p;
        } else {
            decoderSource[i] = L"";
        }
    }

    if (m_decoder) {
        m_gen = m_decoder->SetSource(std::move(decoderSource));
    }

    m_nextDir.clear();
    m_prevDir.clear();
    m_rows.clear();
    m_docW = m_docH = 0.0f;
    m_scrollY = m_targetY = 0.0f;
    m_scrollX = m_targetX = 0.0f;
    m_thumbScrollX = m_thumbTargetScrollX = 0.0f;
    m_thumbMaxScrollX = 0.0f;
    m_thumbDragging = false;
    m_thumbDragMoved = false;
    m_thumbClickCandidate = -1;
    m_lastAutoScrolledPage = -1;
    if (!m_hasCustomSettings || m_zoom < 0.05f) {
        m_zoom = 1.0f;
    }
    m_overscroll = 0.0f;
    m_popOpen = false;
    m_helpOpen = false;
    m_layoutDirty = true;
    m_chrome = 1.0f;
    m_lastActivity = 0.0;
    return true;
}

void ReaderView::SetProgressiveCount(int totalPages) {
    if (totalPages <= 0) return;
    m_expectedTotalPages = totalPages;
    if ((int)m_pages.size() < totalPages) {
        size_t oldSz = m_pages.size();
        m_pages.resize(totalPages);
        for (size_t i = oldSz; i < (size_t)totalPages; ++i) {
            wchar_t buf[64];
            swprintf_s(buf, L"%04d.jpg", (int)i + 1);
            m_pages[i].path = m_dir.empty() ? L"" : (m_dir + L"\\" + buf);
            m_pages[i].probed = false;
            m_pages[i].natW = 800;
            m_pages[i].natH = 200;
            m_pages[i].pendingW = 0;
            if (m_decoder) {
                m_decoder->UpdatePath((int)i, L"");
            }
        }
        m_layoutDirty = true;
        if (m_host) m_host->Invalidate();
    }
}

void ReaderView::CheckProgressiveOnDisk() {
    if (m_dir.empty()) return;
    for (size_t i = 0; i < m_pages.size(); ++i) {
        Page& p = m_pages[i];
        if (!p.probed) {
            if (p.path.empty()) {
                wchar_t buf[64];
                swprintf_s(buf, L"%04d.jpg", (int)i + 1);
                p.path = m_dir + L"\\" + buf;
            }
            if (GetFileAttributesW(p.path.c_str()) == INVALID_FILE_ATTRIBUTES) {
                wchar_t bPng[64], bWebp[64];
                swprintf_s(bPng, L"%04d.png", (int)i + 1);
                swprintf_s(bWebp, L"%04d.webp", (int)i + 1);
                std::wstring cPng = m_dir + L"\\" + bPng;
                std::wstring cWebp = m_dir + L"\\" + bWebp;
                if (GetFileAttributesW(cPng.c_str()) != INVALID_FILE_ATTRIBUTES) p.path = cPng;
                else if (GetFileAttributesW(cWebp.c_str()) != INVALID_FILE_ATTRIBUTES) p.path = cWebp;
            }
            if (!p.path.empty() && GetFileAttributesW(p.path.c_str()) != INVALID_FILE_ATTRIBUTES) {
                p.pendingW = 0;
                if (m_decoder) {
                    m_decoder->UpdatePath((int)i, p.path);
                }
            }
        }
    }
}

void ReaderView::OnPageDownloaded(int index, int totalPages, const std::wstring& pagePath) {
    if (totalPages > 0 && (int)m_pages.size() < totalPages) {
        SetProgressiveCount(totalPages);
    }
    if (index >= 0 && index < (int)m_pages.size()) {
        Page& p = m_pages[(size_t)index];
        p.path = pagePath;
        p.failed = false;
        p.pendingW = 0;
        if (m_decoder) {
            m_decoder->UpdatePath(index, pagePath);
        }
    }
    m_layoutDirty = true;
    if (m_host) m_host->Invalidate();
}

void ReaderView::Close() {
    m_isOpen = false;
    if (m_decoder) m_gen = m_decoder->SetSource({});
    m_pages.clear();
    m_rows.clear();
    m_docW = m_docH = 0.0f;
    m_dir.clear();
    m_scrollDragging = false; m_scrollTrack = {}; m_scrollThumb = {};
    m_thumbStripRc = D2D1_RECT_F{};
    m_thumbScrollX = m_thumbTargetScrollX = m_thumbMaxScrollX = 0.0f;
    m_thumbDragging = false;
    m_thumbDragMoved = false;
    m_thumbClickCandidate = -1;
    m_lastAutoScrolledPage = -1;
}

UINT ReaderView::CapWidth(const Page& p) const {
    // Cozucunun piksel tavanina gore bu sayfanin alabilecegi en genis cozum.
    if (!p.natW || !p.natH) return 0;
    const double w = std::floor(std::sqrt((double)kMaxDecodePixels * p.natW / p.natH));
    return (UINT)std::max(1.0, std::min(w, (double)p.natW));
}

// ------------------------------------------------------------- duzen --------

void ReaderView::NatSize(int i, float& w, float& h) const {
    const Page& p = m_pages[(size_t)i];
    if (p.probed && !p.failed && p.natW && p.natH) {
        w = (float)p.natW;
        h = (float)p.natH;
        return;
    }
    if (!p.probed) {
        w = 800.0f;
        h = 190.0f;
        return;
    }
    // Olculmemis sayfa: ilk olculmus sayfanin oranini kullan.
    for (const Page& q : m_pages) {
        if (q.probed && !q.failed && q.natW && q.natH) {
            w = (float)q.natW;
            h = (float)q.natH;
            return;
        }
    }
    w = kPlaceholderW;
    h = kPlaceholderH;
}

bool ReaderView::Wide(int i) const {
    if (!m_pages[(size_t)i].probed) return false;
    float w, h;
    NatSize(i, w, h);
    return w > h * 1.05f;
}

void ReaderView::FitSingle(int i, float availW, float availH, float& w, float& h) const {
    const Page& p = m_pages[(size_t)i];
    if (!p.probed) {
        w = std::min(availW, 800.0f * m_scale);
        h = 190.0f * m_scale;
        return;
    }
    float nw, nh;
    NatSize(i, nw, nh);
    float k = 1.0f;
    switch (CurrentFit()) {
    case FitMode::Width:    k = availW / nw; break;
    case FitMode::Height:   k = availH / nh; break;
    case FitMode::Window:   k = std::min(availW / nw, availH / nh); break;
    case FitMode::Original: k = 1.0f; break;
    }
    if (!m_set->scaleUp && CurrentFit() != FitMode::Original) k = std::min(k, 1.0f);
    w = nw * k * m_zoom;
    h = nh * k * m_zoom;
}

void ReaderView::Layout() {
    m_rows.clear();
    const float s = m_scale;
    const float viewW = RcW(m_area), viewH = RcH(m_area);
    const bool webtoon = IsWebtoon();
    const float margin = webtoon ? 0.0f : 24.0f * s;
    const float gapY = webtoon ? 0.0f : 16.0f * s;
    const float gapX = webtoon ? 0.0f : 8.0f * s;
    const float padTop = webtoon ? 0.0f : 16.0f * s;
    const float availW = std::max(64.0f, viewW - 2.0f * margin);
    const float availH = std::max(64.0f, viewH - (webtoon ? 0.0f : 32.0f * s));

    float y = padTop;
    float maxRowW = 0.0f;
    const int n = (int)m_pages.size();
    std::vector<float> rowW;

    int i = 0;
    while (i < n) {
        Row row;
        row.y = y;
        const bool pair = IsDoublePage() && i + 1 < n && !(m_set->coverAlone && i == 0) &&
                          !Wide(i) && !Wide(i + 1);
        float total;
        if (!pair) {
            float w, h;
            FitSingle(i, availW, availH, w, h);
            row.a = i;
            row.ra = RcWH(0, 0, w, h);
            row.h = h;
            total = w;
            i += 1;
        } else {
            float wa, ha, wb, hb;
            NatSize(i, wa, ha);
            NatSize(i + 1, wb, hb);
            const float aspA = wa / ha, aspB = wb / hb;
            float hRow = 0.0f;
            switch (CurrentFit()) {
            case FitMode::Width:    hRow = (availW - gapX) / (aspA + aspB); break;
            case FitMode::Height:   hRow = availH; break;
            case FitMode::Window:   hRow = std::min(availH, (availW - gapX) / (aspA + aspB)); break;
            case FitMode::Original: hRow = std::max(ha, hb); break;
            }
            if (!m_set->scaleUp && CurrentFit() != FitMode::Original) hRow = std::min(hRow, std::min(ha, hb));
            hRow *= m_zoom;
            const float w1 = hRow * aspA, w2 = hRow * aspB;
            row.a = i;
            row.b = i + 1;
            // Sagdan sola: ilk sayfa sagda.
            if (IsRtl()) {
                row.rb = RcWH(0, 0, w2, hRow);
                row.ra = RcWH(w2 + gapX, 0, w1, hRow);
            } else {
                row.ra = RcWH(0, 0, w1, hRow);
                row.rb = RcWH(w1 + gapX, 0, w2, hRow);
            }
            row.h = hRow;
            total = w1 + gapX + w2;
            i += 2;
        }
        rowW.push_back(total);
        maxRowW = std::max(maxRowW, total);
        m_rows.push_back(row);
        y += row.h + gapY;
    }

    // Bolum sonu karti: sihirli kaydirmanin hedefi.
    Row end;
    end.endCard = true;
    end.y = y + (webtoon ? 16.0f * s : 0.0f);
    end.h = 132.0f * s;
    const float cardW = std::min(520.0f * s, std::max(240.0f * s, viewW - 48.0f * s));
    end.ra = RcWH(0, 0, cardW, end.h);
    rowW.push_back(cardW);
    m_rows.push_back(end);
    y = end.y + end.h;

    // Alt HUD'un altinda son satir kaybolmasin.
    m_docH = y + (theme::HudH + theme::HudMargin + 16.0f) * s;
    m_docW = std::max(maxRowW + 2.0f * margin, viewW);

    for (size_t k = 0; k < m_rows.size(); ++k) {
        Row& r = m_rows[k];
        const float x0 = (m_docW - rowW[k]) * 0.5f;
        auto place = [&](D2D1_RECT_F& rc) {
            const float w = RcW(rc), h = RcH(rc);
            rc = RcWH(x0 + rc.left, r.y + (r.h - h) * 0.5f, w, h);
        };
        place(r.ra);
        if (r.b >= 0) place(r.rb);
    }

    m_layoutW = viewW;
    m_layoutH = viewH;
    m_layoutScale = s;
    m_layoutDirty = false;
}

int ReaderView::RowAt(float docY) const {
    if (m_rows.empty()) return 0;
    // Satirlar y'ye gore sirali: ikili arama.
    int lo = 0, hi = (int)m_rows.size() - 1;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (m_rows[(size_t)mid].y <= docY) lo = mid; else hi = mid - 1;
    }
    return lo;
}

ReaderView::Anchor ReaderView::CaptureAnchor(float screenY) const {
    Anchor a;
    a.screenY = screenY;
    if (m_rows.empty()) return a;
    const float docY = m_scrollY + (screenY - m_area.top);
    a.row = RowAt(docY);
    const Row& r = m_rows[(size_t)a.row];
    // Kirpma yok: gorunumun ustu ilk satirin ustundeki bosluktaysa (negatif)
    // ya da iki satir arasindaki bosluktaysa (>1) o konum da korunmali.
    a.frac = r.h > 0.0f ? (docY - r.y) / r.h : 0.0f;
    const float vw = RcW(m_area);
    a.fracX = m_docW > vw ? (m_scrollX + vw * 0.5f) / m_docW : 0.5f;
    return a;
}

void ReaderView::ApplyAnchor(const Anchor& a) {
    if (m_rows.empty()) return;
    const int idx = std::clamp(a.row, 0, (int)m_rows.size() - 1);
    const Row& r = m_rows[(size_t)idx];
    const float docY = r.y + a.frac * r.h;
    m_scrollY = m_targetY = std::clamp(docY - (a.screenY - m_area.top), 0.0f, MaxScrollY());
    const float vw = RcW(m_area);
    m_scrollX = m_targetX = std::clamp(a.fracX * m_docW - vw * 0.5f, 0.0f, MaxScrollX());
}

void ReaderView::RelayoutKeepingPosition(float screenY) {
    const bool had = !m_rows.empty();
    const Anchor a = CaptureAnchor(screenY);
    // Suren kaydirma animasyonunun kalan yolu ve surukleme baslangici da
    // korunmali; yoksa olcum gelince animasyon yarida kesilir, surukleme ziplar.
    const float pendY = m_targetY - m_scrollY;
    const float pendX = m_targetX - m_scrollX;
    const float oldY = m_scrollY, oldX = m_scrollX;
    Layout();
    if (!had) return;
    ApplyAnchor(a);
    m_targetY = std::clamp(m_scrollY + pendY, 0.0f, MaxScrollY());
    m_targetX = std::clamp(m_scrollX + pendX, 0.0f, MaxScrollX());
    if (m_dragging) {
        m_dragSy += m_scrollY - oldY;
        m_dragSx += m_scrollX - oldX;
    }
}

float ReaderView::MaxScrollY() const { return std::max(0.0f, m_docH - RcH(m_area)); }
float ReaderView::MaxScrollX() const { return std::max(0.0f, m_docW - RcW(m_area)); }

void ReaderView::SettingsChanged() {
    m_layoutDirty = true;
}

// --------------------------------------------------------- kaydirma ---------

void ReaderView::ScrollTo(float y, bool animate) {
    m_targetY = std::clamp(y, 0.0f, MaxScrollY());
    if (!animate) m_scrollY = m_targetY;
    if (m_host) m_host->Invalidate();
}

void ReaderView::ScrollBy(float dy) { ScrollTo(m_targetY + dy); }

int ReaderView::CurrentRow() const {
    if (m_rows.empty()) return 0;
    return RowAt(m_scrollY + RcH(m_area) * 0.35f);
}

int ReaderView::CurrentPageIndex() const {
    if (m_rows.empty()) return 0;
    int r = CurrentRow();
    if (r >= 0 && r < (int)m_rows.size()) {
        int a = m_rows[(size_t)r].a;
        if (a >= 0) return a;
    }
    return 0;
}

void ReaderView::GoToPage(int page) {
    if (m_rows.empty() || page < 0) return;
    for (size_t ri = 0; ri < m_rows.size(); ++ri) {
        if (m_rows[ri].a == page || m_rows[ri].b == page) {
            ScrollTo(m_rows[ri].y - AlignPad(), false);
            return;
        }
    }
}

// Satira atlarken ustte AlignPad kadar pay birakiyoruz. "Su an hangi
// satirdayim" sorusu ayni payla sorulmali: yoksa atlanan satirin ustundeki
// pay bir onceki satira sayilir ve ikinci basista ayni yere atlanip takilir.
void ReaderView::NextPage() {
    if (m_rows.empty()) return;
    const float viewH = RcH(m_area);
    const int cur = RowAt(m_targetY + AlignPad() + 1.0f);
    const Row& r = m_rows[(size_t)cur];
    // Satir ekrandan uzunsa (webtoon seridi) ekran ekran kaydir.
    if (r.y + r.h > m_targetY + viewH + 1.0f) {
        ScrollTo(std::min(m_targetY + viewH * 0.9f, r.y + r.h - viewH * 0.1f));
        return;
    }
    if (cur + 1 < (int)m_rows.size()) ScrollTo(m_rows[(size_t)cur + 1].y - AlignPad());
    else ScrollTo(MaxScrollY());
}

void ReaderView::PrevPage() {
    if (m_rows.empty()) return;
    const float viewH = RcH(m_area);
    const int cur = RowAt(m_targetY + AlignPad() + 1.0f);
    const Row& r = m_rows[(size_t)cur];
    if (m_targetY > r.y - AlignPad() + 1.0f) {
        // Satirin icindeyiz: once bu satirin basina (uzunsa ekran ekran).
        ScrollTo(std::max(r.y - AlignPad(), m_targetY - viewH * 0.9f));
        return;
    }
    if (cur > 0) {
        const Row& p = m_rows[(size_t)cur - 1];
        if (p.h > viewH) ScrollTo(m_targetY - viewH * 0.9f);
        else ScrollTo(p.y - AlignPad());
    } else {
        ScrollTo(0.0f);
    }
}

bool ReaderView::HasNextChapter() const {
    if (m_host) {
        const auto& chaps = m_host->GetCurrentChapters();
        if (!chaps.empty()) {
            return m_host->HasNextChapter();
        }
    }
    return !m_nextDir.empty();
}

bool ReaderView::HasPrevChapter() const {
    if (m_host) {
        const auto& chaps = m_host->GetCurrentChapters();
        if (!chaps.empty()) {
            return m_host->HasPrevChapter();
        }
    }
    return !m_prevDir.empty();
}

void ReaderView::OpenChapter(int delta) {
    if (m_host && !m_host->GetCurrentChapters().empty()) {
        if (!m_host->NavigateChapter(delta)) {
            m_host->Toast(T(delta > 0 ? S::LastChapter : S::FirstChapter));
        }
        return;
    }

    const std::wstring target = delta > 0 ? m_nextDir : m_prevDir;
    if (target.empty()) {
        if (m_host && m_host->NavigateChapter(delta)) {
            return;
        }
        if (m_host) m_host->Toast(T(delta > 0 ? S::LastChapter : S::FirstChapter));
        return;
    }
    const std::wstring name = paths::FileName(target);
    if (Open(target)) {
        if (m_host) m_host->Toast(std::wstring(T(S::OpeningChapter)) + L" " + name);
        return;
    }
    // Klasor bu arada bosaldi ya da silindi: sessizce takilma, soyle ve komsulari tazele.
    if (m_host) m_host->Toast(T(S::NoImages));
    m_nextDir = local::SiblingChapter(m_dir, +1);
    m_prevDir = local::SiblingChapter(m_dir, -1);
}

void ReaderView::SetFit(FitMode f) {
    if (!m_hasCustomSettings) {
        m_webtoon = IsWebtoon();
        m_doublePage = IsDoublePage();
        m_rtl = IsRtl();
        m_hasCustomSettings = true;
    }
    m_fit = f;
    m_zoom = 1.0f;
    RelayoutKeepingPosition(m_area.top + RcH(m_area) * 0.35f);
    if (m_host) {
        m_host->SaveMangaReadingSettings(m_seriesKey, IsWebtoon(), CurrentFit(), IsDoublePage(), IsRtl(), m_zoom);
        m_host->Invalidate();
    }
}

void ReaderView::ZoomBy(float factor, float mx, float my) {
    const float nz = std::clamp(m_zoom * factor, kMinZoom, kMaxZoom);
    if (std::fabs(nz - m_zoom) < 1e-4f) return;
    const float sy = (my >= m_area.top && my < m_area.bottom) ? my : m_area.top + RcH(m_area) * 0.5f;
    (void)mx;
    m_zoom = nz;
    if (!m_hasCustomSettings) {
        m_webtoon = IsWebtoon();
        m_fit = CurrentFit();
        m_doublePage = IsDoublePage();
        m_rtl = IsRtl();
        m_hasCustomSettings = true;
    }
    RelayoutKeepingPosition(sy);
    if (m_host) {
        m_host->Toast(Fmt(T(S::ZoomValue), (int)std::lround(m_zoom * 100.0f)));
        m_host->SaveMangaReadingSettings(m_seriesKey, IsWebtoon(), CurrentFit(), IsDoublePage(), IsRtl(), m_zoom);
        m_host->Invalidate();
    }
}

void ReaderView::ToggleWebtoon() {
    if (!m_hasCustomSettings) {
        m_fit = CurrentFit();
        m_doublePage = IsDoublePage();
        m_rtl = IsRtl();
        m_hasCustomSettings = true;
    }
    m_webtoon = !IsWebtoon();
    RelayoutKeepingPosition(m_area.top + RcH(m_area) * 0.35f);
    if (m_host) {
        m_host->SaveMangaReadingSettings(m_seriesKey, IsWebtoon(), CurrentFit(), IsDoublePage(), IsRtl(), m_zoom);
        m_host->Invalidate();
    }
}

void ReaderView::ToggleDouble() {
    if (!m_hasCustomSettings) {
        m_webtoon = IsWebtoon();
        m_fit = CurrentFit();
        m_rtl = IsRtl();
        m_hasCustomSettings = true;
    }
    m_doublePage = !IsDoublePage();
    RelayoutKeepingPosition(m_area.top + RcH(m_area) * 0.35f);
    if (m_host) {
        m_host->SaveMangaReadingSettings(m_seriesKey, IsWebtoon(), CurrentFit(), IsDoublePage(), IsRtl(), m_zoom);
        m_host->Invalidate();
    }
}

void ReaderView::ToggleDirection() {
    if (!m_hasCustomSettings) {
        m_webtoon = IsWebtoon();
        m_fit = CurrentFit();
        m_doublePage = IsDoublePage();
        m_hasCustomSettings = true;
    }
    m_rtl = !IsRtl();
    RelayoutKeepingPosition(m_area.top + RcH(m_area) * 0.35f);
    if (m_host) {
        m_host->SaveMangaReadingSettings(m_seriesKey, IsWebtoon(), CurrentFit(), IsDoublePage(), IsRtl(), m_zoom);
        m_host->Invalidate();
    }
}

// -------------------------------------------------------- bellek ------------

void ReaderView::ReleasePage(Page& p) {
    p.tiles.clear();
    p.decW = p.decH = 0;
    if (p.pendingW && m_decoder) {
        m_decoder->CancelRequest(m_gen, (int)(&p - m_pages.data()));
        p.pendingW = 0;
    }
}

void ReaderView::DropAllBitmaps() {
    for (auto& p : m_pages) {
        p.tiles.clear();
        p.decW = p.decH = 0;
        p.pendingW = 0;
        p.bmpFailGen = 0;
    }
}

void ReaderView::Schedule(Renderer& r) {
    if (m_rows.empty() || !m_decoder) return;
    const float top = m_scrollY, bottom = m_scrollY + RcH(m_area);
    const int r0 = RowAt(top);
    const int r1 = RowAt(bottom);
    const int keep0 = std::max(0, r0 - kKeepBehind);
    const int keep1 = std::min((int)m_rows.size() - 1, r1 + kKeepAhead);
    const UINT maxW = r.MaxBitmapSize();

    std::vector<char> keep(m_pages.size(), 0);
    for (int ri = keep0; ri <= keep1; ++ri) {
        const Row& row = m_rows[(size_t)ri];
        if (row.endCard) continue;
        const float dist = ri < r0 ? (float)(r0 - ri) : ri > r1 ? (float)(ri - r1) : 0.0f;
        for (int k = 0; k < 2; ++k) {
            const int idx = k == 0 ? row.a : row.b;
            if (idx < 0) continue;
            keep[(size_t)idx] = 1;
            Page& p = m_pages[(size_t)idx];
            if (p.failed || !p.probed) continue;
            if (p.bmpFailGen == r.DeviceGeneration()) continue; // bu aygitta olmadi: aygit degisince dene
            const float dispW = RcW(k == 0 ? row.ra : row.rb);
            // Cozucu piksel tavanini asan sayfayi zaten kucultur; istek de o
            // genisligi gecmesin, yoksa "daha keskin" hic gelmez ve dongu olur.
            const UINT capW = std::min(CapWidth(p), maxW);
            UINT want = (UINT)std::ceil(std::max(1.0f, dispW));
            want = std::min(want, capW);
            const bool none = p.decW == 0 && p.pendingW == 0;
            const bool sharper = p.decW > 0 && p.decW < capW && want > p.decW * 1.2f && p.pendingW < want;
            if (none || sharper) {
                // Ileri sayfalar geridekilerden once gelsin.
                const float prio = dist + (ri < r0 ? 0.5f : 0.0f);
                m_decoder->Request(m_gen, idx, want, prio);
                p.pendingW = want;
            }
        }
    }
    for (size_t i = 0; i < m_pages.size(); ++i) {
        if (!keep[i] && (m_pages[i].decW || m_pages[i].pendingW)) ReleasePage(m_pages[i]);
    }
}

void ReaderView::OnDecoderSignal(Renderer& r) {
    if (!m_decoder) return;
    std::vector<ProbeResult> probes;
    std::vector<DecodeResult> decodes;
    m_decoder->Take(probes, decodes);

    bool relayout = false;
    for (const auto& pr : probes) {
        if (pr.gen != m_gen || pr.index < 0 || pr.index >= (int)m_pages.size()) continue;
        Page& p = m_pages[(size_t)pr.index];
        if (pr.ok) {
            p.probed = true;
            p.failed = false;
            p.natW = pr.w;
            p.natH = pr.h;
            ++m_probedCount;
            relayout = true;
        } else if (!p.path.empty() && GetFileAttributesW(p.path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            p.failed = true;
            p.probed = true;
        }
    }
    if (relayout && !m_rows.empty()) RelayoutKeepingPosition(m_area.top);
    else if (relayout) m_layoutDirty = true;

    const UINT maxBmp = r.MaxBitmapSize();
    const UINT tileH = std::min(kTileH, maxBmp);
    for (auto& dr : decodes) {
        if (dr.gen != m_gen || dr.index < 0 || dr.index >= (int)m_pages.size()) continue;
        Page& p = m_pages[(size_t)dr.index];
        p.pendingW = 0;
        if (!dr.ok) {
            p.failed = true; // dosya bozuk ya da bicim desteklenmiyor
            continue;
        }
        if (dr.w > maxBmp) {
            p.bmpFailGen = r.DeviceGeneration();
            continue;
        }
        std::vector<Tile> tiles;
        const UINT stride = dr.w * 4;
        bool ok = true;
        for (UINT y0 = 0; y0 < dr.h; y0 += tileH) {
            const UINT th = std::min(tileH, dr.h - y0);
            Tile t;
            t.bmp = r.CreateBitmap(dr.w, th, dr.bgra.data() + (size_t)y0 * stride, stride);
            if (!t.bmp) { ok = false; break; }
            t.y0 = y0;
            t.h = th;
            tiles.push_back(std::move(t));
        }
        if (!ok) {
            // VRAM dolu ya da aygit kaybi: gecici. Sayfayi bozuk sayma.
            p.bmpFailGen = r.DeviceGeneration();
            log::Write(L"Bitmap olusturulamadi (sayfa %d), aygit degisince yeniden denenecek.", dr.index + 1);
            continue;
        }
        p.bmpFailGen = 0;
        p.tiles = std::move(tiles);
        p.decW = dr.w;
        p.decH = dr.h;
    }
    if (m_host) m_host->Invalidate();
}

// ------------------------------------------------------------ kare ----------

void ReaderView::WakeChrome(double now) {
    m_lastActivity = now;
}

bool ReaderView::Animate(float dt, double now) {
    bool more = false;
    const float dy = m_targetY - m_scrollY;
    if (std::fabs(dy) > 0.5f) {
        m_scrollY += dy * (1.0f - std::exp(-dt * 16.0f));
        more = true;
    } else {
        m_scrollY = m_targetY;
    }
    const float dx = m_targetX - m_scrollX;
    if (std::fabs(dx) > 0.5f) {
        m_scrollX += dx * (1.0f - std::exp(-dt * 16.0f));
        more = true;
    } else {
        m_scrollX = m_targetX;
    }

    const float ds = m_thumbTargetScrollX - m_thumbScrollX;
    if (std::fabs(ds) > 0.5f) {
        m_thumbScrollX += ds * (1.0f - std::exp(-dt * 20.0f));
        more = true;
    } else {
        m_thumbScrollX = m_thumbTargetScrollX;
    }

    const bool overHud = RcHit(m_hudRect, m_mx, m_my) ||
                         (!m_pages.empty() && RcHit(m_thumbStripRc, m_mx, m_my)) ||
                         (m_popOpen && RcHit(m_popRect, m_mx, m_my));
    const bool show = !m_set->hudAutoHide || overHud || m_popOpen || m_helpOpen ||
                      (now - m_lastActivity) < theme::HudIdle;
    const float target = show ? 1.0f : 0.0f;
    if (m_chrome != target) {
        const float step = dt / theme::HudFade;
        m_chrome = target > m_chrome ? std::min(target, m_chrome + step) : std::max(target, m_chrome - step);
        more = true;
    }
    return more;
}

double ReaderView::NextWake(double now) const {
    if (!IsOpen() || !m_set->hudAutoHide || m_chrome <= 0.0f || m_popOpen || m_helpOpen) return -1.0;
    return m_lastActivity + theme::HudIdle - now;
}

D2D1_RECT_F ReaderView::ToScreen(const D2D1_RECT_F& d) const {
    const float ox = m_area.left - m_scrollX;
    const float oy = m_area.top - m_scrollY;
    return Rc(d.left + ox, d.top + oy, d.right + ox, d.bottom + oy);
}

void ReaderView::Draw(Renderer& r, Ui& ui, const D2D1_RECT_F& area, double now) {
    if (m_lastActivity == 0.0) m_lastActivity = now;
    m_scale = r.Scale();
    w::SetScale(m_scale);

    CheckProgressiveOnDisk();

    const float s = m_scale;
    const float sidebarW = m_sidebarOpen ? (290.0f * s) : 0.0f;
    const D2D1_RECT_F sidebarRc = m_sidebarOpen ? Rc(area.right - sidebarW, area.top, area.right, area.bottom) : Rc(0, 0, 0, 0);
    m_sidebarRc = sidebarRc;
    const D2D1_RECT_F pagesArea = Rc(area.left, area.top, area.right - sidebarW, area.bottom);

    const bool sizeChanged = std::fabs(RcW(pagesArea) - m_layoutW) > 0.5f || std::fabs(RcH(pagesArea) - m_layoutH) > 0.5f ||
                             m_layoutScale != m_scale;
    const D2D1_RECT_F oldArea = m_area;
    m_area = pagesArea;
    if (m_devGen != r.DeviceGeneration()) {
        m_devGen = r.DeviceGeneration();
        DropAllBitmaps();
    }
    if (m_layoutDirty || sizeChanged) {
        if (!m_rows.empty() && sizeChanged) {
            // Pencere boyutu degisti: ustteki satir yerinde kalsin.
            const D2D1_RECT_F keep = m_area;
            m_area = oldArea;
            const Anchor a = CaptureAnchor(oldArea.top);
            m_area = keep;
            Layout();
            Anchor b = a;
            b.screenY = m_area.top;
            ApplyAnchor(b);
        } else {
            RelayoutKeepingPosition(m_area.top);
        }
    }
    m_targetY = std::clamp(m_targetY, 0.0f, MaxScrollY());
    m_scrollY = std::clamp(m_scrollY, 0.0f, MaxScrollY());
    m_targetX = std::clamp(m_targetX, 0.0f, MaxScrollX());
    m_scrollX = std::clamp(m_scrollX, 0.0f, MaxScrollX());

    Schedule(r);

    // GhostView zemini: secili tema
    if (m_set->readerTheme == 1) { // OLED Siyah
        r.Fill(pagesArea, 0x000000, 1.0f);
    } else if (m_set->readerTheme == 2) { // Sepya
        r.Fill(pagesArea, 0x26221E, 1.0f);
    } else if (m_set->readerTheme == 3) { // Acik
        r.Fill(pagesArea, 0xE0E0E6, 1.0f);
    } else { // Koyu (varsayilan)
        r.Fill(pagesArea, theme::Black, m_set->readerDim);
    }
    r.PushClip(pagesArea);
    DrawPages(r, ui);
    r.PopClip();

    if (m_helpOpen) DrawHelp(r);

    const float stripH = 76.0f * s;
    const D2D1_RECT_F dockRc = RcWH(pagesArea.left, pagesArea.bottom - stripH, RcW(pagesArea), stripH);
    if (!m_pages.empty()) {
        DrawThumbnailStrip(r, ui, dockRc, now);
    } else {
        m_thumbStripRc = D2D1_RECT_F{};
    }

    DrawHud(r, ui, now);
    DrawScrollbar(r);
    if (m_popOpen) DrawPopover(r, ui);

    // Sag Okuma Menusu (Right Sidebar)
    if (m_sidebarOpen) {
        DrawRightSidebar(r, ui, sidebarRc, now);
        if (m_sidebarChDropdownOpen) {
            DrawSidebarChapterDropdown(r, ui, m_sidebarChSelectRc, now);
        }
    }

    DrawSidebarHandle(r,ui,area);

    if (m_pendingChapter != 0) {
        const int delta = m_pendingChapter;
        m_pendingChapter = 0;
        OpenChapter(delta);
        if (m_host) m_host->Invalidate();
    }
}

void ReaderView::DrawScrollbar(Renderer& r) {
    const float s = m_scale;
    const float bottom = m_chrome > 0.05f && m_hudRect.top > m_area.top ? m_hudRect.top - 10*s : m_area.bottom - 10*s;
    m_scrollTrack = Rc(m_area.right-14*s,m_area.top+10*s,m_area.right-3*s,bottom);
    if (MaxScrollY() <= 0 || RcH(m_scrollTrack) <= 0) { m_scrollTrack={}; m_scrollThumb={}; m_scrollDragging=false; return; }
    const float height=std::min(RcH(m_scrollTrack),std::max(32*s,RcH(m_scrollTrack)*RcH(m_area)/m_docH));
    const float top=m_scrollTrack.top+(RcH(m_scrollTrack)-height)*m_scrollY/MaxScrollY();
    m_scrollThumb=Rc(m_scrollTrack.left,top,m_scrollTrack.right,top+height);
    const bool light=m_set->readerTheme==3;
    r.FillRound(m_scrollTrack,5*s,light?theme::Black:theme::White,light?0.09f:0.07f);
    r.FillRound(RcInset(m_scrollThumb,2*s,0),3*s,light?0x586475:0x9AA9BD,m_scrollDragging||RcHit(m_scrollTrack,m_mx,m_my)?0.95f:0.60f);
}

void ReaderView::DragScrollbar(float y) {
    const float travel=RcH(m_scrollTrack)-RcH(m_scrollThumb);
    if (travel <= 0) return;
    m_targetY=m_scrollY=std::clamp((y-m_scrollGrab-m_scrollTrack.top)/travel,0.0f,1.0f)*MaxScrollY();
    if (m_host) m_host->Invalidate();
}

void ReaderView::DrawPages(Renderer& r, Ui& ui) {
    if (m_rows.empty()) return;
    const float s = m_scale;
    const int r0 = RowAt(m_scrollY);
    const int r1 = RowAt(m_scrollY + RcH(m_area));
    for (int ri = r0; ri <= r1 && ri < (int)m_rows.size(); ++ri) {
        const Row& row = m_rows[(size_t)ri];
        if (row.endCard) {
            const D2D1_RECT_F card = ToScreen(row.ra);
            const uint32_t id = Id("reader.endcard");
            const bool hasNext = HasNextChapter();
            const bool clickable = hasNext && m_chrome >= 0.0f;
            // Kart HUD'un altindaysa tiklama HUD'a gitsin.
            const bool blocked = RcHit(m_hudRect, ui.In().mx, ui.In().my) && m_chrome > 0.05f;
            bool clicked = false;
            if (!blocked && clickable) clicked = ui.Button(id, card);
            r.FillRound(card, 14.0f * s, theme::Panel, theme::HudAlpha);
            r.StrokeRound(card, 14.0f * s, theme::Border, 1.0f * s, theme::BorderAlpha);
            if (clickable && ui.Hot(id)) r.FillRound(card, 14.0f * s, theme::White, 0.06f);
            const float pad = 20.0f * s;
            r.Text(T(S::ChapterEnd), Rc(card.left + pad, card.top + 18 * s, card.right - pad, card.top + 44 * s),
                   theme::Text, 17.0f * s, Renderer::Align::Center, Renderer::Weight::Semibold);
            std::wstring line;
            if (hasNext) {
                if (!m_nextDir.empty()) line = std::wstring(T(S::NextLabel)) + L" " + paths::FileName(m_nextDir);
                else line = std::wstring(T(S::NextChapter));
            } else {
                line = i18n::Literal(L"🎉 En son bölümdensiniz. Yeni bölümler çıktığında bildirilecek.");
            }
            r.Text(line, Rc(card.left + pad, card.top + 48 * s, card.right - pad, card.top + 74 * s),
                   (!hasNext) ? theme::TextMuted : theme::Accent, 14.0f * s, Renderer::Align::Center);
            if (hasNext && m_set->magicScroll) {
                r.Text(T(S::ContinueHint), Rc(card.left + pad, card.top + 82 * s, card.right - pad, card.top + 106 * s),
                       theme::TextMuted, 12.5f * s, Renderer::Align::Center);
            }
            // Cizim sirasinda bolum degistirmek m_rows'u gecersiz kilar: ertele.
            if (clicked && !m_dragMoved && hasNext) m_pendingChapter = +1;
            continue;
        }
        for (int k = 0; k < 2; ++k) {
            const int idx = k == 0 ? row.a : row.b;
            if (idx < 0) continue;
            const Page& p = m_pages[(size_t)idx];
            // Tam piksele oturt: kesirli kaydirmada bitmap kenarlari kenar
            // yumusatmaya girip dilim eklerinde ince cizgi birakiyordu.
            D2D1_RECT_F dst = ToScreen(k == 0 ? row.ra : row.rb);
            dst = Rc(std::round(dst.left), std::round(dst.top), std::round(dst.right), std::round(dst.bottom));

            if (!p.probed) {
                // Kademeli akis: henuz inmeyen sayfalar icin kompakt kutu (cok buyuk degil)
                r.FillRound(dst, 10.0f * s, theme::Panel, 0.75f);
                r.StrokeRound(dst, 10.0f * s, theme::Border, 1.0f * s, 0.5f);
                wchar_t msg[96];
                if (m_pages.size() <= 1 && m_expectedTotalPages <= 0) {
                    swprintf_s(msg, i18n::Literal(L"⚡ Bölüm sayfaları taranıyor..."));
                } else {
                    swprintf_s(msg, i18n::Literal(L"⏳ Sayfa %d yükleniyor..."), idx + 1);
                }
                r.Text(msg, dst, theme::Accent, 13.5f * s, Renderer::Align::Center, Renderer::Weight::Semibold);
                continue;
            }

            if (!p.tiles.empty() && p.decH > 0) {
                const float ky = RcH(dst) / (float)p.decH;
                for (size_t t = 0; t < p.tiles.size(); ++t) {
                    const Tile& tile = p.tiles[t];
                    // Komsu dilimler ayni yuvarlanmis siniri paylasir: bosluk da ortusme de yok.
                    const float y0 = std::round(dst.top + tile.y0 * ky);
                    const float y1 = std::round(dst.top + (tile.y0 + tile.h) * ky);
                    if (y1 < m_area.top || y0 > m_area.bottom) continue;
                    r.DrawBitmap(tile.bmp.Get(), Rc(dst.left, y0, dst.right, y1));
                }
            } else {
                r.Fill(dst, theme::Panel, 0.85f);
                const std::wstring label = p.failed
                    ? std::wstring(T(S::DecodeFailed)) + L"\n" + paths::FileName(p.path)
                    : std::to_wstring(idx + 1);
                r.Text(label, RcInset(dst, 12 * s, 12 * s), p.failed ? theme::TextDim : theme::TextMuted,
                       (p.failed ? 14.0f : 28.0f) * s, Renderer::Align::Center,
                       p.failed ? Renderer::Weight::Normal : Renderer::Weight::Semibold, 1.0f, p.failed);
            }
        }
    }
}

void ReaderView::DrawHud(Renderer& r, Ui& ui, double now) {
    (void)now;
    const float s = m_scale;
    const float a = m_chrome;
    if (a <= 0.01f) {
        m_hudRect = D2D1_RECT_F{};
        m_gearRect = D2D1_RECT_F{};
        return;
    }

    enum Kind { Btn, Sep, Dir, Count };
    struct Def {
        Kind kind;
        uint32_t id;
        Icon icon;
        S tip;
        bool active;
        bool enabled;
        float w;
        int group; // dar pencerede gizlenebilir oge grubu; 0 ve 6 hep gorunur
    };
    const FitMode fit = CurrentFit();
    const bool hasPrev = HasPrevChapter(), hasNext = HasNextChapter();
    std::vector<Def> defs;
    defs.push_back({ Btn, Id("hud.prev"),    Icon::ChevronLeft,  S::PrevPage,        false, true, 40, 0 });
    defs.push_back({ Btn, Id("hud.next"),    Icon::ChevronRight, S::NextPage,        false, true, 40, 0 });
    if (hasPrev || hasNext) {
        defs.push_back({ Sep, 0, Icon::Menu, S::AppName, false, true, 9, 1 });
        if (hasPrev) {
            defs.push_back({ Btn, Id("hud.chprev"),  Icon::ChapterPrev,  S::PrevChapter,     false, true, 40, 1 });
        }
        if (hasNext) {
            defs.push_back({ Btn, Id("hud.chnext"),  Icon::ChapterNext,  S::NextChapter,     false, true, 40, 1 });
        }
    }
    defs.push_back({ Sep, 0, Icon::Menu, S::AppName, false, true, 9, 2 });
    defs.push_back({ Btn, Id("hud.fitw"),    Icon::FitWidth,     S::FitWidth,        fit == FitMode::Width, true, 40, 2 });
    defs.push_back({ Btn, Id("hud.fith"),    Icon::FitHeight,    S::FitHeight,       fit == FitMode::Height, true, 40, 2 });
    defs.push_back({ Btn, Id("hud.fitwin"),  Icon::FitWindow,    S::FitWindow,       fit == FitMode::Window, true, 40, 2 });
    defs.push_back({ Btn, Id("hud.fit11"),   Icon::OneToOne,     S::FitOriginal,     fit == FitMode::Original, true, 44, 2 });
    defs.push_back({ Sep, 0, Icon::Menu, S::AppName, false, true, 9, 3 });
    defs.push_back({ Btn, Id("hud.double"),  Icon::DoublePage,   S::DoublePage,      IsDoublePage(), true, 40, 3 });
    defs.push_back({ Btn, Id("hud.webtoon"), Icon::Strip,        S::Webtoon,         IsWebtoon(), true, 40, 3 });
    defs.push_back({ Dir, Id("hud.dir"),     Icon::Menu,         S::Direction,       IsRtl(), true, 50, 3 });
    defs.push_back({ Sep, 0, Icon::Menu, S::AppName, false, true, 9, 4 });
    defs.push_back({ Btn, Id("hud.zoomout"), Icon::Minus,        S::ZoomOut,         false, true, 40, 4 });
    defs.push_back({ Btn, Id("hud.zoomin"),  Icon::Plus,         S::ZoomIn,          false, true, 40, 4 });
    defs.push_back({ Sep, 0, Icon::Menu, S::AppName, false, true, 9, 5 });
    defs.push_back({ Btn, Id("hud.bm"),      Icon::Star,         S::Bookmark,        false, true, 40, 5 });
    defs.push_back({ Btn, Id("hud.dl"),      Icon::Download,     S::DownloadChapter, false, true, 40, 5 });
    defs.push_back({ Btn, Id("hud.sidebar"), Icon::Menu,         S::AppName,         m_sidebarOpen, true, 40, 6 });
    defs.push_back({ Btn, Id("hud.gear"),    Icon::Sliders,      S::ReaderSettings,  m_popOpen, true, 40, 6 });
    defs.push_back({ Count, Id("hud.count"), Icon::Menu,         S::Page,            false, true, 0, 6 });
    m_gearRect = D2D1_RECT_F{}; // cizilmezse eski konumda tiklanabilir kalmasin

    // Sayac
    const int cur = CurrentRow();
    std::wstring counter;
    if (!m_rows.empty()) {
        const Row& row = m_rows[(size_t)cur];
        const int total = (int)m_pages.size();
        if (row.endCard) counter = std::to_wstring(total) + L" / " + std::to_wstring(total);
        else if (row.b >= 0) counter = std::to_wstring(row.a + 1) + L"-" + std::to_wstring(row.b + 1) + L" / " + std::to_wstring(total);
        else counter = std::to_wstring(row.a + 1) + L" / " + std::to_wstring(total);
    }
    const float counterW = r.MeasureText(counter, 13.0f * s, Renderer::Weight::Semibold, true) + 24.0f * s;

    // Dar pencerede once en az gerekli gruplari gizle; gezinme, disli ve
    // sayac her zaman kalsin (disli olmadan okuyucu ayarlarina ulasilamaz).
    bool visible[7] = { true, true, true, true, true, true, true };
    const int dropOrder[] = { 5, 4, 1, 3, 2 };
    auto measure = [&]() {
        float t = 0.0f;
        int n = 0;
        for (const Def& d : defs) {
            if (!visible[d.group]) continue;
            t += d.kind == Count ? counterW : d.w * s;
            ++n;
        }
        return t + (n - 1) * theme::HudGap * s + 2.0f * theme::HudSidePad * s;
    };
    const float avail = RcW(m_area) - 20.0f * s;
    float total = measure();
    for (int g : dropOrder) {
        if (total <= avail) break;
        visible[g] = false;
        total = measure();
    }
    const float barH = theme::HudH * s;
    const float left = m_area.left + (RcW(m_area) - total) * 0.5f;
    float top = m_area.bottom - theme::HudMargin * s - barH;
    if (!m_pages.empty() && m_thumbStripRc.top > 0.0f) {
        top = m_thumbStripRc.top - 8.0f * s - barH;
    }
    m_hudRect = RcWH(left, top, total, barH);

    r.FillRound(m_hudRect, barH * 0.5f, theme::Panel, theme::HudAlpha * a);
    r.StrokeRound(m_hudRect, barH * 0.5f, theme::Border, 1.0f * s, theme::BorderAlpha * a);

    float x = left + theme::HudSidePad * s;
    const float btnH = 40.0f * s;
    const float by = top + (barH - btnH) * 0.5f;
    const bool live = a > 0.3f;
    for (const Def& d : defs) {
        if (!visible[d.group]) continue;
        const float wpx = d.kind == Count ? counterW : d.w * s;
        const D2D1_RECT_F rc = RcWH(x, by, wpx, btnH);
        x += wpx + theme::HudGap * s;

        if (d.kind == Sep) {
            const float cx = (rc.left + rc.right) * 0.5f;
            r.Line(cx, top + barH * 0.34f, cx, top + barH * 0.66f, theme::Border, 1.0f * s, theme::BorderAlpha * a);
            continue;
        }
        if (d.kind == Count) {
            r.Text(counter, rc, theme::TextDim, 13.0f * s, Renderer::Align::Center, Renderer::Weight::Semibold,
                   0.95f * a, false, false, true);
            continue;
        }
        bool clicked = false;
        if (d.kind == Dir) {
            clicked = live && ui.Button(d.id, rc, true);
            const float rad = btnH * 0.5f;
            if (d.active) {
                r.FillRound(rc, rad, theme::Active, theme::ActiveAlpha * a);
                r.StrokeRound(rc, rad, theme::ActiveEdge, 1.0f * s, theme::ActiveEdgeAlpha * a);
            }
            if (ui.Pressed(d.id)) r.FillRound(rc, rad, theme::Press, theme::PressAlpha * a);
            else if (ui.Hot(d.id)) r.FillRound(rc, rad, theme::White, theme::HoverAlpha * a);
            r.Text(IsRtl() ? L"RTL" : L"LTR", rc, theme::Text, 12.5f * s, Renderer::Align::Center,
                   Renderer::Weight::Semibold, a);
            ui.Tip(d.id, T(d.tip), rc);
        } else {
            if (live) {
                std::wstring tipText = T(d.tip);
                if (d.id == Id("hud.sidebar")) tipText = i18n::Literal(L"Okuma Menüsü (M)");
                clicked = w::IconButton(r, ui, d.id, rc, d.icon, 20.0f * s, tipText, d.active, d.enabled, a);
            } else {
                DrawIcon(r, d.icon, rc, 20.0f * s, d.enabled ? theme::Text : theme::TextMuted, a);
            }
        }
        if (d.id == Id("hud.gear")) m_gearRect = rc;
        if (!clicked) continue;

        if      (d.id == Id("hud.prev"))    PrevPage();
        else if (d.id == Id("hud.next"))    NextPage();
        else if (d.id == Id("hud.chprev"))  m_pendingChapter = -1;
        else if (d.id == Id("hud.chnext"))  m_pendingChapter = +1;
        else if (d.id == Id("hud.fitw"))    SetFit(FitMode::Width);
        else if (d.id == Id("hud.fith"))    SetFit(FitMode::Height);
        else if (d.id == Id("hud.fitwin"))  SetFit(FitMode::Window);
        else if (d.id == Id("hud.fit11"))   SetFit(FitMode::Original);
        else if (d.id == Id("hud.double"))  ToggleDouble();
        else if (d.id == Id("hud.webtoon")) ToggleWebtoon();
        else if (d.id == Id("hud.dir"))     ToggleDirection();
        else if (d.id == Id("hud.zoomout")) ZoomBy(1.0f / 1.15f, -1, -1);
        else if (d.id == Id("hud.bm")) {
            if (m_host) {
                const int curPage = CurrentRow();
                m_host->AddBookmark(m_series, m_chapter, curPage);
            }
        }
        else if (d.id == Id("hud.dl")) {
            if (m_host) {
                m_host->DownloadChapter(m_dir, m_series, m_chapter);
            }
        }
        else if (d.id == Id("hud.sidebar")) {
            m_sidebarOpen = !m_sidebarOpen;
        }
        if (m_host) m_host->Invalidate();
    }
}

void ReaderView::DrawPopover(Renderer& r, Ui& ui) {
    const float s = m_scale;
    const float pw = 340.0f * s, rowH = 44.0f * s, pad = 10.0f * s;
    const float ph = pad * 2 + 30.0f * s + rowH * 6;
    const float anchorX = m_gearRect.right > 0 ? (m_gearRect.left + m_gearRect.right) * 0.5f
                                               : m_area.right - pw * 0.5f - 20.0f * s;
    const float left = std::clamp(anchorX - pw * 0.5f, m_area.left + 10.0f * s, m_area.right - pw - 10.0f * s);
    const float bottom = (m_hudRect.top > 0 ? m_hudRect.top : (m_thumbStripRc.top > 0 ? m_thumbStripRc.top - 8.0f * s : m_area.bottom - 90.0f * s)) - 10.0f * s;
    m_popRect = RcWH(left, bottom - ph, pw, ph);

    r.FillRound(m_popRect, 14.0f * s, theme::Panel, 0.97f);
    r.StrokeRound(m_popRect, 14.0f * s, theme::Border, 1.0f * s, theme::BorderAlpha);
    r.Text(T(S::ReaderSettings), Rc(m_popRect.left + 16 * s, m_popRect.top + pad, m_popRect.right - 16 * s,
                                     m_popRect.top + pad + 30 * s),
           theme::TextDim, 12.5f * s, Renderer::Align::Left, Renderer::Weight::Semibold);

    float y = m_popRect.top + pad + 30.0f * s;
    auto row = [&]() {
        const D2D1_RECT_F rc = Rc(m_popRect.left + 4 * s, y, m_popRect.right - 4 * s, y + rowH);
        y += rowH;
        return rc;
    };
    bool changed = false;
    changed |= w::Toggle(r, ui, Id("pop.scaleup"), row(), T(S::ScaleUp), m_set->scaleUp);
    changed |= w::Toggle(r, ui, Id("pop.magic"), row(), T(S::MagicScroll), m_set->magicScroll);
    changed |= w::Toggle(r, ui, Id("pop.cover"), row(), T(S::CoverAlone), m_set->coverAlone);
    changed |= w::Toggle(r, ui, Id("pop.autohide"), row(), T(S::HudAutoHide), m_set->hudAutoHide);

    // Zemin koyulugu: [-] %78 [+]
    const D2D1_RECT_F dimRow = row();
    r.Text(T(S::ReaderDim), Rc(dimRow.left + 12 * s, dimRow.top, dimRow.right - 130 * s, dimRow.bottom),
           theme::Text, 14.0f * s);
    const float bw = 32.0f * s, bh = 30.0f * s, cy = (dimRow.top + dimRow.bottom) * 0.5f;
    const D2D1_RECT_F plus = RcWH(dimRow.right - 12 * s - bw, cy - bh * 0.5f, bw, bh);
    const D2D1_RECT_F minus = RcWH(plus.left - 52 * s - bw, cy - bh * 0.5f, bw, bh);
    r.Text(std::to_wstring((int)std::lround(m_set->readerDim * 100.0f)) + L"%",
           Rc(minus.right, dimRow.top, plus.left, dimRow.bottom), theme::TextDim, 13.0f * s,
           Renderer::Align::Center, Renderer::Weight::Semibold, 1.0f, false, false, true);
    if (w::IconButton(r, ui, Id("pop.dim-"), minus, Icon::Minus, 16 * s, std::wstring())) {
        m_set->readerDim = std::max(0.5f, m_set->readerDim - 0.04f);
        changed = true;
    }
    if (w::IconButton(r, ui, Id("pop.dim+"), plus, Icon::Plus, 16 * s, std::wstring())) {
        m_set->readerDim = std::min(1.0f, m_set->readerDim + 0.04f);
        changed = true;
    }

    // Tema: [Koyu] [OLED] [Sepya] [Acik]
    const D2D1_RECT_F themeRow = row();
    r.Text(i18n::Literal(L"Tema"), Rc(themeRow.left + 12 * s, themeRow.top, themeRow.left + 60 * s, themeRow.bottom),
           theme::Text, 14.0f * s);
    const D2D1_RECT_F segRc = Rc(themeRow.left + 60 * s, themeRow.top + 4 * s, themeRow.right - 12 * s, themeRow.bottom - 4 * s);
    const int pickTheme = w::Segmented(r, ui, Id("pop.theme"), segRc, { i18n::Literal(L"Koyu"), L"OLED", i18n::Literal(L"Sepya"), i18n::Literal(L"Açık") }, m_set->readerTheme);
    if (pickTheme >= 0 && pickTheme != m_set->readerTheme) {
        m_set->readerTheme = pickTheme;
        changed = true;
    }

    if (changed) {
        m_layoutDirty = true;
        if (m_host) { m_host->SaveSettings(); m_host->Invalidate(); }
    }
}

void ReaderView::DrawHelp(Renderer& r) {
    const float s = m_scale;
    const float pw = std::min(760.0f * s, RcW(m_area) - 40.0f * s);
    const float textH = r.MeasureWrappedHeight(T(S::ShortcutsBody), 14.0f * s, pw - 48.0f * s);
    const float ph = 64.0f * s + textH + 24.0f * s;
    const D2D1_RECT_F rc = RcWH(m_area.left + (RcW(m_area) - pw) * 0.5f,
                                m_area.top + (RcH(m_area) - ph) * 0.42f, pw, ph);
    r.FillRound(rc, 16.0f * s, theme::Panel, 0.96f);
    r.StrokeRound(rc, 16.0f * s, theme::Border, 1.0f * s, theme::BorderAlpha);
    r.Text(T(S::Shortcuts), Rc(rc.left + 24 * s, rc.top + 18 * s, rc.right - 24 * s, rc.top + 48 * s),
           theme::Text, 17.0f * s, Renderer::Align::Left, Renderer::Weight::Semibold);
    r.Text(T(S::ShortcutsBody), Rc(rc.left + 24 * s, rc.top + 60 * s, rc.right - 24 * s, rc.bottom - 16 * s),
           theme::TextDim, 14.0f * s, Renderer::Align::Left, Renderer::Weight::Normal, 1.0f, true);
}

// ------------------------------------------------------------ girdi ---------

// ---------------------------------------------------- sag okuma menusu ------

void ReaderView::DrawSidebarHandle(Renderer& r,Ui& ui,const D2D1_RECT_F& area) {
    if(m_sidebarOpen){m_sidebarReopenRc={};return;}
    const float s=m_scale;
    m_sidebarReopenRc=RcWH(area.right-130*s,area.top+12*s,108*s,34*s);
    if(w::TextButton(r,ui,Id("reader.reopen.sidebar"),m_sidebarReopenRc,i18n::Literal(L"☰ Menüyü aç"),true)) {
        m_sidebarOpen=true;m_sidebarChDropdownOpen=false;m_layoutDirty=true;m_sidebarReopenRc={};
        if(m_host)m_host->Invalidate();
    }
    ui.Tip(Id("reader.reopen.sidebar"),i18n::Literal(L"Okuma menüsünü aç (M)"),m_sidebarReopenRc);
}

void ReaderView::DrawRightSidebar(Renderer& r, Ui& ui, const D2D1_RECT_F& sbRc, double now) {
    const float s = m_scale;
    const float w = RcW(sbRc);
    const float pad = 14.0f * s;
    const float contentW = w - pad * 2.0f;

    // Arka plan ve sol kenar cizgisi
    r.Fill(sbRc, theme::Panel, 0.98f);
    r.Line(sbRc.left, sbRc.top, sbRc.left, sbRc.bottom, theme::Border, 1.0f * s, theme::BorderAlpha);

    float curY = sbRc.top + 14.0f * s;

    // 1. Baslik Seridi: Etiket ve Gizle Dugmesi
    const D2D1_RECT_F closeRc = RcWH(sbRc.right - pad - 64.0f * s, curY, 64.0f * s, 26.0f * s);
    if (w::TextButton(r, ui, Id("sb.close"), closeRc, i18n::Literal(L"▶ Gizle"), false)) {
        m_sidebarOpen = false;
        m_sidebarChDropdownOpen = false;
        if (m_host) m_host->Invalidate();
    }
    ui.Tip(Id("sb.close"), i18n::Literal(L"Menüyü Gizle (M)"), closeRc);

    r.Text(i18n::Literal(L"OKUMA MENÜSÜ"), Rc(sbRc.left + pad, curY + 4.0f * s, closeRc.left - 4.0f * s, curY + 22.0f * s),
           theme::Accent, 11.0f * s, Renderer::Align::Left, Renderer::Weight::Bold);
    curY += 30.0f * s;

    // 2. Kaynak Rozeti (Mirror / Source Name)
    if (!m_mirror.empty()) {
        std::wstring mBadge = L"🌐 " + m_mirror;
        const float badgeW = std::min(contentW, 160.0f * s);
        const D2D1_RECT_F badgeRc = RcWH(sbRc.left + pad, curY, badgeW, 20.0f * s);
        r.FillRound(badgeRc, 5.0f * s, theme::Accent, 0.16f);
        r.StrokeRound(badgeRc, 5.0f * s, theme::Accent, 1.0f * s, 0.40f);
        r.Text(mBadge, badgeRc, theme::Accent, 10.5f * s, Renderer::Align::Center, Renderer::Weight::Bold);
        curY += 24.0f * s;
    }

    // 3. Manga Adi
    std::wstring displaySeries = m_series.empty() ? L"Manga" : m_series;
    const D2D1_RECT_F titleRc = RcWH(sbRc.left + pad, curY, contentW, 36.0f * s);
    r.Text(displaySeries, titleRc, theme::Text, 15.0f * s, Renderer::Align::Left, Renderer::Weight::Bold);
    curY += 38.0f * s;

    // 4. Bolum Basligi / Numarasi
    std::wstring displayChap = CleanChapterTitle(m_chapter, m_host ? m_host->GetCurrentChapterIndex() : -1);
    if (!displayChap.empty()) {
        const D2D1_RECT_F chapRc = RcWH(sbRc.left + pad, curY, contentW, 20.0f * s);
        r.Text(displayChap, chapRc, theme::TextDim, 12.5f * s, Renderer::Align::Left, Renderer::Weight::Semibold);
        curY += 24.0f * s;
    }

    // 5. Bolum Secme Selectbox'i
    const auto& chaps = m_host ? m_host->GetCurrentChapters() : std::vector<OnlineChapter>{};
    const float selH = 34.0f * s;
    m_sidebarChSelectRc = RcWH(sbRc.left + pad, curY, contentW, selH);
    const uint32_t selId = Id("sb.ch.select");
    const bool selClicked = ui.Button(selId, m_sidebarChSelectRc);
    const bool selHot = ui.Hot(selId);
    if (selClicked) {
        m_sidebarChDropdownOpen = !m_sidebarChDropdownOpen;
        if (m_sidebarChDropdownOpen) {
            m_sidebarChSearch.Clear();
            m_sidebarChSearch.focused = true;
            m_sidebarChScroll = 0.0f;
            m_sidebarChNeedsScrollToCurrent = true;
        }
        if (m_host) m_host->Invalidate();
    }

    r.FillRound(m_sidebarChSelectRc, 8.0f * s, theme::Panel2, 0.85f);
    r.StrokeRound(m_sidebarChSelectRc, 8.0f * s,
                  (m_sidebarChDropdownOpen || selHot) ? theme::Accent : theme::Border,
                  1.2f * s, (m_sidebarChDropdownOpen || selHot) ? 0.9f : 0.45f);

    std::wstring selText = displayChap.empty() ? i18n::Literal(L"Bölüm Seç") : displayChap;
    if (!chaps.empty()) {
        wchar_t cntBuf[32];
        swprintf_s(cntBuf, L" (%zu)", chaps.size());
        selText += cntBuf;
    }
    r.Text(L"📑 " + selText,
           Rc(m_sidebarChSelectRc.left + 10.0f * s, m_sidebarChSelectRc.top, m_sidebarChSelectRc.right - 26.0f * s, m_sidebarChSelectRc.bottom),
           theme::Text, 12.5f * s, Renderer::Align::Left, Renderer::Weight::Semibold);
    r.Text(m_sidebarChDropdownOpen ? L"▲" : L"▼",
           Rc(m_sidebarChSelectRc.right - 22.0f * s, m_sidebarChSelectRc.top, m_sidebarChSelectRc.right - 6.0f * s, m_sidebarChSelectRc.bottom),
           theme::TextMuted, 10.0f * s, Renderer::Align::Center);
    curY += selH + 16.0f * s;

    // Ayirici
    r.Fill(RcWH(sbRc.left + pad, curY, contentW, 1.0f * s), theme::Border, 0.4f);
    curY += 12.0f * s;

    // 5. Gorunum Modu (Tam Ekran, Genislik, Yukseklik, Orijinal)
    r.Text(i18n::Literal(L"GÖRÜNÜM MODU"), RcWH(sbRc.left + pad, curY, contentW, 16.0f * s),
           theme::TextMuted, 10.5f * s, Renderer::Align::Left, Renderer::Weight::Bold);
    curY += 20.0f * s;

    const float gridBtnW = (contentW - 6.0f * s) * 0.5f;
    const float gridBtnH = 28.0f * s;

    // [ Genişlik ] and [ Yükseklik ]
    const D2D1_RECT_F btnWidthRc = RcWH(sbRc.left + pad, curY, gridBtnW, gridBtnH);
    const D2D1_RECT_F btnHeightRc = RcWH(sbRc.left + pad + gridBtnW + 6.0f * s, curY, gridBtnW, gridBtnH);
    if (w::TextButton(r, ui, Id("sb.fit.w"), btnWidthRc, i18n::Literal(L"Genişlik"), CurrentFit() == FitMode::Width)) {
        SetFit(FitMode::Width);
    }
    if (w::TextButton(r, ui, Id("sb.fit.h"), btnHeightRc, i18n::Literal(L"Yükseklik"), CurrentFit() == FitMode::Height)) {
        SetFit(FitMode::Height);
    }
    curY += gridBtnH + 6.0f * s;

    // [ Orijinal ] and [ Ekrana Uydur ]
    const D2D1_RECT_F btnOrigRc = RcWH(sbRc.left + pad, curY, gridBtnW, gridBtnH);
    const D2D1_RECT_F btnWinRc = RcWH(sbRc.left + pad + gridBtnW + 6.0f * s, curY, gridBtnW, gridBtnH);
    if (w::TextButton(r, ui, Id("sb.fit.orig"), btnOrigRc, i18n::Literal(L"Orijinal (1:1)"), CurrentFit() == FitMode::Original)) {
        SetFit(FitMode::Original);
    }
    if (w::TextButton(r, ui, Id("sb.fit.win"), btnWinRc, i18n::Literal(L"Ekrana Uydur"), CurrentFit() == FitMode::Window)) {
        SetFit(FitMode::Window);
    }
    curY += gridBtnH + 8.0f * s;

    // [ ⛶ Tam Ekran ] Dugmesi
    const D2D1_RECT_F fsRc = RcWH(sbRc.left + pad, curY, contentW, 30.0f * s);
    if (w::TextButton(r, ui, Id("sb.fullscreen"), fsRc, i18n::Literal(L"⛶ Tam Ekran (F11)"), false)) {
        if (m_host) m_host->ToggleFullscreen();
    }
    curY += 30.0f * s + 6.0f * s;

    // [ 📜 Webtoon Modu ] Dugmesi
    const D2D1_RECT_F wtRc = RcWH(sbRc.left + pad, curY, contentW, 28.0f * s);
    if (w::TextButton(r, ui, Id("sb.webtoon"), wtRc, IsWebtoon() ? i18n::Literal(L"✓ Webtoon Modu") : i18n::Literal(L"Webtoon Modu"), IsWebtoon())) {
        ToggleWebtoon();
    }
    curY += 28.0f * s + 16.0f * s;

    // 6. Yakinlastirma (Zoom) Ayarlari
    r.Text(i18n::Literal(L"YAKINLAŞTIRMA"), RcWH(sbRc.left + pad, curY, contentW, 16.0f * s),
           theme::TextMuted, 10.5f * s, Renderer::Align::Left, Renderer::Weight::Bold);
    curY += 20.0f * s;

    const float zoomBtnW = 38.0f * s;
    const float zoomMidW = contentW - zoomBtnW * 2.0f - 8.0f * s;
    const D2D1_RECT_F zoomMinusRc = RcWH(sbRc.left + pad, curY, zoomBtnW, 30.0f * s);
    const D2D1_RECT_F zoomResetRc = RcWH(sbRc.left + pad + zoomBtnW + 4.0f * s, curY, zoomMidW, 30.0f * s);
    const D2D1_RECT_F zoomPlusRc = RcWH(sbRc.left + pad + zoomBtnW + zoomMidW + 8.0f * s, curY, zoomBtnW, 30.0f * s);

    if (w::TextButton(r, ui, Id("sb.zoom.minus"), zoomMinusRc, L"－", false)) {
        ZoomBy(1.0f / 1.15f, -1, -1);
    }
    wchar_t zoomBuf[32];
    swprintf_s(zoomBuf, i18n::Literal(L"%%%d Sıfırla"), (int)std::lround(m_zoom * 100.0f));
    if (w::TextButton(r, ui, Id("sb.zoom.reset"), zoomResetRc, zoomBuf, false)) {
        m_zoom = 1.0f;
        if (!m_hasCustomSettings) {
            m_webtoon = IsWebtoon();
            m_fit = CurrentFit();
            m_doublePage = IsDoublePage();
            m_rtl = IsRtl();
            m_hasCustomSettings = true;
        }
        RelayoutKeepingPosition(m_area.top + RcH(m_area) * 0.35f);
        if (m_host) {
            m_host->SaveMangaReadingSettings(m_seriesKey, IsWebtoon(), CurrentFit(), IsDoublePage(), IsRtl(), m_zoom);
            m_host->Invalidate();
        }
    }
    ui.Tip(Id("sb.zoom.reset"), i18n::Literal(L"Yakınlaştırmayı %100'e sıfırla"), zoomResetRc);
    if (w::TextButton(r, ui, Id("sb.zoom.plus"), zoomPlusRc, L"＋", false)) {
        ZoomBy(1.15f, -1, -1);
    }
    curY += 30.0f * s + 16.0f * s;

    // 7. Bolum Navigasyonu (Onceki / Sonraki)
    const bool hasPrev = HasPrevChapter();
    const bool hasNext = HasNextChapter();
    if (hasPrev && hasNext) {
        const float navW = (contentW - 6.0f * s) * 0.5f;
        const D2D1_RECT_F prevRc = RcWH(sbRc.left + pad, curY, navW, 30.0f * s);
        const D2D1_RECT_F nextRc = RcWH(sbRc.left + pad + navW + 6.0f * s, curY, navW, 30.0f * s);
        if (w::TextButton(r, ui, Id("sb.prev"), prevRc, i18n::Literal(L"◀ Önceki"), false)) {
            OpenChapter(-1);
        }
        if (w::TextButton(r, ui, Id("sb.next"), nextRc, i18n::Literal(L"Sonraki ▶"), false)) {
            OpenChapter(+1);
        }
        curY += 30.0f * s + 16.0f * s;
    } else if (hasPrev && !hasNext) {
        // Son bolum okunuyor: Sonraki bolum butonu gizli, sadece Onceki bolum butonu
        const D2D1_RECT_F prevRc = RcWH(sbRc.left + pad, curY, contentW, 30.0f * s);
        if (w::TextButton(r, ui, Id("sb.prev"), prevRc, i18n::Literal(L"◀ Önceki Bölüm"), false)) {
            OpenChapter(-1);
        }
        curY += 30.0f * s + 16.0f * s;
    } else if (!hasPrev && hasNext) {
        // Ilk bolum okunuyor: Sadece Sonraki bolum butonu
        const D2D1_RECT_F nextRc = RcWH(sbRc.left + pad, curY, contentW, 30.0f * s);
        if (w::TextButton(r, ui, Id("sb.next"), nextRc, i18n::Literal(L"Sonraki Bölüm ▶"), false)) {
            OpenChapter(+1);
        }
        curY += 30.0f * s + 16.0f * s;
    }

    // 8. Islemler (Indir, Resimleri Bastan Yukle)
    r.Text(i18n::Literal(L"İŞLEMLER"), RcWH(sbRc.left + pad, curY, contentW, 16.0f * s),
           theme::TextMuted, 10.5f * s, Renderer::Align::Left, Renderer::Weight::Bold);
    curY += 20.0f * s;

    // [ 💾 Bölümü İndir (.cbz) ]
    const D2D1_RECT_F dlRc = RcWH(sbRc.left + pad, curY, contentW, 32.0f * s);
    if (w::TextButton(r, ui, Id("sb.download"), dlRc, i18n::Literal(L"💾 Bölümü İndir (.cbz)"), false)) {
        if (m_host) m_host->DownloadChapter(m_dir, m_series, m_chapter);
    }
    curY += 32.0f * s + 8.0f * s;

    // [ 🔄 Resimleri Baştan Yükle ]
    const D2D1_RECT_F reloadRc = RcWH(sbRc.left + pad, curY, contentW, 32.0f * s);
    if (w::TextButton(r, ui, Id("sb.reload"), reloadRc, i18n::Literal(L"🔄 Resimleri Baştan Yükle"), false)) {
        if (m_host) m_host->ReloadCurrentChapter();
    }
    curY += 32.0f * s + 18.0f * s;

    // 9. Yukleme Durumu ve Ilerleme
    size_t totalP = m_pages.size();
    size_t loadedP = 0;
    for (const auto& p : m_pages) {
        if (p.probed && !p.failed) loadedP++;
    }
    wchar_t statusBuf[64];
    if (totalP > 0) {
        swprintf_s(statusBuf, i18n::Literal(L"Sayfalar: %zu / %zu yüklendi"), loadedP, totalP);
    } else {
        swprintf_s(statusBuf, i18n::Literal(L"Bölüm taranıyor..."));
    }
    r.Text(statusBuf, RcWH(sbRc.left + pad, curY, contentW, 18.0f * s),
           (loadedP == totalP && totalP > 0) ? theme::Accent : theme::TextDim,
           12.0f * s, Renderer::Align::Center);
    curY += 20.0f * s;

    if (totalP > 0) {
        const float barH = 4.0f * s;
        const D2D1_RECT_F barBg = RcWH(sbRc.left + pad, curY, contentW, barH);
        r.FillRound(barBg, 2.0f * s, theme::Panel2, 0.8f);
        const float fillW = contentW * ((float)loadedP / (float)totalP);
        if (fillW > 0.0f) {
            r.FillRound(RcWH(barBg.left, barBg.top, fillW, barH), 2.0f * s, theme::Accent, 1.0f);
        }
    }
}

void ReaderView::DrawSidebarChapterDropdown(Renderer& r, Ui& ui, const D2D1_RECT_F& anchorRc, double now) {
    const float s = m_scale;
    const auto& chapters = m_host ? m_host->GetCurrentChapters() : std::vector<OnlineChapter>{};
    const int curIdx = m_host ? m_host->GetCurrentChapterIndex() : -1;

    const float dropW = std::max(RcW(anchorRc), 320.0f * s);
    const float dropH = std::min(400.0f * s, RcH(m_area) - 40.0f * s);
    float dropX = anchorRc.right - dropW;
    if (dropX < m_area.left + 10.0f * s) dropX = m_area.left + 10.0f * s;
    float dropY = anchorRc.bottom + 4.0f * s;
    if (dropY + dropH > m_area.bottom - 10.0f * s) {
        dropY = std::max(m_area.top + 10.0f * s, m_area.bottom - dropH - 10.0f * s);
    }
    m_sidebarChDropdownRc = RcWH(dropX, dropY, dropW, dropH);

    // Kart zemini ve golge
    r.FillRound(m_sidebarChDropdownRc, 10.0f * s, theme::Panel, 0.98f);
    r.StrokeRound(m_sidebarChDropdownRc, 10.0f * s, theme::Border, 1.2f * s, 0.9f);

    const float pad = 10.0f * s;
    float curY = dropY + pad;

    // Arama kutusu ve Siralama dugmesi
    const float searchW = dropW - pad * 2.0f - 40.0f * s;
    const D2D1_RECT_F searchRc = RcWH(dropX + pad, curY, searchW, 30.0f * s);
    m_sidebarChSearch.Draw(r, ui, Id("sb.drop.search"), searchRc, i18n::Literal(L"Bölüm ara..."), true, now);

    const D2D1_RECT_F sortRc = RcWH(searchRc.right + 6.0f * s, curY, 34.0f * s, 30.0f * s);
    if (w::TextButton(r, ui, Id("sb.drop.sort"), sortRc, m_sidebarChSortAsc ? L"↑" : L"↓", false)) {
        m_sidebarChSortAsc = !m_sidebarChSortAsc;
        m_sidebarChNeedsScrollToCurrent = true;
        if (m_host) m_host->Invalidate();
    }
    ui.Tip(Id("sb.drop.sort"), m_sidebarChSortAsc ? i18n::Literal(L"Eskiden Yeniye Sıralı") : i18n::Literal(L"Yeniden Eskiye Sıralı"), sortRc);
    curY += 34.0f * s;

    // Bolum listesi
    const float listH = (dropY + dropH - pad) - curY;
    const D2D1_RECT_F listRc = RcWH(dropX + pad, curY, dropW - pad * 2.0f, listH);

    if (chapters.empty()) {
        r.Text(i18n::Literal(L"Çevrimiçi bölüm listesi bulunamadı."), listRc, theme::TextMuted, 12.0f * s, Renderer::Align::Center);
        return;
    }

    // Arama filtreleme
    std::wstring q = m_sidebarChSearch.text;
    for (auto& c : q) c = towlower(c);

    std::vector<size_t> filtered;
    filtered.reserve(chapters.size());
    for (size_t i = 0; i < chapters.size(); ++i) {
        if (q.empty()) {
            filtered.push_back(i);
        } else {
            std::wstring itemTitle = CleanChapterTitle(FromUtf8(chapters[i].title), (int)i);
            for (auto& c : itemTitle) c = towlower(c);
            if (itemTitle.find(q) != std::wstring::npos) {
                filtered.push_back(i);
            }
        }
    }

    if (filtered.empty()) {
        r.Text(i18n::Literal(L"Aramaya uygun bölüm bulunamadı."), listRc, theme::TextMuted, 12.0f * s, Renderer::Align::Center);
        return;
    }

    const float itemH = 34.0f * s;
    const float gap = 4.0f * s;
    const float pitch = itemH + gap;
    const float totalH = (float)filtered.size() * pitch - gap;
    m_sidebarChMaxScroll = std::max(0.0f, totalH - listH);

    // En son okunan bolumun pozisyonuna gore acilma (Kullanici istegi)
    if (m_sidebarChNeedsScrollToCurrent && curIdx >= 0) {
        m_sidebarChNeedsScrollToCurrent = false;
        for (size_t rowIdx = 0; rowIdx < filtered.size(); ++rowIdx) {
            const size_t fi = m_sidebarChSortAsc ? (filtered.size() - 1 - rowIdx) : rowIdx;
            if ((int)filtered[fi] == curIdx) {
                float targetY = (float)rowIdx * pitch - (listH * 0.45f);
                m_sidebarChScroll = std::clamp(targetY, 0.0f, m_sidebarChMaxScroll);
                break;
            }
        }
    }
    m_sidebarChScroll = std::clamp(m_sidebarChScroll, 0.0f, m_sidebarChMaxScroll);

    r.PushClip(listRc);
    const size_t first = (size_t)std::max(0.0f, std::floor(m_sidebarChScroll / pitch));
    for (size_t rowIdx = first; rowIdx < filtered.size(); ++rowIdx) {
        const size_t fi = m_sidebarChSortAsc ? (filtered.size() - 1 - rowIdx) : rowIdx;
        const size_t chapIdx = filtered[fi];
        const float itemY = listRc.top + (float)rowIdx * pitch - m_sidebarChScroll;
        if (itemY >= listRc.bottom) break;

        const D2D1_RECT_F itemRc = RcWH(listRc.left, itemY, RcW(listRc), itemH);
        const bool isCur = ((int)chapIdx == curIdx);
        const uint32_t itemId = Id("sb.drop.item", (uint32_t)chapIdx);
        const bool clicked = ui.Button(itemId, itemRc);
        const bool hot = ui.Hot(itemId);

        if (isCur) {
            r.FillRound(itemRc, 6.0f * s, theme::Active, theme::ActiveAlpha);
            r.StrokeRound(itemRc, 6.0f * s, theme::ActiveEdge, 1.0f * s, theme::ActiveEdgeAlpha);
        } else if (hot) {
            r.FillRound(itemRc, 6.0f * s, theme::White, 0.08f);
        } else {
            r.FillRound(itemRc, 6.0f * s, theme::Panel2, 0.5f);
        }

        std::wstring chapTitle = CleanChapterTitle(FromUtf8(chapters[chapIdx].title), (int)chapIdx);
        const float textPad = 8.0f * s;
        r.Text(chapTitle,
               Rc(itemRc.left + textPad, itemRc.top, itemRc.right - (isCur ? 64.0f * s : textPad), itemRc.bottom),
               isCur ? theme::Text : (hot ? theme::Text : theme::TextDim),
               12.5f * s, Renderer::Align::Left, isCur ? Renderer::Weight::Semibold : Renderer::Weight::Normal);

        if (isCur) {
            const D2D1_RECT_F curTagRc = RcWH(itemRc.right - 58.0f * s, itemRc.top + (itemH - 18.0f * s) * 0.5f, 50.0f * s, 18.0f * s);
            r.FillRound(curTagRc, 9.0f * s, theme::Active, 0.9f);
            r.Text(i18n::Literal(L"Şu anki"), curTagRc, theme::White, 10.0f * s, Renderer::Align::Center, Renderer::Weight::Semibold);
        }

        if (clicked) {
            if (m_host) {
                m_host->OpenChapterByIndex((int)chapIdx);
            }
            m_sidebarChDropdownOpen = false;
            m_sidebarChSearch.focused = false;
            break;
        }
    }
    r.PopClip();

    // Kaydirma cubugu
    if (m_sidebarChMaxScroll > 0.0f) {
        const float barW = 4.0f * s;
        const float trackH = listH;
        const float thumbH = std::max(20.0f * s, trackH * (listH / totalH));
        const float thumbY = listRc.top + (m_sidebarChScroll / m_sidebarChMaxScroll) * (trackH - thumbH);
        const D2D1_RECT_F thumbRc = RcWH(m_sidebarChDropdownRc.right - pad * 0.5f - barW * 0.5f, thumbY, barW, thumbH);
        r.FillRound(thumbRc, barW * 0.5f, theme::TextMuted, 0.5f);
    }
}

void ReaderView::DrawThumbnailStrip(Renderer& r, Ui& ui, const D2D1_RECT_F& dockRc, double now) {
    (void)ui;
    (void)now;
    if (m_pages.empty() || m_chrome <= 0.01f) {
        m_thumbStripRc = D2D1_RECT_F{};
        return;
    }
    const float a = m_chrome;
    const float s = m_scale;
    m_thumbStripRc = dockRc;

    // Panel zemini ve ince ust kenar cizgisi
    r.Fill(dockRc, theme::Panel, 0.92f * a);
    r.Line(dockRc.left, dockRc.top, dockRc.right, dockRc.top, theme::Border, 1.0f * s, 0.8f * a);

    const float padX = 12.0f * s;
    const float cardW = 46.0f * s;
    const float cardH = 62.0f * s;
    const float cardGap = 6.0f * s;
    const float cardY = dockRc.top + (RcH(dockRc) - cardH) * 0.5f;

    const float totalContentW = padX * 2.0f + (float)m_pages.size() * (cardW + cardGap) - cardGap;
    const float visibleW = RcW(dockRc);
    m_thumbMaxScrollX = std::max(0.0f, totalContentW - visibleW);
    m_thumbTargetScrollX = std::clamp(m_thumbTargetScrollX, 0.0f, m_thumbMaxScrollX);
    m_thumbScrollX = std::clamp(m_thumbScrollX, 0.0f, m_thumbMaxScrollX);

    // Aktif sayfaya otomatik kaydirma
    const int curPage = CurrentPageIndex();
    if (curPage != m_lastAutoScrolledPage && curPage >= 0 && curPage < (int)m_pages.size()) {
        m_lastAutoScrolledPage = curPage;
        const float targetLeft = padX + (float)curPage * (cardW + cardGap);
        if (targetLeft < m_thumbTargetScrollX + 40.0f * s) {
            m_thumbTargetScrollX = std::max(0.0f, targetLeft - 40.0f * s);
        } else if (targetLeft + cardW > m_thumbTargetScrollX + visibleW - 40.0f * s) {
            m_thumbTargetScrollX = std::clamp(targetLeft + cardW + 40.0f * s - visibleW, 0.0f, m_thumbMaxScrollX);
        }
    }

    r.PushClip(dockRc);

    for (size_t i = 0; i < m_pages.size(); ++i) {
        const float cardX = dockRc.left + padX + (float)i * (cardW + cardGap) - m_thumbScrollX;
        const D2D1_RECT_F cardRc = RcWH(cardX, cardY, cardW, cardH);

        // Ekran disindakileri kirp
        if (cardRc.right < dockRc.left || cardRc.left > dockRc.right) {
            continue;
        }

        const bool active = ((int)i == curPage);
        const bool hot = (m_mx >= cardRc.left && m_mx <= cardRc.right && m_my >= cardRc.top && m_my <= cardRc.bottom);

        // Kart zemini
        r.FillRound(cardRc, 4.0f * s, theme::Panel2, (active ? 0.95f : (hot ? 0.90f : 0.82f)) * a);

        const Page& p = m_pages[i];
        if (!p.tiles.empty() && p.tiles[0].bmp) {
            r.PushClip(cardRc);
            float dw = cardW, dh = cardH;
            if (p.natW > 0 && p.natH > 0) {
                float aspect = (float)p.natW / (float)p.natH;
                float cardAspect = cardW / cardH;
                if (aspect > cardAspect) {
                    dh = cardH;
                    dw = cardH * aspect;
                } else {
                    dw = cardW;
                    dh = cardW / aspect;
                }
            }
            const D2D1_RECT_F imgRc = RcWH(cardRc.left + (cardW - dw) * 0.5f, cardRc.top, dw, dh);
            r.DrawBitmap(p.tiles[0].bmp.Get(), imgRc, 1.0f * a);
            r.PopClip();
        } else {
            // Henuz yuklenmemis: merkezde sayfa numarasi
            std::wstring num = std::to_wstring(i + 1);
            r.Text(num, cardRc, theme::TextMuted, 12.0f * s, Renderer::Align::Center, Renderer::Weight::Bold, 0.55f * a);
        }

        // Alt etiket (sayfa numarasi seridi)
        const float labelH = 15.0f * s;
        const D2D1_RECT_F labelRc = Rc(cardRc.left, cardRc.bottom - labelH, cardRc.right, cardRc.bottom);
        r.Fill(labelRc, theme::Black, (active ? 0.85f : 0.70f) * a);
        std::wstring pnum = std::to_wstring(i + 1);
        r.Text(pnum, labelRc, active ? theme::Accent : theme::White, 10.0f * s,
               Renderer::Align::Center, Renderer::Weight::Semibold, a);

        // Cerceve / Kontur
        if (active) {
            r.StrokeRound(cardRc, 4.0f * s, theme::Accent, 2.0f * s, 1.0f * a);
        } else if (hot) {
            r.StrokeRound(cardRc, 4.0f * s, theme::Accent, 1.2f * s, 0.6f * a);
        } else {
            r.StrokeRound(cardRc, 4.0f * s, theme::Border, 1.0f * s, 0.4f * a);
        }
    }

    r.PopClip();
}

// ------------------------------------------------------------ girdi ---------

bool ReaderView::OverChrome(float mx, float my) const {
    if (!m_sidebarOpen && RcHit(m_sidebarReopenRc,mx,my)) return true;
    if (RcHit(m_scrollTrack, mx, my)) return true;
    if (m_sidebarOpen && RcHit(m_sidebarRc, mx, my)) return true;
    if (m_sidebarChDropdownOpen && RcHit(m_sidebarChDropdownRc, mx, my)) return true;
    if (m_chrome > 0.05f) {
        if (RcHit(m_hudRect, mx, my)) return true;
        if (!m_pages.empty() && RcHit(m_thumbStripRc, mx, my)) return true;
    }
    if (m_popOpen && RcHit(m_popRect, mx, my)) return true;
    return false;
}

bool ReaderView::ClosePopups() {
    if (m_sidebarChDropdownOpen) {
        m_sidebarChDropdownOpen = false;
        m_sidebarChSearch.focused = false;
        return true;
    }
    if (m_popOpen || m_helpOpen) {
        m_popOpen = false;
        m_helpOpen = false;
        return true;
    }
    return false;
}

void ReaderView::OnMouseMove(float mx, float my, double now) {
    const bool moved = std::fabs(mx - m_mx) > 0.5f || std::fabs(my - m_my) > 0.5f;
    m_mx = mx;
    m_my = my;
    if (moved) WakeChrome(now);
    if (m_scrollDragging) { DragScrollbar(my); return; }
    if (m_thumbDragging) {
        float dx = mx - m_thumbDragStartX;
        if (std::fabs(dx) > 4.0f * m_scale) {
            m_thumbDragMoved = true;
        }
        m_thumbTargetScrollX = std::clamp(m_thumbDragStartScrollX - dx, 0.0f, m_thumbMaxScrollX);
        m_thumbScrollX = m_thumbTargetScrollX;
        if (m_host) m_host->Invalidate();
        return;
    }
    if (m_dragging) {
        const float dx = mx - m_dragMx, dy = my - m_dragMy;
        if (!m_dragMoved && (dx * dx + dy * dy) > 16.0f * m_scale * m_scale) m_dragMoved = true;
        if (m_dragMoved) {
            m_targetY = m_scrollY = std::clamp(m_dragSy - dy, 0.0f, MaxScrollY());
            m_targetX = m_scrollX = std::clamp(m_dragSx - dx, 0.0f, MaxScrollX());
        }
    }
}

bool ReaderView::OnMouseDown(float mx, float my, double now) {
    WakeChrome(now);
    if(!m_sidebarOpen && RcHit(m_sidebarReopenRc,mx,my))return false;
    if (!m_popOpen && !m_helpOpen && !m_sidebarChDropdownOpen && RcHit(m_scrollTrack,mx,my)) {
        m_scrollDragging=true;
        m_scrollGrab=RcHit(m_scrollThumb,mx,my)?my-m_scrollThumb.top:RcH(m_scrollThumb)/2;
        DragScrollbar(my); return true;
    }
    if (m_sidebarChDropdownOpen && !RcHit(m_sidebarChDropdownRc, mx, my) && !RcHit(m_sidebarChSelectRc, mx, my)) {
        m_sidebarChDropdownOpen = false;
        m_sidebarChSearch.focused = false;
        if (m_host) m_host->Invalidate();
    }
    if (m_popOpen && !RcHit(m_popRect, mx, my) && !RcHit(m_gearRect, mx, my)) {
        m_popOpen = false;
        return false;
    }
    if (m_chrome > 0.05f && !m_pages.empty() && RcHit(m_thumbStripRc, mx, my)) {
        m_thumbDragging = true;
        m_thumbDragMoved = false;
        m_thumbDragStartX = mx;
        m_thumbDragStartScrollX = m_thumbTargetScrollX;

        const float s = m_scale;
        const float cardW = 46.0f * s, cardGap = 6.0f * s, padX = 12.0f * s;
        float relX = (mx - m_thumbStripRc.left - padX + m_thumbScrollX);
        if (relX >= 0) {
            int idx = (int)(relX / (cardW + cardGap));
            float offsetInCard = relX - idx * (cardW + cardGap);
            if (offsetInCard <= cardW && idx >= 0 && idx < (int)m_pages.size()) {
                m_thumbClickCandidate = idx;
            } else {
                m_thumbClickCandidate = -1;
            }
        } else {
            m_thumbClickCandidate = -1;
        }
        return true;
    }
    if (OverChrome(mx, my) || !RcHit(m_area, mx, my)) return false;
    m_dragging = true;
    m_dragMoved = false;
    m_dragMx = mx;
    m_dragMy = my;
    m_dragSx = m_scrollX;
    m_dragSy = m_scrollY;
    return true;
}

void ReaderView::OnMouseUp() {
    m_scrollDragging=false;
    if (m_thumbDragging) {
        if (!m_thumbDragMoved && m_thumbClickCandidate >= 0 && m_thumbClickCandidate < (int)m_pages.size()) {
            GoToPage(m_thumbClickCandidate);
        }
        m_thumbDragging = false;
        m_thumbDragMoved = false;
        m_thumbClickCandidate = -1;
        if (m_host) m_host->Invalidate();
    }
    m_dragging = false;
}

void ReaderView::OnMouseLeave() {
    // Fare HUD'un ustundeyken pencereden cikarsa HUD sonsuza dek gorunur kalmasin.
    m_mx = m_my = -1.0f;
    m_thumbDragging = false;
    m_thumbDragMoved = false;
    m_thumbClickCandidate = -1;
}

bool ReaderView::OnDoubleClick(float mx, float my) {
    if (OverChrome(mx, my) || !RcHit(m_area, mx, my)) return false;
    if (m_host) m_host->ToggleFullscreen();
    return true;
}

void ReaderView::OnWheel(float notches, bool ctrl, bool shift, float mx, float my) {
    if (m_sidebarChDropdownOpen && RcHit(m_sidebarChDropdownRc, mx, my)) {
        m_sidebarChScroll = std::clamp(m_sidebarChScroll - notches * 40.0f * m_scale, 0.0f, m_sidebarChMaxScroll);
        if (m_host) m_host->Invalidate();
        return;
    }
    if (m_chrome > 0.05f && !m_pages.empty() && RcHit(m_thumbStripRc, mx, my)) {
        m_thumbTargetScrollX = std::clamp(m_thumbTargetScrollX - notches * 100.0f * m_scale, 0.0f, m_thumbMaxScrollX);
        if (m_host) m_host->Invalidate();
        return;
    }
    if (ctrl) {
        // Hassas tekerlek ve dokunmatik yuzey kesirli adim gonderir: carpan
        // adimla orantili olmali, yoksa sikistirma ile zoom asiri hizli olur.
        ZoomBy(std::pow(1.1f, notches), mx, my);
        return;
    }
    const float step = 120.0f * m_scale;
    if (shift) {
        m_targetX = std::clamp(m_targetX - notches * step, 0.0f, MaxScrollX());
        if (m_host) m_host->Invalidate();
        return;
    }
    const bool atEnd = m_targetY >= MaxScrollY() - 1.0f;
    ScrollBy(-notches * step);

    // Sihirli kaydirma: en alttayken asagi kaydirmaya devam edilirse sonraki bolum.
    const double now = GetTickCount64() / 1000.0;
    if (now - m_overscrollAt > 1.2) m_overscroll = 0.0f;
    m_overscrollAt = now;
    if (notches < 0 && atEnd && m_set->magicScroll && !m_nextDir.empty()) {
        m_overscroll += -notches;
        if (m_overscroll >= 3.0f) {
            m_overscroll = 0.0f;
            OpenChapter(+1);
        }
    } else if (notches > 0) {
        m_overscroll = 0.0f;
    }
}

bool ReaderView::OnChar(wchar_t ch) {
    if (m_sidebarChDropdownOpen && m_sidebarChSearch.focused) {
        if (m_sidebarChSearch.OnChar(ch)) {
            m_sidebarChScroll = 0.0f;
            if (m_host) m_host->Invalidate();
            return true;
        }
    }
    return false;
}

bool ReaderView::OnKey(UINT vk, bool ctrl, bool shift, bool repeat) {
    if (m_sidebarChDropdownOpen && m_sidebarChSearch.focused) {
        if (vk == VK_ESCAPE) {
            m_sidebarChDropdownOpen = false;
            m_sidebarChSearch.focused = false;
            if (m_host) m_host->Invalidate();
            return true;
        }
        if (m_sidebarChSearch.OnKey(vk, ctrl, shift)) {
            m_sidebarChScroll = 0.0f;
            if (m_host) m_host->Invalidate();
            return true;
        }
        return false; // Keep unhandled editor keys out of reader shortcuts.
    }

    const float viewH = RcH(m_area);
    const bool rtl = IsRtl();
    // Durum degistiren tuslar (W B R M H C 1-4 , .) tus tekrarinda calismaz;
    // kaydirma ve sayfa tuslari basili tutulunca akmaya devam eder.
    switch (vk) {
    case 'W': case 'B': case 'R': case 'M': case 'H': case 'C':
    case '1': case '2': case '3': case '4':
    case VK_NUMPAD1: case VK_NUMPAD2: case VK_NUMPAD3: case VK_NUMPAD4:
    case VK_OEM_COMMA: case VK_OEM_PERIOD:
    case '0': case VK_NUMPAD0:
        if (repeat) return !(vk == 'R' && ctrl);
        break;
    default: break;
    }
    switch (vk) {
    case VK_RIGHT: case 'D': rtl ? PrevPage() : NextPage(); return true;
    case VK_LEFT:  case 'A': rtl ? NextPage() : PrevPage(); return true;
    case VK_DOWN:  ScrollBy(90.0f * m_scale); return true;
    case VK_UP:    ScrollBy(-90.0f * m_scale); return true;
    case VK_NEXT:  ScrollBy(viewH * 0.9f); return true;
    case VK_PRIOR: ScrollBy(-viewH * 0.9f); return true;
    case VK_SPACE: ScrollBy(shift ? -viewH * 0.9f : viewH * 0.9f); return true;
    case VK_HOME:  ScrollTo(0.0f); return true;
    case VK_END:   ScrollTo(MaxScrollY()); return true;
    case VK_OEM_COMMA:  OpenChapter(-1); return true;
    case VK_OEM_PERIOD: OpenChapter(+1); return true;
    case 'C':
        m_sidebarOpen = true;
        m_sidebarChDropdownOpen = !m_sidebarChDropdownOpen;
        if (m_sidebarChDropdownOpen) {
            m_sidebarChSearch.Clear();
            m_sidebarChSearch.focused = true;
            m_sidebarChScroll = 0.0f;
            m_sidebarChNeedsScrollToCurrent = true;
        }
        if (m_host) m_host->Invalidate();
        return true;
    case '1': case VK_NUMPAD1: SetFit(FitMode::Width);    if (m_host) m_host->Toast(T(S::FitWidth));    return true;
    case '2': case VK_NUMPAD2: SetFit(FitMode::Height);   if (m_host) m_host->Toast(T(S::FitHeight));   return true;
    case '3': case VK_NUMPAD3: SetFit(FitMode::Window);   if (m_host) m_host->Toast(T(S::FitWindow));   return true;
    case '4': case VK_NUMPAD4: SetFit(FitMode::Original); if (m_host) m_host->Toast(T(S::FitOriginal)); return true;
    case 'W': ToggleWebtoon(); return true;
    case 'B': ToggleDouble(); return true;
    case 'R': if (!ctrl) { ToggleDirection(); return true; } return false;
    case 'M':
        m_sidebarOpen = !m_sidebarOpen;
        if (!m_sidebarOpen) m_sidebarChDropdownOpen = false;
        if (m_host) m_host->Invalidate();
        return true;
    case VK_OEM_PLUS: case VK_ADD:       ZoomBy(1.15f, -1, -1); return true;
    case VK_OEM_MINUS: case VK_SUBTRACT: ZoomBy(1.0f / 1.15f, -1, -1); return true;
    case '0': case VK_NUMPAD0:
        m_zoom = 1.0f;
        RelayoutKeepingPosition(m_area.top + viewH * 0.35f);
        if (m_host) m_host->Toast(Fmt(T(S::ZoomValue), 100));
        return true;
    case VK_OEM_4: // [
    case VK_OEM_6: { // ]
        m_set->readerDim = std::clamp(m_set->readerDim + (vk == VK_OEM_6 ? 0.04f : -0.04f), 0.5f, 1.0f);
        if (m_host) {
            m_host->Toast(Fmt(T(S::DimValue), (int)std::lround(m_set->readerDim * 100.0f)));
            m_host->SaveSettings();
        }
        return true;
    }
    case 'H': m_helpOpen = !m_helpOpen; return true;
    default: return false;
    }
}

} // namespace gm
