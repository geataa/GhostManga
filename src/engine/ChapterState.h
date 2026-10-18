#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>

namespace gm {

struct Chapter {
    std::string title;
    std::string url;
    bool operator==(const Chapter&) const = default;
};

inline std::string ChapterUrlIdentity(std::string url) {
    if (url.rfind("http://", 0) == 0) url.replace(0, 7, "https://");
    if (url.rfind("//", 0) == 0) url = "https:" + url;
    const auto hash = url.find('#');
    if (hash != std::string::npos) url.resize(hash);
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url;
}

inline bool IsMangaDexSource(const std::string& value) {
    static const std::regex source(R"(^(?:https?://(?:www\.)?mangadex\.org(?:/|$)|mangadex(?:v5)?/))",std::regex::icase);
    return std::regex_search(value,source);
}

inline bool IsChapterCandidateUrl(const std::string& url) {
    if(url.empty()) return false;
    if(IsMangaDexSource(url)) {
        static const std::regex chapter(R"(^https?://(?:www\.)?mangadex\.org/chapter/[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}(?:[/?#]|$))",std::regex::icase);
        return std::regex_search(url,chapter);
    }
    // Broad DOM containers also contain group/uploader/navigation links.
    static const std::regex navigation(R"(/(?:users?|groups?|profiles?|authors?|artists?|tags?|genres?|categor(?:y|ies)|login|register|search)(?:[/?#]|$))",std::regex::icase);
    return !std::regex_search(url,navigation);
}

inline double ChapterVolume(const std::string& title) {
    static const std::regex volume(R"((?:^|\s)(?:volume|vol)[ .]*([0-9]+(?:\.[0-9]+)?))",std::regex::icase);
    std::smatch match;
    return std::regex_search(title,match,volume)?std::strtod(match[1].str().c_str(),nullptr):-1;
}

// A volume, series ID or UUID is not a chapter number. Prefer explicit
// chapter markers; only accept a leading number for unlabelled titles.
inline double ChapterNumber(const std::string& title, const std::string& url = "") {
    static const std::regex labelled(R"((?:^|[^a-zA-Z0-9])(?:chapter|chap|ch|episode|ep|bölüm|bolum|c)[ ._:/-]*([0-9]+(?:[.,][0-9]+)?))", std::regex::icase);
    static const std::regex leading(R"(^\s*([0-9]+(?:[.,][0-9]+)?)(?:\s|$|[-:]))");
    std::smatch match;
    std::string number;
    if (std::regex_search(title, match, labelled) || std::regex_search(title, match, leading)) number = match[1];
    else if (std::regex_search(url, match, labelled)) number = match[1];
    if (number.empty()) return -1.0;
    std::replace(number.begin(), number.end(), ',', '.');
    return std::strtod(number.c_str(), nullptr);
}

// AMR's contract is newest first. Some mirrors return oldest first; reverse
// the whole sequence to keep specials in their source positions.
inline void NormalizeChapters(std::vector<Chapter>& chapters) {
    std::unordered_set<std::string> seen;
    std::erase_if(chapters, [&](const Chapter& ch) {
        return !IsChapterCandidateUrl(ch.url) || !seen.insert(ChapterUrlIdentity(ch.url)).second;
    });
    double first = -1, last = -1, firstVolume = -1, lastVolume = -1;
    for (const auto& ch : chapters) {
        const double n = ChapterNumber(ch.title, ch.url);
        if (n >= 0) { if (first < 0) { first = n; firstVolume=ChapterVolume(ch.title); } last = n; lastVolume=ChapterVolume(ch.title); }
    }
    const bool ascending=firstVolume>=0 && lastVolume>=0 && firstVolume!=lastVolume ? lastVolume>firstVolume : last>first;
    if (first >= 0 && ascending) std::reverse(chapters.begin(), chapters.end());
}

inline int ChapterIndex(const std::vector<Chapter>& chapters, const std::string& url) {
    if (url.empty()) return -1;
    const auto identity = ChapterUrlIdentity(url);
    for (size_t i = 0; i < chapters.size(); ++i)
        if (ChapterUrlIdentity(chapters[i].url) == identity) return (int)i;
    return -1;
}

} // namespace gm
