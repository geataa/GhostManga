#include "ChallengeDetector.h"
#include <algorithm>
#include <string_view>

namespace gm::net {

static bool ContainsCi(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return true;
    if (hay.size() < needle.size()) return false;
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
        [](char ch1, char ch2) {
            return tolower((unsigned char)ch1) == tolower((unsigned char)ch2);
        });
    return it != hay.end();
}

ChallengeInfo ChallengeDetector::Detect(const HttpResponse& resp) {
    ChallengeInfo info;
    if ((resp.status >= 300 && resp.status < 400) || resp.status == 404) {
        return info;
    }

    // 1. Official HTTP Headers
    std::string cfMitigated = resp.GetHeader("cf-mitigated");
    if (ContainsCi(cfMitigated, "challenge")) {
        info.type = ChallengeType::CloudflareTurnstile;
        info.reason = "header:cf-mitigated";
        return info;
    }

    std::string vercelMitigated = resp.GetHeader("x-vercel-mitigated");
    if (ContainsCi(vercelMitigated, "challenge") || (resp.status == 429 && !vercelMitigated.empty())) {
        info.type = ChallengeType::VercelWaf;
        info.reason = "header:x-vercel-mitigated";
        return info;
    }

    std::string amzWaf = resp.GetHeader("x-amzn-waf-action");
    if (ContainsCi(amzWaf, "challenge") || ContainsCi(amzWaf, "captcha")) {
        info.type = ChallengeType::AwsWaf;
        info.reason = "header:x-amzn-waf-action";
        return info;
    }

    // 2. Status code + Cloudflare Server/Ray headers
    std::string server = resp.GetHeader("server");
    std::string cfRay = resp.GetHeader("cf-ray");
    bool isCfServer = ContainsCi(server, "cloudflare") || !cfRay.empty();

    // 3. Body inspection (scan up to 64KB)
    std::string_view bodyView = resp.body;
    if (bodyView.size() > 65536) {
        bodyView = bodyView.substr(0, 65536);
    }

    // Check for permanent IP block (BLOCKED)
    if (ContainsCi(bodyView, "blocked_why_headline") || ContainsCi(bodyView, "access denied")) {
        if (isCfServer || resp.status == 403) {
            info.type = ChallengeType::BlockedIp;
            info.reason = "body:blocked_why_headline";
            info.isBlocked = true;
            return info;
        }
    }

    // Cloudflare markers
    if (ContainsCi(bodyView, "cf-turnstile-response") ||
        ContainsCi(bodyView, "/cdn-cgi/challenge-platform/")) {
        info.type = ChallengeType::CloudflareTurnstile;
        info.reason = "body:turnstile";
        return info;
    }

    if (ContainsCi(bodyView, "window._cf_chl_opt") ||
        ContainsCi(bodyView, "challenge-error-title") ||
        ContainsCi(bodyView, "challenge-error-text")) {
        info.type = ChallengeType::CloudflareJsChallenge;
        info.reason = "body:_cf_chl_opt";
        return info;
    }

    // Other bot protections
    if (ContainsCi(bodyView, "sucuri_cloudproxy_uuid_")) {
        info.type = ChallengeType::SucuriDdg;
        info.reason = "body:sucuri";
        return info;
    }

    if (ContainsCi(bodyView, "__ddg1_")) {
        info.type = ChallengeType::SucuriDdg;
        info.reason = "body:ddg";
        return info;
    }

    // Interstitial JS redirects (Manga Clash, etc.)
    if (resp.status == 200 && ContainsCi(bodyView, "<title>Redirecting...</title>")) {
        info.type = ChallengeType::CloudflareJsChallenge;
        info.reason = "body:redirecting_interstitial";
        return info;
    }

    // If 403 or 503 from Cloudflare without specific markers, still candidate
    if ((resp.status == 403 || resp.status == 503) && isCfServer) {
        info.type = ChallengeType::CloudflareJsChallenge;
        info.reason = "status_cf_server";
        return info;
    }

    return info;
}

} // namespace gm::net
