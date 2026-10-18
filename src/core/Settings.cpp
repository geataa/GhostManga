#include "core/Settings.h"
#include "core/FileIo.h"
#include "core/Log.h"
#include "core/Utf8.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <map>

namespace gm {
namespace {

std::wstring Trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && iswspace(s[a])) ++a;
    while (b > a && iswspace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

bool AsBool(const std::wstring& v, bool def) {
    if (v == L"1" || _wcsicmp(v.c_str(), L"true") == 0 || _wcsicmp(v.c_str(), L"evet") == 0) return true;
    if (v == L"0" || _wcsicmp(v.c_str(), L"false") == 0 || _wcsicmp(v.c_str(), L"hayir") == 0) return false;
    return def;
}

int AsInt(const std::wstring& v, int def) {
    if (v.empty()) return def;
    wchar_t* end = nullptr;
    const long n = wcstol(v.c_str(), &end, 10);
    return (end && *end == 0) ? (int)n : def;
}

float AsFloat(const std::wstring& v, float def) {
    if (v.empty()) return def;
    wchar_t* end = nullptr;
    const double d = wcstod(v.c_str(), &end);
    if (!end || *end != 0 || !std::isfinite(d)) return def; // "nan", "inf" kabul etme
    return (float)d;
}

} // namespace

bool Settings::Load(const std::wstring& path) {
    filePath = path;
    saveBlocked = false;
    std::string raw;
    const fileio::ReadResult rr = fileio::ReadText(path, 4 * 1024 * 1024, raw);
    if (rr != fileio::ReadResult::Ok) {
        if (rr != fileio::ReadResult::Missing) {
            saveBlocked = true;
            log::Write(L"Ayar dosyasi var ama okunamadi; bu oturumda uzerine yazilmayacak.");
        }
        return false;
    }

    std::map<std::wstring, std::wstring> kv;
    std::wstring section;
    const std::wstring text = FromUtf8(raw);
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find(L'\n', pos);
        if (nl == std::wstring::npos) nl = text.size();
        const std::wstring line = Trim(text.substr(pos, nl - pos));
        pos = nl + 1;
        if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
        if (line.front() == L'[' && line.back() == L']') {
            section = Trim(line.substr(1, line.size() - 2));
            continue;
        }
        const size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        kv[section + L"." + Trim(line.substr(0, eq))] = Trim(line.substr(eq + 1));
        if (nl == text.size()) break;
    }
    auto get = [&](const wchar_t* k) -> std::wstring {
        auto it = kv.find(k);
        return it == kv.end() ? std::wstring() : it->second;
    };

    const std::wstring l = get(L"gorunum.dil");
    lang = LanguageFromCode(l);

    closeToTray   = AsBool(get(L"arkaplan.kapatinca_tepsiye"), closeToTray);
    trayHintShown = AsBool(get(L"arkaplan.tepsi_ipucu_gosterildi"), trayHintShown);
    bgScanInterval = std::clamp(AsInt(get(L"arkaplan.tarama_araligi_dk"), bgScanInterval), 0, 1440);
    startOnLogin = AsBool(get(L"arkaplan.baslangicta_calistir"), startOnLogin);
    bgNotify      = AsBool(get(L"arkaplan.yeni_bolum_bildir"), bgNotify);

    downloadFormat = AsInt(get(L"indirme.bicim"), downloadFormat);
    downloadDir   = get(L"indirme.klasor");

    winValid = AsBool(get(L"pencere.gecerli"), false);
    winX = AsInt(get(L"pencere.x"), 0);
    winY = AsInt(get(L"pencere.y"), 0);
    winW = AsInt(get(L"pencere.genislik"), 0);
    winH = AsInt(get(L"pencere.yukseklik"), 0);
    winMax = AsBool(get(L"pencere.buyutulmus"), false);

    const int f = AsInt(get(L"okuyucu.boyutlandirma"), (int)fit);
    fit = (f >= 0 && f <= 3) ? (FitMode)f : FitMode::Width;
    webtoon     = AsBool(get(L"okuyucu.webtoon"), webtoon);
    doublePage  = AsBool(get(L"okuyucu.cift_sayfa"), doublePage);
    rtl         = AsBool(get(L"okuyucu.sagdan_sola"), rtl);
    scaleUp     = AsBool(get(L"okuyucu.yukari_olcekle"), scaleUp);
    magicScroll = AsBool(get(L"okuyucu.sihirli_kaydirma"), magicScroll);
    coverAlone  = AsBool(get(L"okuyucu.kapak_tek"), coverAlone);
    hudAutoHide = AsBool(get(L"okuyucu.cubuk_otomatik_gizle"), hudAutoHide);
    readerDim   = std::clamp(AsFloat(get(L"okuyucu.zemin_koyulugu"), readerDim), 0.5f, 1.0f);
    readerTheme = std::clamp(AsInt(get(L"okuyucu.tema"), readerTheme), 0, 3);

    recent.clear();
    for (int i = 0; i < 8; ++i) {
        wchar_t key[32];
        swprintf_s(key, L"kutuphane.son%d", i);
        const std::wstring v = get(key);
        if (!v.empty()) recent.push_back(v);
    }
    librarySort     = std::clamp(AsInt(get(L"kutuphane.siralama"), librarySort), 0, 3);
    librarySortDesc = AsBool(get(L"kutuphane.siralama_ters"), librarySortDesc);
    libraryGroup    = AsBool(get(L"kutuphane.gruplama"), libraryGroup);
    libraryViewMode = std::clamp(AsInt(get(L"kutuphane.gorunum_kipi"), libraryViewMode), 0, 1);
    siteLanguages.clear();hiddenSites.clear();
    const int languages=std::clamp(AsInt(get(L"siteler.dil_sayisi"),0),0,128);
    const int hidden=std::clamp(AsInt(get(L"siteler.gizli_sayisi"),0),0,4096);
    for(int i=0;i<languages;++i){const auto value=get((L"siteler.dil"+std::to_wstring(i)).c_str());if(!value.empty())siteLanguages.push_back(value);}
    for(int i=0;i<hidden;++i){const auto value=get((L"siteler.gizli"+std::to_wstring(i)).c_str());if(!value.empty())hiddenSites.push_back(value);}
    return true;
}

bool Settings::Save() const {
    if (filePath.empty() || saveBlocked) return false;

    std::wstring s;
    auto sec = [&](const wchar_t* name) { s += L"\r\n["; s += name; s += L"]\r\n"; };
    auto put = [&](const wchar_t* k, const std::wstring& v) { s += k; s += L" = "; s += v; s += L"\r\n"; };
    auto putb = [&](const wchar_t* k, bool v) { put(k, v ? L"1" : L"0"); };
    auto puti = [&](const wchar_t* k, int v) { put(k, std::to_wstring(v)); };

    s += L"; GhostManga ayarlari. Elle duzenlenebilir, UTF-8.\r\n";
    sec(L"gorunum");
    put(L"dil", LanguageCode(lang));

    sec(L"arkaplan");
    putb(L"kapatinca_tepsiye", closeToTray);
    putb(L"tepsi_ipucu_gosterildi", trayHintShown);
    puti(L"tarama_araligi_dk", bgScanInterval);
    putb(L"baslangicta_calistir", startOnLogin);
    putb(L"yeni_bolum_bildir", bgNotify);

    sec(L"indirme");
    puti(L"bicim", downloadFormat);
    put(L"klasor", downloadDir);

    sec(L"pencere");
    putb(L"gecerli", winValid);
    puti(L"x", winX);
    puti(L"y", winY);
    puti(L"genislik", winW);
    puti(L"yukseklik", winH);
    putb(L"buyutulmus", winMax);

    sec(L"okuyucu");
    puti(L"boyutlandirma", (int)fit);
    putb(L"webtoon", webtoon);
    putb(L"cift_sayfa", doublePage);
    putb(L"sagdan_sola", rtl);
    putb(L"yukari_olcekle", scaleUp);
    putb(L"sihirli_kaydirma", magicScroll);
    putb(L"kapak_tek", coverAlone);
    putb(L"cubuk_otomatik_gizle", hudAutoHide);
    wchar_t dim[16];
    swprintf_s(dim, L"%.2f", readerDim);
    put(L"zemin_koyulugu", dim);
    puti(L"tema", readerTheme);

    sec(L"kutuphane");
    puti(L"siralama", librarySort);
    putb(L"siralama_ters", librarySortDesc);
    putb(L"gruplama", libraryGroup);
    puti(L"gorunum_kipi", libraryViewMode);
    for (size_t i = 0; i < recent.size() && i < 8; ++i) {
        wchar_t key[32];
        swprintf_s(key, L"son%d", (int)i);
        put(key, recent[i]);
    }

    sec(L"siteler");puti(L"dil_sayisi",(int)siteLanguages.size());puti(L"gizli_sayisi",(int)hiddenSites.size());
    for(size_t i=0;i<siteLanguages.size();++i)put((L"dil"+std::to_wstring(i)).c_str(),siteLanguages[i]);
    for(size_t i=0;i<hiddenSites.size();++i)put((L"gizli"+std::to_wstring(i)).c_str(),hiddenSites[i]);
    return fileio::WriteAtomic(filePath, ToUtf8(s));
}

void Settings::PushRecent(const std::wstring& dir) {
    recent.erase(std::remove_if(recent.begin(), recent.end(),
                                [&](const std::wstring& r) { return _wcsicmp(r.c_str(), dir.c_str()) == 0; }),
                 recent.end());
    recent.insert(recent.begin(), dir);
    if (recent.size() > 8) recent.resize(8);
}

} // namespace gm
