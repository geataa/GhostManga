#include "HttpClient.h"
#include "../core/Utf8.h"
#include <algorithm>
#include <sstream>
#include <vector>
#include <chrono>

#pragma comment(lib, "winhttp.lib")

namespace gm::net {

static std::string ToLower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static std::string DescribeTransportError(DWORD error) {
    switch (error) {
    case 12007: return " (DNS: alan adi cozumlenemedi)";
    case 12002: return " (Baglanti zaman asimi)";
    case 12175: return " (TLS/sertifika dogrulamasi basarisiz)";
    case 12029: return " (Sunucuya baglanti kurulamadi)";
    case 12030: return " (Sunucu baglantiyi kesti)";
    default: return "";
    }
}

std::string HttpResponse::GetHeader(const std::string& name) const {
    const std::string lower = ToLower(name);
    for (const auto& [k, v] : headers) {
        if (ToLower(k) == lower) return v;
    }
    return "";
}

HttpClient::HttpClient() {
    Init();
}

HttpClient::~HttpClient() {
    Close();
}

bool HttpClient::Init(const std::wstring& userAgent) {
    if (m_session) return true;

    std::wstring ua = userAgent.empty()
        ? L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36"
        : userAgent;

    m_session = WinHttpOpen(ua.c_str(),
                            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS,
                            0);
    if (!m_session) return false;

    // Enable HTTP/2
    DWORD http2 = WINHTTP_PROTOCOL_FLAG_HTTP2;
    WinHttpSetOption(m_session, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &http2, sizeof(http2));

    // Enable TLS 1.2 and TLS 1.3
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(m_session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    // Disable WinHTTP automatic cookie handling (we manage cookies in our CookieJar)
    DWORD disableCookies = WINHTTP_DISABLE_COOKIES;
    WinHttpSetOption(m_session, WINHTTP_OPTION_DISABLE_FEATURE, &disableCookies, sizeof(disableCookies));

    // Enable automatic decompression (gzip, deflate)
    DWORD decomp = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
    WinHttpSetOption(m_session, WINHTTP_OPTION_DECOMPRESSION, &decomp, sizeof(decomp));

    // Increase max connections per server for fast parallel image downloads
    DWORD maxConns = 32;
    WinHttpSetOption(m_session, WINHTTP_OPTION_MAX_CONNS_PER_SERVER, &maxConns, sizeof(maxConns));
    WinHttpSetOption(m_session, WINHTTP_OPTION_MAX_CONNS_PER_1_0_SERVER, &maxConns, sizeof(maxConns));

    // Set default timeouts (resolve, connect, send, receive in ms)
    WinHttpSetTimeouts(m_session, 5000, 8000, 15000, 30000);

    return true;
}

void HttpClient::Close() {
    if (m_session) {
        WinHttpCloseHandle(m_session);
        m_session = nullptr;
    }
}

HttpResponse HttpClient::Send(const HttpRequest& req) {
    HttpResponse res;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(req.timeoutMs > 0 ? req.timeoutMs : 30000);
    if (!m_session && !Init()) {
        res.error = "Failed to initialize WinHTTP session";
        return res;
    }

    std::string safeUrl = req.url;
    if (safeUrl.rfind("//", 0) == 0) {
        safeUrl = "https:" + safeUrl;
    }

    std::wstring wUrl = FromUtf8(safeUrl);
    URL_COMPONENTS urlComp{};
    urlComp.dwStructSize = sizeof(urlComp);

    wchar_t hostName[512] = { 0 };
    wchar_t urlPath[8192] = { 0 };
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = (DWORD)std::size(hostName);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = (DWORD)std::size(urlPath);

    if (!WinHttpCrackUrl(wUrl.c_str(), (DWORD)wUrl.length(), 0, &urlComp)) {
        res.error = "Invalid URL: " + req.url;
        return res;
    }

    const bool isHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
    INTERNET_PORT port = urlComp.nPort;

    HINTERNET hConnect = WinHttpConnect(m_session, hostName, port, 0);
    if (!hConnect) {
        res.error = "Connection failed to " + ToUtf8(hostName);
        return res;
    }

    std::wstring wMethod = FromUtf8(req.method);
    DWORD openFlags = isHttps ? WINHTTP_FLAG_SECURE : 0;

    HINTERNET hRequest = WinHttpOpenRequest(hConnect,
                                           wMethod.c_str(),
                                           urlPath,
                                           nullptr,
                                           WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           openFlags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        res.error = "Failed to open WinHTTP request";
        return res;
    }

    // Set timeouts
    if (req.timeoutMs > 0) {
        WinHttpSetTimeouts(hRequest, req.timeoutMs / 4, req.timeoutMs / 4, req.timeoutMs / 2, req.timeoutMs);
    }

    // Redirect policy
    DWORD redirectOption = req.followRedirects ? WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS : WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectOption, sizeof(redirectOption));

    // Build headers
    std::wstring headerStr;
    for (const auto& [k, v] : req.headers) {
        headerStr += FromUtf8(k) + L": " + FromUtf8(v) + L"\r\n";
    }

    // Send request
    const void* bodyPtr = req.body.empty() ? WINHTTP_NO_REQUEST_DATA : req.body.data();
    DWORD bodyLen = (DWORD)req.body.size();

    BOOL sent = WinHttpSendRequest(hRequest,
                                   headerStr.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headerStr.c_str(),
                                   (DWORD)headerStr.length(),
                                   const_cast<void*>(bodyPtr),
                                   bodyLen,
                                   bodyLen,
                                   0);

    if (!sent) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        res.error = "WinHttpSendRequest failed with error: " + std::to_string(err) + DescribeTransportError(err);
        return res;
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        res.error = "WinHttpReceiveResponse failed with error: " + std::to_string(err) + DescribeTransportError(err);
        return res;
    }

    // Query status code
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (WinHttpQueryHeaders(hRequest,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &statusCode,
                            &statusSize,
                            WINHTTP_NO_HEADER_INDEX)) {
        res.status = (int)statusCode;
    }

    // Query all headers raw
    DWORD headersLen = 0;
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                        nullptr, &headersLen, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && headersLen > 0) {
        std::vector<wchar_t> hBuf(headersLen / sizeof(wchar_t) + 1, 0);
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                                hBuf.data(), &headersLen, WINHTTP_NO_HEADER_INDEX)) {
            std::string rawUtf8 = ToUtf8(hBuf.data());
            std::stringstream ss(rawUtf8);
            std::string line;
            while (std::getline(ss, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                size_t colon = line.find(':');
                if (colon != std::string::npos) {
                    std::string key = line.substr(0, colon);
                    std::string val = line.substr(colon + 1);
                    while (!val.empty() && val.front() == ' ') val.erase(0, 1);
                    res.headers.push_back({ std::move(key), std::move(val) });
                }
            }
        }
    }

    // Query Set-Cookie headers with dwIndex loop
    DWORD cookieIndex = 0;
    while (true) {
        DWORD cookieLen = 0;
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_SET_COOKIE, WINHTTP_HEADER_NAME_BY_INDEX,
                            nullptr, &cookieLen, &cookieIndex);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || cookieLen == 0) break;

        std::vector<wchar_t> cBuf(cookieLen / sizeof(wchar_t) + 1, 0);
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_SET_COOKIE, WINHTTP_HEADER_NAME_BY_INDEX,
                                cBuf.data(), &cookieLen, &cookieIndex)) {
            res.setCookies.push_back(ToUtf8(cBuf.data()));
        } else {
            break;
        }
    }

    // Check Content-Length header if available
    DWORD clLen = sizeof(DWORD);
    DWORD expectedContentLength = 0;
    bool hasExpectedLength = WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &expectedContentLength, &clLen, WINHTTP_NO_HEADER_INDEX) != FALSE;

    // Read response body
    std::string responseBody;
    bool readError = false;
    while (true) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) { readError = true; res.error = "Request deadline exceeded"; break; }
        WinHttpSetTimeouts(hRequest, (int)remaining, (int)remaining, (int)remaining, (int)remaining);
        DWORD bytesAvailable = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &bytesAvailable)) {
            readError = true;
            break;
        }
        if (bytesAvailable == 0) {
            break; // EOF
        }
        const DWORD chunkSize = std::min<DWORD>(bytesAvailable, 65536);
        if (responseBody.size() + chunkSize > req.maxResponseBytes) {
            readError = true; res.error = "Response exceeds size limit"; break;
        }
        std::vector<char> buffer(chunkSize);
        DWORD bytesRead = 0;
        if (!WinHttpReadData(hRequest, buffer.data(), chunkSize, &bytesRead) || bytesRead == 0) {
            readError = true;
            break;
        }
        responseBody.append(buffer.data(), bytesRead);
    }

    if (hasExpectedLength && expectedContentLength > 0 && responseBody.size() < expectedContentLength) {
        readError = true;
    }

    // Query final URL if redirected by WinHTTP
    DWORD urlLen = 0;
    WinHttpQueryOption(hRequest, WINHTTP_OPTION_URL, nullptr, &urlLen);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && urlLen > 0) {
        std::vector<wchar_t> urlBuf(urlLen / sizeof(wchar_t) + 1, 0);
        if (WinHttpQueryOption(hRequest, WINHTTP_OPTION_URL, urlBuf.data(), &urlLen)) {
            res.finalUrl = ToUtf8(urlBuf.data());
        }
    }
    if (res.finalUrl.empty()) {
        res.finalUrl = req.url;
    }

    res.body = std::move(responseBody);
    if (!readError && res.status >= 200 && res.status < 400) {
        if (res.body.empty() && req.method != "HEAD") {
            res.success = false;
            res.error = "Empty response body received";
        } else {
            res.success = true;
        }
    } else {
        res.success = false;
        if (readError && res.error.empty()) {
            res.error = "Connection closed or read failed during download";
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);

    // Otomatik Yönlendirme (HTTP -> HTTPS veya şema değişimi WinHTTP tarafından engellenir, elle takip et)
    if (req.followRedirects && (res.status == 301 || res.status == 302 || res.status == 303 || res.status == 307 || res.status == 308)) {
        std::string loc = res.GetHeader("Location");
        if (!loc.empty()) {
            if (loc.find("://") == std::string::npos) {
                if (loc.front() == '/') {
                    loc = (isHttps ? "https://" : "http://") + ToUtf8(hostName) + loc;
                } else {
                    loc = (isHttps ? "https://" : "http://") + ToUtf8(hostName) + "/" + loc;
                }
            }
            static thread_local int s_redirDepth = 0;
            if (s_redirDepth < 5) {
                s_redirDepth++;
                HttpRequest nextReq = req;
                nextReq.url = loc;
                HttpResponse redirRes = Send(nextReq);
                s_redirDepth--;
                return redirRes;
            }
        }
    }

    return res;
}

} // namespace gm::net
