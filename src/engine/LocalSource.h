#pragma once
//
// Yerel bolum klasorleri (FR-DL-010). Bir klasor = bir bolum; kardes klasorler
// = onceki / sonraki bolum. Siralama Windows Gezgini ile ayni (StrCmpLogicalW):
// "2.jpg" "10.jpg"dan once gelir.
//
#include <string>
#include <vector>
#include <cstdint>

namespace gm { class Library; }

namespace gm::local {

bool IsImageFile(const std::wstring& name);

// Klasordeki okunabilir gorseller, dogal sirayla, tam yol.
std::vector<std::wstring> ListImages(const std::wstring& dir);

// Secilen klasorde gorsel yoksa ama bolum alt klasorleri varsa (seri klasoru),
// ilk bolumu dondurur. Hic yoksa bos.
std::wstring ResolveChapterDir(const std::wstring& picked);

// delta = +1 sonraki, -1 onceki bolum. Yoksa bos.
std::wstring SiblingChapter(const std::wstring& dir, int delta);

struct Entry {
    std::wstring path, seriesTitle, chapterTitle, originalPath;
    std::string seriesKey, mirror, mangaUrl, chapterUrl;
    int64_t downloadedAt = 0;
    bool recordedDownload = false;
    bool archive = false;
};

// Metadata is independent of folder names, which can be opaque cache hashes.
Entry Describe(const std::wstring& path, const Library& library);
bool StoreInfo(const Entry& entry, bool newDownload = false);
std::vector<Entry> Collect(const std::vector<std::wstring>& recent,
                          const std::wstring& downloads, const Library& library);
bool CanDelete(const Entry& entry, const std::wstring& downloads);
bool Recycle(const Entry& entry, const std::wstring& downloads, std::wstring& error);

} // namespace gm::local
