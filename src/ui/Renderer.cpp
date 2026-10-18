#include "ui/Renderer.h"
#include "ui/Theme.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace gm {
namespace {

inline D2D1_COLOR_F ColorOf(uint32_t rgb, float a) {
    return D2D1::ColorF(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f,
                        (rgb & 0xFF) / 255.0f, a);
}
inline uint64_t BrushKey(uint32_t color, float alpha) {
    return ((uint64_t)color << 32) | (uint32_t)std::lround(std::clamp(alpha, 0.0f, 1.0f) * 1000.0f);
}
inline uint64_t FormatKey(float sizePx, Renderer::Weight w, bool mono) {
    return ((uint64_t)std::lround(sizePx * 100.0f) << 8) | ((uint64_t)w << 1) | (mono ? 1u : 0u);
}
inline DWRITE_FONT_WEIGHT DwWeight(Renderer::Weight w) {
    switch (w) {
    case Renderer::Weight::Semibold: return DWRITE_FONT_WEIGHT_SEMI_BOLD;
    case Renderer::Weight::Bold:     return DWRITE_FONT_WEIGHT_BOLD;
    default:                         return DWRITE_FONT_WEIGHT_NORMAL;
    }
}

} // namespace

// ------------------------------------------------------------------ kurulum --

bool Renderer::Init(HWND hwnd, UINT dpi, std::wstring* err) {
    m_hwnd = hwnd;
    m_dpi = dpi ? dpi : 96;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    m_width = std::max<LONG>(1, rc.right - rc.left);
    m_height = std::max<LONG>(1, rc.bottom - rc.top);

    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(m_dwrite.GetAddressOf())))) {
        if (err) *err = L"DirectWrite fabrikasi olusturulamadi.";
        return false;
    }
    PickFonts();

    D2D1_FACTORY_OPTIONS opts{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts,
                                 reinterpret_cast<void**>(m_d2dFactory.GetAddressOf())))) {
        if (err) *err = L"Direct2D fabrikasi olusturulamadi.";
        return false;
    }

    D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
        D2D1_LINE_JOIN_ROUND, 10.0f, D2D1_DASH_STYLE_SOLID, 0.0f);
    m_d2dFactory->CreateStrokeStyle(sp, nullptr, 0, &m_round);

    return CreateDeviceResources(err);
}

void Renderer::PickFonts() {
    ComPtr<IDWriteFontCollection> coll;
    if (FAILED(m_dwrite->GetSystemFontCollection(&coll, FALSE))) return;
    auto pick = [&](const wchar_t* const* cands, size_t n, std::wstring& out) {
        for (size_t i = 0; i < n; ++i) {
            UINT32 idx = 0;
            BOOL exists = FALSE;
            if (SUCCEEDED(coll->FindFamilyName(cands[i], &idx, &exists)) && exists) {
                out = cands[i];
                return;
            }
        }
    };
    pick(theme::UiFontCandidates, std::size(theme::UiFontCandidates), m_uiFont);
    pick(theme::MonoFontCandidates, std::size(theme::MonoFontCandidates), m_monoFont);
}

bool Renderer::CreateDeviceResources(std::wstring* err) {
    auto fail = [&](const wchar_t* m) {
        if (err) *err = m;
        log::Write(L"Renderer: %s", m);
        ReleaseDeviceResources(); // yarim kalan kaynak birakma
        return false;
    };

    static const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
    };
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                                   (UINT)std::size(levels), D3D11_SDK_VERSION, &m_d3d, nullptr, &m_d3dCtx);
    if (FAILED(hr)) {
        // Donanim yoksa WARP: yavas ama calisir.
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels,
                               (UINT)std::size(levels), D3D11_SDK_VERSION, &m_d3d, nullptr, &m_d3dCtx);
    }
    if (FAILED(hr)) return fail(L"D3D11 cihazi olusturulamadi.");

    ComPtr<IDXGIDevice1> dxgiDevice;
    if (FAILED(m_d3d.As(&dxgiDevice))) return fail(L"IDXGIDevice alinamadi.");
    dxgiDevice->SetMaximumFrameLatency(1);

    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter))) return fail(L"DXGI adaptoru alinamadi.");
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) return fail(L"IDXGIFactory2 alinamadi.");

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = m_width;
    sd.Height = m_height;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    if (FAILED(factory->CreateSwapChainForComposition(m_d3d.Get(), &sd, nullptr, &m_swap)))
        return fail(L"Swapchain olusturulamadi.");

    if (FAILED(m_d2dFactory->CreateDevice(dxgiDevice.Get(), &m_d2dDevice)))
        return fail(L"Direct2D cihazi olusturulamadi.");
    if (FAILED(m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_ctx)))
        return fail(L"Direct2D baglami olusturulamadi.");
    m_maxBitmap = std::max<UINT>(2048, m_ctx->GetMaximumBitmapSize());

    if (!CreateTargetBitmap()) return fail(L"Hedef bitmap olusturulamadi.");

    if (FAILED(DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&m_dcomp))))
        return fail(L"DirectComposition cihazi olusturulamadi.");
    if (FAILED(m_dcomp->CreateTargetForHwnd(m_hwnd, TRUE, &m_dcompTarget)))
        return fail(L"DirectComposition hedefi olusturulamadi.");
    if (FAILED(m_dcomp->CreateVisual(&m_visual)))
        return fail(L"DirectComposition gorseli olusturulamadi.");
    m_visual->SetContent(m_swap.Get());
    m_dcompTarget->SetRoot(m_visual.Get());
    m_dcomp->Commit();
    return true;
}

bool Renderer::CreateTargetBitmap() {
    if (!m_ctx || !m_swap) return false;
    ComPtr<IDXGISurface> surface;
    if (FAILED(m_swap->GetBuffer(0, IID_PPV_ARGS(&surface)))) return false;
    // 96 DPI sabit: koordinatlar aygit pikselinde kalsin.
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    if (FAILED(m_ctx->CreateBitmapFromDxgiSurface(surface.Get(), &props, &m_target))) return false;
    m_ctx->SetTarget(m_target.Get());
    m_ctx->SetDpi(96.0f, 96.0f);
    m_ctx->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    return true;
}

void Renderer::ReleaseTargetBitmap() {
    if (m_ctx) m_ctx->SetTarget(nullptr);
    m_target.Reset();
}

void Renderer::ReleaseDeviceResources() {
    m_brushes.clear();
    ReleaseTargetBitmap();
    m_visual.Reset();
    m_dcompTarget.Reset();
    m_dcomp.Reset();
    m_ctx.Reset();
    m_d2dDevice.Reset();
    m_swap.Reset();
    m_d3dCtx.Reset();
    m_d3d.Reset();
}

void Renderer::Shutdown() {
    m_inDraw = false;
    m_needRecover = false;
    ReleaseDeviceResources();
    m_formats.clear();
    m_ellipsis.clear();
    m_measure.clear();
    m_round.Reset();
    m_d2dFactory.Reset();
    m_dwrite.Reset();
}

namespace {
bool IsDeviceLost(HRESULT hr) {
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
           hr == DXGI_ERROR_DEVICE_HUNG || hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR ||
           hr == D2DERR_RECREATE_TARGET;
}
} // namespace

void Renderer::RecoverDevice() {
    log::Write(L"Renderer: grafik aygiti kayboldu, yeniden kuruluyor.");
    m_inDraw = false;
    ReleaseDeviceResources();
    ++m_devGen;
    m_lastRecoverTry = GetTickCount64();
    std::wstring err;
    m_needRecover = !CreateDeviceResources(&err);
    if (m_needRecover) log::Write(L"Renderer: yeniden kurulum basarisiz, tekrar denenecek: %s", err.c_str());
}

void Renderer::Resize(UINT w, UINT h) {
    w = std::max<UINT>(1, w);
    h = std::max<UINT>(1, h);
    if (w == m_width && h == m_height && m_target) return;
    const UINT oldW = m_width, oldH = m_height;
    m_width = w;
    m_height = h;
    if (!m_swap) return;
    ReleaseTargetBitmap();
    const HRESULT hr = m_swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
    if (IsDeviceLost(hr)) { RecoverDevice(); return; }
    if (SUCCEEDED(hr)) {
        if (!CreateTargetBitmap()) RecoverDevice();
        return;
    }
    // Baska bir hata: tamponlar eski boyutta duruyor; eskisiyle devam et ki
    // pencere donuk kalmasin, bir sonraki WM_SIZE yeniden dener.
    log::Write(L"Renderer: ResizeBuffers basarisiz (0x%08X), eski boyutla devam.", (unsigned)hr);
    m_width = oldW;
    m_height = oldH;
    if (!CreateTargetBitmap()) RecoverDevice();
}

void Renderer::SetDpi(UINT dpi) {
    if (!dpi || dpi == m_dpi) return;
    m_dpi = dpi;
    m_measure.clear();
}

// ------------------------------------------------------------------- kare --

bool Renderer::Begin() {
    if (!Ready()) {
        // Aygit kurtarma daha once basarisiz olduysa saniyede en fazla bir kez dene.
        const ULONGLONG now = GetTickCount64();
        if (m_needRecover && now - m_lastRecoverTry >= 1000) RecoverDevice();
        if (!Ready()) return false;
    }
    m_ctx->BeginDraw();
    m_inDraw = true;
    // Tamamen saydam temizlik: okuyucunun yari saydam zemini buna dayaniyor.
    m_ctx->Clear(D2D1::ColorF(0, 0, 0, 0));
    return true;
}

void Renderer::End() {
    if (!m_inDraw) return;
    m_inDraw = false;
    if (!m_ctx || !m_swap) return;
    const HRESULT hr = m_ctx->EndDraw();
    if (IsDeviceLost(hr)) { RecoverDevice(); return; }
    const HRESULT pr = m_swap->Present(1, 0);
    if (IsDeviceLost(pr)) RecoverDevice();
}

// --------------------------------------------------------------- ilkeller --

ID2D1SolidColorBrush* Renderer::Brush(uint32_t color, float alpha) {
    if (!m_ctx) return nullptr;
    const uint64_t key = BrushKey(color, alpha);
    auto it = m_brushes.find(key);
    if (it != m_brushes.end()) return it->second.Get();
    ComPtr<ID2D1SolidColorBrush> b;
    if (FAILED(m_ctx->CreateSolidColorBrush(ColorOf(color, std::clamp(alpha, 0.0f, 1.0f)), &b))) return nullptr;
    if (m_brushes.size() > 512) m_brushes.clear();
    return (m_brushes[key] = b).Get();
}

IDWriteTextFormat* Renderer::Format(float sizePx, Weight weight, bool mono) {
    if (!m_dwrite) return nullptr;
    const uint64_t key = FormatKey(sizePx, weight, mono);
    auto it = m_formats.find(key);
    if (it != m_formats.end()) return it->second.Get();
    ComPtr<IDWriteTextFormat> f;
    const std::wstring& family = mono ? m_monoFont : m_uiFont;
    if (FAILED(m_dwrite->CreateTextFormat(family.c_str(), nullptr, DwWeight(weight),
                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                          std::max(1.0f, sizePx), L"", &f)))
        return nullptr;
    return (m_formats[key] = f).Get();
}

IDWriteInlineObject* Renderer::Ellipsis(IDWriteTextFormat* f, uint64_t key) {
    if (!m_dwrite || !f) return nullptr;
    auto it = m_ellipsis.find(key);
    if (it != m_ellipsis.end()) return it->second.Get();
    ComPtr<IDWriteInlineObject> sign;
    if (FAILED(m_dwrite->CreateEllipsisTrimmingSign(f, &sign))) return nullptr;
    return (m_ellipsis[key] = sign).Get();
}

void Renderer::Clear(uint32_t color, float alpha) {
    if (m_ctx) m_ctx->Clear(ColorOf(color, alpha));
}

void Renderer::Fill(const D2D1_RECT_F& r, uint32_t color, float alpha) {
    if (auto* b = Brush(color, alpha)) m_ctx->FillRectangle(r, b);
}

void Renderer::FillRound(const D2D1_RECT_F& r, float radius, uint32_t color, float alpha) {
    if (auto* b = Brush(color, alpha)) m_ctx->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), b);
}

void Renderer::StrokeRound(const D2D1_RECT_F& r, float radius, uint32_t color, float width, float alpha) {
    if (auto* b = Brush(color, alpha)) {
        const float h = width * 0.5f;
        m_ctx->DrawRoundedRectangle(D2D1::RoundedRect(RcInset(r, h, h), radius, radius), b, width);
    }
}

void Renderer::Line(float x0, float y0, float x1, float y1, uint32_t color, float width, float alpha) {
    if (auto* b = Brush(color, alpha))
        m_ctx->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), b, width, m_round.Get());
}

void Renderer::Disc(float cx, float cy, float r, uint32_t color, float alpha) {
    if (auto* b = Brush(color, alpha)) m_ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), b);
}

void Renderer::Ring(float cx, float cy, float r, uint32_t color, float width, float alpha) {
    if (auto* b = Brush(color, alpha)) m_ctx->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), b, width);
}

void Renderer::Polyline(const D2D1_POINT_2F* pts, size_t n, uint32_t color, float width, float alpha, bool closed) {
    if (n < 2 || !m_d2dFactory) return;
    ComPtr<ID2D1PathGeometry> geo;
    if (FAILED(m_d2dFactory->CreatePathGeometry(&geo))) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geo->Open(&sink))) return;
    sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLines(pts + 1, (UINT32)(n - 1));
    sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    if (FAILED(sink->Close())) return;
    if (auto* b = Brush(color, alpha)) m_ctx->DrawGeometry(geo.Get(), b, width, m_round.Get());
}

void Renderer::FillPolygon(const D2D1_POINT_2F* pts, size_t n, uint32_t color, float alpha) {
    if (n < 3 || !m_d2dFactory) return;
    ComPtr<ID2D1PathGeometry> geo;
    if (FAILED(m_d2dFactory->CreatePathGeometry(&geo))) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geo->Open(&sink))) return;
    sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLines(pts + 1, (UINT32)(n - 1));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) return;
    if (auto* b = Brush(color, alpha)) m_ctx->FillGeometry(geo.Get(), b);
}

void Renderer::Text(const std::wstring& s, const D2D1_RECT_F& r, uint32_t color, float sizePx,
                    Align align, Weight weight, float alpha, bool wrap, bool elide, bool mono) {
    if (s.empty() || !m_ctx || RcW(r) <= 0.0f || RcH(r) <= 0.0f) return;
    IDWriteTextFormat* f = Format(sizePx, weight, mono);
    ID2D1SolidColorBrush* b = Brush(color, alpha);
    if (!f || !b) return;
    f->SetTextAlignment(align == Align::Left   ? DWRITE_TEXT_ALIGNMENT_LEADING
                      : align == Align::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                                               : DWRITE_TEXT_ALIGNMENT_TRAILING);
    f->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    f->SetParagraphAlignment(wrap ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR : DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    DWRITE_TRIMMING trim{ DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0 };
    IDWriteInlineObject* sign = nullptr;
    if (elide && !wrap) {
        trim.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
        sign = Ellipsis(f, FormatKey(sizePx, weight, mono));
    }
    f->SetTrimming(&trim, sign);
    m_ctx->DrawTextW(s.c_str(), (UINT32)s.size(), f, r, b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float Renderer::MeasureText(const std::wstring& s, float sizePx, Weight weight, bool mono) {
    if (s.empty() || !m_dwrite) return 0.0f;
    std::wstring key = s;
    key.push_back(L'\x1f');
    key += std::to_wstring(std::lround(sizePx * 100.0f));
    key.push_back((wchar_t)(L'0' + (int)weight + (mono ? 4 : 0)));
    auto it = m_measure.find(key);
    if (it != m_measure.end()) return it->second;

    IDWriteTextFormat* f = Format(sizePx, weight, mono);
    if (!f) return 0.0f;
    f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(m_dwrite->CreateTextLayout(s.c_str(), (UINT32)s.size(), f, 100000.0f, 1000.0f, &layout)))
        return 0.0f;
    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);
    if (m_measure.size() > 4096) m_measure.clear();
    return (m_measure[key] = tm.widthIncludingTrailingWhitespace);
}

float Renderer::MeasureWrappedHeight(const std::wstring& s, float sizePx, float maxW, Weight weight) {
    if (s.empty() || !m_dwrite || maxW <= 0.0f) return 0.0f;
    IDWriteTextFormat* f = Format(sizePx, weight, false);
    if (!f) return 0.0f;
    f->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(m_dwrite->CreateTextLayout(s.c_str(), (UINT32)s.size(), f, maxW, 100000.0f, &layout)))
        return 0.0f;
    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);
    return tm.height;
}

ComPtr<ID2D1Bitmap1> Renderer::CreateBitmap(UINT w, UINT h, const void* data, UINT pitch) {
    ComPtr<ID2D1Bitmap1> bmp;
    if (!m_ctx || !w || !h) return bmp;
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    if (FAILED(m_ctx->CreateBitmap(D2D1::SizeU(w, h), data, pitch, &props, &bmp))) bmp.Reset();
    return bmp;
}

void Renderer::DrawBitmap(ID2D1Bitmap1* bmp, const D2D1_RECT_F& dst, float alpha, const D2D1_RECT_F* src) {
    if (!bmp || !m_ctx) return;
    m_ctx->DrawBitmap(bmp, dst, alpha, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, src, nullptr);
}

void Renderer::PushClip(const D2D1_RECT_F& r) {
    if (m_ctx && m_inDraw) m_ctx->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED);
}

void Renderer::PopClip() {
    if (m_ctx && m_inDraw) m_ctx->PopAxisAlignedClip();
}

} // namespace gm
