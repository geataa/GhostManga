#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>

namespace gm::net {

enum class TransportLane {
    LaneA = 0, // WinHTTP standard
    LaneB = 1, // WinHTTP with custom mirror headers
    LaneC = 2  // WebView2 Evergreen browser
};

struct HostState {
    std::string   host;
    TransportLane lane = TransportLane::LaneA;
    std::string   laneReason;
    int           failCount = 0;
    int           okCount = 0;
    std::chrono::steady_clock::time_point laneSince;
    std::chrono::steady_clock::time_point probeAfter;
};

class TransportRouter {
public:
    static TransportRouter& Instance();

    TransportLane GetLane(const std::string& host);
    void RecordSuccess(const std::string& host, TransportLane lane);
    void RecordChallenge(const std::string& host, const std::string& reason);
    bool ShouldProbeLaneA(const std::string& host);

private:
    TransportRouter() = default;
    ~TransportRouter() = default;

    std::mutex m_mutex;
    std::unordered_map<std::string, HostState> m_states;
};

} // namespace gm::net
