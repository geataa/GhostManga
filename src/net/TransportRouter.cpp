#include "TransportRouter.h"
#include "core/Log.h"
#include "core/Utf8.h"

namespace gm::net {

TransportRouter& TransportRouter::Instance() {
    static TransportRouter s_instance;
    return s_instance;
}

TransportLane TransportRouter::GetLane(const std::string& host) {
    if (host.empty()) return TransportLane::LaneA;

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_states.find(host);
    if (it == m_states.end()) {
        return TransportLane::LaneA;
    }

    // Check if host is Lane C but due for a probe back to Lane A
    if (it->second.lane == TransportLane::LaneC) {
        auto now = std::chrono::steady_clock::now();
        if (now >= it->second.probeAfter) {
            // Probe Lane A
            return TransportLane::LaneA;
        }
    }

    return it->second.lane;
}

void TransportRouter::RecordSuccess(const std::string& host, TransportLane lane) {
    if (host.empty()) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    HostState& s = m_states[host];
    s.host = host;
    s.okCount++;
    s.failCount = 0;

    // If we succeeded on Lane A while in Lane C probe mode:
    if (s.lane == TransportLane::LaneC && lane == TransportLane::LaneA) {
        if (s.okCount >= 2) {
            log::Write(L"[TransportRouter] Host '%s' demoted back to Lane A (clean probe)",
                       FromUtf8(host).c_str());
            s.lane = TransportLane::LaneA;
            s.laneReason.clear();
        }
    }
}

void TransportRouter::RecordChallenge(const std::string& host, const std::string& reason) {
    if (host.empty()) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    HostState& s = m_states[host];
    s.host = host;
    s.failCount++;
    s.okCount = 0;

    auto now = std::chrono::steady_clock::now();
    if (s.lane != TransportLane::LaneC) {
        log::Write(L"[TransportRouter] Host '%s' promoted to Lane C (reason: %s)",
                   FromUtf8(host).c_str(), FromUtf8(reason).c_str());
        s.lane = TransportLane::LaneC;
        s.laneReason = reason;
        s.laneSince = now;
    }
    // Schedule probe after 6 hours
    s.probeAfter = now + std::chrono::hours(6);
}

bool TransportRouter::ShouldProbeLaneA(const std::string& host) {
    if (host.empty()) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_states.find(host);
    if (it == m_states.end() || it->second.lane != TransportLane::LaneC) return false;
    return std::chrono::steady_clock::now() >= it->second.probeAfter;
}

} // namespace gm::net
