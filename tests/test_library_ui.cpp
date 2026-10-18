#include <cassert>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <unordered_set>
#include "core/Settings.h"
#include "engine/Library.h"
#include "ui/Renderer.h"
#include "ui/ShellHost.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"
#include "core/Paths.h"
#include "core/I18n.h"
#include "engine/AmrImport.h"
#include "ui/LibraryView.h"
#include "ui/ReaderView.h"
#include "ui/SettingsView.h"
#include "ui/BrowserView.h"
#include "core/Startup.h"
#include "engine/MirrorEngine.h"
#include "engine/ChapterLoader.h"
#include "engine/CbzWriter.h"
#include "core/FileIo.h"
#include "core/Json.h"
#include "core/Utf8.h"
#include <chrono>
#include <future>
namespace gm {
struct ReaderViewTestAccess {
 static void Setup(ReaderView& v,Settings& s) {v.m_set=&s;v.m_area=Rc(0,0,720,720);v.m_scale=1;v.m_docH=5000;v.m_chrome=0;}
 static void Draw(ReaderView& v,Renderer& r) {v.DrawScrollbar(r);}
 static auto Track(ReaderView& v){return v.m_scrollTrack;}
 static auto Thumb(ReaderView& v){return v.m_scrollThumb;}
 static float Scroll(ReaderView& v){return v.m_scrollY;}
 static void Menu(ReaderView& v,bool on){v.m_area.right=on?430.0f:720.0f;v.m_hudRect=on?Rc(0,580,430,640):Rc(0,0,0,0);v.m_chrome=on?1.0f:0.0f;}
 static void Short(ReaderView& v){v.m_docH=100;}
 static void HideSidebar(ReaderView& v){v.m_sidebarOpen=false;v.m_chrome=0;}
 static void SidebarHandle(ReaderView& v,Renderer& r,Ui& u){v.DrawSidebarHandle(r,u,Rc(0,0,720,720));}
 static auto Handle(ReaderView& v){return v.m_sidebarReopenRc;}
 static bool SidebarOpen(ReaderView& v){return v.m_sidebarOpen;}
 static void ChapterSidebar(ReaderView& v,Renderer& r,Ui& u,const std::wstring& title){v.m_chapter=title;v.DrawRightSidebar(r,u,Rc(430,0,720,720),1.0);}
};
struct LibraryViewTestAccess {
 static void Rebuild(LibraryView& view, const Library& library, const Settings& settings) { view.RebuildRows(library); view.RebuildDisplayItems(settings); }
 static auto& Locals(LibraryView& v){return v.m_localGroups;}
 static void LocalFilter(LibraryView& v,const std::wstring& query){v.m_localSearch.text=query;}
 static void LocalRebuild(LibraryView& v,const Settings& s,const Library& l){v.RebuildLocal(s,l);}
 static void LocalDraw(LibraryView& v,Renderer& r,Ui& u,const D2D1_RECT_F& area,ShellHost& h,Settings& s,Library& l){v.m_tab=LibraryView::kLocal;if(!v.m_localSelected.empty())u.Occlude(area);v.DrawLocalList(r,u,area,s.recent,h,s,l);}
 static bool LocalSelected(LibraryView& v){return !v.m_localSelected.empty();}
 static bool PendingDelete(LibraryView& v){return !v.m_localDeletePath.empty();}
 static void SitesFilter(LibraryView& v,const Settings& s,bool hidden){v.m_showHiddenSites=hidden;v.FilterSites(s);}
 static size_t SitesCount(LibraryView& v){return v.m_filteredMirrors.size();}
 static void SitesDraw(LibraryView& v,Renderer& r,Ui& u,const D2D1_RECT_F& a,ShellHost& h,Settings& s){v.m_sitesAutoChecked=true;v.DrawSitesList(r,u,a,h,s);}
 static bool New(LibraryView& v,ShellHost& h){return v.NewSearchTab(h);}
 static size_t Count(LibraryView& v){return v.m_searchTabs.size();}
 static auto Session(LibraryView& v){return v.m_activeSearch;}
 static void Select(LibraryView& v,size_t i){v.SelectSearchTab(i);}
 static void Close(LibraryView& v,size_t i){v.CloseSearchTab(i);}
 static void Cancel(LibraryView& v){v.CancelSearch(v.m_activeSearch);}
 static void Grid(LibraryView& v,Renderer& r,Ui& u,const D2D1_RECT_F& a,Settings& s,Library& l,ShellHost& h){v.DrawCardGrid(r,u,a,s,l,h);}
 static void Detail(LibraryView& v,Renderer& r,Ui& u,const D2D1_RECT_F& a,Settings& s,Library& l,ShellHost& h){v.DrawDetailCard(r,u,a,l,h,s);}
 static int SelectedRow(LibraryView& v){return v.m_selectedRowIndex;}
 static void SelectDetail(LibraryView& v,int index,int mode){v.m_selectedRowIndex=index;v.m_cardMode=mode;}
 static float SourceScroll(LibraryView& v){return v.m_cardSourcesScroll;}
 static float SourceMaxScroll(LibraryView& v){return v.m_cardSourcesMaxScroll;}
 static auto Language(LibraryView& v){return v.m_rowsLanguage;}
 static auto SelectedKey(LibraryView& v){return v.m_rows[v.m_selectedRowIndex].key;}
 static void ScrollableSearch(LibraryView& v){v.m_tab=LibraryView::kSearch;v.m_activeSearch->input.focused=false;v.m_pitch=50;v.m_listH=500;v.m_maxScroll[LibraryView::kSearch]=1000;v.m_activeSearch->maxScroll=1000;}

 static auto& Items(LibraryView& view) { return view.m_items; }
 static auto& Rows(LibraryView& view) { return view.m_rows; }
 static void Search(LibraryView& view, const std::wstring& query) { view.m_search.text = query; }
 static void ExpandFirst(LibraryView& view) { view.m_expandedGroups.insert(LibraryViewTestAccess::Items(view)[0].groupKey); view.m_displayDirty = true; }
 static auto MakeResult() { return LibraryView::GroupedSearchItem{}; }
 static void SetResult(LibraryView& view, const LibraryView::GroupedSearchItem& result) { view.EnsureSearchTab(); view.m_activeSearch->results = {result}; }
 static size_t Selected(LibraryView& view) { return view.m_activeSearch->results[0].selectedMirrorIdx; }
 static void Draw(LibraryView& view, Renderer& renderer, Ui& ui, const D2D1_RECT_F& area, ShellHost& host, Settings& settings, Library& library, double now) { view.DrawSearchTab(renderer,ui,area,host,settings,library,now); }
};
}
using namespace gm;

struct TestHost : ShellHost {
 Library& library; int added=0; int opened=0; std::string openedChapter;std::wstring openedPath;int openedPaths=0;int deletedPaths=0,saved=0;std::wstring deletedPath;std::vector<std::string> removedSeries;
 explicit TestHost(Library& value):library(value){}
 void Toast(const std::wstring& msg) override {}
 void Invalidate() override {}
 void OpenFolderDialog() override {}
 void OpenArchiveDialog() override {}
 void ImportAmrDialog() override {}
 void ExportBackupDialog() override {}
 void OpenDataFolder() override {}
 void OpenDownloadsFolder() override {}
 void PurgeCache() override {}
 void CheckUpdatesNow() override {}
 void OpenSitesCatalog() override {}
 void OpenUrl(const std::wstring& url) override {}
 void OpenPath(const std::wstring& dir) override {openedPath=dir;++openedPaths;}
 void DeleteLocalChapter(const std::wstring& path) override {deletedPath=path;++deletedPaths;}
 void OpenOnlineManga(const std::string& mirrorName, const std::string& mangaUrl, const std::wstring& title, const std::string& chapterUrl) override {++opened;openedChapter=chapterUrl;}
 void OpenEmbeddedBrowser(const std::string& url, const std::string& mirrorHint) override {}
 void ConvertAndOptimizeLibrary() override {}
 void DeleteSeriesFromLibrary(const std::string& key) override {assert(library.RemoveSeries(key));removedSeries.push_back(key);}
 void AddSeriesToLibrary(const std::string& mirrorName, const std::string& mangaUrl, const std::wstring& title) override {Series item; item.name="One Piece"; item.mirror=mirrorName; item.url=mangaUrl; item.language="en"; item.key=amr::MangaKey(mirrorName,mangaUrl,"en"); if(library.AddSeries(item)) ++added;}
 void RemoveBookmark(size_t index) override {}
 std::vector<std::pair<std::string,bool>> progressChanges;
 void SetReadingProgress(const std::string& key, bool readToLatest) override {progressChanges.emplace_back(key,readToLatest);assert(readToLatest?library.MarkReadUpToLatest(key):library.ResetReadingProgress(key));}
 void StoreChapterCatalogue(const std::string& key, const std::vector<Chapter>& chapters) override {}
 void DispatchUI(std::function<void()> fn) override {}
 void CrawlChaptersForManga(const std::string& mangaUrl, std::function<void(const std::vector<std::pair<std::string, std::string>>&)> callback) override {}
 void SaveSettings() override {++saved;}
 void LanguageChanged() override { }
};

int main(int argc,char** argv) {
 const HRESULT apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED); assert(SUCCEEDED(apartment));
 std::wstring error; assert(paths::Init(&error));
 // A selected-language API miss must never become an unfiltered DOM catalogue.
 { BrowserView browser;bool called=false;browser.CrawlMangaChapters("https://mangadex.org/title/11111111-1111-4111-8111-111111111111",[&](const BrowserView::ChapterCrawlResult& result){called=true;assert(!result.success && result.chapters.empty() && !result.errorMessage.empty());});assert(called); }
 if(argc>1 && std::string(argv[1])=="recycle") {
  const auto root=paths::Join(paths::DataDir(),L"recycle-review-"+std::to_wstring(GetTickCount64()));paths::EnsureDir(root);
  const auto resource=paths::Join(paths::ExeDir(),L"..\\..\\..\\resources\\pirate-logo.png");
  local::Entry folder;folder.path=paths::Join(root,L"chapter");paths::EnsureDir(folder.path);assert(CopyFileW(resource.c_str(),paths::Join(folder.path,L"001.png").c_str(),FALSE));assert(local::StoreInfo(folder));
  auto protectedEntry=folder;protectedEntry.path=root;assert(!local::CanDelete(protectedEntry,root));protectedEntry.path=paths::DataDir();assert(!local::CanDelete(protectedEntry,root));protectedEntry.path=L"E:\\";assert(!local::CanDelete(protectedEntry,root));
  local::Entry archive;archive.path=paths::Join(root,L"chapter.cbz");archive.archive=true;assert(engine::CbzWriter::PackFolder(folder.path,archive.path));assert(local::StoreInfo(archive));
  auto cache=archive;cache.path=paths::Join(paths::DataDir(),L"cache\\archives\\recycle-review-"+std::to_wstring(GetTickCount64()));cache.archive=false;cache.originalPath=archive.path;paths::EnsureDir(cache.path);assert(CopyFileW(resource.c_str(),paths::Join(cache.path,L"001.png").c_str(),FALSE));assert(local::StoreInfo(cache));
  assert(local::Recycle(archive,root,error));assert(GetFileAttributesW(archive.path.c_str())==INVALID_FILE_ATTRIBUTES);assert(GetFileAttributesW((archive.path+L".ghostmanga.json").c_str())==INVALID_FILE_ATTRIBUTES);assert(GetFileAttributesW(cache.path.c_str())==INVALID_FILE_ATTRIBUTES);
  assert(local::Recycle(folder,root,error));assert(GetFileAttributesW(folder.path.c_str())==INVALID_FILE_ATTRIBUTES);assert(RemoveDirectoryW(root.c_str()));CoUninitialize();std::cout<<"Recycle folder, CBZ sidecar, linked archive cache and protected root checks: PASS\n";return 0;
 }
 // Validate the actual .lnk writer in test data, without registering a real login task.
 const auto shortcut=paths::Join(paths::DataDir(),L"startup-test.lnk");
 assert(startup::SetEnabled(true,shortcut));
 Microsoft::WRL::ComPtr<IShellLinkW> link; Microsoft::WRL::ComPtr<IPersistFile> file;
 assert(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link))));assert(SUCCEEDED(link.As(&file)));
 assert(SUCCEEDED(file->Load(shortcut.c_str(),STGM_READ)));
 wchar_t args[256],exe[32768],actual[32768];WIN32_FIND_DATAW data{};
 assert(SUCCEEDED(link->GetArguments(args,256)) && std::wstring(args)==L"--startup");
 assert(SUCCEEDED(link->GetPath(exe,32768,&data,SLGP_RAWPATH)));GetModuleFileNameW(nullptr,actual,32768);assert(_wcsicmp(exe,actual)==0);
 file.Reset();link.Reset();assert(startup::SetEnabled(false,shortcut));assert(GetFileAttributesW(shortcut.c_str())==INVALID_FILE_ATTRIBUTES);
 Settings savedSettings; savedSettings.filePath=paths::Join(paths::DataDir(),L"feature-settings.ini");savedSettings.startOnLogin=true;savedSettings.bgScanInterval=300;savedSettings.siteLanguages={L"tr",L"en"};savedSettings.hiddenSites={L"TR Fixture",L"Hidden Source"};assert(savedSettings.Save());
 Settings loadedSettings;assert(loadedSettings.Load(savedSettings.filePath));assert(loadedSettings.startOnLogin && loadedSettings.bgScanInterval==300);
 assert(loadedSettings.siteLanguages==savedSettings.siteLanguages && loadedSettings.hiddenSites==savedSettings.hiddenSites);
 for(const auto& language:Languages){savedSettings.lang=language.value;assert(savedSettings.Save());Settings roundTrip;assert(roundTrip.Load(savedSettings.filePath));assert(roundTrip.lang==language.value);if(language.value==Lang::Auto)continue;i18n::Set(language.value);assert(i18n::Current()==language.value);for(int i=0;i<(int)S::Count_;++i)assert(T((S)i)[0] && std::wstring(T((S)i))!=L"?");assert(i18n::Text(L"Listeden kaldır")!=L"Listeden kaldır" || language.value==Lang::Tr);assert(i18n::English(i18n::Text(L"Başka kaynaklarda ara"))==L"Search other sources");}
 assert(i18n::Next()==Lang::Tr);i18n::Set(Lang::Tr);
 Library library; const auto path=paths::Join(paths::DataDir(),L"ui-library.json"); DeleteFileW(path.c_str()); DeleteFileW((path+L".bak").c_str()); library.Load(path);
 std::vector<Series> series;
 for(int i=0;i<3;++i) { Series a; a.name="One Piece"; a.mirror="Source"+std::to_string(i); a.url="https://source"+std::to_string(i)+".invalid/title/one"; a.language="en"; a.key=amr::MangaKey(a.mirror,a.url,a.language); assert(library.AddSeries(a)); assert(library.StoreChapters(a.key,{{"Chapter 1",a.url+"/1"},{"Chapter 2",a.url+"/2"}})); series.push_back(a); }
 LibraryView view; Settings settings; settings.libraryGroup=true;
 auto rebuild=[&](){ LibraryViewTestAccess::Rebuild(view,library,settings); };
 rebuild(); assert(LibraryViewTestAccess::Items(view).size()==1 && LibraryViewTestAccess::Items(view)[0].isGroupHeader && !LibraryViewTestAccess::Items(view)[0].caughtUp);
 for(int i=0;i<2;++i) assert(library.MarkReadUpToLatest(series[i].key));
 rebuild(); assert(!LibraryViewTestAccess::Items(view)[0].caughtUp);
 // Filtering the visible children must not hide an unfinished source from the aggregate.
 LibraryViewTestAccess::Search(view,L"Source0"); rebuild(); assert(LibraryViewTestAccess::Items(view).size()==1 && LibraryViewTestAccess::Items(view)[0].caughtUp);
 LibraryViewTestAccess::Search(view,L""); assert(library.MarkReadUpToLatest(series[2].key)); rebuild(); assert(LibraryViewTestAccess::Items(view)[0].caughtUp);
 LibraryViewTestAccess::ExpandFirst(view); rebuild(); assert(LibraryViewTestAccess::Items(view).size()==4); for(const auto& row:LibraryViewTestAccess::Items(view)) assert(row.caughtUp);
 assert(library.ResetReadingProgress(series[1].key)); rebuild(); assert(!LibraryViewTestAccess::Items(view)[0].caughtUp && !LibraryViewTestAccess::Items(view)[2].caughtUp);
 // Failed sources are lower priority than unread and read healthy sources.
 assert(library.StoreUpdateError(series[1].key,"fixture outage")); rebuild();
 assert(LibraryViewTestAccess::Items(view)[0].caughtUp && !LibraryViewTestAccess::Items(view)[0].updateFailed);
 assert(LibraryViewTestAccess::Items(view)[2].updateFailed);
 assert(library.ResetReadingProgress(series[0].key)); rebuild();
 assert(!LibraryViewTestAccess::Items(view)[0].caughtUp && !LibraryViewTestAccess::Items(view)[0].updateFailed);
 for(const auto& entry:series) assert(library.StoreUpdateError(entry.key,"fixture outage")); rebuild();
 assert(LibraryViewTestAccess::Items(view)[0].updateFailed);
 Library restored; restored.Load(path); assert(!restored.FindSeries(series[1].key)->lastUpdateError.empty());
 assert(library.StoreChapters(series[0].key,library.FindSeries(series[0].key)->chapters)); rebuild();
 assert(!LibraryViewTestAccess::Items(view)[0].updateFailed && !LibraryViewTestAccess::Items(view)[0].caughtUp);
 // All seven source buttons must be actionable, including wrapped sources.
 Library searchLibrary; const auto searchPath=paths::Join(paths::DataDir(),L"ui-search-library.json"); DeleteFileW(searchPath.c_str()); DeleteFileW((searchPath+L".bak").c_str()); searchLibrary.Load(searchPath);
 TestHost host(searchLibrary); LibraryView searchView;
 auto result=LibraryViewTestAccess::MakeResult(); result.title="One Piece"; result.titleW=L"One Piece"; result.initial=L"O";
 for(int i=0;i<7;++i) {
  auto name="Test Source "+std::to_string(i); auto url="https://source"+std::to_string(i)+".invalid/title/one";
  result.mirrors.push_back({name,url}); if(i<4) host.AddSeriesToLibrary(name,url,L"One Piece");
 }
 LibraryViewTestAccess::SetResult(searchView,result); host.added=0;
 HWND window=CreateWindowExW(0,L"STATIC",L"Library test",WS_OVERLAPPEDWINDOW,0,0,720,720,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr); assert(window);
 Renderer renderer; assert(renderer.Init(window,96,&error)); renderer.Resize(720,720); Ui ui;
 const auto area=Rc(0,0,720,720); double now=0;
 auto frame=[&](const Input& input){ ui.Begin(input,now+=0.1); assert(renderer.Begin()); LibraryViewTestAccess::Draw(searchView,renderer,ui,area,host,settings,searchLibrary,now); renderer.End(); ui.End(); };
 auto clickSource=[&](int index){
  // Match the actual tooltip for this source. This also verifies hit boxes after wrapping.
  float used=66, x=76+66, y=74+38+42; const float available=630;
  for(int i=0;i<=index;++i) {
   auto label=L"✓ "+std::wstring(L"Test Source ")+std::to_wstring(i);
   float width=renderer.MeasureText(label,11,Renderer::Weight::Semibold)+20;
   if(used+width>available) {used=0;x=76;y+=28;}
   if(i==index) {
    frame(Input{x+width/2,y+11,true,true,false});
    assert(!ui.TipText().empty());
    frame(Input{x+width/2,y+11,false,false,true}); break;
   }
   used+=width+6; x+=width+6;
  }
 };
 clickSource(4); assert(host.added==1 && searchLibrary.All().size()==5);
 assert(searchLibrary.FindSource(result.mirrors[4].mirror,result.mirrors[4].url));
 clickSource(4); assert(host.added==1); // Already selected source does not get duplicated.
 clickSource(6); assert(host.added==2 && searchLibrary.All().size()==6);
 assert(LibraryViewTestAccess::Selected(searchView)==6);
 // Search slots preserve independent results; cancellation retains the visible results.
 auto firstSession=LibraryViewTestAccess::Session(searchView); firstSession->query=L"One Piece";
 for(int i=1;i<5;++i) {assert(LibraryViewTestAccess::New(searchView,host)); auto tab=LibraryViewTestAccess::Session(searchView);tab->query=L"Query "+std::to_wstring(i);LibraryViewTestAccess::SetResult(searchView,result);}
 assert(LibraryViewTestAccess::Count(searchView)==5);assert(!LibraryViewTestAccess::New(searchView,host));
 auto lastSession=LibraryViewTestAccess::Session(searchView);assert(lastSession->query==L"Query 4");
 LibraryViewTestAccess::Select(searchView,0);assert(LibraryViewTestAccess::Selected(searchView)==6);
 LibraryViewTestAccess::ScrollableSearch(searchView);assert(searchView.OnKey(VK_NEXT));assert(firstSession->scroll==450);LibraryViewTestAccess::Select(searchView,4);LibraryViewTestAccess::Select(searchView,0);assert(firstSession->scroll==450);
 firstSession->searching=true;const auto generation=firstSession->generation.load();LibraryViewTestAccess::Cancel(searchView);
 assert(!firstSession->searching && firstSession->generation==generation+1 && firstSession->results.size()==1);
 LibraryViewTestAccess::Close(searchView,0);assert(LibraryViewTestAccess::Count(searchView)==4);assert(LibraryViewTestAccess::New(searchView,host));assert(lastSession->query==L"Query 4");
 // A card click opens details without opening the reader. Next chapter follows catalogue order.
 TestHost cardHost(library); LibraryView cardView;Settings cardSettings;cardSettings.libraryGroup=false;
 LibraryViewTestAccess::Rebuild(cardView,library,cardSettings);
 auto cardFrame=[&](const Input& input,bool detail){ui.Begin(input,now+=0.1);assert(renderer.Begin());if(detail) LibraryViewTestAccess::Detail(cardView,renderer,ui,area,cardSettings,library,cardHost);else LibraryViewTestAccess::Grid(cardView,renderer,ui,area,cardSettings,library,cardHost);renderer.End();ui.End();};
 cardFrame(Input{80,120,true,true,false},false);cardFrame(Input{80,120,false,false,true},false);
 assert(LibraryViewTestAccess::SelectedRow(cardView)>=0 && cardHost.opened==0);
 const auto cardKey=LibraryViewTestAccess::SelectedKey(cardView);const auto expectedChapter=library.FindSeries(cardKey)->chapters.back().url;
 cardFrame(Input{},true);
 // 720 wide area produces a 680x600 detail card, centered at (20,60).
 cardFrame(Input{170,171,true,true,false},true);cardFrame(Input{170,171,false,false,true},true);
 assert(cardHost.opened==1 && cardHost.openedChapter==expectedChapter);
 // The chapter popup used by list and card modes supports both progress actions.
 LibraryViewTestAccess::Rebuild(cardView,library,cardSettings);LibraryViewTestAccess::SelectDetail(cardView,0,1);
 const auto progressKey=LibraryViewTestAccess::SelectedKey(cardView);
 cardFrame(Input{180,209,true,true,false},true);cardFrame(Input{180,209,false,false,true},true);
 assert(cardHost.progressChanges.back()==std::make_pair(progressKey,true));assert(IsCaughtUp(*library.FindSeries(progressKey)));
 LibraryViewTestAccess::Rebuild(cardView,library,cardSettings);
 cardFrame(Input{520,209,true,true,false},true);cardFrame(Input{520,209,false,false,true},true);
 assert(cardHost.progressChanges.back()==std::make_pair(progressKey,false));assert(library.FindSeries(progressKey)->lastChapterUrl.empty());assert(cardHost.opened==1);
 for(const auto& language:Languages) {
  if(language.value==Lang::Auto)continue;i18n::Set(language.value);
  ui.Begin(Input{120,209,false,false,false},now+=0.1);assert(renderer.Begin());
  LibraryViewTestAccess::Detail(cardView,renderer,ui,Rc(0,0,400,720),cardSettings,library,cardHost);renderer.End();
  assert(ui.TipText()==i18n::Literal(L"✅ Son Sayıya Kadar Okundu"));ui.End();
 }
 i18n::Set(Lang::Tr);
 // Removing from chapter details is available in its fixed header.
 Library deleteLibrary;const auto deleteFile=paths::Join(paths::DataDir(),L"card-delete-library.json");DeleteFileW(deleteFile.c_str());DeleteFileW((deleteFile+L".bak").c_str());deleteLibrary.Load(deleteFile);
 for(int i=0;i<7;++i){Series item;item.name="Delete fixture";item.mirror="Removal Source "+std::to_string(i);item.url="https://remove.invalid/"+std::to_string(i);item.language="en";item.key=amr::MangaKey(item.mirror,item.url,item.language);assert(deleteLibrary.AddSeries(item));}
 LibraryView deleteView;Settings deleteSettings;TestHost deleteHost(deleteLibrary);
 auto deleteFrame=[&](const Input& input,const D2D1_RECT_F& box=Rc(0,0,720,720)){ui.Begin(input,now+=0.1);assert(renderer.Begin());LibraryViewTestAccess::Detail(deleteView,renderer,ui,box,deleteSettings,deleteLibrary,deleteHost);renderer.End();ui.End();};
 LibraryViewTestAccess::Rebuild(deleteView,deleteLibrary,deleteSettings);LibraryViewTestAccess::SelectDetail(deleteView,0,1);const auto removedKey=LibraryViewTestAccess::SelectedKey(deleteView);
 deleteFrame(Input{643,87,true,true,false});deleteFrame(Input{643,87,false,false,true});assert(deleteLibrary.All().size()==6 && deleteHost.removedSeries.back()==removedKey && LibraryViewTestAccess::SelectedRow(deleteView)==-1);
 // Seven grouped sources remain reachable by scrolling, including the last delete action.
 Series extra;extra.name="Delete fixture";extra.mirror="Removal Source extra";extra.url="https://remove.invalid/extra";extra.language="en";extra.key=amr::MangaKey(extra.mirror,extra.url,extra.language);assert(deleteLibrary.AddSeries(extra));
 LibraryViewTestAccess::Rebuild(deleteView,deleteLibrary,deleteSettings);LibraryViewTestAccess::SelectDetail(deleteView,0,0);deleteFrame(Input{});assert(LibraryViewTestAccess::SourceMaxScroll(deleteView)>0);deleteView.OnWheel(-999);assert(LibraryViewTestAccess::SourceScroll(deleteView)==LibraryViewTestAccess::SourceMaxScroll(deleteView));
 deleteFrame(Input{624,570,true,true,false});deleteFrame(Input{624,570,false,false,true});assert(deleteLibrary.All().size()==6);
 LibraryViewTestAccess::Rebuild(deleteView,deleteLibrary,deleteSettings);LibraryViewTestAccess::SelectDetail(deleteView,0,0);
 deleteFrame(Input{613,137,true,true,false});deleteFrame(Input{613,137,false,false,true});assert(deleteLibrary.All().empty());
 // Tiny buttons use icons and expose the full translated action as a tooltip.
 for(const auto& language:Languages){if(language.value==Lang::Auto)continue;i18n::Set(language.value);const auto button=RcWH(10,10,32,32);const auto caption=i18n::Text(L"Başka kaynaklarda ara");assert(w::ButtonUsesIcon(renderer,button,caption));ui.Begin(Input{26,26,false,false,false},now+=0.1);assert(renderer.Begin());w::TextButton(renderer,ui,Id("test.icon.fallback"),button,caption,false);renderer.End();assert(ui.TipText()==caption);ui.End();}
 i18n::Set(Lang::Ja);ui.Begin(Input{60,26,false,false,false},now+=0.1);assert(renderer.Begin());w::TextButton(renderer,ui,Id("test.user.title"),RcWH(10,10,200,32),L"New",false);renderer.End();assert(ui.TipText()==L"New");ui.End();
 i18n::Set(Lang::En);LibraryViewTestAccess::Rebuild(cardView,library,cardSettings);assert(LibraryViewTestAccess::Language(cardView)==Lang::En);
 assert(std::any_of(LibraryViewTestAccess::Rows(cardView).begin(),LibraryViewTestAccess::Rows(cardView).end(),[](const auto& row){return row.chapterLabel.find(L"Chapter")!=std::wstring::npos;}));
 i18n::Set(Lang::De);LibraryViewTestAccess::Rebuild(cardView,library,cardSettings);assert(LibraryViewTestAccess::Language(cardView)==Lang::De);
 assert(std::any_of(LibraryViewTestAccess::Rows(cardView).begin(),LibraryViewTestAccess::Rows(cardView).end(),[](const auto& row){return row.chapterLabel.find(L"Kapitel")!=std::wstring::npos;}));
 i18n::Set(Lang::Tr);
 SettingsView languageView;Settings languageSettings;TestHost languageHost(library);
 auto languageFrame=[&](const Input& input){ui.Begin(input,now+=0.1);assert(renderer.Begin());languageView.Draw(renderer,ui,area,languageSettings,library,languageHost);renderer.End();ui.End();};
 languageFrame(Input{634,255,true,true,false});languageFrame(Input{634,255,false,false,true});assert(languageSettings.lang==Lang::Az && languageHost.saved==1);
 ReaderView reader; ReaderViewTestAccess::Setup(reader,settings);
 // The real reader receives already localized titles from MainWindow. Short
 // titles must remain safe when the sidebar formats them a second time.
 for(int language=1;language<(int)Lang::Count_;++language){
  i18n::Set((Lang)language);
  for(const auto& title:std::vector<std::wstring>{L"Chapter 1",L"Chapter 12",L"Bölüm 1",L"Manga: Chapter 1",std::wstring(i18n::Literal(L"Bölüm "))+L"1",L"Manga: "+std::wstring(i18n::Literal(L"Bölüm "))+L"1"}){
   ui.Begin(Input{},now+=0.1);assert(renderer.Begin());
   try {ReaderViewTestAccess::ChapterSidebar(reader,renderer,ui,title);}
   catch(const std::exception& e){std::cerr<<"Reader short chapter title crash reproduced: "<<e.what()<<"\n";return 2;}
   renderer.End();ui.End();
  }
 }
 i18n::Set(Lang::Tr);
 auto drawScroll=[&]{assert(renderer.Begin());ReaderViewTestAccess::Draw(reader,renderer);renderer.End();};
 drawScroll();auto track=ReaderViewTestAccess::Track(reader);auto thumb=ReaderViewTestAccess::Thumb(reader);
 assert(thumb.top==track.top && thumb.bottom<track.bottom);
 assert(reader.OverChrome(track.left+5,thumb.top+5));assert(!reader.OnDoubleClick(track.left+5,thumb.top+5));
 assert(reader.OnMouseDown(track.left+5,thumb.top+5,now));
 reader.OnMouseMove(track.left+5,track.bottom+100,now); reader.OnMouseUp();drawScroll();
 assert(ReaderViewTestAccess::Scroll(reader)==4280 && !reader.Dragging());
 reader.OnMouseDown(track.left+5,track.top-100,now); reader.OnMouseUp();
 // Track clicks move to the clicked document position.
 reader.OnMouseDown(track.left+5,track.top+1,now);reader.OnMouseUp();drawScroll();assert(ReaderViewTestAccess::Scroll(reader)==0);
 ReaderViewTestAccess::Menu(reader,true);drawScroll();assert(ReaderViewTestAccess::Track(reader).right<430 && ReaderViewTestAccess::Track(reader).bottom<580);
 ReaderViewTestAccess::Menu(reader,false);drawScroll();assert(ReaderViewTestAccess::Track(reader).right>700);
 ReaderViewTestAccess::Short(reader);drawScroll();assert(RcH(ReaderViewTestAccess::Track(reader))==0);
 ReaderViewTestAccess::HideSidebar(reader);
 auto handleFrame=[&](const Input& input){ui.Begin(input,now+=0.1);assert(renderer.Begin());ReaderViewTestAccess::SidebarHandle(reader,renderer,ui);renderer.End();ui.End();};
 handleFrame(Input{});assert(RcW(ReaderViewTestAccess::Handle(reader))==108);
 handleFrame(Input{640,28,true,true,false});handleFrame(Input{640,28,false,false,true});assert(ReaderViewTestAccess::SidebarOpen(reader));assert(RcW(ReaderViewTestAccess::Handle(reader))==0);
 // Resolve old hashed caches and discover downloads beyond the eight recent paths.
 const auto dlRoot=paths::Join(paths::DataDir(),L"local-review-"+std::to_wstring(GetTickCount64()));paths::EnsureDir(dlRoot);
 const auto rawParent=paths::Join(dlRoot,L"opaque-series-folder");paths::EnsureDir(rawParent);
 const auto resource=paths::Join(paths::ExeDir(),L"..\\..\\..\\resources\\pirate-logo.png");
 std::vector<local::Entry> downloaded;
 for(int i=0;i<2;++i){local::Entry item;item.path=paths::Join(rawParent,L"opaque-"+std::to_wstring(i));paths::EnsureDir(item.path);assert(CopyFileW(resource.c_str(),paths::Join(item.path,L"001.png").c_str(),FALSE));item.seriesTitle=L"One Piece";item.chapterTitle=L"Chapter "+std::to_wstring(i+1);item.seriesKey=series[0].key;item.mirror=series[0].mirror;item.mangaUrl=series[0].url;item.chapterUrl=series[0].url+"/"+std::to_string(i+1);item.downloadedAt=1730000000000LL+i*1000;item.recordedDownload=true;assert(local::StoreInfo(item));downloaded.push_back(item);}
 auto preserved=local::Describe(downloaded[0].path,library);assert(preserved.seriesTitle==L"One Piece" && preserved.chapterTitle==L"Chapter 1");assert(local::StoreInfo(preserved));assert(local::Describe(preserved.path,library).downloadedAt==downloaded[0].downloadedAt);
 const auto legacyCache=engine::ChapterLoader::Instance().GetCacheDir(series[0].mirror,series[0].url,series[0].url+"/2");assert(CopyFileW(resource.c_str(),paths::Join(legacyCache,L"0001.png").c_str(),FALSE));assert(fileio::WriteAtomic(paths::Join(legacyCache,L".complete"),"1"));
 const auto legacy=local::Describe(legacyCache,library);assert(legacy.seriesTitle==L"One Piece" && legacy.chapterTitle==L"Chapter 2" && legacy.seriesKey==series[0].key);
 local::Entry archive;archive.path=paths::Join(rawParent,L"opaque-file.cbz");archive.seriesTitle=L"Berserk";archive.chapterTitle=L"Chapter 99";archive.archive=true;archive.downloadedAt=1730200000000LL;archive.recordedDownload=true;assert(engine::CbzWriter::PackFolder(downloaded[0].path,archive.path));assert(local::StoreInfo(archive));
 const auto archiveCache=paths::Join(dlRoot,L"extracted-archive");paths::EnsureDir(archiveCache);assert(CopyFileW(resource.c_str(),paths::Join(archiveCache,L"001.png").c_str(),FALSE));auto extracted=archive;extracted.path=archiveCache;extracted.archive=false;extracted.originalPath=archive.path;assert(local::StoreInfo(extracted));
 auto entries=local::Collect({legacyCache,archiveCache,downloaded[0].path},dlRoot,library);
 assert(std::count_if(entries.begin(),entries.end(),[&](const auto& e){return e.path==archive.path;})==1);
 auto a=std::find_if(entries.begin(),entries.end(),[&](const auto& e){return e.path==downloaded[0].path;});auto b=std::find_if(entries.begin(),entries.end(),[&](const auto& e){return e.path==downloaded[1].path;});assert(a!=entries.end() && b!=entries.end() && b<a);
 LibraryView localView;Settings localSettings;localSettings.libraryGroup=true;localSettings.libraryViewMode=1;localSettings.downloadDir=dlRoot;localSettings.recent={legacyCache,archiveCache};
 LibraryViewTestAccess::LocalFilter(localView,L"One Piece");LibraryViewTestAccess::LocalRebuild(localView,localSettings,library);assert(LibraryViewTestAccess::Locals(localView).size()==1 && LibraryViewTestAccess::Locals(localView)[0].chapters.size()>=3);
 LibraryViewTestAccess::LocalFilter(localView,L"Berserk");LibraryViewTestAccess::LocalRebuild(localView,localSettings,library);assert(LibraryViewTestAccess::Locals(localView).size()==1 && LibraryViewTestAccess::Locals(localView)[0].chapters.size()==1);
 TestHost localHost(library);auto localFrame=[&](const Input& input){ui.Begin(input,now+=0.1);assert(renderer.Begin());LibraryViewTestAccess::LocalDraw(localView,renderer,ui,area,localHost,localSettings,library);renderer.End();ui.End();};
 localFrame(Input{80,120,true,true,false});localFrame(Input{80,120,false,false,true});assert(LibraryViewTestAccess::LocalSelected(localView) && localHost.openedPaths==0);
 localFrame(Input{570,151,true,true,false});localFrame(Input{570,151,false,false,true});assert(localHost.openedPaths==1 && localHost.openedPath==archive.path);
 localFrame(Input{80,120,true,true,false});localFrame(Input{80,120,false,false,true});
 localFrame(Input{640,151,true,true,false});localFrame(Input{640,151,false,false,true});assert(LibraryViewTestAccess::PendingDelete(localView) && localHost.deletedPaths==0);
 localView.OnKey(VK_ESCAPE);assert(!LibraryViewTestAccess::PendingDelete(localView) && LibraryViewTestAccess::LocalSelected(localView));
 localFrame(Input{640,151,true,true,false});localFrame(Input{640,151,false,false,true});
 localFrame(Input{653,629,true,true,false});localFrame(Input{653,629,false,false,true});assert(localHost.deletedPaths==1 && localHost.deletedPath==archive.path);
 assert(local::StoreInfo(downloaded[0],true));const auto recorded=local::Describe(downloaded[0].path,library);assert(recorded.recordedDownload && recorded.downloadedAt>downloaded[0].downloadedAt);assert(local::StoreInfo(recorded));assert(local::Describe(recorded.path,library).downloadedAt==recorded.downloadedAt);
 const auto sessionFile=paths::Join(paths::DataDir(),L"session_tabs.json");std::string oldSession;const auto oldResult=fileio::ReadText(sessionFile,4*1024*1024,oldSession);
 auto recoveredTab=json::Value::Obj();recoveredTab.Add("chapterDir",json::Value::Str(ToUtf8(legacyCache)));recoveredTab.Add("mangaTitle",json::Value::Str("Recovered Session Manga"));recoveredTab.Add("chapterTitle",json::Value::Str("Chapter 88"));auto recoveredTabs=json::Value::Arr();recoveredTabs.Push(std::move(recoveredTab));auto recoveredRoot=json::Value::Obj();recoveredRoot.Add("tabs",std::move(recoveredTabs));assert(fileio::WriteAtomic(sessionFile,json::Dump(recoveredRoot)));
 Library emptyLocalLibrary;const auto recovered=local::Describe(legacyCache,emptyLocalLibrary);assert(recovered.seriesTitle==L"Recovered Session Manga" && recovered.chapterTitle==L"Chapter 88");
 if(oldResult==fileio::ReadResult::Ok)assert(fileio::WriteAtomic(sessionFile,oldSession));else DeleteFileW(sessionFile.c_str());
 // Exercise the real asynchronous search path with deterministic delayed source responses.
 auto& mirrors=engine::MirrorEngine::Instance();assert(mirrors.Init());
 auto& js=mirrors.Engine();auto installSearchFixture=[&]{auto fixture=js.Eval(R"JS(
 globalThis.__getAllMirrors=()=>[{name:'TR Fixture',language:'tr',home:'https://fixture.invalid',disabled:false},{name:'EN Fixture',language:'en',home:'https://fixture.invalid',disabled:false},{name:'Multi Fixture',language:'tr,en',home:'https://fixture.invalid',disabled:false}];
 globalThis.__searchManga=async function(name,query){await new Promise(resolve=>setTimeout(resolve,query==='Cancelled'?500:30));return [{title:query,url:'https://fixture.invalid/'+query}];};
 globalThis.__getChapters=async function(){await new Promise(resolve=>setTimeout(resolve,20));return [{title:'Chapter 1',url:'https://fixture.invalid/chapter/1'}];};
 globalThis.__getChapterImages=async function(){await new Promise(resolve=>setTimeout(resolve,20));return ['https://fixture.invalid/page/1.png'];};
 )JS","<search-session-fixture>");assert(!JS_IsException(fixture));JS_FreeValue(js.Ctx(),fixture);};installSearchFixture();
 assert(mirrors.GetMirrors().size()==3);
 LibraryView siteView;Settings siteSettings;TestHost siteHost(library);
 LibraryViewTestAccess::SitesFilter(siteView,siteSettings,false);assert(LibraryViewTestAccess::SitesCount(siteView)==3);
 siteSettings.siteLanguages={L"tr"};LibraryViewTestAccess::SitesFilter(siteView,siteSettings,false);assert(LibraryViewTestAccess::SitesCount(siteView)==2);
 siteSettings.siteLanguages={L"en"};LibraryViewTestAccess::SitesFilter(siteView,siteSettings,false);assert(LibraryViewTestAccess::SitesCount(siteView)==2);
 siteSettings.siteLanguages={L"tr",L"en"};LibraryViewTestAccess::SitesFilter(siteView,siteSettings,false);assert(LibraryViewTestAccess::SitesCount(siteView)==3);
 siteSettings.hiddenSites={L"TR Fixture"};LibraryViewTestAccess::SitesFilter(siteView,siteSettings,false);assert(LibraryViewTestAccess::SitesCount(siteView)==2);
 LibraryViewTestAccess::SitesFilter(siteView,siteSettings,true);assert(LibraryViewTestAccess::SitesCount(siteView)==1);
 LibraryViewTestAccess::Search(siteView,L"EN Fixture");LibraryViewTestAccess::SitesFilter(siteView,siteSettings,true);assert(LibraryViewTestAccess::SitesCount(siteView)==0);
 LibraryViewTestAccess::Search(siteView,L"");siteSettings.hiddenSites.clear();LibraryViewTestAccess::SitesFilter(siteView,siteSettings,false);
 auto siteFrame=[&](const Input& input){ui.Begin(input,now+=0.1);assert(renderer.Begin());LibraryViewTestAccess::SitesDraw(siteView,renderer,ui,area,siteHost,siteSettings);renderer.End();ui.End();};
 siteFrame(Input{});
 siteFrame(Input{663,139,true,true,false});siteFrame(Input{663,139,false,false,true});assert(siteSettings.hiddenSites==std::vector<std::wstring>{L"TR Fixture"} && siteHost.saved==1);
 siteFrame(Input{300,58,true,true,false});siteFrame(Input{300,58,false,false,true});assert(LibraryViewTestAccess::SitesCount(siteView)==1);
 siteFrame(Input{663,139,true,true,false});siteFrame(Input{663,139,false,false,true});assert(siteSettings.hiddenSites.empty() && siteHost.saved==2);
 LibraryView asyncView;std::vector<decltype(firstSession)> sessions;
 for(int i=0;i<5;++i){asyncView.SearchOtherSources(L"Manga "+std::to_wstring(i),host);sessions.push_back(LibraryViewTestAccess::Session(asyncView));}
 // Reader rendering and chapter/page enumeration while four or more real
 // search workers are active: the sequence reported in the crash.
 auto chapterTask=std::async(std::launch::async,[&]{return mirrors.GetChapters("TR Fixture","https://fixture.invalid/manga");});
 auto imageTask=std::async(std::launch::async,[&]{return mirrors.GetChapterImages("TR Fixture","https://fixture.invalid/chapter/1");});
 for(int i=0;i<30;++i){
  ui.Begin(Input{},now+=0.1);assert(renderer.Begin());
  ReaderViewTestAccess::ChapterSidebar(reader,renderer,ui,L"Bölüm 1");
  renderer.End();ui.End();std::this_thread::sleep_for(std::chrono::milliseconds(5));
 }
 const auto chapterResult=chapterTask.get();assert(chapterResult.size()==1 && chapterResult[0].title=="Chapter 1");
 const auto imageResult=imageTask.get();assert(imageResult.size()==1 && imageResult[0]=="https://fixture.invalid/page/1.png");
 assert(LibraryViewTestAccess::Count(asyncView)==5);asyncView.SearchOtherSources(L"Overflow",host);assert(LibraryViewTestAccess::Session(asyncView)==sessions.back());
 const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
 for(const auto& session:sessions){while(session->searching && std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(10));assert(!session->searching);std::lock_guard<std::mutex> lock(session->mutex);assert(session->results.size()==1 && session->results.front().titleW==session->query);}
 LibraryViewTestAccess::Close(asyncView,0);asyncView.SearchOtherSources(L"Cancelled",host);auto cancelled=LibraryViewTestAccess::Session(asyncView);
 std::this_thread::sleep_for(std::chrono::milliseconds(50));LibraryViewTestAccess::Cancel(asyncView);
 const auto cancelledStatus=cancelled->status;LibraryViewTestAccess::Close(asyncView,4);
 // Cancellation can discard a pending VM. Production providers reload from the
 // bundle; these runtime-only test providers must be installed again afterward.
 assert(mirrors.Init());installSearchFixture();
 asyncView.SearchOtherSources(L"Replacement",host);auto replacement=LibraryViewTestAccess::Session(asyncView);
 while(replacement->searching && std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(10));
 asyncView.StopWorkers();assert(cancelled->results.empty() && cancelled->status==cancelledStatus);assert(replacement->results.size()==1 && replacement->results[0].titleW==L"Replacement");
 mirrors.Shutdown();
 renderer.Shutdown(); DestroyWindow(window);CoUninitialize();
 std::cout<<"Reader short/localized chapter titles in 15 languages and concurrent search/chapter/page operations: PASS\n";
 std::cout<<"15 languages, settings round-trip, live language cache refresh, compact icon buttons and full tooltips; Card source/group removal and scrolling to last source; Local metadata, download sorting, deletion confirmation, search tabs/cancellation and reader menu/scrollbar: PASS\n";
}
