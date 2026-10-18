#include "core/I18n.h"
#include "ui/BrowserView.h"
#include "core/Paths.h"
#include "core/Log.h"
#include "core/Utf8.h"
#include "core/Json.h"
#include "core/FileIo.h"
#include "net/Fetch.h"
#include "engine/ImageValidation.h"
#include "engine/ChapterState.h"
#include "browser/BrowserImageBuffer.h"
#include "ui/Theme.h"
#include "ui/Icons.h"

#include <windows.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"

#include <sstream>
#include <iomanip>
#include <thread>
#include <algorithm>
#include <ctime>

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;

namespace gm {

static uint64_t HashUrl(const std::string& str) {
    uint64_t hash = 14695981039346656037ull; // FNV-1a
    for (char c : str) {
        hash ^= (uint8_t)c;
        hash *= 1099511628211ull;
    }
    return hash;
}

static bool IsAdUrl(const std::wstring& wurl) {
    std::string u = ToUtf8(wurl);
    for (char& c : u) c = (char)tolower((unsigned char)c);
    static const char* const kAdKeywords[] = {
        "doubleclick.", "googlesyndication.", "google-analytics.", "adservice.",
        "popcash.", "popads.", "adsterra.", "exoclick.", "mgid.", "propellerads.",
        "trafficjunky.", "juicyads.", "clickadu.", "hilltopads.", "monetag.",
        "yadro.", "criteo.", "taboola.", "outbrain.", "adkeeper.", "adtrue.",
        "bidvertiser.", "ero-advertising.", "coinhive", "creativecdn.",
        "adnxs.", "serving-sys.", "smartadserver.", "pubmatic.", "openx.",
        "rubiconproject.", "/ads.js", "/ad.js", "ad_banner", "banner_ad",
        "popunder", "zeroredirect", "clarity.ms", "histats.com", "/adserver",
        "syndication.", "admaven.", "a-ads.", "runative.", "trafficstars.",
        "richpush.", "notix.", "onclickalgo.", "onclickmega.", "admicro.",
        "buzzoola.", "vidoomy.", "aniview.", "adroll.", "adform.",
        "1xbet.", "bet365.", "betway.", "stake.com", "vulkan-", "casino",
        "bongacams.", "chaturbate.", "stripchat.", "livejasmin.",
        "/pagead/", "/ads?", "adsystem.", "adserver.", "adlightning."
    };
    for (const char* kw : kAdKeywords) {
        if (u.find(kw) != std::string::npos) return true;
    }
    return false;
}

static bool IsValidImageBuffer(const std::string& bytes) {return gm::engine::ValidImage(bytes);}
static bool IsValidImageFile(const std::wstring& path) {return gm::engine::ValidImageFile(path);}

class BrowserView::Impl {
public:
    ComPtr<ICoreWebView2Environment> m_env;
    ComPtr<ICoreWebView2Controller> m_controller;
    ComPtr<ICoreWebView2> m_webview;
    EventRegistrationToken m_srcToken{};
    EventRegistrationToken m_navToken{};
    EventRegistrationToken m_docToken{};
    EventRegistrationToken m_newWinToken{};
    EventRegistrationToken m_accelToken{};
    EventRegistrationToken m_resToken{};
    EventRegistrationToken m_responseToken{};
    EventRegistrationToken m_msgToken{};
    EventRegistrationToken m_dialogToken{};
    EventRegistrationToken m_permToken{};
    HWND m_parent = nullptr;
    BrowserHost* m_host = nullptr;
    BrowserView* m_view = nullptr;
    bool m_ready = false;
    bool m_initStarted = false;
    std::wstring m_pendingUrl;

    enum class CrawlMode { None, Chapters, Images };
    CrawlMode m_crawlMode = CrawlMode::None;
    std::shared_ptr<browser::BrowserImageBuffer> m_browserImages;
    int m_crawlTabIndex = -1;
    std::string m_crawlMangaUrl;
    std::string m_crawlChapterUrl;
    std::wstring m_crawlMangaTitle;
    std::wstring m_crawlChapterTitle;
    std::function<void(const ChapterCrawlResult&)> m_chapterCrawlCb;
    std::function<void(const ImageCrawlResult&)> m_imageCrawlCb;

    std::string m_pendingMangaCrawlUrl;
    std::string m_pendingImageCrawlUrl;
    std::wstring m_pendingImageMangaTitle;
    std::wstring m_pendingImageChapterTitle;
    int m_pendingImageTabIndex = -1;
    bool m_challengeModeActive = false;
    std::string m_challengeTargetUrl;

    bool Init(HWND parent, BrowserHost* host, BrowserView* view) {
        m_parent = parent;
        m_host = host;
        m_view = view;

        if (m_initStarted) return true;
        m_initStarted = true;

        std::wstring dataDir = paths::DataDir() + L"\\webview2";
        CreateDirectoryW(dataDir.c_str(), nullptr);

        auto envOptions = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
        HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, dataDir.c_str(), envOptions.Get(),
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this](HRESULT res, ICoreWebView2Environment* env) -> HRESULT {
                    if (FAILED(res) || !env) {
                        log::Write(L"[BrowserView] WebView2 ortamı oluşturulamadı (0x%08X)", res);
                        return res;
                    }
                    m_env = env;
                    m_env->CreateCoreWebView2Controller(
                        m_parent,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [this](HRESULT cRes, ICoreWebView2Controller* ctrl) -> HRESULT {
                                if (FAILED(cRes) || !ctrl) {
                                    log::Write(L"[BrowserView] WebView2 denetleyicisi oluşturulamadı (0x%08X)", cRes);
                                    return cRes;
                                }
                                m_controller = ctrl;
                                m_controller->get_CoreWebView2(&m_webview);
                                m_controller->put_IsVisible(FALSE);

                                SetupHandlers();
                                m_ready = true;
                                log::Write(L"[BrowserView] WebView2 başarıyla hazırlandı.");

                                if (!m_pendingUrl.empty()) {
                                    m_webview->Navigate(m_pendingUrl.c_str());
                                    m_pendingUrl.clear();
                                }
                                if (m_chapterCrawlCb && !m_pendingMangaCrawlUrl.empty()) {
                                    std::string u = m_pendingMangaCrawlUrl;
                                    m_pendingMangaCrawlUrl.clear();
                                    CrawlMangaChapters(u, m_chapterCrawlCb);
                                }
                                if (m_imageCrawlCb && !m_pendingImageCrawlUrl.empty()) {
                                    int tidx = m_pendingImageTabIndex;
                                    std::string u = m_pendingImageCrawlUrl;
                                    std::wstring mt = m_pendingImageMangaTitle;
                                    std::wstring ct = m_pendingImageChapterTitle;
                                    m_pendingImageCrawlUrl.clear();
                                    CrawlChapterImages(tidx, u, mt, ct, m_imageCrawlCb);
                                }
                                if (m_view) {
                                    m_view->ApplyLastBounds();
                                    if (m_view->IsVisible()) {
                                        m_controller->put_IsVisible(TRUE);
                                    }
                                }
                                if (m_host) m_host->Invalidate();
                                return S_OK;
                            }
                        ).Get()
                    );
                    return S_OK;
                }
            ).Get()
        );

        return SUCCEEDED(hr);
    }

    void SetupHandlers() {
        if (!m_webview || !m_controller) return;

        // 1. URL değişimi dinleyicisi
        m_webview->add_SourceChanged(
            Callback<ICoreWebView2SourceChangedEventHandler>(
                [this](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs*) -> HRESULT {
                    PWSTR uri = nullptr;
                    if (SUCCEEDED(sender->get_Source(&uri)) && uri) {
                        if (m_view) {
                            m_view->m_currentUrl = uri;
                            m_view->m_urlInput.SetText(uri);
                        }
                        CoTaskMemFree(uri);
                        if (m_host) m_host->Invalidate();
                    }
                    return S_OK;
                }
            ).Get(),
            &m_srcToken
        );

        // 2. Doküman başlığı dinleyicisi
        m_webview->add_DocumentTitleChanged(
            Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
                [this](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                    PWSTR title = nullptr;
                    if (SUCCEEDED(sender->get_DocumentTitle(&title)) && title) {
                        if (m_view && m_view->m_mangaTitle.empty()) {
                            m_view->m_mangaTitle = title;
                        }
                        CoTaskMemFree(title);
                        if (m_host) m_host->Invalidate();
                    }
                    return S_OK;
                }
            ).Get(),
            &m_docToken
        );

        // 3. Yeni pencere/sekme taleplerini engelle (Reklam pop-up ve yönlendirmelerini tamamen durdur)
        m_webview->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                    if (args) {
                        args->put_Handled(TRUE);
                    }
                    return S_OK;
                }
            ).Get(),
            &m_newWinToken
        );

        // 3b. Script diyaloglarını (alert, confirm, prompt, onbeforeunload) sessizce kabul et/kapat
        m_webview->add_ScriptDialogOpening(
            Callback<ICoreWebView2ScriptDialogOpeningEventHandler>(
                [](ICoreWebView2*, ICoreWebView2ScriptDialogOpeningEventArgs* args) -> HRESULT {
                    args->Accept();
                    return S_OK;
                }
            ).Get(),
            &m_dialogToken
        );

        // 3c. Bildirim ve izin isteklerini otomatik reddet (Reklam sitelerinin sahte "Robot kontrolü için izin ver" tuzakları)
        m_webview->add_PermissionRequested(
            Callback<ICoreWebView2PermissionRequestedEventHandler>(
                [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
                    args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
                    return S_OK;
                }
            ).Get(),
            &m_permToken
        );

        // 4. Kısayol ve tuş dinleyicisi (WebView2 odağı çaldığında ESC, F11, Alt+Ok tuşlarını yakala)
        m_controller->add_AcceleratorKeyPressed(
            Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                [this](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                    COREWEBVIEW2_KEY_EVENT_KIND kind;
                    if (SUCCEEDED(args->get_KeyEventKind(&kind)) && kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN) {
                        UINT key = 0;
                        if (SUCCEEDED(args->get_VirtualKey(&key))) {
                            if (key == VK_ESCAPE) {
                                args->put_Handled(TRUE);
                                if (m_challengeModeActive) {
                                    m_challengeModeActive = false;
                                    if (m_controller) m_controller->put_IsVisible(FALSE);
                                    if (m_host) {
                                        m_host->RunOnUIThread([this]() {
                                            if (m_view) m_view->CancelChallengeVerification();
                                            if (m_host) m_host->Invalidate();
                                        });
                                    }
                                } else if (m_host) {
                                    m_host->CloseBrowser();
                                }
                                return S_OK;
                            }
                            if (key == VK_F11) {
                                args->put_Handled(TRUE);
                                if (m_parent) PostMessageW(m_parent, WM_KEYDOWN, VK_F11, 0);
                                return S_OK;
                            }
                            if (key == VK_LEFT && (GetKeyState(VK_MENU) & 0x8000)) {
                                args->put_Handled(TRUE);
                                if (m_webview) m_webview->GoBack();
                                return S_OK;
                            }
                            if (key == VK_RIGHT && (GetKeyState(VK_MENU) & 0x8000)) {
                                args->put_Handled(TRUE);
                                if (m_webview) m_webview->GoForward();
                                return S_OK;
                            }
                            if ((key == 'W' || key == 'Q') && (GetKeyState(VK_CONTROL) & 0x8000)) {
                                args->put_Handled(TRUE);
                                if (m_host) m_host->CloseBrowser();
                                return S_OK;
                            }
                        }
                    }
                    return S_OK;
                }
            ).Get(),
            &m_accelToken
        );

        // 5. Ad Blocker: Ağ düzeyinde reklam sunucularını ve izleyicileri engelle
        m_webview->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
        m_webview->add_WebResourceRequested(
            Callback<ICoreWebView2WebResourceRequestedEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
                    ComPtr<ICoreWebView2WebResourceRequest> req;
                    if (SUCCEEDED(args->get_Request(&req)) && req) {
                        PWSTR uri = nullptr;
                        if (SUCCEEDED(req->get_Uri(&uri)) && uri) {
                            std::wstring wuri(uri);
                            CoTaskMemFree(uri);
                            if (IsAdUrl(wuri)) {
                                ComPtr<ICoreWebView2WebResourceResponse> blockResp;
                                if (m_env) {
                                    m_env->CreateWebResourceResponse(nullptr, 204, L"No Content", L"Content-Type: text/plain\r\n", &blockResp);
                                    args->put_Response(blockResp.Get());
                                    return S_OK;
                                }
                            }
                        }
                    }
                    return S_OK;
                }
            ).Get(),
            &m_resToken
        );

        // Reuse images the site's own reader successfully loaded in Chromium.
        // MangaKawaii's CDN rejects a separate WinHTTP session even with Referer.
        ComPtr<ICoreWebView2_2> responseView;
        if(SUCCEEDED(m_webview.As(&responseView)) && responseView) {
            responseView->add_WebResourceResponseReceived(
                Callback<ICoreWebView2WebResourceResponseReceivedEventHandler>(
                    [this](ICoreWebView2*,ICoreWebView2WebResourceResponseReceivedEventArgs* args)->HRESULT {
                        auto buffer=m_browserImages;
                        if(!buffer || m_crawlMode!=CrawlMode::Images || !args) return S_OK;
                        ComPtr<ICoreWebView2WebResourceRequest> request;
                        ComPtr<ICoreWebView2WebResourceResponseView> response;
                        if(FAILED(args->get_Request(&request)) || !request || FAILED(args->get_Response(&response)) || !response) return S_OK;
                        int status=0;response->get_StatusCode(&status);
                        if(status!=200) return S_OK;
                        ComPtr<ICoreWebView2HttpResponseHeaders> headers;
                        if(FAILED(response->get_Headers(&headers)) || !headers) return S_OK;
                        PWSTR type=nullptr;headers->GetHeader(L"Content-Type",&type);
                        const bool isImage=type && _wcsnicmp(type,L"image/",6)==0;
                        CoTaskMemFree(type);
                        if(!isImage) return S_OK;
                        PWSTR uri=nullptr;request->get_Uri(&uri);
                        const std::string url=uri?ToUtf8(uri):"";CoTaskMemFree(uri);
                        if(url.empty()) return S_OK;
                        response->GetContent(Callback<ICoreWebView2WebResourceResponseViewGetContentCompletedHandler>(
                            [buffer,url](HRESULT result,IStream* stream)->HRESULT {
                                if(FAILED(result) || !stream) return S_OK;
                                std::string bytes;
                                char chunk[64*1024];ULONG count=0;
                                HRESULT read;
                                do {
                                    read=stream->Read(chunk,sizeof(chunk),&count);
                                    if(FAILED(read) || bytes.size()+count>browser::BrowserImageBuffer::MaxImageBytes) return S_OK;
                                    bytes.append(chunk,count);
                                } while(read==S_OK && count);
                                buffer->Put(url,std::move(bytes));
                                return S_OK;
                            }).Get());
                        return S_OK;
                    }).Get(),&m_responseToken);
        }

        // 6. DOM Ad Blocker & Popup Engelleme Betiği (Her sayfa ve iframe için)
        std::wstring adBlockScript = LR"JS(
        (function() {
            try {
                // Popup açma ve yönlendirme tuzaklarını engelle (window.open kilitle)
                const noopOpen = function(url) {
                    console.log('[GhostManga AdBlock] Popup engellendi:', url);
                    return null;
                };
                Object.defineProperty(window, 'open', {
                    value: noopOpen,
                    writable: false,
                    configurable: false
                });
            } catch(e) {
                try { window.open = function() { return null; }; } catch(e2) {}
            }

            try {
                // Sahte diyalog tuzaklarını etkisiz kıl
                window.alert = function() {};
                window.confirm = function() { return true; };
                window.prompt = function() { return null; };
            } catch(e) {}

            try {
                // Reklam sınıflarını ve banner'ları gizle (Cloudflare doğrulama öğelerini KESİNLİKLE gizleme!)
                const style = document.createElement('style');
                style.textContent = `
                    [class*="ad-"]:not(#challenge-stage):not(.cf-turnstile):not(#turnstile-wrapper):not(#challenge-form),
                    [id*="ad-"]:not(#challenge-stage):not(.cf-turnstile):not(#turnstile-wrapper):not(#challenge-form),
                    [class*="ad_"]:not(#challenge-stage):not(.cf-turnstile):not(#turnstile-wrapper):not(#challenge-form),
                    [id*="ad_"]:not(#challenge-stage):not(.cf-turnstile):not(#turnstile-wrapper):not(#challenge-form),
                    [class*="adsbygoogle"], [id*="adsbygoogle"], .banner-ad, .ad-banner,
                    iframe[src*="ad"]:not([src*="cloudflare"]):not([src*="turnstile"]):not([src*="captcha"]),
                    iframe[src*="ads"]:not([src*="cloudflare"]):not([src*="turnstile"]),
                    iframe[src*="pop"]:not([src*="cloudflare"]):not([src*="turnstile"]),
                    div[class*="popup"]:not(#challenge-stage):not(.cf-turnstile):not(#turnstile-wrapper):not(#challenge-form),
                    div[id*="popup"]:not(#challenge-stage):not(.cf-turnstile):not(#turnstile-wrapper):not(#challenge-form),
                    .floating-ad, .sticky-ad, .adbox, .ad-container, #ad-overlay, .overlay-ad, .sponsor, .sponsored,
                    [data-ad-slot], [data-ad-client], div[class*="banner"] > a[target="_blank"],
                    .ad_slot, .advertisement, #advertisement, .ad-holder, .ad-wrapper,
                    #disqus_thread, div[class*="float-ad"] {
                        display: none !important;
                        visibility: hidden !important;
                        pointer-events: none !important;
                        height: 0 !important;
                        width: 0 !important;
                        opacity: 0 !important;
                    }
                `;
                (document.head || document.documentElement).appendChild(style);
            } catch(e) {}

            try {
                // Dinamik eklenen reklamları anında sil (Doğrulama widget'larını koru)
                const obs = new MutationObserver(function(muts) {
                    for (const m of muts) {
                        for (const n of m.addedNodes) {
                            if (n.nodeType === 1) {
                                const id = (n.id || '').toLowerCase();
                                const cls = (n.className || '').toString().toLowerCase();
                                if (id.includes('challenge') || id.includes('turnstile') ||
                                    cls.includes('challenge') || cls.includes('turnstile') || cls.includes('cf-')) {
                                    continue;
                                }

                                const tag = n.tagName.toLowerCase();
                                if (tag === 'iframe') {
                                    const src = (n.src || '').toLowerCase();
                                    if (src.includes('cloudflare') || src.includes('turnstile') || src.includes('captcha')) {
                                        continue;
                                    }
                                    if (src.includes('ad') || src.includes('pop') || src.includes('banner')) {
                                        n.remove();
                                        continue;
                                    }
                                }

                                if (cls.includes('ad-') || cls.includes('ad_') || cls.includes('popup') ||
                                    id.includes('ad-') || id.includes('ad_') || id.includes('popup')) {
                                    n.style.display = 'none';
                                }
                            }
                        }
                    }
                });
                obs.observe(document.documentElement, { childList: true, subtree: true });
            } catch(e) {}
        })();
        )JS";

        m_webview->AddScriptToExecuteOnDocumentCreated(
            adBlockScript.c_str(),
            Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
                [](HRESULT, LPCWSTR) -> HRESULT { return S_OK; }
            ).Get()
        );

        // 7. Navigation Tamamlanma Dinleyicisi (Arka Plan Crawl Icin)
        m_webview->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                    BOOL isSuccess = FALSE;
                    if (args) args->get_IsSuccess(&isSuccess);
                    OnNavigationCompleted(isSuccess == TRUE);
                    return S_OK;
                }
            ).Get(),
            &m_navToken
        );

        // 8. JS Mesaj Yakalayici (Crawler Verisi Icin)
        m_webview->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                    PWSTR msgStr = nullptr;
                    if (SUCCEEDED(args->TryGetWebMessageAsString(&msgStr)) && msgStr) {
                        std::wstring wmsg(msgStr);
                        CoTaskMemFree(msgStr);
                        HandleWebMessage(wmsg);
                    } else if (SUCCEEDED(args->get_WebMessageAsJson(&msgStr)) && msgStr) {
                        std::wstring wmsg(msgStr);
                        CoTaskMemFree(msgStr);
                        HandleWebMessage(wmsg);
                    }
                    return S_OK;
                }
            ).Get(),
            &m_msgToken
        );
    }

    void OnNavigationCompleted(bool isSuccess) {
        (void)isSuccess;
        if (m_challengeModeActive) {
            InjectChallengeWatcherScript();
        }
        if (m_crawlMode == CrawlMode::Chapters) {
            InjectChaptersExtractorScript();
        } else if (m_crawlMode == CrawlMode::Images) {
            InjectImagesExtractorScript();
        }
    }

    void InjectChallengeWatcherScript() {
        if (!m_webview) return;
        std::wstring script = LR"JS(
        (function() {
            if (window.__gm_cf_watcher) return;
            window.__gm_cf_watcher = true;

            function isRealBotChallenge() {
                try {
                    if (document.querySelector('.wp-manga-chapter-img, #reader, .reading-content, #main-image, .img-chapter, .chapter-images, .viewer-cnt, .chapter-content, .listing-chapters_wrap, .wp-manga-chapter, ul.chapters, div.eph-num, .summary_content, .series-info, .entry-content img')) {
                        return false;
                    }
                    const imgs = document.querySelectorAll('img');
                    if (imgs && imgs.length > 3) return false;

                    const title = (document.title || '').trim().toLowerCase();
                    const bodyText = (document.body ? (document.body.innerText || document.body.textContent || '') : '').toLowerCase();

                    const hasCfWidget = !!document.querySelector(
                        'iframe[src*="challenges.cloudflare.com"], iframe[src*="turnstile"], ' +
                        '#challenge-stage, #turnstile-wrapper, .cf-turnstile, #cf-turnstile, ' +
                        '#challenge-form, div.cf-browser-verification, div.cf-captcha-container'
                    );
                    const hasCaptcha = !!document.querySelector(
                        'iframe[src*="google.com/recaptcha"], iframe[src*="recaptcha"], iframe[src*="hcaptcha"], .h-captcha'
                    );
                    const hasVerifyPhrase = bodyText.includes('verify you are human') ||
                                            bodyText.includes('verifying you are human') ||
                                            bodyText.includes('checking your browser before accessing') ||
                                            bodyText.includes('needs to review the security of your connection');
                    const isCfTitle = title.includes('just a moment...') || title.includes('just a moment') ||
                                      title.includes('attention required! | cloudflare') || title.includes('ddos-guard');

                    if (hasCaptcha) return true;
                    if (hasCfWidget && (hasVerifyPhrase || isCfTitle || (document.body && document.body.children.length < 15))) return true;
                    if (isCfTitle && hasVerifyPhrase) return true;
                } catch(e) {}
                return false;
            }

            let stableChecks = 0;
            let solvedSent = false;
            let timer = setInterval(function() {
                if (solvedSent) {
                    clearInterval(timer);
                    return;
                }
                const ch = isRealBotChallenge();
                if (!ch) {
                    stableChecks++;
                    if (stableChecks >= 2) {
                        solvedSent = true;
                        clearInterval(timer);
                        window.chrome.webview.postMessage(JSON.stringify({
                            type: 'challenge_solved',
                            url: window.location.href
                        }));
                    }
                } else {
                    stableChecks = 0;
                }
            }, 800);
        })();
        )JS";
        m_webview->ExecuteScript(script.c_str(), nullptr);
    }

    void InjectChaptersExtractorScript() {
        if (!m_webview) return;
        std::wstring c1 = LR"JS(
        (function() {
            let attempts = 0;
            let challengeWait = 0;
            let bestImages = [], stableImagePasses = 0;

            function isRealBotChallenge() {
                try {
                    if (document.querySelector('.wp-manga-chapter-img, #reader, .reading-content, #main-image, .img-chapter, .chapter-images, .viewer-cnt, .chapter-content, .listing-chapters_wrap, .wp-manga-chapter, ul.chapters, div.eph-num, .summary_content, .series-info, .entry-content img')) {
                        return false;
                    }
                    const imgs = document.querySelectorAll('img');
                    if (imgs && imgs.length > 3) return false;

                    const title = (document.title || '').trim().toLowerCase();
                    const bodyText = (document.body ? (document.body.innerText || document.body.textContent || '') : '').toLowerCase();

                    const hasCfWidget = !!document.querySelector(
                        'iframe[src*="challenges.cloudflare.com"], iframe[src*="turnstile"], ' +
                        '#challenge-stage, #turnstile-wrapper, .cf-turnstile, #cf-turnstile, ' +
                        '#challenge-form, div.cf-browser-verification, div.cf-captcha-container'
                    );
                    const hasCaptcha = !!document.querySelector(
                        'iframe[src*="google.com/recaptcha"], iframe[src*="recaptcha"], iframe[src*="hcaptcha"], .h-captcha'
                    );
                    const hasVerifyPhrase = bodyText.includes('verify you are human') ||
                                            bodyText.includes('verifying you are human') ||
                                            bodyText.includes('checking your browser before accessing') ||
                                            bodyText.includes('needs to review the security of your connection');
                    const isCfTitle = title.includes('just a moment...') || title.includes('just a moment') ||
                                      title.includes('attention required! | cloudflare') || title.includes('ddos-guard');

                    if (hasCaptcha) return true;
                    if (hasCfWidget && (hasVerifyPhrase || isCfTitle || (document.body && document.body.children.length < 15))) return true;
                    if (isCfTitle && hasVerifyPhrase) return true;
                } catch(e) {}
                return false;
            }

            async function tryExtractChapters() {
                attempts++;
                if (isRealBotChallenge()) {
                    challengeWait++;
                    // Arka planda kendiliginden gecen JS kontrolleri icin 2.4s bekle (ekran acilmadan cozulsun)
                    if (challengeWait < 3) {
                        setTimeout(tryExtractChapters, 800);
                        return;
                    }
                    // Kullanici tiklamasi gerektiren gercek dogrulama!
                    window.chrome.webview.postMessage(JSON.stringify({
                        type: 'challenge_detected',
                        url: window.location.href,
                        mode: 'chapters'
                    }));
                    return;
                }
                challengeWait = 0;
                try {
                    let chList = [];
                    let seen = new Set();

                    function addChapter(title, href) {
                        if (!href || typeof href !== 'string') return;
                        href = href.trim();
                        if (!href || href.startsWith('javascript:') || href === '#' || href.startsWith('data:')) return;
                        let u;
                        try {
                            u = new URL(href, window.location.href);
                            href = u.href;
                        } catch(e) { return; }

                        // Filter out external ad networks and tracking domains
                        const curHost = window.location.hostname.replace(/^www\./, '');
                        const linkHost = u.hostname.replace(/^www\./, '');
                        if (linkHost !== curHost && !linkHost.endsWith('.' + curHost) && !curHost.endsWith('.' + linkHost)) {
                            return;
                        }

                        const adPatterns = ['pushub', 'adsterra', 'monetag', 'doubleclick', 'onclick', 'click2', 'banner', 'track', 'analytics', 'disqus', 'facebook', 'twitter', 'discord'];
                        for (const p of adPatterns) {
                            if (u.hostname.includes(p) || u.pathname.includes(p)) return;
                        }
                        if (/\/(?:users?|groups?|profiles?|authors?|artists?|tags?|genres?|categor(?:y|ies)|login|register|search)(?:\/|$)/i.test(u.pathname)) return;

                        if (seen.has(href)) return;
                        title = (title || '').replace(/\s+/g, ' ').trim();
                        if (!title) {
                            const parts = href.split('/').filter(Boolean);
                            title = parts.length > 0 ? parts[parts.length - 1] : 'Bölüm';
                        }
                        seen.add(href);
                        chList.push({ title: title, url: href });
                    }
        )JS";

        std::wstring c2 = LR"JS(
                    // 1. Next.js __NEXT_DATA__
                    if (window.__NEXT_DATA__ && window.__NEXT_DATA__.props && window.__NEXT_DATA__.props.pageProps) {
                        const pp = window.__NEXT_DATA__.props.pageProps;
                        const cand = pp.chapters || (pp.manga && pp.manga.chapters) || (pp.series && pp.series.chapters) || (pp.data && pp.data.chapters);
                        if (Array.isArray(cand) && cand.length > 0) {
                            for (const c of cand) {
                                const t = c.title || c.name || (c.chapter ? ('Bölüm ' + c.chapter) : '');
                                const u = c.url || c.slug || c.id;
                                if (u) addChapter(t, u);
                            }
                        }
                    }

                    // Click expand / show more buttons if collapsed (e.g. MangaFox, Madara)
                    try {
                        const moreBtns = document.querySelectorAll('.detail-main-list-more, a[href*="show_more"], .show-more-chapters, #show-more, .btn-show-more, a.more-chapters');
                        for (const b of moreBtns) {
                            if (b && b.offsetParent !== null) b.click();
                        }
                    } catch(e) {}

                    // 2. DOM secicileri
                    if (chList.length === 0) {
                        const selectors = [
                            '.wp-manga-chapter a', 'li.wp-manga-chapter a', '#chapterlist a',
                            '.chapter-list a', '.chapters-list a', '.chp-item a', 'ul.cl-list a',
                            '.eph-num a', 'div.chap-item a', '.listing-chapters_wrap a',
                            '.detail-main-list a', '.detail-main-list-main a', 'ul.detail-main-list a',
                            'ul.detail-main-list li a', '.detail-main-list li a',
                            'a.chapter-name', 'a.chapter-link', 'ul.sub-chap-list a',
                            '.chapter a', '.chap-item a', '.episodes a', 'div[class*="chapter-item"] a',
                            'a[href*="/chapter"]', 'a[href*="/bolum"]', 'a[href*="/ch-"]', 'a[href*="/read/"]',
                            'a[href*="/c/"]', 'a[href*="/c-"]', 'a[href*="/chap-"]'
                        ];
                        if (/^(?:www\.)?sadscans\.net$/.test(location.hostname)) {
                            const slug = location.pathname.split('/').filter(Boolean)[1];
                            selectors.unshift('a[href*="/reader/' + slug + '/"]');
                        }

                        for (const sel of selectors) {
                            const nodes = document.querySelectorAll(sel);
                            if (nodes && nodes.length > 0) {
                                for (const a of nodes) {
                                    const label=a.querySelector('.chapter-name, .chapternum, .chapter-title');
                                    addChapter(label ? label.textContent : (a.title || a.textContent || a.innerText), a.href);
                                }
                                if (chList.length > 0) break;
                            }
                        }
                    }
        )JS";

        std::wstring c3 = LR"JS(
                    // 3. WordPress / Madara ajax/chapters/ fallback
                    if (chList.length === 0 && attempts >= 2) {
                        try {
                            const baseUrl = window.location.href.split('?')[0].replace(/\/+$/, '');
                            const ajaxUrl = baseUrl + '/ajax/chapters/';
                            const resp = await fetch(ajaxUrl, { method: 'POST' });
                            if (resp.ok) {
                                const h = await resp.text();
                                const doc = new DOMParser().parseFromString(h, 'text/html');
                                const nodes = doc.querySelectorAll('.wp-manga-chapter a, a[href*="/chapter"], a[href*="/bolum"], a[href*="/ch-"]');
                                for (const a of nodes) {
                                    addChapter(a.textContent || a.title || a.innerText, a.href);
                                }
                            }
                        } catch(e) {}
                    }

                    if (chList.length > 0 || attempts >= 10) {
                        let mangaTitle = document.title || '';
                        const h1 = document.querySelector('h1');
                        if (h1 && h1.textContent.trim().length > 0) mangaTitle = h1.textContent.trim();

                        window.chrome.webview.postMessage(JSON.stringify({
                            type: 'crawl_chapters_result',
                            success: chList.length > 0,
                            mangaTitle: mangaTitle,
                            chapters: chList,
                            error: chList.length === 0 ? 'Bölüm bulunamadı' : ''
                        }));
                    } else {
                        setTimeout(tryExtractChapters, 600);
                    }
                } catch(err) {
                    window.chrome.webview.postMessage(JSON.stringify({
                        type: 'crawl_chapters_result',
                        success: false,
                        chapters: [],
                        error: String(err)
                    }));
                }
            }
            setTimeout(tryExtractChapters, 400);
        })();
        )JS";

        std::wstring script = c1 + c2 + c3;
        m_webview->ExecuteScript(script.c_str(), nullptr);
    }

    void InjectImagesExtractorScript() {
        if (!m_webview) return;
        std::wstring s1a = LR"JS(
        (async function() {
            let attempts = 0;
            let challengeWait = 0;
            let bestImages = [], stableImagePasses = 0;

            function isRealBotChallenge() {
                try {
                    if (document.querySelector('.wp-manga-chapter-img, #reader, .reading-content, #main-image, .img-chapter, .chapter-images, .viewer-cnt, .chapter-content, .listing-chapters_wrap, .wp-manga-chapter, ul.chapters, div.eph-num, .summary_content, .series-info, .entry-content img')) {
                        return false;
                    }
                    const imgs = document.querySelectorAll('img');
                    if (imgs && imgs.length > 3) return false;

                    const title = (document.title || '').trim().toLowerCase();
                    const bodyText = (document.body ? (document.body.innerText || document.body.textContent || '') : '').toLowerCase();

                    const hasCfWidget = !!document.querySelector(
                        'iframe[src*="challenges.cloudflare.com"], iframe[src*="turnstile"], ' +
                        '#challenge-stage, #turnstile-wrapper, .cf-turnstile, #cf-turnstile, ' +
                        '#challenge-form, div.cf-browser-verification, div.cf-captcha-container'
                    );
                    const hasCaptcha = !!document.querySelector(
                        'iframe[src*="google.com/recaptcha"], iframe[src*="recaptcha"], iframe[src*="hcaptcha"], .h-captcha'
                    );
                    const hasVerifyPhrase = bodyText.includes('verify you are human') ||
                                            bodyText.includes('verifying you are human') ||
                                            bodyText.includes('checking your browser before accessing') ||
                                            bodyText.includes('needs to review the security of your connection');
                    const isCfTitle = title.includes('just a moment...') || title.includes('just a moment') ||
                                      title.includes('attention required! | cloudflare') || title.includes('ddos-guard');

                    if (hasCaptcha) return true;
                    if (hasCfWidget && (hasVerifyPhrase || isCfTitle || (document.body && document.body.children.length < 15))) return true;
                    if (isCfTitle && hasVerifyPhrase) return true;
                } catch(e) {}
                return false;
            }

            async function tryExtractImages() {
                attempts++;
                if (isRealBotChallenge()) {
                    challengeWait++;
                    // Arka planda kendiliginden gecen JS kontrolleri icin 2.4s bekle (ekran acilmadan cozulsun)
                    if (challengeWait < 3) {
                        setTimeout(tryExtractImages, 800);
                        return;
                    }
                    // Kullanici tiklamasi gerektiren gercek dogrulama!
                    window.chrome.webview.postMessage(JSON.stringify({
                        type: 'challenge_detected',
                        url: window.location.href,
                        mode: 'images'
                    }));
                    return;
                }
                challengeWait = 0;

                try {
                    // Lazy-loading tetiklemek icin tum sayfa boyunca kademeli asagi kaydir
                    try {
                        const totalH = Math.max(document.body.scrollHeight, document.documentElement.scrollHeight);
                        if (totalH > 800) {
                            for (let y = 0; y < totalH; y += 800) {
                                window.scrollTo(0, y);
                                await new Promise(r => setTimeout(r, 35));
                            }
                            window.scrollTo(0, totalH);
                            await new Promise(r => setTimeout(r, 120));
                        }
                    } catch(e) {}

                    let imgs = [], seen = new Set();
                    function addUrl(u) {
                        if (!u || typeof u !== 'string') return;
                        u = u.trim();
                        if (u.startsWith('//')) u = 'https:' + u;
                        if (!u || u.startsWith('data:image/svg') || u.startsWith('data:image/gif;base64,R0lGODlhAQABA') || u.length < 5) return;
                        const low = u.toLowerCase();
                        if (low.includes('blank.gif') || low.includes('pixel.gif') || low.includes('loading.') ||
                            low.includes('placeholder') || low.includes('spinner') || low.includes('1x1') ||
                            low.includes('avatar') || low.includes('logo') || low.includes('icon') ||
                            low.includes('banner') || low.includes('ad.') || low.includes('ad_') ||
                            low.includes('widget') || low.includes('badge') || low.includes('donate') ||
                            low.includes('discord') || low.includes('patreon') || low.includes('button')) return;
                        try { u = new URL(u, window.location.href).href; } catch(e) {}
                        if (!seen.has(u)) { seen.add(u); imgs.push(u); }
                    }
        )JS";

        std::wstring s1b = LR"JS(
                    // Noscript etiketlerindeki asil gorselleri ayikla
                    try {
                        const noscripts = document.querySelectorAll('noscript');
                        for (const ns of noscripts) {
                            const text = ns.textContent || '';
                            const m = text.match(/<img[^>]+src=["']([^"']+)["']/gi);
                            if (m) {
                                for (const tag of m) {
                                    const srcM = tag.match(/src=["']([^"']+)["']/i);
                                    if (srcM && srcM[1]) addUrl(srcM[1]);
                                }
                            }
                        }
                    } catch(e) {}

                    function extractFromSrcset(srcset) {
                        if (!srcset || typeof srcset !== 'string') return null;
                        const parts = srcset.split(',');
                        if (parts.length > 0) {
                            const last = parts[parts.length - 1].trim();
                            const urlMatch = last.split(/\s+/)[0];
                            if (urlMatch) return urlMatch;
                        }
                        return null;
                    }

                    const allModeSels = ['select.reading-mode', 'select[name="reading-mode"]', '#btn-read-all', '.reading-mode-all'];
                    for (const ams of allModeSels) {
                        try {
                            const el = document.querySelector(ams);
                            if (el && el.tagName.toLowerCase() === 'select') {
                                for (const opt of el.options) {
                                    const v = (opt.value || '').toLowerCase(), t = (opt.textContent || '').toLowerCase();
                                    if (v.includes('all') || v.includes('webtoon') || t.includes('tüm') || t.includes('all')) {
                                        if (el.value !== opt.value) { el.value = opt.value; el.dispatchEvent(new Event('change', { bubbles: true })); }
                                        break;
                                    }
                                }
                            } else if (el) { el.click(); }
                        } catch(e) {}
                    }
        )JS";

        std::wstring s2a = LR"JS(
                    if (window.ts_reader && window.ts_reader.params && window.ts_reader.params.sources) {
                        try { for (const s of window.ts_reader.params.sources) if (Array.isArray(s.images)) for (const img of s.images) addUrl(img); } catch(e) {}
                    }
                    if (window.newImgs && Array.isArray(window.newImgs)) {
                        for (const u of window.newImgs) addUrl(u);
                    }
                    if (window.imageArray && Array.isArray(window.imageArray)) {
                        for (const u of window.imageArray) addUrl(u);
                    }
                    if (imgs.length === 0 && window.pages && Array.isArray(window.pages)) {
                        for (const p of window.pages) { if (typeof p === 'string') addUrl(p); else if (p && (p.page_image || p.url)) addUrl(p.page_image || p.url); }
                    }
                    if (imgs.length === 0 && window.chapter_images && Array.isArray(window.chapter_images)) for (const u of window.chapter_images) addUrl(u);
                    if (imgs.length === 0 && window.images && Array.isArray(window.images)) for (const u of window.images) addUrl(u);

                    // Dean Edwards packer / regex unpacker (FanFox, MangaHere, vs.)
                    if (imgs.length === 0) {
                        try {
                            const scripts = document.querySelectorAll('script');
                            for (const s of scripts) {
                                const code = s.textContent || '';
                                if (code.includes('function(p,a,c,k,e,d)')) {
                                    try {
                                        const idx = code.indexOf('eval(function(p,a,c,k,e,d)');
                                        if (idx !== -1) {
                                            const sub = code.substring(idx + 4);
                                            const unpacked = (new Function('return ' + sub))();
                                            if (typeof unpacked === 'string') {
                                                const match = unpacked.match(/newImgs\s*=\s*\[([^\]]+)\]/);
                                                if (match) {
                                                    const rawUrls = match[1].split(',');
                                                    for (let ru of rawUrls) {
                                                        ru = ru.trim().replace(/^['"]|['"]$/g, '');
                                                        addUrl(ru);
                                                    }
                                                }
                                            }
                                        }
                                    } catch(e) {}
                                }
                                if (code.includes('newImgs')) {
                                    const match = code.match(/newImgs\s*=\s*\[([^\]]+)\]/);
                                    if (match) {
                                        const rawUrls = match[1].split(',');
                                        for (let ru of rawUrls) {
                                            ru = ru.trim().replace(/^['"]|['"]$/g, '');
                                            addUrl(ru);
                                        }
                                    }
                                }
                            }
                        } catch(e) {}
                    }
        )JS";

        std::wstring s2b = LR"JS(
                    // FanFox / MangaHere in-browser chapterfun.ashx fallback
                    if (imgs.length === 0 && (window.location.hostname.includes('fanfox.net') || window.location.hostname.includes('mangahere.cc'))) {
                        try {
                            const html = document.documentElement.innerHTML;
                            const cidM = html.match(/chapterid\s*=\s*(\d+)/i);
                            const imgCountM = html.match(/imagecount\s*=\s*(\d+)/i);
                            if (cidM && imgCountM) {
                                const cid = cidM[1];
                                const total = parseInt(imgCountM[1]);
                                let key = (document.querySelector('#dm5_key') ? document.querySelector('#dm5_key').value : '') || window.guidkey || '';
                                if (!key) {
                                    const km = html.match(/guidkey\s*=\s*['"]([^'"]+)['"]/);
                                    if (km) key = km[1];
                                }
                                const curl = window.location.href.split('?')[0].replace(/\d+\.html$/, '');
                                for (let p = 1; p <= total; p++) {
                                    if (imgs.length >= total) break;
                                    try {
                                        const cfUrl = `${curl}chapterfun.ashx?cid=${cid}&page=${p}&key=${key}`;
                                        const resp = await fetch(cfUrl, { headers: { 'X-Requested-With': 'XMLHttpRequest' } });
                                        if (resp.ok) {
                                            const txt = await resp.text();
                                            if (txt.includes('eval(function')) {
                                                const sub = txt.trim().substring(4);
                                                const unp = (new Function('return ' + sub))();
                                                const pixM = unp.match(/var pix\s*=\s*"([^"]+)"/);
                                                const pvalM = unp.match(/var pvalue\s*=\s*\[([^\]]+)\]/);
                                                if (pvalM) {
                                                    const pix = pixM ? pixM[1] : '';
                                                    const raw = pvalM[1].split(',').map(s => s.trim().replace(/^['"]|['"]$/g, ''));
                                                    for (const it of raw) {
                                                        addUrl(it.startsWith('http') || it.startsWith('//') ? it : (pix + it));
                                                    }
                                                }
                                            }
                                        }
                                    } catch(e) {}
                                }
                            }
                        } catch(e) {}
                    }

                    // Manga4Life / MangaSee Vue.js extraction
                    if (imgs.length === 0 && window.vm && window.vm.CurChapter && Array.isArray(window.vm.CurChapter.Pages)) {
                        try {
                            const vm = window.vm;
                            const curPath = vm.CurPathName || '';
                            const idxName = vm.IndexName || '';
                            const ch = vm.CurChapter;
                            const dir = ch.Directory ? ch.Directory + '/' : '';
                            const chNum = ch.Chapter.substring(1, 5);
                            const subCh = ch.Chapter.substring(5);
                            for (let p = 1; p <= ch.Pages; p++) {
                                const pageStr = String(p).padStart(3, '0');
                                const u = `https://${curPath}/manga/${idxName}/${dir}${chNum}-${subCh}/${pageStr}.png`;
                                addUrl(u);
                            }
                        } catch(e) {}
                    }
        )JS";

        std::wstring s2c = LR"JS(
                    if (imgs.length === 0) {
                        const containers = [
                            '.reader-main img', 'img.reader-main-img', '#image', '#viewer img', '.comic_wrap img',
                            '#readerarea img', '#readerarea p img', '.reading-content img', '.entry-content img',
                            '.chapter-content img', '.page-break img', '.chapter-image img',
                            '.img-chapter img', '.wp-manga-chapter-img', '.chapter-body img',
                            '.container-chapter-reader img', '#chapter-images img',
                            'div[id*="chapter"] img', 'div[class*="chapter"] img', 'div[class*="reader"] img',
                            '.comic-images img', '.manga-images img', '#images-container img',
                            '.ts-main-image', '.viewer-cnt img', '.read-content img', '#image-container img',
                            '#all img', '.chapter-container img'
                        ];
                        let nodes = [];
                        for (const sel of containers) {
                            const list = document.querySelectorAll(sel);
                            if (list && list.length >= 2) { nodes = Array.from(list); break; }
                        }
                        if (nodes.length === 0) {
                            const allImg = Array.from(document.querySelectorAll('img'));
                            if (allImg.length >= 2) nodes = allImg;
                        }
                        for (const img of nodes) {
                            let src = img.getAttribute('data-src') || img.getAttribute('data-lazy-src') ||
                                      img.getAttribute('data-original') || img.getAttribute('data-url') ||
                                      img.getAttribute('data-echo') || img.getAttribute('data-img') ||
                                      img.getAttribute('data-page') || img.getAttribute('data-cfsrc') ||
                                      img.getAttribute('data-cdn-src') || img.getAttribute('data-full-url') ||
                                      img.getAttribute('data-origin') || img.getAttribute('data-fallback-src') ||
                                      extractFromSrcset(img.getAttribute('data-srcset')) ||
                                      extractFromSrcset(img.getAttribute('srcset')) ||
                                      img.currentSrc || img.src;
                            addUrl(src);
                        }
                    }
                    if (imgs.length === 0 && window.lstImages && Array.isArray(window.lstImages)) for (const u of window.lstImages) addUrl(u);
                    if (imgs.length === 0 && window.manga_images && Array.isArray(window.manga_images)) for (const u of window.manga_images) addUrl(u);
        )JS";

        std::wstring s3 = LR"JS(
                    if (imgs.length < 2) {
                        const pageSel = document.querySelector('select.page-select, select#page, select[name="page"], select.page-selector, select.page_select, .page-select select');
                        if (pageSel && pageSel.options && pageSel.options.length > 1) {
                            let pageUrls = [];
                            for (const opt of pageSel.options) {
                                if (opt.value) {
                                    try { const fullU = new URL(opt.value, window.location.href).href; if (!pageUrls.includes(fullU)) pageUrls.push(fullU); } catch(e) {}
                                }
                            }
                            if (pageUrls.length > 1) {
                                const curImg = document.querySelector('#main-image, #image-0, .viewer img, #reader img, .read-content img, img#image');
                                if (curImg) {
                                    let src = curImg.getAttribute('data-src') || curImg.getAttribute('data-lazy-src') || curImg.getAttribute('data-original') || curImg.currentSrc || curImg.src;
                                    addUrl(src);
                                }
                                const otherUrls = pageUrls.filter(u => u !== window.location.href);
                                for (let i = 0; i < otherUrls.length; i += 5) {
                                    const chunk = otherUrls.slice(i, i + 5);
                                    await Promise.all(chunk.map(async (pUrl) => {
                                        try {
                                            const resp = await fetch(pUrl);
                                            const html = await resp.text();
                                            const doc = new DOMParser().parseFromString(html, 'text/html');
                                            const pImg = doc.querySelector('#main-image, #image-0, .viewer img, #reader img, .read-content img, img#image');
                                            if (pImg) {
                                                let s = pImg.getAttribute('data-src') || pImg.getAttribute('data-lazy-src') || pImg.getAttribute('data-original') || pImg.src;
                                                addUrl(s);
                                            }
                                        } catch(e) {}
                                    }));
                                }
                            }
                        }
                    }

                    if (imgs.length > bestImages.length) { bestImages = imgs.slice(); stableImagePasses = 0; }
                    else if (imgs.length === bestImages.length && imgs.length > 0) ++stableImagePasses;
                    if ((bestImages.length > 2 && stableImagePasses >= 2) || attempts >= 15) {
                        imgs = bestImages;
                        window.chrome.webview.postMessage(JSON.stringify({
                            type: 'crawl_images_result',
                            success: imgs.length > 0,
                            images: imgs,
                            url: window.location.href,
                            cookie: document.cookie || '',
                            userAgent: navigator.userAgent || '',
                            error: imgs.length === 0 ? 'Bölüm görselleri bulunamadı' : ''
                        }));
                    } else {
                        setTimeout(tryExtractImages, 500);
                    }
                } catch(err) {
                    window.chrome.webview.postMessage(JSON.stringify({
                        type: 'crawl_images_result',
                        success: false,
                        images: [],
                        error: String(err)
                    }));
                }
            }
            setTimeout(tryExtractImages, 500);
        })();
        )JS";

        std::wstring script = s1a + s1b + s2a + s2b + s2c + s3;
        m_webview->ExecuteScript(script.c_str(), nullptr);
    }

    void StartDownloadThreads(int tabIdx, const std::vector<std::string>& images, const std::string& pageUrl,
                              const std::string& cookieStr, const std::string& userAgentStr,
                              const std::wstring& cacheDir, const std::wstring& mTitle, const std::wstring& cTitle, BrowserHost* bHost,
                              std::shared_ptr<browser::BrowserImageBuffer> browserImages) {
        std::thread([tabIdx, images, pageUrl, cookieStr, userAgentStr, cacheDir, mTitle, cTitle, bHost, browserImages]() {
            paths::EnsureDir(cacheDir);
            int total = (int)images.size();
            std::atomic<size_t> nextIndex{ 0 };
            std::atomic<int> successCount{ 0 };

            const unsigned int numWorkers = 4;
            std::vector<std::thread> workers;
            workers.reserve(numWorkers);

            for (unsigned int w = 0; w < numWorkers; ++w) {
                workers.emplace_back([&, w]() {
                    while (true) {
                        size_t i = nextIndex.fetch_add(1);
                        if (i >= images.size()) break;

                        const std::string& imgUrl = images[i];
                        std::string ext = ".jpg";
                        size_t qPos = imgUrl.find('?');
                        std::string pOnly = (qPos != std::string::npos) ? imgUrl.substr(0, qPos) : imgUrl;
                        size_t dotPos = pOnly.rfind('.');
                        if (dotPos != std::string::npos && dotPos + 1 < pOnly.size()) {
                            std::string cand = pOnly.substr(dotPos);
                            for (char& c : cand) c = (char)tolower((unsigned char)c);
                            if (cand == ".png" || cand == ".webp" || cand == ".jpeg" || cand == ".jpg" || cand == ".avif") {
                                ext = (cand == ".jpeg") ? ".jpg" : cand;
                            }
                        }

                        wchar_t fileName[64];
                        swprintf_s(fileName, L"%04d%s", (int)i + 1, FromUtf8(ext).c_str());
                        std::wstring outPath = cacheDir + L"\\" + fileName;

                        // Dosya zaten diskte varsa ve geçerli bir resimse doğrudan kullan
                        if (IsValidImageFile(outPath)) {
                            successCount.fetch_add(1);
                            if (bHost) {
                                bHost->OnCrawlerPageReady(tabIdx, (int)i, total, outPath);
                            }
                            continue;
                        } else {
                            DeleteFileW(outPath.c_str());
                        }

                        net::HttpRequest req;
                        req.url = imgUrl;
                        req.timeoutMs = 25000;
                        if (!userAgentStr.empty()) req.headers.push_back({ "User-Agent", userAgentStr });
                        if (!cookieStr.empty()) req.headers.push_back({ "Cookie", cookieStr });
                        if (!pageUrl.empty()) req.headers.push_back({ "Referer", pageUrl });
                        req.headers.push_back({ "Accept", "image/avif,image/webp,image/apng,image/*,*/*;q=0.8" });
                        req.headers.push_back({ "Sec-Fetch-Dest", "image" });
                        req.headers.push_back({ "Sec-Fetch-Mode", "no-cors" });
                        req.headers.push_back({ "Sec-Fetch-Site", "cross-site" });

                        // 3 denemeli indirme (backoff ve resim geçerlilik kontrolü)
                        net::HttpResponse resp;
                        for (int attempt = 0; attempt < 4; ++attempt) {
                            if (attempt > 0) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(attempt * 400));
                            }
                            resp={};
                            if(browserImages) {
                                resp.body=browserImages->Take(imgUrl);
                                resp.success=!resp.body.empty();
                            }
                            if(!resp.success) resp = net::Fetch(req, 0);
                            if (resp.success && !resp.body.empty() && IsValidImageBuffer(resp.body)) {
                                break;
                            }
                        }

                        if (resp.success && !resp.body.empty() && IsValidImageBuffer(resp.body)) {
                            std::wstring tmpPath = outPath + L"." + std::to_wstring(GetCurrentThreadId()) + L".tmp";
                            if (fileio::WriteAtomic(tmpPath, resp.body)) {
                                if (!MoveFileExW(tmpPath.c_str(), outPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {DeleteFileW(tmpPath.c_str());continue;}
                                successCount.fetch_add(1);
                                if (bHost) {
                                    bHost->OnCrawlerPageReady(tabIdx, (int)i, total, outPath);
                                }
                            }
                        }
                    }
                });
            }

            for (auto& t : workers) {
                if (t.joinable()) t.join();
            }

            if (successCount.load() > 0) {
                std::wstring completeMarker = cacheDir + L"\\.complete";
                if (successCount.load() == total) fileio::WriteAtomic(completeMarker, std::to_string(total));
                else DeleteFileW(completeMarker.c_str());
                std::wstring tsFile = cacheDir + L"\\.timestamp";
                int64_t nowSec = (int64_t)time(nullptr);
                fileio::WriteAtomic(tsFile, std::to_string(nowSec));
                if (bHost) {
                    bHost->OnCrawlerChapterReady(tabIdx, cacheDir, mTitle, cTitle);
                    if(successCount.load()<total) bHost->Toast(i18n::Literal(L"Bazı sayfalar yeniden denemelere rağmen indirilemedi. Bölüm tamamlanmış sayılmadı."));
                }
            }
        }).detach();
    }

    void HandleWebMessage(const std::wstring& msg) {
        std::string rawJson = ToUtf8(msg);
        std::string unquoted;
        if (rawJson.size() >= 2 && rawJson.front() == '"' && rawJson.back() == '"') {
            for (size_t i = 1; i + 1 < rawJson.size(); ++i) {
                if (rawJson[i] == '\\' && i + 1 < rawJson.size() - 1) {
                    char n = rawJson[i + 1];
                    if (n == '"') { unquoted += '"'; i++; }
                    else if (n == '\\') { unquoted += '\\'; i++; }
                    else if (n == 'n') { unquoted += '\n'; i++; }
                    else if (n == 'r') { unquoted += '\r'; i++; }
                    else if (n == 't') { unquoted += '\t'; i++; }
                    else { unquoted += rawJson[i]; }
                } else {
                    unquoted += rawJson[i];
                }
            }
        } else {
            unquoted = rawJson;
        }

        json::Value root;
        std::string parseErr;
        if (!json::Parse(unquoted, root, &parseErr) || !root.IsObject()) {
            return;
        }

        std::string type = root.Get("type") ? root.Get("type")->AsString() : "";

        if (type == "challenge_detected") {
            std::string url = root.Get("url") ? root.Get("url")->AsString() : m_crawlChapterUrl;
            std::string mode = root.Get("mode") ? root.Get("mode")->AsString() : (m_crawlMode == CrawlMode::Images ? "images" : "chapters");
            if (m_host) {
                m_host->ShowChallengeVerification(url, m_crawlMangaTitle, mode);
            }
            return;
        }

        if (type == "challenge_solved") {
            m_challengeModeActive = false;
            if (m_host) {
                m_host->OnChallengeSolved();
            }
            return;
        }

        if (type == "crawl_chapters_result") {
            if (!m_chapterCrawlCb) return;
            auto cb = m_chapterCrawlCb;
            m_chapterCrawlCb = nullptr;
            m_crawlMode = CrawlMode::None;

            ChapterCrawlResult res;
            res.success = root.Get("success") ? root.Get("success")->AsBool(false) : false;
            res.mangaTitle = root.Get("mangaTitle") ? root.Get("mangaTitle")->AsString() : "";
            res.errorMessage = root.Get("error") ? root.Get("error")->AsString() : "";

            if (const auto* arr = root.Get("chapters")) {
                if (arr->IsArray()) {
                    for (const auto& it : arr->arr) {
                        if (it.IsObject()) {
                            std::string t = it.Get("title") ? it.Get("title")->AsString() : "";
                            std::string u = it.Get("url") ? it.Get("url")->AsString() : "";
                            if (IsChapterCandidateUrl(u)) res.chapters.push_back({ t, u });
                        }
                    }
                }
            }
            res.success=res.success && !res.chapters.empty();
            if(!res.success && res.errorMessage.empty()) res.errorMessage="Geçerli bölüm bağlantısı bulunamadı";
            cb(res);
            return;
        }

        if (type == "crawl_images_result") {
            if (!m_imageCrawlCb) return;
            auto cb = m_imageCrawlCb;
            m_imageCrawlCb = nullptr;
            m_crawlMode = CrawlMode::None;
            auto browserImages=std::move(m_browserImages);

            bool success = root.Get("success") ? root.Get("success")->AsBool(false) : false;
            std::vector<std::string> images;
            if (const auto* arr = root.Get("images")) {
                if (arr->IsArray()) {
                    for (const auto& it : arr->arr) {
                        if (!it.AsString().empty()) images.push_back(it.AsString());
                    }
                }
            }

            if (!success || images.empty()) {
                ImageCrawlResult res;
                res.success = false;
                res.mangaTitle = m_crawlMangaTitle;
                res.chapterTitle = m_crawlChapterTitle;
                res.errorMessage = root.Get("error") ? root.Get("error")->AsString() : "Bölüm görselleri bulunamadı";
                cb(res);
                return;
            }

            std::string pageUrl = root.Get("url") ? root.Get("url")->AsString() : m_crawlChapterUrl;
            std::string fallbackCookie = root.Get("cookie") ? root.Get("cookie")->AsString() : "";
            std::string userAgentStr = root.Get("userAgent") ? root.Get("userAgent")->AsString() : "";
            std::wstring mTitle = m_crawlMangaTitle;
            std::wstring cTitle = m_crawlChapterTitle;

            std::wstringstream ss;
            ss << std::hex << std::setfill(L'0') << std::setw(16) << HashUrl(pageUrl);
            std::wstring cacheDir = paths::DataDir() + L"\\cache\\browser_" + ss.str();

            ImageCrawlResult res;
            res.success = true;
            res.cacheDir = cacheDir;
            res.mangaTitle = mTitle;
            res.chapterTitle = cTitle;
            res.pageCount = (int)images.size();
            cb(res);

            BrowserHost* bHost = m_host;
            int tabIdx = m_crawlTabIndex;

            // Tum cookieleri (HttpOnly cf_clearance dahil) ICoreWebView2CookieManager'dan cek
            ComPtr<ICoreWebView2_2> wv2;
            if (m_webview && SUCCEEDED(m_webview.As(&wv2)) && wv2) {
                ComPtr<ICoreWebView2CookieManager> cookieMgr;
                if (SUCCEEDED(wv2->get_CookieManager(&cookieMgr)) && cookieMgr) {
                    std::wstring pageUrlW = FromUtf8(pageUrl);
                    cookieMgr->GetCookies(pageUrlW.c_str(),
                        Callback<ICoreWebView2GetCookiesCompletedHandler>(
                            [this, tabIdx, images, pageUrl, userAgentStr, fallbackCookie, cacheDir, mTitle, cTitle, bHost, browserImages](HRESULT hr, ICoreWebView2CookieList* list) -> HRESULT {
                                std::string fullCookies;
                                if (SUCCEEDED(hr) && list) {
                                    UINT count = 0;
                                    list->get_Count(&count);
                                    for (UINT i = 0; i < count; ++i) {
                                        ComPtr<ICoreWebView2Cookie> c;
                                        if (SUCCEEDED(list->GetValueAtIndex(i, &c)) && c) {
                                            LPWSTR name = nullptr, val = nullptr, dom = nullptr, pth = nullptr;
                                            c->get_Name(&name);
                                            c->get_Value(&val);
                                            c->get_Domain(&dom);
                                            c->get_Path(&pth);
                                            if (name && val) {
                                                if (!fullCookies.empty()) fullCookies += "; ";
                                                fullCookies += ToUtf8(name) + "=" + ToUtf8(val);

                                                net::Cookie ck;
                                                ck.name = ToUtf8(name);
                                                ck.value = ToUtf8(val);
                                                if (dom) ck.domain = ToUtf8(dom);
                                                if (pth) ck.path = ToUtf8(pth);
                                                net::Fetcher::Instance().Jar().Set(ck);
                                            }
                                            if (name) CoTaskMemFree(name);
                                            if (val) CoTaskMemFree(val);
                                            if (dom) CoTaskMemFree(dom);
                                            if (pth) CoTaskMemFree(pth);
                                        }
                                    }
                                }
                                std::string finalCookies = !fullCookies.empty() ? fullCookies : fallbackCookie;
                                StartDownloadThreads(tabIdx, images, pageUrl, finalCookies, userAgentStr, cacheDir, mTitle, cTitle, bHost, browserImages);
                                return S_OK;
                            }
                        ).Get()
                    );
                    return;
                }
            }

            StartDownloadThreads(tabIdx, images, pageUrl, fallbackCookie, userAgentStr, cacheDir, mTitle, cTitle, bHost, browserImages);
            return;
        }
    }

    void CrawlMangaChapters(const std::string& mangaUrl, std::function<void(const ChapterCrawlResult&)> onComplete) {
        // An empty MangaDex language feed is authoritative. Its HTML mixes
        // languages, uploaders and external publishers, so do not scrape it.
        if(IsMangaDexSource(mangaUrl)) {
            ChapterCrawlResult result;result.success=false;
            result.errorMessage="MangaDex bölüm listesi kayıtlı dil için API üzerinden alınmalıdır";
            if(onComplete) onComplete(result);
            return;
        }
        if (m_parent && GetCurrentThreadId() != GetWindowThreadProcessId(m_parent, nullptr)) {
            if (m_host) {
                m_host->RunOnUIThread([this, mangaUrl, onComplete]() {
                    CrawlMangaChapters(mangaUrl, onComplete);
                });
            }
            return;
        }

        std::string safeMangaUrl = net::NormalizeMirrorUrl(mangaUrl);
        m_chapterCrawlCb = onComplete;
        m_crawlMangaUrl = safeMangaUrl;
        m_crawlMode = CrawlMode::Chapters;
        m_browserImages.reset();

        if (!m_ready || !m_webview) {
            m_pendingMangaCrawlUrl = safeMangaUrl;
            if (!m_initStarted && m_parent) {
                Init(m_parent, m_host, m_view);
            }
            return;
        }

        m_webview->Navigate(FromUtf8(safeMangaUrl).c_str());
    }

    void CrawlChapterImages(int tabIndex, const std::string& chapterUrl, const std::wstring& mangaTitle, const std::wstring& chapterTitle, std::function<void(const ImageCrawlResult&)> onComplete) {
        if (m_parent && GetCurrentThreadId() != GetWindowThreadProcessId(m_parent, nullptr)) {
            if (m_host) {
                m_host->RunOnUIThread([this, tabIndex, chapterUrl, mangaTitle, chapterTitle, onComplete]() {
                    CrawlChapterImages(tabIndex, chapterUrl, mangaTitle, chapterTitle, onComplete);
                });
            }
            return;
        }

        std::string safeChapterUrl = net::NormalizeMirrorUrl(chapterUrl);
        m_imageCrawlCb = onComplete;
        m_crawlTabIndex = tabIndex;
        m_crawlChapterUrl = safeChapterUrl;
        m_crawlMangaTitle = mangaTitle;
        m_crawlChapterTitle = chapterTitle;
        m_crawlMode = CrawlMode::Images;
        const bool kawaii=safeChapterUrl.rfind("https://www.mangakawaii.fr/",0)==0 || safeChapterUrl.rfind("https://mangakawaii.fr/",0)==0;
        m_browserImages=kawaii?std::make_shared<browser::BrowserImageBuffer>():nullptr;

        if (!m_ready || !m_webview) {
            m_pendingImageTabIndex = tabIndex;
            m_pendingImageCrawlUrl = safeChapterUrl;
            m_pendingImageMangaTitle = mangaTitle;
            m_pendingImageChapterTitle = chapterTitle;
            if (!m_initStarted && m_parent) {
                Init(m_parent, m_host, m_view);
            }
            return;
        }

        m_webview->Navigate(FromUtf8(safeChapterUrl).c_str());
    }

    void FinishChallengeVerification() {
        m_challengeModeActive = false;
        if (m_controller) {
            m_controller->put_IsVisible(FALSE);
        }
        if (m_crawlMode == CrawlMode::Chapters) {
            InjectChaptersExtractorScript();
        } else if (m_crawlMode == CrawlMode::Images) {
            InjectImagesExtractorScript();
        }
    }

    void SyncCookiesToJar(const std::string& targetUrl = "") {
        ComPtr<ICoreWebView2_2> wv2;
        if (!m_webview || FAILED(m_webview.As(&wv2)) || !wv2) return;
        ComPtr<ICoreWebView2CookieManager> cookieMgr;
        if (FAILED(wv2->get_CookieManager(&cookieMgr)) || !cookieMgr) return;

        std::wstring pageUrlW;
        if (!targetUrl.empty()) {
            pageUrlW = FromUtf8(targetUrl);
        } else {
            PWSTR curSource = nullptr;
            if (SUCCEEDED(m_webview->get_Source(&curSource)) && curSource) {
                pageUrlW = curSource;
                CoTaskMemFree(curSource);
            }
        }
        if (pageUrlW.empty()) return;

        cookieMgr->GetCookies(pageUrlW.c_str(),
            Callback<ICoreWebView2GetCookiesCompletedHandler>(
                [](HRESULT hr, ICoreWebView2CookieList* list) -> HRESULT {
                    if (SUCCEEDED(hr) && list) {
                        UINT count = 0;
                        list->get_Count(&count);
                        for (UINT i = 0; i < count; ++i) {
                            ComPtr<ICoreWebView2Cookie> c;
                            if (SUCCEEDED(list->GetValueAtIndex(i, &c)) && c) {
                                LPWSTR name = nullptr, val = nullptr, dom = nullptr, pth = nullptr;
                                c->get_Name(&name);
                                c->get_Value(&val);
                                c->get_Domain(&dom);
                                c->get_Path(&pth);
                                if (name && val) {
                                    net::Cookie ck;
                                    ck.name = ToUtf8(name);
                                    ck.value = ToUtf8(val);
                                    if (dom) ck.domain = ToUtf8(dom);
                                    if (pth) ck.path = ToUtf8(pth);
                                    net::Fetcher::Instance().Jar().Set(ck);
                                }
                                if (name) CoTaskMemFree(name);
                                if (val) CoTaskMemFree(val);
                                if (dom) CoTaskMemFree(dom);
                                if (pth) CoTaskMemFree(pth);
                            }
                        }
                    }
                    return S_OK;
                }
            ).Get()
        );
    }

    void Shutdown() {
        m_browserImages.reset();
        if (m_controller) {
            m_controller->Close();
            m_controller.Reset();
        }
        m_webview.Reset();
        m_env.Reset();
        m_ready = false;
        m_initStarted = false;
    }
};

BrowserView::BrowserView() : m_impl(std::make_unique<Impl>()) {}
BrowserView::~BrowserView() { Shutdown(); }

bool BrowserView::Init(HWND parentHwnd, BrowserHost* host) {
    m_parent = parentHwnd;
    m_host = host;
    return m_impl->Init(parentHwnd, host, this);
}

void BrowserView::Shutdown() {
    if (m_impl) m_impl->Shutdown();
}

void BrowserView::Navigate(const std::wstring& url, const std::wstring& mangaTitle) {
    if (m_parent && GetCurrentThreadId() != GetWindowThreadProcessId(m_parent, nullptr)) {
        if (m_host) {
            m_host->RunOnUIThread([this, url, mangaTitle]() {
                Navigate(url, mangaTitle);
            });
        }
        return;
    }

    std::wstring clean = url;
    size_t f = clean.find_first_not_of(L" \t\r\n");
    if (f != std::wstring::npos) {
        size_t l = clean.find_last_not_of(L" \t\r\n");
        clean = clean.substr(f, l - f + 1);
    }
    if (clean.empty() || clean == L"__FIRST__" || clean == L"__LAST__") {
        return;
    }

    clean = FromUtf8(net::NormalizeMirrorUrl(ToUtf8(clean)));

    m_currentUrl = clean;
    m_urlInput.SetText(clean);
    if (!m_mangaTitle.empty() || !mangaTitle.empty()) {
        m_mangaTitle = mangaTitle;
    }

    if (m_impl->m_ready && m_impl->m_webview) {
        PWSTR curSource = nullptr;
        std::wstring current;
        if (SUCCEEDED(m_impl->m_webview->get_Source(&curSource)) && curSource) {
            current = curSource;
            CoTaskMemFree(curSource);
        }
        if (current == clean) {
            m_impl->m_webview->Reload();
        } else {
            m_impl->m_webview->Navigate(clean.c_str());
        }
    } else {
        m_impl->m_pendingUrl = clean;
        if (!m_impl->m_initStarted && m_parent) {
            m_impl->Init(m_parent, m_host, this);
        }
    }
}

void BrowserView::Show(bool visible) {
    if (m_parent && GetCurrentThreadId() != GetWindowThreadProcessId(m_parent, nullptr)) {
        if (m_host) {
            m_host->RunOnUIThread([this, visible]() {
                Show(visible);
            });
        }
        return;
    }

    m_visible = visible;
    if (m_impl && m_impl->m_controller) {
        m_impl->m_controller->put_IsVisible(visible ? TRUE : FALSE);
    }
}

void BrowserView::UpdateBounds(const D2D1_RECT_F& area) {
    m_lastBounds = area;
    if (!m_impl || !m_impl->m_controller) return;
    RECT rc;
    rc.left = (LONG)std::round(area.left);
    rc.top = (LONG)std::round(area.top);
    rc.right = (LONG)std::round(area.right);
    rc.bottom = (LONG)std::round(area.bottom);
    m_impl->m_controller->put_Bounds(rc);
}

void BrowserView::ApplyLastBounds() {
    if (m_lastBounds.right > m_lastBounds.left && m_lastBounds.bottom > m_lastBounds.top) {
        UpdateBounds(m_lastBounds);
    }
}

void BrowserView::NavigateChapterInBrowser(int delta) {
    if (!m_impl || !m_impl->m_webview) return;
    std::wstring script = LR"JS(
    (function(dir) {
        const nextSelectors = [
            'a.next_page', 'a.next-page', 'a.next', 'a[rel="next"]', '.nav-next a', '.next-post a',
            '.ch-next a', 'a.ch-next', '.btn-next', 'a.btn-next', '.navi-change-chapter-btn-next'
        ];
        const prevSelectors = [
            'a.prev_page', 'a.prev-page', 'a.prev', 'a[rel="prev"]', '.nav-previous a', '.prev-post a',
            '.ch-prev a', 'a.ch-prev', '.btn-prev', 'a.btn-prev', '.navi-change-chapter-btn-prev'
        ];
        const selectors = dir > 0 ? nextSelectors : prevSelectors;
        for (const s of selectors) {
            try {
                const el = document.querySelector(s);
                if (el && el.href) { el.click(); return el.href; }
            } catch(e) {}
        }
        const keywords = dir > 0 ? ['sonraki', 'next', 'ileri', '»'] : ['önceki', 'prev', 'geri', '«'];
        for (const a of document.querySelectorAll('a')) {
            const text = (a.textContent || '').trim().toLowerCase();
            for (const kw of keywords) {
                if (text === kw || text.includes(kw + ' bölüm') || text.includes('bölüm ' + kw)) {
                    a.click();
                    return a.href;
                }
            }
        }
        return '';
    })()JS" + std::to_wstring(delta) + L");";

    m_impl->m_webview->ExecuteScript(script.c_str(), nullptr);
}

void BrowserView::DrawBar(Renderer& r, Ui& ui, const D2D1_RECT_F& bar, double now) {
    (void)now;
    const float s = r.Scale();
    m_lastScale = s;
    w::SetScale(s);

    r.Fill(bar, theme::Panel, 1.0f);
    r.Line(bar.left, bar.bottom - 0.5f, bar.right, bar.bottom - 0.5f, theme::Border, 1.0f, 0.30f);

    float x = bar.left + 8.0f * s;
    const float btnH = 30.0f * s;
    const float btnW = 34.0f * s;
    const float y = bar.top + (RcH(bar) - btnH) * 0.5f;

    // Geri butonu
    if (w::IconButton(r, ui, Id("wv.back"), RcWH(x, y, btnW, btnH), Icon::ChevronLeft, 18.0f * s, i18n::Literal(L"Geri"))) {
        if (m_impl && m_impl->m_webview) m_impl->m_webview->GoBack();
    }
    x += btnW + 4.0f * s;

    // Ileri butonu
    if (w::IconButton(r, ui, Id("wv.fwd"), RcWH(x, y, btnW, btnH), Icon::ChevronRight, 18.0f * s, i18n::Literal(L"İleri"))) {
        if (m_impl && m_impl->m_webview) m_impl->m_webview->GoForward();
    }
    x += btnW + 4.0f * s;

    // Yenile butonu
    if (w::TextButton(r, ui, Id("wv.refresh"), RcWH(x, y, btnW, btnH), L"↻", false)) {
        if (m_impl && m_impl->m_webview) m_impl->m_webview->Reload();
    }
    ui.Tip(Id("wv.refresh"), i18n::Literal(L"Yenile"), RcWH(x, y, btnW, btnH));
    x += btnW + 12.0f * s;

    // Sagdaki butonlar: Kapat ve "GhostManga'da Oku"
    const float closeW = 34.0f * s;
    const float amrBtnW = 200.0f * s;
    const D2D1_RECT_F closeRc = RcWH(bar.right - closeW - 8.0f * s, y, closeW, btnH);
    const D2D1_RECT_F amrRc = RcWH(closeRc.left - amrBtnW - 8.0f * s, y, amrBtnW, btnH);

    // Kapat butonu
    if (w::IconButton(r, ui, Id("wv.close"), closeRc, Icon::Close, 16.0f * s, i18n::Literal(L"Tarayıcıyı Kapat (ESC)"))) {
        if (m_host) m_host->CloseBrowser();
    }

    // GhostManga Okuyucusunda Ac (AMR tarzı görsel çıkarma)
    const std::wstring btnText = m_isExtracting ? i18n::Literal(L"Aktarılıyor...") : i18n::Literal(L"⚡ GhostManga'da Oku");
    if (w::TextButton(r, ui, Id("wv.extract"), amrRc, btnText, true, !m_isExtracting)) {
        ExtractAndRead();
    }

    // Ortada bolum gecis butonlari ve temiz Manga Basligi (ASLA raw URL gosterilmez!)
    const float navBtnW = 125.0f * s;
    const D2D1_RECT_F prevRc = RcWH(x, y, navBtnW, btnH);
    if (w::TextButton(r, ui, Id("wv.prevchap"), prevRc, i18n::Literal(L"◀ Önceki Bölüm"), false)) {
        NavigateChapterInBrowser(-1);
    }
    x += navBtnW + 8.0f * s;

    const float nextX = amrRc.left - navBtnW - 12.0f * s;
    const D2D1_RECT_F nextRc = RcWH(nextX, y, navBtnW, btnH);
    if (w::TextButton(r, ui, Id("wv.nextchap"), nextRc, i18n::Literal(L"Sonraki Bölüm ▶"), false)) {
        NavigateChapterInBrowser(+1);
    }

    // Temiz Manga / Sayfa Basligi
    if (nextX > x + 16.0f * s) {
        const D2D1_RECT_F titleRc = Rc(x, y, nextX - 8.0f * s, y + btnH);
        std::wstring displayTitle = m_mangaTitle.empty() ? i18n::Literal(L"Web Görünümü") : m_mangaTitle;
        r.Text(displayTitle, titleRc, theme::Text, 13.5f * s, Renderer::Align::Center, Renderer::Weight::Semibold);
    }
}

bool BrowserView::OnKey(UINT vk, bool ctrl, bool shift) {
    if (m_urlInput.focused) {
        if (vk == VK_RETURN) {
            m_urlInput.focused = false;
            Navigate(m_urlInput.text);
            return true;
        }
        return m_urlInput.OnKey(vk, ctrl, shift);
    }
    if (vk == VK_ESCAPE) {
        if (m_host) m_host->CloseBrowser();
        return true;
    }
    return false;
}

bool BrowserView::OnChar(wchar_t c) {
    if (m_urlInput.focused) {
        return m_urlInput.OnChar(c);
    }
    return false;
}

void BrowserView::ExtractAndRead() {
    if (!m_impl || !m_impl->m_webview || m_isExtracting) return;

    m_isExtracting = true;
    if (m_host) m_host->Toast(i18n::Literal(L"Sayfa taranıyor, bölüm görselleri aranıyor..."));

    std::wstring script = LR"JS(
    (function() {
        try {
            let imgs = [];
            let seen = new Set();

            function addUrl(u) {
                if (!u || typeof u !== 'string') return;
                u = u.trim();
                if (!u || u.startsWith('data:image/svg')) return;
                const lower = u.toLowerCase();
                if (lower.includes('avatar') || lower.includes('logo') || lower.includes('icon') ||
                    lower.includes('banner') || lower.includes('ad.') || lower.includes('ad_') ||
                    lower.includes('widget') || lower.includes('badge') || lower.includes('donate') ||
                    lower.includes('discord') || lower.includes('patreon') || lower.includes('button')) return;
                try {
                    u = new URL(u, window.location.href).href;
                } catch(e) {}
                if (!seen.has(u)) {
                    seen.add(u);
                    imgs.push(u);
                }
            }

            // 1. Bilinen JS değişkenlerini kontrol et (WordPress / MangaStream / Madara vb.)
            if (window.ts_reader && window.ts_reader.params && window.ts_reader.params.sources) {
                try {
                    for (const s of window.ts_reader.params.sources) {
                        if (s.images && Array.isArray(s.images)) {
                            for (const img of s.images) addUrl(img);
                        }
                    }
                } catch(e) {}
            }
            if (imgs.length === 0 && window.pages && Array.isArray(window.pages)) {
                for (const p of window.pages) {
                    if (typeof p === 'string') addUrl(p);
                    else if (p && p.page_image) addUrl(p.page_image);
                }
            }
            if (imgs.length === 0 && window.chapter_images && Array.isArray(window.chapter_images)) {
                for (const u of window.chapter_images) addUrl(u);
            }

            // 2. DOM Görsellerini tara
            if (imgs.length === 0) {
                const containers = [
                    '#readerarea img', '#readerarea p img', '.reading-content img', '.entry-content img',
                    '.chapter-content img', '.page-break img', '.chapter-image img',
                    '.container-chapter-reader img', '#chapter-images img',
                    'div[id*="chapter"] img', 'div[class*="chapter"] img', 'div[class*="reader"] img',
                    '.comic-images img', '.manga-images img', '#images-container img',
                    '.ts-main-image', '.viewer-cnt img', '.read-content img'
                ];
                let nodes = [];
                for (const sel of containers) {
                    const list = document.querySelectorAll(sel);
                    if (list && list.length >= 2) {
                        nodes = Array.from(list);
                        break;
                    }
                }
                if (nodes.length === 0) {
                    nodes = Array.from(document.querySelectorAll('img'));
                }

                for (const img of nodes) {
                    let src = img.getAttribute('data-src') || img.getAttribute('data-lazy-src') ||
                              img.getAttribute('data-original') || img.getAttribute('data-url') ||
                              img.getAttribute('data-page') || img.getAttribute('data-cfsrc') ||
                              img.getAttribute('data-cdn-src') || img.getAttribute('data-full-url') ||
                              img.currentSrc || img.src;
                    addUrl(src);
                }
            }

            // 3. Eğer görsel bulunamadıysa (seri ana sayfasındaysa), bölüm linklerini ara
            let firstChapUrl = '';
            if (imgs.length < 2) {
                const chSelectors = [
                    '.wp-manga-chapter a', 'li.wp-manga-chapter a', '.chapter-list a',
                    '.chapters-list a', '.chp-item a', 'ul.cl-list a',
                    '#chapterlist a', '.chapter-name a', 'a[href*="/chapter"]', 'a[href*="/bolum"]', 'a[href*="/ch-"]'
                ];
                for (const sel of chSelectors) {
                    const list = document.querySelectorAll(sel);
                    if (list && list.length > 0) {
                        for (const a of list) {
                            if (a.href && !a.href.startsWith('javascript:')) {
                                firstChapUrl = a.href;
                                break;
                            }
                        }
                        if (firstChapUrl) break;
                    }
                }
            }

            return JSON.stringify({
                title: document.title || 'Manga Bölümü',
                url: window.location.href,
                cookie: document.cookie || '',
                userAgent: navigator.userAgent || '',
                images: imgs,
                firstChapterUrl: firstChapUrl
            });
        } catch(err) {
            return JSON.stringify({ error: String(err), images: [] });
        }
    })();
    )JS";

    m_impl->m_webview->ExecuteScript(
        script.c_str(),
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [this](HRESULT hr, LPCWSTR resultJson) -> HRESULT {
                m_isExtracting = false;
                if (FAILED(hr) || !resultJson) {
                    if (m_host) m_host->Toast(i18n::Literal(L"Sayfa betiği çalıştırılamadı."));
                    return S_OK;
                }

                // resultJson is stringified JSON
                std::string rawStr = ToUtf8(resultJson);
                // Unescape JSON string
                std::string jsonStr;
                if (rawStr.size() >= 2 && rawStr.front() == '"' && rawStr.back() == '"') {
                    for (size_t i = 1; i + 1 < rawStr.size(); ++i) {
                        if (rawStr[i] == '\\' && i + 1 < rawStr.size() - 1) {
                            char n = rawStr[i + 1];
                            if (n == '"') { jsonStr += '"'; i++; }
                            else if (n == '\\') { jsonStr += '\\'; i++; }
                            else if (n == 'n') { jsonStr += '\n'; i++; }
                            else if (n == 'r') { jsonStr += '\r'; i++; }
                            else if (n == 't') { jsonStr += '\t'; i++; }
                            else { jsonStr += rawStr[i]; }
                        } else {
                            jsonStr += rawStr[i];
                        }
                    }
                } else {
                    jsonStr = rawStr;
                }

                json::Value root;
                std::string parseErr;
                if (!json::Parse(jsonStr, root, &parseErr) || !root.IsObject()) {
                    if (m_host) m_host->Toast(i18n::Literal(L"Bölüm verisi ayrıştırılamadı."));
                    return S_OK;
                }

                std::vector<std::string> images;
                if (const auto* arr = root.Get("images")) {
                    if (arr->IsArray()) {
                        for (const auto& item : arr->arr) {
                            if (!item.AsString().empty()) images.push_back(item.AsString());
                        }
                    }
                }

                // Eğer görsel bulunamadıysa ve bölüm listesi bağlantısı varsa kullanıcıyı bölüme yönlendir
                if (images.size() < 2) {
                    if (const auto* fc = root.Get("firstChapterUrl")) {
                        std::string chUrl = fc->AsString();
                        if (!chUrl.empty()) {
                            if (m_host) m_host->Toast(i18n::Literal(L"Bölüm listesi algılandı, en son bölüme yönlendiriliyor..."));
                            Navigate(FromUtf8(chUrl));
                            return S_OK;
                        }
                    }
                    if (m_host) m_host->Toast(i18n::Literal(L"Bölüm görseli bulunamadı. Lütfen bölümün tam yüklendiğinden emin olun."));
                    return S_OK;
                }

                std::wstring pageTitle = m_mangaTitle;
                if (const auto* t = root.Get("title")) {
                    std::wstring docTitle = FromUtf8(t->AsString());
                    if (!docTitle.empty()) pageTitle = docTitle;
                }

                std::string pageUrl = m_currentUrl.empty() ? "" : ToUtf8(m_currentUrl);
                if (const auto* u = root.Get("url")) {
                    if (!u->AsString().empty()) pageUrl = u->AsString();
                }

                std::string cookieStr;
                if (const auto* ck = root.Get("cookie")) {
                    cookieStr = ck->AsString();
                }

                std::string userAgentStr;
                if (const auto* ua = root.Get("userAgent")) {
                    userAgentStr = ua->AsString();
                }

                wchar_t infoBuf[128];
                swprintf_s(infoBuf, i18n::Literal(L"Bölüm aktarılıyor: %zu sayfa indiriliyor..."), images.size());
                if (m_host) m_host->Toast(infoBuf);

                // Background download into cache
                std::thread([this, images, pageUrl, pageTitle, cookieStr, userAgentStr]() {
                    std::wstringstream ss;
                    ss << std::hex << std::setfill(L'0') << std::setw(16) << HashUrl(pageUrl);
                    std::wstring cacheDir = paths::DataDir() + L"\\cache\\browser_" + ss.str();
                    paths::EnsureDir(cacheDir);

                    int total = (int)images.size();
                    std::atomic<size_t> nextIndex{0};
                    std::atomic<int> successCount{0};

                    const unsigned int numWorkers = 4;
                    std::vector<std::thread> workers;
                    workers.reserve(numWorkers);

                    for (unsigned int w = 0; w < numWorkers; ++w) {
                        workers.emplace_back([&, w]() {
                            while (true) {
                                size_t i = nextIndex.fetch_add(1);
                                if (i >= images.size()) break;

                                const std::string& imgUrl = images[i];
                                std::string ext = ".jpg";
                                size_t qPos = imgUrl.find('?');
                                std::string pOnly = (qPos != std::string::npos) ? imgUrl.substr(0, qPos) : imgUrl;
                                size_t dotPos = pOnly.rfind('.');
                                if (dotPos != std::string::npos && dotPos + 1 < pOnly.size()) {
                                    std::string cand = pOnly.substr(dotPos);
                                    for (char& c : cand) c = (char)tolower((unsigned char)c);
                                    if (cand == ".png" || cand == ".webp" || cand == ".jpeg" || cand == ".jpg" || cand == ".avif") {
                                        ext = (cand == ".jpeg") ? ".jpg" : cand;
                                    }
                                }

                                wchar_t fileName[64];
                                swprintf_s(fileName, L"%04d%s", (int)i + 1, FromUtf8(ext).c_str());
                                std::wstring outPath = cacheDir + L"\\" + fileName;

                                if (IsValidImageFile(outPath)) {
                                    successCount.fetch_add(1);
                                    continue;
                                }

                                net::HttpRequest req;
                                req.url = imgUrl;
                                req.timeoutMs = 25000;
                                if (!userAgentStr.empty()) {
                                    req.headers.push_back({ "User-Agent", userAgentStr });
                                }
                                if (!cookieStr.empty()) {
                                    req.headers.push_back({ "Cookie", cookieStr });
                                }
                                if (!pageUrl.empty()) {
                                    req.headers.push_back({ "Referer", pageUrl });
                                }
                                req.headers.push_back({ "Accept", "image/avif,image/webp,image/apng,image/*,*/*;q=0.8" });
                                req.headers.push_back({ "Sec-Fetch-Dest", "image" });
                                req.headers.push_back({ "Sec-Fetch-Mode", "no-cors" });
                                req.headers.push_back({ "Sec-Fetch-Site", "cross-site" });

                                net::HttpResponse resp;
                                for(int attempt=0;attempt<4;++attempt) {
                                    if(attempt) std::this_thread::sleep_for(std::chrono::milliseconds(250*(1<<(attempt-1))));
                                    resp=net::Fetch(req,0);
                                    if(resp.success && IsValidImageBuffer(resp.body)) break;
                                }
                                if (resp.success && IsValidImageBuffer(resp.body)) {
                                    std::wstring tmpPath = outPath + L"." + std::to_wstring(GetCurrentThreadId()) + L".tmp";
                                    if (fileio::WriteAtomic(tmpPath, resp.body)) {
                                        if (!MoveFileExW(tmpPath.c_str(), outPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {DeleteFileW(tmpPath.c_str());continue;}
                                        successCount.fetch_add(1);
                                    }
                                }
                            }
                        });
                    }

                    for (auto& t : workers) {
                        if (t.joinable()) t.join();
                    }

                    if (successCount > 0) {
                        std::wstring completeMarker = cacheDir + L"\\.complete";
                        if (successCount.load() == total) fileio::WriteAtomic(completeMarker, std::to_string(total));
                else DeleteFileW(completeMarker.c_str());

                        // Zaman damgası dosyası (.timestamp) - 3 gün sonra temizlik için
                        std::wstring tsFile = cacheDir + L"\\.timestamp";
                        int64_t nowSec = (int64_t)time(nullptr);
                        fileio::WriteAtomic(tsFile, std::to_string(nowSec));

                        if (m_host) {
                            m_host->OpenExtractedChapter(cacheDir, pageTitle, i18n::Literal(L"Aktarılan Bölüm"));
                        }
                    } else {
                        if (m_host) m_host->Toast(i18n::Literal(L"Sayfalar indirilemedi. Cloudflare veya koruma kaynaklı olabilir."));
                    }
                }).detach();

                return S_OK;
            }
        ).Get()
    );
}

void BrowserView::CleanOldBrowserCache(int maxAgeDays) {
    std::wstring cacheBase = paths::DataDir() + L"\\cache";
    std::wstring searchPattern = cacheBase + L"\\browser_*";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileExW(searchPattern.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return;

    int64_t nowSec = (int64_t)time(nullptr);
    int64_t maxAgeSec = (int64_t)maxAgeDays * 86400LL;

    std::vector<std::wstring> dirsToDelete;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            std::wstring dirPath = cacheBase + L"\\" + fd.cFileName;

            bool shouldDelete = false;
            std::wstring tsFile = dirPath + L"\\.timestamp";
            std::string tsContent;
            if (fileio::ReadText(tsFile, 1024, tsContent) == fileio::ReadResult::Ok && !tsContent.empty()) {
                try {
                    int64_t ts = std::stoll(tsContent);
                    if (nowSec - ts > maxAgeSec) {
                        shouldDelete = true;
                    }
                } catch(...) {}
            } else {
                ULARGE_INTEGER uli;
                uli.LowPart = fd.ftLastWriteTime.dwLowDateTime;
                uli.HighPart = fd.ftLastWriteTime.dwHighDateTime;
                int64_t dirSec = (int64_t)((uli.QuadPart - 116444736000000000ull) / 10000000ull);
                if (nowSec - dirSec > maxAgeSec) {
                    shouldDelete = true;
                }
            }

            if (shouldDelete) {
                dirsToDelete.push_back(dirPath);
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    for (const auto& d : dirsToDelete) {
        std::wstring pattern = d + L"\\*";
        WIN32_FIND_DATAW cfd{};
        HANDLE ch = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &cfd, FindExSearchNameMatch, nullptr, 0);
        if (ch != INVALID_HANDLE_VALUE) {
            do {
                if (!(cfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::wstring fpath = d + L"\\" + cfd.cFileName;
                    DeleteFileW(fpath.c_str());
                }
            } while (FindNextFileW(ch, &cfd));
            FindClose(ch);
        }
        RemoveDirectoryW(d.c_str());
        log::Write(L"[BrowserView] Eski önbellek klasörü temizlendi (3+ gün): %s", d.c_str());
    }
}

void BrowserView::CrawlMangaChapters(const std::string& mangaUrl, std::function<void(const ChapterCrawlResult&)> onComplete) {
    if (m_impl) m_impl->CrawlMangaChapters(mangaUrl, onComplete);
}

void BrowserView::CrawlChapterImages(int tabIndex, const std::string& chapterUrl, const std::wstring& mangaTitle, const std::wstring& chapterTitle, std::function<void(const ImageCrawlResult&)> onComplete) {
    if (m_impl) m_impl->CrawlChapterImages(tabIndex, chapterUrl, mangaTitle, chapterTitle, onComplete);
}

void BrowserView::ReloadCurrentPage() {
    if (m_impl && m_impl->m_webview) {
        m_impl->m_webview->Reload();
    }
}

void BrowserView::CancelChallengeVerification() {
    if (m_impl) {
        m_impl->m_challengeModeActive = false;
        m_impl->m_crawlMode = Impl::CrawlMode::None;
        m_impl->m_browserImages.reset();
        if (m_impl->m_controller) {
            m_impl->m_controller->put_IsVisible(FALSE);
        }
    }
}

void BrowserView::FinishChallengeVerification() {
    if (m_impl) {
        m_impl->FinishChallengeVerification();
    }
}

void BrowserView::SyncCookiesToJar(const std::string& url) {
    if (m_impl) {
        m_impl->SyncCookiesToJar(url);
    }
}

bool BrowserView::IsCrawling() const {
    return m_impl && m_impl->m_crawlMode != Impl::CrawlMode::None;
}

void BrowserView::StartChallengeVerification(const std::string& url, const std::wstring& mangaTitle) {
    if (m_parent && GetCurrentThreadId() != GetWindowThreadProcessId(m_parent, nullptr)) {
        if (m_host) {
            m_host->RunOnUIThread([this, url, mangaTitle]() {
                StartChallengeVerification(url, mangaTitle);
            });
        }
        return;
    }

    std::string clean = url;
    size_t f = clean.find_first_not_of(" \t\r\n");
    if (f != std::string::npos) {
        size_t l = clean.find_last_not_of(" \t\r\n");
        clean = clean.substr(f, l - f + 1);
    }
    if (clean.empty() || clean == "__FIRST__" || clean == "__LAST__") {
        return;
    }

    m_impl->m_challengeModeActive = true;
    m_impl->m_challengeTargetUrl = clean;
    m_impl->m_crawlMangaTitle = mangaTitle;

    std::wstring wUrl = FromUtf8(clean);
    m_currentUrl = wUrl;
    m_urlInput.SetText(wUrl);
    if (!mangaTitle.empty()) m_mangaTitle = mangaTitle;

    if (m_impl->m_ready && m_impl->m_webview) {
        PWSTR curSource = nullptr;
        std::wstring current;
        if (SUCCEEDED(m_impl->m_webview->get_Source(&curSource)) && curSource) {
            current = curSource;
            CoTaskMemFree(curSource);
        }

        bool alreadyOnPage = false;
        if (!current.empty() && !wUrl.empty()) {
            if (current == wUrl) {
                alreadyOnPage = true;
            } else {
                auto getHost = [](const std::wstring& u) -> std::wstring {
                    size_t p = u.find(L"://");
                    if (p == std::wstring::npos) return L"";
                    size_t s = u.find(L'/', p + 3);
                    return u.substr(p + 3, (s != std::wstring::npos) ? (s - p - 3) : std::wstring::npos);
                };
                std::wstring h1 = getHost(current);
                std::wstring h2 = getHost(wUrl);
                if (!h1.empty() && h1 == h2) {
                    alreadyOnPage = true;
                }
            }
        }

        if (!alreadyOnPage) {
            m_impl->m_webview->Navigate(wUrl.c_str());
        }
        m_impl->InjectChallengeWatcherScript();

        if (m_impl->m_controller) {
            ApplyLastBounds();
            m_impl->m_controller->put_IsVisible(TRUE);
        }
    } else {
        m_impl->m_pendingUrl = wUrl;
        if (!m_impl->m_initStarted && m_parent) {
            m_impl->Init(m_parent, m_host, this);
        }
    }
}

bool BrowserView::IsChallengeVerificationActive() const {
    return m_impl && m_impl->m_challengeModeActive;
}

} // namespace gm

