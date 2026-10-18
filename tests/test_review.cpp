#include <windows.h>
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include <condition_variable>
#include "engine/Library.h"
#include "engine/AmrImport.h"
#include "engine/MirrorEngine.h"
#include "engine/Scheduler.h"
#include "engine/ChapterLoader.h"
#include "browser/WebViewHost.h"
#include "browser/BrowserImageBuffer.h"
#include "net/Fetch.h"
#include "core/Paths.h"
#include "core/Log.h"
#include "core/FileIo.h"
#include "core/Json.h"

using namespace gm;

static std::atomic<bool> searchGateEntered{false};
static JSValue SearchGate(JSContext*, JSValueConst, int, JSValueConst*) {
    searchGateEntered = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    return JS_UNDEFINED;
}

static void Eval(const std::string& script) {
    auto& js = engine::MirrorEngine::Instance().Engine();
    auto value = js.Eval(script, "<review-test>");
    assert(!JS_IsException(value));
    JS_FreeValue(js.Ctx(), value);
    js.RunJobs();
}

static void TestLibrary() {
    const auto path = paths::Join(paths::DataDir(), L"review-library.json");
    DeleteFileW(path.c_str());
    DeleteFileW((path + L".bak").c_str());
    Library lib;
    lib.Load(path);
    Series a;
    a.name = "Test 123"; a.mirror = "First"; a.url = "https://first.invalid/manga/test"; a.language = "tr";
    a.key = amr::MangaKey(a.mirror, a.url, a.language);
    assert(lib.AddSeries(a));
    Series b = a; b.mirror = "Second"; b.url = "https://second.invalid/manga/test";
    b.key = amr::MangaKey(b.mirror, b.url, b.language);
    assert(lib.AddSeries(b));
    assert(lib.FindSource("First", a.url + "/#bookmark") != nullptr);
    assert(lib.FindSource("Third", "https://third.invalid/manga/test") == nullptr);
    assert(!IsCaughtUp(*lib.FindSeries(a.key))); // Unknown latest stays light.
    assert(!lib.MarkReadUpToLatest(a.key)); // Unknown latest is not success.
    const std::vector<Chapter> source = {{"Volume 1 Chapter 1", "https://first.invalid/chapter/uuid-a"}, {"Volume 1 Chapter 2", "https://first.invalid/chapter/uuid-b"}};
    assert(lib.StoreChapters(a.key, source, 100));
    assert(lib.FindSeries(a.key)->chapters.front().title == "Volume 1 Chapter 2");
    assert(lib.FindSeries(a.key)->upts == 0); // First scan is baseline, not a new release.
    assert(lib.MarkReadUpToLatest(a.key));
    assert(IsCaughtUp(*lib.FindSeries(a.key)));
    assert(lib.FindSeries(b.key)->lastChapterUrl.empty()); // Do not update another mirror with identical path.
    assert(lib.MarkLastRead(a.key, source[0].url, source[0].title));
    assert(lib.FindSeries(a.key)->lastChapterUrl == source[1].url); // Re-reading older chapter does not lower progress.
    assert(lib.ResetReadingProgress(a.key));
    assert(!IsCaughtUp(*lib.FindSeries(a.key)));
    assert(lib.FindSeries(a.key)->lastChapterUrl.empty());
    assert(lib.FindSeries(a.key)->lastReadTs == 0);
    assert(lib.MarkLastRead(a.key, source[0].url, source[0].title));
    auto next = source;
    next.push_back({"Volume 1 Chapter 3", "https://first.invalid/chapter/uuid-c"});
    assert(lib.StoreChapters(a.key, next, 200));
    assert(!IsCaughtUp(*lib.FindSeries(a.key))); // New release makes a completed manga light.
    assert(lib.FindSeries(a.key)->upts == 200);
    assert(lib.StoreChapters(a.key, next, 300));
    assert(lib.FindSeries(a.key)->upts == 200); // Poll time is not release time.
    assert(lib.FindSeries(a.key)->lastChapterUrl == source[0].url);
    Library loaded;
    loaded.Load(path);
    assert(loaded.FindSeries(a.key)->chapters.size() == 3);
    assert(loaded.FindSeries(a.key)->lastCheckedTs == 300);
    assert(loaded.FindSeries(a.key)->lastChapterName == source[0].title);
    const Series before = *loaded.FindSeries(a.key);
    const auto rev = loaded.Revision();
    HANDLE lock = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    assert(lock != INVALID_HANDLE_VALUE);
    assert(!loaded.ResetReadingProgress(a.key));
    assert(*loaded.FindSeries(a.key) == before);
    assert(loaded.Revision() == rev);
    assert(!loaded.RemoveSeries(a.key));
    assert(*loaded.FindSeries(a.key) == before && loaded.Revision() == rev);
    assert(!loaded.ClearAll());
    assert(loaded.All().size() == 2 && loaded.Revision() == rev);
    Series c = a; c.url = "https://first.invalid/new"; c.key = amr::MangaKey(c.mirror,c.url,c.language);
    assert(!loaded.AddSeries(c));
    assert(loaded.All().size() == 2 && loaded.Revision() == rev);
    CloseHandle(lock);
    amr::ImportData import;
    assert(amr::ParseExport(R"({"mangas":[{"m":"First","n":"Test","u":"https://first.invalid/test","l":"https://first.invalid/c1","cn":"Chapter 9"}]})", import) == amr::ImportError::None);
    assert(import.series[0].lastChapterName == "Chapter 9");
    assert(ChapterNumber("Volume 2 Chapter 38.5") == 38.5);
    assert(ChapterNumber("", "https://test.invalid/title/123abc") < 0);
    std::cout << "Library persistence, mirror identity, progress and failed-write rollback: PASS\n";
}

static void TestMirrors() {
    auto& mirrors = engine::MirrorEngine::Instance();
    assert(mirrors.Init());
    const auto names = mirrors.GetMirrors();
    assert(names.size() > 100);
    Eval(R"JS(
        for (const source of __loader.mirrors) {
            if (source.options && Object.hasOwn(source.options,'base_url') && !source.options.base_url) throw new Error('Missing base URL');
        }
        const $iter = cheerio.load('<a>first</a><a>second</a>');
        if ([...$iter('a')].length !== 2) throw new Error('Cheerio iteration missing');
    )JS");
    Eval(R"JS(
        globalThis.__nativeRequest = amrHost.request;
        amrHost.request = async () => { throw new Error('fixture: connection refused'); };
    )JS");
    // Run all registered source parsers on separate worker stacks. A bad source
    // must be isolated as a diagnostic rather than terminate the process.
    int count = 0;
    for (const auto& mirror : names) {
        if (mirror.disabled) continue;
        std::thread worker([&]() { mirrors.SearchManga(mirror.name, "One Piece"); });
        worker.join();
        ++count;
    }
    assert(!mirrors.GetSourceStatuses().empty());
    Eval(R"JS(
        amrHost.request = globalThis.__nativeRequest;
        globalThis.__savedJson = mirrorHelper.loadJson;
        mirrorHelper.loadJson = async () => ({data:[{id:'fixture-title',attributes:{title:{ja:'One Piece &amp; Friends'}}}]});
    )JS");
    const auto result = mirrors.SearchManga("MangaDex V5", "One Piece");
    assert(result.size() == 1);
    assert(result[0].title == "One Piece & Friends"); // Cheerio API .text() and console.debug.
    Eval(R"JS(
        let page = 0;
        mirrorHelper.loadJson = async () => ({limit:1,offset:page,total:2,data:[{
            id:'00000000-0000-4000-8000-'+String(++page).padStart(12,'0'), attributes:{chapter:String(3-page),translatedLanguage:'en',externalUrl:null,publishAt:'2026-01-01',title:'Fixture'}
        }]});
    )JS");
    const auto dexChapters = mirrors.GetChapters("MangaDex V5", "https://mangadex.org/title/fixture");
    assert(dexChapters.size() == 2); // Language dictionary + paginated setTimeout.
    Eval(R"JS(
        amrHost.request = async (url, options) => {
            if (options.method !== 'POST' || options.body !== 'action=search&title=One%20Piece' || !options.headers['Content-Type'])
                throw new Error('POST form options were lost');
            return {status:200,text:'{}'};
        };
        globalThis.__postCheck = mirrorHelper.request('https://test.invalid', {post:true,data:new URLSearchParams({action:'search',title:'One Piece'})});
    )JS");
    mirrors.Engine().RunJobs();
    auto postPromise = mirrors.Engine().Eval("globalThis.__postCheck", "<post-test>");
    assert(JS_PromiseState(mirrors.Engine().Ctx(), postPromise) == JS_PROMISE_FULFILLED);
    JS_FreeValue(mirrors.Engine().Ctx(), postPromise);
    Eval(R"JS(amrHost.request = globalThis.__nativeRequest;)JS");
    Eval(R"JS(
        amrHost.request = async (url, options) => {
            if (url !== 'https://test.invalid/graphql' || options.body !== '{"query":"test"}' || options.headers['Content-Type'] !== 'application/json')
                throw new Error('JSON options were lost');
            return {status:200,text:'{}'};
        };
        globalThis.__jsonCheck = mirrorHelper.request(' test.invalid/graphql ', {method:'POST',data:'{"query":"test"}'});
    )JS");
    auto jsonPromise = mirrors.Engine().Eval("globalThis.__jsonCheck", "<json-test>");
    assert(JS_PromiseState(mirrors.Engine().Ctx(), jsonPromise) == JS_PROMISE_FULFILLED);
    JS_FreeValue(mirrors.Engine().Ctx(), jsonPromise);
    Eval(R"JS(amrHost.request = globalThis.__nativeRequest;)JS");
    Eval(R"JS(
        mirrorHelper.loadJson = globalThis.__savedJson;
        const source = {
            mirrorName:'ReviewFixture', home:'https://test.invalid', domains:['test.invalid'],
            async getMangaList(q) { return [['One Piece','/title/one'],['Unrelated','/title/two']]; },
            async getListChaps() { return [['Chapter 1','/c1'],['Chapter 2','/c2'],['Chapter 2','/c2']]; }
        };
        __loader.lookupMap.set('ReviewFixture',source);
        __loader.mirrors.push(source);
    )JS");
    auto filtered = mirrors.SearchManga("ReviewFixture", "One Piece");
    assert(filtered.size() == 1 && filtered[0].url == "https://test.invalid/title/one");
    auto chapters = mirrors.GetChapters("ReviewFixture", "https://test.invalid/title/one");
    assert(chapters.size() == 2 && chapters.front().title == "Chapter 2");
    engine::UpdateNotification notification;
    assert(engine::Scheduler::Instance().CheckSeries("ReviewFixture", "https://test.invalid/title/one", chapters.front().url, chapters.front().title, notification));
    assert(notification.newChapterCount == 0 && notification.chapters.size() == 2);
    notification = {};
    assert(engine::Scheduler::Instance().CheckSeries("ReviewFixture", "https://test.invalid/title/one", chapters.back().url, chapters.back().title, notification));
    assert(notification.newChapterCount == 1);
    Eval(R"JS(
        const $=cheerio.load('<div>alpha<script>bad</script><a href="/a">one</a><a href="/b">two</a></div>');
        $('script').remove();
        if ($.text() !== 'alphaonetwo' || $('a').eq(-1).attr('href') !== '/b' || $('div').children('a').length !== 2) throw new Error('DOM compatibility');
        __loader.lookupMap.get('ReviewFixture').getMangaList = async () => { throw new Error('fixture parser failure'); };
    )JS");
    assert(mirrors.SearchManga("ReviewFixture", "One Piece").empty());
    auto statuses = mirrors.GetSourceStatuses();
    const auto last = std::find_if(statuses.begin(), statuses.end(), [](const auto& st) { return st.mirror == "ReviewFixture" && st.operation == "Arama"; });
    assert(last != statuses.end() && !last->success && last->error == "fixture parser failure");
    std::cout << count << " source parsers, worker stack changes, search filtering, DOM compatibility, chapter refresh and source diagnostics: PASS\n";
}

static void TestUpdatedSources() {
    auto& mirrors=engine::MirrorEngine::Instance();assert(mirrors.Init());
    assert(mirrors.GetImageReferer("Manga Buddy","https://mangabuddy.com/one-piece/chapter-1")=="https://comizy.io/");
    assert(mirrors.GetImageReferer("Manga Pill","https://mangapill.com/chapters/1")=="https://mangapill.com/chapters/1");
    Eval(R"JS(
        globalThis.__fixturePage=mirrorHelper.loadPage;
        globalThis.__fixtureJson=mirrorHelper.loadJson;
        globalThis.__fixtureCalls=0;
        globalThis.__nextData = value => '<script id="__NEXT_DATA__">'+JSON.stringify({props:{pageProps:value}})+'</script>';
        mirrorHelper.loadPage=async url => {
            if (url.includes('/search?')) {
                if (!url.endsWith('q=One%20Piece%20%26%20Friends')) throw new Error('Query not encoded');
                return __nextData({ssrItems:[{name:'One Piece & Friends',url:'/one-piece'}]});
            }
            if (url.includes('/chapter-')) return __nextData({initialChapter:{images:['https://img.invalid/1.webp','https://img.invalid/2.webp']}});
            return __nextData({initialManga:{id:'title-id',cv:7,chapters:[{name:'Chapter 3',number:3,url:'/one-piece/chapter-3'}]}});
        };
        mirrorHelper.loadJson=async url => {
            ++__fixtureCalls;
            if (url !== 'https://api.comizy.io/titles/title-id/chapters?cv=7') throw new Error('Missing complete chapter request');
            return {success:true,data:{chapters:[{name:'Chapter 1',number:1,url:'/one-piece/chapter-1'}, {name:'Chapter 2',number:2,url:'/one-piece/chapter-2'}, {name:'Chapter 3',number:3,url:'/one-piece/chapter-3'}]}};
        };
    )JS");
    auto found=mirrors.SearchManga("Manga Buddy","One Piece & Friends");
    assert(found.size()==1 && found.front().url=="https://mangabuddy.com/one-piece");
    auto chapters=mirrors.GetChapters("Manga Buddy",found.front().url);
    assert(chapters.size()==3 && chapters.front().title=="Chapter 3" && chapters.back().title=="Chapter 1");
    auto images=mirrors.GetChapterImages("Manga Buddy",chapters.front().url);assert(images.size()==2);
    Eval(R"JS(
        if (__fixtureCalls!==1) throw new Error('Full catalogue not requested');
        mirrorHelper.loadJson=async()=>{throw new Error('fixture full catalogue unavailable');};
    )JS");
    assert(mirrors.GetChapters("Manga Buddy",found.front().url).empty()); // Never mark a partial SSR list complete.
    Eval(R"JS(
        mirrorHelper.loadPage=async url => url.includes('/chapters/')
            ? '<img class="cover" src="https://img.invalid/cover.jpg"><img class="js-page" data-src="https://img.invalid/1.jpg"><img class="js-page" src="https://img.invalid/2.jpg">'
            : '<a href="/manga/1"><div class="font-black">One Piece</div></a>';
    )JS");
    found=mirrors.SearchManga("Manga Pill","One Piece");assert(found.size()==1);
    images=mirrors.GetChapterImages("Manga Pill","https://mangapill.com/chapters/1");assert(images.size()==2 && images.front()=="https://img.invalid/1.jpg");
    Eval(R"JS(
        globalThis.__fixtureCalls=0;
        mirrorHelper.loadPage=async url => {
            ++__fixtureCalls;
            if (url.includes('/chapter/')) return '<img class="PB0mN" src="https://img.invalid/page.jpg">';
            const n=__fixtureCalls;
            return '<div class="_1KYcM"><h4><a href="/manga/one-'+n+'">One Piece '+n+'</a></h4></div><ul class="pager"><li class="next"><a href="https://mangahub.io/search?q=One%20Piece&amp;page='+(n+1)+'">Next</a></li></ul>';
        };
        mirrorHelper.loadJson=async()=>{throw new Error('Legacy API must not be used for rendered pages');};
    )JS");
    found=mirrors.SearchManga("Manga Hub","One Piece");assert(found.size()==3);
    Eval("if (__fixtureCalls!==3) throw new Error('Unbounded search pagination');");
    images=mirrors.GetChapterImages("Manga Hub","https://mangahub.io/chapter/one/chapter-1");assert(images.size()==1);
    Eval(R"JS(
        globalThis.__fixtureCalls=0;
        mirrorHelper.loadPage=async url => {
            if (++__fixtureCalls>1) throw new Error('fixture next page unavailable');
            return '<div class="_1KYcM"><h4><a href="/manga/one">One Piece</a></h4></div><ul class="pager"><li class="next"><a href="https://mangahub.io/page/2">Next</a></li></ul>';
        };
    )JS");
    assert(mirrors.SearchManga("Manga Hub","One Piece").size()==1);
    Eval(R"JS(
        mirrorHelper.loadJson=async url=>({data:[{title:'Solo Swordmaster',public_url:'/comics/solo-swordmaster-bd5bdaf8'}]});
        mirrorHelper.loadPage=async url=>url.includes('/chapter/')
            ? '<img src="https://img.invalid/cover.jpg"><img data-page-index="1" src="https://img.invalid/2.webp"><img data-page-index="0" src="https://img.invalid/1.webp">'
            : '<a href="/comics/solo/chapter/1">Start reading</a><a href="/comics/solo/chapter/21">Chapter 21</a><a href="/comics/solo/chapter/1">Chapter 1</a>';
    )JS");
    found=mirrors.SearchManga("Asura Scans","Solo");assert(found.size()==1 && found.front().url=="https://asurascans.com/comics/solo-swordmaster-bd5bdaf8");
    chapters=mirrors.GetChapters("Asura Scans",found.front().url);assert(chapters.size()==2 && chapters.front().title=="Chapter 21");
    images=mirrors.GetChapterImages("Asura Scans",chapters.front().url);assert(images.size()==2 && images.front()=="https://img.invalid/1.webp");
    Eval(R"JS(
        mirrorHelper.loadPage=async()=>'<html><p>Sign in to read this chapter</p></html>';
    )JS");
    assert(mirrors.GetChapterImages("Asura Scans",chapters.front().url).empty());
    Eval(R"JS(
        mirrorHelper.loadJson=async(url,options)=>{
            if (url!=='https://rizzfables.com/Index/live_search' || !options.post || options.data.search_value!=='Top Tier') throw new Error('Wrong Rizz search request');
            return [{title:'Top Tier Providence'}];
        };
    )JS");
    found=mirrors.SearchManga("Rizz Comic","Top Tier");assert(found.size()==1 && found.front().url=="https://rizzfables.com/series/r2311170-top-tier-providence");
    Eval(R"JS(mirrorHelper.loadPage=__fixturePage; mirrorHelper.loadJson=__fixtureJson;)JS");
    std::cout<<"Updated sources: complete catalogue, query encoding, bounded pagination, new selectors, page ordering and unavailable pages: PASS\n";
}

static void TestSearchRecovery() {
    auto& mirrors=engine::MirrorEngine::Instance();assert(mirrors.Init());
    Eval(R"JS(
        globalThis.__recoveryPage=mirrorHelper.loadPage; globalThis.__recoveryJson=mirrorHelper.loadJson;
        for (const [title,query] of [['One-Piece','one piece'],['OnePiece','One Piece'],['İkinci Şans','ikinci sans'],['進撃の巨人','巨人'],['Even the Almighty Is a First-Time Dad','first time dad']])
            if (!__searchTitleMatches(title,query)) throw new Error('Title normalization lost a relevant result');
        if (__searchTitleMatches('One Punch Man','One Piece') || __searchTitleMatches('進撃の巨人','海賊')) throw new Error('Unrelated title accepted');
        __loader.lookupMap.get('ReviewFixture').getMangaList=async()=>[
            ['One-Piece','/manga/one'],['One Piece','/manga/one#bookmark'],['One Piece','javascript:alert(1)'],
            ['One Punch Man','/manga/punch'],['別名','/manga/alias','One Piece']];
    )JS");
    auto found=mirrors.SearchManga("ReviewFixture","One Piece");assert(found.size()==2);
    Eval(R"JS(
        globalThis.__searchPages=0;
        mirrorHelper.loadJson=async()=>{throw new Error('old AJAX endpoint removed');};
        mirrorHelper.loadPage=async url=>{
            ++__searchPages;
            if (new URL(url).searchParams.get('s')!=='One Piece & Friends' || !url.includes('post_type=wp-manga')) throw new Error('Search query encoding lost');
            const page=__searchPages;
            return '<div class="c-tabs-item"><div class="post-title"><h2><a href="https://gdscans.com/manga/result-'+page+'/">One Piece &amp; Friends '+page+'</a></h2></div></div>'+
                '<div class="c-tabs-item"><div class="post-title"><a href="https://ads.invalid/manga/one">One Piece &amp; Friends</a><a href="/chapter/1">One Piece &amp; Friends</a></div></div>'+
                '<a rel="next" href="https://gdscans.com/page/'+(page+1)+'/?s=One+Piece+%26+Friends&amp;post_type=wp-manga">Next</a>';
        };
    )JS");
    found=mirrors.SearchManga("GD Scans","One Piece & Friends");assert(found.size()==3);
    Eval(R"JS(
        if (__searchPages!==3) throw new Error('Search pagination must be bounded');
        mirrorHelper.loadPage=async()=>'<html><h1>Domain for sale</h1></html>';
    )JS");
    assert(mirrors.SearchManga("GD Scans","One Piece").empty());
    {const auto statuses=mirrors.GetSourceStatuses();const auto it=std::find_if(statuses.begin(),statuses.end(),[](const auto& s){return s.mirror=="GD Scans"&&s.operation=="Arama";});assert(it!=statuses.end()&&!it->success);}
    Eval(R"JS(
        mirrorHelper.loadPage=async()=>'<div class="listupd"><div class="bsx"><a href="/manga/one"><div class="tt">One-Piece</div></a></div></div>';
    )JS");
    assert(mirrors.SearchManga("Komikav","One Piece").size()==1);
    Eval(R"JS(
        globalThis.__searchPages=0;
        mirrorHelper.loadPage=async url=>{
            if (++__searchPages>1 || !url.includes('keyword=Tower%20of%20God')) throw new Error('WebToon should only fetch search page');
            return '<ul class="webtoon_list"><li><a href="https://www.webtoons.com/en/fantasy/tower-of-god/list?title_no=95"><strong class="title">Tower of God</strong></a></li></ul>';
        };
    )JS");
    found=mirrors.SearchManga("WebToons","Tower of God");assert(found.size()==1&&found[0].url.find("title_no=95")!=std::string::npos);
    Eval(R"JS(
        globalThis.__searchPages=0;
        mirrorHelper.loadPage=async url=>{
            const page=++__searchPages;
            if (page>2) throw new Error('Pagination must stop at disabled next span');
            return '<a class="detail_list_link" href="https://www.webtoons.com/en/test/viewer?title_no=95&amp;episode_no='+page+'"><span class="tx">#'+page+'</span><span class="subj">Episode '+page+'</span></a>'+
                '<div class="paginate"><a href="#" aria-current="true">Current</a>'+
                (page===1?'<a href="https://www.webtoons.com/en/fantasy/tower-of-god/list?title_no=95&amp;page=2">Next</a>':'<span class="pg_next off">Next</span>')+'</div>';
        };
    )JS");
    assert(mirrors.GetChapters("WebToons",found[0].url).size()==2);
    Eval(R"JS(
        mirrorHelper.loadPage=async(url,options)=>{
            if (!url.endsWith('/ajax/chapters/')) return '<div id="manga-chapters-holder" data-id="123"></div>';
            if (!options.post) throw new Error('Chapter form must POST');
            return '<li class="wp-manga-chapter"><a href="https://manhwatop.com/manga/fixture/chapter-1/">Chapter 1</a></li>';
        };
    )JS");
    assert(mirrors.GetChapters("Manhwa Top","https://manhwatop.com/manga/fixture/").size()==1);
    Eval(R"JS(
        mirrorHelper.loadPage=async(url,options)=>{
            if (url==='https://v7.kiryuu.to/') return '<form hx-post="https://v7.kiryuu.to/wp-admin/admin-ajax.php?nonce=public&amp;action=search"></form>';
            if (!options || options.method!=='POST' || options.data.get('query')!=='One Piece') throw new Error('Kiryuu native form request lost');
            return '<div id="searchResults"><a href="https://v7.kiryuu.to/manga/one-piece/"><h3>One Piece</h3><p>Synopsis must not become the title</p></a></div>';
        };
    )JS");
    found=mirrors.SearchManga("Kiryuu","One Piece");assert(found.size()==1&&found[0].title=="One Piece");
    Eval(R"JS(
        globalThis.__searchPages=0;
        mirrorHelper.loadJson=async url=>{
            if (!url.includes('/series/search?q=Ancient%20Sovereign')) throw new Error('Qi search request wrong');
            return {data:[{slug:'eternal',title:'The Eternal Supreme',alternativeTitles:'Ancient Sovereign'}],next:++__searchPages<2?2:null};
        };
    )JS");
    found=mirrors.SearchManga("Night Scans","Ancient Sovereign");assert(found.size()==1&&found[0].title=="The Eternal Supreme");
    Eval(R"JS(
        mirrorHelper.loadJson=async url=>url.includes('cursor=next')
            ? {data:[{slug:'chapter-1',number:1,isFree:true}],hasMore:false}
            : {data:[{slug:'chapter-2',number:2,isFree:true},{slug:'locked',number:3,requiresPurchase:true}],hasMore:true,nextCursor:'next'};
        mirrorHelper.loadPage=async()=>'<script id="ng-state" type="application/json">'+JSON.stringify({reader:{u:'https://api.qimanga.com/api/v1/series/eternal/chapters/chapter-2',b:{isFree:true,images:[{order:1,url:'https://image.invalid/2.jpg'},{order:0,url:'https://image.invalid/1.jpg'}]}}})+'</script>';
    )JS");
    auto chapters=mirrors.GetChapters("Night Scans","https://qimanga.com/series/eternal");assert(chapters.size()==2);
    auto images=mirrors.GetChapterImages("Night Scans",chapters[0].url);assert(images.size()==2&&images[0]=="https://image.invalid/1.jpg");
    Eval(R"JS(
        globalThis.__searchPages=0;
        mirrorHelper.loadJson=async url=>{
            if (!url.includes('limit=100') || !url.includes('offset='+(__searchPages*100))) throw new Error('MangaDex search pagination wrong');
            return {total:101,data:[{id:'title-'+(++__searchPages),attributes:{title:{ja:'別名'},altTitles:[{en:'One Piece'}]}}]};
        };
    )JS");
    found=mirrors.SearchManga("MangaDex V5","One Piece");assert(found.size()==2);
    // Already-cancelled and queued work must not reach source adapters.
    assert(mirrors.SearchManga("MangaDex V5","Cancelled",[]{return true;}).empty());
    Eval(R"JS(
        if (__searchPages!==2) throw new Error('Cancelled search ran');
        mirrorHelper.loadPage=__recoveryPage; mirrorHelper.loadJson=__recoveryJson;
    )JS");
    auto* ctx=mirrors.Engine().Ctx();
    auto global=JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx,global,"__searchGate",JS_NewCFunction(ctx,SearchGate,"__searchGate",0));
    JS_FreeValue(ctx,global);
    Eval(R"JS(__loader.lookupMap.get('ReviewFixture').getMangaList=async()=>{__searchGate();return [['One Piece','/manga/one']];};)JS");
    searchGateEntered=false;
    std::thread holder([&]{assert(mirrors.SearchManga("ReviewFixture","One Piece").size()==1);});
    while(!searchGateEntered) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto started=std::chrono::steady_clock::now();
    assert(mirrors.SearchManga("MangaDex V5","One Piece",[]{return true;}).empty());
    assert(std::chrono::steady_clock::now()-started<std::chrono::milliseconds(150));
    holder.join();
    searchGateEntered=false;
    assert(mirrors.SearchManga("ReviewFixture","One Piece",[]{return searchGateEntered.load();}).empty());
    assert(mirrors.Init()); // A cancelled pending promise may discard its VM safely.
    Eval(R"JS(__loader.lookupMap.set('ReviewFixture',{home:'https://test.invalid',getMangaList:async()=>[['One Piece','/manga/one']]});)JS");
    assert(mirrors.SearchManga("ReviewFixture","One Piece").size()==1); // Cancellation does not poison the shared VM.
    std::cout<<"Search recovery: title variants/aliases, safe deduplication, AJAX-to-HTML fallback, bounded pagination, native site forms and cancellation: PASS\n";
}

static void TestRequestedSources() {
    assert(net::NormalizeMirrorUrl("https://www.mangakawaii.io/manga/one/fr/3/1")=="https://www.mangakawaii.fr/manga/one/3");
    assert(net::NormalizeMirrorUrl("https://www.mangakawaii.fr/manga/one/chapitres?page=2")=="https://www.mangakawaii.fr/manga/one/chapitres?page=2");
    assert(net::NormalizeMirrorUrl("https://sadscans.com/series/one-piece")=="https://sadscans.net/seriler/one-piece");
    assert(net::NormalizeMirrorUrl("https://sadscans.com.invalid/series/one-piece")=="https://sadscans.com.invalid/series/one-piece");
    assert(net::NormalizeMirrorUrl("https://example.invalid/?u=mangakawaii.io")=="https://example.invalid/?u=mangakawaii.io");
    {
        browser::BrowserImageBuffer buffer;
        assert(!buffer.Put("empty",{}));
        assert(!buffer.Put("too-big",std::string(browser::BrowserImageBuffer::MaxImageBytes+1,'a')));
        for(int i=0;i<4;++i) assert(buffer.Put(std::to_string(i),std::string(browser::BrowserImageBuffer::MaxImageBytes,'a'+i)));
        assert(!buffer.Put("overflow","a"));
        assert(buffer.Take("2")==std::string(browser::BrowserImageBuffer::MaxImageBytes,'c'));
        assert(buffer.Take("2").empty());
        assert(buffer.Put("new",std::string("binary\x00\xff",8)));
        assert(!buffer.Put("new","duplicate"));
        assert(buffer.Take("new")==std::string("binary\x00\xff",8));
    }
    auto& mirrors=engine::MirrorEngine::Instance();assert(mirrors.Init());
    const auto registered=mirrors.GetMirrors();
    for(const auto* name:{"Evil Flowers","FunManga","Manga Komi","ManhwaHentai","Webtoon.xyz","Comic Kiba","Aqua Manga","247 Manga","Hiperdex","IsekaiScans","Kun Manga","Leviatan Scans","Lily Manga","Manga Bob"})
        assert(std::none_of(registered.begin(),registered.end(),[&](const auto& m){return m.name==name;}));
    for(const auto* name:{"Toonily","Dragon Tea","Sad Scans","MangaKawaii","Toonily.Net","MangaDex V5"})
        assert(std::any_of(registered.begin(),registered.end(),[&](const auto& m){return m.name==name;}));
    Eval(R"JS(
        globalThis.__requestedPage=mirrorHelper.loadPage;
        globalThis.__requestedJson=mirrorHelper.loadJson;
        if (!__loader.lookupMap.get('Toonily').chapter_url.test('/serie/title/chapter-1/')) throw new Error('Current Toonily reader path missing');
        const dragon=__loader.lookupMap.get('Dragon Tea');
        if (dragon.options.search_url!=='https://dragontea.ink/' || !dragon.chapter_url.test('/novel/title/chapter-1/')) throw new Error('Current Dragon Tea paths missing');
        mirrorHelper.loadPage=async url=>{
            if(url!=='https://www.mangakawaii.fr/recherche?q=One%20Piece%20%26%20Friends')throw new Error('Wrong Kawaii V5 search');
            return '<h1>Résultats</h1><a class="mk-card" href="/manga/one"><b class="mk-display">One Piece &amp; Friends</b></a>'+
                '<a class="mk-card" href="/manga/one"><b class="mk-display">One Piece &amp; Friends</b></a>'+
                '<a class="mk-card" href="/anime/one"><b class="mk-display">One Piece &amp; Friends</b></a>'+
                '<a class="mk-card" href="https://ads.invalid/manga/one"><b class="mk-display">One Piece &amp; Friends</b></a>';
        };
    )JS");
    auto found=mirrors.SearchManga("MangaKawaii","One Piece & Friends");assert(found.size()==1);
    Eval(R"JS(
        globalThis.__requestedCalls=0;
        mirrorHelper.loadPage=async(url,options)=>{
            ++__requestedCalls;
            if(__requestedCalls===1){
                if(url!=='https://www.mangakawaii.fr/manga/one')throw new Error('Old manga domain not migrated');
                return '<div id="chapitres" data-url="https://www.mangakawaii.fr/manga/one/chapitres"><div class="ch-row" data-number="3"><a href="/manga/one/3">Read</a></div><div class="ch-row" data-number="4">Locked</div><button data-ch-next="2"></button></div>';
            }
            if(url!=='https://www.mangakawaii.fr/manga/one/chapitres?page=2&ch=&order=desc' || options.headers['X-Requested-With']!=='fetch')throw new Error('Wrong V5 pagination');
            return '<div data-ch-list><div class="ch-row" data-number="2"><a href="/manga/one/2">Read</a></div><div class="ch-row" data-number="3"><a href="/manga/one/3">Duplicate</a></div></div>';
        };
    )JS");
    auto chapters=mirrors.GetChapters("MangaKawaii","https://www.mangakawaii.io/manga/one");assert(chapters.size()==2&&chapters.front().title=="Chapitre 3");
    Eval(R"JS(
        if(__requestedCalls!==2)throw new Error('Kawaii pagination incomplete');
        mirrorHelper.loadPage=async url=>{
            if(url.includes('/chapitres?'))throw new Error('Locked pages must not be requested');
            return '<p>Seuls les 10 derniers chapitres restent lisibles</p><div id="chapitres" data-url="https://www.mangakawaii.fr/manga/one/chapitres"><div class="ch-row" data-number="3"><a href="/manga/one/3">Read</a></div><div class="ch-row" data-number="2">Locked</div><button data-ch-next="2"></button></div>';
        };
    )JS");
    assert(mirrors.GetChapters("MangaKawaii","https://www.mangakawaii.fr/manga/one").size()==1);
    Eval(R"JS(
        mirrorHelper.loadPage=async url=>{
            if(url.includes('/chapitres?'))throw new Error('fixture: second page blocked');
            return '<div id="chapitres" data-url="https://www.mangakawaii.fr/manga/one/chapitres"><div class="ch-row" data-number="3"><a href="/manga/one/3">Read</a></div><button data-ch-next="2"></button></div>';
        };
    )JS");
    assert(mirrors.GetChapters("MangaKawaii","https://www.mangakawaii.fr/manga/one").empty());
    Eval(R"JS(
        mirrorHelper.loadPage=async url=>{
            if(url!=='https://www.mangakawaii.fr/manga/one/3')throw new Error('Legacy chapter must normalize before fetching');
            const data={title:"One Piece's story",back:'/manga/one',imgs:['https://image.invalid/01.webp?e=123&st=fixture','https://image.invalid/02.webp']};
            const encoded=JSON.stringify(data).replace(/[\s\S]/g,c=>'\\u'+c.charCodeAt(0).toString(16).padStart(4,'0'));
            return '<div x-data="rd(JSON.parse(\''+encoded+'\'))"><img id="pg-1"></div>';
        };
    )JS");
    auto images=mirrors.GetChapterImages("MangaKawaii","https://www.mangakawaii.io/manga/one/fr/3/1");assert(images.size()==2&&images.front()=="https://image.invalid/01.webp?e=123&st=fixture");
    Eval(R"JS(
        mirrorHelper.loadPage=async url=>{
            if(url!=='https://sadscans.net/seriler')throw new Error('Sad Scans old search endpoint used');
            return '<a href="/seriler/one-piece"><h2>One Piece</h2></a><a href="/seriler/one-piece"><img alt="One Piece"></a>';
        };
    )JS");
    found=mirrors.SearchManga("Sad Scans","One Piece");assert(found.size()==1&&found[0].url=="https://sadscans.net/seriler/one-piece");
    Eval(R"JS(
        mirrorHelper.loadPage=async url=>{
            if(url!=='https://sadscans.net/seriler/one-piece')throw new Error('Sad Scans legacy series not migrated');
            return '<a href="/reader/one-piece/chapter-3/abc">3. Bölüm</a><a href="/reader/another/chapter-4/def">4. Bölüm</a>';
        };
    )JS");
    chapters=mirrors.GetChapters("Sad Scans","https://sadscans.com/series/one-piece");assert(chapters.size()==1);
    Eval(R"JS(
        mirrorHelper.loadPage=async()=>'<script type="application/ld+json">{"@type":"ItemList","numberOfItems":20,"itemListElement":[{"item":{"name":"old chapter","url":"https://sadscans.net/reader/one-piece/chapter-1/abc"}}]}</script>';
    )JS");
    assert(mirrors.GetChapters("Sad Scans","https://sadscans.net/seriler/one-piece").empty());
    Eval(R"JS(
        mirrorHelper.loadPage=async url=>{
            if(!url.startsWith('https://sadscans.net/reader/'))throw new Error('Sad Scans legacy reader not migrated');
            return '<img src="/images/logo.jpg"><img src="/assets/series/series-id/abc/01.webp"><img data-src="/assets/series/series-id/abc/02.webp"><img src="/assets/series/series-id/def/01.webp">';
        };
    )JS");
    images=mirrors.GetChapterImages("Sad Scans","https://sadscans.com/reader/one-piece/chapter-3/abc");assert(images.size()==2);
    Eval(R"JS(mirrorHelper.loadPage=__requestedPage;mirrorHelper.loadJson=__requestedJson;)JS");
    std::cout<<"Requested sources: removal scope, current URLs, Kawaii V5 pagination/locked chapters/reader data, legacy bookmarks, Sad Scans catalogue guards: PASS\n";
}

static void TestMangaDexCatalogue() {
    auto& engine=engine::MirrorEngine::Instance();assert(engine.Init());
    const std::string manga="https://mangadex.org/title/11111111-1111-4111-8111-111111111111";
    const std::string first="https://mangadex.org/chapter/00000000-0000-4000-8000-000000000010";
    const std::string read="https://mangadex.org/chapter/00000000-0000-4000-8000-000000000006";
    Eval(R"JS(
        globalThis.__dexJson=mirrorHelper.loadJson;globalThis.__dexPage=0;
        const chapter=(id,volume,number,extra={})=>({id:'00000000-0000-4000-8000-'+String(id).padStart(12,'0'),attributes:{volume,chapter:number,translatedLanguage:'en',pages:10,publishAt:'2026-01-01',...extra}});
        mirrorHelper.loadJson=async url=>{
            if(!url.includes('translatedLanguage[]=en') || !url.includes('includeExternalUrl=0') || !url.includes('includeUnavailable=0'))throw new Error('Missing language/readability filter');
            if(!url.includes('offset='+(__dexPage?5:0)))throw new Error('Wrong offset');
            return __dexPage++===0?{limit:5,offset:0,total:11,data:[
                chapter(1,'2','1'),chapter(2,'1','1'),chapter(3,'2','99',{translatedLanguage:'pl'}),
                chapter(4,'2','98',{externalUrl:'https://publisher.invalid'}),chapter(5,'2','97',{isUnavailable:true})
            ]}:{limit:6,offset:5,total:11,data:[
                chapter(6,'2','1',{publishAt:'2025-01-01'}),chapter(7,'1','1',{publishAt:'2026-02-01'}),
                chapter(8,null,null,{title:'One shot A'}),chapter(9,null,null,{title:'One shot B'}),chapter(10,'2','2'),
                {...chapter(11,'9','900'),relationships:[{type:'manga',id:'foreign-title'}]}
            ]};
        };
    )JS");
    const auto chapters=engine.GetChapters("MangaDex V5",manga,"en");
    assert(chapters.size()==5 && chapters.front().url==first && chapters[1].url==read);
    assert(chapters[2].title=="Volume 1 - Chapter 1" && chapters[3].title=="One shot A" && chapters[4].title=="One shot B");
    Eval(R"JS(
        mirrorHelper.loadJson=async url=>{if(!url.includes('translatedLanguage[]=ja'))throw new Error('Legacy language code not mapped');return {limit:500,offset:0,total:0,data:[]};};
    )JS");
    assert(engine.GetChapters("MangaDex V5",manga,"jp").empty());
    auto status=engine.GetSourceStatuses();assert(std::any_of(status.begin(),status.end(),[](const auto& s){return s.mirror=="MangaDex V5" && s.operation=="Bölümler" && s.error.find("jp dilinde")!=std::string::npos;}));
    Eval(R"JS(
        globalThis.__dexPage=0;
        mirrorHelper.loadJson=async()=>{if(__dexPage++)throw new Error('fixture second page failed');return {limit:1,offset:0,total:2,data:[{id:'00000000-0000-4000-8000-000000000001',attributes:{chapter:'1',translatedLanguage:'en'}}]};};
    )JS");
    assert(engine.GetChapters("MangaDex V5",manga,"en").empty()); // No partially scanned catalogue.
    Eval(R"JS(mirrorHelper.loadJson=__dexJson;)JS");
    assert(IsMangaDexSource(manga));assert(!IsMangaDexSource("https://mangadex.org.evil.invalid/title/a"));
    assert(!IsChapterCandidateUrl("https://mangadex.org/group/11111111-1111-4111-8111-111111111111/comikey"));
    assert(!IsChapterCandidateUrl("https://reader.invalid/user/uploader"));assert(IsChapterCandidateUrl(read));
    std::vector<Chapter> volumes={{"Volume 2 Chapter 1","https://test.invalid/v2/c1"},{"Volume 1 Chapter 50","https://test.invalid/v1/c50"}};
    NormalizeChapters(volumes);assert(volumes.front().title=="Volume 2 Chapter 1");
    const auto path=paths::Join(paths::DataDir(),L"dex-migration.json");
    auto legacy=json::Value::Obj();legacy.Add("mirror",json::Value::Str("MangaDex V5"));legacy.Add("name",json::Value::Str("Fixture"));legacy.Add("url",json::Value::Str(manga));legacy.Add("language",json::Value::Str("en"));
    legacy.Add("lastChapterReadURL",json::Value::Str(read));legacy.Add("lastChapterReadName",json::Value::Str("Ch. 10Uploader"));legacy.Add("lastReadTs",json::Value::Num(1791570326503));
    legacy.Add("latestChapterUrl",json::Value::Str("https://mangadex.org/group/uploader"));legacy.Add("latestChapterName",json::Value::Str("Uploader"));
    auto oldChapters=json::Value::Arr(),oldChapter=json::Value::Obj();oldChapter.Add("t",json::Value::Str("Ch. 10Uploader"));oldChapter.Add("u",json::Value::Str(read));oldChapters.Push(oldChapter);legacy.Add("chapters",oldChapters);
    auto root=json::Value::Obj(), mangas=json::Value::Arr();mangas.Push(legacy);root.Add("mangas",mangas);
    assert(fileio::WriteAtomic(path,json::Dump(root)));Library library;library.Load(path);
    const auto key=library.All().front().key;const auto* series=library.FindSeries(key);
    assert(series && series->lastChapterUrl==read && series->lastReadTs==1791570326503 && series->latestChapterUrl.empty() && series->chapters.empty() && !series->lastUpdateError.empty());
    assert(!library.MarkReadUpToLatest(key));
    assert(!library.StoreChapters(key,{{"Uploader","https://mangadex.org/group/uploader"}}));
    std::vector<Chapter> catalogue;for(const auto& c:chapters)catalogue.push_back({c.title,c.url});
    assert(library.StoreChapters(key,catalogue,2000));assert(library.FindSeries(key)->lastChapterName=="Volume 2 - Chapter 1");
    Library restored;restored.Load(path);assert(restored.FindSeries(key)->chapters.size()==5 && restored.FindSeries(key)->chapterCatalogueVersion==1 && restored.FindSeries(key)->lastChapterUrl==read);
    // Write the legacy on-disk format into this isolated test profile.
    uint64_t cacheHash=14695981039346656037ull;for(unsigned char c:key){cacheHash^=c;cacheHash*=1099511628211ull;}
    wchar_t cacheName[64];swprintf_s(cacheName,L"%016llx.json",(unsigned long long)cacheHash);
    const auto cacheFolder=paths::Join(paths::CacheDir(),L"chapters_list");paths::EnsureDir(cacheFolder);
    const auto cachePath=paths::Join(cacheFolder,cacheName);
    assert(fileio::WriteAtomic(cachePath,"[{\"t\":\"Ch. 10Uploader\",\"u\":\""+read+"\"}]"));
    std::vector<std::pair<std::string,std::string>> pairs;assert(!amr::LoadCachedChapterList(key,pairs));
    for(const auto& c:chapters)pairs.emplace_back(c.title,c.url);
    assert(amr::SaveCachedChapterList(key,pairs));pairs.clear();assert(amr::LoadCachedChapterList(key,pairs) && pairs.size()==5);
    std::cout<<"MangaDex language isolation, pagination, volume-aware deduplication, empty/incomplete feed, profile rejection, progress-preserving cache migration: PASS\n";
}

static void TestRetry() {
    auto& mirrors=engine::MirrorEngine::Instance(); assert(mirrors.Init());
    const std::string token=std::to_string(GetTickCount64());
    Eval("globalThis.__imageCalls=0; globalThis.__getChapterImages=async()=>Array.from({length:++__imageCalls<3?1:15},(_,i)=>'http://127.0.0.1:18881/image/"+token+"/'+i+'.png');");
    auto& loader=engine::ChapterLoader::Instance();
    const auto dir=loader.GetCacheDir("ReviewRetry",token,"chapter");
    std::atomic<int> ready=0;
    assert(loader.DownloadChapter("ReviewRetry","http://127.0.0.1:18881/chapter",dir,[&](int,int total,const std::wstring&){assert(total==15);++ready;}));
    assert(ready==15 && loader.IsCached(dir));
    auto calls=mirrors.Engine().Eval("__imageCalls","<retry>"); assert(JS_VALUE_GET_INT(calls)==3); JS_FreeValue(mirrors.Engine().Ctx(),calls);
    assert(DeleteFileW((dir+L"\\0007.png").c_str())); assert(!loader.IsCached(dir));
    Eval("globalThis.__getChapterImages=async()=>['http://127.0.0.1:18881/image/"+token+"/0.png'];");
    assert(!loader.DownloadChapter("ReviewRetry","http://127.0.0.1:18881/chapter",dir)); assert(!loader.IsCached(dir));
    Eval("globalThis.__getChapterImages=async()=>Array.from({length:15},(_,i)=>'http://127.0.0.1:18881/image/"+token+"/'+i+'.png');");
    assert(loader.DownloadChapter("ReviewRetry","http://127.0.0.1:18881/chapter",dir)); assert(loader.IsCached(dir));
    assert(fileio::WriteAtomic(dir+L"\\.complete","ok")); assert(!loader.IsCached(dir)); // Legacy markers did not prove page count.
    assert(loader.DownloadChapter("ReviewRetry","http://127.0.0.1:18881/chapter",dir)); assert(loader.IsCached(dir));
    Eval("globalThis.__getChapterImages=async()=>Array.from({length:15},(_,i)=>'http://127.0.0.1:18881/image/"+token+"fail/'+i+'.png?permanent=1');");
    const auto failDir=loader.GetCacheDir("ReviewRetry",token,"failed");
    assert(!loader.DownloadChapter("ReviewRetry","http://127.0.0.1:18881/chapter",failDir)); assert(!loader.IsCached(failDir));
    const auto cancelDir=loader.GetCacheDir("ReviewRetry",token,"cancel");
    assert(!loader.DownloadChapter("ReviewRetry","http://127.0.0.1:18881/chapter",cancelDir,nullptr,[]{return true;}));
    Eval("globalThis.__getChapterImages=async()=>Array.from({length:3},(_,i)=>'http://127.0.0.1:18881/image/"+token+"referer/'+i+'.png?referer=comizy');");
    const auto refererDir=loader.GetCacheDir("Manga Buddy",token,"referer");
    assert(loader.DownloadChapter("Manga Buddy","https://mangabuddy.com/one-piece/chapter-1",refererDir));
    assert(loader.IsCached(refererDir));
    std::cout<<"Third-attempt catalogue, per-image retries, missing cache page, permanent failure and cancellation: PASS\n";
    std::cout<<"Source image Referer is applied to actual downloads: PASS\n";
}

static void TestSchedule() {
    assert(fileio::WriteAtomic(paths::Join(paths::DataDir(),L"library.json"),"{\"mangas\":[],\"bookmarks\":[]}"));
    const auto timestamp=paths::Join(paths::DataDir(),L"scan.timestamp");
    assert(fileio::WriteAtomic(timestamp,"0"));
    auto& scheduler=engine::Scheduler::Instance();
    std::mutex mutex; std::condition_variable cv; int scans=0;
    auto finished=[&]{std::lock_guard<std::mutex> lock(mutex);++scans;cv.notify_all();};
    auto started=std::chrono::steady_clock::now();
    scheduler.Start(nullptr,finished,60);
    {std::unique_lock<std::mutex> lock(mutex);assert(cv.wait_for(lock,std::chrono::seconds(35),[&]{return scans==1;}));}
    assert(std::chrono::steady_clock::now()-started>=std::chrono::seconds(29));
    scheduler.Stop();
    std::string stamp;assert(fileio::ReadText(timestamp,64,stamp)==fileio::ReadResult::Ok && std::stoll(stamp)>0);
    scheduler.Start(nullptr,finished,300);
    scheduler.Start(nullptr,finished,60); // Apply a changed interval to an existing worker.
    {std::unique_lock<std::mutex> lock(mutex);assert(!cv.wait_for(lock,std::chrono::milliseconds(300),[&]{return scans>1;}));}
    scheduler.Stop();
    scheduler.Start(nullptr,finished,0);scheduler.TriggerCheckNow();
    {std::unique_lock<std::mutex> lock(mutex);assert(cv.wait_for(lock,std::chrono::seconds(2),[&]{return scans==2;}));}
    {std::unique_lock<std::mutex> lock(mutex);assert(!cv.wait_for(lock,std::chrono::milliseconds(300),[&]{return scans>2;}));}
    scheduler.Stop();
    std::cout<<"Real 30-second startup delay, persisted schedule, live interval change and manual-only scan: PASS\n";
}

int main(int argc, char** argv) {
    std::wstring err;
    assert(paths::Init(&err));
    log::Init();
    assert(engine::Scheduler::DelaySeconds(10000,0,60,true)==30);
    assert(engine::Scheduler::DelaySeconds(10000,9000,60,true)==2600);
    assert(engine::Scheduler::DelaySeconds(10000,5000,60,true)==30);
    assert(engine::Scheduler::DelaySeconds(10000,9000,300,false)==17000);
    assert(engine::Scheduler::DelaySeconds(10000,10000,5000,false)==86400);
    assert(engine::Scheduler::DelaySeconds(10000,0,0,true)==-1);
    if (argc > 1 && std::string(argv[1]) == "access") {
        auto& mirrors=engine::MirrorEngine::Instance();assert(mirrors.Init());
        engine::MirrorInfo mirror;mirror.name="Access Fixture";mirror.home="http://127.0.0.1:18881/";
        const auto good=mirrors.CheckAccess(mirror,3000);assert(good.success && good.httpStatus==200 && good.error.empty());
        mirror.home="http://127.0.0.1:18881/missing";const auto missing=mirrors.CheckAccess(mirror,3000);assert(!missing.success && missing.httpStatus==404 && !missing.error.empty());
        mirror.home="http://127.0.0.1:18881/redirect?token="+std::to_string(GetTickCount64());const auto challenge=mirrors.CheckAccess(mirror,3000);assert(!challenge.success && challenge.httpStatus==200 && !challenge.error.empty());
        mirror.home.clear();const auto absent=mirrors.CheckAccess(mirror,3000);assert(!absent.success && absent.error=="Kaynak adresi bulunamadı");
        const auto statuses=mirrors.GetSourceStatuses();assert(std::count_if(statuses.begin(),statuses.end(),[&](const auto& status){return status.mirror==mirror.name && status.operation=="Erişim";})==1);
        std::string persisted;assert(fileio::ReadText(paths::Join(paths::DataDir(),L"source_status.json"),4*1024*1024,persisted)==fileio::ReadResult::Ok);assert(persisted.find("httpStatus")!=std::string::npos);
        std::cout<<"Source access: HTTP success, 404, JavaScript challenge, missing address and status replacement/persistence: PASS\n";
    } else if (argc > 1 && std::string(argv[1]) == "retry") { TestRetry();
    } else if (argc > 1 && std::string(argv[1]) == "schedule") { TestSchedule();
    } else if (argc > 1 && std::string(argv[1]) == "browser") {
        net::HttpRequest req;
        req.url = "http://127.0.0.1:18881/echo";
        req.method = "POST";
        req.body = "title=One%20Piece";
        req.headers = {{"Content-Type","application/x-www-form-urlencoded"},{"X-Review","yes"}};
        req.timeoutMs = 10000;
        const auto response = browser::WebViewHost::Instance().Execute(req, false);
        std::cout << response.status << " " << response.error << " " << response.body << std::endl;
        assert(response.success && response.status == 201);
        assert(response.body == "title=One%20Piece|yes");
        assert(response.GetHeader("X-Reply") == "verified");
        req.url = "http://127.0.0.1:18881/missing"; req.method = "GET"; req.body.clear();
        const auto failure = browser::WebViewHost::Instance().Execute(req, false);
        assert(!failure.success && failure.status == 404);
        req.url = "http://127.0.0.1:18881/redirect?token=" + std::to_string(GetTickCount64());
        const auto redirect = browser::WebViewHost::Instance().Execute(req, false);
        std::cout << "Redirect: " << redirect.status << " " << redirect.error << " " << redirect.body << std::endl;
        assert(redirect.success && redirect.body == "js-navigation-completed");
        req.url = "http://127.0.0.1:18881/"; req.maxResponseBytes = 8;
        net::HttpClient client;
        const auto oversized = client.Send(req);
        assert(!oversized.success && oversized.error == "Response exceeds size limit");
        std::cout << "Browser transport method/body/headers/status: PASS\n";
    } else if (argc > 1 && std::string(argv[1]) == "sources") {
        auto& mirrors = engine::MirrorEngine::Instance();
        mirrors.Init();
        for (const auto& name : {"Manga Clash","MangaSee","Luminous Scans","Reaper Scans (Fake)","Manhwa Freak","Ruya Manga","Komikid","ScanFR"}) {
            const auto found = mirrors.SearchManga(name, "One Piece");
            std::cout << name << ": " << found.size() << " results\n" << std::flush;
            for (const auto& status : mirrors.GetSourceStatuses()) if (status.mirror == name && status.operation == "Arama")
                std::cout << "  " << (status.success ? "OK" : status.error) << std::endl;
        }
    } else if (argc > 1 && std::string(argv[1]) == "live") {
        auto& mirrors = engine::MirrorEngine::Instance();
        const auto names = mirrors.GetMirrors();
        for (const auto& wanted : {"MangaDex V5", "Manga-Fox", "Manga Here", "Manga Read", "ManhuaPlus"}) {
            std::string name;
            for (const auto& m : names) if (amr::SafeName(m.name) == amr::SafeName(wanted)) name = m.name;
            if (name.empty()) { std::cout << wanted << ": not registered\n"; continue; }
            const auto found = mirrors.SearchManga(name, "One Piece");
            std::cout << name << ": " << found.size() << " search results\n" << std::flush;
            if (!found.empty()) {
                auto chosen = found.begin();
                for (auto it = found.begin(); it != found.end(); ++it) if (it->title == "One Piece") { chosen = it; break; }
                const auto chapters = mirrors.GetChapters(name, chosen->url);
                std::cout << "  " << chapters.size() << " chapters\n" << std::flush;
                if (!chapters.empty() && name == "MangaDex V5") {
                    const auto images = mirrors.GetChapterImages(name, chapters.front().url);
                    std::cout << "  " << images.size() << " page URLs\n" << std::flush;
                }
            }
        }
    } else if(argc>1 && std::string(argv[1])=="requested") { TestRequestedSources();
    } else { TestLibrary(); TestMirrors(); TestUpdatedSources(); TestSearchRecovery(); TestMangaDexCatalogue(); TestRequestedSources(); }
    engine::MirrorEngine::Instance().Shutdown();
    browser::WebViewHost::Instance().Shutdown();
    return 0;
}
