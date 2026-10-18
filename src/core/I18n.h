#pragma once
//
// 15 dilli gomulu katalog. Duzenlenebilir kaynak: resources/i18n/*.tsv.
// Dil, Windows arayuz dilinden secilir; kullanici ezebilir (FR-I18N-002).
//
#include "core/Settings.h"

namespace gm {

// X(Kimlik, Turkce, Ingilizce)
#define GM_STRINGS(X) \
    X(AppName,          L"GhostManga",                          L"GhostManga") \
    X(Library,          L"Kütüphane",                           L"Library") \
    X(Search,           L"Ara",                                 L"Search") \
    X(Downloads,        L"İndirmeler",                          L"Downloads") \
    X(Bookmarks,        L"Yer imleri",                          L"Bookmarks") \
    X(OpenLocal,        L"Yerel klasör aç",                     L"Open local folder") \
    X(Sites,            L"Siteler",                             L"Sites") \
    X(SettingsTitle,    L"Ayarlar",                             L"Settings") \
    X(About,            L"Hakkında",                            L"About") \
    X(Categories,       L"KATEGORİLER",                         L"CATEGORIES") \
    X(CatNew,           L"Yeni",                                L"New") \
    X(CatUnread,        L"Okunmamış",                           L"Unread") \
    X(CatOneshots,      L"Tek atımlık",                         L"One-shots") \
    X(AddCategory,      L"Kategori ekle",                       L"Add category") \
    X(LastScanNever,    L"Son tarama: henüz yok",               L"Last scan: never") \
    X(ScanNow,          L"Şimdi tara",                          L"Scan now") \
    X(SoonTag,          L"yakında",                             L"soon") \
    X(SoonCats,         L"Kategori ekleme ve filtreleme M3 ile geliyor. AMR kategorilerin şimdiden içe aktarılıyor.", L"Adding and filtering categories arrives in M3. Your AMR categories are already imported.") \
    X(SoonM3,          L"Bu özellik M3'te geliyor: site motoru ve arama.",        L"Coming in M3: site engine and search.") \
    X(SoonM5,           L"Bu özellik M5'te geliyor: indirme ve çevrimdışı.",       L"Coming in M5: downloads and offline.") \
    X(SoonM6,           L"Bu özellik M6'da geliyor: arka plan taraması.",           L"Coming in M6: background scanning.") \
    X(SoonM7,           L"Bu özellik M7'de geliyor: yer imleri ve senkron.",        L"Coming in M7: bookmarks and sync.") \
    X(EmptyLibTitle,    L"Kütüphanen henüz boş",                L"Your library is empty") \
    X(EmptyLibBody,     L"AMR okuma listeni içe aktar ya da bilgisayarındaki bir bölüm klasörünü aç.", L"Import your AMR reading list or open a chapter folder from your computer.") \
    X(ImportAmr,        L"AMR listeni içe aktar",               L"Import AMR list") \
    X(DropHint,         L"İpucu: bir bölüm klasörünü ya da AMR JSON dosyasını pencereye sürükleyip bırakabilirsin.", L"Tip: you can drop a chapter folder or an AMR JSON file onto the window.") \
    X(Recent,           L"Son açılanlar",                       L"Recently opened") \
    X(OpenArrow,        L"Aç",                                  L"Open") \
    X(LibrarySubtitle,  L"Son açtığın yerel bölüm klasörleri.", L"Chapter folders you opened recently.") \
    X(LibrarySeriesSubtitle, L"AMR okuma listen. Satıra tıklayınca kaldığın bölüm tarayıcıda açılır; uygulama içinde okuma M3 ile gelecek.", L"Your AMR reading list. Click a row to open where you left off in the browser; in-app reading arrives in M3.") \
    X(ReadingList,      L"Okuma listesi",                       L"Reading list") \
    X(LocalFolders,     L"Yerel klasörler",                     L"Local folders") \
    X(LastReadLabel,    L"Son okunan:",                         L"Last read:") \
    X(NotFollowing,     L"takip dışı",                          L"not followed") \
    X(OpenOnSite,       L"Sitede aç",                           L"Open on site") \
    X(SortBy,           L"Sırala",                              L"Sort") \
    X(SortUpdate,       L"Güncelleme tarihi",                   L"Update date") \
    X(SortLastRead,     L"Son okunan",                          L"Last read") \
    X(SortName,         L"İsim",                                L"Name") \
    X(SortAdded,        L"Eklenme tarihi",                      L"Date added") \
    X(GroupByName,      L"Grupla",                              L"Group") \
    X(SearchPlaceholder,L"Kütüphanede ara…",                    L"Search library…") \
    X(SourcesCountFmt,  L"%d kaynak",                           L"%d sources") \
    X(JustNow,          L"az önce",                             L"just now") \
    X(MinsAgoFmt,       L"%d dk önce",                          L"%d min ago") \
    X(HoursAgoFmt,      L"%d saat önce",                        L"%d hrs ago") \
    X(DaysAgoFmt,       L"%d gün önce",                         L"%d days ago") \
    X(Yesterday,        L"dün",                                 L"yesterday") \
    X(Ascending,        L"Artan",                               L"Ascending") \
    X(Descending,       L"Azalan",                              L"Descending") \
    X(NoSearchResults,  L"Aramayla eşleşen seri bulunamadı.",   L"No series matched your search.") \
    X(ExportHowTo,      L"AMR'de Import / Export sayfasında Export'a bas. İnen JSON dosyasını seç ya da bu pencereye sürükle.", L"In AMR, open Import / Export and press Export. Pick the downloaded JSON file or drop it onto this window.") \
    X(PickAmrTitle,     L"AMR dışa aktarma dosyasını seç",      L"Choose your AMR export file") \
    X(AmrFileFilter,    L"AMR dışa aktarma (*.json)",           L"AMR export (*.json)") \
    X(AllFilesFilter,   L"Tüm dosyalar",                        L"All files") \
    X(ImportDoneFmt,    L"İçe aktarıldı: %d yeni, %d güncellendi, %d aynı", L"Imported: %d new, %d updated, %d unchanged") \
    X(ImportBookmarksFmt, L"  ·  %d yeni yer imi",              L"  ·  %d new bookmarks") \
    X(ImportSkippedFmt, L"  ·  %d kayıt atlandı",               L"  ·  %d entries skipped") \
    X(ImportErrRead,    L"Dosya okunamadı.",                    L"Could not read the file.") \
    X(ImportErrTooBig,  L"Dosya çok büyük ya da belleğe sığmıyor. AMR dışa aktarma dosyası bu kadar büyük olmaz.", L"The file is too large or does not fit in memory. An AMR export is never this big.") \
    X(ImportErrJsonFmt, L"Dosya JSON olarak okunamadı (satır %zu, sütun %zu).", L"Could not parse the file as JSON (line %zu, column %zu).") \
    X(ImportErrNotAmr,  L"Bu dosya AMR dışa aktarma dosyası değil: içinde “mangas” listesi yok.", L"This is not an AMR export: it has no “mangas” list.") \
    X(ImportErrEmpty,   L"Dosyada içe aktarılacak seri yok.",   L"The file has no series to import.") \
    X(ImportSaveFailed, L"Liste diske yazılamadı, hiçbir şey içe aktarılmadı. library.json'u kullanan bir program varsa kapatıp yeniden dene.", L"The list could not be saved, so nothing was imported. Close any program using library.json and try again.") \
    X(LibRestoredFromBak, L"Kütüphane dosyası bozuktu; son yedekten geri yüklendi. Bozuk dosya veri klasöründe duruyor.", L"The library file was damaged and has been restored from the last backup. The damaged file is kept in the data folder.") \
    X(LibResetCorrupt,  L"Kütüphane dosyası bozuktu ve yedek de okunamadı; liste boş başladı. Bozuk dosya veri klasöründe duruyor.", L"The library file was damaged and the backup could not be read, so the list starts empty. The damaged file is kept in the data folder.") \
    X(LibBlocked,       L"Kütüphane dosyası okunamadı; bu oturumda değişiklikler kaydedilmeyecek.", L"The library file could not be read; changes will not be saved in this session.") \
    X(BadUrl,           L"Bu kaydın açılabilir bir web adresi yok.", L"This entry has no web address to open.") \
    X(LibraryCountFmt, L"%d seri, %d yer imi",                 L"%d series, %d bookmarks") \
    X(Minimize,         L"Küçült",                              L"Minimize") \
    X(HideToTray,       L"Gizle — tepside çalışmaya devam eder", L"Hide — keeps running in the tray") \
    X(Close,            L"Kapat",                               L"Close") \
    X(Menu,             L"Menü",                                L"Menu") \
    X(PrevPage,         L"Önceki sayfa",                        L"Previous page") \
    X(NextPage,         L"Sonraki sayfa",                       L"Next page") \
    X(PrevChapter,      L"Önceki bölüm",                        L"Previous chapter") \
    X(NextChapter,      L"Sonraki bölüm",                       L"Next chapter") \
    X(FitWidth,         L"Genişliğe sığdır",                    L"Fit width") \
    X(FitHeight,        L"Yüksekliğe sığdır",                   L"Fit height") \
    X(FitWindow,        L"Pencereye sığdır",                    L"Fit window") \
    X(FitOriginal,      L"Gerçek boyut (1:1)",                  L"Actual size (1:1)") \
    X(DoublePage,       L"Çift sayfa",                          L"Double page") \
    X(Webtoon,          L"Webtoon — sayfalar arası boşluk yok", L"Webtoon — no gaps between pages") \
    X(Direction,        L"Okuma yönü",                          L"Reading direction") \
    X(ZoomIn,           L"Yakınlaştır",                         L"Zoom in") \
    X(ZoomOut,          L"Uzaklaştır",                          L"Zoom out") \
    X(Bookmark,         L"Yer imi",                             L"Bookmark") \
    X(DownloadChapter,  L"Bu bölümü indir",                     L"Download this chapter") \
    X(ReaderSettings,   L"Okuyucu ayarları",                    L"Reader settings") \
    X(ScaleUp,          L"Küçük görselleri büyüt",              L"Upscale small images") \
    X(MagicScroll,      L"Sihirli kaydırma — sonda sonraki bölüme geç", L"Magic scroll — continue to next chapter") \
    X(CoverAlone,       L"Çift sayfada kapak tek başına",       L"Cover alone in double page") \
    X(HudAutoHide,      L"Alt çubuğu otomatik gizle",           L"Auto-hide bottom bar") \
    X(ReaderDim,        L"Zemin koyuluğu",                      L"Backdrop darkness") \
    X(Page,             L"Sayfa",                               L"Page") \
    X(DecodeFailed,     L"Görsel açılamadı",                    L"Could not decode image") \
    X(NoImages,         L"Bu klasörde okunabilir görsel yok.",  L"No readable images in this folder.") \
    X(ChapterEnd,       L"Bölüm sonu",                          L"End of chapter") \
    X(ContinueHint,     L"Sonraki bölüme geçmek için kaydırmaya devam et", L"Keep scrolling for the next chapter") \
    X(NextLabel,        L"Sonraki:",                            L"Next:") \
    X(LastChapter,      L"Bu son bölüm.",                       L"This is the last chapter.") \
    X(FirstChapter,     L"Bu ilk bölüm.",                       L"This is the first chapter.") \
    X(OpeningChapter,   L"Açılıyor:",                           L"Opening:") \
    X(Loading,          L"Yükleniyor…",                         L"Loading…") \
    X(TrayShow,         L"Göster",                              L"Show") \
    X(TraySettings,     L"Ayarlar",                             L"Settings") \
    X(TrayQuit,         L"Çık",                                 L"Quit") \
    X(TrayStillRunning, L"GhostManga tepside çalışmaya devam ediyor. Tamamen çıkmak için tepsi menüsünden Çık'ı seç.", L"GhostManga keeps running in the tray. Use Quit in the tray menu to exit.") \
    X(TabAppearance,    L"Görünüm",                             L"Appearance") \
    X(TabReader,        L"Okuyucu",                             L"Reader") \
    X(TabLibrary,       L"Kütüphane",                           L"Library") \
    X(TabSites,         L"Siteler",                             L"Sites") \
    X(TabDownloads,     L"İndirme",                             L"Downloads") \
    X(TabBackground,    L"Arka plan",                           L"Background") \
    X(TabSync,          L"Senkron",                             L"Sync") \
    X(TabAdvanced,      L"Gelişmiş",                            L"Advanced") \
    X(TabAbout,         L"Hakkında",                            L"About") \
    X(Language,         L"Dil",                                 L"Language") \
    X(LangAuto,         L"Otomatik",                            L"Automatic") \
    X(LangTr,           L"Türkçe",                              L"Türkçe") \
    X(LangEn,           L"English",                             L"English") \
    X(FitModeLabel,     L"Boyutlandırma",                       L"Fit mode") \
    X(FitW,             L"Genişlik",                            L"Width") \
    X(FitH,             L"Yükseklik",                           L"Height") \
    X(FitWin,           L"Pencere",                             L"Window") \
    X(Fit11,            L"1:1",                                 L"1:1") \
    X(RtlLabel,         L"Sağdan sola oku (manga)",             L"Read right to left (manga)") \
    X(WebtoonDefault,   L"Webtoon kipi",                        L"Webtoon mode") \
    X(DoublePageLabel,  L"Çift sayfa",                          L"Double page") \
    X(CloseToTray,      L"Kapat düğmesi uygulamayı tepsiye indirsin", L"Close button sends the app to the tray") \
    X(CloseToTrayHint,  L"Kapalıysa Kapat uygulamadan tamamen çıkar. Gizle düğmesi her zaman tepsiye indirir.", L"When off, Close quits the app. Hide always goes to the tray.") \
    X(BackgroundScanNote, L"Denetim uygulama tepsideyken de çalışır. Kapalı seçeneği otomatik denetimi durdurur; elle denetleyebilirsiniz.", L"Checks continue in the system tray. Off disables automatic checks; manual checks remain available.") \
    X(DataFolder,       L"Veri klasörü",                        L"Data folder") \
    X(OpenFolderBtn,    L"Klasörü aç",                          L"Open folder") \
    X(PortableOn,       L"Taşınabilir kip açık: tüm veriler exe'nin yanında.", L"Portable mode on: all data sits next to the exe.") \
    X(PortableOff,      L"Exe klasörü yazılamıyor, veriler bu bilgisayarda tutuluyor.", L"The exe folder is read-only, data is kept on this computer.") \
    X(WebView2Runtime,  L"Edge WebView2 çalışma zamanı",        L"Edge WebView2 runtime") \
    X(NotInstalled,     L"kurulu değil",                        L"not installed") \
    X(WebView2Note,     L"Robot kontrolü ve Turnstile doğrulamasını arka planda çözer.", L"Solves bot checks and Turnstile challenges in the background.") \
    X(VersionLabel,     L"Sürüm",                               L"Version") \
    X(VersionValue,     L"1.0.0 · Tam Sürüm & Çevrimdışı",       L"1.0.0 · Full Engine & Offline") \
    X(StackLabel,       L"Yığın",                               L"Stack") \
    X(StackValue,       L"Saf Win32 · D3D11 · DirectComposition · Direct2D · WIC", L"Pure Win32 · D3D11 · DirectComposition · Direct2D · WIC") \
    X(PrivacyLabel,     L"Gizlilik",                            L"Privacy") \
    X(PrivacyValue,     L"GhostManga'nın kendi kodu hiçbir sunucuya veri göndermez ve kayıt defterine yazmaz.", L"GhostManga's own code sends no data to any server and writes nothing to the registry.") \
    X(TabComingLibrary, L"Aynı dosyayı yeniden içe aktarmak güvenli: yeni seriler eklenir, var olanlar dosyadaki durumla güncellenir, kategoriler birleşir.", L"Importing the same file again is safe: new series are added, existing ones take state from file, categories are merged.") \
    X(TabComingSites,   L"AMR siteler kataloğu yüklendi. QuickJS-ng C11 ve Lexbor HTML5 motoru devrede.", L"AMR sites catalog loaded. QuickJS-ng C11 and Lexbor HTML5 engines active.") \
    X(TabComingDownloads, L"Bölümleri CBZ veya ham görsel klasörü olarak çevrimdışı indirme seçenekleri.", L"Offline chapter download options as CBZ or raw image folders.") \
    X(TabComingSync,    L"Kütüphane yedekleme ve dışa aktarma araçları.", L"Library backup and export tools.") \
    X(Shortcuts,        L"Kısayollar",                          L"Shortcuts") \
    X(ShortcutsBody,    L"← →  sayfa   ·   , .  bölüm   ·   W  webtoon   ·   B  çift sayfa   ·   R  yön   ·   M  menü   ·   + − 0  yakınlaştırma   ·   [ ]  zemin   ·   F11  tam ekran   ·   Ctrl+O  klasör aç   ·   Ctrl+Shift+O  arşiv aç   ·   Ctrl+I  AMR içe aktar   ·   L  dil   ·   Esc  geri", L"← →  page   ·   , .  chapter   ·   W  webtoon   ·   B  double page   ·   R  direction   ·   M  menu   ·   + − 0  zoom   ·   [ ]  backdrop   ·   F11  full screen   ·   Ctrl+O  open folder   ·   Ctrl+Shift+O  open archive   ·   Ctrl+I  import AMR   ·   L  language   ·   Esc  back") \
    X(PickFolderTitle,  L"Bölüm klasörünü seç",                 L"Choose a chapter folder") \
    X(LanguageChanged,  L"Dil: Türkçe",                         L"Language: English") \
    X(DimValue,         L"Zemin koyuluğu: %d%%",                L"Backdrop darkness: %d%%") \
    X(ZoomValue,        L"Yakınlaştırma: %d%%",                 L"Zoom: %d%%") \
    X(AlreadyRunning,   L"GhostManga zaten açık, pencere öne getirildi.", L"GhostManga is already running.") \
    X(On,               L"Açık",                                L"On") \
    X(Off,              L"Kapalı",                              L"Off") \
    X(ExportBackupBtn,  L"Kütüphaneyi dışa aktar (.json)",      L"Export library (.json)") \
    X(ExportSuccess,    L"Kütüphane başarıyla dışa aktarıldı.", L"Library exported successfully.") \
    X(ExportFailed,     L"Dışa aktarma başarısız oldu.",        L"Export failed.") \
    X(SaveBackupTitle,  L"Kütüphane yedeğini kaydet",           L"Save library backup") \
    X(CachePurged,      L"Önbellek temizlendi (%u MB alan açıldı).", L"Cache purged (%u MB freed).") \
    X(PurgeCacheBtn,    L"Önbelleği temizle",                   L"Purge cache") \
    X(CacheSizeLabel,   L"Önbellek boyutu",                     L"Cache size") \
    X(ScanIntervalLabel,L"Güncelleme kontrol aralığı",          L"Update check interval") \
    X(ScanIntervalOff,  L"Kapalı",                              L"Disabled") \
    X(ScanInterval15,   L"15 dk",                               L"15 min") \
    X(ScanInterval30,   L"30 dk",                               L"30 min") \
    X(ScanInterval60,   L"1 saat",                              L"1 hour") \
    X(ScanInterval360,  L"6 saat",                              L"6 hours") \
    X(NotifyNewChapters,L"Yeni bölümlerde Windows bildirimi göster", L"Show Windows notifications on new chapters") \
    X(ManualScanNow,    L"Şimdi güncellemeleri kontrol et",     L"Check for updates now") \
    X(ManualScanTriggered, L"Takip edilen seriler taranıyor…",  L"Scanning followed series…") \
    X(DownloadFormatLabel, L"İndirme biçimi",                   L"Download format") \
    X(DownloadFormatCbz, L"CBZ Arşivi",                         L"CBZ Archive") \
    X(DownloadFormatFolder, L"Klasör",                          L"Folder") \
    X(DownloadDirLabel, L"İndirme klasörü",                     L"Download directory") \
    X(SitesLoadedFmt,   L"%d aktif ayna / site yüklendi",       L"%d active mirrors loaded") \
    X(OpenSitesCatalog, L"Site kataloğunu aç",                  L"Open sites catalog") \
    X(OpenArchiveBtn,   L"Arşiv aç (.cbz, .zip)",               L"Open archive (.cbz, .zip)") \
    X(PickArchiveTitle, L"Çizgi roman arşivini seç",            L"Choose comic archive") \
    X(ArchiveFilter,    L"Çizgi roman arşivi (*.cbz;*.zip)",    L"Comic archive (*.cbz;*.zip)")

enum class S : int {
#define GM_ENUM(id, tr, en) id,
    GM_STRINGS(GM_ENUM)
#undef GM_ENUM
    Count_
};

namespace i18n {

void Set(Lang preferred);     // Auto ise Windows dilinden secer
bool IsTurkish();
Lang Current();
Lang Next();
std::wstring Text(const std::wstring& source);
std::wstring English(const std::wstring& source);
const wchar_t* Literal(const wchar_t* source);
const wchar_t* T(S id);

} // namespace i18n

using i18n::T;

} // namespace gm
