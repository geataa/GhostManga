#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>

namespace gm::net {

struct Cookie {
    std::string name;
    std::string value;
    std::string domain;
    std::string path;
    int64_t     expires = 0; // epoch seconds, 0 = session
    bool        secure = false;
    bool        httpOnly = false;
};

class CookieJar {
public:
    CookieJar() = default;
    ~CookieJar() = default;

    // Parses a single "Set-Cookie: ..." line from an HTTP response for the given request URL/host.
    void SetCookie(const std::string& requestHost, const std::string& requestPath,
                   const std::string& setCookieLine);

    // Explicitly set or update a cookie
    void Set(const Cookie& cookie);

    // Get cookie value for a specific domain and name
    std::string Get(const std::string& domain, const std::string& name);

    // Generates the "Cookie: name1=val1; name2=val2" header content for a request.
    std::string GetCookieHeader(const std::string& host, const std::string& path, bool isSecure);

    // Clear all cookies, or cookies for a specific domain
    void Clear();
    void ClearDomain(const std::string& domain);

    // Serialization for persistence
    std::string ToJson() const;
    void LoadJson(const std::string& jsonStr);

private:
    static bool DomainMatches(const std::string& host, const std::string& cookieDomain);
    static bool PathMatches(const std::string& requestPath, const std::string& cookiePath);

    mutable std::mutex  m_mutex;
    std::vector<Cookie> m_cookies;
};

} // namespace gm::net
