#pragma once
//
// Okuma listesi (E1). Kaynak AMR yedek dosyasi (AmrImport); seriler
// portable_data\library.json'da tutulur. Alan adlari AMR'nin uzun adlari,
// dosya elle okunabilir.
//
#include <cstdint>
#include <string>
#include <vector>
#include "engine/ChapterState.h"

namespace gm {

struct Series {
    std::string key;             // AMR anahtari: safename(ayna)/adres-yolu[_dil]
    std::string mirror;          // ayna adi, ham ("MangaDex V5")
    std::string name;
    std::string displayName;     // kullanicinin verdigi ad, bos olabilir
    std::string url;             // seri sayfasi
    std::string lastChapterUrl;  // kullanicinin son okudugu bolum adresi
    std::string lastChapterName; // kullanicinin son okudugu bolum adi
    std::string latestChapterUrl;  // sitede yayinlanan en son bolum adresi
    std::string latestChapterName; // sitede yayinlanan en son bolum adi
    std::vector<Chapter> chapters; // newest first, persistent source catalogue
    int chapterCatalogueVersion = 0; // v1 MangaDex catalogues come from a language-filtered API feed
    int64_t lastCheckedTs = 0;
    std::string lastUpdateError;
    std::string language;
    std::vector<std::string> cats;
    int read = 0;       // AMR ters mantik: 0 takip ediliyor, 1 takip birakildi
    int update = 1;     // 1 yeni bolum aranir, 0 aranmaz
    int display = 0;
    int layout = 0;     // paketli okuma ayari (05-AMR-VERI-FORMATLARI bolum 3)
    int webtoon = 0;    // 0: sayfa, 1: webtoon
    int fitMode = 0;    // 0: Width, 1: Height, 2: Window, 3: Original
    int doublePage = 0; // 0: tek sayfa, 1: cift sayfa
    int rtl = 0;        // 0: LTR, 1: RTL
    bool hasCustomReaderSettings = false; // bu manga icin ozel ayar kaydedildi mi
    double zoom = 0.0;
    int64_t upts = 0;   // ms, sitede son yeni bolum bulunma zamani
    int64_t lastReadTs = 0; // ms, son okuma zamani (M2.5)
    int64_t addedTs = 0;    // ms, kutuphaneye eklenme tarihi (M2.5)
    std::string groupOverride; // elle belirlenen grup adi (M2.5)

    // Yalniz ice aktarma sirasinda anlamli, kaydedilmez. Library bunlari
    // saklarken hep true yapar.
    //  hasState: kayit okuma durumu tasiyor mu. AMR "paylasim icin" disa
    //            aktarmada yalniz m,n,u,g yazar; eksik alanlar "sifirla"
    //            demek degildir, mevcut ilerleme korunmali.
    //  hasDisplayName: V1 bicimi kullanici adini hic tasimaz; eksikligi
    //            "adi sil" demek degildir.
    bool hasState = true;
    bool hasDisplayName = true;

    bool operator==(const Series&) const = default;
};

inline bool IsCaughtUp(const Series& series) {
    const std::string& latest = !series.latestChapterUrl.empty() ? series.latestChapterUrl :
        (!series.chapters.empty() ? series.chapters.front().url : series.latestChapterUrl);
    return !series.lastChapterUrl.empty() && !latest.empty() &&
        ChapterUrlIdentity(series.lastChapterUrl) == ChapterUrlIdentity(latest);
}

struct Bookmark {
    std::string mirror, name, url, chapUrl, chapName, note, type, scanUrl, scanName;

    bool operator==(const Bookmark&) const = default;
};

// Kopyalanabilir: ice aktarma once kopyada birlestirip kaydeder, yalniz
// kayit basarirsa asil listeyi degistirir (bellek ile disk ayrismasin).
class Library {
public:
    enum class LoadNote {
        None,
        RestoredFromBackup,  // dosya bozuktu, kenara alindi, .bak'tan donuldu
        ResetCorrupt,        // dosya da yedek de bozuk: bos basladi
        Blocked,             // okunamadi (kilitli, cok buyuk): bu oturumda yazilmaz
    };

    // Dosya yoksa bos baslar. Bozuksa <yol>.bozuk-<tarih> olarak kenara
    // tasinir ve .bak denenir.
    void Load(const std::wstring& path);
    bool Save();

    struct MergeResult {
        int added = 0, updated = 0, unchanged = 0;
        int bookmarksAdded = 0;
    };
    // Ayni anahtarli seri varsa dosyadaki durum kazanir, kategoriler birlesir.
    // Durum tasimayan kayit (paylasim kipi) mevcut ilerlemeye dokunmaz.
    MergeResult Merge(std::vector<Series>&& series, std::vector<Bookmark>&& bookmarks);

    // Seriyi son okunan olarak isaretler, zamani guncelleyip kaydeder.
    bool MarkLastRead(const std::string& key, const std::string& readChapUrl = "", const std::string& readChapName = "");

    // Serinin guncelleme zamanini ve sitedeki en yeni bolumunu kaydeder (kullanicinin okuma durumuna DOKUNMAZ).
    bool MarkUpdated(const std::string& key, int64_t upts, const std::string& latestChapUrl = "", const std::string& latestChapName = "");
    bool StoreUpdateError(const std::string& key, const std::string& error);
    bool StoreChapters(const std::string& key, std::vector<Chapter> chapters, int64_t checkedTs = 0);

    // Manga bazli okuyucu ayarlarini kaydeder (webtoon, fitMode, doublePage, rtl, zoom)
    bool UpdateReadingSettings(const std::string& key, bool webtoon, int fitMode, bool doublePage, bool rtl, double zoom = 0.0);

    // Anahtara veya URL'ye gore seri bulur
    const Series* FindSeries(const std::string& key) const;
    const Series* FindSource(const std::string& mirror, const std::string& url) const;

    // Yeni seri ekleme, silme ve kaydetme
    bool AddSeries(const Series& s);
    bool RemoveSeries(const std::string& key);

    // Okuma ilerlemesini sifirlar (okunmadi yapar)
    bool ResetReadingProgress(const std::string& key);

    // Son yayinlanan bolume kadar okundu olarak isaretler
    bool MarkReadUpToLatest(const std::string& key);

    // Tum kutuphaneyi sifirlar (cift onay ile cagirilir)
    bool ClearAll();

    // Kutuphaneyi temizler, linkleri duzeltir, yinelenenleri birlestirir ve optimize eder.
    bool ConvertAndOptimize(int& fixedCount);

    // Yer imi ekleme / silme
    bool AddBookmark(const Bookmark& bm);
    bool RemoveBookmark(size_t index);

    // Kategori sayilari (Yeni, Okunmamis, Tek atimlik)
    void GetStandardCategoryCounts(int& newCount, int& unreadCount, int& oneshotCount) const;

    const std::vector<Series>& All() const { return m_series; }
    const std::vector<Bookmark>& Bookmarks() const { return m_bookmarks; }
    uint32_t Revision() const { return m_rev; }       // her degisiklikte artar
    bool SaveBlocked() const { return m_saveBlocked; }
    LoadNote Note() const { return m_note; }
    const std::wstring& AsidePath() const { return m_aside; }

private:
    bool ParseInto(const std::string& text);
    bool CommitSeries(size_t index, Series previous);

    std::wstring m_path;
    std::wstring m_aside;
    std::vector<Series> m_series;
    std::vector<Bookmark> m_bookmarks;
    uint32_t m_rev = 1;
    bool m_saveBlocked = false;
    LoadNote m_note = LoadNote::None;
};

} // namespace gm
