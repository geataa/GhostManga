#pragma once

#include "HttpClient.h"
#include "CookieJar.h"
#include <memory>
#include <unordered_map>
#include <chrono>

namespace gm::net {

class RateLimiter {
public:
    RateLimiter() = default;

    // Wait if needed to obey politeness delay (e.g. minDelayMs per host)
    void Acquire(const std::string& host, int minDelayMs = 250);

private:
    std::mutex m_mutex;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_lastRequest;
};

// Single gateway for all network requests in GhostManga
class Fetcher {
public:
    static Fetcher& Instance();

    CookieJar& Jar() { return m_jar; }
    HttpClient& Client() { return m_client; }

    HttpResponse Fetch(HttpRequest req, int minDelayMs = 250);

private:
    Fetcher();
    ~Fetcher() = default;

    CookieJar   m_jar;
    HttpClient  m_client;
    RateLimiter m_limiter;
};

// Convenience top-level function
inline HttpResponse Fetch(const HttpRequest& req, int minDelayMs = 250) {
    return Fetcher::Instance().Fetch(req, minDelayMs);
}

// Redirect dead/expired domains to working mirror domains
inline std::string NormalizeMirrorUrl(const std::string& url) {
    if (url.empty()) return url;
    // Match complete authorities, never a domain mentioned inside a query.
    for(const auto* scheme:{"https://","http://"}) {
        for(const auto* host:{"www.mangakawaii.io","mangakawaii.io","www.mangakawaii.fr","mangakawaii.fr"}) {
            const auto prefix=std::string(scheme)+host+"/";
            if(url.rfind(prefix,0)!=0) continue;
            auto path=url.substr(prefix.size());
            const auto suffixAt=path.find_first_of("?#");
            const auto suffix=suffixAt==std::string::npos?"":path.substr(suffixAt);
            path=path.substr(0,suffixAt);
            if(path.rfind("manga/",0)==0) {
                const auto locale=path.find('/',6);
                if(locale!=std::string::npos && path.size()>locale+3 && path[locale+3]=='/' &&
                   path[locale+1]>='a' && path[locale+1]<='z' && path[locale+2]>='a' && path[locale+2]<='z') {
                    path.erase(locale+1,3);
                    const auto page=path.find('/',locale+1);
                    if(page!=std::string::npos) path.resize(page);
                }
            }
            return "https://www.mangakawaii.fr/"+path+suffix;
        }
        for(const auto* host:{"sadscans.com","www.sadscans.com"}) {
            const auto prefix=std::string(scheme)+host+"/";
            if(url.rfind(prefix,0)!=0) continue;
            auto path=url.substr(prefix.size());
            if(path.rfind("series/",0)==0)path.replace(0,7,"seriler/");
            return "https://sadscans.net/"+path;
        }
    }
    std::string s = url;

    // Manga Clash: toonclash.com -> mangaclash.com
    size_t p = s.find("toonclash.com");
    if (p != std::string::npos) {
        s.replace(p, 13, "mangaclash.com");
    }

    // Manga4Life: manga4life.com -> mangasee123.com
    p = s.find("manga4life.com");
    if (p != std::string::npos) {
        s.replace(p, 14, "mangasee123.com");
    }

    // Batoto: dead / seized domains -> batocomic.com
    const char* const batoDomains[] = { "bato.to", "battwo.com", "xbato.com", "dto.to", "mangatoto.com", "comiko.net" };
    for (const char* bd : batoDomains) {
        p = s.find(bd);
        if (p != std::string::npos) {
            s.replace(p, strlen(bd), "batocomic.com");
            break;
        }
    }

    // Manga Hasu: mangahasu.se -> mangahasu.me
    p = s.find("mangahasu.se");
    if (p != std::string::npos) {
        s.replace(p, 12, "mangahasu.me");
    }

    return s;
}

} // namespace gm::net
