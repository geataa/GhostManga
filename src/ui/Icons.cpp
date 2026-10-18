#include "ui/Icons.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gm {
namespace {

constexpr float kPi = 3.14159265358979f;

struct Grid {
    float ox, oy, u;
    D2D1_POINT_2F P(float x, float y) const { return D2D1::Point2F(ox + x * u, oy + y * u); }
    float X(float x) const { return ox + x * u; }
    float Y(float y) const { return oy + y * u; }
};

void Seg(Renderer& r, const Grid& g, float x0, float y0, float x1, float y1, uint32_t c, float w, float a) {
    r.Line(g.X(x0), g.Y(y0), g.X(x1), g.Y(y1), c, w, a);
}

void Poly(Renderer& r, const Grid& g, std::initializer_list<float> xy, uint32_t c, float w, float a,
          bool closed = false) {
    std::vector<D2D1_POINT_2F> pts;
    const float* p = xy.begin();
    for (size_t i = 0; i + 1 < xy.size(); i += 2) pts.push_back(g.P(p[i], p[i + 1]));
    r.Polyline(pts.data(), pts.size(), c, w, a, closed);
}

void Rect(Renderer& r, const Grid& g, float l, float t, float rr, float b, float rad, uint32_t c, float w, float a) {
    r.StrokeRound(Rc(g.X(l), g.Y(t), g.X(rr), g.Y(b)), rad * g.u, c, w, a);
}

std::vector<D2D1_POINT_2F> GhostOutline(const Grid& g) {
    std::vector<D2D1_POINT_2F> pts;
    const float cx = 12.0f, cy = 10.5f, rad = 6.5f;
    pts.push_back(g.P(cx - rad, 19.0f));
    for (int i = 0; i <= 14; ++i) {
        const float th = kPi + kPi * (float)i / 14.0f; // soldan tepeye, saga
        pts.push_back(g.P(cx + rad * std::cos(th), cy + rad * std::sin(th)));
    }
    pts.push_back(g.P(cx + rad, 19.0f));
    // Alt kenar: uc dalga
    const float left = cx - rad, right = cx + rad, step = (right - left) / 6.0f;
    for (int i = 1; i <= 6; ++i) {
        const float x = right - step * (float)i;
        const float y = (i % 2 == 1) ? 17.0f : 19.0f;
        pts.push_back(g.P(x, y));
    }
    return pts;
}

} // namespace

void DrawIcon(Renderer& r, Icon icon, const D2D1_RECT_F& box, float sizePx, uint32_t c, float a) {
    const float cx = (box.left + box.right) * 0.5f;
    const float cy = (box.top + box.bottom) * 0.5f;
    const Grid g{ std::round(cx - sizePx * 0.5f), std::round(cy - sizePx * 0.5f), sizePx / 24.0f };
    const float w = std::max(1.0f, std::round(1.6f * sizePx / 16.0f * 10.0f) / 10.0f);

    switch (icon) {
    case Icon::Trash:
        Seg(r,g,5,7,19,7,c,w,a);Seg(r,g,9,4,15,4,c,w,a);
        Seg(r,g,7,7,8,20,c,w,a);Seg(r,g,17,7,16,20,c,w,a);Seg(r,g,8,20,16,20,c,w,a);
        Seg(r,g,10,10,10.5f,17,c,w,a);Seg(r,g,14,10,13.5f,17,c,w,a);break;
    case Icon::Read:
        Seg(r,g,8,5,19,12,c,w,a);Seg(r,g,19,12,8,19,c,w,a);Seg(r,g,8,19,8,5,c,w,a);break;
    case Icon::Check:
        Seg(r,g,5,12,10,17,c,w,a);Seg(r,g,10,17,20,6,c,w,a);break;
    case Icon::Refresh:
        for(int i=0;i<16;++i){const float t=0.8f+i*0.3f;Seg(r,g,12+7*std::cos(t),12+7*std::sin(t),12+7*std::cos(t+0.3f),12+7*std::sin(t+0.3f),c,w,a);}
        Seg(r,g,18,4,19,9,c,w,a);Seg(r,g,19,9,14,8,c,w,a);break;
    case Icon::Eye: case Icon::EyeOff:
        Seg(r,g,3,12,7,7,c,w,a);Seg(r,g,7,7,17,7,c,w,a);Seg(r,g,17,7,21,12,c,w,a);
        Seg(r,g,21,12,17,17,c,w,a);Seg(r,g,17,17,7,17,c,w,a);Seg(r,g,7,17,3,12,c,w,a);
        r.Disc(g.X(12),g.Y(12),2*g.u,c,a);if(icon==Icon::EyeOff)Seg(r,g,4,3,20,21,c,w,a);break;
    case Icon::Menu:
        Seg(r, g, 5, 7, 19, 7, c, w, a);
        Seg(r, g, 5, 12, 19, 12, c, w, a);
        Seg(r, g, 5, 17, 19, 17, c, w, a);
        break;
    case Icon::Minimize:
        Seg(r, g, 6, 12, 18, 12, c, w, a);
        break;
    case Icon::Maximize:
        Rect(r,g,6,6,18,18,0,c,w,a); break;
    case Icon::Restore:
        Rect(r,g,8,5,19,16,0,c,w,a);
        r.Fill(Rc(g.X(5),g.Y(8),g.X(16),g.Y(19)),theme::Panel,a);
        Rect(r,g,5,8,16,19,0,c,w,a); break;
    case Icon::Close:
        Seg(r, g, 7, 7, 17, 17, c, w, a);
        Seg(r, g, 17, 7, 7, 17, c, w, a);
        break;
    case Icon::Ghost: {
        Seg(r,g,4,4,20,20,c,w,a); Seg(r,g,4,20,20,4,c,w,a);
        r.Disc(g.X(12),g.Y(10.5f),6.0f*g.u,c,a);
        r.FillRound(Rc(g.X(8),g.Y(12),g.X(16),g.Y(18)),1*g.u,c,a);
        r.Disc(g.X(9.5f),g.Y(11),1.7f*g.u,theme::Ground,a);
        r.Disc(g.X(14.5f),g.Y(11),1.7f*g.u,theme::Ground,a);
        Seg(r,g,10,15,10,17,theme::Ground,g.u,a); Seg(r,g,12,15,12,17,theme::Ground,g.u,a); Seg(r,g,14,15,14,17,theme::Ground,g.u,a);
        for(int i=0;i<3;++i) Seg(r,g,7.0f+i,6,10.0f+i,13,0xB62A3A,g.u,a);
        break;
    }
    case Icon::ChevronLeft:  Poly(r, g, { 14, 6, 8, 12, 14, 18 }, c, w, a); break;
    case Icon::ChevronRight: Poly(r, g, { 10, 6, 16, 12, 10, 18 }, c, w, a); break;
    case Icon::ChapterPrev:
        Seg(r, g, 6.5f, 6, 6.5f, 18, c, w, a);
        Poly(r, g, { 17, 6, 11, 12, 17, 18 }, c, w, a);
        break;
    case Icon::ChapterNext:
        Seg(r, g, 17.5f, 6, 17.5f, 18, c, w, a);
        Poly(r, g, { 7, 6, 13, 12, 7, 18 }, c, w, a);
        break;
    case Icon::FitWidth:
        Seg(r, g, 4, 7, 4, 17, c, w, a);
        Seg(r, g, 20, 7, 20, 17, c, w, a);
        Seg(r, g, 7.5f, 12, 16.5f, 12, c, w, a);
        Poly(r, g, { 10, 9.5f, 7.5f, 12, 10, 14.5f }, c, w, a);
        Poly(r, g, { 14, 9.5f, 16.5f, 12, 14, 14.5f }, c, w, a);
        break;
    case Icon::FitHeight:
        Seg(r, g, 7, 4, 17, 4, c, w, a);
        Seg(r, g, 7, 20, 17, 20, c, w, a);
        Seg(r, g, 12, 7.5f, 12, 16.5f, c, w, a);
        Poly(r, g, { 9.5f, 10, 12, 7.5f, 14.5f, 10 }, c, w, a);
        Poly(r, g, { 9.5f, 14, 12, 16.5f, 14.5f, 14 }, c, w, a);
        break;
    case Icon::FitWindow:
        Poly(r, g, { 5, 9, 5, 5, 9, 5 }, c, w, a);
        Poly(r, g, { 15, 5, 19, 5, 19, 9 }, c, w, a);
        Poly(r, g, { 19, 15, 19, 19, 15, 19 }, c, w, a);
        Poly(r, g, { 9, 19, 5, 19, 5, 15 }, c, w, a);
        break;
    case Icon::OneToOne:
        r.Text(L"1:1", box, c, 11.5f * g.u, Renderer::Align::Center, Renderer::Weight::Semibold, a, false, false);
        break;
    case Icon::DoublePage:
        Rect(r, g, 3.5f, 6, 11.3f, 18, 1.0f, c, w, a);
        Rect(r, g, 12.7f, 6, 20.5f, 18, 1.0f, c, w, a);
        break;
    case Icon::Strip:
        Rect(r, g, 7.5f, 3, 16.5f, 21, 1.0f, c, w, a);
        Seg(r, g, 7.5f, 9, 16.5f, 9, c, w, a);
        Seg(r, g, 7.5f, 15, 16.5f, 15, c, w, a);
        break;
    case Icon::Star: {
        D2D1_POINT_2F pts[10];
        for (int i = 0; i < 10; ++i) {
            const float rad = (i % 2 == 0) ? 8.2f : 3.6f;
            const float th = -kPi / 2.0f + kPi * (float)i / 5.0f;
            pts[i] = g.P(12.0f + rad * std::cos(th), 12.8f + rad * std::sin(th));
        }
        r.Polyline(pts, 10, c, w, a, true);
        break;
    }
    case Icon::Download:
        Seg(r, g, 12, 4, 12, 15, c, w, a);
        Poly(r, g, { 7.5f, 10.5f, 12, 15, 16.5f, 10.5f }, c, w, a);
        Seg(r, g, 5, 19.5f, 19, 19.5f, c, w, a);
        break;
    case Icon::Sliders:
        Seg(r, g, 4, 7, 20, 7, c, w, a);
        Seg(r, g, 4, 12, 20, 12, c, w, a);
        Seg(r, g, 4, 17, 20, 17, c, w, a);
        r.Disc(g.X(15), g.Y(7), 2.2f * g.u, c, a);
        r.Disc(g.X(9), g.Y(12), 2.2f * g.u, c, a);
        r.Disc(g.X(14), g.Y(17), 2.2f * g.u, c, a);
        break;
    case Icon::Bookshelf:
        Rect(r,g,4,5,8,19,0,c,w,a);Rect(r,g,9,5,13,19,0,c,w,a);
        Poly(r,g,{15,5,19,4,22,18,18,19},c,w,a,true);Seg(r,g,3,21,22,21,c,w,a);break;
    case Icon::Library:
        Rect(r, g, 4, 4, 11, 11, 1.5f, c, w, a);
        Rect(r, g, 13, 4, 20, 11, 1.5f, c, w, a);
        Rect(r, g, 4, 13, 11, 20, 1.5f, c, w, a);
        Rect(r, g, 13, 13, 20, 20, 1.5f, c, w, a);
        break;
    case Icon::Search:
        r.Ring(g.X(10.5f), g.Y(10.5f), 6.0f * g.u, c, w, a);
        Seg(r, g, 15, 15, 20, 20, c, w, a);
        break;
    case Icon::Folder:
        Poly(r, g, { 3.5f, 6, 9, 6, 11, 8.5f, 20.5f, 8.5f, 20.5f, 18.5f, 3.5f, 18.5f }, c, w, a, true);
        break;
    case Icon::Globe: {
        r.Ring(g.X(12), g.Y(12), 8.0f * g.u, c, w, a);
        D2D1_POINT_2F pts[25];
        for (int i = 0; i <= 24; ++i) {
            const float th = 2.0f * kPi * (float)i / 24.0f;
            pts[i] = g.P(12.0f + 3.6f * std::cos(th), 12.0f + 8.0f * std::sin(th));
        }
        r.Polyline(pts, 25, c, w, a, true);
        Seg(r, g, 4, 12, 20, 12, c, w, a);
        break;
    }
    case Icon::Info:
        r.Ring(g.X(12), g.Y(12), 8.0f * g.u, c, w, a);
        Seg(r, g, 12, 11, 12, 16.5f, c, w, a);
        r.Disc(g.X(12), g.Y(8), 1.1f * g.u, c, a);
        break;
    case Icon::Plus:
        Seg(r, g, 6, 12, 18, 12, c, w, a);
        Seg(r, g, 12, 6, 12, 18, c, w, a);
        break;
    case Icon::Minus:
        Seg(r, g, 6, 12, 18, 12, c, w, a);
        break;
    case Icon::List:
        Seg(r, g, 5, 7, 19, 7, c, w, a);
        Seg(r, g, 5, 12, 19, 12, c, w, a);
        Seg(r, g, 5, 17, 19, 17, c, w, a);
        break;
    }
}

HICON MakeAppIcon(int size) {
    return reinterpret_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101),IMAGE_ICON,size,size,LR_DEFAULTCOLOR));
}

} // namespace gm
