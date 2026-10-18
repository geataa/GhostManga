#pragma once

namespace gm::engine {

inline constexpr const char* kPolyfillsJs = R"JS(
// Global console
const __formatArg = a => {
    if (a === undefined) return 'undefined';
    if (a === null) return 'null';
    if (a instanceof Error || (typeof a === 'object' && (a.message || a.stack))) {
        return String(a.message || a) + (a.stack ? ('\n' + a.stack) : '');
    }
    if (typeof a === 'object') {
        try { return JSON.stringify(a); } catch(e) { return String(a); }
    }
    return String(a);
};
globalThis.console = {
    log: (...args) => amrHost.log(args.map(__formatArg).join(' ')),
    warn: (...args) => amrHost.log('[WARN] ' + args.map(__formatArg).join(' ')),
    error: (...args) => amrHost.log('[ERROR] ' + args.map(__formatArg).join(' ')),
    info: (...args) => amrHost.log(args.map(__formatArg).join(' ')),
    debug: (...args) => amrHost.log(args.map(__formatArg).join(' '))
};

globalThis.atob = (s) => amrHost.atob(s);
globalThis.btoa = (s) => amrHost.btoa(s);

const __timers = new Map();
let __timerId = 0;
globalThis.setTimeout = (fn, delay = 0, ...args) => {
    const id = ++__timerId;
    __timers.set(id, {fn, args, at: Date.now() + Math.max(0, Number(delay) || 0)});
    return id;
};
globalThis.clearTimeout = id => __timers.delete(id);
globalThis.__pumpTimers = () => {
    for (const [id, timer] of __timers) {
        if (timer.at <= Date.now()) { __timers.delete(id); timer.fn(...timer.args); }
    }
};

// Global fetch
globalThis.fetch = async function(url, options = {}) {
    const res = await amrHost.request(url, options);
    return {
        ok: res.status >= 200 && res.status < 300,
        status: res.status,
        headers: {
            get: (name) => {
                if (!res.headers) return null;
                const lower = name.toLowerCase();
                for (const k of Object.keys(res.headers)) {
                    if (k.toLowerCase() === lower) return res.headers[k];
                }
                return null;
            }
        },
        text: async () => res.text || "",
        json: async () => JSON.parse(res.text || "null")
    };
};

// browser.cookies polyfill for webextension-polyfill
globalThis.browser = {
    cookies: {
        get: async (details) => {
            let domain = details.domain || "";
            if (!domain && details.url) {
                try { domain = new URL(details.url).hostname; } catch(e){}
            }
            const val = amrHost.cookieGet(domain, details.name);
            return val ? { name: details.name, value: val } : null;
        },
        set: async (details) => {
            let domain = details.domain || "";
            if (!domain && details.url) {
                try { domain = new URL(details.url).hostname; } catch(e){}
            }
            amrHost.cookieSet(domain, details.name, details.value, details.path || "/", details.expirationDate || 0);
            return { name: details.name, value: details.value };
        }
    }
};

// AbortSignal.timeout polyfill
if (!globalThis.AbortSignal) globalThis.AbortSignal = {};
globalThis.AbortSignal.timeout = function(ms) { return {}; };

// URLSearchParams polyfill
class URLSearchParamsPolyfill {
    constructor(init) {
        this.params = [];
        if (typeof init === 'string') {
            if (init.startsWith('?')) init = init.slice(1);
            if (init.length > 0) {
                const pairs = init.split('&');
                for (const pair of pairs) {
                    const idx = pair.indexOf('=');
                    if (idx !== -1) {
                        this.append(decodeURIComponent(pair.slice(0, idx).replace(/\+/g, ' ')), decodeURIComponent(pair.slice(idx + 1).replace(/\+/g, ' ')));
                    } else {
                        this.append(decodeURIComponent(pair), '');
                    }
                }
            }
        } else if (typeof init === 'object' && init !== null) {
            for (const [k, v] of Object.entries(init)) {
                this.append(k, String(v));
            }
        }
    }
    append(name, value) { this.params.push([String(name), String(value)]); }
    entries() { return this.params[Symbol.iterator](); }
    [Symbol.iterator]() { return this.entries(); }
    get(name) {
        const found = this.params.find(p => p[0] === name);
        return found ? found[1] : null;
    }
    set(name, value) {
        const idx = this.params.findIndex(p => p[0] === name);
        if (idx !== -1) this.params[idx][1] = String(value);
        else this.append(name, value);
    }
    toString() {
        return this.params.map(p => encodeURIComponent(p[0]) + '=' + encodeURIComponent(p[1])).join('&');
    }
}
globalThis.URLSearchParams = URLSearchParamsPolyfill;

// URL polyfill
class URLPolyfill {
    constructor(url, base) {
        let full = String(url || "");
        if (full.startsWith('//')) {
            full = 'https:' + full;
        } else if (base) {
            let b = String(base);
            if (!b.endsWith('/')) b += '/';
            if (full.startsWith('/')) {
                const m = b.match(/^(https?:\/\/[^\/]+)/i);
                full = (m ? m[1] : b) + full;
            } else if (!full.match(/^https?:\/\//i)) {
                full = b + full;
            }
        }
        if (full.startsWith('//')) {
            full = 'https:' + full;
        }
        const m = full.match(/^(https?:)\/\/([^\/?#:]+)(?::(\d+))?([^?#]*)(?:\?([^#]*))?(?:#(.*))?/i);
        if (!m) {
            this.href = full;
            this.protocol = "https:";
            this.hostname = "";
            this.host = "";
            this.port = "";
            this.pathname = full;
            this.search = "";
            this.origin = "";
            this.searchParams = new URLSearchParamsPolyfill("");
            return;
        }
        this.protocol = m[1].toLowerCase();
        this.hostname = m[2].toLowerCase();
        this.port = m[3] || "";
        this.host = this.port ? this.hostname + ":" + this.port : this.hostname;
        this.pathname = m[4] || "/";
        this.search = m[5] ? "?" + m[5] : "";
        this.hash = m[6] ? "#" + m[6] : "";
        this.origin = this.protocol + "//" + this.host;
        this.href = this.origin + this.pathname + this.search + this.hash;
        this.searchParams = new URLSearchParamsPolyfill(this.search);
    }
}
globalThis.URL = URLPolyfill;

// MirrorHelper class
class MirrorHelper {
    constructor(options = {}) {
        this.options = options;
        this.crypto = globalThis.CryptoJS;
    }

    async loadPage(url, options = {}) {
        const res = await this.request(url, options);
        let text = await res.text();
        return text;
    }

    async loadJson(url, options = {}) {
        const res = await this.request(url, options);
        try {
            return await res.json();
        } catch (e) {
            const text = await res.text();
            throw new Error(/^\s*</.test(text) ? 'Kaynak JSON yerine HTML sayfasi dondurdu; site API yapisi degismis veya erisim sayfasi devrede' : 'Kaynak gecersiz JSON yaniti dondurdu');
        }
    }

    request(url, options = {}) {
        url = String(url).trim();
        if (!/^https?:\/\//i.test(url) && /^[^/?\s]+\.[^/?\s]+/.test(url)) url = 'https://' + url;
        url = url.replace(/^(https?:\/\/[^/]+)\/{2,}/i, '$1/');
        const opts = {...options, headers: {...(options.headers || {})}};
        for (const name of Object.keys(opts.headers))
            if (opts.headers[name] === undefined || opts.headers[name] === null) delete opts.headers[name];
        if (opts.post) opts.method = 'POST';
        if (opts.data !== undefined) {
            let jsonBody = Object.entries(opts.headers).some(([k,v]) => k.toLowerCase() === 'content-type' && String(v).includes('application/json'));
            if (typeof opts.data === 'string' && /^[\s]*[\[{]/.test(opts.data)) {
                try { JSON.parse(opts.data); jsonBody = true; } catch(e) {}
            }
            const data = typeof opts.data === 'string' ? opts.data : jsonBody ? JSON.stringify(opts.data) :
                opts.data instanceof URLSearchParams ? opts.data.toString() : new URLSearchParams(opts.data).toString();
            if (String(opts.method || 'GET').toUpperCase() === 'GET') {
                url += (String(url).includes('?') ? '&' : '?') + data;
            } else {
                opts.body = data;
                if (!Object.keys(opts.headers).some(k => k.toLowerCase() === 'content-type'))
                    opts.headers['Content-Type'] = jsonBody ? 'application/json' : 'application/x-www-form-urlencoded; charset=UTF-8';
            }
        }
        return fetch(url, opts);
    }

    getVariableFromScript(varname, sc) {
        if (!sc) return undefined;
        let res = undefined;
        const rx = new RegExp(
            "(var|let|const)\\s+" + varname + "\\s*=\\s*([0-9]+|\\\"|\\'|\\{|\\[|JSON\\s*\\.\\s*parse\\()",
            "gmi"
        );
        const match = rx.exec(sc);
        if (match) {
            const ind = match.index;
            const varchar = match[2];
            const start = sc.indexOf(varchar, ind) + 1;
            if (varchar.match(/[0-9]+/)) {
                res = Number(varchar);
            } else if (varchar === '"' || varchar === "'") {
                let found = false, curpos = start, prevbs = false;
                while (!found && curpos < sc.length) {
                    const c = sc.charAt(curpos++);
                    if (c === varchar && !prevbs) {
                        found = true;
                        break;
                    }
                    prevbs = (c === "\\");
                }
                res = sc.substring(start, curpos - 1);
            } else {
                let curpos = start + varchar.length - 1, openings = 1;
                const opening = varchar === "JSON.parse(" ? "(" : varchar;
                const opposite = varchar === "[" ? "]" : varchar === "{" ? "}" : ")";
                while (openings > 0 && curpos < sc.length) {
                    const c = sc.charAt(curpos++);
                    if (c === opening) openings++;
                    if (c === opposite) openings--;
                }
                let toparse = sc.substring(start - 1 + varchar.length - 1, curpos);
                if (toparse.match(/atob\s*\(/g)) {
                    const m = /(?:'|").*(?:'|")/g.exec(toparse);
                    if (m) toparse = atob(m[0].substring(1, m[0].length - 1));
                }
                try { res = JSON.parse(toparse); } catch(e){}
            }
        }
        return res;
    }

    async setCookie(details) {
        return browser.cookies.set(details);
    }

    async getCookie(details) {
        return browser.cookies.get(details);
    }

    getOption(option) {
        return this.options[option] || "";
    }

    setOptions(options) {
        this.options = options;
    }
}
globalThis.MirrorHelper = MirrorHelper;
globalThis.mirrorHelper = new MirrorHelper({});
)JS";

inline constexpr const char* kCheerioShimJs = R"JS(
// Cheerio shim backed by amrHost and Lexbor
(function() {
    class CheerioNode {
        constructor(docId, nodeId) {
            this._docId = docId;
            this._nodeId = nodeId;
        }
        attr(name) {
            return amrHost.attr(this._docId, this._nodeId, name);
        }
        text() {
            return amrHost.text(this._docId, this._nodeId);
        }
        html() {
            return amrHost.html(this._docId, this._nodeId);
        }
    }

    class CheerioCollection {
        constructor(docId, nodeIds = []) {
            this._docId = docId;
            this._nodeIds = nodeIds;
            this.length = nodeIds.length;
            for (let i = 0; i < nodeIds.length; ++i) {
                this[i] = new CheerioNode(docId, nodeIds[i]);
            }
        }

        attr(name) {
            if (this.length === 0) return undefined;
            return amrHost.attr(this._docId, this._nodeIds[0], name);
        }

        text() {
            if (this.length === 0) return "";
            let s = "";
            for (let i = 0; i < this.length; ++i) {
                s += amrHost.text(this._docId, this._nodeIds[i]);
            }
            return s;
        }

        html() {
            if (this.length === 0) return "";
            return amrHost.html(this._docId, this._nodeIds[0]);
        }

        val() {
            return this.attr('value') || "";
        }

        hasClass(className) {
            if (this.length === 0) return false;
            const cls = this.attr('class') || "";
            return cls.split(/\s+/).includes(className);
        }

        first() {
            return this.length > 0 ? new CheerioCollection(this._docId, [this._nodeIds[0]]) : new CheerioCollection(this._docId, []);
        }

        last() {
            return this.length > 0 ? new CheerioCollection(this._docId, [this._nodeIds[this.length - 1]]) : new CheerioCollection(this._docId, []);
        }

        eq(index) {
            const i = index < 0 ? this.length + index : index;
            return new CheerioCollection(this._docId, i >= 0 && i < this.length ? [this._nodeIds[i]] : []);
        }
        get(index) { return index === undefined ? this.toArray() : this[index < 0 ? this.length + index : index]; }
        toArray() { return Array.from({length: this.length}, (_, i) => this[i]); }
        [Symbol.iterator]() { return this.toArray()[Symbol.iterator](); }
        slice(start, end) { return new CheerioCollection(this._docId, this._nodeIds.slice(start, end)); }
        filter(selector) {
            return new CheerioCollection(this._docId, this._nodeIds.filter((id, i) => {
                const node = this[i];
                if (typeof selector === 'function') return !!selector.call(node, i, node);
                const parent = amrHost.parent(this._docId, id);
                return (amrHost.select(this._docId, parent < 0 ? 0 : parent, selector) || []).includes(id);
            }));
        }

        each(fn) {
            for (let i = 0; i < this.length; ++i) {
                const node = this[i];
                // In cheerio, `this` inside each is the raw element
                const ret = fn.call(node, i, node);
                if (ret === false) break;
            }
            return this;
        }

        find(selector) {
            const resultIds = [];
            for (let i = 0; i < this.length; ++i) {
                const ids = amrHost.select(this._docId, this._nodeIds[i], selector);
                if (Array.isArray(ids)) {
                    for (const id of ids) {
                        if (!resultIds.includes(id)) resultIds.push(id);
                    }
                }
            }
            return new CheerioCollection(this._docId, resultIds);
        }

        children(selector) {
            const resultIds = [];
            for (let i = 0; i < this.length; ++i) {
                const ids = amrHost.children(this._docId, this._nodeIds[i]);
                if (Array.isArray(ids)) resultIds.push(...ids);
            }
            const result = new CheerioCollection(this._docId, resultIds);
            return selector ? result.filter(selector) : result;
        }

        contents() {
            return this.children();
        }

        parent() {
            if (this.length === 0) return new CheerioCollection(this._docId, []);
            const pid = amrHost.parent(this._docId, this._nodeIds[0]);
            return pid >= 0 ? new CheerioCollection(this._docId, [pid]) : new CheerioCollection(this._docId, []);
        }

        next() {
            if (this.length === 0) return new CheerioCollection(this._docId, []);
            const nid = amrHost.next(this._docId, this._nodeIds[0]);
            return nid >= 0 ? new CheerioCollection(this._docId, [nid]) : new CheerioCollection(this._docId, []);
        }

        prev() {
            if (this.length === 0) return new CheerioCollection(this._docId, []);
            const pid = amrHost.prev(this._docId, this._nodeIds[0]);
            return pid >= 0 ? new CheerioCollection(this._docId, [pid]) : new CheerioCollection(this._docId, []);
        }

        remove() {
            for (const id of this._nodeIds) amrHost.remove(this._docId, id);
            return this;
        }
    }

    function createCheerioInstance(html) {
        const docId = amrHost.parse(html);

        function $(selector, context) {
            if (!selector) return new CheerioCollection(docId, []);

            // If selector is already a CheerioNode, wrap it
            if (selector instanceof CheerioNode) {
                return new CheerioCollection(selector._docId, [selector._nodeId]);
            }
            // If selector is a CheerioCollection, return it
            if (selector instanceof CheerioCollection) {
                return selector;
            }

            // If context is provided (e.g. $(selector, $(this)))
            let rootNodeId = 0;
            let currentDocId = docId;
            if (context instanceof CheerioNode) {
                rootNodeId = context._nodeId;
                currentDocId = context._docId;
            } else if (context instanceof CheerioCollection && context.length > 0) {
                rootNodeId = context._nodeIds[0];
                currentDocId = context._docId;
            }

            const ids = amrHost.select(currentDocId, rootNodeId, String(selector));
            return new CheerioCollection(currentDocId, ids || []);
        }

        $.root = () => new CheerioCollection(docId, [0]);
        $.text = () => $.root().text();
        $.html = () => $.root().html();
        return $;
    }

    globalThis.load = createCheerioInstance;
    globalThis.cheerio = { load: createCheerioInstance };
})();
)JS";

} // namespace gm::engine
