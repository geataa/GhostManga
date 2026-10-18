#include "engine/LocalSource.h"
#include "core/Paths.h"

#include <windows.h>
#include <shlwapi.h>
#include <algorithm>

namespace gm::local {
namespace {

const wchar_t* const kExt[] = {
    L".jpg", L".jpeg", L".png", L".gif", L".bmp", L".webp", L".avif", L".jxl", L".tif", L".tiff", L".heic",
};

void SortLogical(std::vector<std::wstring>& v) {
    std::sort(v.begin(), v.end(), [](const std::wstring& a, const std::wstring& b) {
        return StrCmpLogicalW(paths::FileName(a).c_str(), paths::FileName(b).c_str()) < 0;
    });
}

std::vector<std::wstring> ListSubdirs(const std::wstring& dir) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileExW(paths::Join(dir, L"*").c_str(), FindExInfoBasic, &fd,
                                FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        out.push_back(paths::Join(dir, fd.cFileName));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    SortLogical(out);
    return out;
}

bool HasImages(const std::wstring& dir) {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileExW(paths::Join(dir, L"*").c_str(), FindExInfoBasic, &fd,
                                FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    do {
        // ListImages ile ayni suzgec: gizli dosya sayilmaz, yoksa "gorsel var"
        // denip acilinca bos cikan bolum olur.
        if (!(fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN)) && IsImageFile(fd.cFileName)) {
            found = true;
            break;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

} // namespace

bool IsImageFile(const std::wstring& name) {
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    const std::wstring ext = name.substr(dot);
    for (const wchar_t* e : kExt) {
        if (_wcsicmp(ext.c_str(), e) == 0) return true;
    }
    return false;
}

std::vector<std::wstring> ListImages(const std::wstring& dir) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileExW(paths::Join(dir, L"*").c_str(), FindExInfoBasic, &fd,
                                FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN)) continue;
        if (IsImageFile(fd.cFileName)) out.push_back(paths::Join(dir, fd.cFileName));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    SortLogical(out);
    return out;
}

std::wstring ResolveChapterDir(const std::wstring& picked) {
    if (HasImages(picked)) return picked;
    for (const auto& sub : ListSubdirs(picked)) {
        if (HasImages(sub)) return sub;
    }
    return std::wstring();
}

std::wstring SiblingChapter(const std::wstring& dir, int delta) {
    const std::wstring parent = paths::ParentDir(dir);
    if (parent.empty()) return std::wstring();
    const auto sibs = ListSubdirs(parent);
    int idx = -1;
    for (size_t i = 0; i < sibs.size(); ++i) {
        if (_wcsicmp(sibs[i].c_str(), dir.c_str()) == 0) { idx = (int)i; break; }
    }
    if (idx < 0) return std::wstring();
    for (int i = idx + delta; i >= 0 && i < (int)sibs.size(); i += delta) {
        if (HasImages(sibs[(size_t)i])) return sibs[(size_t)i];
    }
    return std::wstring();
}

} // namespace gm::local
#include "engine/Library.h"
#include "engine/AmrImport.h"
#include "core/FileIo.h"
#include "core/Json.h"
#include "core/Utf8.h"
#include <unordered_set>
#include <chrono>
#include <functional>

namespace gm::local {
namespace {
bool IsLocalArchive(const std::wstring& path) { const auto dot=path.find_last_of(L'.'); if(dot==std::wstring::npos)return false;const auto ext=path.substr(dot);return _wcsicmp(ext.c_str(),L".cbz")==0 || _wcsicmp(ext.c_str(),L".zip")==0; }
int64_t FileTimeMs(const FILETIME& ft) {
    ULARGE_INTEGER value{}; value.LowPart=ft.dwLowDateTime;value.HighPart=ft.dwHighDateTime;
    return value.QuadPart >= 116444736000000000ULL ? (int64_t)((value.QuadPart-116444736000000000ULL)/10000) : 0;
}
int64_t Modified(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA attr{};
    return GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&attr) ? FileTimeMs(attr.ftLastWriteTime) : 0;
}
std::wstring InfoPath(const std::wstring& path) {
    return IsLocalArchive(path) ? path+L".ghostmanga.json" : paths::Join(path,L".ghostmanga.json");
}
std::wstring HashName(const std::string& text) {
    uint64_t hash=14695981039346656037ULL;for(unsigned char c:text){hash^=c;hash*=1099511628211ULL;}
    wchar_t value[17];swprintf_s(value,L"%016llx",(unsigned long long)hash);return value;
}
bool IsHash(const std::wstring& text) {
    return text.size()==16 && std::all_of(text.begin(),text.end(),[](wchar_t c){return iswxdigit(c)!=0;});
}
Entry LoadInfo(const std::wstring& path) {
    Entry entry;entry.path=path;entry.archive=IsLocalArchive(path);
    std::string text;json::Value obj;
    if(fileio::ReadText(InfoPath(path),128*1024,text)==fileio::ReadResult::Ok && json::Parse(text,obj,nullptr) && obj.IsObject()) {
        auto str=[&](const char* key){const auto* value=obj.Get(key);return value?value->AsString():std::string();};
        entry.originalPath=FromUtf8(str("originalPath"));entry.seriesTitle=FromUtf8(str("seriesTitle"));entry.chapterTitle=FromUtf8(str("chapterTitle"));
        entry.seriesKey=str("seriesKey");entry.mirror=str("mirror");entry.mangaUrl=str("mangaUrl");entry.chapterUrl=str("chapterUrl");
        if(const auto* ts=obj.Get("downloadedAt"))entry.downloadedAt=ts->AsInt();
        if(const auto* recorded=obj.Get("recordedDownload"))entry.recordedDownload=recorded->AsBool();
    }
    if(entry.downloadedAt<=0 && _wcsicmp(paths::FileName(paths::ParentDir(path)).c_str(),L"archives")!=0){entry.downloadedAt=Modified(paths::Join(path,L".complete"));entry.recordedDownload=entry.downloadedAt>0;}
    if(entry.downloadedAt<=0)entry.downloadedAt=Modified(path);
    return entry;
}
}
Entry Describe(const std::wstring& path,const Library& library) {
    auto entry=LoadInfo(path);
    const Series* matched=!entry.seriesKey.empty()?library.FindSeries(entry.seriesKey):nullptr;
    if(!matched && !entry.mangaUrl.empty()) matched=library.FindSource(entry.mirror,entry.mangaUrl);
    if(!matched && IsHash(paths::FileName(path)) && IsHash(paths::FileName(paths::ParentDir(path)))) {
        const auto mangaHash=paths::FileName(paths::ParentDir(path));
        auto mirror=paths::FileName(paths::ParentDir(paths::ParentDir(path)));
        for(const auto& series:library.All()) {
            auto safeMirror=FromUtf8(series.mirror);
            for(auto& c:safeMirror) if(wcschr(L"<>:\"/\\|?*",c)) c=L'_';
            if(HashName(series.url)!=mangaHash || _wcsicmp(safeMirror.c_str(),mirror.c_str())!=0)continue;
            std::vector<Chapter> chapters=series.chapters;
            if(chapters.empty()) {std::vector<std::pair<std::string,std::string>> saved;if(amr::LoadCachedChapterList(series.key,saved))for(const auto& c:saved)chapters.push_back({c.first,c.second});}
            if(!series.lastChapterUrl.empty())chapters.push_back({series.lastChapterName,series.lastChapterUrl});
            if(!series.latestChapterUrl.empty())chapters.push_back({series.latestChapterName,series.latestChapterUrl});
            for(const auto& chapter:chapters) if(HashName(chapter.url)==paths::FileName(path)) {
                matched=&series;entry.chapterTitle=FromUtf8(chapter.title);entry.chapterUrl=chapter.url;break;
            }
            if(matched)break;
        }
    }
    if(!matched && entry.seriesTitle.empty() && IsHash(paths::FileName(path))) {
        std::string saved;json::Value root;
        if(fileio::ReadText(paths::Join(paths::DataDir(),L"session_tabs.json"),4*1024*1024,saved)==fileio::ReadResult::Ok && json::Parse(saved,root,nullptr)) {
            const auto* tabs=root.Get("tabs");
            if(tabs && tabs->IsArray())for(const auto& tab:tabs->arr) {
                const auto* dir=tab.Get("chapterDir");if(!dir || _wcsicmp(FromUtf8(dir->AsString()).c_str(),path.c_str())!=0)continue;
                if(const auto* title=tab.Get("mangaTitle"))entry.seriesTitle=FromUtf8(title->AsString());
                if(const auto* title=tab.Get("chapterTitle"))entry.chapterTitle=FromUtf8(title->AsString());
                if(const auto* value=tab.Get("mirror"))entry.mirror=value->AsString();
                if(const auto* value=tab.Get("mangaUrl"))entry.mangaUrl=value->AsString();
                if(const auto* value=tab.Get("chapterUrl"))entry.chapterUrl=value->AsString();
                if(!entry.mangaUrl.empty())matched=library.FindSource(entry.mirror,entry.mangaUrl);
                break;
            }
        }
    }
    if(matched) {
        entry.seriesTitle=FromUtf8(matched->displayName.empty()?matched->name:matched->displayName);
        entry.seriesKey=matched->key;entry.mirror=matched->mirror;entry.mangaUrl=matched->url;
    }
    if(entry.seriesTitle.empty()) {
        const auto name=paths::FileName(path),parent=paths::FileName(paths::ParentDir(path));
        entry.seriesTitle=IsHash(parent)||IsHash(name)?L"Kimliği bulunamayan yerel manga":parent;
        if(entry.seriesTitle.empty())entry.seriesTitle=name;
    }
    if(entry.chapterTitle.empty()) {
        entry.chapterTitle=paths::FileName(path);
        if(entry.archive)entry.chapterTitle=entry.chapterTitle.substr(0,entry.chapterTitle.find_last_of(L'.'));
        if(IsHash(entry.chapterTitle))entry.chapterTitle=L"Bölüm adı bilinmiyor";
    }
    return entry;
}
bool StoreInfo(const Entry& entry,bool newDownload) {
    auto copy=entry;
    const auto old=LoadInfo(entry.path);
    if(!newDownload && !old.seriesTitle.empty() && old.downloadedAt>0){copy.downloadedAt=old.downloadedAt;copy.recordedDownload=old.recordedDownload;}
    else if(newDownload){copy.downloadedAt=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();copy.recordedDownload=true;}
    else if(copy.downloadedAt<=0){copy.downloadedAt=old.downloadedAt;copy.recordedDownload=old.recordedDownload;}
    auto obj=json::Value::Obj();obj.Add("originalPath",json::Value::Str(ToUtf8(copy.originalPath)));obj.Add("seriesTitle",json::Value::Str(ToUtf8(copy.seriesTitle)));obj.Add("chapterTitle",json::Value::Str(ToUtf8(copy.chapterTitle)));
    obj.Add("seriesKey",json::Value::Str(copy.seriesKey));obj.Add("mirror",json::Value::Str(copy.mirror));obj.Add("mangaUrl",json::Value::Str(copy.mangaUrl));obj.Add("chapterUrl",json::Value::Str(copy.chapterUrl));
    obj.Add("downloadedAt",json::Value::Num((double)copy.downloadedAt));obj.Add("recordedDownload",json::Value::Bool(copy.recordedDownload));
    return fileio::WriteAtomic(InfoPath(copy.path),json::Dump(obj));
}
std::vector<Entry> Collect(const std::vector<std::wstring>& recent,const std::wstring& downloads,const Library& library) {
    std::vector<Entry> out;std::unordered_set<std::wstring> seen;
    auto add=[&](const std::wstring& path){auto entry=Describe(path,library);if(!entry.originalPath.empty() && GetFileAttributesW(entry.originalPath.c_str())!=INVALID_FILE_ATTRIBUTES){entry.path=entry.originalPath;entry.archive=IsLocalArchive(entry.path);}auto identity=entry.path;std::transform(identity.begin(),identity.end(),identity.begin(),towlower);if(seen.insert(identity).second)out.push_back(std::move(entry));};
    std::function<void(const std::wstring&,int,bool)> scan=[&](const std::wstring& dir,int depth,bool cache) {
        if(PathIsNetworkPathW(dir.c_str()))return;
        const auto attr=GetFileAttributesW(dir.c_str());if(attr==INVALID_FILE_ATTRIBUTES || !(attr&FILE_ATTRIBUTE_DIRECTORY) || (attr&FILE_ATTRIBUTE_REPARSE_POINT))return;
        if(HasImages(dir)) {if(!cache || GetFileAttributesW(paths::Join(dir,L".complete").c_str())!=INVALID_FILE_ATTRIBUTES)add(dir);return;}
        if(depth<=0)return;
        WIN32_FIND_DATAW fd{};HANDLE find=FindFirstFileW(paths::Join(dir,L"*").c_str(),&fd);if(find==INVALID_HANDLE_VALUE)return;
        do {if(fd.cFileName[0]==L'.' || fd.dwFileAttributes&(FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_REPARSE_POINT))continue;
            const auto path=paths::Join(dir,fd.cFileName);
            if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)scan(path,depth-1,cache);
            else if(!cache && IsLocalArchive(path))add(path);
        }while(FindNextFileW(find,&fd));FindClose(find);
    };
    for(const auto& path:recent) {
        if(PathIsNetworkPathW(path.c_str()))continue;
        const auto attr=GetFileAttributesW(path.c_str());if(attr==INVALID_FILE_ATTRIBUTES)continue;
        if(attr&FILE_ATTRIBUTE_DIRECTORY)scan(path,2,false);else if(IsLocalArchive(path))add(path);
    }
    scan(downloads,2,false);scan(paths::Join(paths::DataDir(),L"cache"),3,true);
    std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){return a.downloadedAt!=b.downloadedAt?a.downloadedAt>b.downloadedAt:a.path<b.path;});
    return out;
}
} // namespace gm::local
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

namespace gm::local {
namespace {
std::wstring AbsoluteLocalPath(const std::wstring& path) {
    const DWORD size=GetFullPathNameW(path.c_str(),0,nullptr,nullptr);if(!size)return {};
    std::wstring out(size,L'\0');const DWORD written=GetFullPathNameW(path.c_str(),size,out.data(),nullptr);if(!written || written>=size)return {};
    out.resize(written);while(out.size()>3 && (out.back()==L'\\'||out.back()==L'/'))out.pop_back();return out;
}
bool SamePath(const std::wstring& a,const std::wstring& b){return _wcsicmp(AbsoluteLocalPath(a).c_str(),AbsoluteLocalPath(b).c_str())==0;}
}
bool CanDelete(const Entry& entry,const std::wstring& downloads) {
    const auto path=AbsoluteLocalPath(entry.path);if(path.empty() || path.size()<=3 || PathIsNetworkPathW(path.c_str()))return false;
    if(SamePath(path,paths::DataDir()) || SamePath(path,paths::ExeDir()) || SamePath(path,downloads) || SamePath(path,paths::Join(paths::DataDir(),L"cache")))return false;
    const auto attr=GetFileAttributesW(path.c_str());if(attr==INVALID_FILE_ATTRIBUTES || (attr&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    if(attr&FILE_ATTRIBUTE_DIRECTORY)return HasImages(path);
    return IsLocalArchive(path);
}
bool Recycle(const Entry& entry,const std::wstring& downloads,std::wstring& error) {
    if(!CanDelete(entry,downloads)){error=L"Bu konum bir yerel bölüm değil veya korunan bir ana klasör.";return false;}
    const HRESULT apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(apartment)){error=L"Dosya işlemi başlatılamadı.";return false;}
    auto perform=[&](){
        Microsoft::WRL::ComPtr<IFileOperation> operation;
        HRESULT hr=CoCreateInstance(CLSID_FileOperation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&operation));
        if(FAILED(hr))return hr;
        hr=operation->SetOperationFlags(FOF_ALLOWUNDO|FOF_NOCONFIRMATION|FOF_NOERRORUI|FOF_SILENT|FOFX_RECYCLEONDELETE|FOFX_EARLYFAILURE);
        if(FAILED(hr))return hr;
        std::vector<std::wstring> targets{AbsoluteLocalPath(entry.path)};
        if(IsLocalArchive(entry.path)) {
            for(const auto& suffix:{L".ghostmanga.json",L".ghostmanga.json.bak"})if(GetFileAttributesW((entry.path+suffix).c_str())!=INVALID_FILE_ATTRIBUTES)targets.push_back(AbsoluteLocalPath(entry.path+suffix));
            for(const auto& cache:ListSubdirs(paths::Join(paths::DataDir(),L"cache\\archives"))) {
                const auto info=LoadInfo(cache);
                if(!info.originalPath.empty() && SamePath(info.originalPath,entry.path) && CanDelete(info,downloads))targets.push_back(AbsoluteLocalPath(cache));
            }
        }
        for(const auto& target:targets){
            Microsoft::WRL::ComPtr<IShellItem> item;
            hr=SHCreateItemFromParsingName(target.c_str(),nullptr,IID_PPV_ARGS(&item));if(FAILED(hr))return hr;
            hr=operation->DeleteItem(item.Get(),nullptr);if(FAILED(hr))return hr;
        }
        hr=operation->PerformOperations();BOOL aborted=FALSE;operation->GetAnyOperationsAborted(&aborted);
        if(aborted && SUCCEEDED(hr))return E_ABORT;return hr;
    };
    const HRESULT hr=perform();CoUninitialize();
    if(FAILED(hr)){wchar_t text[128];swprintf_s(text,L"Geri Dönüşüm Kutusu'na taşınamadı (0x%08X).",(unsigned)hr);error=text;return false;}
    if(GetFileAttributesW(entry.path.c_str())!=INVALID_FILE_ATTRIBUTES){error=L"Dosya taşınamadı; başka bir işlem tarafından kullanılıyor olabilir.";return false;}
    error.clear();return true;
}
} // namespace gm::local
