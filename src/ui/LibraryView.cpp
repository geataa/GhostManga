#include "ui/LibraryView.h"
#include "core/I18n.h"
#include "core/Paths.h"
#include "core/Utf8.h"
#include "engine/AmrImport.h"
#include "engine/MirrorEngine.h"
#include "engine/CoverManager.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <windows.h>
#include <shlwapi.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <chrono>

namespace gm {
namespace {

std::wstring OneLine(const std::wstring& s, size_t maxLen) {
    std::wstring o;
    o.reserve(std::min(s.size(), maxLen + 1));
    bool pend = false;
    for (wchar_t c : s) {
        if (iswspace(c) || c < 0x20 || c == 0x7F || c == 0x85 || c == 0x2028 || c == 0x2029) {
            pend = !o.empty();
            continue;
        }
        if (o.size() >= maxLen) {
            if (IS_LOW_SURROGATE(c) && !o.empty() && IS_HIGH_SURROGATE(o.back())) o.pop_back();
            o += L"\x2026";
            return o;
        }
        if (pend) { o += L' '; pend = false; }
        o += c;
    }
    return o;
}

std::wstring FirstLetter(const std::wstring& title) {
    if (title.empty()) return L"?";
    const size_t n = (IS_HIGH_SURROGATE(title[0]) && title.size() > 1) ? 2 : 1;
    std::wstring s = title.substr(0, n);
    CharUpperBuffW(s.data(), (DWORD)s.size());
    return s;
}

std::wstring ToLowerInvariant(const std::wstring& s) {
    if (s.empty()) return std::wstring();
    std::wstring o(s.size(), L'\0');
    LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, s.data(), (int)s.size(),
                  o.data(), (int)o.size(), nullptr, nullptr, 0);
    return o;
}

std::wstring NormalizeForGroup(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    bool inParen = false, inBracket = false;
    for (wchar_t c : s) {
        if (c == L'(') inParen = true;
        else if (c == L')') { inParen = false; continue; }
        else if (c == L'[') inBracket = true;
        else if (c == L']') { inBracket = false; continue; }
        if (inParen || inBracket) continue;
        if (iswalnum(c)) out += c;
    }
    if (out.empty()) {
        for (wchar_t c : s) if (iswalnum(c)) out += c;
    }
    if (out.empty()) out = s;
    return ToLowerInvariant(out);
}

uint32_t GetMirrorColor(const std::string& mirror) {
    std::string m = mirror;
    std::transform(m.begin(), m.end(), m.begin(), [](unsigned char c) { return (char)::tolower(c); });
    if (m.find("dex") != std::string::npos) return 0xFF6740;      // MangaDex orange
    if (m.find("asura") != std::string::npos) return 0x9B59B6;    // Asura purple
    if (m.find("reaper") != std::string::npos) return 0xE74C3C;   // Reaper red
    if (m.find("flame") != std::string::npos) return 0xE67E22;    // Flame orange
    if (m.find("webtoon") != std::string::npos) return 0x00DC64;  // Webtoon green
    if (m.find("bat") != std::string::npos) return 0x3498DB;      // MangaBat blue
    if (m.find("nelo") != std::string::npos) return 0x2980B9;     // MangaNelo dark blue
    if (m.find("comick") != std::string::npos) return 0xEE5253;   // Comick coral
    if (m.find("kakalot") != std::string::npos) return 0x1DD1A1; // MangaKakalot teal
    if (m.find("scan") != std::string::npos) return 0x54A0FF;     // Scan blue
    if (m.find("yerel") != std::string::npos || m.find("local") != std::string::npos) return 0xF1C40F; // Local yellow

    const uint32_t palette[] = {
        0xFF6B6B, 0x48DBFB, 0x1DD1A1, 0xF368E0, 0xFF9F43, 0x54A0FF,
        0x5F27CD, 0x00D2D3, 0xFF9FF3, 0xFECA57, 0x10AC84, 0xEE5253
    };
    uint32_t hash = 5381;
    for (char c : m) hash = ((hash << 5) + hash) + (uint8_t)c;
    return palette[hash % (sizeof(palette) / sizeof(palette[0]))];
}

void DrawMirrorBadge(Renderer& r, const std::string& mirror, const D2D1_RECT_F& badgeRc, float s) {
    if (mirror.empty()) return;
    const uint32_t col = GetMirrorColor(mirror);
    r.FillRound(badgeRc, 4.0f * s, col, 0.20f);
    r.StrokeRound(badgeRc, 4.0f * s, col, 1.0f * s, 0.40f);
    r.Text(FromUtf8(mirror), badgeRc, col, 10.5f * s, Renderer::Align::Center, Renderer::Weight::Semibold);
}

double ParseChapterNumber(const std::string& str) {
    return ChapterNumber(str);
}

std::wstring CleanChapterTitle(const std::wstring& rawTitle, int fallbackIdx = -1) {
    std::wstring t = rawTitle;
    while (!t.empty() && (t.front() == L' ' || t.front() == L'\t' || t.front() == L'\r' || t.front() == L'\n')) t.erase(t.begin());
    while (!t.empty() && (t.back() == L' ' || t.back() == L'\t' || t.back() == L'\r' || t.back() == L'\n')) t.pop_back();

    while (!t.empty() && (t.back() == L'-' || t.back() == L':' || t.back() == L'—' || t.back() == L' ' || t.back() == L'|')) {
        t.pop_back();
        while (!t.empty() && t.back() == L' ') t.pop_back();
    }

    if (t.empty()) {
        if (fallbackIdx >= 0) return i18n::Literal(L"Bölüm ") + std::to_wstring(fallbackIdx + 1);
        return L"";
    }

    bool onlyNum = true;
    for (wchar_t c : t) {
        if (!iswdigit(c) && c != L'.' && c != L' ' && c != L'#') { onlyNum = false; break; }
    }
    if (onlyNum) {
        while (!t.empty() && t.front() == L'#') t.erase(t.begin());
        while (!t.empty() && t.front() == L' ') t.erase(t.begin());
        return i18n::Literal(L"Bölüm ") + t;
    }

    if (t.rfind(L"Chapter ", 0) == 0) t = i18n::Literal(L"Bölüm ") + t.substr(8);
    else if (t.rfind(L"chapter ", 0) == 0) t = i18n::Literal(L"Bölüm ") + t.substr(8);
    else if (t.rfind(L"Ch. ", 0) == 0) t = i18n::Literal(L"Bölüm ") + t.substr(4);
    else if (t.rfind(L"Ch.", 0) == 0) t = i18n::Literal(L"Bölüm ") + t.substr(3);

    size_t sep = t.find_first_of(L":-—|");
    if (sep != std::wstring::npos) {
        std::wstring leftPart = t.substr(0, sep);
        std::wstring rightPart = t.substr(sep + 1);
        while (!leftPart.empty() && leftPart.back() == L' ') leftPart.pop_back();
        while (!rightPart.empty() && rightPart.front() == L' ') rightPart.erase(rightPart.begin());
        while (!rightPart.empty() && rightPart.back() == L' ') rightPart.pop_back();

        if (rightPart.empty()) {
            t = leftPart;
        } else {
            std::wstring normRight = rightPart;
            if (normRight.rfind(L"Chapter ", 0) == 0) normRight = i18n::Literal(L"Bölüm ") + normRight.substr(8);
            else if (normRight.rfind(L"Ch. ", 0) == 0) normRight = i18n::Literal(L"Bölüm ") + normRight.substr(4);
            else if (normRight.rfind(L"Ch.", 0) == 0) normRight = i18n::Literal(L"Bölüm ") + normRight.substr(3);

            bool rightOnlyNum = true;
            for (wchar_t c : rightPart) {
                if (!iswdigit(c) && c != L'.' && c != L' ') { rightOnlyNum = false; break; }
            }

            if (normRight == leftPart || rightOnlyNum) {
                t = leftPart;
            }
        }
    }

    return t;
}

int64_t CurrentEpochMs() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return (int64_t)((uli.QuadPart - 116444736000000000ull) / 10000ull);
}

std::wstring FormatRelativeTime(int64_t epochMs) {
    if (epochMs <= 0) return std::wstring();
    const int64_t nowMs = CurrentEpochMs();
    const int64_t diffSec = (nowMs - epochMs) / 1000;
    if (diffSec < 0) return std::wstring();
    if (diffSec < 60) return T(S::JustNow);
    const int64_t diffMin = diffSec / 60;
    if (diffMin < 60) {
        wchar_t buf[32];
        swprintf_s(buf, T(S::MinsAgoFmt), (int)diffMin);
        return buf;
    }
    const int64_t diffHours = diffMin / 60;
    if (diffHours < 24) {
        wchar_t buf[32];
        swprintf_s(buf, T(S::HoursAgoFmt), (int)diffHours);
        return buf;
    }
    const int64_t diffDays = diffHours / 24;
    if (diffDays == 1) return T(S::Yesterday);
    if (diffDays < 30) {
        wchar_t buf[32];
        swprintf_s(buf, T(S::DaysAgoFmt), (int)diffDays);
        return buf;
    }
    if (diffDays < 365) {
        wchar_t buf[32];
        swprintf_s(buf,i18n::Literal(L"%d ay önce"),(int)(diffDays/30));
        return buf;
    }
    wchar_t buf[32];
    swprintf_s(buf,i18n::Literal(L"%d yıl önce"),(int)(diffDays/365));
    return buf;
}

} // namespace

// ------------------------------------------------------------ veri ----------

const std::vector<std::wstring>& LibraryView::ExistingRecent(const std::vector<std::wstring>& recent) {
    if (m_checkedOnce && recent == m_checkedFor) return m_existing;
    m_checkedOnce = true;
    m_checkedFor = recent;
    m_existing.clear();
    for (const auto& d : recent) {
        if (PathIsNetworkPathW(d.c_str())) { m_existing.push_back(d); continue; }
        const DWORD attr = GetFileAttributesW(d.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) m_existing.push_back(d);
    }
    return m_existing;
}

void LibraryView::RebuildRows(const Library& lib) {
    if (m_rowsRev == lib.Revision() && m_rowsLanguage == i18n::Current()) return;
    m_displayDirty=true;
    const std::string selectedKey = m_selectedRowIndex >= 0 && (size_t)m_selectedRowIndex < m_rows.size()
        ? m_rows[m_selectedRowIndex].key : std::string();
    m_rowsRev = lib.Revision();
    m_rowsLanguage = i18n::Current();
    m_rows.clear();
    const auto& all = lib.All();
    m_rows.reserve(all.size());
    for (size_t i = 0; i < all.size(); ++i) {
        const Series& s = all[i];
        Row row;
        row.index = i;
        row.key = s.key;
        row.mirror = s.mirror;
        row.mangaUrl = s.url;
        row.title = OneLine(FromUtf8(s.displayName.empty() ? s.name : s.displayName), 300);
        if (row.title.empty()) row.title = OneLine(FromUtf8(s.url), 300);
        row.initial = FirstLetter(row.title);

        std::wstring sub = OneLine(FromUtf8(s.mirror), 120);
        std::string rawChapLabel = amr::ChapterLabel(s);
        if (rawChapLabel.empty() && !s.lastChapterName.empty()) rawChapLabel = s.lastChapterName;
        row.chapterLabel = CleanChapterTitle(OneLine(FromUtf8(rawChapLabel), 40));
        const std::wstring chapter = row.chapterLabel;
        if (!chapter.empty()) {
            if (!sub.empty()) sub += L"  ·  ";
            sub += T(S::LastReadLabel);
            sub += L" ";
            sub += chapter;
        }
        std::wstring cats;
        for (const auto& c : s.cats) {
            const std::wstring one = OneLine(FromUtf8(c), 60);
            if (one.empty()) continue;
            if (!cats.empty()) cats += L", ";
            cats += one;
        }
        if (!cats.empty()) {
            if (!sub.empty()) sub += L"  ·  ";
            sub += cats;
        }
        if (sub.size() > 400) {
            sub.resize(IS_HIGH_SURROGATE(sub[399]) ? 399 : 400);
            sub += L"\x2026";
        }
        row.sub = std::move(sub);
        row.lang = OneLine(FromUtf8(s.language), 12);
        CharUpperBuffW(row.lang.data(), (DWORD)row.lang.size());
        row.stopped = s.read != 0;
        row.openUrl = FromUtf8(s.lastChapterUrl.empty() ? s.url : s.lastChapterUrl);
        row.lastChapterUrl = s.lastChapterUrl;
        row.caughtUp = IsCaughtUp(s);
        row.updateFailed = !s.lastUpdateError.empty();
        row.latestChapterUrl = s.latestChapterUrl;
        row.latestChapterName = s.latestChapterName;
        if (!s.latestChapterName.empty()) {
            row.latestChapterLabel = CleanChapterTitle(OneLine(FromUtf8(s.latestChapterName), 40));
        }
        row.upts = s.upts;
        row.lastReadTs = s.lastReadTs;
        row.addedTs = s.addedTs;
        row.normTitle = NormalizeForGroup(row.title);
        row.relUpts = FormatRelativeTime(row.upts);
        row.relLastRead = FormatRelativeTime(row.lastReadTs);

        // Arama dizisi (hepsi kucuk harf)
        row.searchStr = ToLowerInvariant(row.title + L" " + row.sub + L" " + row.lang + L" " + FromUtf8(s.mirror));
        if (!s.chapters.empty()) {
            std::lock_guard<std::mutex> lock(m_chapterMutex);
            auto& cache = m_chapterCache[s.key];
            cache.mirror = s.mirror;
            cache.mangaUrl = s.url;
            cache.chapters.clear();
            const int current = ChapterIndex(s.chapters, s.lastChapterUrl);
            for (size_t ci = 0; ci < s.chapters.size(); ++ci) {
                ChapterItem item;
                item.title = s.chapters[ci].title;
                item.url = s.chapters[ci].url;
                item.isCurrent = (int)ci == current;
                item.isRead = current >= 0 && (int)ci >= current;
                cache.chapters.push_back(std::move(item));
            }
            cache.failed = false;
        }
        m_rows.push_back(std::move(row));
    }
    if (!selectedKey.empty()) {
        m_selectedRowIndex = -1;
        for (size_t i = 0; i < m_rows.size(); ++i)
            if (m_rows[i].key == selectedKey) { m_selectedRowIndex = (int)i; break; }
        if (m_selectedRowIndex < 0) m_cardMode = 0;
    }
}

void LibraryView::RebuildDisplayItems(const Settings& set) {
    const std::wstring q = ToLowerInvariant(m_search.text);
    const bool queryChanged = (q != m_lastQuery);
    const bool sortChanged = (set.librarySort != m_lastSort || set.librarySortDesc != m_lastSortDesc);
    const bool groupChanged = (set.libraryGroup != m_lastGroup);
    const bool rowsChanged = (m_itemsRowsRev != m_rowsRev);
    const auto errorRevision = m_updateErrorRevision.load();
    if (!queryChanged && !sortChanged && !groupChanged && !rowsChanged && !m_displayDirty && m_displayErrorRevision == errorRevision) return;
    m_displayErrorRevision = errorRevision;

    m_displayDirty = false;
    m_itemsRowsRev = m_rowsRev;

    m_lastQuery = q;
    m_lastSort = set.librarySort;
    m_lastSortDesc = set.librarySortDesc;
    m_lastGroup = set.libraryGroup;
    m_items.clear();

    // 1. Filtreleme
    std::vector<size_t> matched;
    matched.reserve(m_rows.size());
    for (size_t i = 0; i < m_rows.size(); ++i) {
        const Row& row = m_rows[i];
        if (!q.empty() && row.searchStr.find(q) == std::wstring::npos) continue;

        if (m_filterNew != 2) {
            bool isNew = (row.upts > row.lastReadTs && row.upts > 0);
            if (m_filterNew == 0 && !isNew) continue;
            if (m_filterNew == 1 && isNew) continue;
        }
        if (m_filterUnread != 2) {
            bool isUnread = (row.lastReadTs == 0 && !row.stopped);
            if (m_filterUnread == 0 && !isUnread) continue;
            if (m_filterUnread == 1 && isUnread) continue;
        }
        if (m_filterOneshot != 2) {
            bool isOneShot = (row.searchStr.find(L"oneshot") != std::wstring::npos ||
                              row.searchStr.find(L"one-shot") != std::wstring::npos);
            if (m_filterOneshot == 0 && !isOneShot) continue;
            if (m_filterOneshot == 1 && isOneShot) continue;
        }
        matched.push_back(i);
    }

    auto compRows = [&](size_t ia, size_t ib) -> bool {
        const Row& a = m_rows[ia];
        const Row& b = m_rows[ib];
        int cmp = 0;
        switch (set.librarySort) {
        case 0: // Guncelleme tarihi (upts)
            if (a.upts != b.upts) cmp = a.upts > b.upts ? -1 : 1;
            break;
        case 1: // Son okunan (lastReadTs)
            if ((a.lastReadTs > 0) != (b.lastReadTs > 0)) {
                return (a.lastReadTs > 0); // okunanlar basa
            }
            if (a.lastReadTs != b.lastReadTs) cmp = a.lastReadTs > b.lastReadTs ? -1 : 1;
            break;
        case 2: // Isim
            cmp = (CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE | SORT_DIGITSASNUMBERS,
                                  a.title.c_str(), (int)a.title.size(), b.title.c_str(), (int)b.title.size(),
                                  nullptr, nullptr, 0) == CSTR_LESS_THAN) ? -1 : 1;
            return set.librarySortDesc ? (cmp < 0) : (cmp > 0);
        case 3: // Eklenme tarihi (addedTs)
            if (a.addedTs != b.addedTs) cmp = a.addedTs > b.addedTs ? -1 : 1;
            break;
        default: break;
        }
        if (cmp == 0) {
            // Esitlik durumunda isme gore dogal sira
            cmp = (CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE | SORT_DIGITSASNUMBERS,
                                  a.title.c_str(), (int)a.title.size(), b.title.c_str(), (int)b.title.size(),
                                  nullptr, nullptr, 0) == CSTR_LESS_THAN) ? -1 : 1;
        }
        return set.librarySortDesc ? (cmp < 0) : (cmp > 0);
    };

    if (!set.libraryGroup) {
        // Duz liste (gruplama kapali)
        std::stable_sort(matched.begin(), matched.end(), compRows);
        m_items.reserve(matched.size());
        for (size_t idx : matched) {
            DisplayItem item;
            item.rowIndex = idx;
            item.caughtUp = m_rows[idx].caughtUp;
            item.updateFailed = m_rows[idx].updateFailed || HasUpdateError(m_rows[idx].key);
            if (!m_rows[idx].mirror.empty()) item.groupMirrors.push_back(m_rows[idx].mirror);
            m_items.push_back(std::move(item));
        }
        return;
    }

    // 2. Gruplama acik: NormalizeForGroup ile anahtarla
    struct GroupDef {
        std::string key;
        std::wstring title;
        std::wstring initial;
        std::vector<size_t> rowIndices;
        int64_t maxUpts = 0;
        int64_t maxLastRead = 0;
        int64_t maxAdded = 0;
    };

    std::map<std::wstring, GroupDef> groupMap;
    for (size_t idx : matched) {
        const Row& r = m_rows[idx];
        const std::wstring& gk = r.normTitle;
        auto& g = groupMap[gk];
        if (g.rowIndices.empty()) {
            g.key = ToUtf8(gk);
            g.title = r.title;
            g.initial = r.initial;
        }
        g.rowIndices.push_back(idx);
        g.maxUpts = std::max(g.maxUpts, r.upts);
        g.maxLastRead = std::max(g.maxLastRead, r.lastReadTs);
        g.maxAdded = std::max(g.maxAdded, r.addedTs);
    }

    std::vector<GroupDef> groups;
    groups.reserve(groupMap.size());
    for (auto& pair : groupMap) {
        // Grup icindeki satirlari sirala
        std::stable_sort(pair.second.rowIndices.begin(), pair.second.rowIndices.end(), compRows);
        // Birincil uyenin basligini al (en cok okunan veya en guncel)
        if (!pair.second.rowIndices.empty()) {
            pair.second.title = m_rows[pair.second.rowIndices[0]].title;
            pair.second.initial = m_rows[pair.second.rowIndices[0]].initial;
        }
        groups.push_back(std::move(pair.second));
    }

    // Gruplari secili olcute gore sirala
    std::stable_sort(groups.begin(), groups.end(), [&](const GroupDef& a, const GroupDef& b) -> bool {
        int cmp = 0;
        switch (set.librarySort) {
        case 0:
            if (a.maxUpts != b.maxUpts) cmp = a.maxUpts > b.maxUpts ? -1 : 1;
            break;
        case 1:
            if ((a.maxLastRead > 0) != (b.maxLastRead > 0)) return (a.maxLastRead > 0);
            if (a.maxLastRead != b.maxLastRead) cmp = a.maxLastRead > b.maxLastRead ? -1 : 1;
            break;
        case 2:
            cmp = (CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE | SORT_DIGITSASNUMBERS,
                                  a.title.c_str(), (int)a.title.size(), b.title.c_str(), (int)b.title.size(),
                                  nullptr, nullptr, 0) == CSTR_LESS_THAN) ? -1 : 1;
            return set.librarySortDesc ? (cmp < 0) : (cmp > 0);
        case 3:
            if (a.maxAdded != b.maxAdded) cmp = a.maxAdded > b.maxAdded ? -1 : 1;
            break;
        default: break;
        }
        if (cmp == 0) {
            cmp = (CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE | SORT_DIGITSASNUMBERS,
                                  a.title.c_str(), (int)a.title.size(), b.title.c_str(), (int)b.title.size(),
                                  nullptr, nullptr, 0) == CSTR_LESS_THAN) ? -1 : 1;
        }
        return set.librarySortDesc ? (cmp < 0) : (cmp > 0);
    });

    // Gruplari duzlestir
    for (const auto& g : groups) {
        if (g.rowIndices.size() == 1) {
            DisplayItem item;
            item.rowIndex = g.rowIndices[0];
            item.caughtUp = m_rows[item.rowIndex].caughtUp;
            item.updateFailed = m_rows[item.rowIndex].updateFailed || HasUpdateError(m_rows[item.rowIndex].key);
            if (!m_rows[g.rowIndices[0]].mirror.empty()) item.groupMirrors.push_back(m_rows[g.rowIndices[0]].mirror);
            m_items.push_back(std::move(item));
        } else {
            const bool expanded = (m_expandedGroups.count(g.key) > 0);
            DisplayItem header;
            header.isGroupHeader = true;
            header.groupKey = g.key;
            header.groupTitle = g.title;
            header.groupInitial = g.initial;
            header.childCount = g.rowIndices.size();
            header.maxTime = (set.librarySort == 1) ? g.maxLastRead : g.maxUpts;
            header.relTime = FormatRelativeTime(header.maxTime);
            header.rowIndex = g.rowIndices[0]; // birincil
            header.expanded = expanded;
            header.caughtUp = true;
            header.updateFailed = true;
            for (const auto& candidate : m_rows) {
                if (ToUtf8(candidate.normTitle) != g.key) continue;
                if (candidate.updateFailed || HasUpdateError(candidate.key)) continue;
                header.updateFailed = false;
                if (!candidate.caughtUp) header.caughtUp = false;
            }
            if (header.updateFailed) header.caughtUp = false;
            for (size_t childIdx : g.rowIndices) {
                const auto& mName = m_rows[childIdx].mirror;
                if (!mName.empty() && std::find(header.groupMirrors.begin(), header.groupMirrors.end(), mName) == header.groupMirrors.end()) {
                    header.groupMirrors.push_back(mName);
                }
            }
            m_items.push_back(std::move(header));

            if (expanded) {
                for (size_t childIdx : g.rowIndices) {
                    DisplayItem child;
                    child.isChild = true;
                    child.groupKey = g.key;
                    child.groupTitle = g.title;
                    child.rowIndex = childIdx;
                    child.caughtUp = m_rows[childIdx].caughtUp;
                    child.updateFailed = m_rows[childIdx].updateFailed || HasUpdateError(m_rows[childIdx].key);
                    if (!m_rows[childIdx].mirror.empty()) child.groupMirrors.push_back(m_rows[childIdx].mirror);
                    m_items.push_back(std::move(child));
                }
            }
        }
    }
}

// ------------------------------------------------------------ kaydirma ------

float LibraryView::ClampScroll(float listH, float contentH) {
    const float maxS = std::max(0.0f, contentH - listH);
    float& sc = m_scroll[m_tab];
    sc = std::clamp(sc, 0.0f, maxS);
    m_maxScroll[m_tab] = maxS;
    return sc;
}

void LibraryView::OnWheel(float notches) {
    if(m_tab==kLocal && !m_localSelected.empty()){m_localDetailScroll=std::clamp(m_localDetailScroll-notches*86.0f,0.0f,m_localDetailMaxScroll);return;}
    if (m_chDropdownOpen) {
        m_chDropdownScroll = std::clamp(m_chDropdownScroll - notches * 42.0f, 0.0f, m_chDropdownMaxScroll);
        return;
    }
    if (m_selectedRowIndex >= 0 && m_cardMode == 1) {
        m_chapterScroll = std::clamp(m_chapterScroll - notches * 42.0f, 0.0f, m_chapterMaxScroll);
        return;
    }
    if(m_selectedRowIndex>=0 && m_cardMode==0){m_cardSourcesScroll=std::clamp(m_cardSourcesScroll-notches*54.0f,0.0f,m_cardSourcesMaxScroll);return;}
    float& sc = m_scroll[m_tab];
    sc = std::clamp(sc - notches * m_pitch * 2.0f, 0.0f, m_maxScroll[m_tab]);
    if(m_tab==kSearch && m_activeSearch) m_activeSearch->scroll=sc;
}

bool LibraryView::OnKey(UINT vk, bool ctrl, bool shift) {
    if(m_tab==kLocal && !m_localSelected.empty()){if(vk==VK_ESCAPE){if(!m_localDeletePath.empty())m_localDeletePath.clear();else m_localSelected.clear();}return true;}
    if(m_tab==kLocal && m_localSearch.focused){if(vk==VK_ESCAPE){m_localSearch.focused=false;return true;}if(m_localSearch.OnKey(vk,ctrl,shift))return true;}
    if (m_chDropdownOpen) {
        if (vk == VK_ESCAPE) {
            m_chDropdownOpen = false;
            m_chDropdownSearch.focused = false;
            return true;
        }
        if (m_chDropdownSearch.focused) {
            if (m_chDropdownSearch.OnKey(vk, ctrl, shift)) return true;
        }
        if (!m_chDropdownSearch.focused) {
            if (vk == VK_UP) {
                m_chDropdownScroll = std::clamp(m_chDropdownScroll - 38.0f, 0.0f, m_chDropdownMaxScroll);
                return true;
            }
            if (vk == VK_DOWN) {
                m_chDropdownScroll = std::clamp(m_chDropdownScroll + 38.0f, 0.0f, m_chDropdownMaxScroll);
                return true;
            }
            if (vk == VK_PRIOR) {
                m_chDropdownScroll = std::clamp(m_chDropdownScroll - 190.0f, 0.0f, m_chDropdownMaxScroll);
                return true;
            }
            if (vk == VK_NEXT) {
                m_chDropdownScroll = std::clamp(m_chDropdownScroll + 190.0f, 0.0f, m_chDropdownMaxScroll);
                return true;
            }
        }
    }
    if (m_selectedRowIndex >= 0) {
        if (m_cardMode == 1 && m_chapterSearch.focused) {
            if (m_chapterSearch.OnKey(vk, ctrl, shift)) return true;
            if (vk == VK_ESCAPE) {
                m_chapterSearch.focused = false;
                return true;
            }
        }
        if (vk == VK_ESCAPE) {
            if (m_cardMode == 1) {
                m_cardMode = 0;
            } else {
                m_selectedRowIndex = -1;
            }
            return true;
        }
        if (m_cardMode == 1 && !m_chapterSearch.focused) {
            if (vk == VK_UP) {
                m_chapterScroll = std::clamp(m_chapterScroll - 42.0f, 0.0f, m_chapterMaxScroll);
                return true;
            }
            if (vk == VK_DOWN) {
                m_chapterScroll = std::clamp(m_chapterScroll + 42.0f, 0.0f, m_chapterMaxScroll);
                return true;
            }
            if (vk == VK_PRIOR) {
                m_chapterScroll = std::clamp(m_chapterScroll - 200.0f, 0.0f, m_chapterMaxScroll);
                return true;
            }
            if (vk == VK_NEXT) {
                m_chapterScroll = std::clamp(m_chapterScroll + 200.0f, 0.0f, m_chapterMaxScroll);
                return true;
            }
            if (vk == VK_HOME) {
                m_chapterScroll = 0.0f;
                return true;
            }
            if (vk == VK_END) {
                m_chapterScroll = m_chapterMaxScroll;
                return true;
            }
        }
    }
    if (m_resetConfirmStep > 0) {
        if (vk == VK_ESCAPE) {
            m_resetConfirmStep = 0;
            return true;
        }
    }
    if (m_tab == kSearch && m_activeSearch && m_activeSearch->input.focused) {
        if (vk == VK_ESCAPE) {
            m_activeSearch->input.focused = false;
            return true;
        }
        if (m_activeSearch->input.OnKey(vk, ctrl, shift)) return true;
    }
    if (m_tab != kSearch && m_tab != kLocal && m_selectedRowIndex < 0 && !m_chDropdownOpen && m_search.focused) {
        if (m_search.OnKey(vk, ctrl, shift)) return true;
        if (vk == VK_ESCAPE) {
            m_search.focused = false;
            return true;
        }
    }
    float& sc = m_scroll[m_tab];
    const float page = std::max(m_pitch, m_listH * 0.9f);
    switch (vk) {
    case VK_HOME:  sc = 0.0f; break;
    case VK_END:   sc = m_maxScroll[m_tab]; break;
    case VK_PRIOR: sc -= page; break;
    case VK_NEXT:  sc += page; break;
    case VK_UP:    sc -= m_pitch; break;
    case VK_DOWN:  sc += m_pitch; break;
    default: return false;
    }
    sc = std::clamp(sc, 0.0f, m_maxScroll[m_tab]);
    if (m_tab == kSearch && m_activeSearch) m_activeSearch->scroll = sc;
    return true;
}

bool LibraryView::OnChar(wchar_t c) {
    if(m_tab==kLocal && m_localSearch.focused && m_localSelected.empty())return m_localSearch.OnChar(c);
    if (m_resetConfirmStep > 0) return true;
    if (m_chDropdownOpen && m_chDropdownSearch.focused) {
        if (m_chDropdownSearch.OnChar(c)) {
            m_chDropdownScroll = 0.0f;
            return true;
        }
        return false;
    }
    if (m_selectedRowIndex >= 0 && m_cardMode == 1 && m_chapterSearch.focused) {
        return m_chapterSearch.OnChar(c);
    }
    if (m_tab == kSearch && m_activeSearch && m_activeSearch->input.focused) {
        return m_activeSearch->input.OnChar(c);
    }
    if (m_tab != kSearch && m_tab != kLocal && m_selectedRowIndex < 0 && !m_chDropdownOpen && m_search.focused) {
        return m_search.OnChar(c);
    }
    return false;
}

void LibraryView::FocusSearch() {
    m_tab = kSeries;
    m_search.focused = true;
    m_search.SelectAll();
}

void LibraryView::ShowReadingList() {
    m_tab = kSeries;
    m_scroll[kSeries] = 0.0f;
}

void LibraryView::DrawScrollbar(Renderer& r, Ui& ui, const D2D1_RECT_F& list, float contentH, ShellHost& host) {
    const float listH = RcH(list);
    if (contentH <= listH + 0.5f || listH <= 0.0f) return;
    const float s = r.Scale();
    const float maxS = contentH - listH;
    const float barW = 10.0f * s;
    const float thumbH = std::max(36.0f * s, listH * (listH / contentH));
    float& sc = m_scroll[m_tab];
    sc = std::clamp(sc, 0.0f, maxS);

    const float thumbY = list.top + (listH - thumbH) * (sc / maxS);
    const float x = list.right + 6.0f * s;
    const D2D1_RECT_F trackRc = RcWH(x, list.top, barW, listH);
    const D2D1_RECT_F thumbRc = RcWH(x, thumbY, barW, thumbH);

    // Genisletilmis etkilesim ve surukleme alani
    const D2D1_RECT_F hitRc = RcWH(x - 4.0f * s, list.top, barW + 8.0f * s, listH);
    const bool isHot = RcHit(hitRc, ui.In().mx, ui.In().my);

    if (ui.In().pressed && isHot) {
        m_scrollDragging = true;
        m_scrollDragStartY = ui.In().my;
        if (RcHit(thumbRc, ui.In().mx, ui.In().my)) {
            m_scrollDragStartVal = sc;
        } else {
            const float targetSc = std::clamp((ui.In().my - list.top - thumbH * 0.5f) / (listH - thumbH) * maxS, 0.0f, maxS);
            sc = targetSc;
            m_scrollDragStartVal = targetSc;
            host.Invalidate();
        }
    }

    if (m_scrollDragging) {
        if (ui.In().down) {
            const float dy = ui.In().my - m_scrollDragStartY;
            const float newSc = std::clamp(m_scrollDragStartVal + dy * (maxS / (listH - thumbH)), 0.0f, maxS);
            if (std::abs(newSc - sc) > 0.5f) {
                sc = newSc;
                host.Invalidate();
            }
        } else {
            m_scrollDragging = false;
        }
    }

    // Cizim: Genisletilmis belirgin arkalik ve kolayca tutulabilir modern cubuk
    r.FillRound(trackRc, barW * 0.5f, theme::Panel2, 0.45f);
    r.StrokeRound(trackRc, barW * 0.5f, theme::Border, 1.0f * s, 0.35f);

    const float thumbAlpha = m_scrollDragging ? 0.95f : (isHot ? 0.80f : 0.50f);
    const uint32_t thumbColor = m_scrollDragging ? theme::Active : (isHot ? theme::Text : theme::TextMuted);
    r.FillRound(thumbRc, barW * 0.5f, thumbColor, thumbAlpha);
}

// ------------------------------------------------------------ cizim ---------

void LibraryView::Draw(Renderer& r, Ui& ui, const D2D1_RECT_F& area, Settings& set, Library& lib,
                       ShellHost& host, double now) {
    const float s = r.Scale();
    w::SetScale(s);
    r.Fill(area, theme::Ground, 1.0f);

    if (m_lastScale > 0.0f && s != m_lastScale) {
        const float k = s / m_lastScale;
        for (float& v : m_scroll) v *= k;
        for (float& v : m_maxScroll) v *= k;
        m_pitch *= k;
        m_listH *= k;
    }
    m_lastScale = s;

    RebuildRows(lib);
    RebuildDisplayItems(set);
    if (m_chDropdownOpen) {
        for (const auto& row : m_rows) if (row.key == m_chDropdownRowKey) { m_chDropdownRow = row; break; }
    }

    if(m_tab==kLocal) RebuildLocal(set, lib);
    const std::vector<std::wstring>& recent = ExistingRecent(set.recent);
    const bool hasSeries = !m_rows.empty();
    const bool hasLocal = !recent.empty() || !m_localGroups.empty();
    // Search, sources and diagnostics remain available with an empty library.
    if (!hasSeries && !hasLocal && m_tab == kSeries) m_tab = kSearch;

    const bool modalOpen = (m_selectedRowIndex >= 0 && (size_t)m_selectedRowIndex < m_rows.size());
    if (modalOpen || (m_tab == kLocal && !m_localSelected.empty())) {
        // Alttaki bilesenlerin (toolbar, butonlar, satirlar) tiklanmasini engelle
        ui.Occlude(area);
    }

    const float padX = 32.0f * s;
    const float maxW = 1680.0f * s;
    const float contentW = std::min(RcW(area) - padX * 2.0f, maxW);
    const float left = area.left + (RcW(area) - contentW) * 0.5f;
    const float right = left + contentW;
    float y = area.top + 24.0f * s;

    // Baslik & Butonlar
    const float btnH = 34.0f * s;
    const float openW = 110.0f * s;
    const float arcW = 135.0f * s;
    const float impW = 150.0f * s;
    const float gap = 8.0f * s;

    const D2D1_RECT_F openBtn = Rc(right - openW, y + 1 * s, right, y + 1 * s + btnH);
    const D2D1_RECT_F arcBtn = Rc(openBtn.left - gap - arcW, y + 1 * s, openBtn.left - gap, y + 1 * s + btnH);
    const D2D1_RECT_F importBtn = Rc(arcBtn.left - gap - impW, y + 1 * s, arcBtn.left - gap, y + 1 * s + btnH);

    r.Text(T(S::Library), Rc(left, y, importBtn.left - 16 * s, y + 34 * s), theme::Text, 24.0f * s,
           Renderer::Align::Left, Renderer::Weight::Semibold);

    if (w::TextButton(r, ui, Id("lib.import"), importBtn, T(S::ImportAmr), !hasSeries)) host.ImportAmrDialog();
    if (w::TextButton(r, ui, Id("lib.arc"), arcBtn, i18n::Literal(L"Arşiv Aç (.cbz)"), false)) host.OpenArchiveDialog();
    if (w::TextButton(r, ui, Id("lib.open"), openBtn, T(S::OpenLocal), false)) host.OpenFolderDialog();
    y += 38.0f * s;

    std::wstring subTitle;
    switch (m_tab) {
    case kSeries: subTitle = T(S::LibrarySeriesSubtitle); break;
    case kLocal: subTitle = i18n::Literal(L"Manga adı ve bölümleriyle yerel arşivin; en son indirilenler üstte."); break;
    case kBookmarks: subTitle = i18n::Literal(L"Kaydedilen manga ve bölüm yer imleri"); break;
    case kSites: subTitle = i18n::Literal(L"Desteklenen çevrimiçi manga ve webtoon aynaları"); break;
    case kHealth: subTitle = i18n::Literal(L"Kaynakların son arama, bölüm ve resim denetimleri; erişim hataları ve nedenleri"); break;
    case kSearch: subTitle = i18n::Literal(L"Tüm çevrimiçi kaynaklarda manga ara, eşleşenleri grupla ve tek tıkla oku veya kütüphaneye ekle"); break;
    }
    r.Text(subTitle, Rc(left, y, right, y + 22 * s), theme::TextMuted, 13.0f * s);
    y += 34.0f * s;

    // Sekmeler
    std::vector<std::wstring> tabLabels;
    wchar_t a[96], b[96], c[96], d[96];
    swprintf_s(a, L"%s  %zu", T(S::ReadingList), m_rows.size());
    tabLabels.push_back(a);
    swprintf_s(b, L"%s  %zu", T(S::LocalFolders), m_localEntries.size());
    tabLabels.push_back(b);
    swprintf_s(c, i18n::Literal(L"Yer İmleri  %zu"), lib.Bookmarks().size());
    tabLabels.push_back(c);
    swprintf_s(d, i18n::Literal(L"Siteler  %zu"), engine::MirrorEngine::Instance().GetMirrorsCount());
    tabLabels.push_back(d);
    tabLabels.push_back(i18n::Literal(L"🔍 Manga Ara"));
    tabLabels.push_back(i18n::Literal(L"Kaynak Durumu"));

    const int activeSegment = std::clamp(m_tab, 0, 5);
    const float tabW = std::min(1040.0f * s, (right - left));
    const int pick = w::Segmented(r, ui, Id("lib.tabs"), Rc(left, y, left + tabW, y + 36 * s), tabLabels, activeSegment);
    if (pick >= 0 && pick != activeSegment) {
        m_tab = pick;
        host.Invalidate();
    }
    y += 48.0f * s;

    // Arama ve Siralama Toolbar'i (Okuma listesi veya Siteler aktifken)
    if (m_tab == kSeries) {
        const D2D1_RECT_F tbRc = Rc(left, y, right, y + 36.0f * s);
        DrawToolbar(r, ui, tbRc, set, lib, host, now);
        y += 44.0f * s;
    }

    if (m_sortMenuOpen && (m_tab == kSeries || m_tab == kSites)) {
        const float menuW = 190.0f * s;
        const float itemH = 34.0f * s;
        const float menuH = 4 * itemH + 8.0f * s;
        const D2D1_RECT_F menuRc = RcWH(m_sortBtnRc.left, m_sortBtnRc.bottom + 4.0f * s, menuW, menuH);
        ui.Occlude(menuRc);
    }

    const D2D1_RECT_F list = Rc(left, y, right, area.bottom - 44.0f * s);
    if (m_tab == kSeries) DrawSeriesList(r, ui, list, set, lib, host);
    else if (m_tab == kLocal) DrawLocalList(r, ui, list, recent, host, set, lib);
    else if (m_tab == kBookmarks) DrawBookmarkList(r, ui, list, lib.Bookmarks(), host);
    else if (m_tab == kSites) DrawSitesList(r, ui, list, host, set);
    else if (m_tab == kSearch) DrawSearchTab(r, ui, list, host, set, lib, now);
    else if (m_tab == kHealth) DrawSourceHealth(r, ui, list, host);

    r.Text(T(S::DropHint), Rc(left, area.bottom - 36 * s, right, area.bottom - 12 * s), theme::TextMuted, 12.0f * s,
           Renderer::Align::Center);

    if (m_sortMenuOpen && (m_tab == kSeries || m_tab == kSites)) {
        ui.ClearOcclusion();
        DrawSortMenu(r, ui, set, host);
    }

    const bool isModalNow = (m_selectedRowIndex >= 0 && (size_t)m_selectedRowIndex < m_rows.size());
    if (isModalNow) {
        // Modal cizilmeden once occlusion'i temizle ki kart ve dugmeleri tiklanabilsin
        ui.ClearOcclusion();
        DrawDetailCard(r, ui, area, lib, host, set);
    }

    if (m_chDropdownOpen) {
        ui.ClearOcclusion();
        DrawRowChapterDropdown(r, ui, area, host, now);
    }

    if (m_resetConfirmStep > 0) {
        ui.ClearOcclusion();
        DrawResetConfirmModal(r, ui, area, lib, set, host);
    }
}

void LibraryView::DrawToolbar(Renderer& r, Ui& ui, const D2D1_RECT_F& rc, Settings& set, Library& lib,
                             ShellHost& host, double now) {
    const float s = r.Scale();
    const float btnH = RcH(rc);

    // Sagdan sola buton genislikleri
    const float resetBtnW = (m_tab == kSeries) ? 86.0f * s : 0.0f;
    const float checkBtnW = (m_tab == kSeries) ? 90.0f * s : 0.0f;
    const float optBtnW = (m_tab == kSeries) ? 90.0f * s : 0.0f;
    const float viewBtnW = (m_tab == kSeries) ? 104.0f * s : 0.0f;
    const float groupBtnW = 96.0f * s;
    const float dirBtnW = 36.0f * s;
    const float sortBtnW = 160.0f * s;
    const float gap = 6.0f * s;

    const D2D1_RECT_F resetRc = (m_tab == kSeries) ? Rc(rc.right - resetBtnW, rc.top, rc.right, rc.bottom) : Rc(0, 0, 0, 0);
    const float checkRight = (m_tab == kSeries) ? (resetRc.left - gap) : rc.right;
    const D2D1_RECT_F checkRc = (m_tab == kSeries) ? Rc(checkRight - checkBtnW, rc.top, checkRight, rc.bottom) : Rc(0, 0, 0, 0);
    const float optRight = (m_tab == kSeries) ? (checkRc.left - gap) : rc.right;
    const D2D1_RECT_F optRc = (m_tab == kSeries) ? Rc(optRight - optBtnW, rc.top, optRight, rc.bottom) : Rc(0, 0, 0, 0);
    const float viewRight = (m_tab == kSeries) ? (optRc.left - gap) : rc.right;
    const D2D1_RECT_F viewRc = (m_tab == kSeries) ? Rc(viewRight - viewBtnW, rc.top, viewRight, rc.bottom) : Rc(0, 0, 0, 0);
    const float rightSide = (m_tab == kSeries) ? (viewRc.left - gap) : rc.right;
    const D2D1_RECT_F groupRc = Rc(rightSide - groupBtnW, rc.top, rightSide, rc.bottom);
    const D2D1_RECT_F dirRc = Rc(groupRc.left - gap - dirBtnW, rc.top, groupRc.left - gap, rc.bottom);
    const D2D1_RECT_F sortRc = Rc(dirRc.left - gap - sortBtnW, rc.top, dirRc.left - gap, rc.bottom);
    const D2D1_RECT_F searchRc = Rc(rc.left, rc.top, sortRc.left - gap, rc.bottom);

    // 1. Arama kutusu
    if (m_search.Draw(r, ui, Id("lib.searchbox"), searchRc, T(S::SearchPlaceholder), true, now)) {
        RebuildDisplayItems(set);
        host.Invalidate();
    }

    // 2. Siralama secici dugmesi
    const wchar_t* sortNames[] = { T(S::SortUpdate), T(S::SortLastRead), T(S::SortName), T(S::SortAdded) };
    const int curSort = std::clamp(set.librarySort, 0, 3);
    wchar_t sortLabel[64];
    swprintf_s(sortLabel, L"%s: %s ▾", T(S::SortBy), sortNames[curSort]);

    m_sortBtnRc = sortRc;
    if (w::TextButton(r, ui, Id("lib.sortbtn"), sortRc, sortLabel, false)) {
        m_sortMenuOpen = !m_sortMenuOpen;
    }

    // 3. Yon (Artan / Azalan) dugmesi
    const std::wstring dirLabel = set.librarySortDesc ? L"↓" : L"↑";
    if (w::TextButton(r, ui, Id("lib.dirbtn"), dirRc, dirLabel, false)) {
        set.librarySortDesc = !set.librarySortDesc;
        set.Save();
        RebuildDisplayItems(set);
        host.Invalidate();
    }
    ui.Tip(Id("lib.dirbtn"), set.librarySortDesc ? T(S::Descending) : T(S::Ascending), dirRc);

    // 4. Gruplama ac/kapa dugmesi
    if (w::TextButton(r, ui, Id("lib.groupbtn"), groupRc, T(S::GroupByName), set.libraryGroup)) {
        set.libraryGroup = !set.libraryGroup;
        set.Save();
        RebuildDisplayItems(set);
        host.Invalidate();
    }

    // 5. Gorunum Kipi (Liste / Kart görünümü) ve Diger Dugmeler
    if (m_tab == kSeries) {
        const wchar_t* viewLabel = (set.libraryViewMode == 1) ? i18n::Literal(L"☷ Kartlar") : i18n::Literal(L"☰ Liste");
        if (w::TextButton(r, ui, Id("lib.viewbtn"), viewRc, viewLabel, set.libraryViewMode == 1)) {
            set.libraryViewMode = (set.libraryViewMode == 0) ? 1 : 0;
            set.Save();
            m_displayDirty = true;
            host.Invalidate();
        }
        ui.Tip(Id("lib.viewbtn"), i18n::Literal(L"Görünümü değiştir (Liste / Kart görünümü)"), viewRc);

        if (w::TextButton(r, ui, Id("lib.optbtn"), optRc, i18n::Literal(L"⚡ Optimize"), false)) {
            host.ConvertAndOptimizeLibrary();
            m_rowsRev = 0;
            m_displayDirty = true;
            host.Invalidate();
        }
        ui.Tip(Id("lib.optbtn"), i18n::Literal(L"Kütüphaneyi temizler, yinelenenleri birleştirir ve hızlandırır"), optRc);

        if (w::TextButton(r, ui, Id("lib.checkbtn"), checkRc, i18n::Literal(L"↻ Denetle"), false)) {
            host.CheckUpdatesNow();
        }
        ui.Tip(Id("lib.checkbtn"), i18n::Literal(L"Yeni bölümleri hemen kontrol et"), checkRc);

        if (w::TextButton(r, ui, Id("lib.resetbtn"), resetRc, i18n::Literal(L"🗑️ Sıfırla"), false)) {
            m_resetConfirmStep = 1;
            host.Invalidate();
        }
        ui.Tip(Id("lib.resetbtn"), i18n::Literal(L"Tüm kütüphaneyi ve okuma geçmişini sıfırla (2 aşamalı güvenlik onayı)"), resetRc);
    }
}

void LibraryView::DrawSortMenu(Renderer& r, Ui& ui, Settings& set, ShellHost& host) {
    const float s = r.Scale();
    const wchar_t* sortNames[] = { T(S::SortUpdate), T(S::SortLastRead), T(S::SortName), T(S::SortAdded) };
    const int curSort = std::clamp(set.librarySort, 0, 3);
    const float menuW = 190.0f * s;
    const float itemH = 34.0f * s;
    const float menuH = 4 * itemH + 8.0f * s;
    const D2D1_RECT_F menuRc = RcWH(m_sortBtnRc.left, m_sortBtnRc.bottom + 4.0f * s, menuW, menuH);

    r.FillRound(menuRc, 8.0f * s, theme::Elevated, 1.0f);
    r.StrokeRound(menuRc, 8.0f * s, theme::Border, 1.0f * s, 0.9f);

    for (int i = 0; i < 4; ++i) {
        const D2D1_RECT_F itemRc = RcWH(menuRc.left + 4.0f * s, menuRc.top + 4.0f * s + (float)i * itemH,
                                        menuW - 8.0f * s, itemH);
        const uint32_t itemId = Id("lib.sortitem", (uint32_t)i);
        const bool clicked = ui.Button(itemId, itemRc);
        const bool isCur = (curSort == i);

        if (ui.Hot(itemId)) r.FillRound(itemRc, 6.0f * s, theme::White, 0.12f);
        if (isCur) r.FillRound(itemRc, 6.0f * s, theme::Active, 0.18f);

        r.Text(isCur ? L"✓ " : L"   ", Rc(itemRc.left + 6 * s, itemRc.top, itemRc.left + 24 * s, itemRc.bottom),
               theme::Accent, 13.0f * s, Renderer::Align::Left, Renderer::Weight::Bold);
        r.Text(sortNames[i], Rc(itemRc.left + 24 * s, itemRc.top, itemRc.right - 8 * s, itemRc.bottom),
               isCur ? theme::White : theme::Text, 13.0f * s, Renderer::Align::Left,
               isCur ? Renderer::Weight::Semibold : Renderer::Weight::Normal);

        if (clicked) {
            set.librarySort = i;
            m_sortMenuOpen = false;
            set.Save();
            RebuildDisplayItems(set);
            host.Invalidate();
        }
    }

    // Menu disina basilinca kapat
    if (ui.In().pressed && !RcHit(menuRc, ui.In().mx, ui.In().my) && !RcHit(m_sortBtnRc, ui.In().mx, ui.In().my)) {
        m_sortMenuOpen = false;
    }
}

void LibraryView::DrawSeriesList(Renderer& r, Ui& ui, const D2D1_RECT_F& list, Settings& set, Library& lib,
                                ShellHost& host) {
    if (set.libraryViewMode == 1) {
        DrawCardGrid(r, ui, list, set, lib, host);
        return;
    }

    const float s = r.Scale();
    const float rowH = 64.0f * s, gap = 8.0f * s, pitch = rowH + gap;
    m_pitch = pitch;
    m_listH = RcH(list);

    if (m_items.empty()) {
        r.PushClip(list);
        r.Text(T(S::NoSearchResults), Rc(list.left, list.top + 40 * s, list.right, list.top + 80 * s),
               theme::TextMuted, 14.0f * s, Renderer::Align::Center);
        r.PopClip();
        return;
    }

    const float contentH = (float)m_items.size() * pitch - gap;
    const float scroll = ClampScroll(RcH(list), contentH);

    r.PushClip(list);
    std::string toggleGroupKey;
    const size_t first = (size_t)std::max(0.0f, std::floor(scroll / pitch));
    for (size_t i = first; i < m_items.size(); ++i) {
        const float top = list.top + (float)i * pitch - scroll;
        if (top >= list.bottom) break;

        const DisplayItem& item = m_items[i];
        const Row& row = m_rows[item.rowIndex];

        // 1. GRUP BASLIGI
        if (item.isGroupHeader) {
            const D2D1_RECT_F rc = Rc(list.left, top, list.right, top + rowH);
            const D2D1_RECT_F hit = Rc(rc.left, std::max(rc.top, list.top), rc.right, std::min(rc.bottom, list.bottom));
            const uint32_t id = Id("lib.group", Id(item.groupKey.c_str()));
            const bool clicked = RcH(hit) > 1.0f && ui.Button(id, hit);
            const bool hot = ui.Hot(id);

            // Eğer KAPALI ise: Arkaya 2 katman kart efekti ver (kartlık gibi görünüm)
            if (!item.expanded) {
                const D2D1_RECT_F deck2 = Rc(rc.left + 5.0f * s, rc.top - 5.0f * s, rc.right - 5.0f * s, rc.bottom - 5.0f * s);
                r.FillRound(deck2, 12.0f * s, 0x1A1C24, 0.65f);
                r.StrokeRound(deck2, 12.0f * s, theme::Border, 1.0f * s, 0.45f);

                const D2D1_RECT_F deck1 = Rc(rc.left + 2.5f * s, rc.top - 2.5f * s, rc.right - 2.5f * s, rc.bottom - 2.5f * s);
                r.FillRound(deck1, 12.0f * s, 0x222630, 0.85f);
                r.StrokeRound(deck1, 12.0f * s, theme::Border, 1.0f * s, 0.65f);
            }

            r.FillRound(rc, 12.0f * s, item.updateFailed ? theme::UpdateErrorTone : item.caughtUp ? theme::ReadTone : theme::UnreadTone, 1.0f);
            if (ui.Pressed(id)) r.FillRound(rc, 12.0f * s, theme::Press, theme::PressAlpha);
            else if (hot) r.FillRound(rc, 12.0f * s, theme::White, 0.06f);
            if (item.expanded) r.StrokeRound(rc, 12.0f * s, theme::Accent, 1.5f * s, 0.85f);
            else r.StrokeRound(rc, 12.0f * s, theme::Border, 1.0f * s, 0.70f);

            // Sol ikon: Zarif ac/kapa oku - dikey ortalanmis
            const float arrowW = 28.0f * s;
            const D2D1_RECT_F leftArrowRc = RcWH(rc.left + 14.0f * s, rc.top + (rowH - 24.0f * s) * 0.5f, arrowW, 24.0f * s);
            r.Text(item.expanded ? L"▼" : L"▶", leftArrowRc, theme::Accent, 14.0f * s, Renderer::Align::Center, Renderer::Weight::Bold);

            // Sag kisim: Kaynak sayisi pill'i (örn. 3 Kaynak)
            wchar_t countStr[32];
            swprintf_s(countStr, T(S::SourcesCountFmt), (int)item.childCount);
            const float tw = r.MeasureText(countStr, 11.0f * s) + 16.0f * s;
            const D2D1_RECT_F pillRc = RcWH(rc.right - tw - 16.0f * s, rc.top + (rowH - 24 * s) * 0.5f, tw, 24 * s);
            r.FillRound(pillRc, 12.0f * s, theme::Elevated, 1.0f);
            r.Text(countStr, pillRc, theme::Accent, 11.0f * s, Renderer::Align::Center, Renderer::Weight::Semibold);

            // Hizli Oku butonu (⚡ Oku) - dikey ortalanmis
            const float btnH = 28.0f * s;
            const float by = rc.top + (rowH - btnH) * 0.5f;
            const float rdBtnW = 66.0f * s;
            const D2D1_RECT_F rdRc = RcWH(pillRc.left - 8.0f * s - rdBtnW, by, rdBtnW, btnH);
            bool gActionClicked = false;
            const uint32_t rdId = Id("lib.grp.rd", Id(item.groupKey.c_str()));
            if (w::TextButton(r, ui, rdId, rdRc, i18n::Literal(L"⚡ Oku"), true)) {
                gActionClicked = true;

                if (!row.mirror.empty() && !row.mangaUrl.empty()) {
                    host.OpenOnlineManga(row.mirror, row.mangaUrl, row.title, !row.lastChapterUrl.empty() ? row.lastChapterUrl : "__FIRST__");
                } else {
                    host.OpenUrl(row.openUrl);
                }
            }

            // Baslik ve metin blogu - dikey tam ortalanmis
            const float tx = leftArrowRc.right + 10.0f * s;
            const float tr = rdRc.left - 12.0f * s;
            const float textBlockH = 38.0f * s;
            const float textTop = rc.top + (rowH - textBlockH) * 0.5f;
            const float titleW = r.MeasureText(item.groupTitle, 15.0f * s, Renderer::Weight::Semibold);
            const float titleRight = std::min(tx + titleW, tr - 60.0f * s);
            r.Text(item.groupTitle, Rc(tx, textTop, titleRight, textTop + 20.0f * s), theme::Text,
                   15.0f * s, Renderer::Align::Left, Renderer::Weight::Semibold);

            // Gruptaki tum kaynaklarin rozetlerini goster
            float bX = titleRight + 8.0f * s;
            const float bH = 18.0f * s;
            const float bY = textTop + 1.0f * s;
            for (const auto& mName : item.groupMirrors) {
                if (mName.empty()) continue;
                std::wstring mW = FromUtf8(mName);
                const float bW = r.MeasureText(mW, 10.5f * s, Renderer::Weight::Semibold) + 12.0f * s;
                if (bX + bW > tr) break;
                DrawMirrorBadge(r, mName, RcWH(bX, bY, bW, bH), s);
                bX += bW + 5.0f * s;
            }

            std::wstring groupSub = row.sub;
            if (!item.relTime.empty()) groupSub += L"  ·  " + item.relTime;
            r.Text(groupSub, Rc(tx, textTop + 20.0f * s, tr, textTop + 38.0f * s), theme::TextMuted, 12.5f * s);

            if (clicked && !gActionClicked) {
                toggleGroupKey = item.groupKey;
            }
            continue;
        }

        // 2. GRUP COCUK SATIRI VEYA TEKIL SATIR
        const float indent = item.isChild ? 32.0f * s : 0.0f;
        const D2D1_RECT_F rc = Rc(list.left + indent, top, list.right, top + rowH);
        const D2D1_RECT_F hit = Rc(rc.left, std::max(rc.top, list.top), rc.right, std::min(rc.bottom, list.bottom));
        const uint32_t rowKeyHash = Id(row.key.c_str());
        const uint32_t id = Id("lib.series", rowKeyHash);
        const bool clicked = RcH(hit) > 1.0f && ui.Button(id, hit);
        const bool hot = ui.Hot(id);

        r.FillRound(rc, 12.0f * s, item.updateFailed ? theme::UpdateErrorTone : item.caughtUp ? theme::ReadTone : theme::UnreadTone, 1.0f);
        if (ui.Pressed(id)) r.FillRound(rc, 12.0f * s, theme::Press, theme::PressAlpha);
        else if (hot) r.FillRound(rc, 12.0f * s, theme::White, 0.06f);

        // Ikon kutusu (ayna ozel rengiyle)
        const D2D1_RECT_F box = RcWH(rc.left + 12 * s, rc.top + 12 * s, 40 * s, 40 * s);
        const uint32_t mCol = GetMirrorColor(row.mirror);
        r.FillRound(box, 10.0f * s, mCol, 0.16f);
        r.StrokeRound(box, 10.0f * s, mCol, 1.0f * s, 0.35f);
        if (item.isChild) {
            DrawIcon(r, Icon::Globe, box, 18.0f * s, mCol);
        } else {
            r.Text(row.initial, box, row.stopped ? theme::TextMuted : mCol, 18.0f * s,
                   Renderer::Align::Center, Renderer::Weight::Semibold);
        }

        // Sag uc: Eylem butonlari ve etiketler (HER ZAMAN GORUNUR)
        float tagsLeft = rc.right - 12.0f * s;
        bool actionClicked = false;
        const float btnH = 28.0f * s;
        const float by = rc.top + (rowH - btnH) * 0.5f;

        // 1. Oku butonu (⚡ Oku)
        const float rdBtnW = 66.0f * s;
        const D2D1_RECT_F rdRc = RcWH(tagsLeft - rdBtnW, by, rdBtnW, btnH);
        if (w::TextButton(r, ui, Id("lib.row.rd", rowKeyHash), rdRc, i18n::Literal(L"⚡ Oku"), true)) {
            actionClicked = true;

            if (!row.mirror.empty() && !row.mangaUrl.empty()) {
                host.OpenOnlineManga(row.mirror, row.mangaUrl, row.title, !row.lastChapterUrl.empty() ? row.lastChapterUrl : "__FIRST__");
            } else {
                host.OpenUrl(row.openUrl);
            }
        }
        ui.Tip(Id("lib.row.rd", rowKeyHash), row.lastChapterUrl.empty() ? i18n::Literal(L"Okumaya Başla (İlk Bölüm)") : i18n::Literal(L"Kaldığın Yerden Devam Et"), rdRc);
        tagsLeft = rdRc.left - 6.0f * s;

        // 2. Bolum Sec Dropdown / Selectbox Butonu (📑 c912 ▾ veya 📑 Bölüm Seç ▾)
        std::wstring chLabelText = row.chapterLabel.empty() ? i18n::Literal(L"Bölüm Seç") : row.chapterLabel;
        std::wstring chBtnText = L"📑 " + chLabelText + L" ▾";
        const float chTextW = r.MeasureText(chBtnText, 11.5f * s, Renderer::Weight::Semibold);
        const float chBtnW = std::clamp(chTextW + 18.0f * s, 94.0f * s, 150.0f * s);
        const D2D1_RECT_F chRc = RcWH(tagsLeft - chBtnW, by, chBtnW, btnH);
        if (w::TextButton(r, ui, Id("lib.row.ch", rowKeyHash), chRc, chBtnText, false)) {
            actionClicked = true;
            if (m_chDropdownOpen && m_chDropdownRowKey == row.key) {
                m_chDropdownOpen = false;
                m_chDropdownSearch.focused = false;
            } else {
                m_chDropdownOpen = true;
                m_chDropdownRowKey = row.key;
                m_chDropdownAnchorRc = chRc;
                m_chDropdownRow = row;
                m_chDropdownSearch.Clear();
                m_chDropdownSearch.focused = true;
                m_chDropdownScroll = 0.0f;
                m_chDropdownSortAsc = false;
                m_chDropdownNeedsScrollToCurrent = true;
                FetchChaptersForManga(row.key, row.mirror, row.mangaUrl, row.lastChapterUrl, host);
            }
            host.Invalidate();
        }
        ui.Tip(Id("lib.row.ch", rowKeyHash), i18n::Literal(L"Bölüm Seç: ") + (row.chapterLabel.empty() ? i18n::Literal(L"Tüm bölümleri listele") : row.chapterLabel), chRc);
        tagsLeft = chRc.left - 6.0f * s;

        // 3. Teker Teker Hizli Eylemler (Kullanici istekleri: Silme, Okumayi Sifirla, Son Sayiya Kadar Okundu)
        const float actBtnW = 28.0f * s;

        // 3a. Silme Butonu (🗑️)
        const D2D1_RECT_F delRowRc = RcWH(tagsLeft - actBtnW, by, actBtnW, btnH);
        if (w::IconButton(r, ui, Id("lib.row.del", rowKeyHash), delRowRc, Icon::Close, 14.0f * s, i18n::Literal(L"Bu seriyi kütüphaneden kaldır"))) {
            actionClicked = true;
            host.DeleteSeriesFromLibrary(row.key);
            m_rowsRev = 0;
            m_displayDirty = true;
            host.Invalidate();
        }
        ui.Tip(Id("lib.row.del", rowKeyHash), i18n::Literal(L"Seriyi Kütüphaneden Sil"), delRowRc);
        tagsLeft = delRowRc.left - 4.0f * s;

        // 3b. Okumayi Sifirla Butonu (🔄)
        const D2D1_RECT_F resetRowRc = RcWH(tagsLeft - actBtnW, by, actBtnW, btnH);
        if (w::TextButton(r, ui, Id("lib.row.rst", rowKeyHash), resetRowRc, L"🔄", false)) {
            actionClicked = true;
            host.SetReadingProgress(row.key, false);
            m_rowsRev = 0;
            m_displayDirty = true;
            host.Invalidate();
        }
        ui.Tip(Id("lib.row.rst", rowKeyHash), i18n::Literal(L"Okuma İlerlemesini Sıfırla (Okunmadı Yap)"), resetRowRc);
        tagsLeft = resetRowRc.left - 4.0f * s;

        // 3c. Son Sayiya Kadar Okundu Butonu (✅)
        const D2D1_RECT_F markAllRc = RcWH(tagsLeft - actBtnW, by, actBtnW, btnH);
        if (w::TextButton(r, ui, Id("lib.row.allrd", rowKeyHash), markAllRc, L"✅", false)) {
            actionClicked = true;
            host.SetReadingProgress(row.key, true);
            m_rowsRev = 0;
            m_displayDirty = true;
            host.Invalidate();
        }
        ui.Tip(Id("lib.row.allrd", rowKeyHash), i18n::Literal(L"Son Çıkan Sayıya Kadar Okundu İşaretle"), markAllRc);
        tagsLeft = markAllRc.left - 8.0f * s;

        // 4. Guncelleme Hatasi Alindiysa Tatli Bir Uyari Rozeti (Kullanici istegi)
        if (row.updateFailed || HasUpdateError(row.key)) {
            const std::wstring errBadge = i18n::Literal(L"⚠️ Güncellenemedi");
            const float ebw = r.MeasureText(errBadge, 11.0f * s, Renderer::Weight::Semibold) + 14.0f * s;
            if (tagsLeft - ebw >= rc.left + 260.0f * s) {
                const D2D1_RECT_F ebRc = RcWH(tagsLeft - ebw, rc.top + (rowH - 22 * s) * 0.5f, ebw, 22 * s);
                r.FillRound(ebRc, 11.0f * s, 0xF39C12, 0.20f);
                r.StrokeRound(ebRc, 11.0f * s, 0xF39C12, 1.0f * s, 0.45f);
                r.Text(errBadge, ebRc, 0xF39C12, 11.0f * s, Renderer::Align::Center, Renderer::Weight::Semibold);
                const uint32_t ebId = Id("lib.row.err", rowKeyHash);
                ui.Tip(ebId, i18n::Literal(L"Uyarı: Bu kaynaktan son bölümler çekilirken hata oluştu. Bölümler butonundan veya siteyi açarak deneyebilirsiniz."), ebRc);
                tagsLeft = ebRc.left - 6.0f * s;
            }
        }

        // 5. Durum / Zaman / Takip Pill'leri
        auto pill = [&](const std::wstring& t, uint32_t color) {
            const float ptw = r.MeasureText(t, 11.0f * s) + 14.0f * s;
            if (tagsLeft - ptw < rc.left + 260.0f * s) return; // sol tarafa tasma
            const D2D1_RECT_F p = RcWH(tagsLeft - ptw, rc.top + (rowH - 22 * s) * 0.5f, ptw, 22 * s);
            r.FillRound(p, 11.0f * s, theme::Elevated, 1.0f);
            r.Text(t, p, color, 11.0f * s, Renderer::Align::Center);
            tagsLeft = p.left - 6.0f * s;
        };

        const std::wstring& rel = (set.librarySort == 1) ? row.relLastRead : row.relUpts;
        if (!rel.empty()) pill(rel, theme::TextMuted);
        if (!row.lang.empty()) pill(row.lang, theme::TextDim);
        if (row.stopped) pill(T(S::NotFollowing), theme::TextMuted);

        const float tx = box.right + 14.0f * s;
        const float tr = std::max(tx + 40.0f * s, tagsLeft - 12.0f * s);
        const float titleW = r.MeasureText(row.title, 14.5f * s, Renderer::Weight::Semibold);
        const float titleRight = std::min(tx + titleW, tr - 60.0f * s);
        r.Text(row.title, Rc(tx, rc.top + 11 * s, titleRight, rc.top + 33 * s),
               row.stopped ? theme::TextDim : theme::Text,
               14.5f * s, Renderer::Align::Left, Renderer::Weight::Semibold);

        // Kaynak Rozeti
        if (!row.mirror.empty()) {
            float bX = titleRight + 8.0f * s;
            const float bH = 18.0f * s;
            const float bY = rc.top + 13.0f * s;
            std::wstring mW = FromUtf8(row.mirror);
            const float bW = r.MeasureText(mW, 10.5f * s, Renderer::Weight::Semibold) + 12.0f * s;
            if (bX + bW <= tr) {
                DrawMirrorBadge(r, row.mirror, RcWH(bX, bY, bW, bH), s);
            }
        }
        r.Text(row.sub, Rc(tx, rc.top + 33 * s, tr, rc.top + 53 * s), theme::TextMuted, 12.5f * s);

        if (clicked && !actionClicked) {
            m_chDropdownOpen = false;
            m_selectedRowIndex = (int)item.rowIndex;
            m_cardMode = 0;
            m_cardSourcesScroll=0;
            host.Invalidate();
        }
    }
    r.PopClip();

    if (!toggleGroupKey.empty()) {
        if (m_expandedGroups.count(toggleGroupKey)) m_expandedGroups.erase(toggleGroupKey);
        else m_expandedGroups.insert(toggleGroupKey);
        m_displayDirty = true;
        RebuildDisplayItems(set);
        host.Invalidate();
    }

    DrawScrollbar(r, ui, list, contentH, host);
}

void LibraryView::DrawCardGrid(Renderer& r, Ui& ui, const D2D1_RECT_F& list, Settings& set, Library& lib,
                                   ShellHost& host) {
    const float s = r.Scale();

    if (m_items.empty()) {
        r.PushClip(list);
        r.Text(T(S::NoSearchResults), Rc(list.left, list.top + 40 * s, list.right, list.top + 80 * s),
               theme::TextMuted, 14.0f * s, Renderer::Align::Center);
        r.PopClip();
        return;
    }

    const float scrollbarReserve = 18.0f * s;
    const float pad = 8.0f * s;
    const float availW = RcW(list) - scrollbarReserve - pad * 2.0f;
    const float minCardW = 185.0f * s;
    const float gap = 14.0f * s;

    int cols = std::max(1, (int)std::floor((availW + gap) / (minCardW + gap)));
    const float cardW = (availW - (cols - 1) * gap) / cols;
    const float coverH = cardW * 1.38f;
    const float infoH = 74.0f * s;
    const float cardH = coverH + infoH;
    const float rowPitch = cardH + gap;

    m_pitch = rowPitch;
    m_listH = RcH(list);

    std::vector<size_t> cells;
    size_t cell = 0;
    for (size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i].isGroupHeader && m_items[i].expanded && cell % cols) cell += cols - cell % cols;
        cells.push_back(cell++);
        if ((m_items[i].isChild || (m_items[i].isGroupHeader && m_items[i].expanded)) &&
            (i + 1 == m_items.size() || !m_items[i + 1].isChild) && cell % cols) cell += cols - cell % cols;
    }
    const int totalRows = (int)((cell + cols - 1) / cols);
    const float contentH = (float)totalRows * rowPitch + pad * 2.0f - gap;
    const float scroll = ClampScroll(RcH(list), contentH);

    r.PushClip(list);

    std::string toggleGroupKey;
    const int firstRow = std::max(0, (int)std::floor((scroll - pad) / rowPitch));
    for (size_t i = 0; i < m_items.size(); ++i) if (m_items[i].isGroupHeader && m_items[i].expanded) {
        size_t last = i;
        while (last + 1 < m_items.size() && m_items[last + 1].isChild) ++last;
        const float top = list.top + pad + (float)(cells[i] / cols) * rowPitch - scroll;
        const float bottom = list.top + pad + (float)(cells[last] / cols) * rowPitch - scroll + cardH;
        const auto band = Rc(list.left + 2*s, top - 4*s, list.right - scrollbarReserve, bottom + 4*s);
        r.FillRound(band, 14*s, theme::Accent, 0.10f);
        r.StrokeRound(band, 14*s, theme::Accent, 2*s, 0.65f);
    }
    const size_t firstIdx = (size_t)(std::lower_bound(cells.begin(), cells.end(), (size_t)(firstRow * cols)) - cells.begin());

    for (size_t i = firstIdx; i < m_items.size(); ++i) {
        const int row = (int)(cells[i] / cols);
        const int col = (int)(cells[i] % cols);

        const float cardTop = list.top + pad + (float)row * rowPitch - scroll;
        if (cardTop >= list.bottom) break;
        if (cardTop + cardH < list.top) continue;

        const float cardLeft = list.left + pad + (float)col * (cardW + gap);
        const D2D1_RECT_F cardRc = RcWH(cardLeft, cardTop, cardW, cardH);
        const D2D1_RECT_F hit = Rc(cardRc.left, std::max(cardRc.top, list.top), cardRc.right, std::min(cardRc.bottom, list.bottom));

        const DisplayItem& item = m_items[i];
        const Row& rowData = m_rows[item.rowIndex];

        // ==========================================
        // 1. GRUP BAŞLIĞI - KARTLIK GİBİ GÖRÜNÜM
        // ==========================================
        if (item.isGroupHeader) {
            const uint32_t grpId = Id("lib.p.grp", Id(item.groupKey.c_str()));
            const bool grpHot = ui.Hot(grpId);
            const bool grpClicked = RcH(hit) > 1.0f && ui.Button(grpId, hit);

            if (grpClicked) {
                toggleGroupKey = item.groupKey;
            }

            // Eğer KAPALI ise: Arkaya 2 katman kart çizerek "KARTLIK" (Deck of Cards) efekti ver!
            if (!item.expanded) {
                // Katman 2 (En arkadaki kart)
                const float dx2 = 8.0f * s, dy2 = -7.0f * s;
                const D2D1_RECT_F backRc2 = RcWH(cardRc.left + dx2, cardRc.top + dy2, cardW, cardH);
                r.FillRound(backRc2, 12.0f * s, 0x1A1C24, 0.70f);
                r.StrokeRound(backRc2, 12.0f * s, theme::Border, 1.0f * s, 0.50f);

                // Katman 1 (Ortadaki kart)
                const float dx1 = 4.0f * s, dy1 = -3.5f * s;
                const D2D1_RECT_F backRc1 = RcWH(cardRc.left + dx1, cardRc.top + dy1, cardW, cardH);
                r.FillRound(backRc1, 12.0f * s, 0x222630, 0.85f);
                r.StrokeRound(backRc1, 12.0f * s, theme::Border, 1.0f * s, 0.70f);
            }

            // Ön Kart (Grup Kapağı)
            r.FillRound(cardRc, 12.0f * s, item.updateFailed ? theme::UpdateErrorTone : item.caughtUp ? theme::ReadTone : theme::UnreadTone, 1.0f);
            if (item.expanded) {
                r.StrokeRound(cardRc, 12.0f * s, theme::Accent, 1.5f * s, 0.85f);
            } else if (grpHot) {
                r.StrokeRound(cardRc, 12.0f * s, theme::Accent, 1.5f * s, 0.75f);
                r.FillRound(cardRc, 12.0f * s, theme::White, 0.05f);
            } else {
                r.StrokeRound(cardRc, 12.0f * s, theme::Border, 1.0f * s, 0.8f);
            }

            // Kapak resmi alanı
            const D2D1_RECT_F coverRc = Rc(cardRc.left + 5.0f * s, cardRc.top + 5.0f * s, cardRc.right - 5.0f * s, cardRc.top + coverH);
            ID2D1Bitmap1* bmp = engine::CoverManager::Instance().GetBitmap(r, rowData.key, rowData.mangaUrl, rowData.mirror);
            if (bmp) {
                r.PushClip(coverRc);
                r.DrawBitmap(bmp, coverRc);
                if (item.caughtUp) r.Fill(coverRc, theme::Black, 0.30f);
                r.PopClip();
            } else {
                r.FillRound(coverRc, 8.0f * s, 0x1E2129, 1.0f);
                r.Text(item.groupInitial.empty() ? L"M" : item.groupInitial,
                       RcWH(coverRc.left, coverRc.top + (coverH - 50.0f * s) * 0.5f, RcW(coverRc), 50.0f * s),
                       theme::TextDim, 32.0f * s, Renderer::Align::Center, Renderer::Weight::Bold);
            }

            // Kapak Üzeri Rozet: Kartlık / Kaynak Sayısı Rozeti (örn. 🗂 3 Kaynak)
            wchar_t countStr[32];
            swprintf_s(countStr, i18n::Literal(L"🗂 %d Kaynak"), (int)item.childCount);
            const float cntW = r.MeasureText(countStr, 10.5f * s, Renderer::Weight::Bold) + 14.0f * s;
            const D2D1_RECT_F pillRc = RcWH(coverRc.right - cntW - 6.0f * s, coverRc.top + 6.0f * s, cntW, 22.0f * s);
            r.FillRound(pillRc, 11.0f * s, theme::Accent, 0.90f);
            r.Text(countStr, pillRc, theme::White, 10.5f * s, Renderer::Align::Center, Renderer::Weight::Bold);

            // Açık / Kapalı İndikatörü
            const wchar_t* stStr = item.expanded ? i18n::Literal(L"▼ GRUP AÇIK") : i18n::Literal(L"▶ AÇ");
            const float stW = r.MeasureText(stStr, 10.0f * s, Renderer::Weight::Bold) + 12.0f * s;
            const D2D1_RECT_F stRc = RcWH(coverRc.left + 6.0f * s, coverRc.top + 6.0f * s, stW, 20.0f * s);
            r.FillRound(stRc, 10.0f * s, 0x14161C, 0.85f);
            r.Text(stStr, stRc, item.expanded ? theme::Accent : theme::TextMuted, 10.0f * s, Renderer::Align::Center, Renderer::Weight::Bold);

            // Alt Bilgi Alanı: Başlık ve Açıklama
            const float textTop = coverRc.bottom + 6.0f * s;
            const D2D1_RECT_F titleRc = Rc(cardRc.left + 8.0f * s, textTop, cardRc.right - 8.0f * s, textTop + 20.0f * s);
            r.Text(item.groupTitle, titleRc, theme::Text, 13.0f * s, Renderer::Align::Left, Renderer::Weight::Semibold);

            // Gruptaki kaynakların isimleri
            std::wstring mirrorsSummary;
            for (size_t mi = 0; mi < item.groupMirrors.size() && mi < 3; ++mi) {
                if (mi > 0) mirrorsSummary += L" · ";
                mirrorsSummary += FromUtf8(item.groupMirrors[mi]);
            }
            if (item.groupMirrors.size() > 3) mirrorsSummary += L" ...";
            const D2D1_RECT_F mirRc = Rc(cardRc.left + 8.0f * s, textTop + 22.0f * s, cardRc.right - 8.0f * s, textTop + 38.0f * s);
            r.Text(mirrorsSummary, mirRc, theme::TextMuted, 10.5f * s, Renderer::Align::Left);

            // Tıklama İpucu
            const D2D1_RECT_F hintRc = Rc(cardRc.left + 8.0f * s, cardRc.bottom - 20.0f * s, cardRc.right - 8.0f * s, cardRc.bottom - 4.0f * s);
            r.Text(item.expanded ? i18n::Literal(L"✕ Kapatmak için tıkla") : i18n::Literal(L"👆 Kartlığı açmak için tıkla"), hintRc,
                   item.expanded ? theme::TextDim : theme::Accent, 10.0f * s, Renderer::Align::Center);

            continue;
        }

        // ==========================================
        // 2. TEKİL MANGA KARTI VEYA AÇILMIŞ GRUP ÇOCUĞU
        // ==========================================
        const uint32_t cardId = Id("lib.p.card", (uint32_t)i);
        const bool cardHot = ui.Hot(cardId);
        const bool cardClicked = RcH(hit) > 1.0f && ui.Button(cardId, hit);

        // Arka plan
        r.FillRound(cardRc, 12.0f * s, item.updateFailed ? theme::UpdateErrorTone : item.caughtUp ? theme::ReadTone : theme::UnreadTone, 1.0f);
        if (ui.Pressed(cardId)) {
            r.FillRound(cardRc, 12.0f * s, theme::Press, theme::PressAlpha);
        } else if (cardHot) {
            r.FillRound(cardRc, 12.0f * s, theme::White, 0.05f);
            r.StrokeRound(cardRc, 12.0f * s, theme::Accent, 1.5f * s, 0.75f);
        } else if (item.isChild) {
            r.StrokeRound(cardRc, 12.0f * s, theme::Accent, 1.5f * s, 0.75f);
        } else {
            r.StrokeRound(cardRc, 12.0f * s, theme::Border, 1.0f * s, 0.7f);
        }

        // Kapak Resmi
        const D2D1_RECT_F coverRc = Rc(cardRc.left + 5.0f * s, cardRc.top + 5.0f * s, cardRc.right - 5.0f * s, cardRc.top + coverH);
        ID2D1Bitmap1* bmp = engine::CoverManager::Instance().GetBitmap(r, rowData.key, rowData.mangaUrl, rowData.mirror);
        if (bmp) {
            r.PushClip(coverRc);
            r.DrawBitmap(bmp, coverRc);
            if (item.caughtUp) r.Fill(coverRc, theme::Black, 0.30f);
            r.PopClip();
        } else {
            r.FillRound(coverRc, 8.0f * s, 0x1E2129, 1.0f);
            r.Text(rowData.initial.empty() ? L"M" : rowData.initial,
                   RcWH(coverRc.left, coverRc.top + (coverH - 50.0f * s) * 0.5f, RcW(coverRc), 50.0f * s),
                   theme::TextDim, 32.0f * s, Renderer::Align::Center, Renderer::Weight::Bold);
        }

        // Sol Üst Rozet: Ayna Adı (örn. Batoto, MangaFox)
        if (!rowData.mirror.empty()) {
            std::wstring mW = FromUtf8(rowData.mirror);
            const float mW_measured = r.MeasureText(mW, 10.0f * s, Renderer::Weight::Semibold) + 12.0f * s;
            const D2D1_RECT_F mirBadgeRc = RcWH(coverRc.left + 6.0f * s, coverRc.top + 6.0f * s, mW_measured, 20.0f * s);
            r.FillRound(mirBadgeRc, 10.0f * s, 0x14161C, 0.85f);
            r.Text(mW, mirBadgeRc, theme::White, 10.0f * s, Renderer::Align::Center, Renderer::Weight::Semibold);
        }

        if (item.isChild) {
            const auto badge = Rc(coverRc.left + 6*s, coverRc.bottom - 26*s, coverRc.right - 6*s, coverRc.bottom - 4*s);
            r.FillRound(badge, 6*s, theme::Accent, 0.92f);
            r.Text(i18n::Literal(L"Grup: ") + item.groupTitle, badge, theme::White, 10*s, Renderer::Align::Center, Renderer::Weight::Semibold);
        }

        // Sağ Üst Rozet: Yeni Bölüm veya Okunmadı
        const bool hasNew = (rowData.upts > 0 && rowData.upts > rowData.lastReadTs);
        if (hasNew) {
            const D2D1_RECT_F newRc = RcWH(coverRc.right - 48.0f * s - 6.0f * s, coverRc.top + 6.0f * s, 48.0f * s, 20.0f * s);
            r.FillRound(newRc, 10.0f * s, 0x16A34A, 0.90f);
            r.Text(i18n::Literal(L"YENİ"), newRc, theme::White, 10.0f * s, Renderer::Align::Center, Renderer::Weight::Bold);
        } else if (rowData.lastReadTs == 0) {
            const D2D1_RECT_F unreadRc = RcWH(coverRc.right - 14.0f * s - 6.0f * s, coverRc.top + 6.0f * s, 14.0f * s, 14.0f * s);
            r.Disc(unreadRc.left + 7.0f * s, unreadRc.top + 7.0f * s, 6.0f * s, theme::Accent, 1.0f);
        }

        // Alt Bilgi Alanı: Başlık, Bölüm ve Süre
        const float textTop = coverRc.bottom + 6.0f * s;
        const D2D1_RECT_F titleRc = Rc(cardRc.left + 8.0f * s, textTop, cardRc.right - 8.0f * s, textTop + 20.0f * s);
        r.Text(rowData.title, titleRc, theme::Text, 13.0f * s, Renderer::Align::Left, Renderer::Weight::Semibold);

        // Son veya En Yeni Bölüm
        const std::wstring chText = !rowData.latestChapterLabel.empty()
            ? rowData.latestChapterLabel
            : (!rowData.chapterLabel.empty() ? rowData.chapterLabel : i18n::Literal(L"Bölüm 1"));
        const D2D1_RECT_F chRc = Rc(cardRc.left + 8.0f * s, textTop + 22.0f * s, cardRc.right - 68.0f * s, textTop + 38.0f * s);
        r.Text(chText, chRc, theme::Accent, 11.5f * s, Renderer::Align::Left, Renderer::Weight::Semibold);

        // Göreli zaman (örn. 3 sa önce)
        const std::wstring timeText = !rowData.relUpts.empty() ? rowData.relUpts : rowData.relLastRead;
        const D2D1_RECT_F timeRc = Rc(cardRc.left + 8.0f * s, cardRc.bottom - 20.0f * s, cardRc.right - 68.0f * s, cardRc.bottom - 4.0f * s);
        r.Text(timeText, timeRc, theme::TextMuted, 10.0f * s, Renderer::Align::Left);

        // Hızlı Butonlar: [ 📋 Bölümler ] ve [ ⚡ Oku ]
        const float btnH = 26.0f * s;
        const float rdBtnW = 56.0f * s;
        const float infoBtnW = 66.0f * s;
        const D2D1_RECT_F rdBtnRc = RcWH(cardRc.right - rdBtnW - 8.0f * s, cardRc.bottom - btnH - 6.0f * s, rdBtnW, btnH);
        const D2D1_RECT_F infoBtnRc = RcWH(rdBtnRc.left - infoBtnW - 6.0f * s, cardRc.bottom - btnH - 6.0f * s, infoBtnW, btnH);

        const uint32_t rdBtnId = Id("lib.p.rd", (uint32_t)i);
        const uint32_t infoBtnId = Id("lib.p.inf", (uint32_t)i);

        bool actionClicked = false;
        if (w::TextButton(r, ui, rdBtnId, rdBtnRc, i18n::Literal(L"Detay"), true)) actionClicked = true;
        if (w::TextButton(r, ui, infoBtnId, infoBtnRc, i18n::Literal(L"Ara"), false)) {
            const auto title = rowData.title;
            host.DispatchUI([this, title, &host]() { SearchOtherSources(title, host); });
        } else if (cardClicked || actionClicked) {
            m_selectedRowIndex = (int)item.rowIndex;
            m_cardMode = 1;
            m_chapterSearch.Clear();
            m_chapterScroll = 0;
            m_chapterFilterTab = 0;
            m_chapterNeedsScrollToCurrent = true;
            host.Invalidate();
        }
    }

    r.PopClip();

    if (!toggleGroupKey.empty()) {
        if (m_expandedGroups.count(toggleGroupKey)) m_expandedGroups.erase(toggleGroupKey);
        else m_expandedGroups.insert(toggleGroupKey);
        m_displayDirty = true;
        RebuildDisplayItems(set);
        host.Invalidate();
    }

    // Kaydırma çubuğu
    DrawScrollbar(r, ui, list, contentH, host);
}

void LibraryView::RebuildLocal(const Settings& set, const Library& lib) {
    const auto root=set.downloadDir.empty()?paths::Join(paths::DataDir(),L"downloads"):set.downloadDir;
    bool rebuilt=false;
    if(m_localDataDirty || m_localRoot!=root || m_localRecent!=set.recent || m_localRevision!=lib.Revision()) {
        rebuilt=true;
        m_localEntries=local::Collect(set.recent,root,lib);
        m_localRoot=root;m_localRecent=set.recent;m_localRevision=lib.Revision();m_localDataDirty=false;
    }
    const auto query=ToLowerInvariant(m_localSearch.text);
    if(!rebuilt && m_localLayoutQuery==query && m_localLayoutSort==m_localSort && m_localLayoutGroup==set.libraryGroup && m_localLayoutDesc==m_localSortDesc)return;
    m_localLayoutQuery=query;m_localLayoutSort=m_localSort;m_localLayoutGroup=set.libraryGroup;m_localLayoutDesc=m_localSortDesc;
    m_localGroups.clear();
    std::map<std::wstring,size_t> groups;
    for(const auto& entry:m_localEntries) {
        const auto searchable=ToLowerInvariant(entry.seriesTitle+L" "+entry.chapterTitle+L" "+FromUtf8(entry.mirror));
        if(!query.empty() && searchable.find(query)==std::wstring::npos)continue;
        const auto key=set.libraryGroup && entry.seriesTitle!=i18n::Literal(L"Kimliği bulunamayan yerel manga") ? NormalizeForGroup(entry.seriesTitle) : entry.path;
        auto it=groups.find(key);
        if(it==groups.end()) {groups[key]=m_localGroups.size();LocalGroup group;group.key=key;group.title=entry.seriesTitle;group.downloadedAt=entry.downloadedAt;group.recordedDownload=entry.recordedDownload;m_localGroups.push_back(std::move(group));it=groups.find(key);}
        auto& group=m_localGroups[it->second];group.chapters.push_back(entry);
        if(entry.downloadedAt>group.downloadedAt){group.downloadedAt=entry.downloadedAt;group.recordedDownload=entry.recordedDownload;}
    }
    std::sort(m_localGroups.begin(),m_localGroups.end(),[&](const auto& a,const auto& b){
        int comparison=0;
        if(m_localSort==0)comparison=a.downloadedAt<b.downloadedAt?-1:a.downloadedAt>b.downloadedAt?1:0;
        else if(m_localSort==1)comparison=StrCmpLogicalW(a.title.c_str(),b.title.c_str());
        else comparison=a.chapters.size()<b.chapters.size()?-1:a.chapters.size()>b.chapters.size()?1:0;
        if(comparison==0)return a.key<b.key;
        return m_localSortDesc?comparison>0:comparison<0;
    });
}

static std::wstring LocalDate(int64_t milliseconds,bool recorded) {
    if(milliseconds<=0)return i18n::Literal(L"Tarih bilinmiyor");
    ULARGE_INTEGER value{};value.QuadPart=(uint64_t)milliseconds*10000+116444736000000000ULL;
    FILETIME utc{value.LowPart,value.HighPart},local{};SYSTEMTIME time{};
    FileTimeToLocalFileTime(&utc,&local);FileTimeToSystemTime(&local,&time);
    wchar_t text[96];swprintf_s(text,L"%s: %02d.%02d.%04d %02d:%02d",recorded?i18n::Literal(L"İndirildi"):i18n::Literal(L"Dosya tarihi"),time.wDay,time.wMonth,time.wYear,time.wHour,time.wMinute);
    return text;
}

void LibraryView::DrawLocalList(Renderer& r, Ui& ui, const D2D1_RECT_F& list, const std::vector<std::wstring>& recent,
                                ShellHost& host, Settings& set, Library& lib) {
    const float s=r.Scale();
    float x=list.right;
    auto button=[&](const char* id,const wchar_t* text,float width,bool active=false){x-=width*s;const auto rc=RcWH(x,list.top,width*s,32*s);const bool clicked=w::TextButton(r,ui,Id(id),rc,text,active);x-=6*s;return clicked;};
    if(button("local.refresh",i18n::Literal(L"Yenile"),66)){m_localDataDirty=true;host.Invalidate();}
    if(button("local.view",set.libraryViewMode?i18n::Literal(L"Kartlar"):i18n::Literal(L"Liste"),76,set.libraryViewMode==1)){set.libraryViewMode=1-set.libraryViewMode;host.SaveSettings();}
    if(button("local.group",i18n::Literal(L"Grupla"),76,set.libraryGroup)){set.libraryGroup=!set.libraryGroup;host.SaveSettings();m_scroll[kLocal]=0;}
    if(button("local.direction",m_localSortDesc?L"↓":L"↑",32)){m_localSortDesc=!m_localSortDesc;m_scroll[kLocal]=0;}
    const wchar_t* sort=m_localSort==0?i18n::Literal(L"İndirme zamanı"):m_localSort==1?i18n::Literal(L"Manga adı"):i18n::Literal(L"Bölüm sayısı");
    if(button("local.sort",sort,132)){m_localSort=(m_localSort+1)%3;m_scroll[kLocal]=0;}
    m_localSearch.Draw(r,ui,Id("local.search"),Rc(list.left,list.top,std::max(list.left+80*s,x),list.top+32*s),i18n::Literal(L"Yerel manga veya bölüm ara..."),true,0);
    RebuildLocal(set,lib);
    const auto body=Rc(list.left,list.top+46*s,list.right,list.bottom);
    const bool cards=set.libraryViewMode==1;
    const float gap=14*s, available=RcW(body)-18*s;
    const int cols=cards?std::max(1,(int)std::floor((available+gap)/(190*s+gap))):1;
    const float width=(available-(cols-1)*gap)/cols;
    const float coverH=cards?width*1.38f:0;
    const float height=cards?coverH+100*s:104*s;
    const float pitch=height+gap;
    m_pitch=pitch;m_listH=RcH(body);
    const float contentH=m_localGroups.empty()?0:(float)((m_localGroups.size()+cols-1)/cols)*pitch-gap;
    const float scroll=ClampScroll(RcH(body),contentH);
    if(m_localGroups.empty())r.Text(i18n::Literal(L"Yerel manga bulunamadı. Bir klasör/arşiv açabilir veya bölüm indirebilirsin."),body,theme::TextMuted,13*s,Renderer::Align::Center);
    r.PushClip(body);
    const size_t first=(size_t)std::max(0.0f,std::floor(scroll/pitch))*cols;
    for(size_t i=first;i<m_localGroups.size();++i){
        const auto& group=m_localGroups[i];const auto& entry=group.chapters.front();
        const float y=body.top+(float)(i/cols)*pitch-scroll;if(y>=body.bottom)break;
        const auto card=RcWH(body.left+(float)(i%cols)*(width+gap),y,width,height);
        const auto hit=Rc(card.left,std::max(card.top,body.top),card.right,std::min(card.bottom,body.bottom));
        const auto id=Id("local.manga",Id(ToUtf8(group.key).c_str()));const bool click=RcH(hit)>1 && ui.Button(id,hit);
        bool read=true;
        for(const auto& chapter:group.chapters){const auto* series=lib.FindSeries(chapter.seriesKey);const int progress=series?ChapterIndex(series->chapters,series->lastChapterUrl):-1;const int current=series?ChapterIndex(series->chapters,chapter.chapterUrl):-1;if(progress<0 || current<progress || current<0){read=false;break;}}
        r.FillRound(card,12*s,read?theme::ReadTone:theme::UnreadTone,1);
        r.StrokeRound(card,12*s,ui.Hot(id)?theme::Accent:theme::Border,1*s,0.75f);
        const auto cover=cards?Rc(card.left+5*s,card.top+5*s,card.right-5*s,card.top+coverH):RcWH(card.left+8*s,card.top+8*s,62*s,88*s);
        ID2D1Bitmap1* bitmap=nullptr;
        if(!entry.seriesKey.empty())bitmap=engine::CoverManager::Instance().GetBitmap(r,entry.seriesKey);
        if(!bitmap && !entry.archive){const auto pages=local::ListImages(entry.path);if(!pages.empty())bitmap=engine::CoverManager::Instance().GetLocalBitmap(r,pages.front());}
        if(bitmap){r.PushClip(cover);r.DrawBitmap(bitmap,cover);r.PopClip();}
        else {r.FillRound(cover,8*s,theme::Elevated,1);r.Text(group.title.substr(0,1),cover,theme::TextDim,24*s,Renderer::Align::Center,Renderer::Weight::Bold);}
        const float tx=cards?card.left+10*s:cover.right+14*s;
        const float ty=cards?cover.bottom+8*s:card.top+10*s;
        r.Text(group.title,Rc(tx,ty,card.right-10*s,ty+22*s),theme::Text,14*s,Renderer::Align::Left,Renderer::Weight::Semibold);
        const auto detail=group.chapters.size()>1?std::to_wstring(group.chapters.size())+i18n::Literal(L" yerel bölüm • ")+entry.chapterTitle:entry.chapterTitle;
        r.Text(detail,Rc(tx,ty+25*s,card.right-10*s,ty+44*s),theme::Accent,11*s);
        const auto source=entry.mirror.empty()?i18n::Literal(L"Yerel manga"):FromUtf8(entry.mirror);
        r.Text(source+L" • "+(entry.archive?L"CBZ/ZIP":i18n::Literal(L"Klasör")),Rc(tx,ty+46*s,card.right-10*s,ty+63*s),theme::TextMuted,10.5f*s);
        r.Text(LocalDate(group.downloadedAt,group.recordedDownload),Rc(tx,ty+65*s,card.right-10*s,ty+83*s),theme::TextDim,10*s);
        ui.Tip(id,group.title+L"\n"+entry.chapterTitle+L"\n"+entry.path,card);
        if(click){m_localSelected=group.key;m_localDeletePath.clear();m_localDetailScroll=0;host.Invalidate();}
    }
    r.PopClip();DrawScrollbar(r,ui,body,contentH,host);
    if(!m_localSelected.empty()){ui.ClearOcclusion();DrawLocalDetail(r,ui,list,host);}
}

void LibraryView::DrawLocalDetail(Renderer& r,Ui& ui,const D2D1_RECT_F& area,ShellHost& host){
    auto it=std::find_if(m_localGroups.begin(),m_localGroups.end(),[&](const auto& group){return group.key==m_localSelected;});
    if(it==m_localGroups.end()){m_localSelected.clear();return;}
    const float s=r.Scale();const float width=std::min(760*s,RcW(area)-24*s),height=std::min(620*s,RcH(area)-24*s);
    const auto card=RcWH(area.left+(RcW(area)-width)*0.5f,area.top+(RcH(area)-height)*0.5f,width,height);
    r.Fill(area,theme::Black,0.65f);r.FillRound(card,14*s,theme::Elevated,1);r.StrokeRound(card,14*s,theme::Border,1*s,0.8f);
    const auto close=RcWH(card.right-38*s,card.top+12*s,26*s,26*s);
    if(w::IconButton(r,ui,Id("local.detail.close"),close,Icon::Close,16*s,i18n::Literal(L"Kapat")) || (ui.In().pressed&&!RcHit(card,ui.In().mx,ui.In().my))){m_localSelected.clear();host.Invalidate();return;}
    r.Text(it->title,Rc(card.left+18*s,card.top+12*s,close.left-8*s,card.top+38*s),theme::Text,17*s,Renderer::Align::Left,Renderer::Weight::Bold);
    r.Text(std::to_wstring(it->chapters.size())+i18n::Literal(L" yerel bölüm • En son indirilenler üstte"),Rc(card.left+18*s,card.top+42*s,card.right-18*s,card.top+63*s),theme::TextMuted,12*s);
    const auto body=Rc(card.left+16*s,card.top+76*s,card.right-16*s,card.bottom-(m_localDeletePath.empty()?14:90)*s);
    const float pitch=86*s;const float content=(float)it->chapters.size()*pitch;
    m_localDetailMaxScroll=std::max(0.0f,content-RcH(body));m_localDetailScroll=std::clamp(m_localDetailScroll,0.0f,m_localDetailMaxScroll);
    if(!m_localDeletePath.empty())ui.Occlude(body);
    r.PushClip(body);
    for(size_t i=(size_t)(m_localDetailScroll/pitch);i<it->chapters.size();++i){
        const auto& entry=it->chapters[i];const float y=body.top+(float)i*pitch-m_localDetailScroll;if(y>=body.bottom)break;
        const auto row=Rc(body.left,y,body.right-8*s,y+78*s);r.FillRound(row,8*s,theme::Panel,1);
        r.Text(entry.chapterTitle,Rc(row.left+10*s,y+6*s,row.right-158*s,y+27*s),theme::Text,13*s,Renderer::Align::Left,Renderer::Weight::Semibold);
        r.Text((entry.mirror.empty()?i18n::Literal(L"Yerel"):FromUtf8(entry.mirror))+L" • "+LocalDate(entry.downloadedAt,entry.recordedDownload),Rc(row.left+10*s,y+29*s,row.right-158*s,y+47*s),theme::TextMuted,10.5f*s);
        r.Text(entry.path,Rc(row.left+10*s,y+49*s,row.right-10*s,y+72*s),theme::TextDim,10*s);
        const auto button=RcWH(row.right-148*s,y+10*s,68*s,30*s);
        if(y>=body.top && button.bottom<=body.bottom && w::TextButton(r,ui,Id("local.read",(uint32_t)i),button,i18n::Literal(L"Oku"),true)){
            const auto path=entry.path;m_localSelected.clear();r.PopClip();host.OpenPath(path);return;
        }
        const auto remove=RcWH(row.right-72*s,y+10*s,62*s,30*s);
        if(y>=body.top && remove.bottom<=body.bottom && w::TextButton(r,ui,Id("local.delete",(uint32_t)i),remove,i18n::Literal(L"Sil"),false))m_localDeletePath=entry.path;
    }
    r.PopClip();
    if(!m_localDeletePath.empty()) {
        ui.ClearOcclusion();const float top=card.bottom-84*s;
        r.Text(i18n::Literal(L"Bu yerel bölüm ve içeriği Geri Dönüşüm Kutusu'na taşınsın mı?"),Rc(card.left+18*s,top,card.right-18*s,top+20*s),theme::Text,11*s);
        r.Text(m_localDeletePath,Rc(card.left+18*s,top+22*s,card.right-178*s,card.bottom-14*s),theme::TextMuted,10*s);
        if(w::TextButton(r,ui,Id("local.delete.cancel"),RcWH(card.right-176*s,top+28*s,76*s,30*s),i18n::Literal(L"Vazgeç"),false))m_localDeletePath.clear();
        if(w::TextButton(r,ui,Id("local.delete.confirm"),RcWH(card.right-92*s,top+28*s,74*s,30*s),i18n::Literal(L"Sil"),true)) {
            const auto path=m_localDeletePath;m_localDeletePath.clear();m_localSelected.clear();host.DeleteLocalChapter(path);return;
        }
    }
    if(m_localDetailMaxScroll>0){const float thumbH=std::max(24*s,RcH(body)*RcH(body)/content);const float top=body.top+m_localDetailScroll/m_localDetailMaxScroll*(RcH(body)-thumbH);r.FillRound(RcWH(body.right-4*s,top,4*s,thumbH),2*s,theme::TextMuted,0.6f);}
}

void LibraryView::DrawBookmarkList(Renderer& r, Ui& ui, const D2D1_RECT_F& list, const std::vector<Bookmark>& bookmarks,
                                   ShellHost& host) {
    const float s = r.Scale();
    if (bookmarks.empty()) {
        const float midX = (list.left + list.right) * 0.5f;
        const float midY = (list.top + list.bottom) * 0.5f - 20.0f * s;
        r.Text(i18n::Literal(L"Henüz yer imi eklenmedi."), Rc(midX - 250 * s, midY, midX + 250 * s, midY + 28 * s),
               theme::TextDim, 16.0f * s, Renderer::Align::Center, Renderer::Weight::Semibold);
        r.Text(i18n::Literal(L"Manga okurken alt çubuktaki yıldız simgesine basarak yer imi ekleyebilirsiniz."),
               Rc(midX - 260 * s, midY + 32 * s, midX + 260 * s, midY + 70 * s),
               theme::TextMuted, 13.0f * s, Renderer::Align::Center, Renderer::Weight::Normal, 1.0f, true);
        return;
    }

    const float rowH = 64.0f * s, gap = 8.0f * s, pitch = rowH + gap;
    m_pitch = pitch;
    m_listH = RcH(list);
    const float contentH = (float)bookmarks.size() * pitch - gap;
    const float scroll = ClampScroll(RcH(list), contentH);

    r.PushClip(list);
    const size_t first = (size_t)std::max(0.0f, std::floor(scroll / pitch));
    for (size_t i = first; i < bookmarks.size(); ++i) {
        const float top = list.top + (float)i * pitch - scroll;
        if (top >= list.bottom) break;
        const auto& b = bookmarks[i];

        const D2D1_RECT_F row = Rc(list.left, top, list.right, top + rowH);
        const D2D1_RECT_F hit = Rc(row.left, std::max(row.top, list.top), row.right, std::min(row.bottom, list.bottom));
        const uint32_t id = Id("lib.bm", (uint32_t)i);
        const bool clicked = RcH(hit) > 1.0f && ui.Button(id, hit);
        r.FillRound(row, 12.0f * s, theme::Panel, 1.0f);
        if (ui.Pressed(id)) r.FillRound(row, 12.0f * s, theme::Press, theme::PressAlpha);
        else if (ui.Hot(id)) r.FillRound(row, 12.0f * s, theme::White, 0.06f);

        const D2D1_RECT_F iconBox = RcWH(row.left + 12 * s, row.top + 12 * s, 40 * s, 40 * s);
        r.FillRound(iconBox, 10.0f * s, theme::Elevated, 1.0f);
        DrawIcon(r, Icon::Star, iconBox, 20.0f * s, theme::Accent);

        const float tx = iconBox.right + 14.0f * s;
        std::wstring nameW = FromUtf8(b.name);
        r.Text(nameW, Rc(tx, row.top + 10 * s, row.right - 130 * s, row.top + 32 * s),
               theme::Text, 14.5f * s, Renderer::Align::Left, Renderer::Weight::Semibold);

        std::wstring sub = FromUtf8(b.mirror);
        if (!b.chapName.empty()) sub += L"  ·  " + FromUtf8(b.chapName);
        if (!b.note.empty()) sub += L"  (" + FromUtf8(b.note) + L")";
        r.Text(sub, Rc(tx, row.top + 32 * s, row.right - 130 * s, row.top + 52 * s), theme::TextMuted, 12.0f * s);

        // Silme butonu (X)
        const D2D1_RECT_F delRc = RcWH(row.right - 36 * s, row.top + 18 * s, 26 * s, 26 * s);
        const uint32_t delId = Id("lib.bmdel", (uint32_t)i);
        const bool delClicked = ui.Button(delId, delRc);
        if (ui.Hot(delId)) r.FillRound(delRc, 6.0f * s, theme::White, 0.12f);
        r.Text(L"✕", delRc, ui.Hot(delId) ? theme::Accent : theme::TextMuted, 13.0f * s, Renderer::Align::Center, Renderer::Weight::Bold);
        ui.Tip(delId, i18n::Literal(L"Yer imini sil"), delRc);

        r.Text(std::wstring(T(S::OpenArrow)) + L"  ›", Rc(row.right - 120 * s, row.top, row.right - 44 * s, row.bottom),
               ui.Hot(id) ? theme::Accent : theme::TextMuted, 13.0f * s, Renderer::Align::Right, Renderer::Weight::Semibold);

        if (delClicked) {
            host.RemoveBookmark(i);
        } else if (clicked) {
            if (!b.mirror.empty() && !b.url.empty()) {
                host.OpenOnlineManga(b.mirror, b.url, nameW, b.chapUrl);
            } else if (!b.chapUrl.empty()) {
                host.OpenUrl(FromUtf8(b.chapUrl));
            } else if (!b.url.empty()) {
                host.OpenUrl(FromUtf8(b.url));
            }
        }
    }
    r.PopClip();
    DrawScrollbar(r, ui, list, contentH, host);
}

static std::vector<std::wstring> SiteLanguages(const std::string& language) {
    auto value=ToLowerInvariant(FromUtf8(language));std::vector<std::wstring> out;std::wstring token;
    auto add=[&](){if(!token.empty() && std::find(out.begin(),out.end(),token)==out.end())out.push_back(token);token.clear();};
    for(wchar_t c:value){if(iswalnum(c)||c==L'-'||c==L'_')token+=c;else add();}add();
    if(out.empty())out.push_back(L"bilinmiyor");return out;
}
void LibraryView::EnsureMirrorsCache() {
    if(m_mirrorsCacheBuilt)return;
    const auto& mirrors=engine::MirrorEngine::Instance().GetMirrors();if(mirrors.empty())return;
    m_mirrorDisplayList.clear();m_siteLanguages.clear();
    for(size_t i=0;i<mirrors.size();++i){const auto& mirror=mirrors[i];MirrorDisplayItem item;item.index=i;
        item.nameW=FromUtf8(mirror.name);item.domainStr=mirror.domains.empty()?FromUtf8(mirror.home):FromUtf8(mirror.domains.front());
        item.languages=SiteLanguages(mirror.language);item.langUpper=FromUtf8(mirror.language);for(auto& c:item.langUpper)c=(wchar_t)towupper(c);
        item.searchLower=ToLowerInvariant(item.nameW+L" "+FromUtf8(mirror.language)+L" "+item.domainStr);
        for(const auto& language:item.languages)if(std::find(m_siteLanguages.begin(),m_siteLanguages.end(),language)==m_siteLanguages.end())m_siteLanguages.push_back(language);
        m_mirrorDisplayList.push_back(std::move(item));
    }
    std::sort(m_siteLanguages.begin(),m_siteLanguages.end());m_mirrorsCacheBuilt=true;
}
void LibraryView::FilterSites(const Settings& set) {
    EnsureMirrorsCache();m_filteredMirrors.clear();const auto query=ToLowerInvariant(m_search.text);
    for(size_t i=0;i<m_mirrorDisplayList.size();++i){const auto& item=m_mirrorDisplayList[i];
        const bool hidden=std::find(set.hiddenSites.begin(),set.hiddenSites.end(),item.nameW)!=set.hiddenSites.end();
        if(hidden!=m_showHiddenSites)continue;
        if(!query.empty() && item.searchLower.find(query)==std::wstring::npos)continue;
        if(!set.siteLanguages.empty() && std::none_of(item.languages.begin(),item.languages.end(),[&](const auto& language){return std::find(set.siteLanguages.begin(),set.siteLanguages.end(),language)!=set.siteLanguages.end();}))continue;
        m_filteredMirrors.push_back(i);
    }
}
void LibraryView::StartSiteChecks(ShellHost& host,const std::vector<size_t>& indices) {
    if(m_stopping || m_siteChecking || indices.empty())return;
    std::vector<engine::MirrorInfo> mirrors;const auto& all=engine::MirrorEngine::Instance().GetMirrors();
    for(size_t index:indices)if(index<m_mirrorDisplayList.size())mirrors.push_back(all[m_mirrorDisplayList[index].index]);
    if(mirrors.empty())return;
    m_siteChecking=true;m_siteCancel=false;m_siteCompleted=0;m_siteTotal=mirrors.size();
    m_workers.emplace_back([this,mirrors=std::move(mirrors),&host](){
        std::atomic<size_t> next{0};std::vector<std::thread> workers;
        for(size_t i=0;i<std::min((size_t)3,mirrors.size());++i)workers.emplace_back([&](){
            while(!m_stopping && !m_siteCancel){const auto index=next.fetch_add(1);if(index>=mirrors.size())break;
                engine::MirrorEngine::Instance().CheckAccess(mirrors[index]);++m_siteCompleted;host.Invalidate();}
        });
        for(auto& worker:workers)worker.join();m_siteChecking=false;host.Invalidate();
    });
}
void LibraryView::DrawSitesList(Renderer& r,Ui& ui,const D2D1_RECT_F& list,ShellHost& host,Settings& set) {
    const float s=r.Scale();FilterSites(set);
    const auto input=Rc(list.left,list.top,list.right-302*s,list.top+34*s);
    m_search.Draw(r,ui,Id("sites.query"),input,i18n::Literal(L"Site adı, adres veya dil ara..."),true,0);
    if(w::TextButton(r,ui,Id("sites.languages"),RcWH(list.right-294*s,list.top,130*s,34*s),set.siteLanguages.empty()?i18n::Literal(L"Diller: tümü"):i18n::Literal(L"Diller: ")+std::to_wstring(set.siteLanguages.size()),m_siteLanguagesOpen))m_siteLanguagesOpen=!m_siteLanguagesOpen;
    if(w::TextButton(r,ui,Id("sites.check"),RcWH(list.right-156*s,list.top,156*s,34*s),m_siteChecking?i18n::Literal(L"Denetimi durdur"):i18n::Literal(L"Erişimi denetle"),true)) {
        if(m_siteChecking)m_siteCancel=true;else StartSiteChecks(host,m_filteredMirrors);host.Invalidate();
    }
    size_t hidden=0;for(const auto& item:m_mirrorDisplayList)if(std::find(set.hiddenSites.begin(),set.hiddenSites.end(),item.nameW)!=set.hiddenSites.end())++hidden;
    const auto tabs=RcWH(list.left,list.top+42*s,std::min(400*s,RcW(list)),32*s);
    const int tab=w::Segmented(r,ui,Id("sites.visibility"),tabs,{i18n::Literal(L"Siteler (")+std::to_wstring(m_mirrorDisplayList.size()-hidden)+L")",i18n::Literal(L"Gizlenenler (")+std::to_wstring(hidden)+L")"},m_showHiddenSites?1:0);
    if(tab>=0 && (tab==1)!=m_showHiddenSites){m_showHiddenSites=tab==1;m_scroll[kSites]=0;FilterSites(set);}
    float y=list.top+84*s;
    if(m_siteLanguagesOpen){float x=list.left;const float height=28*s;
        auto chip=[&](const std::wstring& language,const std::wstring& label,bool selected){const float width=r.MeasureText(label,11*s)+24*s;if(x+width>list.right){x=list.left;y+=height+6*s;}
            const auto box=RcWH(x,y,width,height);x+=width+6*s;
            if(w::TextButton(r,ui,Id("sites.language",Id(ToUtf8(language).c_str())),box,label,selected)){
                if(language.empty())set.siteLanguages.clear();else {auto it=std::find(set.siteLanguages.begin(),set.siteLanguages.end(),language);if(it==set.siteLanguages.end())set.siteLanguages.push_back(language);else set.siteLanguages.erase(it);}
                host.SaveSettings();m_scroll[kSites]=0;FilterSites(set);host.Invalidate();
            }
        };
        chip(L"",i18n::Literal(L"Tümü"),set.siteLanguages.empty());
        for(const auto& language:m_siteLanguages){auto label=language;for(auto& c:label)c=(wchar_t)towupper(c);chip(language,label,std::find(set.siteLanguages.begin(),set.siteLanguages.end(),language)!=set.siteLanguages.end());}
        y+=height+10*s;
    }
    const auto progress=m_siteChecking?i18n::Literal(L"Denetleniyor: ")+std::to_wstring(m_siteCompleted.load())+L" / "+std::to_wstring(m_siteTotal.load()):std::to_wstring(m_filteredMirrors.size())+i18n::Literal(L" site • Ana sayfa erişimi ve kaynak işlemleri ayrı denetlenir");
    r.Text(progress,Rc(list.left,y,list.right,y+20*s),theme::TextMuted,11*s);y+=28*s;
    const auto body=Rc(list.left,y,list.right,list.bottom);const float height=112*s,gap=8*s,pitch=height+gap;
    m_pitch=pitch;m_listH=RcH(body);const float content=m_filteredMirrors.empty()?0:(float)m_filteredMirrors.size()*pitch-gap;
    const float scroll=ClampScroll(RcH(body),content);
    const auto statuses=engine::MirrorEngine::Instance().GetSourceStatuses();
    const auto& mirrors=engine::MirrorEngine::Instance().GetMirrors();
    if(!m_sitesAutoChecked){m_sitesAutoChecked=true;std::vector<size_t> pending;
        const int64_t nowMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        for(size_t index:m_filteredMirrors){const auto& name=mirrors[m_mirrorDisplayList[index].index].name;
            const auto it=std::find_if(statuses.begin(),statuses.end(),[&](const auto& status){return status.mirror==name && status.operation=="Erişim";});
            if(it==statuses.end() || nowMs-it->checkedAt>24LL*60*60*1000)pending.push_back(index);
        }
        StartSiteChecks(host,pending);
    }
    if(m_filteredMirrors.empty())r.Text(m_showHiddenSites?i18n::Literal(L"Bu filtrelerle gizlenen site yok."):i18n::Literal(L"Bu filtrelerle eşleşen site yok."),body,theme::TextMuted,13*s,Renderer::Align::Center);
    r.PushClip(body);
    for(size_t i=(size_t)std::max(0.0f,std::floor(scroll/pitch));i<m_filteredMirrors.size();++i){
        const float top=body.top+(float)i*pitch-scroll;if(top>=body.bottom)break;
        const auto index=m_filteredMirrors[i];const auto& item=m_mirrorDisplayList[index];const auto& mirror=mirrors[item.index];
        const engine::SourceStatus* access=nullptr;const engine::SourceStatus* last=nullptr;
        for(const auto& status:statuses)if(status.mirror==mirror.name){if(status.operation=="Erişim" && (!access || status.checkedAt>access->checkedAt))access=&status;else if(status.operation!="Erişim" && (!last || status.checkedAt>last->checkedAt))last=&status;}
        const auto row=Rc(body.left,top,body.right-12*s,top+height);
        const uint32_t tone=access?(access->success?theme::ReadTone:theme::UpdateErrorTone):theme::Panel;
        r.FillRound(row,12*s,tone,1);r.StrokeRound(row,12*s,theme::Border,1*s,0.65f);
        const auto icon=RcWH(row.left+10*s,top+12*s,36*s,36*s);DrawIcon(r,Icon::Globe,icon,20*s,theme::Accent);
        const float tx=icon.right+12*s,tr=row.right-278*s;
        r.Text(item.nameW+L"  ["+item.langUpper+L"]",Rc(tx,top+8*s,tr,top+29*s),theme::Text,14*s,Renderer::Align::Left,Renderer::Weight::Semibold);
        r.Text(item.domainStr+(mirror.disabled?i18n::Literal(L" • Kaynak modülü devre dışı"):i18n::Literal(L" • Kaynak modülü aktif")),Rc(tx,top+31*s,tr,top+49*s),theme::TextMuted,10.5f*s);
        const auto state=access?(access->success?i18n::Literal(L"Erişiliyor • HTTP ")+std::to_wstring(access->httpStatus):i18n::Literal(L"Otomatik erişim yok: ")+FromUtf8(access->error)):i18n::Literal(L"Erişim henüz denetlenmedi");
        r.Text(state,Rc(tx,top+52*s,row.right-10*s,top+73*s),access && !access->success?theme::Text:theme::TextMuted,11*s);
        const auto detail=last?FromUtf8(last->operation)+L": "+(last->success?i18n::Literal(L"başarılı"):FromUtf8(last->error))+(access?L" • ":L""):L"";
        r.Text(detail+(access?i18n::Literal(L"Son denetim: ")+FormatRelativeTime(access->checkedAt):L""),Rc(tx,top+78*s,row.right-10*s,top+100*s),theme::TextDim,10*s);
        const float by=top+12*s;
        if(w::TextButton(r,ui,Id("sites.open",(uint32_t)item.index),RcWH(row.right-268*s,by,96*s,30*s),i18n::Literal(L"Siteyi aç ↗"),false)){const auto url=mirror.home.empty()?(mirror.domains.empty()?"":"https://"+mirror.domains.front()):mirror.home;if(!url.empty())host.OpenUrl(FromUtf8(url));}
        if(w::TextButton(r,ui,Id("sites.probe",(uint32_t)item.index),RcWH(row.right-164*s,by,76*s,30*s),i18n::Literal(L"Denetle"),false,!m_siteChecking))StartSiteChecks(host,{index});
        if(w::TextButton(r,ui,Id("sites.hide",(uint32_t)item.index),RcWH(row.right-80*s,by,70*s,30*s),m_showHiddenSites?i18n::Literal(L"Göster"):i18n::Literal(L"Gizle"),false)){
            auto it=std::find(set.hiddenSites.begin(),set.hiddenSites.end(),item.nameW);if(it==set.hiddenSites.end())set.hiddenSites.push_back(item.nameW);else set.hiddenSites.erase(it);
            host.SaveSettings();host.Invalidate();r.PopClip();return;
        }
    }
    r.PopClip();DrawScrollbar(r,ui,body,content,host);
}

void LibraryView::DrawEmpty(Renderer& r, Ui& ui, const D2D1_RECT_F& area, ShellHost& host) {
    const float s = r.Scale();
    const float midX = (area.left + area.right) * 0.5f;
    const float midY = (area.top + area.bottom) * 0.5f - 40.0f * s;

    const D2D1_RECT_F ghostBox = RcWH(midX - 40.0f * s, midY - 40.0f * s, 80.0f * s, 80.0f * s);
    r.FillRound(ghostBox, 24.0f * s, theme::Panel, 1.0f);
    DrawIcon(r, Icon::Ghost, ghostBox, 44.0f * s, theme::Accent);

    r.Text(T(S::EmptyLibTitle), Rc(midX - 250 * s, midY + 54 * s, midX + 250 * s, midY + 84 * s), theme::Text,
           20.0f * s, Renderer::Align::Center, Renderer::Weight::Semibold);
    r.Text(T(S::EmptyLibBody), Rc(midX - 260 * s, midY + 88 * s, midX + 260 * s, midY + 130 * s), theme::TextMuted,
           13.5f * s, Renderer::Align::Center, Renderer::Weight::Normal, 1.0f, true);

    const float btnW = 200.0f * s, btnH = 38.0f * s;
    const D2D1_RECT_F importRc = RcWH(midX - btnW - 8.0f * s, midY + 144.0f * s, btnW, btnH);
    const D2D1_RECT_F openRc = RcWH(midX + 8.0f * s, midY + 144.0f * s, btnW, btnH);

    if (w::TextButton(r, ui, Id("lib.empty.import"), importRc, T(S::ImportAmr), true)) {
        host.ImportAmrDialog();
    }
    if (w::TextButton(r, ui, Id("lib.empty.open"), openRc, T(S::OpenLocal), false)) {
        host.OpenFolderDialog();
    }
}

void LibraryView::FetchChaptersForManga(const std::string& key, const std::string& mirror, const std::string& mangaUrl, const std::string& lastChapUrl, ShellHost& host) {
    if (m_stopping) return;
    auto buildChapterItems = [](const std::vector<std::pair<std::string, std::string>>& rawPairs, const std::string& lastChap) -> std::vector<ChapterItem> {
        std::vector<ChapterItem> items;
        items.reserve(rawPairs.size());

        int curIdx = -1;
        for (int i = 0; i < (int)rawPairs.size(); ++i) {
            if (!lastChap.empty() && rawPairs[i].second == lastChap) {
                curIdx = i;
                break;
            }
        }

        bool isDescending = true;
        if (rawPairs.size() >= 2) {
            double numFirst = ParseChapterNumber(rawPairs.front().first);
            double numLast = ParseChapterNumber(rawPairs.back().first);
            if (numFirst >= 0 && numLast >= 0 && numFirst < numLast) {
                isDescending = false; // 1, 2, 3...
            }
        }

        double curNum = -1.0;
        if (curIdx >= 0) {
            curNum = ParseChapterNumber(rawPairs[curIdx].first);
        }

        for (int i = 0; i < (int)rawPairs.size(); ++i) {
            ChapterItem ci;
            ci.title = rawPairs[i].first;
            ci.url = rawPairs[i].second;
            ci.isCurrent = (i == curIdx);

            if (curIdx >= 0) {
                ci.isRead = isDescending ? (i >= curIdx) : (i <= curIdx);
            } else if (curNum >= 0.0) {
                double num = ParseChapterNumber(ci.title);
                ci.isRead = (num >= 0.0 && num <= curNum);
            } else {
                ci.isRead = false;
            }
            items.push_back(std::move(ci));
        }
        return items;
    };

    // 1. Check in-memory cache
    {
        std::lock_guard<std::mutex> lock(m_chapterMutex);
        auto& cache = m_chapterCache[key];
        if (cache.mirror.empty()) cache.mirror = mirror;
        if (cache.mangaUrl.empty()) cache.mangaUrl = mangaUrl;
        if (!cache.chapters.empty()) return;
        if (cache.loading) return;
    }

    // 2. Try loading from persistent disk cache immediately
    std::vector<std::pair<std::string, std::string>> cachedPairs;
    if (amr::LoadCachedChapterList(key, cachedPairs)) {
        std::vector<Chapter> catalogue;
        for (const auto& ch : cachedPairs) catalogue.push_back({ch.first, ch.second});
        host.StoreChapterCatalogue(key, catalogue);
        auto items = buildChapterItems(cachedPairs, lastChapUrl);
        {
            std::lock_guard<std::mutex> lock(m_chapterMutex);
            auto& cache = m_chapterCache[key];
            cache.chapters = std::move(items);
            cache.loading = false;
            cache.failed = false;
        }
        host.Invalidate();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_chapterMutex);
        auto& cache = m_chapterCache[key];
        cache.loading = true;
        cache.failed = false;
    }

    std::string safeMangaUrl = mangaUrl;
    if (safeMangaUrl.rfind("http://", 0) == 0) {
        safeMangaUrl = "https://" + safeMangaUrl.substr(7);
    }

    m_workers.emplace_back([this, key, mirror, safeMangaUrl, lastChapUrl, buildChapterItems, &host]() {
        try {
            auto rawChapters = engine::MirrorEngine::Instance().GetChapters(mirror, safeMangaUrl, key.substr(key.find_last_of('_') + 1));
            if (rawChapters.empty()) {
                host.CrawlChaptersForManga(safeMangaUrl, [this, key, lastChapUrl, buildChapterItems, &host](const std::vector<std::pair<std::string, std::string>>& crawled) {
                    if (m_stopping) return;
                    if (crawled.empty()) {
                        host.StoreChapterCatalogue(key, {});
                        std::lock_guard<std::mutex> lock(m_chapterMutex);
                        auto& cache = m_chapterCache[key];
                        cache.loading = false;
                        cache.failed = true;
                        SetUpdateError(key, i18n::Literal(L"Bölüm listesi alınamadı (Site koruması veya bağlantı engeli)"));
                        host.Invalidate();
                        return;
                    }
                    amr::SaveCachedChapterList(key, crawled);
                    std::vector<Chapter> catalogue;
        for (const auto& ch : crawled) catalogue.push_back({ch.first, ch.second});
        host.StoreChapterCatalogue(key, catalogue);
        auto items = buildChapterItems(crawled, lastChapUrl);
                    {
                        std::lock_guard<std::mutex> lock(m_chapterMutex);
                        auto& cache = m_chapterCache[key];
                        cache.chapters = std::move(items);
                        cache.loading = false;
                        cache.failed = false;
                        ClearUpdateError(key);
                    }
                    host.Invalidate();
                });
                return;
            }

            std::vector<std::pair<std::string, std::string>> pairs;
            pairs.reserve(rawChapters.size());
            for (const auto& rc : rawChapters) pairs.push_back({ rc.title, rc.url });
            amr::SaveCachedChapterList(key, pairs);
            std::vector<Chapter> catalogue;
        for (const auto& ch : pairs) catalogue.push_back({ch.first, ch.second});
        host.StoreChapterCatalogue(key, catalogue);
        auto items = buildChapterItems(pairs, lastChapUrl);

            {
                std::lock_guard<std::mutex> lock(m_chapterMutex);
                auto& cache = m_chapterCache[key];
                cache.chapters = std::move(items);
                cache.loading = false;
                cache.failed = cache.chapters.empty();
                if (cache.failed) SetUpdateError(key, i18n::Literal(L"Bölüm listesi boş döndü"));
                else ClearUpdateError(key);
            }
        } catch (...) {
            host.StoreChapterCatalogue(key, {});
            std::lock_guard<std::mutex> lock(m_chapterMutex);
            auto& cache = m_chapterCache[key];
            cache.loading = false;
            cache.failed = true;
            SetUpdateError(key, i18n::Literal(L"Bağlantı hatası oluştu"));
        }
        host.Invalidate();
    });
}

void LibraryView::DrawDetailCard(Renderer& r, Ui& ui, const D2D1_RECT_F& area, Library& lib, ShellHost& host, Settings& set) {
    if (m_selectedRowIndex < 0 || (size_t)m_selectedRowIndex >= m_rows.size()) {
        m_selectedRowIndex = -1;
        m_cardMode = 0;
        return;
    }
    const float s = r.Scale();
    const Row& row = m_rows[(size_t)m_selectedRowIndex];

    // Dim background
    r.Fill(area, theme::Black, 0.65f);

    // ---------------------- MOD 1: BOLUM LISTESI (GECMIS & OKUNMAMIS) ----------------------
    if (m_cardMode == 1) {
        const float cardW = std::min(720.0f * s, RcW(area) - 40.0f * s);
        const float cardH = std::min(600.0f * s, RcH(area) - 40.0f * s);
        const D2D1_RECT_F card = RcWH(area.left + (RcW(area) - cardW) * 0.5f, area.top + (RcH(area) - cardH) * 0.5f, cardW, cardH);

        // Kart disina basilinca kapat
        if (ui.In().pressed && !RcHit(card, ui.In().mx, ui.In().my)) {
            m_selectedRowIndex = -1;
            m_cardMode = 0;
            host.Invalidate();
            return;
        }

        r.FillRound(card, 16.0f * s, theme::Elevated, 1.0f);
        r.StrokeRound(card, 16.0f * s, theme::Border, 1.5f * s, 0.8f);

        // Kapat butonu sag ust
        const D2D1_RECT_F closeRc = RcWH(card.right - 36.0f * s, card.top + 14.0f * s, 26.0f * s, 26.0f * s);
        if (w::IconButton(r, ui, Id("card.ch.close"), closeRc, Icon::Close, 16.0f * s, i18n::Literal(L"Kapat"))) {
            m_selectedRowIndex = -1;
            m_cardMode = 0;
            host.Invalidate();
            return;
        }

        // Geri butonu sol ust
        const float backW = 76.0f * s, backH = 28.0f * s;
        const D2D1_RECT_F backRc = RcWH(card.left + 20.0f * s, card.top + 14.0f * s, backW, backH);
        if (w::TextButton(r, ui, Id("card.ch.back"), backRc, i18n::Literal(L"‹ Geri"), false)) {
            m_cardMode = 0;
            host.Invalidate();
            return;
        }

        // Baslik ve kaynak rozeti
        const float tx = backRc.right + 14.0f * s;
        const auto removeRc=RcWH(closeRc.left-34*s,closeRc.top,26*s,26*s);
        if(w::IconButton(r,ui,Id("card.ch.remove"),removeRc,Icon::Trash,17*s,i18n::Text(L"Listeden kaldır"))) {
            const auto key=row.key;host.DeleteSeriesFromLibrary(key);m_selectedRowIndex=-1;m_cardMode=0;m_rowsRev=0;m_displayDirty=true;host.Invalidate();return;
        }
        const float tr = removeRc.left - 12.0f * s;
        const float titleW = r.MeasureText(row.title, 16.0f * s, Renderer::Weight::Bold);
        const float badgeWidth=row.mirror.empty()?0:r.MeasureText(FromUtf8(row.mirror),11*s,Renderer::Weight::Semibold)+14*s;
        const float badgeSpace=std::min(std::max(0.0f,(tr-tx)*0.4f),badgeWidth+8*s);
        const float titleRight = std::min(tx+titleW,tr-badgeSpace);
        r.Text(row.title, Rc(tx, card.top + 16.0f * s, titleRight, card.top + 40.0f * s), theme::Text, 16.0f * s, Renderer::Align::Left, Renderer::Weight::Bold);

        if (!row.mirror.empty()) {
            const float bH = 20.0f * s;
            const float bY = card.top + 18.0f * s;
            const float bW=std::min(badgeWidth,std::max(0.0f,tr-titleRight-8*s));
            DrawMirrorBadge(r, row.mirror, RcWH(titleRight + 8.0f * s, bY, bW, bH), s);
        }

        const Series* detailSeries = lib.FindSeries(row.key);
        std::wstring details = i18n::Literal(L"Son okunan: ") + (row.chapterLabel.empty() ? i18n::Literal(L"Henüz okunmadı") : row.chapterLabel);
        details += L"   •   "+i18n::Text(L"En yeni: ") + (row.latestChapterLabel.empty() ? i18n::Literal(L"Bilinmiyor") : row.latestChapterLabel);
        r.Text(details, Rc(card.left + 20*s, card.top + 48*s, card.right - 20*s, card.top + 69*s), theme::TextMuted, 11*s);
        const std::wstring state = detailSeries && !detailSeries->lastUpdateError.empty() ? i18n::Literal(L"Güncellenemedi: ") + FromUtf8(detailSeries->lastUpdateError) : i18n::Literal(L"Kaydedilen bölüm: ") + std::to_wstring(detailSeries ? detailSeries->chapters.size() : 0);
        r.Text(state, Rc(card.left + 20*s, card.top + 70*s, card.right - 20*s, card.top + 91*s), theme::TextDim, 11*s);
        std::string next;
        { std::lock_guard<std::mutex> lock(m_chapterMutex);
          auto it = m_chapterCache.find(row.key);
          if (it != m_chapterCache.end() && !it->second.chapters.empty()) {
            const auto& chapters = it->second.chapters;
            if (row.lastChapterUrl.empty()) next = chapters.back().url;
            else for (size_t i=1; i<chapters.size(); ++i) if (ChapterUrlIdentity(chapters[i].url) == ChapterUrlIdentity(row.lastChapterUrl)) { next=chapters[i-1].url; break; }
          }
        }
        const float actionW=cardW-40*s, actionGap=8*s;
        const float resumeW=std::min(90*s,actionW*0.23f), nextW=std::min(130*s,actionW*0.30f);
        if (w::TextButton(r, ui, Id("card.ch.resume"), RcWH(card.left+20*s,card.top+96*s,resumeW,30*s), i18n::Literal(L"Oku"), true)) {
            host.OpenOnlineManga(row.mirror,row.mangaUrl,row.title,row.lastChapterUrl.empty()?"__FIRST__":row.lastChapterUrl);
            m_selectedRowIndex=-1; return;
        }
        if (w::TextButton(r, ui, Id("card.ch.next"), RcWH(card.left+20*s+resumeW+actionGap,card.top+96*s,nextW,30*s), i18n::Literal(L"Sonraki bölüm"), false, !next.empty())) {
            host.OpenOnlineManga(row.mirror,row.mangaUrl,row.title,next); m_selectedRowIndex=-1; return;
        }
        if (w::TextButton(r, ui, Id("card.ch.other"), RcWH(card.left+20*s+resumeW+nextW+2*actionGap,card.top+96*s,actionW-resumeW-nextW-2*actionGap,30*s), i18n::Literal(L"Başka kaynaklarda ara"), false)) {
            const auto title=row.title; host.DispatchUI([this,title,&host](){SearchOtherSources(title,host);}); return;
        }

        const float progressW=(actionW-actionGap)*0.5f;
        const auto markRc=RcWH(card.left+20*s,card.top+134*s,progressW,30*s);
        const auto resetRc=RcWH(markRc.right+actionGap,markRc.top,progressW,30*s);
        if(w::TextButton(r,ui,Id("card.ch.markallread"),markRc,i18n::Literal(L"✅ Son Sayıya Kadar Okundu"),false)) {
            host.SetReadingProgress(row.key,true);m_rowsRev=0;m_displayDirty=true;host.Invalidate();return;
        }
        if(w::TextButton(r,ui,Id("card.ch.resetread"),resetRc,i18n::Literal(L"🔄 Okumayı Sıfırla"),false)) {
            host.SetReadingProgress(row.key,false);m_rowsRev=0;m_displayDirty=true;host.Invalidate();return;
        }

        // Keep every action inside the popup, also at narrow window widths.
        float cy = card.top + 178.0f * s;
        const float ctrlH = 32.0f * s;
        const bool narrow=actionW<480*s;
        const float tabW = narrow?actionW:240.0f*s;
        const D2D1_RECT_F tabRc = RcWH(card.right - 20.0f * s - tabW, cy+(narrow?40*s:0), tabW, ctrlH);
        const int pickTab = w::Segmented(r, ui, Id("card.ch.filter"), tabRc, { i18n::Literal(L"Tümü"), i18n::Literal(L"Okunmamış"), i18n::Literal(L"Okunanlar") }, m_chapterFilterTab);
        if (pickTab >= 0 && pickTab != m_chapterFilterTab) {
            m_chapterFilterTab = pickTab;
            m_chapterScroll = 0.0f;
        }

        const float sortW = 95.0f * s;
        const D2D1_RECT_F sortRc = RcWH(narrow?card.right-20*s-sortW:tabRc.left-8*s-sortW, cy, sortW, ctrlH);
        if (w::TextButton(r, ui, Id("card.ch.sortdir"), sortRc, m_chapterSortAsc ? i18n::Literal(L"↑ Bölüm 1") : i18n::Literal(L"↓ En Yeni"), false)) {
            m_chapterSortAsc = !m_chapterSortAsc;
            m_chapterNeedsScrollToCurrent = true;
            host.Invalidate();
        }
        ui.Tip(Id("card.ch.sortdir"), m_chapterSortAsc ? i18n::Literal(L"Sıralama: Eskiden Yeniye (Bölüm 1 başta)") : i18n::Literal(L"Sıralama: Yeniden Eskiye (En son bölüm başta)"), sortRc);

        const float searchLeft = card.left + 20.0f * s;
        const float searchW = std::max(120.0f * s, sortRc.left - 8.0f * s - searchLeft);
        const D2D1_RECT_F searchRc = RcWH(searchLeft, cy, searchW, ctrlH);
        m_chapterSearch.Draw(r, ui, Id("card.ch.search"), searchRc, i18n::Literal(L"Bölüm ara..."), true, 0.0);

        cy += ctrlH + 12.0f * s + (narrow?40*s:0);
        r.Line(card.left + 20.0f * s, cy, card.right - 20.0f * s, cy, theme::Border, 1.0f * s, 0.4f);
        cy += 8.0f * s;

        // Bolum listesi alani
        const D2D1_RECT_F listRc = Rc(card.left + 20.0f * s, cy, card.right - 20.0f * s, card.bottom - 16.0f * s);
        const float listH = RcH(listRc);

        MangaChapterCache cacheCopy;
        {
            std::lock_guard<std::mutex> lock(m_chapterMutex);
            auto it = m_chapterCache.find(row.key);
            if (it != m_chapterCache.end()) cacheCopy = it->second;
        }

        if (cacheCopy.chapters.empty()) {
            std::vector<std::pair<std::string, std::string>> diskPairs;
            if (amr::LoadCachedChapterList(row.key, diskPairs) && !diskPairs.empty()) {
                FetchChaptersForManga(row.key, row.mirror, row.mangaUrl, row.lastChapterUrl, host);
                std::lock_guard<std::mutex> lock(m_chapterMutex);
                cacheCopy = m_chapterCache[row.key];
            } else if (!cacheCopy.loading && !cacheCopy.failed) {
                FetchChaptersForManga(row.key, row.mirror, row.mangaUrl, row.lastChapterUrl, host);
                std::lock_guard<std::mutex> lock(m_chapterMutex);
                cacheCopy = m_chapterCache[row.key];
            }
        }

        {
            std::vector<Chapter> chapters;
            for (const auto& ch : cacheCopy.chapters) chapters.push_back({ch.title, ch.url});
            const int current = ChapterIndex(chapters, row.lastChapterUrl);
            for (size_t i = 0; i < cacheCopy.chapters.size(); ++i) {
                cacheCopy.chapters[i].isCurrent = (int)i == current && current >= 0;
                cacheCopy.chapters[i].isRead = current >= 0 && (int)i >= current;
            }
        }
        if (cacheCopy.loading) {
            r.Text(i18n::Literal(L"Bölümler kaynaktan çekiliyor, lütfen bekleyin..."), listRc, theme::Accent, 14.0f * s, Renderer::Align::Center);
        } else if (cacheCopy.failed) {
            r.Text(i18n::Literal(L"Bölüm listesi çekilemedi veya kaynak yanıt vermedi."), Rc(listRc.left, listRc.top + 30.0f * s, listRc.right, listRc.top + 60.0f * s), theme::TextDim, 14.0f * s, Renderer::Align::Center);
            const D2D1_RECT_F retryRc = RcWH(listRc.left + (RcW(listRc) - 150.0f * s) * 0.5f, listRc.top + 70.0f * s, 150.0f * s, 34.0f * s);
            if (w::TextButton(r, ui, Id("card.ch.retry"), retryRc, i18n::Literal(L"🔄 Yeniden Dene"), true)) {
                FetchChaptersForManga(row.key, row.mirror, row.mangaUrl, row.lastChapterUrl, host);
                host.Invalidate();
            }
        } else {
            // Filtre uygula
            std::wstring q = ToLowerInvariant(m_chapterSearch.text);
            std::vector<const ChapterItem*> displayChapters;
            for (const auto& ci : cacheCopy.chapters) {
                if (!q.empty()) {
                    std::wstring titleW = ToLowerInvariant(FromUtf8(ci.title));
                    if (titleW.find(q) == std::wstring::npos) continue;
                }
                if (m_chapterFilterTab == 1 && ci.isRead) continue;    // Yalniz okunmamis
                if (m_chapterFilterTab == 2 && !ci.isRead) continue;   // Yalniz okunanlar
                displayChapters.push_back(&ci);
            }

            if (m_chapterSortAsc) {
                std::reverse(displayChapters.begin(), displayChapters.end());
            }

            if (displayChapters.empty()) {
                r.Text(i18n::Literal(L"Eşleşen bölüm bulunamadı."), listRc, theme::TextMuted, 13.5f * s, Renderer::Align::Center);
            } else {
                const float itemH = 38.0f * s;
                const float gap = 4.0f * s;
                const float pitch = itemH + gap;
                const float totalH = (float)displayChapters.size() * pitch - gap;
                m_chapterMaxScroll = std::max(0.0f, totalH - listH);

                // En son okunan bolumun pozisyonuna gore acilma (Kullanici istegi)
                if (m_chapterNeedsScrollToCurrent) {
                    m_chapterNeedsScrollToCurrent = false;
                    int curIdx = -1;
                    for (size_t ci = 0; ci < displayChapters.size(); ++ci) {
                        if (displayChapters[ci]->isCurrent || (!row.lastChapterUrl.empty() && displayChapters[ci]->url == row.lastChapterUrl)) {
                            curIdx = (int)ci;
                            break;
                        }
                    }
                    if (curIdx >= 0) {
                        float targetY = (float)curIdx * pitch - (listH * 0.45f);
                        m_chapterScroll = std::clamp(targetY, 0.0f, m_chapterMaxScroll);
                    }
                }
                m_chapterScroll = std::clamp(m_chapterScroll, 0.0f, m_chapterMaxScroll);

                r.PushClip(listRc);
                const size_t first = (size_t)std::max(0.0f, std::floor(m_chapterScroll / pitch));
                for (size_t ci = first; ci < displayChapters.size(); ++ci) {
                    const float itemY = listRc.top + (float)ci * pitch - m_chapterScroll;
                    if (itemY >= listRc.bottom) break;

                    const ChapterItem& ch = *displayChapters[ci];
                    const D2D1_RECT_F itemRc = RcWH(listRc.left, itemY, RcW(listRc) - (m_chapterMaxScroll > 0 ? 10.0f * s : 0.0f), itemH);
                    const D2D1_RECT_F hitRc = Rc(itemRc.left, std::max(itemRc.top, listRc.top), itemRc.right, std::min(itemRc.bottom, listRc.bottom));
                    const uint32_t cid = Id("card.ch.row", (uint32_t)ci);
                    const bool rowClicked = (RcH(hitRc) > 1.0f) && ui.Button(cid, hitRc);
                    const bool hot = ui.Hot(cid);

                    if (ch.isCurrent) {
                        r.FillRound(itemRc, 8.0f * s, theme::Active, 0.20f);
                        r.StrokeRound(itemRc, 8.0f * s, theme::ActiveEdge, 1.0f * s, 0.5f);
                    } else if (hot) {
                        r.FillRound(itemRc, 8.0f * s, theme::White, 0.06f);
                    } else {
                        r.FillRound(itemRc, 8.0f * s, theme::Panel, 1.0f);
                    }

                    // Butonlar sagda
                    float bx = itemRc.right - 8.0f * s;
                    const float btnH = 26.0f * s;
                    const float by = itemRc.top + (itemH - btnH) * 0.5f;

                    // Oku butonu
                    const float rdW = 72.0f * s;
                    bx -= rdW;
                    const D2D1_RECT_F rdRc = RcWH(bx, by, rdW, btnH);
                    const bool readClicked = (RcH(hitRc) >= btnH * 0.7f) && w::TextButton(r, ui, Id("card.ch.rd", (uint32_t)ci), rdRc, i18n::Literal(L"⚡ Oku"), true);
                    if (readClicked || rowClicked) {

                        host.OpenOnlineManga(cacheCopy.mirror.empty() ? row.mirror : cacheCopy.mirror,
                                             cacheCopy.mangaUrl.empty() ? row.mangaUrl : cacheCopy.mangaUrl,
                                             row.title, ch.url);
                        m_selectedRowIndex = -1;
                        m_cardMode = 0;
                        r.PopClip();
                        return;
                    }
                    bx -= 8.0f * s;

                    // Durum Rozeti (Son Okunan / Okundu / Yeni)
                    float tagLeft = bx;
                    if (ch.isCurrent) {
                        const float tW = 90.0f * s;
                        tagLeft -= tW;
                        const D2D1_RECT_F tagRc = RcWH(tagLeft, itemRc.top + (itemH - 22.0f * s) * 0.5f, tW, 22.0f * s);
                        r.FillRound(tagRc, 11.0f * s, theme::Active, 0.85f);
                        r.Text(i18n::Literal(L"📍 Son Okunan"), tagRc, theme::White, 11.0f * s, Renderer::Align::Center, Renderer::Weight::Semibold);
                    } else if (ch.isRead) {
                        const float tW = 68.0f * s;
                        tagLeft -= tW;
                        const D2D1_RECT_F tagRc = RcWH(tagLeft, itemRc.top + (itemH - 22.0f * s) * 0.5f, tW, 22.0f * s);
                        r.FillRound(tagRc, 11.0f * s, theme::Elevated, 1.0f);
                        r.Text(i18n::Literal(L"✓ Okundu"), tagRc, theme::TextMuted, 11.0f * s, Renderer::Align::Center);
                    } else {
                        const float tW = 56.0f * s;
                        tagLeft -= tW;
                        const D2D1_RECT_F tagRc = RcWH(tagLeft, itemRc.top + (itemH - 22.0f * s) * 0.5f, tW, 22.0f * s);
                        r.FillRound(tagRc, 11.0f * s, 0xF1C40F, 0.22f);
                        r.StrokeRound(tagRc, 11.0f * s, 0xF1C40F, 1.0f * s, 0.40f);
                        r.Text(i18n::Literal(L"⚡ Yeni"), tagRc, 0xF1C40F, 11.0f * s, Renderer::Align::Center, Renderer::Weight::Semibold);
                    }

                    // Bolum basligi
                    std::wstring chapTitle = CleanChapterTitle(FromUtf8(ch.title), (int)ci);
                    r.Text(chapTitle, Rc(itemRc.left + 12.0f * s, itemRc.top, tagLeft - 10.0f * s, itemRc.bottom),
                           ch.isCurrent ? theme::Text : (ch.isRead ? theme::TextDim : theme::Text),
                           13.5f * s, Renderer::Align::Left,
                           ch.isCurrent ? Renderer::Weight::Semibold : Renderer::Weight::Normal);
                }
                r.PopClip();

                if (m_chapterMaxScroll > 0.0f) {
                    const float barW = 4.0f * s;
                    const float trackH = listH;
                    const float thumbH = std::max(20.0f * s, trackH * (listH / totalH));
                    const float thumbY = listRc.top + (m_chapterScroll / m_chapterMaxScroll) * (trackH - thumbH);
                    const D2D1_RECT_F thumbRc = RcWH(card.right - 20.0f * s - barW, thumbY, barW, thumbH);
                    r.FillRound(thumbRc, barW * 0.5f, theme::TextMuted, 0.4f);
                }
            }
        }
        return;
    }

    // ---------------------- MOD 0: GENEL BAKIS & KAYNAKLAR ----------------------
    std::vector<size_t> mirrorsForThisManga;
    for (size_t ri = 0; ri < m_rows.size(); ++ri) {
        if (m_rows[ri].normTitle == row.normTitle) {
            mirrorsForThisManga.push_back(ri);
        }
    }

    const bool multiMirror = (mirrorsForThisManga.size() > 1);
    const float cardW = std::min(620.0f * s, RcW(area) - 40.0f * s);
    const float extraH = multiMirror ? std::min(280.0f * s, (float)mirrorsForThisManga.size() * 56.0f * s + 36.0f * s) : 220.0f * s;
    const float cardH = std::min(RcH(area)-40*s,std::min(560.0f * s, 220.0f * s + extraH));
    const D2D1_RECT_F card = RcWH(area.left + (RcW(area) - cardW) * 0.5f, area.top + (RcH(area) - cardH) * 0.5f, cardW, cardH);

    // Kart disina basilinca kapat
    if (ui.In().pressed && !RcHit(card, ui.In().mx, ui.In().my)) {
        m_selectedRowIndex = -1;
        m_cardMode = 0;
        host.Invalidate();
        return;
    }

    r.FillRound(card, 16.0f * s, theme::Elevated, 1.0f);
    r.StrokeRound(card, 16.0f * s, theme::Border, 1.5f * s, 0.8f);

    // Kapat butonu sag ust
    const D2D1_RECT_F closeRc = RcWH(card.right - 36.0f * s, card.top + 14.0f * s, 26.0f * s, 26.0f * s);
    if (w::IconButton(r, ui, Id("card.close"), closeRc, Icon::Close, 16.0f * s, i18n::Literal(L"Kapat"))) {
        m_selectedRowIndex = -1;
        m_cardMode = 0;
        host.Invalidate();
        return;
    }

    // Baslik
    const auto removeRc=RcWH(closeRc.left-34*s,closeRc.top,26*s,26*s);
    if(w::IconButton(r,ui,Id("card.remove"),removeRc,Icon::Trash,17*s,i18n::Text(multiMirror?i18n::Literal(L"Grubu listeden kaldır"):i18n::Literal(L"Listeden kaldır")))) {
        std::vector<std::string> keys;if(multiMirror){for(auto i:mirrorsForThisManga)keys.push_back(m_rows[i].key);}else keys.push_back(row.key);
        for(const auto& key:keys)host.DeleteSeriesFromLibrary(key);
        m_selectedRowIndex=-1;m_cardMode=0;m_rowsRev=0;m_displayDirty=true;host.Invalidate();return;
    }
    float y = card.top + 20.0f * s;
    r.Text(row.title, Rc(card.left + 24.0f * s, y, removeRc.left - 12.0f * s, y + 26.0f * s), theme::Text, 17.0f * s, Renderer::Align::Left, Renderer::Weight::Bold);
    y += 30.0f * s;

    // Alt bilgi (Ayna & Son Bolum / Kaynak Sayisi)
    std::wstring subHeader = multiMirror ? (std::to_wstring(mirrorsForThisManga.size()) + i18n::Literal(L" Farklı Kaynak / Ayna Takip Ediliyor")) : row.sub;
    r.Text(subHeader, Rc(card.left + 24.0f * s, y, card.right - 24.0f * s, y + 20.0f * s), theme::TextMuted, 13.0f * s);
    y += 24.0f * s;

    if (w::TextButton(r, ui, Id("card.other"), RcWH(card.left+24*s, y,202*s,28*s), i18n::Literal(L"Başka kaynaklarda ara"), false)) {
        const auto title=row.title; host.DispatchUI([this,title,&host](){SearchOtherSources(title,host);}); return;
    }
    y += 36*s;
    // Cizgi
    r.Line(card.left + 24.0f * s, y, card.right - 24.0f * s, y, theme::Border, 1.0f * s, 0.4f);
    y += 14.0f * s;

    if (multiMirror) {
        // Coklu kaynak listesi
        r.Text(i18n::Literal(L"📚 Mevcut Kaynaklar ve Aynalar:"), Rc(card.left + 24.0f * s, y, card.right - 24.0f * s, y + 20.0f * s),
               theme::Accent, 13.0f * s, Renderer::Align::Left, Renderer::Weight::Semibold);
        y += 24.0f * s;

        const float mItemH = 48.0f * s;
        const float gap = 6.0f * s;
        const auto sourcesRc=Rc(card.left+24*s,y,card.right-24*s,card.bottom-16*s);
        m_cardSourcesMaxScroll=std::max(0.0f,(float)mirrorsForThisManga.size()*(mItemH+gap)-gap-RcH(sourcesRc));
        m_cardSourcesScroll=std::clamp(m_cardSourcesScroll,0.0f,m_cardSourcesMaxScroll);
        r.PushClip(sourcesRc);
        for (size_t mi = 0; mi < mirrorsForThisManga.size(); ++mi) {
            y=sourcesRc.top+(float)mi*(mItemH+gap)-m_cardSourcesScroll;
            if(y+mItemH<=sourcesRc.top)continue;if(y>=sourcesRc.bottom)break;

            const size_t mRowIdx = mirrorsForThisManga[mi];
            const Row& mr = m_rows[mRowIdx];
            const D2D1_RECT_F itemRc = RcWH(card.left + 24.0f * s, y, cardW - 48.0f * s, mItemH);

            r.FillRound(itemRc, 8.0f * s, theme::Panel, 1.0f);

            // Ayna ve Dil Rozeti
            const float mBadgeH = 20.0f * s;
            const float mBadgeY = itemRc.top + 8.0f * s;
            std::wstring mLabel = FromUtf8(mr.mirror);
            if (!mr.lang.empty()) mLabel += L" [" + mr.lang + L"]";
            const float mBadgeW = std::min(r.MeasureText(mLabel, 11.0f * s, Renderer::Weight::Semibold)+14*s,std::max(60*s,RcW(itemRc)-240*s));
            DrawMirrorBadge(r, mr.mirror, RcWH(itemRc.left + 12.0f * s, mBadgeY, mBadgeW, mBadgeH), s);

            // Son bolum / zaman
            std::wstring mSub = mr.relLastRead.empty() ? mr.relUpts : (i18n::Literal(L"Son: ") + mr.relLastRead);
            r.Text(mSub, Rc(itemRc.left + 14.0f * s + mBadgeW, itemRc.top + 8.0f * s, itemRc.left + 250.0f * s, itemRc.bottom - 8.0f * s),
                   theme::TextMuted, 11.5f * s, Renderer::Align::Left);

            // Butonlar sagda
            const float btnH = 28.0f * s;
            const float by = itemRc.top + (mItemH - btnH) * 0.5f;
            float bx = itemRc.right - 8.0f * s;

            // Sil (X)
            const float delW = 28.0f * s;
            bx -= delW;
            const bool actionable=by>=sourcesRc.top && by+btnH<=sourcesRc.bottom;
            if (w::IconButton(r, ui, Id("card.m.del", (uint32_t)mRowIdx), RcWH(bx, by, delW, btnH), Icon::Trash, 17.0f * s, i18n::Text(L"Bu kaynağı listeden kaldır"),false,actionable)) {
                r.PopClip();
                host.DeleteSeriesFromLibrary(mr.key);
                m_selectedRowIndex = -1;
                m_cardMode = 0;
                m_rowsRev = 0;
                m_displayDirty = true;
                host.Invalidate();
                return;
            }
            bx -= 6.0f * s;

            // Tum Bolumler
            const float chW = 88.0f * s;
            bx -= chW;
            if (w::TextButton(r, ui, Id("card.m.ch", (uint32_t)mRowIdx), RcWH(bx, by, chW, btnH), i18n::Literal(L"📑 Bölümler"), false,actionable)) {
                r.PopClip();
                m_selectedRowIndex = (int)mRowIdx;
                m_cardMode = 1;
                m_chapterSearch.Clear();
                m_chapterFilterTab = 0;
                m_chapterScroll = 0.0f;
                m_chapterNeedsScrollToCurrent = true;
                FetchChaptersForManga(mr.key, mr.mirror, mr.mangaUrl, mr.lastChapterUrl, host);
                host.Invalidate();
                return;
            }
            bx -= 6.0f * s;

            // Oku
            const float rdW = 68.0f * s;
            bx -= rdW;
            if (w::TextButton(r, ui, Id("card.m.rd", (uint32_t)mRowIdx), RcWH(bx, by, rdW, btnH), i18n::Literal(L"⚡ Oku"), true,actionable)) {
                r.PopClip();

                if (!mr.mirror.empty() && !mr.mangaUrl.empty()) {
                    host.OpenOnlineManga(mr.mirror, mr.mangaUrl, mr.title, !mr.lastChapterUrl.empty() ? mr.lastChapterUrl : "__FIRST__");
                } else {
                    host.OpenUrl(mr.openUrl);
                }
                m_selectedRowIndex = -1;
                m_cardMode = 0;
                return;
            }

            y += mItemH + gap;
        }
        r.PopClip();
        if(m_cardSourcesMaxScroll>0){const float h=std::max(24*s,RcH(sourcesRc)*RcH(sourcesRc)/((float)mirrorsForThisManga.size()*(mItemH+gap)-gap));r.FillRound(RcWH(sourcesRc.right-3*s,sourcesRc.top+(RcH(sourcesRc)-h)*m_cardSourcesScroll/m_cardSourcesMaxScroll,3*s,h),1.5f*s,theme::TextMuted,0.6f);}
    } else {
        m_cardSourcesScroll=0;m_cardSourcesMaxScroll=0;
        // Tek kaynak: Buyuk eylem butonlari
        std::wstring details;
        if (row.lastReadTs > 0) details += i18n::Literal(L"Son okuma: ") + row.relLastRead;
        if (row.upts > 0) {
            if (!details.empty()) details += L"  ·  ";
            details += i18n::Literal(L"Son güncelleme: ") + row.relUpts;
        }
        if (!details.empty()) {
            r.Text(details, Rc(card.left + 24.0f * s, y, card.right - 24.0f * s, y + 20.0f * s), theme::TextDim, 12.0f * s);
            y += 24.0f * s;
        }

        const float btnH = 38.0f * s;
        const float btnGap = 10.0f * s;
        const float btnW = cardW - 48.0f * s;

        // 1. Kalinan Yerden Oku & Ilk Bolumu Oku / En Son Bolum
        const float halfBtnW = (btnW - 10.0f * s) * 0.5f;
        const D2D1_RECT_F readRc = RcWH(card.left + 24.0f * s, y, halfBtnW, btnH);
        const D2D1_RECT_F readFirstRc = RcWH(readRc.right + 10.0f * s, y, halfBtnW, btnH);

        bool hasProgress = !row.lastChapterUrl.empty();
        std::wstring primaryLabel = hasProgress ? i18n::Literal(L"⚡ Kaldığın Yerden Oku") : i18n::Literal(L"📖 Okumaya Başla");
        std::wstring secondaryLabel = hasProgress ? i18n::Literal(L"⏮ İlk Bölüm (Bölüm 1)") : i18n::Literal(L"🚀 En Son Çıkan Bölüm");

        if (w::TextButton(r, ui, Id("card.read"), readRc, primaryLabel, true)) {

            if (!row.mirror.empty() && !row.mangaUrl.empty()) {
                host.OpenOnlineManga(row.mirror, row.mangaUrl, row.title, hasProgress ? row.lastChapterUrl : "__FIRST__");
            } else {
                host.OpenUrl(row.openUrl);
            }
            m_selectedRowIndex = -1;
            m_cardMode = 0;
            return;
        }
        if (w::TextButton(r, ui, Id("card.readfirst"), readFirstRc, secondaryLabel, false)) {

            if (!row.mirror.empty() && !row.mangaUrl.empty()) {
                host.OpenOnlineManga(row.mirror, row.mangaUrl, row.title, hasProgress ? "__FIRST__" : (!row.latestChapterUrl.empty() ? row.latestChapterUrl : "__LAST__"));
            } else {
                host.OpenUrl(row.openUrl);
            }
            m_selectedRowIndex = -1;
            m_cardMode = 0;
            return;
        }
        y += btnH + btnGap;

        // 2. Tum Bolumler (Gecmis ve Okunmamis)
        const D2D1_RECT_F chListRc = RcWH(card.left + 24.0f * s, y, btnW, btnH);
        if (w::TextButton(r, ui, Id("card.allchapters"), chListRc, i18n::Literal(L"📑 Tüm Bölümler (Geçmiş ve Okunmamış)"), false)) {
            m_cardMode = 1;
            m_chapterSearch.Clear();
            m_chapterFilterTab = 0;
            m_chapterScroll = 0.0f;
            m_chapterNeedsScrollToCurrent = true;
            FetchChaptersForManga(row.key, row.mirror, row.mangaUrl, row.lastChapterUrl, host);
            host.Invalidate();
            return;
        }
        y += btnH + btnGap;

        // 3. Okuma Durumu Kontrolleri (Son sayiya kadar okundu & Okumayi Sifirla)
        const float halfActW = (btnW - 10.0f * s) * 0.5f;
        const D2D1_RECT_F markAllRc = RcWH(card.left + 24.0f * s, y, halfActW, btnH);
        const D2D1_RECT_F resetRc = RcWH(markAllRc.right + 10.0f * s, y, halfActW, btnH);

        if (w::TextButton(r, ui, Id("card.markallread"), markAllRc, i18n::Literal(L"✅ Son Sayıya Kadar Okundu"), false)) {
            host.SetReadingProgress(row.key, true);
            m_rowsRev = 0;
            m_displayDirty = true;
            host.Invalidate();
        }
        ui.Tip(Id("card.markallread"), i18n::Literal(L"Sitedeki son bölüme kadar tüm bölümleri okundu olarak işaretle"), markAllRc);

        if (w::TextButton(r, ui, Id("card.resetread"), resetRc, i18n::Literal(L"🔄 Okumayı Sıfırla"), false)) {
            host.SetReadingProgress(row.key, false);
            m_rowsRev = 0;
            m_displayDirty = true;
            host.Invalidate();
        }
        ui.Tip(Id("card.resetread"), i18n::Literal(L"Okuma geçmişini sıfırlar ve seriyi okunmadı yapar"), resetRc);
        y += btnH + btnGap;

        // 4. Guncelleme Uyarisi (Hata varsa)
        if (row.updateFailed || HasUpdateError(row.key)) {
            const D2D1_RECT_F errRc = RcWH(card.left + 24.0f * s, y, btnW, 28.0f * s);
            r.FillRound(errRc, 6.0f * s, 0xF39C12, 0.18f);
            r.StrokeRound(errRc, 6.0f * s, 0xF39C12, 1.0f * s, 0.40f);
            r.Text(i18n::Literal(L"⚠️ Güncelleme Uyarısı: Kaynaktan son bölümler çekilemedi"),
                   errRc, 0xF39C12, 11.5f * s, Renderer::Align::Center, Renderer::Weight::Semibold);
            y += 32.0f * s;
        }

        // 5. Kutuphaneden Sil
        const D2D1_RECT_F delRc = RcWH(card.left + 24.0f * s, y, btnW, btnH);
        if (w::TextButton(r, ui, Id("card.del"), delRc, i18n::Literal(L"🗑️ Bu Seriyi Kütüphaneden Kaldır"), false)) {
            host.DeleteSeriesFromLibrary(row.key);
            m_selectedRowIndex = -1;
            m_cardMode = 0;
            m_rowsRev = 0;
            m_displayDirty = true;
            host.Invalidate();
            return;
        }
    }
}

void LibraryView::DrawRowChapterDropdown(Renderer& r, Ui& ui, const D2D1_RECT_F& area, ShellHost& host, double now) {
    if (!m_chDropdownOpen) return;

    const float s = r.Scale();
    const D2D1_RECT_F anchor = m_chDropdownAnchorRc;

    const float dropW = 380.0f * s;
    const float dropH = std::min(460.0f * s, RcH(area) - 40.0f * s);
    float dropX = anchor.right - dropW;
    if (dropX < area.left + 16.0f * s) dropX = area.left + 16.0f * s;
    if (dropX + dropW > area.right - 16.0f * s) dropX = area.right - 16.0f * s - dropW;

    float dropY = anchor.bottom + 4.0f * s;
    if (dropY + dropH > area.bottom - 12.0f * s) {
        if (anchor.top - dropH - 4.0f * s >= area.top + 12.0f * s) {
            dropY = anchor.top - dropH - 4.0f * s; // Yukariya dogru ac
        } else {
            dropY = std::max(area.top + 12.0f * s, area.bottom - dropH - 12.0f * s);
        }
    }
    m_chDropdownRc = RcWH(dropX, dropY, dropW, dropH);

    // Dropdown disina basilinca kapat
    if (ui.In().pressed && !RcHit(m_chDropdownRc, ui.In().mx, ui.In().my) && !RcHit(anchor, ui.In().mx, ui.In().my)) {
        m_chDropdownOpen = false;
        m_chDropdownSearch.focused = false;
        host.Invalidate();
        return;
    }

    // Golge ve panel zemini
    r.FillRound(RcWH(dropX, dropY + 4.0f * s, dropW, dropH), 10.0f * s, theme::Ground, 0.65f);
    r.FillRound(m_chDropdownRc, 10.0f * s, theme::Panel, 1.0f);
    r.StrokeRound(m_chDropdownRc, 10.0f * s, theme::Border, 1.2f * s, 0.95f);

    const float pad = 12.0f * s;
    float curY = dropY + pad;

    // Baslik & Kaynak Rozeti
    const float titleH = 22.0f * s;
    const float maxTitleW = dropW - pad * 2.0f - 80.0f * s;
    r.Text(m_chDropdownRow.title, RcWH(dropX + pad, curY, maxTitleW, titleH), theme::Text, 14.0f * s, Renderer::Align::Left, Renderer::Weight::Bold);
    if (!m_chDropdownRow.mirror.empty()) {
        const float bH = 18.0f * s;
        const float bW = r.MeasureText(FromUtf8(m_chDropdownRow.mirror), 10.0f * s, Renderer::Weight::Semibold) + 12.0f * s;
        DrawMirrorBadge(r, m_chDropdownRow.mirror, RcWH(dropX + dropW - pad - bW, curY + 2.0f * s, bW, bH), s);
    }
    curY += titleH + 8.0f * s;

    // Arama kutusu + Siralama Toggle butonu
    const float ctrlH = 32.0f * s;
    const float sortW = 36.0f * s;
    const float searchW = dropW - pad * 2.0f - sortW - 6.0f * s;
    const D2D1_RECT_F searchRc = RcWH(dropX + pad, curY, searchW, ctrlH);
    m_chDropdownSearch.Draw(r, ui, Id("chdrop.search"), searchRc, i18n::Literal(L"Bölüm ara..."), true, now);

    const D2D1_RECT_F sortRc = RcWH(searchRc.right + 6.0f * s, curY, sortW, ctrlH);
    if (w::TextButton(r, ui, Id("chdrop.sort"), sortRc, m_chDropdownSortAsc ? L"↑" : L"↓", false)) {
        m_chDropdownSortAsc = !m_chDropdownSortAsc;
        m_chDropdownNeedsScrollToCurrent = true;
        host.Invalidate();
    }
    ui.Tip(Id("chdrop.sort"), m_chDropdownSortAsc ? i18n::Literal(L"Sıralama: Eskiden Yeniye (Bölüm 1 başta)") : i18n::Literal(L"Sıralama: Yeniden Eskiye (En son bölüm başta)"), sortRc);
    curY += ctrlH + 10.0f * s;

    r.Line(dropX + pad, curY, dropX + dropW - pad, curY, theme::Border, 1.0f * s, 0.4f);
    curY += 8.0f * s;

    // Bolum listesi alani
    const float listH = (dropY + dropH - pad) - curY;
    const D2D1_RECT_F listRc = RcWH(dropX + pad, curY, dropW - pad * 2.0f, listH);

    MangaChapterCache cacheCopy;
    {
        std::lock_guard<std::mutex> lock(m_chapterMutex);
        auto it = m_chapterCache.find(m_chDropdownRow.key);
        if (it != m_chapterCache.end()) cacheCopy = it->second;
    }

    if (cacheCopy.chapters.empty()) {
        std::vector<std::pair<std::string, std::string>> diskPairs;
        if (amr::LoadCachedChapterList(m_chDropdownRow.key, diskPairs) && !diskPairs.empty()) {
            FetchChaptersForManga(m_chDropdownRow.key, m_chDropdownRow.mirror, m_chDropdownRow.mangaUrl, m_chDropdownRow.lastChapterUrl, host);
            std::lock_guard<std::mutex> lock(m_chapterMutex);
            cacheCopy = m_chapterCache[m_chDropdownRow.key];
        } else if (!cacheCopy.loading && !cacheCopy.failed) {
            FetchChaptersForManga(m_chDropdownRow.key, m_chDropdownRow.mirror, m_chDropdownRow.mangaUrl, m_chDropdownRow.lastChapterUrl, host);
            std::lock_guard<std::mutex> lock(m_chapterMutex);
            cacheCopy = m_chapterCache[m_chDropdownRow.key];
        }
    }

        {
            std::vector<Chapter> chapters;
            for (const auto& ch : cacheCopy.chapters) chapters.push_back({ch.title, ch.url});
            const int current = ChapterIndex(chapters, m_chDropdownRow.lastChapterUrl);
            for (size_t i = 0; i < cacheCopy.chapters.size(); ++i) {
                cacheCopy.chapters[i].isCurrent = (int)i == current && current >= 0;
                cacheCopy.chapters[i].isRead = current >= 0 && (int)i >= current;
            }
        }
    if (cacheCopy.loading) {
        r.Text(i18n::Literal(L"⚡ Bölümler taranıyor..."), listRc, theme::Accent, 13.0f * s, Renderer::Align::Center);
        return;
    }
    if (cacheCopy.failed) {
        r.Text(i18n::Literal(L"Bölümler çekilemedi."), Rc(listRc.left, listRc.top + 20.0f * s, listRc.right, listRc.top + 50.0f * s), theme::TextDim, 13.0f * s, Renderer::Align::Center);
        const D2D1_RECT_F retryRc = RcWH(listRc.left + (RcW(listRc) - 130.0f * s) * 0.5f, listRc.top + 60.0f * s, 130.0f * s, 32.0f * s);
        if (w::TextButton(r, ui, Id("chdrop.retry"), retryRc, i18n::Literal(L"🔄 Yeniden Dene"), true)) {
            FetchChaptersForManga(m_chDropdownRow.key, m_chDropdownRow.mirror, m_chDropdownRow.mangaUrl, m_chDropdownRow.lastChapterUrl, host);
            host.Invalidate();
        }
        return;
    }
    if (cacheCopy.chapters.empty()) {
        r.Text(i18n::Literal(L"Bölüm bulunamadı."), listRc, theme::TextMuted, 13.0f * s, Renderer::Align::Center);
        return;
    }

    // Arama filtreleme
    std::wstring q = m_chDropdownSearch.text;
    for (auto& c : q) c = towlower(c);

    std::vector<const ChapterItem*> displayChapters;
    displayChapters.reserve(cacheCopy.chapters.size());

    if (m_chDropdownSortAsc) {
        for (int i = (int)cacheCopy.chapters.size() - 1; i >= 0; --i) {
            const auto& ci = cacheCopy.chapters[i];
            if (q.empty()) {
                displayChapters.push_back(&ci);
            } else {
                std::wstring t = CleanChapterTitle(FromUtf8(ci.title), i);
                for (auto& c : t) c = towlower(c);
                if (t.find(q) != std::wstring::npos) displayChapters.push_back(&ci);
            }
        }
    } else {
        for (size_t i = 0; i < cacheCopy.chapters.size(); ++i) {
            const auto& ci = cacheCopy.chapters[i];
            if (q.empty()) {
                displayChapters.push_back(&ci);
            } else {
                std::wstring t = CleanChapterTitle(FromUtf8(ci.title), (int)i);
                for (auto& c : t) c = towlower(c);
                if (t.find(q) != std::wstring::npos) displayChapters.push_back(&ci);
            }
        }
    }

    if (displayChapters.empty()) {
        r.Text(i18n::Literal(L"Aramaya uygun bölüm bulunamadı."), listRc, theme::TextMuted, 12.5f * s, Renderer::Align::Center);
        return;
    }

    const float itemH = 34.0f * s;
    const float gap = 4.0f * s;
    const float pitch = itemH + gap;
    const float totalH = (float)displayChapters.size() * pitch - gap;
    m_chDropdownMaxScroll = std::max(0.0f, totalH - listH);

    // En son okunan sayinin pozisyonuna gore acilma (Kullanici istegi)
    if (m_chDropdownNeedsScrollToCurrent) {
        m_chDropdownNeedsScrollToCurrent = false;
        int curIdx = -1;
        for (size_t ci = 0; ci < displayChapters.size(); ++ci) {
            const ChapterItem& ch = *displayChapters[ci];
            if (ch.isCurrent || (!m_chDropdownRow.lastChapterUrl.empty() && ch.url == m_chDropdownRow.lastChapterUrl)) {
                curIdx = (int)ci;
                break;
            }
        }
        if (curIdx >= 0) {
            float targetY = (float)curIdx * pitch - (listH * 0.45f);
            m_chDropdownScroll = std::clamp(targetY, 0.0f, m_chDropdownMaxScroll);
        }
    }
    m_chDropdownScroll = std::clamp(m_chDropdownScroll, 0.0f, m_chDropdownMaxScroll);

    r.PushClip(listRc);
    const size_t first = (size_t)std::max(0.0f, std::floor(m_chDropdownScroll / pitch));
    for (size_t ci = first; ci < displayChapters.size(); ++ci) {
        const float itemY = listRc.top + (float)ci * pitch - m_chDropdownScroll;
        if (itemY >= listRc.bottom) break;

        const ChapterItem& ch = *displayChapters[ci];
        const float barReserve = (m_chDropdownMaxScroll > 0.0f) ? 14.0f * s : 0.0f;
        const D2D1_RECT_F itemRc = RcWH(listRc.left, itemY, RcW(listRc) - barReserve, itemH);
        const D2D1_RECT_F hitRc = Rc(itemRc.left, std::max(itemRc.top, listRc.top), itemRc.right, std::min(itemRc.bottom, listRc.bottom));

        const uint32_t cid = Id("chdrop.row", (uint32_t)ci);
        const bool clicked = (RcH(hitRc) > 1.0f) && ui.Button(cid, hitRc);
        const bool hot = ui.Hot(cid);

        bool isCurrent = ch.isCurrent || (!m_chDropdownRow.lastChapterUrl.empty() && ch.url == m_chDropdownRow.lastChapterUrl);

        if (isCurrent) {
            r.FillRound(itemRc, 6.0f * s, theme::Active, theme::ActiveAlpha);
            r.StrokeRound(itemRc, 6.0f * s, theme::ActiveEdge, 1.0f * s, theme::ActiveEdgeAlpha);
        } else if (hot) {
            r.FillRound(itemRc, 6.0f * s, theme::White, 0.08f);
        } else {
            r.FillRound(itemRc, 6.0f * s, theme::Panel2, 0.40f);
        }

        std::wstring cleanTitle = CleanChapterTitle(FromUtf8(ch.title));
        const float rightMargin = isCurrent ? 84.0f * s : (ch.isRead ? 36.0f * s : 8.0f * s);
        r.Text(cleanTitle, Rc(itemRc.left + 8.0f * s, itemRc.top, itemRc.right - rightMargin, itemRc.bottom),
               isCurrent ? theme::Text : (ch.isRead ? theme::TextMuted : theme::Text),
               12.5f * s, Renderer::Align::Left, isCurrent ? Renderer::Weight::Semibold : Renderer::Weight::Normal);

        if (isCurrent) {
            const D2D1_RECT_F curTagRc = RcWH(itemRc.right - 78.0f * s, itemRc.top + (itemH - 18.0f * s) * 0.5f, 72.0f * s, 18.0f * s);
            r.FillRound(curTagRc, 9.0f * s, theme::Active, 0.9f);
            r.Text(i18n::Literal(L"Kaldığın Yer"), curTagRc, theme::White, 10.0f * s, Renderer::Align::Center, Renderer::Weight::Semibold);
        } else if (ch.isRead) {
            r.Text(L"✓", Rc(itemRc.right - 26.0f * s, itemRc.top, itemRc.right - 6.0f * s, itemRc.bottom),
                   theme::TextMuted, 12.0f * s, Renderer::Align::Center);
        }

        if (clicked) {
            host.OpenOnlineManga(m_chDropdownRow.mirror, m_chDropdownRow.mangaUrl, m_chDropdownRow.title, ch.url);
            m_chDropdownOpen = false;
            m_chDropdownSearch.focused = false;
            host.Invalidate();
            break;
        }
    }
    r.PopClip();

    // Kaydirma cubugu
    if (m_chDropdownMaxScroll > 0.0f) {
        const float barW = 8.0f * s;
        const float trackH = listH;
        const float thumbH = std::max(24.0f * s, trackH * (listH / totalH));
        const float thumbY = listRc.top + (m_chDropdownScroll / m_chDropdownMaxScroll) * (trackH - thumbH);
        const D2D1_RECT_F trackRc = RcWH(listRc.right - barW, listRc.top, barW, trackH);
        const D2D1_RECT_F thumbRc = RcWH(listRc.right - barW, thumbY, barW, thumbH);

        r.FillRound(trackRc, barW * 0.5f, theme::Panel2, 0.45f);
        r.FillRound(thumbRc, barW * 0.5f, theme::TextMuted, 0.6f);
    }
}

void LibraryView::DrawResetConfirmModal(Renderer& r, Ui& ui, const D2D1_RECT_F& area, Library& lib, Settings& set, ShellHost& host) {
    const float s = r.Scale();
    r.Fill(area, theme::Black, 0.75f);

    const float modalW = std::min(520.0f * s, RcW(area) - 40.0f * s);
    const float modalH = 260.0f * s;
    const D2D1_RECT_F modalRc = RcWH((RcW(area) - modalW) * 0.5f, (RcH(area) - modalH) * 0.5f, modalW, modalH);

    r.FillRound(modalRc, 16.0f * s, theme::Elevated, 1.0f);
    r.StrokeRound(modalRc, 16.0f * s, m_resetConfirmStep == 2 ? 0xE74C3C : theme::Border, 1.5f * s, 0.95f);

    // Kapat (X) butonu
    const D2D1_RECT_F closeRc = RcWH(modalRc.right - 36.0f * s, modalRc.top + 14.0f * s, 24.0f * s, 24.0f * s);
    if (w::IconButton(r, ui, Id("reset.modal.close"), closeRc, Icon::Close, 14.0f * s, i18n::Literal(L"Kapat"))) {
        m_resetConfirmStep = 0;
        host.Invalidate();
        return;
    }

    float y = modalRc.top + 24.0f * s;
    if (m_resetConfirmStep == 1) {
        // Asama 1: Ilk Onay
        r.Text(i18n::Literal(L"⚠️ Kütüphaneyi Sıfırla (1/2 Onay)"), Rc(modalRc.left + 24.0f * s, y, modalRc.right - 44.0f * s, y + 26.0f * s),
               theme::Text, 17.0f * s, Renderer::Align::Left, Renderer::Weight::Bold);
        y += 34.0f * s;

        r.Text(i18n::Literal(L"Tüm kütüphanedeki manga listesini ve okuma kayıtlarını silmek istediğinize emin misiniz?\n\nBu işlem geri alınamaz!"),
               Rc(modalRc.left + 24.0f * s, y, modalRc.right - 24.0f * s, y + 80.0f * s),
               theme::TextMuted, 13.5f * s, Renderer::Align::Left, Renderer::Weight::Normal, 1.0f, true);
        y += 92.0f * s;

        const float btnW = (modalW - 58.0f * s) * 0.5f;
        const float btnH = 38.0f * s;
        const D2D1_RECT_F cancelRc = RcWH(modalRc.left + 24.0f * s, y, btnW, btnH);
        const D2D1_RECT_F nextRc = RcWH(cancelRc.right + 10.0f * s, y, btnW, btnH);

        if (w::TextButton(r, ui, Id("reset.modal.cancel"), cancelRc, i18n::Literal(L"Vazgeç / İptal"), false)) {
            m_resetConfirmStep = 0;
            host.Invalidate();
            return;
        }
        if (w::TextButton(r, ui, Id("reset.modal.step1"), nextRc, i18n::Literal(L"Evet, Silmek İstiyorum (1/2) ›"), false)) {
            m_resetConfirmStep = 2;
            host.Invalidate();
            return;
        }
    } else if (m_resetConfirmStep == 2) {
        // Asama 2: Ikinci ve Son Onay (Kullanici istegi: 2 kere sorsun sinir bozucu ama oyle olsun :D)
        r.Text(i18n::Literal(L"🚨 DİKKAT: GERÇEKTEN HER ŞEY SİLİNSİN Mİ? (2/2)"),
               Rc(modalRc.left + 24.0f * s, y, modalRc.right - 44.0f * s, y + 26.0f * s),
               0xE74C3C, 15.0f * s, Renderer::Align::Left, Renderer::Weight::Bold);
        y += 34.0f * s;

        r.Text(i18n::Literal(L"DİKKAT: Bu son güvenlik kontrolüdür!\nKütüphanedeki TÜM seriler, okuma geçmişi ve ayarlar kalıcı olarak temizlenecektir!\n\nSon kez soruyoruz: Emin misiniz?"),
               Rc(modalRc.left + 24.0f * s, y, modalRc.right - 24.0f * s, y + 80.0f * s),
               theme::Text, 13.0f * s, Renderer::Align::Left, Renderer::Weight::Normal, 1.0f, true);
        y += 92.0f * s;

        const float btnW = (modalW - 58.0f * s) * 0.5f;
        const float btnH = 38.0f * s;
        const D2D1_RECT_F cancelRc = RcWH(modalRc.left + 24.0f * s, y, btnW, btnH);
        const D2D1_RECT_F wipeRc = RcWH(cancelRc.right + 10.0f * s, y, btnW, btnH);

        if (w::TextButton(r, ui, Id("reset.modal.cancel2"), cancelRc, i18n::Literal(L"Hayır, Vazgeç!"), false)) {
            m_resetConfirmStep = 0;
            host.Invalidate();
            return;
        }
        if (w::TextButton(r, ui, Id("reset.modal.wipe"), wipeRc, i18n::Literal(L"💥 EVET, HEPSİNİ SİL!"), true)) {
            lib.ClearAll();
            m_resetConfirmStep = 0;
            m_rowsRev = 0;
            m_selectedRowIndex = -1;
            m_cardMode = 0;
            m_displayDirty = true;
            host.Toast(i18n::Literal(L"Kütüphane başarıyla tamamen sıfırlandı."));
            host.Invalidate();
            return;
        }
    }
}

static std::wstring NormalizeForSearch(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) {
        if (iswalnum(c)) out.push_back(towlower(c));
        else if (iswspace(c) || c == L'-' || c == L'_' || c == L':') {
            if (out.empty() || out.back() != L' ') out.push_back(L' ');
        }
    }
    while (!out.empty() && out.back() == L' ') out.pop_back();
    return out;
}

static int CalculateSearchRelevance(const std::wstring& titleW, const std::wstring& queryW, size_t mirrorCount) {
    std::wstring t = NormalizeForSearch(titleW);
    std::wstring q = NormalizeForSearch(queryW);
    if (t.empty() || q.empty()) return 0;

    int score = 0;
    if (t == q) {
        score = 10000;
    } else if (t.rfind(q, 0) == 0) { // Starts with
        int diff = (int)t.length() - (int)q.length();
        score = 7500 - std::min(2000, diff * 15);
    } else {
        size_t pos = t.find(q);
        if (pos != std::wstring::npos) { // Substring
            int diff = (int)t.length() - (int)q.length();
            score = 5000 - (int)pos * 40 - std::min(1500, diff * 10);
        } else {
            // Word token match
            std::vector<std::wstring> qWords;
            size_t start = 0;
            while (start < q.size()) {
                size_t sp = q.find(L' ', start);
                if (sp == std::wstring::npos) sp = q.size();
                if (sp > start) qWords.push_back(q.substr(start, sp - start));
                start = sp + 1;
            }
            int matchedWords = 0;
            for (const auto& w : qWords) {
                if (t.find(w) != std::wstring::npos) matchedWords++;
            }
            if (!qWords.empty() && matchedWords == (int)qWords.size()) {
                score = 3500 - std::min(1000, (int)t.length() * 5);
            } else if (matchedWords > 0) {
                score = (matchedWords * 2000) / (int)qWords.size();
            }
        }
    }
    // Multi-mirror popularity bonus
    score += (int)std::min((size_t)10, mirrorCount) * 150;
    return std::max(1, score);
}

void LibraryView::EnsureSearchTab() {
    if(m_activeSearch) return;
    auto session=std::make_shared<SearchSession>();session->id=m_nextSearchId++;
    m_searchTabs.push_back(session);m_activeSearch=session;
}
bool LibraryView::NewSearchTab(ShellHost& host) {
    if(m_searchTabs.size()>=5) {host.Toast(i18n::Literal(L"En fazla 5 arama sekmesi açılabilir. Önce bir sekmeyi kapatın."));return false;}
    if(m_activeSearch) {m_activeSearch->scroll=m_scroll[kSearch];m_activeSearch->maxScroll=m_maxScroll[kSearch];m_activeSearch->input.focused=false;}
    auto session=std::make_shared<SearchSession>();session->id=m_nextSearchId++;
    m_searchTabs.push_back(session);m_activeSearch=session;m_scroll[kSearch]=m_maxScroll[kSearch]=0;session->input.focused=true;
    return true;
}
void LibraryView::SelectSearchTab(size_t index) {
    if(index>=m_searchTabs.size()) return;
    if(m_activeSearch) {m_activeSearch->scroll=m_scroll[kSearch];m_activeSearch->maxScroll=m_maxScroll[kSearch];m_activeSearch->input.focused=false;}
    m_activeSearch=m_searchTabs[index];m_scroll[kSearch]=m_activeSearch->scroll;m_maxScroll[kSearch]=m_activeSearch->maxScroll;
}
void LibraryView::CancelSearch(const std::shared_ptr<SearchSession>& session) {
    if(!session) return;
    std::lock_guard<std::mutex> lock(session->mutex);
    ++session->generation;session->searching=false;session->status=i18n::Literal(L"Arama iptal edildi. Bulunan sonuçlar korundu.");
}
void LibraryView::CloseSearchTab(size_t index) {
    if(index>=m_searchTabs.size()) return;
    const auto closing=m_searchTabs[index];CancelSearch(closing);
    const bool active=closing==m_activeSearch;
    m_searchTabs.erase(m_searchTabs.begin()+index);
    if(active) {
        m_activeSearch.reset();
        if(!m_searchTabs.empty()) {m_activeSearch=m_searchTabs[std::min(index,m_searchTabs.size()-1)];m_scroll[kSearch]=m_activeSearch->scroll;m_maxScroll[kSearch]=m_activeSearch->maxScroll;}
        else {m_scroll[kSearch]=m_maxScroll[kSearch]=0;}
    }
}
void LibraryView::SearchOtherSources(const std::wstring& title, ShellHost& host) {
    m_tab=kSearch;m_selectedRowIndex=-1;m_cardMode=0;m_chDropdownOpen=false;
    LaunchGlobalMangaSearch(title,host);host.Invalidate();
}

void LibraryView::LaunchGlobalMangaSearch(const std::wstring& query, ShellHost& host) {
    if (m_stopping) return;
    std::wstring trimmed = query;
    while (!trimmed.empty() && iswspace(trimmed.front())) trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && iswspace(trimmed.back())) trimmed.pop_back();
    if (trimmed.empty()) return;

    EnsureSearchTab();
    if (!m_activeSearch->query.empty() && !NewSearchTab(host)) return;
    const auto session = m_activeSearch;
    session->input.SetText(trimmed);
    uint64_t currentGen = ++m_activeSearch->generation;
    m_activeSearch->query = trimmed;
    m_activeSearch->searching = true;
    {
        std::lock_guard<std::mutex> lock(m_activeSearch->mutex);
        m_activeSearch->status = i18n::Literal(L"Kaynaklar taranıyor...");
        m_activeSearch->results.clear();
    }
    m_scroll[kSearch] = 0.0f;
    host.Invalidate();

    m_workers.emplace_back([this, session, trimmed, currentGen, &host]() {
        try {
        const auto& allMirrors = engine::MirrorEngine::Instance().GetMirrors();
        std::vector<std::string> activeMirrors;

        // Correct registered names; responsive catalogues produce useful results
        // before older/offline sources spend their request deadlines.
        std::vector<std::string> priority = {
            "MangaDex V5", "Manga Buddy", "Manga Pill", "Manga-Fox", "Manga Here",
            "Manga Read", "Manga Katana", "Manga-Freak", "Asura Scans", "Rizz Comic",
            "Kiryuu", "Night Scans", "WebToons"
        };
        const auto searchable = [](const engine::MirrorInfo& mirror) {
            return !mirror.disabled && mirror.name != "Test Site" && mirror.name != "Komga" &&
                mirror.name != "Tachidesk" && (mirror.home.starts_with("https://") || mirror.home.starts_with("http://"));
        };
        for (const auto& p : priority) {
            for (const auto& m : allMirrors) {
                if (searchable(m) && m.name == p) {
                    activeMirrors.push_back(m.name);
                    break;
                }
            }
        }
        // Diger etkin aynalar
        for (const auto& m : allMirrors) {
            if (searchable(m)) {
                if (std::find(activeMirrors.begin(), activeMirrors.end(), m.name) == activeMirrors.end()) {
                    activeMirrors.push_back(m.name);
                }
            }
        }
        const auto sourceStatuses = engine::MirrorEngine::Instance().GetSourceStatuses();
        const auto searchRank = [&](const std::string& name) {
            for (const auto& status : sourceStatuses) if (status.mirror == name && status.operation == "Arama")
                return status.success ? (status.resultCount > 0 ? 0 : 1) : 2;
            return 1;
        };
        std::stable_sort(activeMirrors.begin(), activeMirrors.end(), [&](const auto& a, const auto& b) { return searchRank(a) < searchRank(b); });

        std::string queryUtf8 = ToUtf8(trimmed);
        std::unordered_map<std::wstring, GroupedSearchItem> groupMap;

        int scanned = 0, matchingSources = 0, failedSources = 0;
        const auto cancelled = [&] { return m_stopping || !session->searching || session->generation.load() != currentGen; };
        for (const auto& mName : activeMirrors) {
            if (cancelled()) break;
            {
                std::lock_guard<std::mutex> lock(session->mutex);
                wchar_t statusBuf[160];
                swprintf_s(statusBuf, i18n::Literal(L"Taranıyor: %d/%d kaynak (%zu manga bulundu)"),
                    scanned, (int)activeMirrors.size(), session->results.size());
                session->status = std::wstring(statusBuf) + L" · " + FromUtf8(mName);
            }
            host.Invalidate();

            auto results = engine::MirrorEngine::Instance().SearchManga(mName, queryUtf8, cancelled);
            if (cancelled()) break;
            scanned++;
            bool failed = false;
            for (const auto& status : engine::MirrorEngine::Instance().GetSourceStatuses())
                if (status.mirror == mName && status.operation == "Arama" && !status.success) { failed = true; break; }
            if (!results.empty()) ++matchingSources;
            else if (failed) ++failedSources;

            {
                std::lock_guard<std::mutex> lock(session->mutex);
                if (session->generation.load() != currentGen) break;

                if (!results.empty()) {
                    for (const auto& r : results) {
                        if (r.title.empty() || r.url.empty()) continue;
                        std::wstring titleW = FromUtf8(r.title);
                        std::wstring normKey = NormalizeForSearch(titleW);
                        if (normKey.empty()) normKey = titleW;

                        auto it = groupMap.find(normKey);
                        if (it == groupMap.end()) {
                            GroupedSearchItem item;
                            item.title = r.title;
                            item.titleW = titleW;
                            item.initial = titleW.empty() ? L"M" : titleW.substr(0, 1);
                            item.mirrors.push_back({ mName, r.url });
                            groupMap.emplace(normKey, std::move(item));
                        } else {
                            bool exists = false;
                            for (const auto& mi : it->second.mirrors) {
                                if (mi.mirror == mName) { exists = true; break; }
                            }
                            if (!exists) {
                                it->second.mirrors.push_back({ mName, r.url });
                            }
                        }
                    }

                    // Skorlari hesapla ve sirala
                    std::vector<GroupedSearchItem> sortedList;
                    sortedList.reserve(groupMap.size());
                    for (auto& pair : groupMap) {
                        pair.second.relevanceScore = CalculateSearchRelevance(pair.second.titleW, trimmed, pair.second.mirrors.size());
                        sortedList.push_back(pair.second);
                    }
                    std::sort(sortedList.begin(), sortedList.end(), [](const GroupedSearchItem& a, const GroupedSearchItem& b) {
                        if (a.relevanceScore != b.relevanceScore) return a.relevanceScore > b.relevanceScore;
                        return a.mirrors.size() > b.mirrors.size();
                    });
                    for (auto& fresh : sortedList) for (const auto& old : session->results) {
                        if (fresh.title != old.title || old.selectedMirrorIdx >= old.mirrors.size()) continue;
                        const auto& selected = old.mirrors[old.selectedMirrorIdx];
                        for (size_t i=0;i<fresh.mirrors.size();++i) if (fresh.mirrors[i].mirror==selected.mirror && fresh.mirrors[i].url==selected.url) fresh.selectedMirrorIdx=i;
                    }
                    session->results = std::move(sortedList);
                }

                wchar_t statusBuf[128];
                swprintf_s(statusBuf, i18n::Literal(L"Taranıyor: %d/%d kaynak (%zu manga bulundu)"),
                           scanned, (int)activeMirrors.size(), session->results.size());
                session->status = statusBuf;
            }
            host.Invalidate();
        }

        if (!m_stopping && session->generation.load() == currentGen) {
            std::lock_guard<std::mutex> lock(session->mutex);
            if (session->generation.load() != currentGen) return;
            session->searching = false;
            wchar_t finBuf[256], countsBuf[160];
            swprintf_s(finBuf, i18n::Literal(L"Arama tamamlandı: %zu manga bulundu (%d kaynak tarandı)"),
                       session->results.size(), scanned);
            session->status = finBuf;
            swprintf_s(countsBuf, i18n::Literal(L" · %d sonuç veren, %d eşleşmeyen, %d hatalı kaynak"),
                matchingSources, scanned - matchingSources - failedSources, failedSources);
            session->status += L"\n" + std::wstring(countsBuf);
            host.Invalidate();
        }
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(session->mutex);
            if (session->generation.load() == currentGen) {
                session->searching = false;
                session->status = i18n::Literal(L"Arama hatası: ") + FromUtf8(e.what());
            }
            host.Invalidate();
        } catch (...) {
            std::lock_guard<std::mutex> lock(session->mutex);
            if (session->generation.load() == currentGen) {
                session->searching = false;
                session->status = i18n::Literal(L"Arama durduruldu. Kaynak Durumu sayfasını kontrol edin.");
            }
            host.Invalidate();
        }
    });
}

void LibraryView::DrawSearchTab(Renderer& r, Ui& ui, const D2D1_RECT_F& list, ShellHost& host, Settings& set, Library& lib, double now) {
    EnsureSearchTab();
    const auto session = m_activeSearch;
    m_scroll[kSearch] = session->scroll;
    m_maxScroll[kSearch] = session->maxScroll;
    const float s = r.Scale();
    const float tabHeight = 34*s;
    const float tabWidth = std::max(40.0f*s, (RcW(list)-40*s)/5);
    for(size_t i=0;i<m_searchTabs.size();++i) {
        const auto tab=m_searchTabs[i];
        const auto box=RcWH(list.left+i*tabWidth,list.top,tabWidth-4*s,tabHeight);
        const auto close=RcWH(box.right-25*s,box.top+4*s,22*s,26*s);
        const auto hit=Rc(box.left,box.top,close.left-2*s,box.bottom);
        r.FillRound(box,6*s,tab==session?theme::Active:theme::Panel,tab==session?0.32f:1.0f);
        if(ui.Button(Id("search.tab",tab->id),hit)) {SelectSearchTab(i);host.Invalidate();return;}
        const auto title=tab->query.empty()?i18n::Literal(L"Yeni arama"):tab->query;
        r.Text(title,RcInset(hit,8*s,0),theme::Text,12*s,Renderer::Align::Left,Renderer::Weight::Semibold,1,false,true,true);
        if(w::IconButton(r,ui,Id("search.close",tab->id),close,Icon::Close,12*s,i18n::Literal(L"Arama sekmesini kapat"))) {CloseSearchTab(i);ui.CancelActive();host.Invalidate();return;}
    }
    if(w::IconButton(r,ui,Id("search.new"),RcWH(list.right-32*s,list.top,30*s,tabHeight),Icon::Plus,16*s,i18n::Literal(L"Yeni arama (en fazla 5 sekme)"),false,m_searchTabs.size()<5)) {
        NewSearchTab(host);host.Invalidate();return;
    }
    const float topBarH = 38.0f * s;
    const D2D1_RECT_F searchBarRc = Rc(list.left, list.top+tabHeight+8*s, list.right, list.top+tabHeight+8*s + topBarH);

    // Arama butonu ve kutusu
    const float btnW = 150.0f * s;
    const float clearW = 75.0f * s;
    const D2D1_RECT_F searchBtnRc = RcWH(searchBarRc.right - btnW, searchBarRc.top, btnW, topBarH);
    const D2D1_RECT_F clearBtnRc = RcWH(searchBtnRc.left - 6.0f * s - clearW, searchBarRc.top, clearW, topBarH);
    const D2D1_RECT_F inputRc = Rc(searchBarRc.left, searchBarRc.top, clearBtnRc.left - 6.0f * s, searchBarRc.bottom);

    if (session->input.Draw(r, ui, Id("lib.gsearch.input"), inputRc, i18n::Literal(L"Tüm kaynaklarda manga veya webtoon ara (Örn: Solo Leveling, One Piece)..."), true, now)) {
        const auto query=session->input.text; LaunchGlobalMangaSearch(query, host); return;
    }

    if (w::TextButton(r, ui, Id("lib.gsearch.clear"), clearBtnRc, i18n::Literal(L"Temizle"), false)) {
        CancelSearch(session);
        session->input.Clear();
        {
            std::lock_guard<std::mutex> lock(session->mutex);
            session->results.clear();
            session->query.clear();
            session->status.clear();
        }
        session->scroll=0; session->maxScroll=0; m_scroll[kSearch]=0; m_maxScroll[kSearch]=0;
        host.Invalidate();
    }

    if (w::TextButton(r, ui, Id("lib.gsearch.btn"), searchBtnRc, session->searching ? i18n::Literal(L"Durdur") : i18n::Literal(L"🔍 Tümünde Ara"), true)) {
        if(session->searching) CancelSearch(session);
        else {const auto query=session->input.text;LaunchGlobalMangaSearch(query, host);}
        host.Invalidate();return;
    }

    std::wstring statusText;
    bool hasResults = false;
    {
        std::lock_guard<std::mutex> lock(session->mutex);
        statusText = session->status;
        hasResults = !session->results.empty();
    }

    // Reserve a second row only for the completed search breakdown.
    const float statusH = (statusText.find(L'\n') == std::wstring::npos ? 24.0f : 44.0f) * s;
    const D2D1_RECT_F statusRc = RcWH(list.left, searchBarRc.bottom + 6.0f * s, RcW(list), statusH);

    if (!statusText.empty()) {
        r.Text(statusText, statusRc, session->searching ? theme::Accent : theme::TextMuted, 12.0f * s, Renderer::Align::Left, Renderer::Weight::Normal, 1.0f, true);
    }

    const D2D1_RECT_F contentList = Rc(list.left, statusRc.bottom + 6.0f * s, list.right, list.bottom);

    // Bos Durum (Henuz arama yapilmadi veya sonuc yok)
    if (!hasResults) {
        if (!session->searching) {
            const float midX = (contentList.left + contentList.right) * 0.5f;
            const float midY = (contentList.top + contentList.bottom) * 0.5f - 30.0f * s;

            if (session->query.empty()) {
                // Karsilama ve Hizli Arama Onerileri
                const D2D1_RECT_F iconBox = RcWH(midX - 36.0f * s, midY - 40.0f * s, 72.0f * s, 72.0f * s);
                r.FillRound(iconBox, 20.0f * s, theme::Panel, 1.0f);
                DrawIcon(r, Icon::Search, iconBox, 36.0f * s, theme::Accent);

                r.Text(i18n::Literal(L"Tüm Kaynaklarda Manga Arama (AMR Motoru)"),
                       Rc(midX - 300 * s, midY + 44 * s, midX + 300 * s, midY + 70 * s),
                       theme::Text, 18.0f * s, Renderer::Align::Center, Renderer::Weight::Semibold);

                r.Text(i18n::Literal(L"Etkin manga ve webtoon kaynaklarında arama yapın.\nSonuçlar isme göre gruplanır, en alakalı olanlar en üstte gösterilir."),
                       Rc(midX - 320 * s, midY + 74 * s, midX + 320 * s, midY + 120 * s),
                       theme::TextMuted, 13.0f * s, Renderer::Align::Center, Renderer::Weight::Normal, 1.0f, true);

                // Populer arama cipleri
                const std::vector<std::wstring> suggestions = {
                    L"Solo Leveling", L"One Piece", L"Berserk", L"Chainsaw Man", L"Jujutsu Kaisen", L"Naruto"
                };
                float chipX = midX - 280.0f * s;
                float chipY = midY + 130.0f * s;
                for (size_t si = 0; si < suggestions.size(); ++si) {
                    const auto& sug = suggestions[si];
                    const float sugW = r.MeasureText(sug, 11.5f * s) + 20.0f * s;
                    const D2D1_RECT_F sugRc = RcWH(chipX, chipY, sugW, 28.0f * s);
                    if (w::TextButton(r, ui, Id("lib.gsug", (uint32_t)si), sugRc, sug, false)) {
                        session->input.SetText(sug);
                        LaunchGlobalMangaSearch(sug, host);return;
                    }
                    chipX += sugW + 8.0f * s;
                }
            } else {
                r.Text(L"\"" + session->query + i18n::Literal(L"\" için hiçbir kaynakta manga bulunamadı."),
                       Rc(midX - 250 * s, midY, midX + 250 * s, midY + 40 * s),
                       theme::TextMuted, 14.0f * s, Renderer::Align::Center);
            }
        }
        return;
    }

    // Sonuclar Listesi (AMR Stili Gruplanmis Kartlar)
    const float gap = 8.0f * s;
    m_pitch = 82.0f * s;
    m_listH = RcH(contentList);

    std::vector<GroupedSearchItem> resultsCopy;
    {
        std::lock_guard<std::mutex> lock(session->mutex);
        resultsCopy = session->results;
    }

    // Wrap every source badge instead of hiding the remaining sources behind +N.
    std::vector<float> offsets{0.0f};
    for (const auto& result : resultsCopy) {
        const float available = std::max(100.0f * s, RcW(contentList) - 90.0f * s);
        float used = 66.0f * s;
        int lines = 1;
        for (const auto& source : result.mirrors) {
            const float width = std::min(available, r.MeasureText(L"✓ " + FromUtf8(source.mirror), 11.0f * s, Renderer::Weight::Semibold) + 20.0f * s);
            if (used + width > available) { ++lines; used = 0; }
            used += width + 6.0f * s;
        }
        offsets.push_back(offsets.back() + std::max(74.0f * s, (48.0f + lines * 28.0f) * s) + gap);
    }
    const float contentH = std::max(0.0f, offsets.back() - gap);
    const float scroll = ClampScroll(RcH(contentList), contentH);

    r.PushClip(contentList);
    const size_t first = (size_t)std::max<ptrdiff_t>(0, std::upper_bound(offsets.begin(), offsets.end(), scroll) - offsets.begin() - 1);
    for (size_t i = first; i < resultsCopy.size(); ++i) {
        const float top = contentList.top + offsets[i] - scroll;
        if (top >= contentList.bottom) break;

        auto& item = resultsCopy[i];
        if (item.mirrors.empty()) continue;
        const float rowH = offsets[i + 1] - offsets[i] - gap;
        const D2D1_RECT_F rowRc = Rc(contentList.left, top, contentList.right, top + rowH);
        const uint32_t rowId = Id("lib.gsresult", (uint32_t)i);

        r.FillRound(rowRc, 12.0f * s, theme::Panel, 1.0f);
        if (ui.Hot(rowId)) r.FillRound(rowRc, 12.0f * s, theme::White, 0.04f);

        // Sol ikon kutusu
        const D2D1_RECT_F iconBox = RcWH(rowRc.left + 12 * s, rowRc.top + 12 * s, 50 * s, 50 * s);
        r.FillRound(iconBox, 10.0f * s, theme::Elevated, 1.0f);
        r.Text(item.initial, iconBox, theme::Accent, 22.0f * s, Renderer::Align::Center, Renderer::Weight::Bold);

        // Sagdaki Butonlar
        float btnRight = rowRc.right - 14.0f * s;
        const float btnH = 32.0f * s;
        const float by = rowRc.top + 14.0f * s;

        // Secili ayna bilgisi
        size_t selIdx = std::min(item.selectedMirrorIdx, item.mirrors.size() - 1);
        const auto& activeMirror = item.mirrors[selIdx];

        // 1. Oku Butonu (⚡ Oku)
        const float rdW = 86.0f * s;
        btnRight -= rdW;
        const D2D1_RECT_F rdRc = RcWH(btnRight, by, rdW, btnH);
        if (w::TextButton(r, ui, Id("lib.gs.read", (uint32_t)i), rdRc, i18n::Literal(L"⚡ Oku"), true)) {
            host.OpenOnlineManga(activeMirror.mirror, activeMirror.url, item.titleW, "__FIRST__");
        }
        btnRight -= 8.0f * s;

        // 2. Kutuphaneye Ekle Butonu (+ Kütüphane / ✓ Kütüphanede)
        bool alreadyInLib = lib.FindSource(activeMirror.mirror, activeMirror.url) != nullptr;

        const float addW = alreadyInLib ? 115.0f * s : 110.0f * s;
        btnRight -= addW;
        const D2D1_RECT_F addRc = RcWH(btnRight, by, addW, btnH);
        if (alreadyInLib) {
            r.FillRound(addRc, 8.0f * s, theme::Elevated, 1.0f);
            r.Text(i18n::Literal(L"✓ Kütüphanede"), addRc, theme::TextMuted, 12.0f * s, Renderer::Align::Center);
        } else {
            if (w::TextButton(r, ui, Id("lib.gs.add", (uint32_t)i), addRc, i18n::Literal(L"+ Kütüphane"), false)) {
                host.AddSeriesToLibrary(activeMirror.mirror, activeMirror.url, item.titleW);

                host.Invalidate();
            }
        }
        btnRight -= 12.0f * s;

        // Baslik ve Alaka Rozeti
        const float tx = iconBox.right + 14.0f * s;
        const float titleW = r.MeasureText(item.titleW, 15.0f * s, Renderer::Weight::Bold);
        const float titleRight = std::min(tx + titleW, btnRight - 100.0f * s);
        r.Text(item.titleW, Rc(tx, rowRc.top + 10 * s, titleRight, rowRc.top + 32 * s),
               theme::Text, 15.0f * s, Renderer::Align::Left, Renderer::Weight::Bold);

        // Alaka Derecesi Rozeti (En ustte en yakin olanlar)
        float badgeX = titleRight + 10.0f * s;
        if (item.relevanceScore >= 9000) {
            const float bw = 100.0f * s;
            const D2D1_RECT_F bRc = RcWH(badgeX, rowRc.top + 11 * s, bw, 20 * s);
            r.FillRound(bRc, 10.0f * s, 0x2ECC71, 0.22f);
            r.StrokeRound(bRc, 10.0f * s, 0x2ECC71, 1.0f * s, 0.50f);
            r.Text(i18n::Literal(L"🎯 Tam Eşleşme"), bRc, 0x2ECC71, 10.5f * s, Renderer::Align::Center, Renderer::Weight::Semibold);
        } else if (item.relevanceScore >= 5000) {
            const float bw = 95.0f * s;
            const D2D1_RECT_F bRc = RcWH(badgeX, rowRc.top + 11 * s, bw, 20 * s);
            r.FillRound(bRc, 10.0f * s, theme::Accent, 0.20f);
            r.StrokeRound(bRc, 10.0f * s, theme::Accent, 1.0f * s, 0.45f);
            r.Text(i18n::Literal(L"⭐ Yüksek Alaka"), bRc, theme::Accent, 10.5f * s, Renderer::Align::Center, Renderer::Weight::Semibold);
        }

        // Library membership and the active reading source are separate states.
        float chipX = tx + 66.0f * s;
        float chipY = rowRc.top + 38.0f * s;
        const float chipH = 22.0f * s;
        const float chipRight = rowRc.right - 14.0f * s;
        r.Text(i18n::Literal(L"Kaynaklar:"), RcWH(tx, chipY + 2.0f * s, 65.0f * s, chipH), theme::TextMuted, 11.5f * s);
        for (size_t mi = 0; mi < item.mirrors.size(); ++mi) {
            const auto& source = item.mirrors[mi];
            bool inLibrary = lib.FindSource(source.mirror, source.url) != nullptr;
            std::wstring label = (inLibrary ? L"✓ " : L"+ ") + FromUtf8(source.mirror);
            const float chipW = std::min(chipRight - tx, r.MeasureText(L"✓ " + FromUtf8(source.mirror), 11.0f * s, Renderer::Weight::Semibold) + 20.0f * s);
            if (chipX + chipW > chipRight) { chipX = tx; chipY += 28.0f * s; }
            const auto chipRc = RcWH(chipX, chipY, chipW, chipH);
            const uint32_t chipId = Id("lib.gs.chip", Id((item.title + "|" + source.mirror + "|" + source.url).c_str()));
            if (ui.Button(chipId, chipRc)) {
                {
                    std::lock_guard<std::mutex> lock(session->mutex);
                    for (auto& current : session->results) if (current.title == item.title) {
                        for (size_t ci = 0; ci < current.mirrors.size(); ++ci)
                            if (current.mirrors[ci].mirror == source.mirror && current.mirrors[ci].url == source.url) current.selectedMirrorIdx = ci;
                        break;
                    }
                }
                if (!inLibrary) host.AddSeriesToLibrary(source.mirror, source.url, item.titleW);
                inLibrary = lib.FindSource(source.mirror, source.url) != nullptr;
                label = (inLibrary ? L"✓ " : L"+ ") + FromUtf8(source.mirror);
                host.Invalidate();
            }
            r.FillRound(chipRc, 11.0f * s, inLibrary ? theme::Active : theme::Panel, inLibrary ? 0.40f : 1.0f);
            r.StrokeRound(chipRc, 11.0f * s, inLibrary ? theme::ActiveEdge : theme::Border, inLibrary ? 1.5f * s : 1.0f * s, inLibrary ? 0.9f : 0.35f);
            r.Text(label, chipRc, inLibrary ? theme::White : theme::TextMuted, 11.0f * s, Renderer::Align::Center, Renderer::Weight::Semibold);
            if (mi == selIdx) r.Fill(Rc(chipRc.left + 8*s, chipRc.bottom - 2*s, chipRc.right - 8*s, chipRc.bottom), theme::White, 0.75f);
            ui.Tip(chipId, inLibrary ? i18n::Literal(L"Kütüphanede · Bu kaynağı okumak için seç") : i18n::Literal(L"Bu manga kaynağını okuma listesine ekle"), chipRc);
            chipX += chipW + 6.0f * s;
        }

    }
    r.PopClip();

    DrawScrollbar(r, ui, contentList, contentH, host);
    session->scroll=m_scroll[kSearch];session->maxScroll=m_maxScroll[kSearch];
}

void LibraryView::StopWorkers() {
    m_stopping = true;
    m_siteCancel=true;
    for (const auto& session : m_searchTabs) CancelSearch(session);
    for (auto& worker : m_workers) if (worker.joinable()) worker.join();
    m_workers.clear();
}

void LibraryView::DrawSourceHealth(Renderer& r, Ui& ui, const D2D1_RECT_F& list, ShellHost& host) {
    const float s = r.Scale();
    auto statuses = engine::MirrorEngine::Instance().GetSourceStatuses();
    const size_t failures = std::count_if(statuses.begin(), statuses.end(), [](const auto& st) { return !st.success; });
    const float topH = 38 * s;
    const auto toggleRc = RcWH(list.right - 340*s, list.top, 155*s, topH);
    if (w::TextButton(r, ui, Id("health.filter"), toggleRc, m_onlyFailedSources ? i18n::Literal(L"Tüm Denetimler") : i18n::Literal(L"Yalnız Hatalar"), false)) {
        m_onlyFailedSources = !m_onlyFailedSources;
        m_scroll[kHealth] = 0;
        host.Invalidate();
    }
    const auto logRc = RcWH(list.right - 175*s, list.top, 175*s, topH);
    if (w::TextButton(r, ui, Id("health.logs"), logRc, i18n::Literal(L"Log Klasörünü Aç"), false)) host.OpenDataFolder();
    r.Text(std::to_wstring(failures) + i18n::Literal(L" hatalı işlem / ") + std::to_wstring(statuses.size()) + i18n::Literal(L" kayıtlı denetim"), Rc(list.left, list.top, toggleRc.left - 8*s, list.top + topH), theme::Text, 14*s);
    const auto body = Rc(list.left, list.top + 50*s, list.right, list.bottom);
    std::erase_if(statuses, [&](const auto& st) { return m_onlyFailedSources && st.success; });
    std::sort(statuses.begin(), statuses.end(), [](const auto& a, const auto& b) { return a.checkedAt > b.checkedAt; });
    if (statuses.empty()) {
        r.Text(i18n::Literal(L"Henüz kayıtlı hata yok. Manga araması veya Mangaları Denetle işlemiyle kaynaklar kontrol edilir.\nKontrol edilmemiş siteler çalışıyor kabul edilmez."), body, theme::TextMuted, 14*s, Renderer::Align::Center, Renderer::Weight::Normal, 1.0f, true);
        return;
    }
    const float pitch = 96*s;
    const float height = pitch * (float)statuses.size();
    ClampScroll(RcH(body), height);
    r.PushClip(body);
    for (size_t i = (size_t)std::max(0.0f, std::floor(m_scroll[kHealth]/pitch)); i < statuses.size(); ++i) {
        const float y = body.top + (float)i*pitch - m_scroll[kHealth];
        if (y >= body.bottom) break;
        const auto& st = statuses[i];
        const auto card = Rc(body.left, y, body.right - 12*s, y + pitch - 8*s);
        r.FillRound(card, 8*s, theme::Panel, 1);
        r.Text(FromUtf8(st.mirror) + L" · " + FromUtf8(st.operation), Rc(card.left + 14*s, y+10*s, card.right-14*s, y+34*s), theme::Text, 14*s, Renderer::Align::Left, Renderer::Weight::Semibold);
        const auto detail = st.success ? (st.resultCount == 0 ? i18n::Literal(L"İstek başarılı; eşleşen manga yok.") : i18n::Literal(L"Başarılı · ") + std::to_wstring(st.resultCount) + i18n::Literal(L" sonuç")) : FromUtf8(st.error);
        r.Text(detail, Rc(card.left+14*s, y+36*s, card.right-14*s, y+61*s), st.success ? theme::Accent : 0xF39C12, 12*s);
        r.Text(i18n::Literal(L"Son kontrol: ") + FormatRelativeTime(st.checkedAt), Rc(card.left+14*s, y+63*s, card.right-14*s, y+81*s), theme::TextMuted, 11*s);
        if (ui.Button(Id("health.row", (uint32_t)i), card)) {
            for (const auto& mirror : engine::MirrorEngine::Instance().GetMirrors())
                if (mirror.name == st.mirror) { host.OpenEmbeddedBrowser(mirror.home, mirror.name); break; }
        }
        ui.Tip(Id("health.row", (uint32_t)i), detail, card);
    }
    r.PopClip();
    DrawScrollbar(r, ui, body, height, host);
}

} // namespace gm
