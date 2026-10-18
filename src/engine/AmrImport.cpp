#include "engine/AmrImport.h"
#include "core/FileIo.h"
#include "core/Json.h"
#include "core/Paths.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <unordered_map>
#include <unordered_set>

namespace gm::amr {
namespace {

constexpr uint64_t kMaxExportBytes = 64ull * 1024 * 1024;

// Kisa anahtar (tam kip) ya da uzun anahtar (V1 ve elle yazilmis dosya).
const json::Value* Field(const json::Value& o, const char* shortKey, const char* longKey) {
    const json::Value* v = o.Get(shortKey);
    if ((!v || v->IsNull()) && longKey) v = o.Get(longKey);
    return (v && !v->IsNull()) ? v : nullptr;
}

std::string Str(const json::Value& o, const char* shortKey, const char* longKey) {
    const json::Value* v = Field(o, shortKey, longKey);
    return v ? v->AsString() : std::string();
}

int64_t Int(const json::Value& o, const char* shortKey, const char* longKey, int64_t def) {
    const json::Value* v = Field(o, shortKey, longKey);
    return v ? v->AsInt(def) : def;
}

// V1'de listeler JSON metni olarak gomulu: metinse bir kez daha ayristir.
bool Unwrap(const json::Value& in, json::Value& holder, const json::Value*& out, bool& wasString) {
    wasString = false;
    if (in.IsString()) {
        std::string err;
        if (!json::Parse(in.str, holder, &err)) return false;
        out = &holder;
        wasString = true;
        return true;
    }
    out = &in;
    return true;
}

// Alan sinirlari: gercek AMR verisi bunlarin cok altinda. Uydurma ya da bozuk
// dosyadaki megabaytlik ad her karede DirectWrite'ta dizilip arayuzu
// kilitlemesin, kutuphane.json sismesin.
constexpr size_t kMaxUrl = 8192;
constexpr size_t kMaxText = 1024;
constexpr size_t kMaxMirror = 256;
constexpr size_t kMaxLanguage = 32;
constexpr size_t kMaxCatLen = 128;

// UTF-8 karakterinin ortasindan kesmeden kisalt.
std::string Clip(std::string s, size_t max) {
    if (s.size() <= max) return s;
    size_t n = max;
    while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) --n;
    s.resize(n);
    return s;
}

std::vector<std::string> Cats(const json::Value& o) {
    std::vector<std::string> out;
    const json::Value* v = Field(o, "c", "cats");
    if (!v) return out;
    json::Value holder;
    const json::Value* arr = nullptr;
    bool wasString = false;
    if (!Unwrap(*v, holder, arr, wasString) || !arr->IsArray()) return out;
    std::unordered_set<std::string> seen;   // dogrusal: binlerce kategoride karesel yavaslama olmasin
    for (const json::Value& c : arr->arr) {
        if (out.size() >= kMaxCats) break;
        std::string s = c.AsString();
        if (s.empty() || s.size() > kMaxCatLen) continue;
        if (seen.insert(s).second) out.push_back(std::move(s));
    }
    return out;
}

bool ReadSeries(const json::Value& o, Series& s) {
    if (!o.IsObject()) return false;
    s.url = Str(o, "u", "url");
    s.mirror = Str(o, "m", "mirror");
    s.language = Str(o, "g", "language");
    // Anahtara giren alanlar kisaltilamaz; olcusuzse kayit atlanir.
    if (s.url.empty() || s.url.size() > kMaxUrl || s.mirror.size() > kMaxMirror || s.language.size() > kMaxLanguage)
        return false;
    s.name = Clip(Str(o, "n", "name"), kMaxText);
    if (s.name.empty()) s.name = s.url;
    s.displayName = Clip(Str(o, "dn", "displayName"), kMaxText);
    s.lastChapterUrl = Str(o, "l", "lastChapterReadURL");
    if (s.lastChapterUrl.size() > kMaxUrl) s.lastChapterUrl.clear();
    if (const json::Value* cn = o.Get("cn"); cn && cn->IsArray() && !cn->arr.empty()) {
        s.lastChapterName = Clip(cn->arr[0].AsString(), kMaxText);
    } else if (const json::Value* ln = o.Get("lastChapterReadName")) {
        s.lastChapterName = Clip(ln->AsString(), kMaxText);   // kendi kutuphane.json'umuz
    } else if (const json::Value* cn = o.Get("cn"); cn && cn->IsString()) {
        s.lastChapterName = Clip(cn->AsString(), kMaxText);
    }
    s.latestChapterUrl = Str(o, "latestChapterUrl", nullptr);
    s.latestChapterName = Str(o, "latestChapterName", nullptr);
    s.lastUpdateError = Str(o, "lastUpdateError", nullptr);
    s.lastCheckedTs = Int(o, "lastCheckedTs", nullptr, 0);
    s.chapterCatalogueVersion=(int)Int(o,"chapterCatalogueVersion",nullptr,0);
    if (const auto* chapters = o.Get("chapters"); chapters && chapters->IsArray()) {
        for (const auto& ch : chapters->arr) {
            if (s.chapters.size() >= 20000) break;
            if (!ch.IsObject()) continue;
            Chapter c{Clip(Str(ch, "t", "title"), kMaxText), Str(ch, "u", "url")};
            if (!c.url.empty() && c.url.size() <= kMaxUrl) s.chapters.push_back(std::move(c));
        }
        NormalizeChapters(s.chapters);
    }
    s.cats = Cats(o);
    // Tam kip l ve ut'yi her zaman yazar, kutuphane.json da oyle; paylasim
    // kipi ikisini de yazmaz. Kisa anahtarli (V2) kayitta dn'nin yoklugu
    // "ad yok" demektir; V1 ise dn'yi hic tasimaz.
    s.hasState = Field(o, "l", "lastChapterReadURL") || Field(o, "ut", "upts");
    s.hasDisplayName = o.Get("dn") || o.Get("displayName") || o.Get("u");
    // Eksik alan hata degil: AMR yalniz varsayilandan farkliysa yazar.
    s.read    = (int)Int(o, "r", "read", 0);
    s.update  = (int)Int(o, "p", "update", 1);
    s.display = (int)Int(o, "d", "display", 0);
    s.layout  = (int)Int(o, "y", "layout", 0);
    s.webtoon = (int)Int(o, "wt", "webtoon", 0);
    if (const json::Value* fm = o.Get("fitMode")) s.fitMode = (int)fm->AsInt(0);
    if (const json::Value* dp = o.Get("doublePage")) s.doublePage = (int)dp->AsInt(0);
    if (const json::Value* rt = o.Get("rtl")) s.rtl = (int)rt->AsInt(0);
    if (const json::Value* cr = o.Get("hasCustomReaderSettings")) s.hasCustomReaderSettings = cr->AsBool(false);
    else if (Field(o, "wt", "webtoon")) s.hasCustomReaderSettings = true;
    if (const json::Value* z = Field(o, "z", "zoom")) s.zoom = z->AsNumber(0.0);
    s.upts = Int(o, "ut", "upts", 0);
    if (const json::Value* lr = Field(o, "lastReadTs", "ts")) {
        int64_t val = (int64_t)lr->AsNumber(0.0);
        if (val > 0 && val < 10000000000ll) val *= 1000;
        s.lastReadTs = val;
    }
    if (const json::Value* at = Field(o, "addedTs", "added")) {
        s.addedTs = (int64_t)at->AsNumber(0.0);
    }
    s.groupOverride = Str(o, "groupOverride", "group");
    s.key = MangaKey(s.mirror, s.url, s.language);
    if(IsMangaDexSource(s.url) && s.chapterCatalogueVersion<1 && (!s.chapters.empty() || !s.latestChapterUrl.empty())) {
        // Old HTML catalogues mixed languages and profile links. Apply the
        // migration to both library loads and imported backups, keeping reads.
        s.lastUpdateError="MangaDex bölüm listesi kayıtlı dilde yeniden doğrulanmalı";
        s.chapters.clear();s.latestChapterUrl.clear();s.latestChapterName.clear();s.lastCheckedTs=0;s.upts=0;
    }
    return true;
}

bool ReadBookmark(const json::Value& o, Bookmark& b) {
    if (!o.IsObject()) return false;
    b.mirror   = Str(o, "m", "mirror");
    b.name     = Clip(Str(o, "n", "name"), kMaxText);
    b.url      = Str(o, "u", "url");
    b.chapUrl  = Str(o, "c", "chapUrl");
    b.chapName = Clip(Str(o, "h", "chapName"), kMaxText);
    b.note     = Clip(Str(o, "o", "note"), 4 * kMaxText);
    b.type     = Clip(Str(o, "t", "type"), 16);
    b.scanUrl  = Str(o, "s", "scanUrl");
    b.scanName = Clip(Str(o, "a", "scanName"), kMaxText);
    if (b.mirror.size() > kMaxMirror || b.url.size() > kMaxUrl || b.chapUrl.size() > kMaxUrl ||
        b.scanUrl.size() > kMaxUrl)
        return false;
    return !b.chapUrl.empty() || !b.url.empty();
}

bool IsHexOrDash(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == '-';
}

// Adres parcasi yalniz kimlik mi (UUID, uzun sayi)? Kullaniciya bir sey anlatmaz.
bool IsIdLike(const std::string& s) {
    if (s.empty()) return true;
    bool allHex = true, allDigit = true;
    for (char c : s) {
        allHex = allHex && IsHexOrDash(c);
        allDigit = allDigit && (c >= '0' && c <= '9');
    }
    if (allHex && s.size() >= 16) return true;
    if (allDigit && s.size() >= 7) return true;
    return false;
}

std::string PercentDecode(const std::string& s) {
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const int h = hex(s[i + 1]), l = hex(s[i + 2]);
            if (h >= 0 && l >= 0) {
                o += (char)(h * 16 + l);
                i += 2;
                continue;
            }
        }
        o += s[i];
    }
    return o;
}

} // namespace

std::string SafeName(std::string_view name) {
    if (name == "null") return std::string();
    std::string o;
    for (char c : name) {
        if (c >= 'A' && c <= 'Z') o += (char)(c - 'A' + 'a');
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) o += c;
    }
    return o;
}

std::string AfterHostUrl(std::string_view url) {
    // AMR: "://" iceriyorsa ya da "//" ile basliyorsa "/" ile bol, ilk uc
    // parcayi at; degilse ilk parcayi at. Kalan "/" ile birlesir.
    const bool withHost = url.find("://") != std::string_view::npos || url.substr(0, 2) == "//";
    const int drop = withHost ? 3 : 1;
    size_t pos = 0;
    for (int i = 0; i < drop; ++i) {
        const size_t slash = url.find('/', pos);
        if (slash == std::string_view::npos) return std::string();
        pos = slash + 1;
    }
    return std::string(url.substr(pos));
}

std::string MangaKey(std::string_view mirror, std::string_view url, std::string_view language) {
    if (url.empty()) return "_no_key_";
    std::string key = mirror.empty() ? std::string("unknown") : SafeName(mirror);
    key += '/';
    key += AfterHostUrl(url);
    if (!language.empty()) {
        key += '_';
        key += language;
    }
    return key;
}

std::string BookmarkKey(const Bookmark& b) {
    // shared/utils.ts bookmarkKey: mangaKey(chapUrl) + ("_" + mangaKey(scanUrl)).
    // Yalniz seri adresi olan yer iminde bos anahtar "_no_key_"a coker; seri adresini kullan.
    std::string id = MangaKey(b.mirror, b.chapUrl.empty() ? b.url : b.chapUrl, "");
    if (!b.scanUrl.empty()) {
        id += '_';
        id += MangaKey(b.mirror, b.scanUrl, "");
    }
    return id;
}

std::string ChapterLabel(const Series& s) {
    if (!s.lastChapterName.empty()) return s.lastChapterName;
    std::string path = AfterHostUrl(s.lastChapterUrl);
    const size_t cut = path.find_first_of("?#");
    if (cut != std::string::npos) path.resize(cut);

    // Son iki parcadan rakam iceren ve kimlik olmayani sec:
    //   .../chapter-12          -> "chapter 12"
    //   .../episode-5/viewer    -> "episode 5"   (Webtoons)
    //   .../chapter/<uuid>      -> ""            (MangaDex: adresten ad cikmaz)
    std::string seg;
    for (int tries = 0; tries < 2 && !path.empty(); ++tries) {
        while (!path.empty() && path.back() == '/') path.pop_back();
        const size_t slash = path.find_last_of('/');
        std::string cand = PercentDecode(slash == std::string::npos ? path : path.substr(slash + 1));
        path.resize(slash == std::string::npos ? 0 : slash);
        for (const char* ext : { ".html", ".htm", ".php" }) {
            const size_t n = strlen(ext);
            if (cand.size() > n && cand.compare(cand.size() - n, n, ext) == 0) cand.resize(cand.size() - n);
        }
        if (IsIdLike(cand)) break;
        if (cand.find_first_of("0123456789") != std::string::npos) {
            seg = std::move(cand);
            break;
        }
    }
    if (seg.empty()) return std::string();
    std::string out;
    for (char c : seg) {
        const char d = (c == '-' || c == '_' || c == '+') ? ' ' : c;
        if (d == ' ' && (out.empty() || out.back() == ' ')) continue;
        out += d;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    if (out.size() > 60) return std::string();
    return out;
}

ImportError ParseExport(std::string_view text, ImportData& out) {
    out = ImportData();
    json::Value root;
    if (!json::Parse(text, root, &out.detail, &out.errLine, &out.errCol)) return ImportError::BadJson;

    const json::Value* mangasIn = nullptr;
    const json::Value* bookmarksIn = nullptr;
    if (root.IsObject()) {
        mangasIn = root.Get("mangas");
        bookmarksIn = root.Get("bookmarks");
    } else if (root.IsArray()) {
        mangasIn = &root; // yalniz liste kaydedilmis: hosgoruyle kabul et
    }
    if (!mangasIn && !bookmarksIn) return ImportError::NotAmr;

    std::unordered_map<std::string, size_t> seen;
    if (mangasIn && !mangasIn->IsNull()) {
        json::Value holder;
        const json::Value* list = nullptr;
        bool wasString = false;
        if (!Unwrap(*mangasIn, holder, list, wasString)) return ImportError::NotAmr;
        out.v1 = out.v1 || wasString;
        if (!list->IsArray()) return ImportError::NotAmr;
        // Oge sayisi dosyadan gelir: gecersiz ogeler icin de yer ayirmamak icin sinirli.
        out.series.reserve(std::min<size_t>(list->arr.size(), 20000));
        for (const json::Value& e : list->arr) {
            Series s;
            if (!ReadSeries(e, s)) {
                ++out.skipped;
                continue;
            }
            // Ayni dosyada iki kez gecen seri: sonraki kazanir.
            auto it = seen.find(s.key);
            if (it != seen.end()) {
                out.series[it->second] = std::move(s);
                continue;
            }
            seen.emplace(s.key, out.series.size());
            out.series.push_back(std::move(s));
        }
    }

    if (bookmarksIn && !bookmarksIn->IsNull()) {
        json::Value holder;
        const json::Value* list = nullptr;
        bool wasString = false;
        if (Unwrap(*bookmarksIn, holder, list, wasString) && list->IsArray()) {
            out.v1 = out.v1 || wasString;
            for (const json::Value& e : list->arr) {
                Bookmark b;
                if (ReadBookmark(e, b)) out.bookmarks.push_back(std::move(b));
                else ++out.skipped;
            }
        }
    }

    if (out.series.empty() && out.bookmarks.empty()) return ImportError::Empty;
    return ImportError::None;
}

ImportError LoadExportFile(const std::wstring& path, ImportData& out) {
    // Bellek yetmezse (dev ya da uydurma dosya) surec cokmesin: pencere
    // yordamindan kacan istisna uygulamayi sonlandirir.
    try {
        out = ImportData();
        std::string text;
        switch (fileio::ReadText(path, kMaxExportBytes, text)) {
        case fileio::ReadResult::Ok: break;
        case fileio::ReadResult::TooBig: return ImportError::TooBig;
        default: return ImportError::ReadFailed;
        }
        return ParseExport(text, out);
    } catch (const std::bad_alloc&) {
        out = ImportData();
        return ImportError::TooBig;
    }
}

bool ExportAmrFile(const std::wstring& path, const Library& lib) {
    using namespace json;
    Value root = Value::Obj();
    Value mangasArr = Value::Arr();
    for (const auto& s : lib.All()) {
        Value m = Value::Obj();
        m.Add("m", Value::Str(s.mirror));
        m.Add("n", Value::Str(s.name));
        m.Add("u", Value::Str(s.url));
        if (!s.lastChapterUrl.empty()) m.Add("l", Value::Str(s.lastChapterUrl));
        if (!s.lastChapterName.empty()) m.Add("cn", Value::Str(s.lastChapterName));
        if (s.upts > 0) m.Add("ut", Value::Num(static_cast<double>(s.upts)));
        if (s.read != 0) m.Add("r", Value::Num(s.read));
        if (s.update != 1) m.Add("p", Value::Num(s.update));
        if (s.display != 0) m.Add("d", Value::Num(s.display));
        if (s.layout != 0) m.Add("y", Value::Num(s.layout));
        if (!s.displayName.empty()) m.Add("dn", Value::Str(s.displayName));
        if (!s.language.empty()) m.Add("g", Value::Str(s.language));
        if (s.webtoon != 0) m.Add("wt", Value::Num(s.webtoon));
        if (s.zoom != 0.0) m.Add("z", Value::Num(s.zoom));
        if (!s.cats.empty()) {
            Value catsArr = Value::Arr();
            for (const auto& c : s.cats) catsArr.Push(Value::Str(c));
            m.Add("c", std::move(catsArr));
        }
        mangasArr.Push(std::move(m));
    }
    root.Add("mangas", std::move(mangasArr));

    Value bmArr = Value::Arr();
    for (const auto& b : lib.Bookmarks()) {
        Value bm = Value::Obj();
        if (!b.mirror.empty()) bm.Add("m", Value::Str(b.mirror));
        if (!b.name.empty()) bm.Add("n", Value::Str(b.name));
        if (!b.url.empty()) bm.Add("u", Value::Str(b.url));
        if (!b.chapUrl.empty()) bm.Add("c", Value::Str(b.chapUrl));
        if (!b.chapName.empty()) bm.Add("h", Value::Str(b.chapName));
        if (!b.note.empty()) bm.Add("o", Value::Str(b.note));
        if (!b.type.empty()) bm.Add("t", Value::Str(b.type));
        if (!b.scanUrl.empty()) bm.Add("s", Value::Str(b.scanUrl));
        if (!b.scanName.empty()) bm.Add("a", Value::Str(b.scanName));
        bmArr.Push(std::move(bm));
    }
    root.Add("bookmarks", std::move(bmArr));

    std::string text = Dump(root);
    return fileio::WriteAtomic(path, text);
}

static uint64_t HashKey64(std::string_view str) {
    uint64_t hash = 14695981039346656037ull;
    for (char c : str) {
        hash ^= (uint64_t)(unsigned char)c;
        hash *= 1099511628211ull;
    }
    return hash;
}

static std::wstring ChapterCacheFilePath(const std::string& key) {
    std::wstring dir = paths::Join(paths::CacheDir(), L"chapters_list");
    paths::EnsureDir(dir);
    wchar_t buf[64];
    swprintf_s(buf, L"%016llx.json", (unsigned long long)HashKey64(key));
    return paths::Join(dir, buf);
}

bool SaveCachedChapterList(const std::string& key, const std::vector<std::pair<std::string, std::string>>& chapters) {
    if (key.empty() || chapters.empty()) return false;
    if(std::any_of(chapters.begin(),chapters.end(),[](const auto& ch){return !IsChapterCandidateUrl(ch.second);})) return false;
    using namespace json;
    Value arr = Value::Arr();
    for (const auto& ch : chapters) {
        Value item = Value::Obj();
        item.Add("t", Value::Str(ch.first));
        item.Add("u", Value::Str(ch.second));
        arr.Push(std::move(item));
    }
    Value document=std::move(arr);
    if(IsMangaDexSource(key)) {
        Value wrapped=Value::Obj();wrapped.Add("version",Value::Num(1));wrapped.Add("chapters",std::move(document));document=std::move(wrapped);
    }
    std::string text = Dump(document);
    return fileio::WriteAtomic(ChapterCacheFilePath(key), text);
}

bool LoadCachedChapterList(const std::string& key, std::vector<std::pair<std::string, std::string>>& chapters) {
    chapters.clear();
    if (key.empty()) return false;
    std::string text;
    if (fileio::ReadText(ChapterCacheFilePath(key), 8 * 1024 * 1024, text) != fileio::ReadResult::Ok || text.empty()) {
        return false;
    }
    json::Value root;
    std::string err;
    if (!json::Parse(text, root, &err)) {
        return false;
    }
    const json::Value* list=&root;
    if(IsMangaDexSource(key)) {
        const auto* version=root.Get("version");list=root.Get("chapters");
        if(!version || version->AsInt()!=1 || !list) return false;
    }
    if(!list->IsArray()) return false;
    chapters.reserve(list->arr.size());
    for (const auto& item : list->arr) {
        if (!item.IsObject()) continue;
        const auto* tVal = item.Get("t");
        const auto* uVal = item.Get("u");
        if (tVal && uVal) {
            if(!IsChapterCandidateUrl(uVal->AsString())) { chapters.clear();return false; }
            chapters.push_back({ tVal->AsString(), uVal->AsString() });
        }
    }
    return !chapters.empty();
}

} // namespace gm::amr
