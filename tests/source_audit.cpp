// Bounded live source diagnostics. Run from build/source-audit/<slot>/ so all
// cookies, logs and status writes stay separate from the user's library.
#include "core/Paths.h"
#include "core/FileIo.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/Utf8.h"
#include "engine/MirrorEngine.h"
#include "browser/WebViewHost.h"
#include "net/Fetch.h"
#include <iostream>
#include <algorithm>

using namespace gm;
using json::Value;
int main(int argc,char** argv) {
    std::wstring error;
    if(!paths::Init(&error)) return 2;
    log::Init();
    auto& engine=engine::MirrorEngine::Instance();
    if(argc>2 && std::string(argv[1])=="probe") {
        net::HttpRequest request;request.url=argv[2];request.timeoutMs=20000;
        request.maxResponseBytes=4*1024*1024;
        const auto response=argc>3 && std::string(argv[3])=="browser"
            ?browser::WebViewHost::Instance().Execute(request,false):net::Fetch(request);
        fileio::WriteAtomic(paths::Join(paths::ExeDir(),L"probe.html"),response.body);
        std::cout<<response.status<<" bytes="<<response.body.size()<<" url="<<response.finalUrl<<" error="<<response.error<<std::endl;
        browser::WebViewHost::Instance().Shutdown();return response.success?0:1;
    }
    const auto mirrors=engine.GetMirrors();
    if(argc<2 || std::string(argv[1])=="list") {
        auto list=Value::Arr();
        for(size_t i=0;i<mirrors.size();++i){const auto& m=mirrors[i];auto o=Value::Obj();
            o.Add("index",Value::Num((double)i));o.Add("name",Value::Str(m.name));o.Add("home",Value::Str(m.home));
            o.Add("language",Value::Str(m.language));o.Add("disabled",Value::Bool(m.disabled));list.Push(std::move(o));}
        std::cout<<json::Dump(list)<<std::endl;return 0;
    }
    const std::string requested=argv[1];
    const auto named=std::find_if(mirrors.begin(),mirrors.end(),[&](const auto& m){return m.name==requested;});
    int index=-1;
    if(named!=mirrors.end()) index=(int)std::distance(mirrors.begin(),named);
    else if(!requested.empty() && requested.find_first_not_of("0123456789")==std::string::npos) index=std::stoi(requested);
    if(index<0 || index>=(int)mirrors.size()) {std::cerr<<"Source not registered: "<<requested<<std::endl;return 3;}
    const auto& m=mirrors[index];auto report=Value::Obj();
    const std::string query=argc>2?argv[2]:"One Piece";
    const bool searchOnly=argc>3 && std::string(argv[3])=="--search-only";
    report.Add("query",Value::Str(query));
    report.Add("name",Value::Str(m.name));report.Add("home",Value::Str(m.home));report.Add("disabled",Value::Bool(m.disabled));
    auto stages=Value::Arr();
    const auto output=paths::Join(paths::ExeDir(),L"result.json");
    auto save=[&]{auto copy=report;copy.Add("stages",stages);fileio::WriteAtomic(output,json::Dump(copy));};
    auto record=[&](const std::string& operation,int count){
        auto o=Value::Obj();o.Add("operation",Value::Str(operation));o.Add("count",Value::Num(count));
        for(const auto& s:engine.GetSourceStatuses())if(s.mirror==m.name && s.operation==operation){
            o.Add("success",Value::Bool(s.success));o.Add("error",Value::Str(s.error));o.Add("http",Value::Num(s.httpStatus));}
        stages.Push(std::move(o));save();
    };
    save();if(!searchOnly){engine.CheckAccess(m,8000);record("Erişim",0);}
    if(!m.disabled){
        const auto found=engine.SearchManga(m.name,query);record("Arama",(int)found.size());
        auto titles=Value::Arr();
        for(const auto& item:found){auto row=Value::Obj();row.Add("title",Value::Str(item.title));row.Add("url",Value::Str(item.url));titles.Push(std::move(row));}
        report.Add("results",std::move(titles));
        if(!searchOnly && !found.empty()){
            auto chosen=std::find_if(found.begin(),found.end(),[](const auto& r){return r.title=="One Piece";});
            if(chosen==found.end())chosen=found.begin();
            report.Add("manga",Value::Str(chosen->url));
            std::string language=m.language.substr(0,m.language.find(','));if(language.empty() || language=="all")language="en";
            const auto chapters=engine.GetChapters(m.name,chosen->url,language);record("Bölümler",(int)chapters.size());
            if(!chapters.empty()){
                report.Add("chapter",Value::Str(chapters.front().url));
                const auto images=engine.GetChapterImages(m.name,chapters.front().url);record("Sayfalar",(int)images.size());
                if(!images.empty()) {
                    net::HttpRequest request;request.url=images.front();request.timeoutMs=10000;
                    request.maxResponseBytes=8*1024*1024;request.allowBrowserFallback=false;
                    request.headers.emplace_back("Referer",engine.GetImageReferer(m.name,chapters.front().url));
                    const auto response=net::Fetch(request);
                    const auto& bytes=response.body;
                    const bool raster=bytes.starts_with("\x89PNG\r\n\x1a\n") || bytes.starts_with("\xff\xd8\xff") ||
                        bytes.starts_with("GIF8") || (bytes.starts_with("RIFF") && bytes.size()>12 && bytes.substr(8,4)=="WEBP") ||
                        (bytes.size()>12 && bytes.substr(4,8)=="ftypavif");
                    const bool ok=response.success && raster;
                    auto stage=Value::Obj();stage.Add("operation",Value::Str("İlk resim"));stage.Add("count",Value::Num(ok?1:0));
                    stage.Add("success",Value::Bool(ok));stage.Add("http",Value::Num(response.status));
                    stage.Add("error",Value::Str(ok?"":!response.error.empty()?response.error:"Yanıt kullanılabilir resim verisi değil"));
                    stage.Add("bytes",Value::Num((double)bytes.size()));stages.Push(std::move(stage));save();
                }
            }
        }
    }
    report.Add("complete",Value::Bool(true));save();
    engine.Shutdown();browser::WebViewHost::Instance().Shutdown();
}
