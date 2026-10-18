#include "MirrorEngine.h"
#include "JsRuntimeSetup.h"
#include "../core/FileIo.h"
#include "../core/Paths.h"
#include "../core/Log.h"
#include "../core/Json.h"
#include "../core/Utf8.h"
#include "ChapterState.h"
#include "net/Fetch.h"
#include "net/ChallengeDetector.h"
#include <algorithm>
#include <thread>
#include <chrono>

namespace gm::engine {

MirrorEngine& MirrorEngine::Instance() {
    static MirrorEngine s_instance;
    return s_instance;
}

MirrorEngine::~MirrorEngine() {
    Shutdown();
}

void MirrorEngine::Shutdown() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized) return;
    m_engine.Shutdown();
    m_bridge.ReleaseAllDocs();
    // Mirror metadata is immutable and referenced by the UI. Keep it alive
    // when discarding a timed-out VM operation.
    m_initialized = false;
}

std::string MirrorEngine::LoadBundleSource() {
    // 1. Check portable_data/mirrors.override.js
    std::wstring overridePath = paths::DataDir() + L"\\mirrors.override.js";
    std::string overrideSrc;
    if (fileio::ReadText(overridePath, 10 * 1024 * 1024, overrideSrc) == fileio::ReadResult::Ok && !overrideSrc.empty()) {
        log::Write(L"Ayna paketi override yuklendi: %ls", overridePath.c_str());
        return overrideSrc;
    }

    // 2. Check resources/mirrors/mirrors.bundle.js on disk
    std::wstring bundlePath = paths::ExeDir() + L"\\resources\\mirrors\\mirrors.bundle.js";
    std::string bundleSrc;
    if (fileio::ReadText(bundlePath, 10 * 1024 * 1024, bundleSrc) == fileio::ReadResult::Ok && !bundleSrc.empty()) {
        return bundleSrc;
    }

    // 3. Fallback: current directory resources
    if (fileio::ReadText(L"resources\\mirrors\\mirrors.bundle.js", 10 * 1024 * 1024, bundleSrc) == fileio::ReadResult::Ok && !bundleSrc.empty()) {
        return bundleSrc;
    }

    return "";
}

bool MirrorEngine::Init() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_initialized) return true;

    if (!m_engine.Init(128 * 1024 * 1024)) {
        log::Write(L"JsEngine baslatilamadi!");
        return false;
    }

    m_bridge.Install(m_engine);

    // 1. Polyfills
    JSValue rPoly = m_engine.Eval(kPolyfillsJs, "<polyfills>");
    JS_FreeValue(m_engine.Ctx(), rPoly);

    // 2. Cheerio Shim
    JSValue rCheerio = m_engine.Eval(kCheerioShimJs, "<cheerio>");
    JS_FreeValue(m_engine.Ctx(), rCheerio);

    // 3. CryptoJS if available
    std::string cryptoSrc;
    std::wstring cryptoPath = paths::ExeDir() + L"\\third_party\\crypto-js\\crypto-js.js";
    if (fileio::ReadText(cryptoPath, 2 * 1024 * 1024, cryptoSrc) == fileio::ReadResult::Ok && !cryptoSrc.empty()) {
        JSValue rCrypto = m_engine.Eval(cryptoSrc + "\nglobalThis.CryptoJS = CryptoJS;", "<crypto-js>");
        JS_FreeValue(m_engine.Ctx(), rCrypto);
    } else if (fileio::ReadText(L"third_party\\crypto-js\\crypto-js.js", 2 * 1024 * 1024, cryptoSrc) == fileio::ReadResult::Ok && !cryptoSrc.empty()) {
        JSValue rCrypto = m_engine.Eval(cryptoSrc + "\nglobalThis.CryptoJS = CryptoJS;", "<crypto-js>");
        JS_FreeValue(m_engine.Ctx(), rCrypto);
    }

    // 4. Mirrors Bundle
    std::string bundleSrc = LoadBundleSource();
    if (bundleSrc.empty()) {
        log::Write(L"Ayna paketi bulunamadi (mirrors.bundle.js)!");
        return false;
    }

    JSValue rBundle = m_engine.Eval(bundleSrc, "<mirrors.bundle.js>");
    if (JS_IsException(rBundle)) {
        log::Write(L"Ayna paketi derleme hatasi!");
        JS_FreeValue(m_engine.Ctx(), rBundle);
        return false;
    }
    JS_FreeValue(m_engine.Ctx(), rBundle);

    // 5. Initialize Loader & Helpers
    const char* initScript = R"JS(
        globalThis.__loader = globalThis.getMirrorLoader(globalThis.mirrorHelper);
        const canonicalHome = url => {
            url = String(url || '').trim();
            if (url.startsWith('//')) url = 'https:' + url;
            else if (url && !/^https?:\/\//i.test(url) && /^[^/?\s]+\.[^/?\s]+/.test(url)) url = 'https://' + url;
            return url;
        };
        for (const source of globalThis.__loader.mirrors || []) {
            source.home = canonicalHome(source.home);
            if (source.options) for (const key of ['base_url', 'search_url']) {
                if (Object.prototype.hasOwnProperty.call(source.options, key)) {
                    const value = canonicalHome(source.options[key] || source.home);
                    source.options[key] = key === 'base_url' || /^https?:\/\/[^/]+\/?$/.test(value)
                        ? value.replace(/\/?$/, '/') : value;
                }
            }
        }

        globalThis.__findImpl = async function(mirrorNameOrUrl) {
            if (!mirrorNameOrUrl) return null;
            let impl = await globalThis.__loader.getImpl(mirrorNameOrUrl);
            if (impl) return impl;

            const mirrors = globalThis.__loader.mirrors || [];
            const clean = s => String(s || "").toLowerCase().replace(/[^a-z0-9]/g, "");
            const targetClean = clean(mirrorNameOrUrl);

            // 1. Clean name match (e.g. "mangafox" vs "manga-fox" vs "fanfox")
            if (targetClean) {
                for (const m of mirrors) {
                    if (clean(m.mirrorName) === targetClean) return m;
                }
            }

            // 2. Domain match or URL match
            const lowerTarget = String(mirrorNameOrUrl).toLowerCase();
            for (const m of mirrors) {
                if (m.domains && m.domains.some(d => {
                    const dl = d.toLowerCase();
                    return lowerTarget.includes(dl) || (targetClean && dl.includes(targetClean));
                })) {
                    return m;
                }
                if (m.home) {
                    const hl = m.home.toLowerCase().replace(/^https?:\/\//, '').replace(/\/$/, '');
                    if (lowerTarget.includes(hl) || (targetClean && hl.includes(targetClean))) {
                        return m;
                    }
                }
            }

            return null;
        };

        globalThis.__getAllMirrors = function() {
            const mirrors = globalThis.__loader.getAll();
            return mirrors.map(m => ({
                name: m.mirrorName,
                language: m.languages || "",
                domains: m.domains || [],
                home: m.home || "",
                imageReferer: m.imageReferer || "",
                canListFullMangas: !!m.canListFullMangas,
                disabled: !!m.disabled,
                iconDataUrl: m.mirrorIcon || ""
            }));
        };

        globalThis.__searchManga = async function(mirrorName, query) {
            const impl = await globalThis.__findImpl(mirrorName);
            if (!impl || impl.disabled) { globalThis.__lastMirrorError = "Kaynak bulunamadi veya devre disi"; return []; }
            try {
                const res = await impl.getMangaList(query);
                // Full-catalogue mirrors ignore the query. Filter their output
                // before grouping or the UI receives thousands of unrelated titles.
                if (!Array.isArray(res)) return [];
                const results = [], seen = new Set();
                for (const item of res) {
                    if (!item || !item[0] || !item[1] ||
                        !__searchTitleMatches(String(item[0]) + ' ' + String(item[2] || ''), query)) continue;
                    try {
                        const raw = String(item[1]).trim();
                        if (/^[a-z][\w+.-]*:/i.test(raw) && !/^https?:/i.test(raw)) continue;
                        const url = new URL(raw, impl.home);
                        if (!url.origin || !/^https?:$/.test(url.protocol)) continue;
                        const canonical = url.origin + url.pathname + url.search;
                        if (seen.has(canonical)) continue;
                        seen.add(canonical);
                        results.push({title:String(item[0]).trim(), url:canonical});
                    } catch (_) {} // One malformed card must not discard the other results.
                }
                return results;
            } catch(e) {
                console.error("[searchManga error]", e);
                globalThis.__lastMirrorError = String(e.message || e);
                return [];
            }
        };

        globalThis.__getChapters = async function(mirrorName, mangaUrl, language) {
            let impl = await globalThis.__findImpl(mirrorName);
            if (!impl && mangaUrl) {
                impl = await globalThis.__findImpl(mangaUrl);
            }
            if (!impl) {
                globalThis.__lastMirrorError = "Kaynak bulunamadi: " + mirrorName;
                console.error("[getChapters] Mirror not found for:", mirrorName, mangaUrl);
                return [];
            }
            try {
                let res = await impl.getListChaps(mangaUrl, language || "en");
                // MangaDex returns a dictionary of language -> chapter lists.
                if (res && !Array.isArray(res) && typeof res === 'object')
                    res = res[language || "en"] || (language ? [] : (res.en || Object.values(res).find(Array.isArray)));
                if (!Array.isArray(res)) return [];
                return res.map(item => ({
                    title: String(item[0] || ""),
                    url: item[1] ? new URL(String(item[1]), impl.home).href : ""
                }));
            } catch(e) {
                console.error("[getChapters error]", e);
                globalThis.__lastMirrorError = String(e.message || e);
                return [];
            }
        };

        globalThis.__getChapterImages = async function(mirrorName, chapterUrl) {
            let impl = await globalThis.__findImpl(mirrorName);
            if (!impl && chapterUrl) {
                impl = await globalThis.__findImpl(chapterUrl);
            }
            if (!impl) {
                globalThis.__lastMirrorError = "Kaynak bulunamadi: " + mirrorName;
                console.error("[getChapterImages] Mirror not found for:", mirrorName, chapterUrl);
                return [];
            }
            try {
                if (chapterUrl.startsWith("http://")) chapterUrl = "https://" + chapterUrl.slice(7);
                if (typeof impl.normalizeUrl === 'function') chapterUrl = impl.normalizeUrl(chapterUrl);
                // MangaDex's API reader does not need the protected HTML shell.
                const doc = impl.api === 'https://api.mangadex.org' ? '<html></html>' :
                    await globalThis.mirrorHelper.loadPage(chapterUrl, { nocache: true });
                if (!doc) return [];

                // 1. Direct unpack check for Dean Edwards packed scripts (MangaHere, MangaFox / FanFox)
                if (doc.includes("eval(function(p,a,c,k,e,d)")) {
                    try {
                        const evStart = doc.indexOf("eval(function(p,a,c,k,e,d)");
                        const sub = doc.substring(evStart + 4);
                        let pCount = 0, endIdx = -1;
                        for (let i = 0; i < sub.length; i++) {
                            if (sub[i] === '(') pCount++;
                            else if (sub[i] === ')') { if (--pCount === 0) { endIdx = i; break; } }
                        }
                        if (endIdx !== -1) {
                            const unpacked = (new Function("return " + sub.substring(0, endIdx + 1)))();
                            if (typeof unpacked === "string") {
                                const m = unpacked.match(/newImgs\s*=\s*\[([^\]]+)\]/);
                                if (m) {
                                    const rawUrls = m[1].split(",");
                                    const parsed = rawUrls.map(u => u.trim().replace(/^['"]|['"]$/g, '')).filter(Boolean);
                                    if (parsed.length > 0) return parsed;
                                }
                            }
                        }
                    } catch(e) {}
                }

                // 2. Fallback to mirror implementation getListImages
                const res = await impl.getListImages(doc, chapterUrl, null);
                if (!Array.isArray(res) || res.length === 0) return [];

                // Check if getListImages returned HTML page URLs (e.g. MangaHere / FanFox with .html)
                const hasHtml = res.some(u => String(u).includes(".html") || String(u).includes(".htm"));
                if (hasHtml) {
                    const resolved = [];
                    const cidMatch = doc.match(/chapterid\s*=\s*(\d+)/i);
                    const isFanFoxOrHere = (mirrorName && (mirrorName.includes("Fox") || mirrorName.includes("Here"))) ||
                                           chapterUrl.includes("fanfox.net") || chapterUrl.includes("mangahere.cc");

                    if (isFanFoxOrHere && cidMatch) {
                        const cid = cidMatch[1];
                        let key = "";
                        const evStart = doc.indexOf("eval(function(p,a,c,k,e,d)");
                        if (evStart !== -1) {
                            try {
                                const sub = doc.substring(evStart + 4);
                                let pCount = 0, endIdx = -1;
                                for (let i = 0; i < sub.length; i++) {
                                    if (sub[i] === '(') pCount++;
                                    else if (sub[i] === ')') { if (--pCount === 0) { endIdx = i; break; } }
                                }
                                const unp = (new Function("return " + sub.substring(0, endIdx + 1)))();
                                const km = unp.match(/guidkey\s*=\s*([^;]+)/i);
                                if (km) key = (new Function("return " + km[1]))();
                            } catch(e) {}
                        }
                        const imgCountM = doc.match(/imagecount\s*=\s*(\d+)/i);
                        const totalPages = imgCountM ? parseInt(imgCountM[1]) : res.length;
                        const curl = chapterUrl.substring(0, chapterUrl.lastIndexOf("/") + 1);

                        const seenUrls = new Set();
                        for (let pageNum = 1; pageNum <= totalPages; pageNum++) {
                            if (seenUrls.size >= totalPages) break;
                            try {
                                const chapfunUrl = `${curl}chapterfun.ashx?cid=${cid}&page=${pageNum}&key=${key}`;
                                const funData = await globalThis.mirrorHelper.loadPage(chapfunUrl, {
                                    nocache: true,
                                    headers: {
                                        "Referer": chapterUrl,
                                        "X-Requested-With": "XMLHttpRequest",
                                        "Cookie": "isAdult=1"
                                    }
                                });
                                if (funData && funData.includes("eval(function")) {
                                    const sub2 = funData.trim().substring(4);
                                    const unp2 = (new Function("return " + sub2))();
                                    const pixM = unp2.match(/var pix\s*=\s*"([^"]+)"/);
                                    const pvalM = unp2.match(/var pvalue\s*=\s*\[([^\]]+)\]/);
                                    if (pvalM) {
                                        const pix = pixM ? pixM[1] : "";
                                        const items = pvalM[1].split(",").map(s => s.trim().replace(/^['"]|['"]$/g, ''));
                                        for (const it of items) {
                                            const fullImg = it.startsWith("http") || it.startsWith("//") ? it : (pix + it);
                                            if (!seenUrls.has(fullImg)) {
                                                seenUrls.add(fullImg);
                                                resolved.push(fullImg);
                                            }
                                        }
                                    }
                                }
                            } catch(e) {}
                        }
                    }

                    if (resolved.length > 0) {
                        return resolved;
                    }

                    // Try impl.getImageUrlFromPage if available
                    if (typeof impl.getImageUrlFromPage === 'function') {
                        const batchSize = 6;
                        for (let i = 0; i < res.length; i += batchSize) {
                            const chunk = res.slice(i, i + batchSize);
                            const chunkRes = await Promise.all(chunk.map(async u => {
                                try {
                                    return await impl.getImageUrlFromPage(u);
                                } catch(e) { return ""; }
                            }));
                            for (const r of chunkRes) {
                                if (r && !r.includes(".html") && !r.includes(".htm")) resolved.push(r);
                            }
                        }
                    }

                    if (resolved.length > 0) return resolved;

                    // If .html could not be resolved to genuine image URLs, return [] to trigger crawler!
                    return [];
                }

                // Filter out any non-image URLs
                return res.map(url => String(url || "").trim())
                          .filter(url => url.length > 0 && !url.includes(".html") && !url.includes(".htm") && !url.startsWith("data:text"));
            } catch(e) {
                console.error("[getChapterImages error]", e);
                globalThis.__lastMirrorError = String(e.message || e);
                return [];
            }
        };
    )JS";

    JSValue rInit = m_engine.Eval(initScript, "<initScript>");
    JS_FreeValue(m_engine.Ctx(), rInit);

    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        if (m_statuses.empty()) {
            std::string text;
            json::Value arr;
            if (fileio::ReadText(paths::Join(paths::DataDir(), L"source_status.json"), 1024 * 1024, text) == fileio::ReadResult::Ok && json::Parse(text, arr, nullptr) && arr.IsArray()) {
                for (const auto& o : arr.arr) {
                    SourceStatus item;
                    if (const auto* v = o.Get("mirror")) item.mirror = v->AsString();
                    if (const auto* v = o.Get("operation")) item.operation = v->AsString();
                    if (const auto* v = o.Get("error")) item.error = v->AsString();
                    if (const auto* v = o.Get("resultCount")) item.resultCount = (int)v->AsInt();
                    if (const auto* v = o.Get("checkedAt")) item.checkedAt = v->AsInt();
                    if (const auto* v = o.Get("success")) item.success = v->AsBool();
                    if (const auto* v = o.Get("httpStatus")) item.httpStatus=(int)v->AsInt();
                    if (!item.mirror.empty()) m_statuses.push_back(std::move(item));
                }
            }
        }
    }
    m_initialized = true;
    log::Write(L"MirrorEngine basariyla baslatildi.");
    return true;
}

const std::vector<MirrorInfo>& MirrorEngine::GetMirrors() {
    static const std::vector<MirrorInfo> s_empty;
    if (m_cachedMirrorsCount.load(std::memory_order_acquire) > 0 && !m_cachedMirrors.empty()) {
        return m_cachedMirrors;
    }
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized && !Init()) return s_empty;
    if (!m_cachedMirrors.empty()) return m_cachedMirrors;

    JSValue global = JS_GetGlobalObject(m_engine.Ctx());
    JSValue fn = JS_GetPropertyStr(m_engine.Ctx(), global, "__getAllMirrors");
    JS_FreeValue(m_engine.Ctx(), global);

    if (!JS_IsFunction(m_engine.Ctx(), fn)) {
        JS_FreeValue(m_engine.Ctx(), fn);
        return s_empty;
    }

    JSValue res = m_engine.Call(fn, JS_UNDEFINED, 0, nullptr);
    JS_FreeValue(m_engine.Ctx(), fn);

    std::string jsonStr = m_engine.ToJson(res);
    JS_FreeValue(m_engine.Ctx(), res);

    json::Value v;
    std::string err;
    if (json::Parse(jsonStr, v, &err) && v.IsArray()) {
        for (const auto& item : v.arr) {
            if (!item.IsObject()) continue;
            MirrorInfo info;
            if (const auto* n = item.Get("name")) info.name = n->AsString();
            if (const auto* l = item.Get("language")) info.language = l->AsString();
            if (const auto* h = item.Get("home")) info.home = h->AsString();
            if (const auto* h = item.Get("imageReferer")) info.imageReferer = h->AsString();
            if (const auto* c = item.Get("canListFullMangas")) info.canListFullMangas = c->AsBool();
            if (const auto* d = item.Get("disabled")) info.disabled = d->AsBool();
            if (const auto* i = item.Get("iconDataUrl")) info.iconDataUrl = i->AsString();
            if (const auto* dom = item.Get("domains")) {
                if (dom->IsArray()) {
                    for (const auto& di : dom->arr) info.domains.push_back(di.AsString());
                }
            }
            m_cachedMirrors.push_back(std::move(info));
        }
    }

    m_cachedMirrorsCount.store(m_cachedMirrors.size(), std::memory_order_release);
    log::Write(L"Yuklenen ayna sayisi: %zu", m_cachedMirrors.size());
    return m_cachedMirrors;
}

std::vector<SearchResult> MirrorEngine::SearchManga(const std::string& mirrorName, const std::string& query,
                                                  const std::function<bool()>& cancelled) {
    std::unique_lock<std::recursive_mutex> lock(m_mutex, std::defer_lock);
    while (!lock.try_lock()) {
        if (cancelled && cancelled()) return {};
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (cancelled && cancelled()) return {};
    if (!m_initialized && !Init()) return {};

    JS_UpdateStackTop(m_engine.Rt());
    m_engine.SetCancelCheck(cancelled);
    struct ClearOperation { JsEngine& engine; ~ClearOperation() { engine.SetCancelCheck(); engine.SetTimeoutSeconds(0); } } clear{m_engine};
    m_engine.SetTimeoutSeconds(15);
    JSValue resetError = m_engine.Eval("globalThis.__lastMirrorError = '';", "<operation>");
    JS_FreeValue(m_engine.Ctx(), resetError);
    JSValue global = JS_GetGlobalObject(m_engine.Ctx());
    JSValue fn = JS_GetPropertyStr(m_engine.Ctx(), global, "__searchManga");
    JS_FreeValue(m_engine.Ctx(), global);

    if (!JS_IsFunction(m_engine.Ctx(), fn)) {
        JS_FreeValue(m_engine.Ctx(), fn);
        return {};
    }

    JSValue args[2] = {
        JS_NewString(m_engine.Ctx(), mirrorName.c_str()),
        JS_NewString(m_engine.Ctx(), query.c_str())
    };

    JSValue promise = m_engine.Call(fn, JS_UNDEFINED, 2, args);
    JS_FreeValue(m_engine.Ctx(), args[0]);
    JS_FreeValue(m_engine.Ctx(), args[1]);
    JS_FreeValue(m_engine.Ctx(), fn);

    // Drain microtasks
    m_engine.RunJobs();

    std::vector<SearchResult> results;
    if (JS_IsPromise(promise)) {
        auto startWait = std::chrono::steady_clock::now();
        while (JS_PromiseState(m_engine.Ctx(), promise) == JS_PROMISE_PENDING) {
            if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startWait).count() > 15 || m_engine.RemainingMilliseconds() <= 0) {
                log::Write(L"[MirrorEngine] SearchManga stopped (cancelled or 15s deadline)");
                break;
            }
            if (m_engine.RunJobs() == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        if (JS_PromiseState(m_engine.Ctx(), promise) == JS_PROMISE_FULFILLED) {
            JSValue val = JS_PromiseResult(m_engine.Ctx(), promise);
            std::string jsonStr = m_engine.ToJson(val);
            JS_FreeValue(m_engine.Ctx(), val);

            json::Value parsed;
            std::string err;
            if (json::Parse(jsonStr, parsed, &err) && parsed.IsArray()) {
                for (const auto& item : parsed.arr) {
                    if (!item.IsObject()) continue;
                    SearchResult sr;
                    if (const auto* t = item.Get("title")) sr.title = t->AsString();
                    if (const auto* u = item.Get("url")) sr.url = u->AsString();
                    if (!sr.title.empty()) results.push_back(std::move(sr));
                }
            }
        }
    }
    const bool wasCancelled = cancelled && cancelled();
    if (!wasCancelled) RecordStatus(mirrorName, "Arama", (int)results.size(), promise);
    const bool abandoned = JS_IsPromise(promise) && JS_PromiseState(m_engine.Ctx(), promise) == JS_PROMISE_PENDING;
    m_engine.SetTimeoutSeconds(0);
    JS_FreeValue(m_engine.Ctx(), promise);
    m_bridge.ReleaseAllDocs();
    // A pending promise can retain DOM handles. Never run its jobs in the next
    // operation after releasing those documents.
    if (abandoned) Shutdown();
    return wasCancelled ? std::vector<SearchResult>{} : results;
}

std::vector<ChapterInfo> MirrorEngine::GetChapters(const std::string& mirrorName, const std::string& mangaUrl, const std::string& language) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized && !Init()) return {};

    JS_UpdateStackTop(m_engine.Rt());
    m_engine.SetTimeoutSeconds(30);
    JSValue resetError = m_engine.Eval("globalThis.__lastMirrorError = '';", "<operation>");
    JS_FreeValue(m_engine.Ctx(), resetError);
    JSValue global = JS_GetGlobalObject(m_engine.Ctx());
    JSValue fn = JS_GetPropertyStr(m_engine.Ctx(), global, "__getChapters");
    JS_FreeValue(m_engine.Ctx(), global);

    if (!JS_IsFunction(m_engine.Ctx(), fn)) {
        JS_FreeValue(m_engine.Ctx(), fn);
        return {};
    }

    JSValue args[3] = {
        JS_NewString(m_engine.Ctx(), mirrorName.c_str()),
        JS_NewString(m_engine.Ctx(), mangaUrl.c_str()),
        JS_NewString(m_engine.Ctx(), language.c_str())
    };

    JSValue promise = m_engine.Call(fn, JS_UNDEFINED, 3, args);
    JS_FreeValue(m_engine.Ctx(), args[0]);
    JS_FreeValue(m_engine.Ctx(), args[1]);
    JS_FreeValue(m_engine.Ctx(), args[2]);
    JS_FreeValue(m_engine.Ctx(), fn);

    m_engine.RunJobs();

    std::vector<ChapterInfo> chapters;
    if (JS_IsPromise(promise)) {
        auto startWait = std::chrono::steady_clock::now();
        while (JS_PromiseState(m_engine.Ctx(), promise) == JS_PROMISE_PENDING) {
            if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startWait).count() > 30 || m_engine.RemainingMilliseconds() <= 0) {
                log::Write(L"[MirrorEngine] GetChapters timeout (10s) reached, breaking wait");
                break;
            }
            if (m_engine.RunJobs() == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        if (JS_PromiseState(m_engine.Ctx(), promise) == JS_PROMISE_FULFILLED) {
            JSValue val = JS_PromiseResult(m_engine.Ctx(), promise);
            std::string jsonStr = m_engine.ToJson(val);
            JS_FreeValue(m_engine.Ctx(), val);

            json::Value parsed;
            std::string err;
            if (json::Parse(jsonStr, parsed, &err) && parsed.IsArray()) {
                for (const auto& item : parsed.arr) {
                    if (!item.IsObject()) continue;
                    ChapterInfo ci;
                    if (const auto* t = item.Get("title")) ci.title = t->AsString();
                    if (const auto* u = item.Get("url")) ci.url = u->AsString();
                    if (!ci.url.empty()) chapters.push_back(std::move(ci));
                }
            }
        }
    }
    std::vector<Chapter> normalized;
    for (const auto& ch : chapters) normalized.push_back({ch.title, ch.url});
    NormalizeChapters(normalized);
    chapters.clear();
    for (auto& ch : normalized) chapters.push_back({std::move(ch.title), std::move(ch.url)});
    RecordStatus(mirrorName, "Bölümler", (int)chapters.size(), promise);
    const bool abandoned = JS_IsPromise(promise) && JS_PromiseState(m_engine.Ctx(), promise) == JS_PROMISE_PENDING;
    m_engine.SetTimeoutSeconds(0);
    JS_FreeValue(m_engine.Ctx(), promise);
    m_bridge.ReleaseAllDocs();
    // A pending promise can retain DOM handles. Never run its jobs in the next
    // operation after releasing those documents.
    if (abandoned) Shutdown();
    return chapters;
}

std::vector<std::string> MirrorEngine::GetChapterImages(const std::string& mirrorName, const std::string& chapterUrl) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized && !Init()) return {};

    JS_UpdateStackTop(m_engine.Rt());
    m_engine.SetTimeoutSeconds(60);
    JSValue resetError = m_engine.Eval("globalThis.__lastMirrorError = '';", "<operation>");
    JS_FreeValue(m_engine.Ctx(), resetError);
    JSValue global = JS_GetGlobalObject(m_engine.Ctx());
    JSValue fn = JS_GetPropertyStr(m_engine.Ctx(), global, "__getChapterImages");
    JS_FreeValue(m_engine.Ctx(), global);

    if (!JS_IsFunction(m_engine.Ctx(), fn)) {
        JS_FreeValue(m_engine.Ctx(), fn);
        return {};
    }

    JSValue args[2] = {
        JS_NewString(m_engine.Ctx(), mirrorName.c_str()),
        JS_NewString(m_engine.Ctx(), chapterUrl.c_str())
    };

    JSValue promise = m_engine.Call(fn, JS_UNDEFINED, 2, args);
    JS_FreeValue(m_engine.Ctx(), args[0]);
    JS_FreeValue(m_engine.Ctx(), args[1]);
    JS_FreeValue(m_engine.Ctx(), fn);

    m_engine.RunJobs();

    std::vector<std::string> images;
    if (JS_IsPromise(promise)) {
        auto startWait = std::chrono::steady_clock::now();
        while (JS_PromiseState(m_engine.Ctx(), promise) == JS_PROMISE_PENDING) {
            if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startWait).count() > 60 || m_engine.RemainingMilliseconds() <= 0) {
                log::Write(L"[MirrorEngine] GetChapterImages timeout (10s) reached, breaking wait");
                break;
            }
            if (m_engine.RunJobs() == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        if (JS_PromiseState(m_engine.Ctx(), promise) == JS_PROMISE_FULFILLED) {
            JSValue val = JS_PromiseResult(m_engine.Ctx(), promise);
            std::string jsonStr = m_engine.ToJson(val);
            JS_FreeValue(m_engine.Ctx(), val);

            json::Value parsed;
            std::string err;
            if (json::Parse(jsonStr, parsed, &err) && parsed.IsArray()) {
                for (const auto& item : parsed.arr) {
                    std::string imgUrl = item.AsString();
                    size_t f = imgUrl.find_first_not_of(" \t\r\n");
                    if (f != std::string::npos) {
                        size_t l = imgUrl.find_last_not_of(" \t\r\n");
                        imgUrl = imgUrl.substr(f, l - f + 1);
                    }
                    if (!imgUrl.empty()) images.push_back(std::move(imgUrl));
                }
            }
        }
    }
    RecordStatus(mirrorName, "Sayfalar", (int)images.size(), promise);
    const bool abandoned = JS_IsPromise(promise) && JS_PromiseState(m_engine.Ctx(), promise) == JS_PROMISE_PENDING;
    m_engine.SetTimeoutSeconds(0);
    JS_FreeValue(m_engine.Ctx(), promise);
    m_bridge.ReleaseAllDocs();
    // A pending promise can retain DOM handles. Never run its jobs in the next
    // operation after releasing those documents.
    if (abandoned) Shutdown();
    return images;
}

std::string MirrorEngine::GetImageReferer(const std::string& mirrorName, const std::string& chapterUrl) {
    for (const auto& mirror : GetMirrors()) {
        if (mirror.name == mirrorName && (mirror.imageReferer.starts_with("https://") || mirror.imageReferer.starts_with("http://")))
            return mirror.imageReferer;
    }
    return chapterUrl;
}

SourceStatus MirrorEngine::CheckAccess(const MirrorInfo& mirror,int timeoutMs) {
    SourceStatus status;status.mirror=mirror.name;status.operation="Erişim";
    status.checkedAt=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    net::HttpRequest request;request.url=mirror.home.empty()?(mirror.domains.empty()?"":"https://"+mirror.domains.front()):mirror.home;
    request.timeoutMs=std::clamp(timeoutMs,1000,15000);request.maxResponseBytes=2*1024*1024;request.allowBrowserFallback=false;
    if(request.url.empty())status.error="Kaynak adresi bulunamadı";
    else {
        const auto response=net::Fetch(request);const auto challenge=net::ChallengeDetector::Detect(response);
        status.httpStatus=response.status;
        status.success=response.success && !challenge.NeedsBrowser() && !challenge.isBlocked;
        if(!status.success)status.error=!response.error.empty()?response.error:!challenge.reason.empty()?challenge.reason:"HTTP "+std::to_string(response.status);
        if(status.success)status.resultCount=1;
    }
    StoreStatus(status);return status;
}

std::vector<SourceStatus> MirrorEngine::GetSourceStatuses() const {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    return m_statuses;
}

void MirrorEngine::RecordStatus(const std::string& mirror, const std::string& operation, int count, JSValueConst promise) {
    SourceStatus status;
    status.mirror = mirror;
    status.operation = operation;
    status.resultCount = count;
    status.checkedAt = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    JSValue global = JS_GetGlobalObject(m_engine.Ctx());
    JSValue error = JS_GetPropertyStr(m_engine.Ctx(), global, "__lastMirrorError");
    status.error = m_engine.ToStr(error);
    JS_FreeValue(m_engine.Ctx(), error);
    JS_FreeValue(m_engine.Ctx(), global);
    if (JS_IsPromise(promise)) {
        const auto state = JS_PromiseState(m_engine.Ctx(), promise);
        if (state == JS_PROMISE_PENDING) status.error = "Kaynak zaman asimina ugradi";
        if (state == JS_PROMISE_REJECTED) {
            JSValue reason = JS_PromiseResult(m_engine.Ctx(), promise);
            status.error = m_engine.ToStr(reason);
            JS_FreeValue(m_engine.Ctx(), reason);
        }
    } else if (JS_IsException(promise)) status.error = "Kaynak betigi calistirilamadi";
    if (status.error == "undefined") status.error.clear();
    if (status.error.empty() && count == 0 && operation != "Arama") status.error = "Kaynak bos liste dondurdu; site degismis veya koruma devrede olabilir";
    status.success = status.error.empty();
    StoreStatus(std::move(status));
}

void MirrorEngine::StoreStatus(SourceStatus status) {
    const auto& mirror = status.mirror;
    const auto& operation = status.operation;
    if (!status.success) log::Write(L"[SourceFailure] %ls | %ls | %ls", FromUtf8(mirror).c_str(), FromUtf8(operation).c_str(), FromUtf8(status.error).c_str());
    std::lock_guard<std::mutex> lock(m_statusMutex);
    auto it = std::find_if(m_statuses.begin(), m_statuses.end(), [&](const auto& s) { return s.mirror == mirror && s.operation == operation; });
    if (it == m_statuses.end()) m_statuses.push_back(std::move(status));
    else *it = std::move(status);
    json::Value arr = json::Value::Arr();
    for (const auto& item : m_statuses) {
        json::Value o = json::Value::Obj();
        o.Add("mirror", json::Value::Str(item.mirror));
        o.Add("operation", json::Value::Str(item.operation));
        o.Add("error", json::Value::Str(item.error));
        o.Add("resultCount", json::Value::Num(item.resultCount));
        o.Add("checkedAt", json::Value::Num((double)item.checkedAt));
        o.Add("success", json::Value::Bool(item.success));
        o.Add("httpStatus",json::Value::Num(item.httpStatus));
        arr.Push(std::move(o));
    }
    fileio::WriteAtomic(paths::Join(paths::DataDir(), L"source_status.json"), json::Dump(arr));
}

} // namespace gm::engine
