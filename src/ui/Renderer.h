#pragma once
//
// D3D11 + DXGI + DirectComposition tabani, uzerinde Direct2D ve DirectWrite.
// FullTerminal'in Renderer'inin kabuk kismindan turedi; terminal yerine
// bitmap (manga sayfasi) cizimi ve vektor ikon icin yol geometrisi eklendi.
//
// Tum koordinatlar aygit pikselidir. dp -> px donusumu cagiran tarafta
// Scale() ile yapilir.
//
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <dcomp.h>
#include <wrl/client.h>

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>

namespace gm {

using Microsoft::WRL::ComPtr;

inline D2D1_RECT_F Rc(float l, float t, float r, float b) { return D2D1::RectF(l, t, r, b); }
inline D2D1_RECT_F RcWH(float x, float y, float w, float h) { return D2D1::RectF(x, y, x + w, y + h); }
inline float RcW(const D2D1_RECT_F& r) { return r.right - r.left; }
inline float RcH(const D2D1_RECT_F& r) { return r.bottom - r.top; }
inline bool RcHit(const D2D1_RECT_F& r, float x, float y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}
inline D2D1_RECT_F RcInset(const D2D1_RECT_F& r, float dx, float dy) {
    return D2D1::RectF(r.left + dx, r.top + dy, r.right - dx, r.bottom - dy);
}

class Renderer {
public:
    enum class Align { Left, Center, Right };
    enum class Weight { Normal, Semibold, Bold };

    bool Init(HWND hwnd, UINT dpi, std::wstring* err);
    void Shutdown();
    bool Ready() const { return m_target != nullptr; }
    HWND Window() const { return m_hwnd; }

    void Resize(UINT pxW, UINT pxH);
    void SetDpi(UINT dpi);

    UINT  Width()  const { return m_width; }
    UINT  Height() const { return m_height; }
    float Scale()  const { return m_dpi / 96.0f; }
    UINT  MaxBitmapSize() const { return m_maxBitmap; }

    // Aygit kaybinda artar. Bitmap tutan her katman bunu izleyip kendi
    // bitmaplerini atmali: eski cihazin bitmapleri yeni baglamda cizilemez.
    uint32_t DeviceGeneration() const { return m_devGen; }

    bool Begin();
    void End();

    void Clear(uint32_t color, float alpha);
    void Fill(const D2D1_RECT_F& r, uint32_t color, float alpha = 1.0f);
    void FillRound(const D2D1_RECT_F& r, float radius, uint32_t color, float alpha = 1.0f);
    void StrokeRound(const D2D1_RECT_F& r, float radius, uint32_t color, float width = 1.0f, float alpha = 1.0f);
    void Line(float x0, float y0, float x1, float y1, uint32_t color, float width = 1.0f, float alpha = 1.0f);
    void Disc(float cx, float cy, float r, uint32_t color, float alpha = 1.0f);
    void Ring(float cx, float cy, float r, uint32_t color, float width = 1.0f, float alpha = 1.0f);
    void Polyline(const D2D1_POINT_2F* pts, size_t n, uint32_t color, float width,
                  float alpha = 1.0f, bool closed = false);
    void FillPolygon(const D2D1_POINT_2F* pts, size_t n, uint32_t color, float alpha = 1.0f);

    void Text(const std::wstring& s, const D2D1_RECT_F& r, uint32_t color, float sizePx,
              Align align = Align::Left, Weight weight = Weight::Normal, float alpha = 1.0f,
              bool wrap = false, bool elide = true, bool mono = false);
    float MeasureText(const std::wstring& s, float sizePx, Weight weight = Weight::Normal, bool mono = false);
    float MeasureWrappedHeight(const std::wstring& s, float sizePx, float maxW, Weight weight = Weight::Normal);

    // 32bpp premultiplied BGRA
    ComPtr<ID2D1Bitmap1> CreateBitmap(UINT w, UINT h, const void* data, UINT pitch);
    void DrawBitmap(ID2D1Bitmap1* bmp, const D2D1_RECT_F& dst, float alpha = 1.0f,
                    const D2D1_RECT_F* src = nullptr);

    void PushClip(const D2D1_RECT_F& r);
    void PopClip();

private:
    bool CreateDeviceResources(std::wstring* err);
    void ReleaseDeviceResources();
    bool CreateTargetBitmap();
    void ReleaseTargetBitmap();
    void RecoverDevice();
    void PickFonts();

    ID2D1SolidColorBrush* Brush(uint32_t color, float alpha);
    IDWriteTextFormat* Format(float sizePx, Weight weight, bool mono);
    IDWriteInlineObject* Ellipsis(IDWriteTextFormat* f, uint64_t key);

    ComPtr<ID3D11Device>          m_d3d;
    ComPtr<ID3D11DeviceContext>   m_d3dCtx;
    ComPtr<IDXGISwapChain1>       m_swap;
    ComPtr<ID2D1Factory1>         m_d2dFactory;
    ComPtr<ID2D1Device>           m_d2dDevice;
    ComPtr<ID2D1DeviceContext>    m_ctx;
    ComPtr<ID2D1Bitmap1>          m_target;
    ComPtr<IDWriteFactory>        m_dwrite;
    ComPtr<IDCompositionDevice>   m_dcomp;
    ComPtr<IDCompositionTarget>   m_dcompTarget;
    ComPtr<IDCompositionVisual>   m_visual;
    ComPtr<ID2D1StrokeStyle>      m_round;

    std::map<uint64_t, ComPtr<ID2D1SolidColorBrush>> m_brushes;
    std::map<uint64_t, ComPtr<IDWriteTextFormat>>    m_formats;
    std::map<uint64_t, ComPtr<IDWriteInlineObject>>  m_ellipsis;
    std::unordered_map<std::wstring, float>          m_measure;

    std::wstring m_uiFont = L"Segoe UI";
    std::wstring m_monoFont = L"Consolas";

    HWND     m_hwnd = nullptr;
    UINT     m_width = 0, m_height = 0;
    UINT     m_dpi = 96;
    UINT     m_maxBitmap = 4096;
    uint32_t m_devGen = 1;
    bool     m_inDraw = false;
    bool     m_needRecover = false;     // kurtarma basarisiz: Begin'de yeniden dene
    ULONGLONG m_lastRecoverTry = 0;
};

} // namespace gm
