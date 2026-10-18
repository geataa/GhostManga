#pragma once

#include "net/HttpClient.h"
#include <string>
#include <memory>
#include <future>

namespace gm::browser {

class WebViewHost {
public:
    static WebViewHost& Instance();

    // Check if Edge WebView2 Evergreen Runtime is installed on system
    bool IsAvailable();

    // Initialize the STA worker thread and WebView2 environment
    bool Init();

    // Graceful shutdown of STA worker and WebView2
    void Shutdown();

    // Execute request in browser context (Lane C)
    net::HttpResponse Execute(const net::HttpRequest& req, bool allowInteractive = false);

private:
    WebViewHost();
    ~WebViewHost();

    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace gm::browser
