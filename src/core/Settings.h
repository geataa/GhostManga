#pragma once
//
// Kullanici ayarlari: portable_data\settings.ini, UTF-8, elle duzenlenebilir.
// Kayit defterine hicbir sey yazilmaz (FR-SET-002). Dosya yoksa varsayilanlar,
// bozuksa okunabilen satirlar alinir, kalan varsayilan kalir (FR-SET-003).
//
// Okuyucu alanlarinin AMR karsiliklari yanlarinda yazili; iceri aktarimda
// ayni anlami tasimalari gerekiyor (05-AMR-VERI-FORMATLARI.md bolum 3).
//
#include <string>
#include <vector>

namespace gm {

enum class Lang { Auto = 0, Tr = 1, En = 2, De, Fr, Es, It, Pt, Ru, Pl, Nl, Ja, Ko, Zh, Id, Az, Count_ };
struct LanguageOption { Lang value; const wchar_t* code; const wchar_t* name; };
inline constexpr LanguageOption Languages[] = {
    {Lang::Auto,L"otomatik",L"Automatic"},{Lang::Tr,L"tr",L"Türkçe"},{Lang::En,L"en",L"English"},
    {Lang::De,L"de",L"Deutsch"},{Lang::Fr,L"fr",L"Français"},{Lang::Es,L"es",L"Español"},
    {Lang::It,L"it",L"Italiano"},{Lang::Pt,L"pt",L"Português"},{Lang::Ru,L"ru",L"Русский"},
    {Lang::Pl,L"pl",L"Polski"},{Lang::Nl,L"nl",L"Nederlands"},{Lang::Ja,L"ja",L"日本語"},
    {Lang::Ko,L"ko",L"한국어"},{Lang::Zh,L"zh",L"简体中文"},{Lang::Id,L"id",L"Bahasa Indonesia"},{Lang::Az,L"az",L"Azərbaycan dili"}
};
inline Lang LanguageFromCode(const std::wstring& code) { for(const auto& option:Languages)if(code==option.code)return option.value;return Lang::Auto; }
inline const wchar_t* LanguageCode(Lang value) {for(const auto& option:Languages)if(value==option.value)return option.code;return L"otomatik";}

// AMR resizeMode: 0 genislik, 1 yukseklik, 2 kapsayici, 3 boyutlandirma yok
enum class FitMode { Width = 0, Height = 1, Window = 2, Original = 3 };

struct Settings {
    // Gorunum
    Lang lang = Lang::Auto;

    // Arka plan
    bool startOnLogin = false;
    bool closeToTray = true;
    bool trayHintShown = false;
    int  bgScanInterval = 30;         // 0: kapali, 15..1440 dk (en fazla bir gun)
    bool bgNotify = true;             // yeni bolumlerde bildirim goster

    // Indirme
    int  downloadFormat = 0;          // 0: CBZ arsivi, 1: ham klasor
    std::wstring downloadDir;         // bos ise data_dir/downloads

    // Pencere
    bool winValid = false;
    int  winX = 0, winY = 0, winW = 0, winH = 0;
    bool winMax = false;

    // Okuyucu
    FitMode fit = FitMode::Width;   // AMR resizeMode
    bool webtoon = false;           // AMR webtoonDefault
    bool doublePage = false;        // AMR displayBook
    bool rtl = false;               // AMR readingDirection (1 = sagdan sola)
    bool scaleUp = false;           // AMR scaleUp
    bool magicScroll = true;        // AMR magicScrollEnabled
    bool coverAlone = true;         // cift sayfada kapak tek basina (FR-RDR-023)
    bool hudAutoHide = true;        // AMR bottomNavigationEnabled'in tersi degil: cubuk solsun mu
    float readerDim = 0.78f;        // GhostView karartmasi, 0.50 - 1.00
    int   readerTheme = 0;          // 0: Koyu Gri, 1: OLED Siyah, 2: Sepya, 3: Acik Gri

    // Kutuphane
    std::vector<std::wstring> recent; // son acilan yerel klasorler, en fazla 8
    int  librarySort = 0;             // 0: guncelleme, 1: son okunan, 2: isim, 3: eklenme
    bool librarySortDesc = true;      // true: azalan (yeni once, Z->A)
    bool libraryGroup = true;         // isme gore grupla (M2.5)
    int  libraryViewMode = 1;         // 0: Liste, 1: Kart / Izgara
    std::vector<std::wstring> siteLanguages; // empty: all languages
    std::vector<std::wstring> hiddenSites;

    bool Load(const std::wstring& path);
    bool Save() const;
    void PushRecent(const std::wstring& dir);

    std::wstring filePath;
    // Dosya var ama okunamadi (kilitli, izin yok): bu oturumda uzerine yazma,
    // yoksa kullanicinin ayarlari varsayilanlarla ezilir.
    bool saveBlocked = false;
};

} // namespace gm
