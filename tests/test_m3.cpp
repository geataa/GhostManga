#include <iostream>
#include <cassert>
#include "../src/net/CookieJar.h"
#include "../src/net/HttpClient.h"
#include "../src/net/Fetch.h"
#include "../src/engine/HtmlDocument.h"
#include "../src/engine/JsEngine.h"
#include "../src/engine/JsBridge.h"
#include "../src/engine/MirrorEngine.h"
#include "../src/core/Paths.h"
#include "../src/core/Log.h"

using namespace gm;

int main() {
    FILE* tr = fopen("test_trace.txt", "w");
    if (tr) { fprintf(tr, "START\n"); fflush(tr); }

    std::cout << "=============================================\n";
    std::cout << "  GhostManga M3 Automated Verification Tests\n";
    std::cout << "=============================================\n";

    if (tr) { fprintf(tr, "Before paths::Init\n"); fflush(tr); }
    std::wstring err;
    paths::Init(&err);
    if (tr) { fprintf(tr, "After paths::Init\n"); fflush(tr); }
    log::Init();
    if (tr) { fprintf(tr, "After log::Init\n"); fflush(tr); }

    // Test 1: CookieJar
    std::cout << "[TEST 1] Testing CookieJar RFC 6265 compliance...\n";
    {
        net::CookieJar jar;
        jar.SetCookie("example.com", "/manga", "session=xyz123; Path=/manga; Secure; HttpOnly");
        jar.SetCookie("sub.example.com", "/", "theme=dark; Domain=example.com");

        std::string hdr = jar.GetCookieHeader("example.com", "/manga/ch1", true);
        assert(hdr.find("session=xyz123") != std::string::npos);
        assert(hdr.find("theme=dark") != std::string::npos);

        std::string subHdr = jar.GetCookieHeader("sub.example.com", "/", false);
        assert(subHdr.find("theme=dark") != std::string::npos);
        assert(subHdr.find("session=xyz123") == std::string::npos); // Not matching path & secure
        std::cout << "  -> CookieJar: PASSED\n";
        if (tr) { fprintf(tr, "TEST 1 PASSED\n"); fflush(tr); }
    }

    // Test 2: HtmlDocument & Lexbor
    std::cout << "[TEST 2] Testing HtmlDocument & CSS Selectors (Lexbor)...\n";
    {
        engine::HtmlDocument doc;
        const char* html = R"HTML(
            <html>
            <body>
                <div class="manga-list">
                    <a href="/manga/1" class="item">One Piece</a>
                    <a href="/manga/2" class="item">Naruto</a>
                    <a href="/manga/3" class="item special">Bleach</a>
                </div>
                <div class="chapters">
                    <ul>
                        <li><a href="/ch/100">Chapter 100</a></li>
                        <li><a href="/ch/99">Chapter 99</a></li>
                    </ul>
                </div>
            </body>
            </html>
        )HTML";

        if (tr) { fprintf(tr, "T2: parsing html\n"); fflush(tr); }
        bool ok = doc.Parse(html);
        if (tr) { fprintf(tr, "T2: parsed ok=%d\n", ok ? 1 : 0); fflush(tr); }
        assert(ok);

        if (tr) { fprintf(tr, "T2: selecting .manga-list a.item\n"); fflush(tr); }
        auto items = doc.Select(".manga-list a.item");
        if (tr) { fprintf(tr, "T2: items.size=%zu\n", items.size()); fflush(tr); }
        assert(items.size() == 3);
        assert(items[0].Text() == "One Piece");
        assert(items[0].Attr("href") == "/manga/1");
        assert(items[2].Text() == "Bleach");

        // jQuery pseudo :first
        if (tr) { fprintf(tr, "T2: selecting :first\n"); fflush(tr); }
        auto firstItem = doc.Select(".manga-list a.item:first");
        if (tr) { fprintf(tr, "T2: firstItem.size=%zu\n", firstItem.size()); fflush(tr); }
        assert(firstItem.size() == 1);
        assert(firstItem[0].Text() == "One Piece");

        // jQuery pseudo :last
        if (tr) { fprintf(tr, "T2: selecting :last\n"); fflush(tr); }
        auto lastItem = doc.Select(".manga-list a.item:last");
        if (tr) { fprintf(tr, "T2: lastItem.size=%zu\n", lastItem.size()); fflush(tr); }
        assert(lastItem.size() == 1);
        assert(lastItem[0].Text() == "Bleach");

        // jQuery pseudo :contains
        if (tr) { fprintf(tr, "T2: selecting :contains\n"); fflush(tr); }
        auto containsBleach = doc.Select("a:contains('Bleach')");
        if (tr) { fprintf(tr, "T2: containsBleach.size=%zu\n", containsBleach.size()); fflush(tr); }
        assert(containsBleach.size() == 1);
        assert(containsBleach[0].Text() == "Bleach");

        std::cout << "  -> HtmlDocument & Selectors: PASSED\n" << std::flush;
        if (tr) { fprintf(tr, "TEST 2 PASSED\n"); fflush(tr); }
    }

    // Test 3: JsEngine & JsBridge
    std::cout << "[TEST 3] Testing JsEngine & JsBridge (QuickJS)...\n";
    {
        engine::JsEngine js;
        bool ok = js.Init();
        assert(ok);

        engine::JsBridge bridge;
        bridge.Install(js);

        JSValue r = js.Eval("1 + 2");
        assert(js.ToStr(r) == "3");
        JS_FreeValue(js.Ctx(), r);

        // Test amrHost.atob / btoa
        JSValue rBtoa = js.Eval("amrHost.btoa('GhostManga')");
        assert(js.ToStr(rBtoa) == "R2hvc3RNYW5nYQ==");
        JS_FreeValue(js.Ctx(), rBtoa);

        JSValue rAtob = js.Eval("amrHost.atob('R2hvc3RNYW5nYQ==')");
        assert(js.ToStr(rAtob) == "GhostManga");
        JS_FreeValue(js.Ctx(), rAtob);

        // Test amrHost.parse & select
        JSValue rDom = js.Eval(R"JS(
            const docId = amrHost.parse('<div class="box"><span>Hello Lexbor</span></div>');
            const nodes = amrHost.select(docId, 0, '.box span');
            const txt = amrHost.text(docId, nodes[0]);
            amrHost.releaseDoc(docId);
            txt;
        )JS");
        assert(js.ToStr(rDom) == "Hello Lexbor");
        JS_FreeValue(js.Ctx(), rDom);

        std::cout << "  -> JsEngine & JsBridge: PASSED\n";
        if (tr) { fprintf(tr, "TEST 3 PASSED\n"); fflush(tr); }
    }

    // Test 4: MirrorEngine & 144 Mirror Catalog
    std::cout << "[TEST 4] Testing MirrorEngine & AMR Mirrors Catalog...\n";
    {
        auto& me = engine::MirrorEngine::Instance();
        bool ok = me.Init();
        assert(ok);

        auto mirrors = me.GetMirrors();
        std::cout << "  -> Total mirrors loaded: " << mirrors.size() << "\n";
        if (tr) { fprintf(tr, "Mirrors count: %zu\n", mirrors.size()); fflush(tr); }
        assert(mirrors.size() >= 100);

        bool foundMangaPill = false;
        bool foundSeriManga = false;
        for (const auto& m : mirrors) {
            if (m.name == "Manga Pill") foundMangaPill = true;
            if (m.name == "Seri Manga") foundSeriManga = true;
        }
        assert(foundMangaPill);
        assert(foundSeriManga);
        std::cout << "  -> Found expected mirrors ('Manga Pill', 'Seri Manga'): PASSED\n";
        if (tr) { fprintf(tr, "TEST 4 PASSED\n"); fflush(tr); }
    }

    std::cout << "\nALL VERIFICATION TESTS COMPLETED SUCCESSFULLY!\n";
    if (tr) { fprintf(tr, "ALL FINISHED\n"); fclose(tr); }
    return 0;
}
