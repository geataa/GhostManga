#include "JsBridge.h"
#include "../net/Fetch.h"
#include "../core/Log.h"
#include "../core/Utf8.h"
#include <string>
#include <vector>
#include <lexbor/dom/interfaces/node.h>

namespace gm::engine {

static JsBridge* s_bridgeInstance = nullptr;

// Base64 Helpers
static const std::string kBase64Chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

static std::string Base64Encode(const unsigned char* data, size_t len) {
    std::string ret;
    int i = 0, j = 0;
    unsigned char char_array_3[3], char_array_4[4];

    while (len--) {
        char_array_3[i++] = *(data++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for (i = 0; i < 4; i++) ret += kBase64Chars[char_array_4[i]];
            i = 0;
        }
    }
    if (i) {
        for (j = i; j < 3; j++) char_array_3[j] = '\0';
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        for (j = 0; j < i + 1; j++) ret += kBase64Chars[char_array_4[j]];
        while (i++ < 3) ret += '=';
    }
    return ret;
}

static std::string Base64Decode(const std::string& encoded_string) {
    size_t in_len = encoded_string.size();
    int i = 0, j = 0, in_ = 0;
    unsigned char char_array_4[4], char_array_3[3];
    std::string ret;

    while (in_len-- && (encoded_string[in_] != '=') &&
           (isalnum((unsigned char)encoded_string[in_]) || (encoded_string[in_] == '+') || (encoded_string[in_] == '/'))) {
        char_array_4[i++] = encoded_string[in_]; in_++;
        if (i == 4) {
            for (i = 0; i < 4; i++)
                char_array_4[i] = (unsigned char)kBase64Chars.find(char_array_4[i]);
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
            for (i = 0; i < 3; i++) ret += char_array_3[i];
            i = 0;
        }
    }
    if (i) {
        for (j = 0; j < i; j++)
            char_array_4[j] = (unsigned char)kBase64Chars.find(char_array_4[j]);
        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        for (j = 0; j < i - 1; j++) ret += char_array_3[j];
    }
    return ret;
}

int JsBridge::RegisterDoc(std::unique_ptr<HtmlDocument> doc) {
    std::lock_guard<std::mutex> lock(m_docMutex);
    int id = m_nextDocId++;
    DocContext ctx;
    ctx.doc = std::move(doc);
    // Root node is id 0
    ctx.GetOrAddNode(ctx.doc->Root().Raw());
    m_docs[id] = std::move(ctx);
    return id;
}

void JsBridge::ReleaseDoc(int docId) {
    std::lock_guard<std::mutex> lock(m_docMutex);
    m_docs.erase(docId);
}

void JsBridge::ReleaseAllDocs() {
    std::lock_guard<std::mutex> lock(m_docMutex);
    m_docs.clear();
}

DocContext* JsBridge::GetDoc(int docId) {
    std::lock_guard<std::mutex> lock(m_docMutex);
    auto it = m_docs.find(docId);
    return (it != m_docs.end()) ? &it->second : nullptr;
}

// C Callbacks for amrHost

static JSValue js_amrHost_request(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_EXCEPTION;

    const char* urlStr = JS_ToCString(ctx, argv[0]);
    if (!urlStr) return JS_EXCEPTION;
    std::string url(urlStr);
    JS_FreeCString(ctx, urlStr);

    net::HttpRequest req;
    req.url = url;
    req.maxResponseBytes = 16 * 1024 * 1024;
    auto* engine = static_cast<JsEngine*>(JS_GetContextOpaque(ctx));
    if (engine && engine->RemainingMilliseconds() <= 0)
        return JS_ThrowInternalError(ctx, "Kaynak istegi zaman asimina ugradi");
    req.timeoutMs = engine ? std::min(10000, engine->RemainingMilliseconds()) : 10000;

    if (argc >= 2 && JS_IsObject(argv[1])) {
        JSValue methodVal = JS_GetPropertyStr(ctx, argv[1], "method");
        if (JS_IsString(methodVal)) {
            const char* m = JS_ToCString(ctx, methodVal);
            if (m) { req.method = m; JS_FreeCString(ctx, m); }
        }
        JS_FreeValue(ctx, methodVal);

        JSValue bodyVal = JS_GetPropertyStr(ctx, argv[1], "body");
        if (JS_IsString(bodyVal)) {
            const char* b = JS_ToCString(ctx, bodyVal);
            if (b) { req.body = b; JS_FreeCString(ctx, b); }
        }
        JS_FreeValue(ctx, bodyVal);

        JSValue headersVal = JS_GetPropertyStr(ctx, argv[1], "headers");
        if (JS_IsObject(headersVal)) {
            JSPropertyEnum* tab = nullptr;
            uint32_t len = 0;
            if (JS_GetOwnPropertyNames(ctx, &tab, &len, headersVal, JS_GPN_STRING_MASK) == 0) {
                for (uint32_t i = 0; i < len; ++i) {
                    const char* propName = JS_AtomToCString(ctx, tab[i].atom);
                    JSValue propVal = JS_GetProperty(ctx, headersVal, tab[i].atom);
                    const char* propValStr = JS_ToCString(ctx, propVal);
                    if (propName && propValStr) {
                        req.headers.push_back({ propName, propValStr });
                    }
                    if (propValStr) JS_FreeCString(ctx, propValStr);
                    if (propName) JS_FreeCString(ctx, propName);
                    JS_FreeValue(ctx, propVal);
                    JS_FreeAtom(ctx, tab[i].atom);
                }
                js_free(ctx, tab);
            }
        }
        JS_FreeValue(ctx, headersVal);
    }

    net::HttpResponse res = net::Fetch(req);
    if (!res.success || res.status < 200 || res.status >= 300)
        return JS_ThrowInternalError(ctx, "HTTP %d: %s", res.status, res.error.empty() ? "Kaynak yanit vermedi veya erisim engellendi" : res.error.c_str());

    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);

    JSValue resObj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, resObj, "status", JS_NewInt32(ctx, res.status));
    JS_SetPropertyStr(ctx, resObj, "text", JS_NewStringLen(ctx, res.body.data(), res.body.size()));
    JS_SetPropertyStr(ctx, resObj, "finalUrl", JS_NewString(ctx, res.finalUrl.c_str()));

    JSValue hdrObj = JS_NewObject(ctx);
    for (const auto& [k, v] : res.headers) {
        JS_SetPropertyStr(ctx, hdrObj, k.c_str(), JS_NewString(ctx, v.c_str()));
    }
    JS_SetPropertyStr(ctx, resObj, "headers", hdrObj);

    // Resolve promise
    JS_Call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, &resObj);
    JS_FreeValue(ctx, resObj);
    JS_FreeValue(ctx, resolving_funcs[0]);
    JS_FreeValue(ctx, resolving_funcs[1]);

    return promise;
}

static JSValue js_amrHost_cookieGet(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_UNDEFINED;
    const char* domain = JS_ToCString(ctx, argv[0]);
    const char* name = JS_ToCString(ctx, argv[1]);
    std::string val;
    if (domain && name) {
        val = net::Fetcher::Instance().Jar().Get(domain, name);
    }
    if (domain) JS_FreeCString(ctx, domain);
    if (name) JS_FreeCString(ctx, name);
    return JS_NewString(ctx, val.c_str());
}

static JSValue js_amrHost_cookieSet(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_UNDEFINED;
    const char* domain = JS_ToCString(ctx, argv[0]);
    const char* name = JS_ToCString(ctx, argv[1]);
    const char* value = JS_ToCString(ctx, argv[2]);
    const char* path = (argc >= 4) ? JS_ToCString(ctx, argv[3]) : nullptr;
    int64_t expires = 0;
    if (argc >= 5) JS_ToInt64(ctx, &expires, argv[4]);

    if (domain && name && value) {
        net::Cookie c;
        c.domain = domain;
        c.name = name;
        c.value = value;
        c.path = path ? path : "/";
        c.expires = expires;
        net::Fetcher::Instance().Jar().Set(c);
    }

    if (domain) JS_FreeCString(ctx, domain);
    if (name) JS_FreeCString(ctx, name);
    if (value) JS_FreeCString(ctx, value);
    if (path) JS_FreeCString(ctx, path);
    return JS_UNDEFINED;
}

static JSValue js_amrHost_log(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc > 0) {
        const char* s = JS_ToCString(ctx, argv[0]);
        if (s) {
            log::Write(L"[AMR JS] %ls", FromUtf8(s).c_str());
            JS_FreeCString(ctx, s);
        }
    }
    return JS_UNDEFINED;
}

static JSValue js_amrHost_atob(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_NewString(ctx, "");
    size_t sLen = 0;
    const char* s = JS_ToCStringLen(ctx, &sLen, argv[0]);
    if (!s) return JS_NewString(ctx, "");
    std::string res = Base64Decode(std::string(s, sLen));
    JS_FreeCString(ctx, s);

    // atob returns binary string where each character is a byte (Latin-1)
    std::string utf8Str;
    utf8Str.reserve(res.size() * 2);
    for (unsigned char c : res) {
        if (c < 0x80) {
            utf8Str.push_back(static_cast<char>(c));
        } else {
            utf8Str.push_back(static_cast<char>(0xC0 | (c >> 6)));
            utf8Str.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return JS_NewStringLen(ctx, utf8Str.data(), utf8Str.size());
}

static JSValue js_amrHost_btoa(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_NewString(ctx, "");
    size_t sLen = 0;
    const char* s = JS_ToCStringLen(ctx, &sLen, argv[0]);
    if (!s) return JS_NewString(ctx, "");

    // Decode UTF-8 bytes back to Latin-1 character values if needed
    std::vector<unsigned char> bytes;
    bytes.reserve(sLen);
    for (size_t i = 0; i < sLen; ) {
        unsigned char c = static_cast<unsigned char>(s[i++]);
        if (c < 0x80) {
            bytes.push_back(c);
        } else if ((c & 0xE0) == 0xC0 && i < sLen) {
            unsigned char c2 = static_cast<unsigned char>(s[i++]);
            bytes.push_back(static_cast<unsigned char>(((c & 0x1F) << 6) | (c2 & 0x3F)));
        } else {
            bytes.push_back(c);
        }
    }
    JS_FreeCString(ctx, s);

    std::string res = Base64Encode(bytes.data(), bytes.size());
    return JS_NewStringLen(ctx, res.data(), res.size());
}

// DOM methods

static JSValue js_amrHost_parse(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_bridgeInstance || argc < 1) return JS_NewInt32(ctx, -1);
    const char* html = JS_ToCString(ctx, argv[0]);
    if (!html) return JS_NewInt32(ctx, -1);

    auto doc = std::make_unique<HtmlDocument>();
    doc->Parse(html);
    JS_FreeCString(ctx, html);

    int id = s_bridgeInstance->RegisterDoc(std::move(doc));
    return JS_NewInt32(ctx, id);
}

static JSValue js_amrHost_releaseDoc(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_bridgeInstance || argc < 1) return JS_UNDEFINED;
    int32_t docId = -1;
    JS_ToInt32(ctx, &docId, argv[0]);
    s_bridgeInstance->ReleaseDoc(docId);
    return JS_UNDEFINED;
}

static JSValue js_amrHost_select(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_bridgeInstance || argc < 3) return JS_NewArray(ctx);
    int32_t docId = -1, nodeId = -1;
    JS_ToInt32(ctx, &docId, argv[0]);
    JS_ToInt32(ctx, &nodeId, argv[1]);
    const char* sel = JS_ToCString(ctx, argv[2]);
    if (!sel) return JS_NewArray(ctx);

    DocContext* d = s_bridgeInstance->GetDoc(docId);
    if (!d || nodeId < 0 || nodeId >= (int)d->nodes.size()) {
        JS_FreeCString(ctx, sel);
        return JS_NewArray(ctx);
    }

    lxb_dom_node* target = d->nodes[nodeId];
    std::vector<HtmlNode> matched = d->doc->SelectFrom(target, sel);
    JS_FreeCString(ctx, sel);

    JSValue arr = JS_NewArray(ctx);
    for (uint32_t i = 0; i < (uint32_t)matched.size(); ++i) {
        int nid = d->GetOrAddNode(matched[i].Raw());
        JS_SetPropertyUint32(ctx, arr, i, JS_NewInt32(ctx, nid));
    }
    return arr;
}

static JSValue js_amrHost_attr(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_bridgeInstance || argc < 3) return JS_UNDEFINED;
    int32_t docId = -1, nodeId = -1;
    JS_ToInt32(ctx, &docId, argv[0]);
    JS_ToInt32(ctx, &nodeId, argv[1]);
    const char* attrName = JS_ToCString(ctx, argv[2]);
    if (!attrName) return JS_UNDEFINED;

    DocContext* d = s_bridgeInstance->GetDoc(docId);
    if (!d || nodeId < 0 || nodeId >= (int)d->nodes.size()) {
        JS_FreeCString(ctx, attrName);
        return JS_UNDEFINED;
    }

    HtmlNode n(d->doc.get(), d->nodes[nodeId]);
    if (!n.HasAttr(attrName)) {
        JS_FreeCString(ctx, attrName);
        return JS_UNDEFINED;
    }
    std::string val = n.Attr(attrName);
    JS_FreeCString(ctx, attrName);
    return JS_NewString(ctx, val.c_str());
}

static JSValue js_amrHost_text(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_bridgeInstance || argc < 2) return JS_NewString(ctx, "");
    int32_t docId = -1, nodeId = -1;
    JS_ToInt32(ctx, &docId, argv[0]);
    JS_ToInt32(ctx, &nodeId, argv[1]);

    DocContext* d = s_bridgeInstance->GetDoc(docId);
    if (!d || nodeId < 0 || nodeId >= (int)d->nodes.size()) {
        return JS_NewString(ctx, "");
    }

    HtmlNode n(d->doc.get(), d->nodes[nodeId]);
    std::string t = n.Text();
    return JS_NewString(ctx, t.c_str());
}

static JSValue js_amrHost_html(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_bridgeInstance || argc < 2) return JS_NewString(ctx, "");
    int32_t docId = -1, nodeId = -1;
    JS_ToInt32(ctx, &docId, argv[0]);
    JS_ToInt32(ctx, &nodeId, argv[1]);

    DocContext* d = s_bridgeInstance->GetDoc(docId);
    if (!d || nodeId < 0 || nodeId >= (int)d->nodes.size()) {
        return JS_NewString(ctx, "");
    }

    HtmlNode n(d->doc.get(), d->nodes[nodeId]);
    std::string h = n.Html();
    return JS_NewString(ctx, h.c_str());
}

static JSValue js_amrHost_tagName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_bridgeInstance || argc < 2) return JS_NewString(ctx, "");
    int32_t docId = -1, nodeId = -1;
    JS_ToInt32(ctx, &docId, argv[0]);
    JS_ToInt32(ctx, &nodeId, argv[1]);

    DocContext* d = s_bridgeInstance->GetDoc(docId);
    if (!d || nodeId < 0 || nodeId >= (int)d->nodes.size()) {
        return JS_NewString(ctx, "");
    }

    HtmlNode n(d->doc.get(), d->nodes[nodeId]);
    std::string tag = n.TagName();
    return JS_NewString(ctx, tag.c_str());
}

static JSValue js_amrHost_children(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_bridgeInstance || argc < 2) return JS_NewArray(ctx);
    int32_t docId = -1, nodeId = -1;
    JS_ToInt32(ctx, &docId, argv[0]);
    JS_ToInt32(ctx, &nodeId, argv[1]);

    DocContext* d = s_bridgeInstance->GetDoc(docId);
    if (!d || nodeId < 0 || nodeId >= (int)d->nodes.size()) {
        return JS_NewArray(ctx);
    }

    HtmlNode n(d->doc.get(), d->nodes[nodeId]);
    std::vector<HtmlNode> ch = n.Children();
    JSValue arr = JS_NewArray(ctx);
    for (uint32_t i = 0; i < (uint32_t)ch.size(); ++i) {
        int nid = d->GetOrAddNode(ch[i].Raw());
        JS_SetPropertyUint32(ctx, arr, i, JS_NewInt32(ctx, nid));
    }
    return arr;
}

static JSValue js_amrHost_parent(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_bridgeInstance || argc < 2) return JS_NewInt32(ctx, -1);
    int32_t docId = -1, nodeId = -1;
    JS_ToInt32(ctx, &docId, argv[0]);
    JS_ToInt32(ctx, &nodeId, argv[1]);

    DocContext* d = s_bridgeInstance->GetDoc(docId);
    if (!d || nodeId < 0 || nodeId >= (int)d->nodes.size()) {
        return JS_NewInt32(ctx, -1);
    }

    HtmlNode p = HtmlNode(d->doc.get(), d->nodes[nodeId]).Parent();
    return JS_NewInt32(ctx, p.IsValid() ? d->GetOrAddNode(p.Raw()) : -1);
}

static JSValue js_amrHost_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_bridgeInstance || argc < 2) return JS_NewInt32(ctx, -1);
    int32_t docId = -1, nodeId = -1;
    JS_ToInt32(ctx, &docId, argv[0]);
    JS_ToInt32(ctx, &nodeId, argv[1]);

    DocContext* d = s_bridgeInstance->GetDoc(docId);
    if (!d || nodeId < 0 || nodeId >= (int)d->nodes.size()) {
        return JS_NewInt32(ctx, -1);
    }

    HtmlNode n = HtmlNode(d->doc.get(), d->nodes[nodeId]).Next();
    return JS_NewInt32(ctx, n.IsValid() ? d->GetOrAddNode(n.Raw()) : -1);
}

static JSValue js_amrHost_remove(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    int32_t docId = -1, nodeId = -1;
    if (!s_bridgeInstance || argc < 2) return JS_UNDEFINED;
    JS_ToInt32(ctx, &docId, argv[0]);
    JS_ToInt32(ctx, &nodeId, argv[1]);
    auto* d = s_bridgeInstance->GetDoc(docId);
    if (d && nodeId > 0 && nodeId < (int)d->nodes.size()) lxb_dom_node_remove(d->nodes[nodeId]);
    return JS_UNDEFINED;
}

static JSValue js_amrHost_prev(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_bridgeInstance || argc < 2) return JS_NewInt32(ctx, -1);
    int32_t docId = -1, nodeId = -1;
    JS_ToInt32(ctx, &docId, argv[0]);
    JS_ToInt32(ctx, &nodeId, argv[1]);

    DocContext* d = s_bridgeInstance->GetDoc(docId);
    if (!d || nodeId < 0 || nodeId >= (int)d->nodes.size()) {
        return JS_NewInt32(ctx, -1);
    }

    HtmlNode p = HtmlNode(d->doc.get(), d->nodes[nodeId]).Prev();
    return JS_NewInt32(ctx, p.IsValid() ? d->GetOrAddNode(p.Raw()) : -1);
}

void JsBridge::Install(JsEngine& engine) {
    s_bridgeInstance = this;
    JSContext* ctx = engine.Ctx();
    JSValue global = JS_GetGlobalObject(ctx);

    JSValue host = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, host, "request", JS_NewCFunction(ctx, js_amrHost_request, "request", 2));
    JS_SetPropertyStr(ctx, host, "cookieGet", JS_NewCFunction(ctx, js_amrHost_cookieGet, "cookieGet", 2));
    JS_SetPropertyStr(ctx, host, "cookieSet", JS_NewCFunction(ctx, js_amrHost_cookieSet, "cookieSet", 5));
    JS_SetPropertyStr(ctx, host, "log", JS_NewCFunction(ctx, js_amrHost_log, "log", 1));
    JS_SetPropertyStr(ctx, host, "atob", JS_NewCFunction(ctx, js_amrHost_atob, "atob", 1));
    JS_SetPropertyStr(ctx, host, "btoa", JS_NewCFunction(ctx, js_amrHost_btoa, "btoa", 1));

    JS_SetPropertyStr(ctx, host, "parse", JS_NewCFunction(ctx, js_amrHost_parse, "parse", 1));
    JS_SetPropertyStr(ctx, host, "releaseDoc", JS_NewCFunction(ctx, js_amrHost_releaseDoc, "releaseDoc", 1));
    JS_SetPropertyStr(ctx, host, "select", JS_NewCFunction(ctx, js_amrHost_select, "select", 3));
    JS_SetPropertyStr(ctx, host, "attr", JS_NewCFunction(ctx, js_amrHost_attr, "attr", 3));
    JS_SetPropertyStr(ctx, host, "text", JS_NewCFunction(ctx, js_amrHost_text, "text", 2));
    JS_SetPropertyStr(ctx, host, "html", JS_NewCFunction(ctx, js_amrHost_html, "html", 2));
    JS_SetPropertyStr(ctx, host, "tagName", JS_NewCFunction(ctx, js_amrHost_tagName, "tagName", 2));
    JS_SetPropertyStr(ctx, host, "children", JS_NewCFunction(ctx, js_amrHost_children, "children", 2));
    JS_SetPropertyStr(ctx, host, "parent", JS_NewCFunction(ctx, js_amrHost_parent, "parent", 2));
    JS_SetPropertyStr(ctx, host, "next", JS_NewCFunction(ctx, js_amrHost_next, "next", 2));
    JS_SetPropertyStr(ctx, host, "prev", JS_NewCFunction(ctx, js_amrHost_prev, "prev", 2));
    JS_SetPropertyStr(ctx, host, "remove", JS_NewCFunction(ctx, js_amrHost_remove, "remove", 2));

    JS_SetPropertyStr(ctx, global, "amrHost", host);
    JS_FreeValue(ctx, global);
}

} // namespace gm::engine
