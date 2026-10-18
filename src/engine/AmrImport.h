#pragma once
//
// AMR dis aktarma dosyasini okur (FR-IMP-001, FR-IMP-002).
// Kullanici AMR'de Import / Export > Export ile JSON kaydeder, burada secer.
// Bicim ve alan eslemesi: docs/05-AMR-VERI-FORMATLARI.md bolum 4.
//
#include "engine/Library.h"

#include <string>
#include <string_view>
#include <vector>

namespace gm::amr {

enum class ImportError { None, ReadFailed, TooBig, BadJson, NotAmr, Empty };

// Bir seride en fazla bu kadar kategori tutulur (Library birlestirirken de).
inline constexpr size_t kMaxCats = 64;

struct ImportData {
    std::vector<Series> series;
    std::vector<Bookmark> bookmarks;
    int skipped = 0;        // adresi olmayan, okunamayan ya da olcusuz kayit
    bool v1 = false;        // AMR V1 bicimi (listeler JSON metni icinde)
    std::string detail;     // BadJson: gunluk icin ayrinti ("satir 3, sutun 14: ...")
    size_t errLine = 0, errCol = 0;  // BadJson: kullaniciya gosterilecek konum
};

// AMR bookmarkKey ile ayni kimlik: sema ve alan adi bagimsiz.
std::string BookmarkKey(const Bookmark& b);

ImportError LoadExportFile(const std::wstring& path, ImportData& out);
ImportError ParseExport(std::string_view utf8, ImportData& out);

// AMR ile birebir ayni anahtar (shared/utils.ts mangaKey).
std::string SafeName(std::string_view name);
std::string AfterHostUrl(std::string_view url);
std::string MangaKey(std::string_view mirror, std::string_view url, std::string_view language);

// Son okunan bolum icin kisa ad. Dosyada ad yoksa adresten turetir;
// adres yalniz kimlik iceriyorsa (MangaDex UUID gibi) bos doner.
std::string ChapterLabel(const Series& s);

// Kutuphaneyi AMR v2 biciminde disa aktarir (FR-IMP-007).
bool ExportAmrFile(const std::wstring& path, const Library& lib);

// Bolum listesini diske kalici olarak kaydeder / diskten okur
bool SaveCachedChapterList(const std::string& key, const std::vector<std::pair<std::string, std::string>>& chapters);
bool LoadCachedChapterList(const std::string& key, std::vector<std::pair<std::string, std::string>>& chapters);

} // namespace gm::amr
