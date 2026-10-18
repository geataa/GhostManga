#include "Fetch.h"
#include "ChallengeDetector.h"
#include "TransportRouter.h"
#include "browser/WebViewHost.h"
#include "core/Log.h"
#include "core/Utf8.h"
#include <thread>

namespace gm::net {

static void ExtractHostAndPath(const std::string& url, std::string& host, std::string& path, bool& isSecure) {
    host.clear();
    path = "/";
    isSecure = false;

    if (url.rfind("//", 0) == 0) {
        ExtractHostAndPath("https:" + url, host, path, isSecure);
        return;
    }

    size_t schemePos = url.find("://");
    if (schemePos == std::string::npos) return;

    std::string scheme = url.substr(0, schemePos);
    for (char& c : scheme) c = (char)tolower((unsigned char)c);
    isSecure = (scheme == "https");

    size_t hostStart = schemePos + 3;
    size_t pathStart = url.find('/', hostStart);

    if (pathStart == std::string::npos) {
        host = url.substr(hostStart);
    } else {
        host = url.substr(hostStart, pathStart - hostStart);
        path = url.substr(pathStart);
    }

    size_t colon = host.find(':');
    if (colon != std::string::npos) {
        host = host.substr(0, colon);
    }
}

void RateLimiter::Acquire(const std::string& host, int minDelayMs) {
    if (minDelayMs <= 0 || host.empty()) return;

    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    std::chrono::milliseconds sleepDuration(0);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_lastRequest.find(host);
        if (it != m_lastRequest.end()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second);
            if (elapsed.count() < minDelayMs) {
                sleepDuration = std::chrono::milliseconds(minDelayMs) - elapsed;
            }
        }
        m_lastRequest[host] = now + sleepDuration;
    }

    if (sleepDuration.count() > 0) {
        std::this_thread::sleep_for(sleepDuration);
    }
}

Fetcher::Fetcher() {
    m_client.Init();
}

Fetcher& Fetcher::Instance() {
    static Fetcher s_instance;
    return s_instance;
}

HttpResponse Fetcher::Fetch(HttpRequest req, int minDelayMs) {
    req.url = NormalizeMirrorUrl(req.url);
    if (req.url.rfind("//", 0) == 0) {
        req.url = "https:" + req.url;
    }

    std::string host, path;
    bool isSecure = false;
    ExtractHostAndPath(req.url, host, path, isSecure);

    // Rate limiting per host
    if (!host.empty()) {
        m_limiter.Acquire(host, minDelayMs);
    }

    // 1. Check Transport Lane for this host
    TransportLane lane = TransportRouter::Instance().GetLane(host);
    if (req.allowBrowserFallback && lane == TransportLane::LaneC && browser::WebViewHost::Instance().IsAvailable()) {
        HttpResponse bRes = browser::WebViewHost::Instance().Execute(req, false);
        const auto browserChallenge = ChallengeDetector::Detect(bRes);
        if (bRes.success && !browserChallenge.NeedsBrowser() && !browserChallenge.isBlocked) {
            TransportRouter::Instance().RecordSuccess(host, TransportLane::LaneC);
            return bRes;
        }
        bRes.success = false;
        if (bRes.error.empty()) bRes.error = browserChallenge.reason.empty() ? "Tarayici kaynaga ulasamadi" : browserChallenge.reason;
        return bRes;
    }

    // Attach cookies from Jar if not already explicitly provided in headers
    bool hasCookieHeader = false;
    for (const auto& [k, v] : req.headers) {
        if (_stricmp(k.c_str(), "cookie") == 0) {
            hasCookieHeader = true;
            break;
        }
    }

    if (!hasCookieHeader && !host.empty()) {
        std::string cookieVal = m_jar.GetCookieHeader(host, path, isSecure);
        if (!cookieVal.empty()) {
            req.headers.push_back({ "Cookie", cookieVal });
        }
    }

    // Perform HTTP request via Lane A/B (WinHTTP)
    HttpResponse res = m_client.Send(req);

    // 2. Bot protection check on the response
    ChallengeInfo cInfo = ChallengeDetector::Detect(res);
    const bool nativeTransportFailure = !res.success &&
        (res.error.find("12175") != std::string::npos || res.error.find("12030") != std::string::npos);
    // These requested sources work in a normal browser session while rejecting
    // WinHTTP. Let the existing app browser profile try once; a 403 remains a
    // failure if Chromium is also denied. No cookie export or TLS exceptions.
    const bool sourceBrowserSession = cInfo.isBlocked &&
        (host=="sadscans.net" || host=="www.sadscans.net" || host=="www.mangakawaii.fr" ||
         host=="mangakawaii.fr" || host=="dragontea.ink" || host=="toonily.com");
    if (req.allowBrowserFallback && (cInfo.NeedsBrowser() || nativeTransportFailure || sourceBrowserSession) && browser::WebViewHost::Instance().IsAvailable()) {
        log::Write(L"[Fetch] Challenge detected on '%s' (%s). Escalating to Lane C (WebView2)...",
                   FromUtf8(host).c_str(), FromUtf8(cInfo.reason).c_str());
        if (cInfo.NeedsBrowser()) TransportRouter::Instance().RecordChallenge(host, cInfo.reason);
        HttpResponse bRes = browser::WebViewHost::Instance().Execute(req, false);
        const auto browserChallenge = ChallengeDetector::Detect(bRes);
        if (bRes.success && !browserChallenge.NeedsBrowser() && !browserChallenge.isBlocked) {
            if (nativeTransportFailure) TransportRouter::Instance().RecordChallenge(host, "WinHTTP tasiyici uyumsuzlugu");
            if (sourceBrowserSession) TransportRouter::Instance().RecordChallenge(host, "Kaynak tarayici oturumu gerektiriyor");
            TransportRouter::Instance().RecordSuccess(host, TransportLane::LaneC);
            return bRes;
        }
        bRes.success = false;
        if (bRes.error.empty()) bRes.error = browserChallenge.reason.empty() ? "Tarayici kaynaga ulasamadi" : browserChallenge.reason;
        return bRes;
    } else if (res.success && !cInfo.isBlocked) {
        TransportRouter::Instance().RecordSuccess(host, TransportLane::LaneA);
    }

    // Save received cookies into Jar (blacklist filtered)
    std::string cookieHost = host;
    std::string cookiePath = path;
    if (!res.finalUrl.empty() && res.finalUrl != req.url) {
        std::string fHost, fPath;
        bool fSec = false;
        ExtractHostAndPath(res.finalUrl, fHost, fPath, fSec);
        if (!fHost.empty()) {
            cookieHost = fHost;
            cookiePath = fPath;
        }
    }
    if (!cookieHost.empty()) {
        for (const auto& sc : res.setCookies) {
            m_jar.SetCookie(cookieHost, cookiePath, sc);
        }
    }

    return res;
}

} // namespace gm::net
