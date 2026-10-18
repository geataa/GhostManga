#pragma once
//
// Ortak kontroller. Hepsi GhostView etkilesim kuralini izler:
// bosta dolgu yok, uzerine gelince beyaz %22, basinca vurgu %45,
// etkin durumda vurgu %45 dolgu + camgobegi kenar.
//
#include "ui/Icons.h"
#include "ui/Ui.h"

#include <string>
#include <vector>

namespace gm::w {

// Kapsul ikon dugmesi (HUD ve baslik seridi). Tiklandiysa true.
bool IconButton(Renderer& r, Ui& ui, uint32_t id, const D2D1_RECT_F& rc, Icon icon, float iconPx,
                const std::wstring& tip, bool active = false, bool enabled = true, float alpha = 1.0f,
                float radius = -1.0f);

// Metinli kapsul dugme. primary: vurgu dolgulu.
bool TextButton(Renderer& r, Ui& ui, uint32_t id, const D2D1_RECT_F& rc, const std::wstring& text,
                bool primary, bool enabled = true, float alpha = 1.0f);
bool ButtonUsesIcon(Renderer& r,const D2D1_RECT_F& rc,const std::wstring& text);

// Satir boyu anahtar: solda etiket (ve istege bagli aciklama), sagda anahtar.
// Deger degistiyse true.
bool Toggle(Renderer& r, Ui& ui, uint32_t id, const D2D1_RECT_F& row, const std::wstring& label,
            bool& value, const std::wstring& hint = std::wstring());

// Parcali secici. Yeni secim veya -1.
int Segmented(Renderer& r, Ui& ui, uint32_t id, const D2D1_RECT_F& rc,
              const std::vector<std::wstring>& labels, int selected);

// Metin giris kutusu (arama, adlandirma, filtreleme icin).
class TextInput {
public:
    std::wstring text;
    size_t cursor = 0;
    bool focused = false;

    bool Draw(Renderer& r, Ui& ui, uint32_t id, const D2D1_RECT_F& rc, const std::wstring& placeholder,
              bool showSearchIcon = false, double now = 0.0);
    bool OnChar(wchar_t c);
    bool OnKey(UINT vk, bool ctrl, bool shift = false);
    void SetText(const std::wstring& value);
    void Clear() { SetText(L""); }
    void SelectAll();
    bool HasSelection() const;
    std::wstring SelectedText() const;
    void Paste(const std::wstring& clipText);
private:
    friend struct TextInputTestAccess;
    size_t m_anchor = std::wstring::npos;
    float m_scrollX = 0.0f;
    bool m_selecting = false;
    HWND m_owner = nullptr;
    void NormalizeSelection();
    bool EraseSelection();
    void MoveCursor(size_t position, bool extend);
    bool HandleKey(UINT vk, bool ctrl, bool shift,
                   bool (*read)(HWND, std::wstring&), bool (*write)(HWND, const std::wstring&));
};

float Dp(float v); // aktif olcekle carpar (Begin'de ayarlanir)
void SetScale(float s);

} // namespace gm::w
