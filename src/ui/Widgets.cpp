#include "ui/Widgets.h"
#include "ui/Theme.h"
#include "core/I18n.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace gm::w {
namespace {
float g_scale = 1.0f;
}

void SetScale(float s) { g_scale = s; }
float Dp(float v) { return v * g_scale; }

bool IconButton(Renderer& r, Ui& ui, uint32_t id, const D2D1_RECT_F& rc, Icon icon, float iconPx,
                const std::wstring& tip, bool active, bool enabled, float alpha, float radius) {
    const bool clicked = ui.Button(id, rc, enabled);
    const float rad = radius >= 0.0f ? radius : std::min(RcW(rc), RcH(rc)) * 0.5f;
    if (active) {
        r.FillRound(rc, rad, theme::Active, theme::ActiveAlpha * alpha);
        r.StrokeRound(rc, rad, theme::ActiveEdge, Dp(1.0f), theme::ActiveEdgeAlpha * alpha);
    }
    if (enabled && ui.Pressed(id)) {
        r.FillRound(rc, rad, theme::Press, theme::PressAlpha * alpha);
    } else if (enabled && ui.Hot(id)) {
        r.FillRound(rc, rad, theme::White, theme::HoverAlpha * alpha);
    }
    const uint32_t col = enabled ? theme::Text : theme::TextMuted;
    DrawIcon(r, icon, rc, iconPx, col, (enabled ? 1.0f : 0.55f) * alpha);
    ui.Tip(id, tip, rc);
    return clicked;
}

bool ButtonUsesIcon(Renderer& r,const D2D1_RECT_F& rc,const std::wstring& text) {
    return r.MeasureText(text,Dp(13.5f),Renderer::Weight::Semibold)>RcW(rc)-Dp(24);
}
static Icon ActionIcon(const std::wstring& text) {
    for(const auto& language:Languages)if(text==language.name)return Icon::Globe;
    auto label=i18n::English(text);std::transform(label.begin(),label.end(),label.begin(),towlower);
    if(label.find(L"remove")!=label.npos || label.find(L"delete")!=label.npos || label.find(L"purge")!=label.npos)return Icon::Trash;
    if(label.find(L"cancel")!=label.npos || label.find(L"stop")!=label.npos || label.find(L"close")!=label.npos)return Icon::Close;
    if(label.find(L"search")!=label.npos || label.find(L"find")!=label.npos)return Icon::Search;
    if(label.find(L"reset")!=label.npos || label.find(L"retry")!=label.npos || label.find(L"check")!=label.npos)return Icon::Refresh;
    if(label.find(L"mark")!=label.npos || label.find(L"read to")!=label.npos)return Icon::Check;
    if(label.find(L"chapter")!=label.npos && label.find(L"read")==label.npos)return Icon::List;
    if(label.find(L"read")!=label.npos || label.find(L"resume")!=label.npos)return Icon::Read;
    if(label.find(L"download")!=label.npos || label.find(L"export")!=label.npos)return Icon::Download;
    if(label.find(L"add")!=label.npos || label.find(L"import")!=label.npos || label.find(L"library")!=label.npos)return Icon::Plus;
    if(label.find(L"hide")!=label.npos)return Icon::EyeOff;
    if(label.find(L"show")!=label.npos)return Icon::Eye;
    if(label.find(L"site")!=label.npos)return Icon::Globe;
    if(label.find(L"folder")!=label.npos || label.find(L"open")!=label.npos)return Icon::Folder;
    if(label.find(L"back")!=label.npos)return Icon::ChevronLeft;
    return Icon::Info;
}
bool TextButton(Renderer& r, Ui& ui, uint32_t id, const D2D1_RECT_F& rc, const std::wstring& text,
                bool primary, bool enabled, float alpha) {
    const bool clicked = ui.Button(id, rc, enabled);
    const float rad = RcH(rc) * 0.5f;
    if (primary) {
        r.FillRound(rc, rad, theme::Accent, (enabled ? 1.0f : 0.4f) * alpha);
        if (enabled && ui.Pressed(id)) r.FillRound(rc, rad, theme::Black, 0.18f * alpha);
        else if (enabled && ui.Hot(id)) r.FillRound(rc, rad, theme::White, 0.14f * alpha);
    } else {
        r.FillRound(rc, rad, theme::Elevated, alpha);
        if (enabled && ui.Pressed(id)) r.FillRound(rc, rad, theme::Press, theme::PressAlpha * alpha);
        else if (enabled && ui.Hot(id)) r.FillRound(rc, rad, theme::White, 0.10f * alpha);
    }
    const auto& label=text;const auto color=primary?theme::White:theme::Text;
    if(ButtonUsesIcon(r,rc,label))DrawIcon(r,ActionIcon(text),rc,std::min(Dp(18),RcH(rc)-Dp(10)),color,(enabled?1.0f:0.5f)*alpha);
    else r.Text(label,RcInset(rc,Dp(12),0),color,Dp(13.5f),Renderer::Align::Center,Renderer::Weight::Semibold,(enabled?1.0f:0.5f)*alpha);
    ui.Tip(id,label,rc);
    return clicked;
}

bool Toggle(Renderer& r, Ui& ui, uint32_t id, const D2D1_RECT_F& row, const std::wstring& label,
            bool& value, const std::wstring& hint) {
    const bool clicked = ui.Button(id, row, true);
    if (ui.Hot(id)) r.FillRound(row, Dp(8), theme::White, 0.05f);

    const float trackW = Dp(40), trackH = Dp(22);
    const float tx = row.right - Dp(12) - trackW;
    const float ty = hint.empty() ? (row.top + row.bottom - trackH) * 0.5f : row.top + Dp(10);
    const D2D1_RECT_F track = RcWH(tx, ty, trackW, trackH);

    if (clicked) value = !value;
    r.FillRound(track, trackH * 0.5f, value ? theme::Accent : theme::Elevated, 1.0f);
    if (!value) r.StrokeRound(track, trackH * 0.5f, theme::Border, Dp(1), theme::BorderAlpha);
    const float knobR = Dp(8);
    const float kx = value ? track.right - Dp(11) : track.left + Dp(11);
    r.Disc(kx, (track.top + track.bottom) * 0.5f, knobR, value ? theme::White : theme::TextDim, 1.0f);

    const float textRight = tx - Dp(16);
    if (hint.empty()) {
        r.Text(label, Rc(row.left + Dp(12), row.top, textRight, row.bottom), theme::Text, Dp(14));
    } else {
        r.Text(label, Rc(row.left + Dp(12), row.top + Dp(6), textRight, row.top + Dp(30)), theme::Text, Dp(14));
        r.Text(hint, Rc(row.left + Dp(12), row.top + Dp(30), textRight, row.bottom - Dp(4)),
               theme::TextMuted, Dp(12.5f), Renderer::Align::Left, Renderer::Weight::Normal, 1.0f, true);
    }
    return clicked;
}

int Segmented(Renderer& r, Ui& ui, uint32_t id, const D2D1_RECT_F& rc,
              const std::vector<std::wstring>& labels, int selected) {
    if (labels.empty()) return -1;
    const float rad = RcH(rc) * 0.5f;
    r.FillRound(rc, rad, theme::Panel2, 1.0f);
    r.StrokeRound(rc, rad, theme::Border, Dp(1), theme::BorderAlpha);
    const float segW = RcW(rc) / (float)labels.size();
    int result = -1;
    for (size_t i = 0; i < labels.size(); ++i) {
        const D2D1_RECT_F s = RcInset(RcWH(rc.left + segW * i, rc.top, segW, RcH(rc)), Dp(3), Dp(3));
        const uint32_t sid = id ^ (uint32_t)((i + 1) * 0x9E3779B9u);
        if (ui.Button(sid, s)) result = (int)i;
        const bool on = (int)i == selected;
        if (on) {
            r.FillRound(s, RcH(s) * 0.5f, theme::Active, theme::ActiveAlpha);
            r.StrokeRound(s, RcH(s) * 0.5f, theme::ActiveEdge, Dp(1), theme::ActiveEdgeAlpha);
        } else if (ui.Pressed(sid)) {
            r.FillRound(s, RcH(s) * 0.5f, theme::Press, theme::PressAlpha);
        } else if (ui.Hot(sid)) {
            r.FillRound(s, RcH(s) * 0.5f, theme::White, 0.12f);
        }
        const auto& label=labels[i];const auto weight=on?Renderer::Weight::Semibold:Renderer::Weight::Normal;
        const float width=r.MeasureText(label,Dp(13),weight);
        const float size=width>RcW(s)-Dp(12)?std::max(Dp(10),Dp(13)*(RcW(s)-Dp(12))/std::max(width,1.0f)):Dp(13);
        r.Text(label,RcInset(s,Dp(6),0),on?theme::White:theme::TextDim,size,Renderer::Align::Center,weight);
        ui.Tip(sid,label,s);
    }
    return result;
}

namespace {
size_t PreviousCharacter(const std::wstring& text, size_t position) {
    if (!position) return 0;
    --position;
    if (position && text[position] >= 0xdc00 && text[position] <= 0xdfff &&
        text[position-1] >= 0xd800 && text[position-1] <= 0xdbff) --position;
    return position;
}
size_t NextCharacter(const std::wstring& text, size_t position) {
    if (position >= text.size()) return text.size();
    if (text[position] >= 0xd800 && text[position] <= 0xdbff && position+1 < text.size() &&
        text[position+1] >= 0xdc00 && text[position+1] <= 0xdfff) ++position;
    return position + 1;
}
bool ReadClipboardText(HWND owner, std::wstring& value) {
    if (!OpenClipboard(owner)) return false;
    bool ok = false;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        const size_t count = GlobalSize(data) / sizeof(wchar_t);
        if (count && count <= 8 * 1024 * 1024) {
            if (const auto* text = static_cast<const wchar_t*>(GlobalLock(data))) {
                const auto* end = std::find(text, text + count, L'\0');
                if (end != text + count) { value.assign(text, end); ok = true; }
                GlobalUnlock(data);
            }
        }
    }
    CloseClipboard();
    return ok;
}
bool WriteClipboardText(HWND owner, const std::wstring& value) {
    // EmptyClipboard requires a real owner for the following SetClipboardData.
    if (!IsWindow(owner)) owner = GetActiveWindow();
    if (!IsWindow(owner)) return false;
    HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, (value.size() + 1) * sizeof(wchar_t));
    if (!data) return false;
    auto* buffer = static_cast<wchar_t*>(GlobalLock(data));
    if (!buffer) { GlobalFree(data); return false; }
    std::copy(value.c_str(), value.c_str() + value.size() + 1, buffer);
    GlobalUnlock(data);
    bool ok = false;
    if (OpenClipboard(owner)) {
        if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, data)) ok = true;
        CloseClipboard();
    }
    if (!ok) GlobalFree(data); // Windows owns the allocation only after success.
    return ok;
}
}

void TextInput::SetText(const std::wstring& value) {
    text = value; cursor = text.size(); m_anchor = std::wstring::npos;
    m_scrollX = 0; m_selecting = false;
}
void TextInput::NormalizeSelection() {
    cursor = std::min(cursor, text.size());
    if (m_anchor != std::wstring::npos) m_anchor = std::min(m_anchor, text.size());
}
bool TextInput::HasSelection() const {
    return m_anchor != std::wstring::npos && std::min(m_anchor, text.size()) != std::min(cursor, text.size());
}
std::wstring TextInput::SelectedText() const {
    if (!HasSelection()) return {};
    const size_t first = std::min(m_anchor, cursor), last = std::min(std::max(m_anchor, cursor), text.size());
    return text.substr(first, last - first);
}
void TextInput::SelectAll() { m_anchor = 0; cursor = text.size(); }
bool TextInput::EraseSelection() {
    NormalizeSelection();
    if (!HasSelection()) return false;
    const size_t first = std::min(m_anchor, cursor), last = std::max(m_anchor, cursor);
    text.erase(first, last-first); cursor = first; m_anchor = std::wstring::npos;
    return true;
}
void TextInput::MoveCursor(size_t position, bool extend) {
    NormalizeSelection();
    if (extend) { if (m_anchor == std::wstring::npos) m_anchor = cursor; }
    else m_anchor = std::wstring::npos;
    cursor = std::min(position, text.size());
}

bool TextInput::Draw(Renderer& r, Ui& ui, uint32_t id, const D2D1_RECT_F& rc, const std::wstring& placeholder,
                     bool showSearchIcon, double now) {
    m_owner = r.Window();
    NormalizeSelection();
    bool changed = false;
    const bool over = ui.Over(rc);
    if (ui.In().pressed) { focused = over; if (!over) m_selecting = false; }
    const float rad = RcH(rc) * 0.5f;
    r.FillRound(rc, rad, theme::Panel2, 1.0f);
    r.StrokeRound(rc, rad, focused ? theme::ActiveEdge : theme::Border,
                  Dp(focused ? 1.5f : 1.0f), focused ? 0.9f : over ? 0.5f : theme::BorderAlpha);
    float textLeft = rc.left + Dp(14), textRight = rc.right - Dp(14);
    if (showSearchIcon) {
        const auto iconRc = RcWH(rc.left + Dp(10), rc.top + (RcH(rc)-Dp(16))*0.5f, Dp(16), Dp(16));
        DrawIcon(r, Icon::Search, iconRc, Dp(15), focused ? theme::Accent : theme::TextMuted, 1.0f);
        textLeft += Dp(20);
    }
    bool overClear = false;
    if (!text.empty()) {
        const float btnW = Dp(20);
        const auto xBtn = RcWH(rc.right-btnW-Dp(10), rc.top+(RcH(rc)-btnW)*0.5f, btnW, btnW);
        const uint32_t xId = id ^ 0x55AA55AAu;
        overClear = RcHit(xBtn, ui.In().mx, ui.In().my);
        if (ui.Button(xId, xBtn)) { Clear(); changed = true; }
        if (ui.Hot(xId)) r.FillRound(xBtn, btnW*0.5f, theme::White, 0.15f);
        r.Text(L"×", xBtn, theme::TextMuted, Dp(15), Renderer::Align::Center, Renderer::Weight::Bold);
        textRight = xBtn.left-Dp(6);
    }
    const float width = std::max(Dp(1), textRight-textLeft);
    const auto textRc = Rc(textLeft, rc.top+Dp(2), textRight, rc.bottom-Dp(2));
    const auto advance = [&](size_t position) { return r.MeasureText(text.substr(0, position), Dp(13)); };
    const float fullWidth = r.MeasureText(text, Dp(13));
    const auto hit = [&] {
        const float x = ui.In().mx-textLeft+m_scrollX;
        size_t lo=0, hi=text.size();
        while (lo<hi) {const size_t mid=lo+(hi-lo)/2; if (advance(mid)<x) lo=mid+1; else hi=mid;}
        if (lo && x < (advance(lo-1)+advance(lo))*0.5f) --lo;
        if (lo && lo<text.size() && text[lo]>=0xdc00 && text[lo]<=0xdfff && text[lo-1]>=0xd800 && text[lo-1]<=0xdbff) --lo;
        return lo;
    };
    if (ui.In().pressed && over && !overClear) {
        MoveCursor(hit(), (GetKeyState(VK_SHIFT)&0x8000)!=0);
        if (m_anchor==std::wstring::npos) m_anchor=cursor;
        m_selecting=true;
    } else if (m_selecting && (ui.In().down || ui.In().released)) {
        if (ui.In().mx<textLeft) m_scrollX=std::max(0.0f,m_scrollX-Dp(12));
        if (ui.In().mx>textRight) m_scrollX=std::min(fullWidth,m_scrollX+Dp(12));
        MoveCursor(hit(),true);
    }
    if (ui.In().released || !focused) m_selecting=false;
    const float caret = advance(cursor);
    if (focused) {
        if (caret<m_scrollX) m_scrollX=caret;
        if (caret>m_scrollX+width-Dp(2)) m_scrollX=caret-width+Dp(2);
    }
    m_scrollX=std::clamp(m_scrollX,0.0f,std::max(0.0f,fullWidth-width+Dp(2)));
    const float origin=textLeft-m_scrollX;
    r.PushClip(textRc);
    if (focused && HasSelection()) {
        const float start=advance(std::min(cursor,m_anchor)), end=advance(std::max(cursor,m_anchor));
        r.Fill(Rc(origin+start,rc.top+Dp(7),origin+end,rc.bottom-Dp(7)),theme::Accent,0.3f);
    }
    if (text.empty()) r.Text(placeholder,textRc,theme::TextMuted,Dp(13));
    else r.Text(text,Rc(origin,textRc.top,origin+std::max(fullWidth+Dp(2),width),textRc.bottom),
                theme::Text,Dp(13),Renderer::Align::Left,Renderer::Weight::Normal,1.0f,false,false);
    if (focused && (now==0.0 || std::fmod(now,1.0)<0.55)) {
        r.Fill(RcWH(origin+caret,rc.top+(RcH(rc)-Dp(16))*0.5f,Dp(1.5f),Dp(16)),theme::Accent,1.0f);
    }
    r.PopClip();
    return changed;
}

bool TextInput::OnChar(wchar_t c) {
    if (!focused) return false;
    if (c>=32 && c!=127) { NormalizeSelection(); EraseSelection(); text.insert(cursor,1,c); ++cursor; m_anchor=std::wstring::npos; }
    return true; // Ctrl shortcut WM_CHAR messages must not become text or bubble.
}

bool TextInput::OnKey(UINT vk, bool ctrl, bool shift) {
    return HandleKey(vk,ctrl,shift,ReadClipboardText,WriteClipboardText);
}
bool TextInput::HandleKey(UINT vk, bool ctrl, bool shift,
                          bool (*read)(HWND,std::wstring&), bool (*write)(HWND,const std::wstring&)) {
    if (!focused) return false;
    NormalizeSelection();
    if (ctrl && vk=='A') { SelectAll(); return true; }
    if ((ctrl && (vk=='C' || vk=='X' || vk==VK_INSERT)) || (shift && vk==VK_DELETE)) {
        if (HasSelection() && write(m_owner,SelectedText()) && (vk=='X' || vk==VK_DELETE)) EraseSelection();
        return true;
    }
    if ((ctrl && vk=='V') || (shift && vk==VK_INSERT)) {
        std::wstring value;
        if (read(m_owner,value)) Paste(value);
        return true;
    }
    const auto wordLeft = [&] {
        size_t pos=cursor;
        while (pos && iswspace(text[PreviousCharacter(text,pos)])) pos=PreviousCharacter(text,pos);
        if (!pos) return pos;
        const bool word=iswalnum(text[PreviousCharacter(text,pos)])!=0;
        while (pos && !iswspace(text[PreviousCharacter(text,pos)]) && (iswalnum(text[PreviousCharacter(text,pos)])!=0)==word) pos=PreviousCharacter(text,pos);
        return pos;
    };
    const auto wordRight = [&] {
        size_t pos=cursor;
        if (pos<text.size()) {
            const bool word=iswalnum(text[pos])!=0;
            while (pos<text.size() && !iswspace(text[pos]) && (iswalnum(text[pos])!=0)==word) pos=NextCharacter(text,pos);
        }
        while (pos<text.size() && iswspace(text[pos])) pos=NextCharacter(text,pos);
        return pos;
    };
    switch (vk) {
    case VK_BACK:
        if (!EraseSelection() && cursor) {const size_t first=ctrl?wordLeft():PreviousCharacter(text,cursor);text.erase(first,cursor-first);cursor=first;}
        m_anchor=std::wstring::npos;return true;
    case VK_DELETE:
        if (!EraseSelection() && cursor<text.size()) text.erase(cursor,(ctrl?wordRight():NextCharacter(text,cursor))-cursor);
        m_anchor=std::wstring::npos;return true;
    case VK_LEFT:
        MoveCursor(!shift && HasSelection()?std::min(cursor,m_anchor):ctrl?wordLeft():PreviousCharacter(text,cursor),shift);return true;
    case VK_RIGHT:
        MoveCursor(!shift && HasSelection()?std::max(cursor,m_anchor):ctrl?wordRight():NextCharacter(text,cursor),shift);return true;
    case VK_HOME: MoveCursor(0,shift);return true;
    case VK_END: MoveCursor(text.size(),shift);return true;
    case VK_ESCAPE: focused=false;m_selecting=false;return true;
    case VK_UP: case VK_DOWN: case VK_PRIOR: case VK_NEXT: return true;
    default:
        // Printable keydowns belong to WM_CHAR, never to reader/app hotkeys.
        return !ctrl && (vk==VK_SPACE || (vk>='0' && vk<='Z') || (vk>=VK_NUMPAD0 && vk<=VK_DIVIDE) || (vk>=VK_OEM_1 && vk<=VK_OEM_102));
    }
}

void TextInput::Paste(const std::wstring& clipText) {
    std::wstring clean;
    clean.reserve(clipText.size());
    for (wchar_t c : clipText) if (c>=32 && c!=127) clean+=c;
    if (clean.empty()) return;
    NormalizeSelection(); EraseSelection();
    text.insert(cursor,clean); cursor+=clean.size(); m_anchor=std::wstring::npos;
}

} // namespace gm::w
