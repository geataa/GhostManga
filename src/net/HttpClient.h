#pragma once

#include <string>
#include <vector>
#include <utility>
#include <cstdint>
#include <windows.h>
#include <winhttp.h>

namespace gm::net {

struct HttpRequest {
    std::string url;
    std::string method = "GET";
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    int         timeoutMs = 30000;
    bool        followRedirects = true;
    bool        allowBrowserFallback = true;
    size_t      maxResponseBytes = 64 * 1024 * 1024;
};

struct HttpResponse {
    int         status = 0;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    std::string finalUrl;
    std::vector<std::string> setCookies;
    std::string error;
    bool        success = false;

    std::string GetHeader(const std::string& name) const;
};

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    // Disable copy
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    bool Init(const std::wstring& userAgent = L"");
    void Close();

    HttpResponse Send(const HttpRequest& req);

private:
    HINTERNET m_session = nullptr;
};

} // namespace gm::net
