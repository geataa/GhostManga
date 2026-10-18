#pragma once
//
// Elle cizilen vektor ikonlar. Ikon fontu yok (04-UI-SPEC.md bolum 2.1):
// her ikon 24 birimlik bir izgarada tasarlanip kutuya olceklenir, boylece
// her DPI'da keskin kalir ve Windows surumune bagli bir font gerekmez.
//
#include "ui/Renderer.h"

namespace gm {

enum class Icon {
    Menu, Minimize, Maximize, Restore, Ghost, Close,
    ChevronLeft, ChevronRight, ChapterPrev, ChapterNext,
    FitWidth, FitHeight, FitWindow, OneToOne,
    DoublePage, Strip, Star, Download, Sliders,
    Bookshelf, Library, Search, Folder, Globe, Info, Plus, Minus, List,
    Trash, Read, Check, Refresh, Eye, EyeOff,
};

// sizePx: ikonun kapladigi kare. Kutunun ortasina yerlesir.
void DrawIcon(Renderer& r, Icon icon, const D2D1_RECT_F& box, float sizePx,
              uint32_t color, float alpha = 1.0f);

// Tepsi ve pencere simgesi: vurgu renginde yuvarlak kare icinde hayalet.
// Calisma aninda uretilir, .ico kaynagi gerekmez.
HICON MakeAppIcon(int sizePx);

} // namespace gm
