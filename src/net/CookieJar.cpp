#include "CookieJar.h"
#include "../core/Json.h"
#include <chrono>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace gm::net {

static std::string ToLower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static std::string Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static int64_t CurrentEpochSec() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool CookieJar::DomainMatches(const std::string& host, const std::string& cookieDomain) {
    std::string h = ToLower(host);
    std::string d = ToLower(cookieDomain);
    while (!d.empty() && d.front() == '.') d.erase(0, 1);
    while (!h.empty() && h.front() == '.') h.erase(0, 1);

    if (h == d) return true;
    if (h.length() > d.length()) {
        size_t offset = h.length() - d.length();
        if (h.compare(offset, d.length(), d) == 0 && h[offset - 1] == '.') {
            return true;
        }
    }
    return false;
}

bool CookieJar::PathMatches(const std::string& requestPath, const std::string& cookiePath) {
    if (cookiePath.empty() || cookiePath == "/") return true;
    std::string rp = requestPath.empty() ? "/" : requestPath;
    if (rp == cookiePath) return true;
    if (rp.rfind(cookiePath, 0) == 0) {
        if (cookiePath.back() == '/' || rp[cookiePath.length()] == '/') {
            return true;
        }
    }
    return false;
}

static bool IsBlacklistedCookie(const std::string& name) {
    std::string n = ToLower(name);
    if (n.rfind("cf_", 0) == 0 || n.rfind("_cf", 0) == 0 || n.rfind("__cf", 0) == 0) return true;
    if (n.rfind("__ddg", 0) == 0 || n.rfind("sucuri_cloudproxy", 0) == 0) return true;
    if (n == "aws-waf-token" || n == "_vcrcs") return true;
    return false;
}

void CookieJar::Set(const Cookie& cookie) {
    if (cookie.name.empty() || IsBlacklistedCookie(cookie.name)) return;
    std::lock_guard<std::mutex> lock(m_mutex);

    // Remove existing cookie with same name, domain, path
    for (auto it = m_cookies.begin(); it != m_cookies.end(); ++it) {
        if (it->name == cookie.name && it->domain == cookie.domain && it->path == cookie.path) {
            *it = cookie;
            return;
        }
    }
    m_cookies.push_back(cookie);
}

std::string CookieJar::Get(const std::string& domain, const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const int64_t now = CurrentEpochSec();

    for (const auto& c : m_cookies) {
        if (c.name == name && DomainMatches(domain, c.domain)) {
            if (c.expires == 0 || c.expires > now) {
                return c.value;
            }
        }
    }
    return "";
}

void CookieJar::SetCookie(const std::string& requestHost, const std::string& requestPath,
                         const std::string& setCookieLine) {
    if (setCookieLine.empty()) return;

    Cookie c;
    c.domain = ToLower(requestHost);
    c.path = requestPath.empty() ? "/" : requestPath;
    // Default path to directory
    size_t lastSlash = c.path.find_last_of('/');
    if (lastSlash != std::string::npos && lastSlash > 0) {
        c.path = c.path.substr(0, lastSlash);
    } else {
        c.path = "/";
    }

    std::stringstream ss(setCookieLine);
    std::string token;
    bool isFirst = true;

    while (std::getline(ss, token, ';')) {
        token = Trim(token);
        if (token.empty()) continue;

        size_t eq = token.find('=');
        if (isFirst) {
            isFirst = false;
            if (eq != std::string::npos) {
                c.name = Trim(token.substr(0, eq));
                c.value = Trim(token.substr(eq + 1));
            } else {
                c.name = token;
                c.value = "";
            }
            continue;
        }

        std::string attrName = (eq != std::string::npos) ? Trim(token.substr(0, eq)) : token;
        std::string attrVal = (eq != std::string::npos) ? Trim(token.substr(eq + 1)) : "";
        std::string lowerAttr = ToLower(attrName);

        if (lowerAttr == "domain" && !attrVal.empty()) {
            std::string d = ToLower(attrVal);
            while (!d.empty() && d.front() == '.') d.erase(0, 1);
            if (!d.empty()) c.domain = d;
        } else if (lowerAttr == "path" && !attrVal.empty()) {
            c.path = attrVal;
        } else if (lowerAttr == "secure") {
            c.secure = true;
        } else if (lowerAttr == "httponly") {
            c.httpOnly = true;
        } else if (lowerAttr == "max-age" && !attrVal.empty()) {
            try {
                int64_t maxAge = std::stoll(attrVal);
                c.expires = CurrentEpochSec() + maxAge;
            } catch (...) {}
        }
    }

    if (!c.name.empty()) {
        Set(c);
    }
}

std::string CookieJar::GetCookieHeader(const std::string& host, const std::string& path, bool isSecure) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const int64_t now = CurrentEpochSec();

    std::string header;
    for (const auto& c : m_cookies) {
        if (c.expires > 0 && c.expires <= now) continue;
        if (c.secure && !isSecure) continue;
        if (!DomainMatches(host, c.domain)) continue;
        if (!PathMatches(path, c.path)) continue;

        if (!header.empty()) header += "; ";
        header += c.name + "=" + c.value;
    }
    return header;
}

void CookieJar::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cookies.clear();
}

void CookieJar::ClearDomain(const std::string& domain) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string d = ToLower(domain);
    m_cookies.erase(std::remove_if(m_cookies.begin(), m_cookies.end(), [&](const Cookie& c) {
        return DomainMatches(d, c.domain);
    }), m_cookies.end());
}

std::string CookieJar::ToJson() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    json::Value arr = json::Value::Arr();
    for (const auto& c : m_cookies) {
        json::Value obj = json::Value::Obj();
        obj.Add("name", json::Value::Str(c.name));
        obj.Add("value", json::Value::Str(c.value));
        obj.Add("domain", json::Value::Str(c.domain));
        obj.Add("path", json::Value::Str(c.path));
        obj.Add("expires", json::Value::Num((double)c.expires));
        obj.Add("secure", json::Value::Bool(c.secure));
        obj.Add("httpOnly", json::Value::Bool(c.httpOnly));
        arr.Push(std::move(obj));
    }
    return json::Dump(arr);
}

void CookieJar::LoadJson(const std::string& jsonStr) {
    json::Value v;
    std::string err;
    if (!json::Parse(jsonStr, v, &err) || !v.IsArray()) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_cookies.clear();
    for (const auto& item : v.arr) {
        if (!item.IsObject()) continue;
        Cookie c;
        if (const auto* n = item.Get("name")) c.name = n->AsString();
        if (const auto* val = item.Get("value")) c.value = val->AsString();
        if (const auto* dom = item.Get("domain")) c.domain = dom->AsString();
        if (const auto* p = item.Get("path")) c.path = p->AsString();
        if (const auto* exp = item.Get("expires")) c.expires = exp->AsInt();
        if (const auto* sec = item.Get("secure")) c.secure = sec->AsBool();
        if (const auto* ho = item.Get("httpOnly")) c.httpOnly = ho->AsBool();
        if (!c.name.empty()) m_cookies.push_back(std::move(c));
    }
}

} // namespace gm::net
