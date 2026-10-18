#include "WebViewHost.h"
#include "core/Paths.h"
#include "core/Log.h"
#include "core/Utf8.h"
#include "core/Json.h"

#include <windows.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <chrono>

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;

namespace gm::browser {

static constexpr UINT WM_DISPATCH_WORK = WM_USER + 101;
static constexpr UINT WM_SHUTDOWN_WV   = WM_USER + 102;

struct BrowserTask {
    net::HttpRequest req;
    bool allowInteractive = false;
    std::promise<net::HttpResponse> prom;
    std::atomic<bool> satisfied{ false };
    bool fetchStarted = false; // Accessed only by the browser's STA thread.
    bool retriedInterstitial = false;
};

class WebViewHost::Impl {
public:
    Impl() = default;
    ~Impl() { Shutdown(); }

    bool IsAvailable() {
        PWSTR ver = nullptr;
        HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &ver);
        if (SUCCEEDED(hr) && ver) {
            CoTaskMemFree(ver);
            return true;
        }
        return false;
    }

    bool Init() {
        if (m_running && m_ready) return true;
        if (!IsAvailable()) {
            log::Write(L"[WebViewHost] Microsoft Edge WebView2 Evergreen runtime is not installed.");
            return false;
        }

        std::unique_lock<std::mutex> lock(m_initMutex);
        if (m_running && m_ready) return true;
        if (m_worker.joinable()) {
            m_running = false;
            if (m_hwndHost) PostMessageW(m_hwndHost, WM_SHUTDOWN_WV, 0, 0);
            m_worker.join();
        }
        m_ready = false;
        m_initFailed = false;

        m_worker = std::thread(&Impl::WorkerThread, this);
        m_initCv.wait_for(lock, std::chrono::seconds(15), [this]() { return m_ready.load() || m_initFailed.load(); });

        return m_ready;
    }

    void Shutdown() {
        if (!m_running && !m_worker.joinable()) return;
        m_running = false;

        if (m_hwndHost) {
            PostMessageW(m_hwndHost, WM_SHUTDOWN_WV, 0, 0);
        }

        if (m_worker.joinable()) {
            m_worker.join();
        }

        m_ready = false;
        m_initFailed = false;
    }

    net::HttpResponse Execute(const net::HttpRequest& req, bool allowInteractive) {
        if (!m_ready && !Init()) {
            net::HttpResponse resp;
            resp.status = 503;
            resp.error = "WebView2 runtime is not available";
            return resp;
        }

        auto task = std::make_shared<BrowserTask>();
        task->req = req;
        task->allowInteractive = allowInteractive;
        auto fut = task->prom.get_future();

        {
            std::lock_guard<std::mutex> lock(m_taskMutex);
            m_tasks.push(task);
        }

        PostMessageW(m_hwndHost, WM_DISPATCH_WORK, 0, 0);

        // Wait with timeout
        auto status = fut.wait_for(std::chrono::milliseconds(req.timeoutMs > 0 ? req.timeoutMs : 30000));
        if (status == std::future_status::ready) {
            try {
                return fut.get();
            } catch (...) {
                net::HttpResponse errResp;
                errResp.status = 500;
                errResp.error = "WebView2 future exception";
                return errResp;
            }
        }

        task->satisfied.store(true);
        if (m_hwndHost) {
            PostMessageW(m_hwndHost, WM_DISPATCH_WORK, 0, 0);
        }
        net::HttpResponse timeoutResp;
        timeoutResp.status = 408;
        timeoutResp.error = "Request timed out in WebView2";
        return timeoutResp;
    }

private:
    void WorkerThread() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr)) {
            SignalInit(false);
            return;
        }

        // Register host window class
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = Impl::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"GhostManga_WebViewHost";
        RegisterClassExW(&wc);

        m_hwndHost = CreateWindowExW(
            WS_EX_TOOLWINDOW,
            wc.lpszClassName,
            L"GhostManga WebView Host",
            WS_POPUP | WS_CLIPCHILDREN,
            -2000, -2000, 1, 1,
            nullptr, nullptr, wc.hInstance, this
        );

        if (!m_hwndHost) {
            CoUninitialize();
            SignalInit(false);
            return;
        }

        // Interactive verification and background fetch use the same Chromium
        // profile. Clearance cookies remain in Chromium, never in WinHTTP.
        std::wstring dataDir = paths::DataDir() + L"\\webview2";
        CreateDirectoryW(dataDir.c_str(), nullptr);

        auto envOptions = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();

        hr = CreateCoreWebView2EnvironmentWithOptions(
            nullptr,
            dataDir.c_str(),
            envOptions.Get(),
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this](HRESULT res, ICoreWebView2Environment* env) -> HRESULT {
                    if (FAILED(res) || !env) {
                        SignalInit(false);
                        PostQuitMessage(0);
                        return res;
                    }
                    m_env = env;
                    m_env->CreateCoreWebView2Controller(
                        m_hwndHost,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [this](HRESULT cRes, ICoreWebView2Controller* ctrl) -> HRESULT {
                                if (FAILED(cRes) || !ctrl) {
                                    SignalInit(false);
                                    PostQuitMessage(0);
                                    return cRes;
                                }
                                m_controller = ctrl;
                                m_controller->get_CoreWebView2(&m_webview);

                                // Initially hidden 1x1
                                m_controller->put_IsVisible(FALSE);
                                RECT bounds = { 0, 0, 1, 1 };
                                m_controller->put_Bounds(bounds);

                                // Setup navigation handlers
                                SetupHandlers();

                                m_running = true;
                                SignalInit(true);
                                return S_OK;
                            }
                        ).Get()
                    );
                    return S_OK;
                }
            ).Get()
        );

        if (FAILED(hr)) {
            DestroyWindow(m_hwndHost);
            CoUninitialize();
            SignalInit(false);
            return;
        }

        // Message Loop
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // Cleanup
        if (m_controller) {
            m_controller->Close();
            m_controller.Reset();
        }
        m_webview.Reset();
        m_env.Reset();

        DestroyWindow(m_hwndHost);
        m_hwndHost = nullptr;

        CoUninitialize();
    }

    void SetupHandlers() {
        if (!m_webview) return;

        m_webview->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                    if (!m_waitingForNavigation || !args) return S_OK;
                    PWSTR uri = nullptr;
                    if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
                        args->get_NavigationId(&m_navigationId);
                        CoTaskMemFree(uri);
                    }
                    return S_OK;
                }).Get(), &m_navStartToken);

        m_webview->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                    if (!m_currentTask || m_currentTask->satisfied.load() || !m_waitingForNavigation) return S_OK;
                    UINT64 navigationId = 0;
                    if (args) args->get_NavigationId(&navigationId);
                    if (!m_navigationId || navigationId != m_navigationId) return S_OK;
                    BOOL ok = FALSE;
                    if (args) args->get_IsSuccess(&ok);
                    if (!ok && args) {
                        COREWEBVIEW2_WEB_ERROR_STATUS status;
                        args->get_WebErrorStatus(&status);
                        if (status == COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED) return S_OK;
                    }
                    m_waitingForNavigation = false;
                    if (ok) ExecuteFetch(m_currentTask);
                    else {
                        net::HttpResponse response;
                        COREWEBVIEW2_WEB_ERROR_STATUS status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                        if (args) args->get_WebErrorStatus(&status);
                        response.error = "WebView2 kaynak sayfasina ulasamadi (ag hata kodu: " + std::to_string((int)status) + ")";
                        CompleteCurrentTask(response);
                    }
                    return S_OK;
                }).Get(), &m_navToken);

        // Block all popup windows and new window requests
        m_webview->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                    if (args) args->put_Handled(TRUE);
                    return S_OK;
                }
            ).Get(),
            &m_newWinToken
        );

        // Suppress script dialogs (alert, confirm, prompt)
        m_webview->add_ScriptDialogOpening(
            Callback<ICoreWebView2ScriptDialogOpeningEventHandler>(
                [](ICoreWebView2*, ICoreWebView2ScriptDialogOpeningEventArgs* args) -> HRESULT {
                    if (args) args->Accept();
                    return S_OK;
                }
            ).Get(),
            &m_dialogToken
        );

        // Deny all permission requests (notifications, geolocation, etc.)
        m_webview->add_PermissionRequested(
            Callback<ICoreWebView2PermissionRequestedEventHandler>(
                [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
                    if (args) args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
                    return S_OK;
                }
            ).Get(),
            &m_permToken
        );

        ComPtr<ICoreWebView2Settings> settings;
        if (SUCCEEDED(m_webview->get_Settings(&settings)) && settings) {
            settings->put_AreDefaultContextMenusEnabled(FALSE);
            settings->put_AreDevToolsEnabled(FALSE);
            settings->put_IsStatusBarEnabled(FALSE);
            settings->put_AreHostObjectsAllowed(FALSE);
        }
    }

    void ProcessNextTask() {
        if (m_currentTask) {
            if (m_currentTask->satisfied.load()) {
                m_currentTask.reset();
            } else {
                return; // Busy
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_taskMutex);
            if (m_tasks.empty()) return;
            m_currentTask = m_tasks.front();
            m_tasks.pop();
        }

        if (!m_webview || !m_currentTask) return;

        auto task = m_currentTask;
        if (task->satisfied.load()) { m_currentTask.reset(); ProcessNextTask(); return; }
        const auto scheme = task->req.url.find("://");
        const auto slash = task->req.url.find('/', scheme == std::string::npos ? 0 : scheme + 3);
        const std::string origin = slash == std::string::npos ? task->req.url : task->req.url.substr(0, slash);
        PWSTR source = nullptr;
        std::string current;
        if (SUCCEEDED(m_webview->get_Source(&source)) && source) { current = ToUtf8(source); CoTaskMemFree(source); }
        m_waitingForNavigation = false;
        if (current.rfind(origin + "/", 0) == 0) ExecuteFetch(task);
        else {
            m_controller->put_IsVisible(FALSE);
            ShowWindow(m_hwndHost, SW_HIDE);
            // Establish a same-origin browser context. The actual request below
            // retains method, POST body and headers instead of navigating to it.
            m_navigationTarget = origin + "/";
            m_navigationId = 0;
            m_waitingForNavigation = true;
            const HRESULT hr = m_webview->Navigate(FromUtf8(m_navigationTarget).c_str());
            if (FAILED(hr)) {
                net::HttpResponse response;
                response.error = "WebView2 gezinme baslatilamadi";
                CompleteCurrentTask(response);
            }
        }
    }

    void ExecuteFetch(const std::shared_ptr<BrowserTask>& task) {
        if (task->fetchStarted || task->satisfied.load()) return;
        task->fetchStarted = true;
        using json::Value;
        Value options = Value::Obj();
        options.Add("method", Value::Str(task->req.method));
        options.Add("credentials", Value::Str("include"));
        Value headers = Value::Obj();
        for (const auto& [name, value] : task->req.headers) {
            // Chromium owns these headers. The requested Referer is set via the
            // fetch referrer option and cookies stay in the browser session.
            if (_stricmp(name.c_str(), "Cookie") == 0 || _stricmp(name.c_str(), "User-Agent") == 0 || _stricmp(name.c_str(), "Origin") == 0 || _strnicmp(name.c_str(), "Sec-", 4) == 0) continue;
            if (_stricmp(name.c_str(), "Referer") == 0) { options.Add("referrer", Value::Str(value)); continue; }
            headers.Add(name, Value::Str(value));
        }
        options.Add("headers", std::move(headers));
        if (task->req.method != "GET" && task->req.method != "HEAD") options.Add("body", Value::Str(task->req.body));
        std::string expression = "(async()=>{try{const o=" + json::Dump(options) + ";o.signal=AbortSignal.timeout(" + std::to_string(std::max(1, task->req.timeoutMs)) + ");const r=await fetch(" + json::Dump(Value::Str(task->req.url)) + ",o);const text=await r.text();return {status:r.status,text,finalUrl:r.url,headers:Object.fromEntries(r.headers.entries())};}catch(e){return {status:0,error:String(e.message||e)};}})()";
        Value parameters = Value::Obj();
        parameters.Add("expression", Value::Str(expression));
        parameters.Add("awaitPromise", Value::Bool(true));
        parameters.Add("returnByValue", Value::Bool(true));
        const auto parameterText = FromUtf8(json::Dump(parameters));
        const HRESULT call = m_webview->CallDevToolsProtocolMethod(L"Runtime.evaluate", parameterText.c_str(),
            Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
                [this, task](HRESULT hr, LPCWSTR text) -> HRESULT {
                    if (m_currentTask != task || task->satisfied.load()) return S_OK;
                    net::HttpResponse response;
                    response.finalUrl = task->req.url;
                    json::Value root;
                    if (SUCCEEDED(hr) && text && json::Parse(ToUtf8(text), root, nullptr)) {
                        const auto* result = root.Get("result");
                        const auto* value = result ? result->Get("value") : nullptr;
                        if (value && value->IsObject()) {
                            if (const auto* v = value->Get("status")) response.status = (int)v->AsInt();
                            if (const auto* v = value->Get("text")) response.body = v->AsString();
                            if (const auto* v = value->Get("finalUrl")) response.finalUrl = v->AsString();
                            if (const auto* v = value->Get("error")) response.error = v->AsString();
                            if (const auto* v = value->Get("headers"); v && v->IsObject())
                                for (const auto& header : v->obj) response.headers.emplace_back(header.key, header.value.AsString());
                            response.success = response.status >= 200 && response.status < 300;
                            if (response.body.size() > task->req.maxResponseBytes) {
                                response.body.clear(); response.success = false; response.error = "Response exceeds size limit";
                            }
                        }
                    }
                    // A fetched HTML redirect cannot execute its scripts. Visit
                    // the GET once in Chromium, then repeat the real fetch.
                    if (response.status == 200 && task->req.method == "GET" && !task->retriedInterstitial &&
                        response.body.find("<title>Redirecting...</title>") != std::string::npos) {
                        task->retriedInterstitial = true;
                        task->fetchStarted = false;
                        m_navigationTarget = task->req.url;
                        m_navigationId = 0;
                        m_waitingForNavigation = true;
                        if (SUCCEEDED(m_webview->Navigate(FromUtf8(m_navigationTarget).c_str()))) return S_OK;
                        m_waitingForNavigation = false;
                    }
                    if (!response.success && response.error.empty()) response.error = "WebView2 HTTP " + std::to_string(response.status);
                    CompleteCurrentTask(response);
                    return S_OK;
                }).Get());
        if (FAILED(call)) {
            net::HttpResponse response;
            response.error = "WebView2 fetch baslatilamadi";
            CompleteCurrentTask(response);
        }
    }

    void CompleteCurrentTask(const net::HttpResponse& resp) {
        if (!m_currentTask) return;

        auto task = m_currentTask;
        m_currentTask.reset();

        // Ensure window stays hidden
        if (m_controller) {
            m_controller->put_IsVisible(FALSE);
            ShowWindow(m_hwndHost, SW_HIDE);
        }

        if (!task->satisfied.exchange(true)) {
            try {
                task->prom.set_value(resp);
            } catch (...) {
            }
        }

        // Process any queued tasks
        ProcessNextTask();
    }

    void SignalInit(bool ok) {
        std::lock_guard<std::mutex> lock(m_initMutex);
        m_ready = ok;
        m_initFailed = !ok;
        m_initCv.notify_all();
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        Impl* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = reinterpret_cast<Impl*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (self) {
            if (msg == WM_DISPATCH_WORK) {
                self->ProcessNextTask();
                return 0;
            } else if (msg == WM_SHUTDOWN_WV) {
                PostQuitMessage(0);
                return 0;
            }
        }

        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    HWND m_hwndHost = nullptr;
    ComPtr<ICoreWebView2Environment> m_env;
    ComPtr<ICoreWebView2Controller>  m_controller;
    ComPtr<ICoreWebView2>            m_webview;
    EventRegistrationToken           m_navToken{};
    EventRegistrationToken           m_navStartToken{};
    bool m_waitingForNavigation = false;
    UINT64 m_navigationId = 0;
    std::string m_navigationTarget;
    EventRegistrationToken           m_newWinToken{};
    EventRegistrationToken           m_dialogToken{};
    EventRegistrationToken           m_permToken{};

    std::thread m_worker;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_ready{false};
    std::atomic<bool> m_initFailed{false};

    std::mutex m_initMutex;
    std::condition_variable m_initCv;

    std::mutex m_taskMutex;
    std::queue<std::shared_ptr<BrowserTask>> m_tasks;
    std::shared_ptr<BrowserTask> m_currentTask;
};

WebViewHost::WebViewHost() : m_impl(std::make_unique<Impl>()) {}
WebViewHost::~WebViewHost() = default;

WebViewHost& WebViewHost::Instance() {
    static WebViewHost s_instance;
    return s_instance;
}

bool WebViewHost::IsAvailable() {
    return m_impl->IsAvailable();
}

bool WebViewHost::Init() {
    return m_impl->Init();
}

void WebViewHost::Shutdown() {
    m_impl->Shutdown();
}

net::HttpResponse WebViewHost::Execute(const net::HttpRequest& req, bool allowInteractive) {
    return m_impl->Execute(req, allowInteractive);
}

} // namespace gm::browser
