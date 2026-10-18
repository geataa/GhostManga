#include "engine/Library.h"
#include "core/FileIo.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/Utf8.h"
#include "engine/AmrImport.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <new>
#include <unordered_map>
#include <unordered_set>

namespace gm {
namespace {

constexpr uint64_t kMaxLibraryBytes = 256ull * 1024 * 1024;

int64_t NowEpochMs() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return (int64_t)((uli.QuadPart - 116444736000000000ull) / 10000ull);
}

json::Value SeriesToJson(const Series& s) {
    using json::Value;
    Value o = Value::Obj();
    o.Add("key", Value::Str(s.key));
    o.Add("lastUpdateError", Value::Str(s.lastUpdateError));
    o.Add("lastCheckedTs", Value::Num((double)s.lastCheckedTs));
    if(s.chapterCatalogueVersion) o.Add("chapterCatalogueVersion",Value::Num(s.chapterCatalogueVersion));
    Value chapters = Value::Arr();
    for (const auto& ch : s.chapters) {
        Value item = Value::Obj();
        item.Add("t", Value::Str(ch.title));
        item.Add("u", Value::Str(ch.url));
        chapters.Push(std::move(item));
    }
    o.Add("chapters", std::move(chapters));
    o.Add("mirror", Value::Str(s.mirror));
    o.Add("name", Value::Str(s.name));
    if (!s.displayName.empty()) o.Add("displayName", Value::Str(s.displayName));
    o.Add("url", Value::Str(s.url));
    o.Add("lastChapterReadURL", Value::Str(s.lastChapterUrl));
    if (!s.lastChapterName.empty()) o.Add("lastChapterReadName", Value::Str(s.lastChapterName));
    if (!s.latestChapterUrl.empty()) o.Add("latestChapterUrl", Value::Str(s.latestChapterUrl));
    if (!s.latestChapterName.empty()) o.Add("latestChapterName", Value::Str(s.latestChapterName));
    if (!s.language.empty()) o.Add("language", Value::Str(s.language));
    Value cats = Value::Arr();
    for (const auto& c : s.cats) cats.Push(Value::Str(c));
    o.Add("cats", std::move(cats));
    o.Add("read", Value::Num(s.read));
    o.Add("update", Value::Num(s.update));
    o.Add("display", Value::Num(s.display));
    o.Add("layout", Value::Num(s.layout));
    o.Add("webtoon", Value::Num(s.webtoon));
    o.Add("fitMode", Value::Num(s.fitMode));
    o.Add("doublePage", Value::Num(s.doublePage));
    o.Add("rtl", Value::Num(s.rtl));
    o.Add("hasCustomReaderSettings", Value::Bool(s.hasCustomReaderSettings));
    o.Add("zoom", Value::Num(s.zoom));
    o.Add("upts", Value::Num((double)s.upts));
    if (s.lastReadTs > 0) o.Add("lastReadTs", Value::Num((double)s.lastReadTs));
    if (s.addedTs > 0) o.Add("addedTs", Value::Num((double)s.addedTs));
    if (!s.groupOverride.empty()) o.Add("groupOverride", Value::Str(s.groupOverride));
    return o;
}

json::Value BookmarkToJson(const Bookmark& b) {
    using json::Value;
    Value o = Value::Obj();
    auto put = [&](const char* k, const std::string& v) {
        if (!v.empty()) o.Add(k, Value::Str(v));
    };
    put("mirror", b.mirror);
    put("name", b.name);
    put("url", b.url);
    put("chapUrl", b.chapUrl);
    put("chapName", b.chapName);
    put("note", b.note);
    put("type", b.type);
    put("scanUrl", b.scanUrl);
    put("scanName", b.scanName);
    return o;
}

} // namespace

bool Library::ParseInto(const std::string& text) {
    // Kendi dosyamiz da AMR uzun alan adlariyla yazilir: ayni okuyucu.
    amr::ImportData data;
    const amr::ImportError e = amr::ParseExport(text, data);
    if (e != amr::ImportError::None && e != amr::ImportError::Empty) {
        log::Write(L"Kutuphane dosyasi ayristirilamadi: %s", FromUtf8(data.detail).c_str());
        return false;
    }
    m_series = std::move(data.series);
    m_bookmarks = std::move(data.bookmarks);
    for (Series& s : m_series) s.hasState = s.hasDisplayName = true;
    return true;
}

void Library::Load(const std::wstring& path) {
    m_path = path;
    m_aside.clear();
    m_series.clear();
    m_bookmarks.clear();
    m_saveBlocked = false;
    m_note = LoadNote::None;
    ++m_rev;

    try {
        std::string text;
        switch (fileio::ReadText(path, kMaxLibraryBytes, text)) {
        case fileio::ReadResult::Ok: break;
        case fileio::ReadResult::Missing: return;
        default:
            m_saveBlocked = true;
            m_note = LoadNote::Blocked;
            log::Write(L"Kutuphane dosyasi okunamadi; bu oturumda uzerine yazilmayacak: %s", path.c_str());
            return;
        }
        if (ParseInto(text)) {
            log::Write(L"Kutuphane yuklendi: %zu seri, %zu yer imi.", m_series.size(), m_bookmarks.size());
            return;
        }
    } catch (const std::bad_alloc&) {
        m_series.clear();
        m_bookmarks.clear();
        m_saveBlocked = true;
        m_note = LoadNote::Blocked;
        log::Write(L"Kutuphane dosyasi bellege sigmadi; bu oturumda uzerine yazilmayacak.");
        return;
    }

    // Bozuk: kullanicinin verisini silme. Kenara TASI (kopyalama degil):
    // yerinde kalsaydi bir sonraki kayit onu .bak'in, yani son saglam
    // surumun ustune kopyalardi.
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t suffix[64];
    swprintf_s(suffix, L".bozuk-%04u%02u%02u-%02u%02u%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
               st.wSecond);
    const std::wstring aside = path + suffix;
    if (!MoveFileExW(path.c_str(), aside.c_str(), MOVEFILE_WRITE_THROUGH)) {
        m_saveBlocked = true;
        m_note = LoadNote::Blocked;
        log::Write(L"Kutuphane dosyasi bozuk ve kenara alinamadi (hata %lu); uzerine yazilmayacak.", GetLastError());
        return;
    }
    m_aside = aside;
    log::Write(L"Kutuphane dosyasi bozuk; kenara alindi: %s", aside.c_str());

    // Son saglam surum: .bak
    try {
        std::string bak;
        const fileio::ReadResult br = fileio::ReadText(path + L".bak", kMaxLibraryBytes, bak);
        if (br == fileio::ReadResult::Ok && ParseInto(bak)) {
            m_note = LoadNote::RestoredFromBackup;
            // Hemen yaz: library.json artik yok; kullanici bir sey ice aktarmadan
            // kapatirsa sonraki acilista liste sessizce bos gelirdi. Hedef dosya
            // olmadigi icin bu kayit .bak'a dokunmaz.
            const bool saved = Save();
            log::Write(L"Kutuphane yedekten yuklendi: %zu seri%s.", m_series.size(), saved ? L"" : L" (yazilamadi)");
            return;
        }
        // .bak var ama su an okunamiyor (kilitli, cok buyuk): bos listeyle kayda
        // izin verme; ikinci kayit bos dosyayi tek saglam yedegin ustune kopyalardi.
        if (br != fileio::ReadResult::Ok && br != fileio::ReadResult::Missing) {
            m_series.clear();
            m_bookmarks.clear();
            m_saveBlocked = true;
            m_note = LoadNote::Blocked;
            log::Write(L"Kutuphane yedegi okunamadi; bu oturumda uzerine yazilmayacak.");
            return;
        }
    } catch (const std::bad_alloc&) {
        m_series.clear();
        m_bookmarks.clear();
        m_saveBlocked = true;
        m_note = LoadNote::Blocked;
        log::Write(L"Kutuphane yedegi okunurken bellek yetmedi; bu oturumda uzerine yazilmayacak.");
        return;
    }
    m_series.clear();
    m_bookmarks.clear();
    m_note = LoadNote::ResetCorrupt;
}

bool Library::Save() {
    if (m_path.empty() || m_saveBlocked) return false;
    // Okuyucunun deger siniri (json::kMaxValues) asilacaksa yazma: dosya bir
    // sonraki acilista "bozuk" sayilip kenara alinirdi. Seri basina en fazla
    // 26 deger (nesne + alanlar + cats dizisi) ve kategori basina 1 deger.
    size_t values = 8 + m_bookmarks.size() * 11;
    for (const Series& s : m_series) values += 32 + s.cats.size() + s.chapters.size() * 3;
    if (values > json::kMaxValues) {
        log::Write(L"Kutuphane cok buyuk (%zu seri); kaydedilmedi.", m_series.size());
        return false;
    }
    using json::Value;
    std::string text;
    try {
        Value root = Value::Obj();
        root.Add("version", Value::Num(1));
        root.Add("app", Value::Str("GhostManga"));
        Value mangas = Value::Arr();
        for (const Series& s : m_series) mangas.Push(SeriesToJson(s));
        root.Add("mangas", std::move(mangas));
        Value bms = Value::Arr();
        for (const Bookmark& b : m_bookmarks) bms.Push(BookmarkToJson(b));
        root.Add("bookmarks", std::move(bms));
        text = json::Dump(root);
    } catch (const std::bad_alloc&) {
        log::Write(L"Kutuphane kaydedilemedi: bellek yetmedi.");
        return false;
    }
    // Load'un okuyamayacagi dosyayi hic yazma: yeniden acilista liste bos
    // gorunur ve tum kayitlar engellenirdi.
    if (text.size() > kMaxLibraryBytes) {
        log::Write(L"Kutuphane cok buyuk (%zu bayt); kaydedilmedi.", text.size());
        return false;
    }
    return fileio::WriteAtomic(m_path, text, true);
}

Library::MergeResult Library::Merge(std::vector<Series>&& series, std::vector<Bookmark>&& bookmarks) {
    MergeResult res;
    std::unordered_map<std::string, size_t> index;
    index.reserve(m_series.size() + series.size());
    for (size_t i = 0; i < m_series.size(); ++i) index.emplace(m_series[i].key, i);

    for (Series& in : series) {
        auto it = index.find(in.key);
        if (it == index.end()) {
            in.hasState = in.hasDisplayName = true;
            if (in.addedTs == 0) in.addedTs = NowEpochMs();
            index.emplace(in.key, m_series.size());
            m_series.push_back(std::move(in));
            ++res.added;
            continue;
        }
        Series& cur = m_series[it->second];
        if (!in.hasState) {
            // Paylasim kipi: dosyada okuma durumu yok. Eksik alanlar "sifirla"
            // demek degil; AMR'nin kendi ice aktaricisi da mevcut durumu korur.
            in.displayName = cur.displayName;
            in.lastChapterUrl = cur.lastChapterUrl;
            in.lastChapterName = cur.lastChapterName;
            in.read = cur.read;
            in.update = cur.update;
            in.display = cur.display;
            in.layout = cur.layout;
            in.webtoon = cur.webtoon;
            in.zoom = cur.zoom;
            in.upts = cur.upts;
        } else if (!in.hasDisplayName) {
            in.displayName = cur.displayName;   // V1 kullanici adini tasimaz
        }
        in.hasState = in.hasDisplayName = true;
        in.lastReadTs = (std::max)(in.lastReadTs, cur.lastReadTs);
        in.addedTs = cur.addedTs > 0 ? cur.addedTs : (in.addedTs > 0 ? in.addedTs : NowEpochMs());
        if (in.groupOverride.empty()) in.groupOverride = cur.groupOverride;
        // AMR dosyalari GhostManga'ya ozel alanlari tasimaz: manga bazli okuma
        // ayarlari ve sitedeki en yeni bolum bilgisi ice aktarmada kaybolmasin.
        if (in.latestChapterUrl.empty()) in.latestChapterUrl = cur.latestChapterUrl;
        if (in.latestChapterName.empty()) in.latestChapterName = cur.latestChapterName;
        if (!in.hasCustomReaderSettings && cur.hasCustomReaderSettings) {
            in.webtoon = cur.webtoon;
            in.fitMode = cur.fitMode;
            in.doublePage = cur.doublePage;
            in.rtl = cur.rtl;
            in.zoom = cur.zoom;
            in.hasCustomReaderSettings = true;
        }
        // Kategoriler birlesir: kutuphanede elle eklenen kategori kaybolmasin.
        std::unordered_set<std::string> have(in.cats.begin(), in.cats.end());
        for (const auto& c : cur.cats) {
            if (in.cats.size() >= amr::kMaxCats) break;
            if (have.insert(c).second) in.cats.push_back(c);
        }
        in.chapters = cur.chapters;
        in.chapterCatalogueVersion = cur.chapterCatalogueVersion;
        in.latestChapterUrl = cur.latestChapterUrl;
        in.latestChapterName = cur.latestChapterName;
        in.lastCheckedTs = cur.lastCheckedTs;
        in.lastUpdateError = cur.lastUpdateError;
        if (in.lastChapterName.empty() && in.lastChapterUrl == cur.lastChapterUrl) in.lastChapterName = cur.lastChapterName;
        if (in == cur) {
            ++res.unchanged;
        } else {
            cur = std::move(in);
            ++res.updated;
        }
    }

    // Yer imi kimligi AMR'ninki: alan adi ya da http/https degisince ayni yer
    // imi ikinci kez eklenmesin.
    std::unordered_map<std::string, size_t> bmIndex;
    for (size_t i = 0; i < m_bookmarks.size(); ++i) bmIndex.emplace(amr::BookmarkKey(m_bookmarks[i]), i);
    bool bmChanged = false;
    for (Bookmark& b : bookmarks) {
        const std::string id = amr::BookmarkKey(b);
        auto it = bmIndex.find(id);
        if (it == bmIndex.end()) {
            bmIndex.emplace(id, m_bookmarks.size());
            m_bookmarks.push_back(std::move(b));
            ++res.bookmarksAdded;
            bmChanged = true;
        } else if (!(m_bookmarks[it->second] == b)) {
            m_bookmarks[it->second] = std::move(b);
            bmChanged = true;
        }
    }

    if (res.added || res.updated || bmChanged) ++m_rev;
    return res;
}

static std::string StripLangSuffix(const std::string& str) {
    // Yalniz alfabetik dil son ekleri (_en, _tr): "abc_01" ile "abc_02" ayni sayilmasin.
    const size_t n = str.size();
    if (n > 3 && str[n - 3] == '_' && isalpha((unsigned char)str[n - 2]) && isalpha((unsigned char)str[n - 1])) {
        return str.substr(0, n - 3);
    }
    return str;
}

static std::string PathAfterFirstSlash(const std::string& str) {
    size_t slash = str.find('/');
    if (slash != std::string::npos && slash + 1 < str.size()) {
        return str.substr(slash + 1);
    }
    return str;
}

// Alt dizgi eslesmesi yalniz yol parcasi sinirinda gecerli: "manga/one-piece"
// anahtari "manga/one-piece-colored" serisini, "title/12" de "title/123"u yakalamasin.
static bool ContainsAtBoundary(const std::string& hay, const std::string& needle) {
    if (needle.empty() || hay.size() < needle.size()) return false;
    for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + 1)) {
        const size_t e = p + needle.size();
        const bool startOk = (p == 0) || hay[p - 1] == '/';
        const bool endOk = (e == hay.size()) || strchr("/?#_", hay[e]) != nullptr;
        if (startOk && endOk) return true;
    }
    return false;
}

static bool MatchSeriesKeyStrict(const Series& s, const std::string& key) {
    if (key.empty()) return false;
    if (s.key == key || s.url == key) return true;

    const std::string bareKey = StripLangSuffix(key);
    const std::string bareSKey = StripLangSuffix(s.key);
    if (bareKey == bareSKey || s.key == bareKey || bareSKey == key) return true;

    const std::string sHostPath = amr::AfterHostUrl(s.url);
    if (!sHostPath.empty()) {
        if (amr::AfterHostUrl(key) == sHostPath) return true;
        if (PathAfterFirstSlash(bareKey) == sHostPath) return true;
    }
    return false;
}

static bool MatchSeriesKeyFuzzy(const Series& s, const std::string& key) {
    if (key.empty()) return false;
    const std::string sHostPath = amr::AfterHostUrl(s.url);
    if (!sHostPath.empty() && ContainsAtBoundary(key, sHostPath)) return true;

    const std::string bareKey = StripLangSuffix(key);
    if (!bareKey.empty()) {
        if (!s.url.empty() && ContainsAtBoundary(s.url, bareKey)) return true;
        if (!s.key.empty() && ContainsAtBoundary(s.key, bareKey)) return true;
    }
    return false;
}

// Once tum listede kesin eslesme aranir; yoksa esnek eslesmeye dusulur.
// Boylece esnek kural, kesin eslesen baska bir seriden once gelen yanlis
// seriyi secemez (yanlis seriye yazma / yanlis seriyi silme).
static size_t FindSeriesIndex(const std::vector<Series>& list, const std::string& key) {
    if (key.empty()) return std::string::npos;
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].key == key || list[i].url == key) return i;
    // Language suffix compatibility is limited to the same mirror key.
    for (size_t i = 0; i < list.size(); ++i)
        if (StripLangSuffix(list[i].key) == StripLangSuffix(key)) return i;
    return std::string::npos;
}

bool Library::CommitSeries(size_t index, Series previous) {
    if (Save()) { ++m_rev; return true; }
    m_series[index] = std::move(previous);
    return false;
}

bool Library::MarkLastRead(const std::string& key, const std::string& url, const std::string& name) {
    const auto idx = FindSeriesIndex(m_series, key);
    if (idx == std::string::npos) return false;
    Series previous = m_series[idx];
    auto& s = m_series[idx];
    s.lastReadTs = NowEpochMs();
    if (!url.empty() && url != "__FIRST__" && url != "__LAST__") {
        const int oldIndex = ChapterIndex(s.chapters, s.lastChapterUrl);
        const int newIndex = ChapterIndex(s.chapters, url);
        const double oldNum = ChapterNumber(s.lastChapterName, s.lastChapterUrl);
        const double newNum = ChapterNumber(name, url);
        const bool advance = s.lastChapterUrl.empty() ||
            (oldIndex >= 0 && newIndex >= 0 ? newIndex <= oldIndex :
                (oldNum >= 0 && newNum >= 0 ? newNum >= oldNum : true));
        if (advance) {
            s.lastChapterUrl = url;
            s.lastChapterName = name;
            if (s.lastChapterName.empty() && newIndex >= 0) s.lastChapterName = s.chapters[newIndex].title;
        }
    }
    return CommitSeries(idx, std::move(previous));
}

bool Library::MarkUpdated(const std::string& key, int64_t ts, const std::string& url, const std::string& name) {
    const auto idx = FindSeriesIndex(m_series, key);
    if (idx == std::string::npos || url.empty()) return false;
    Series previous = m_series[idx];
    auto& s = m_series[idx];
    if (ChapterUrlIdentity(s.latestChapterUrl) != ChapterUrlIdentity(url) && ts > 0) s.upts = ts;
    s.latestChapterUrl = url;
    s.latestChapterName = name;
    return CommitSeries(idx, std::move(previous));
}

bool Library::StoreUpdateError(const std::string& key, const std::string& error) {
    const auto idx = FindSeriesIndex(m_series,key);
    if (idx == std::string::npos) return false;
    Series previous = m_series[idx];
    m_series[idx].lastUpdateError = error;
    m_series[idx].lastCheckedTs = NowEpochMs();
    return CommitSeries(idx,std::move(previous));
}

bool Library::StoreChapters(const std::string& key, std::vector<Chapter> chapters, int64_t checkedTs) {
    if(std::any_of(chapters.begin(),chapters.end(),[](const Chapter& chapter){return !IsChapterCandidateUrl(chapter.url);})) return false;
    NormalizeChapters(chapters);
    const auto idx = FindSeriesIndex(m_series, key);
    if (idx == std::string::npos || chapters.empty()) return false;
    Series previous = m_series[idx];
    auto& s = m_series[idx];
    const auto& latest = chapters.front();
    const bool changed = !s.latestChapterUrl.empty() &&
        ChapterUrlIdentity(latest.url) != ChapterUrlIdentity(s.latestChapterUrl);
    // Only publish a new-release timestamp when the previously known chapter
    // is older in the new catalogue, or both explicit numbers prove advancement.
    const int previousIndex = ChapterIndex(chapters, s.latestChapterUrl);
    const double oldNum = ChapterNumber(s.latestChapterName, s.latestChapterUrl);
    const double newNum = ChapterNumber(latest.title, latest.url);
    if (changed && (previousIndex > 0 || (oldNum >= 0 && newNum > oldNum)))
        s.upts = checkedTs > 0 ? checkedTs : NowEpochMs();
    s.lastUpdateError.clear();
    s.chapterCatalogueVersion=IsMangaDexSource(s.url)?1:0;
    s.chapters = std::move(chapters);
    const int lastReadIndex=ChapterIndex(s.chapters,s.lastChapterUrl);
    if(lastReadIndex>=0) s.lastChapterName=s.chapters[(size_t)lastReadIndex].title;
    s.latestChapterUrl = s.chapters.front().url;
    s.latestChapterName = s.chapters.front().title;
    s.lastCheckedTs = checkedTs > 0 ? checkedTs : NowEpochMs();
    return CommitSeries(idx, std::move(previous));
}

bool Library::UpdateReadingSettings(const std::string& key, bool webtoon, int fitMode, bool doublePage, bool rtl, double zoom) {
    if (key.empty()) return false;
    {
        const size_t idx = FindSeriesIndex(m_series, key);
        if (idx != std::string::npos) {
            Series& s = m_series[idx];
            s.webtoon = webtoon ? 1 : 0;
            s.fitMode = fitMode;
            s.doublePage = doublePage ? 1 : 0;
            s.rtl = rtl ? 1 : 0;
            if (zoom > 0.05) s.zoom = zoom;
            s.hasCustomReaderSettings = true;
            ++m_rev;
            Save();
            return true;
        }
    }
    return false;
}

const Series* Library::FindSeries(const std::string& key) const {
    const size_t idx = FindSeriesIndex(m_series, key);
    return idx != std::string::npos ? &m_series[idx] : nullptr;
}

bool Library::AddSeries(const Series& s) {
    if (s.name.empty() || s.url.empty()) return false;
    if (FindSource(s.mirror, s.url)) return false;
    for (const auto& existing : m_series) {
        if (!s.key.empty() && existing.key == s.key) return false;
        if (existing.url == s.url) return false;
    }
    Series toAdd = s;
    if (toAdd.addedTs <= 0) toAdd.addedTs = NowEpochMs();
    m_series.push_back(std::move(toAdd));
    if (!Save()) { m_series.pop_back(); return false; }
    ++m_rev;
    return true;
}

const Series* Library::FindSource(const std::string& mirror, const std::string& url) const {
    if (mirror.empty() || url.empty()) return nullptr;
    const auto key = amr::MangaKey(mirror, ChapterUrlIdentity(url), "");
    for (const auto& series : m_series)
        if (amr::MangaKey(series.mirror, ChapterUrlIdentity(series.url), "") == key) return &series;
    return nullptr;
}

bool Library::RemoveSeries(const std::string& key) {
    const size_t idx = FindSeriesIndex(m_series, key);
    if (idx != std::string::npos) {
        Series previous = m_series[idx];
        m_series.erase(m_series.begin() + (ptrdiff_t)idx);
        if (!Save()) {
            m_series.insert(m_series.begin() + (ptrdiff_t)idx, std::move(previous));
            return false;
        }
        ++m_rev;
        return true;
    }
    return false;
}

bool Library::ResetReadingProgress(const std::string& key) {
    const auto idx = FindSeriesIndex(m_series, key);
    if (idx == std::string::npos) return false;
    Series previous = m_series[idx];
    auto& s = m_series[idx];
    s.lastChapterUrl.clear();
    s.lastChapterName.clear();
    s.lastReadTs = 0;
    return CommitSeries(idx, std::move(previous));
}

bool Library::MarkReadUpToLatest(const std::string& key) {
    const auto idx = FindSeriesIndex(m_series, key);
    if (idx == std::string::npos || m_series[idx].latestChapterUrl.empty()) return false;
    Series previous = m_series[idx];
    auto& s = m_series[idx];
    s.lastChapterUrl = s.latestChapterUrl;
    s.lastChapterName = s.latestChapterName;
    s.lastReadTs = NowEpochMs();
    return CommitSeries(idx, std::move(previous));
}

bool Library::ClearAll() {
    auto previousSeries = std::move(m_series);
    auto previousBookmarks = std::move(m_bookmarks);
    m_series.clear();
    m_bookmarks.clear();
    if (!Save()) {
        m_series = std::move(previousSeries);
        m_bookmarks = std::move(previousBookmarks);
        return false;
    }
    ++m_rev;
    return true;
}

static std::string CleanStr(const std::string& str) {
    if (str.empty()) return "";
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    std::string out = str.substr(first, (last - first + 1));

    auto replaceAll = [&](const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = out.find(from, pos)) != std::string::npos) {
            out.replace(pos, from.length(), to);
            pos += to.length();
        }
    };
    replaceAll("&amp;", "&");
    replaceAll("&#039;", "'");
    replaceAll("&apos;", "'");
    replaceAll("&quot;", "\"");
    replaceAll("&lt;", "<");
    replaceAll("&gt;", ">");
    replaceAll("&nbsp;", " ");
    replaceAll("&#8217;", "'");
    replaceAll("&#8216;", "'");
    replaceAll("&#8220;", "\"");
    replaceAll("&#8221;", "\"");
    return out;
}

bool Library::ConvertAndOptimize(int& fixedCount) {
    fixedCount = 0;
    const int64_t nowMs = NowEpochMs();

    // 1. Her bir seriyi temizle ve alanlarini normallestir
    for (Series& s : m_series) {
        std::string origName = s.name;
        s.name = CleanStr(s.name);
        if (s.name != origName) fixedCount++;

        std::string origDisp = s.displayName;
        s.displayName = CleanStr(s.displayName);
        if (s.displayName != origDisp) fixedCount++;

        std::string origMirror = s.mirror;
        s.mirror = CleanStr(s.mirror);
        if (s.mirror != origMirror) fixedCount++;

        std::string origUrl = s.url;
        s.url = CleanStr(s.url);
        if (s.url.rfind("//", 0) == 0) {
            s.url = "https:" + s.url;
            fixedCount++;
        }
        if (s.url != origUrl) fixedCount++;

        s.lastChapterUrl = CleanStr(s.lastChapterUrl);
        s.lastChapterName = CleanStr(s.lastChapterName);
        s.groupOverride = CleanStr(s.groupOverride);

        // Kategorileri tekillestir ve bos olanlari ayikla
        std::vector<std::string> cleanCats;
        std::unordered_set<std::string> seenCats;
        for (const auto& c : s.cats) {
            std::string cc = CleanStr(c);
            if (!cc.empty() && seenCats.insert(cc).second) {
                cleanCats.push_back(std::move(cc));
            } else {
                fixedCount++;
            }
        }
        std::sort(cleanCats.begin(), cleanCats.end());
        s.cats = std::move(cleanCats);

        if (s.addedTs <= 0) {
            s.addedTs = nowMs;
            fixedCount++;
        }
        if (s.upts < 0) { s.upts = 0; fixedCount++; }
        if (s.lastReadTs < 0) { s.lastReadTs = 0; fixedCount++; }
    }

    // 2. Yinelenen (duplicate) serileri tespit et ve birlestir
    std::vector<Series> uniqueSeries;
    std::unordered_map<std::string, size_t> keyMap;
    std::unordered_map<std::string, size_t> urlMap;

    for (Series& s : m_series) {
        if (s.url.empty()) {
            fixedCount++;
            continue; // URL'siz kayitlari temizle
        }

        std::string normUrl = s.url;
        for (char& c : normUrl) c = (char)tolower((unsigned char)c);
        while (!normUrl.empty() && normUrl.back() == '/') normUrl.pop_back();

        auto itUrl = urlMap.find(normUrl);
        auto itKey = (!s.key.empty()) ? keyMap.find(s.key) : keyMap.end();

        size_t targetIdx = size_t(-1);
        if (itUrl != urlMap.end()) targetIdx = itUrl->second;
        else if (itKey != keyMap.end()) targetIdx = itKey->second;

        if (targetIdx != size_t(-1)) {
            // Zaten var: birlestir
            Series& ex = uniqueSeries[targetIdx];
            if (ex.displayName.empty() && !s.displayName.empty()) ex.displayName = s.displayName;
            if (s.read == 0) ex.read = 0; // takip ediliyorsa takip durumunu koru
            if (s.lastReadTs > ex.lastReadTs) {
                ex.lastReadTs = s.lastReadTs;
                if (!s.lastChapterUrl.empty()) ex.lastChapterUrl = s.lastChapterUrl;
                if (!s.lastChapterName.empty()) ex.lastChapterName = s.lastChapterName;
            }
            if (s.upts > ex.upts) ex.upts = s.upts;
            // Kategorileri birlestir
            std::unordered_set<std::string> catSet(ex.cats.begin(), ex.cats.end());
            for (const auto& c : s.cats) {
                if (catSet.insert(c).second) ex.cats.push_back(c);
            }
            fixedCount++;
        } else {
            size_t newIdx = uniqueSeries.size();
            if (!s.key.empty()) keyMap[s.key] = newIdx;
            urlMap[normUrl] = newIdx;
            uniqueSeries.push_back(std::move(s));
        }
    }

    m_series = std::move(uniqueSeries);

    // 3. Yer imlerini temizle ve yinelenenleri kaldir
    std::vector<Bookmark> uniqueBookmarks;
    std::unordered_set<std::string> seenBms;
    for (Bookmark& b : m_bookmarks) {
        b.name = CleanStr(b.name);
        b.chapName = CleanStr(b.chapName);
        b.url = CleanStr(b.url);
        b.chapUrl = CleanStr(b.chapUrl);
        std::string bmKey = (b.chapUrl.empty() && b.url.empty()) ? "" : amr::BookmarkKey(b);
        if (!bmKey.empty() && seenBms.insert(bmKey).second) {
            uniqueBookmarks.push_back(std::move(b));
        } else {
            fixedCount++;
        }
    }
    m_bookmarks = std::move(uniqueBookmarks);

    ++m_rev;
    Save();
    return true;
}

bool Library::AddBookmark(const Bookmark& bm) {
    if (bm.name.empty() || bm.chapUrl.empty()) return false;
    const std::string key = amr::BookmarkKey(bm);
    for (auto& existing : m_bookmarks) {
        if (amr::BookmarkKey(existing) == key) {
            existing = bm;
            ++m_rev;
            return Save();
        }
    }
    m_bookmarks.push_back(bm);
    ++m_rev;
    return Save();
}

bool Library::RemoveBookmark(size_t index) {
    if (index >= m_bookmarks.size()) return false;
    m_bookmarks.erase(m_bookmarks.begin() + index);
    ++m_rev;
    return Save();
}

void Library::GetStandardCategoryCounts(int& newCount, int& unreadCount, int& oneshotCount) const {
    newCount = 0;
    unreadCount = 0;
    oneshotCount = 0;
    for (const auto& s : m_series) {
        if (s.upts > s.lastReadTs && s.upts > 0) ++newCount;
        if (s.read == 0) ++unreadCount;
        bool isOneShot = false;
        for (const auto& c : s.cats) {
            std::string lc = c;
            for (char& ch : lc) ch = (char)::tolower((unsigned char)ch);
            if (lc.find("oneshot") != std::string::npos || lc.find("one-shot") != std::string::npos) {
                isOneShot = true;
                break;
            }
        }
        if (!isOneShot) {
            std::string ln = s.name;
            for (char& ch : ln) ch = (char)::tolower((unsigned char)ch);
            if (ln.find("oneshot") != std::string::npos || ln.find("one-shot") != std::string::npos) {
                isOneShot = true;
            }
        }
        if (isOneShot) ++oneshotCount;
    }
}

} // namespace gm
