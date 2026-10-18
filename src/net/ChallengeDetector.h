#pragma once

#include "HttpClient.h"
#include <string>

namespace gm::net {

enum class ChallengeType {
    None,
    CloudflareTurnstile,
    CloudflareJsChallenge,
    VercelWaf,
    AwsWaf,
    SucuriDdg,
    BlockedIp
};

struct ChallengeInfo {
    ChallengeType type = ChallengeType::None;
    std::string   reason;
    bool          isBlocked = false;

    bool NeedsBrowser() const {
        return type != ChallengeType::None && !isBlocked;
    }
};

class ChallengeDetector {
public:
    static ChallengeInfo Detect(const HttpResponse& resp);
};

} // namespace gm::net
