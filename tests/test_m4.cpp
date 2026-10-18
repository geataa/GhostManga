#include <iostream>
#include <cassert>
#include "../src/net/HttpClient.h"
#include "../src/net/CookieJar.h"
#include "../src/net/ChallengeDetector.h"
#include "../src/net/TransportRouter.h"
#include "../src/browser/WebViewHost.h"
#include "../src/core/Paths.h"
#include "../src/core/Log.h"

using namespace gm;

int main() {
    std::cout << "=============================================\n";
    std::cout << "  GhostManga M4 Automated Verification Tests \n";
    std::cout << "=============================================\n";

    std::wstring err;
    paths::Init(&err);
    log::Init();

    // Test 1: ChallengeDetector
    std::cout << "[TEST 1] Testing ChallengeDetector...\n";
    {
        // 1a. cf-mitigated header
        net::HttpResponse r1;
        r1.status = 200;
        r1.headers.push_back({ "cf-mitigated", "challenge" });
        auto info1 = net::ChallengeDetector::Detect(r1);
        assert(info1.type == net::ChallengeType::CloudflareTurnstile);
        assert(info1.NeedsBrowser() == true);

        // 1b. Cloudflare Turnstile in body
        net::HttpResponse r2;
        r2.status = 403;
        r2.headers.push_back({ "server", "cloudflare" });
        r2.body = "<html><body><form><input name=\"cf-turnstile-response\"/></form></body></html>";
        auto info2 = net::ChallengeDetector::Detect(r2);
        assert(info2.type == net::ChallengeType::CloudflareTurnstile);
        assert(info2.NeedsBrowser() == true);

        // 1c. Cloudflare JS Challenge
        net::HttpResponse r3;
        r3.status = 503;
        r3.headers.push_back({ "server", "cloudflare" });
        r3.body = "<html><script>window._cf_chl_opt={};</script></html>";
        auto info3 = net::ChallengeDetector::Detect(r3);
        assert(info3.type == net::ChallengeType::CloudflareJsChallenge);
        assert(info3.NeedsBrowser() == true);

        // 1d. Blocked IP
        net::HttpResponse r4;
        r4.status = 403;
        r4.headers.push_back({ "server", "cloudflare" });
        r4.body = "<html><h2 data-translate=\"blocked_why_headline\">Sorry, you have been blocked</h2></html>";
        auto info4 = net::ChallengeDetector::Detect(r4);
        assert(info4.type == net::ChallengeType::BlockedIp);
        assert(info4.isBlocked == true);
        assert(info4.NeedsBrowser() == false);

        std::cout << "  -> ChallengeDetector: PASSED\n";
    }

    // Test 2: TransportRouter
    std::cout << "[TEST 2] Testing TransportRouter state machine...\n";
    {
        auto& router = net::TransportRouter::Instance();
        std::string host = "asurascans.com";

        // Initial state should be Lane A
        assert(router.GetLane(host) == net::TransportLane::LaneA);

        // Record challenge -> promotes to Lane C
        router.RecordChallenge(host, "turnstile");
        assert(router.GetLane(host) == net::TransportLane::LaneC);

        // Probe successes demote back to Lane A
        router.RecordSuccess(host, net::TransportLane::LaneA);
        router.RecordSuccess(host, net::TransportLane::LaneA);
        assert(router.GetLane(host) == net::TransportLane::LaneA);

        std::cout << "  -> TransportRouter: PASSED\n";
    }

    // Test 3: CookieJar Cloudflare Blacklist
    std::cout << "[TEST 3] Testing CookieJar security blacklist...\n";
    {
        net::CookieJar jar;
        jar.SetCookie("asurascans.com", "/", "cf_clearance=secret123; Path=/; Secure; HttpOnly");
        jar.SetCookie("asurascans.com", "/", "__cf_bm=bm456; Path=/; Secure; HttpOnly");
        jar.SetCookie("asurascans.com", "/", "session_token=valid_user; Path=/; Secure; HttpOnly");

        std::string hdr = jar.GetCookieHeader("asurascans.com", "/", true);
        assert(hdr.find("session_token=valid_user") != std::string::npos);
        assert(hdr.find("cf_clearance") == std::string::npos); // strictly blacklisted
        assert(hdr.find("__cf_bm") == std::string::npos);      // strictly blacklisted

        std::cout << "  -> CookieJar Blacklist: PASSED\n";
    }

    // Test 4: WebViewHost Availability
    std::cout << "[TEST 4] Testing WebViewHost runtime availability...\n";
    {
        bool available = browser::WebViewHost::Instance().IsAvailable();
        std::cout << "  -> Edge WebView2 Evergreen Installed: " << (available ? "YES" : "NO") << "\n";
        assert(available);
        std::cout << "  -> WebViewHost: PASSED\n";
    }

    std::cout << "\nALL M4 VERIFICATION TESTS COMPLETED SUCCESSFULLY!\n";
    return 0;
}
