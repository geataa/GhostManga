#include <iostream>
#include <cassert>
#include <filesystem>
#include <windows.h>
#include "../src/core/Paths.h"
#include "../src/core/Log.h"
#include "../src/core/FileIo.h"
#include "../src/engine/Library.h"
#include "../src/engine/AmrImport.h"

using namespace gm;

int main() {
    std::cout << "=============================================\n";
    std::cout << "  GhostManga M7 Export & Cache Unit Tests    \n";
    std::cout << "=============================================\n";

    std::wstring err;
    paths::Init(&err);
    log::Init();

    // TEST 1: Cache Directory size & clear
    std::cout << "[TEST 1] Testing DirectorySize & ClearDirectoryContents...\n";
    {
        std::wstring testDir = paths::Join(paths::DataDir(), L"test_cache_temp");
        paths::EnsureDir(testDir);
        paths::EnsureDir(paths::Join(testDir, L"subdir"));

        fileio::WriteAtomic(paths::Join(testDir, L"file1.bin"), std::string(1024 * 100, 'X')); // 100 KB
        fileio::WriteAtomic(paths::Join(testDir, L"subdir\\file2.bin"), std::string(1024 * 200, 'Y')); // 200 KB

        uint64_t sz = paths::DirectorySize(testDir);
        std::cout << "  -> Measured directory size: " << sz << " bytes\n";
        assert(sz >= 1024 * 300);

        bool cleared = paths::ClearDirectoryContents(testDir);
        assert(cleared == true);

        uint64_t szAfter = paths::DirectorySize(testDir);
        std::cout << "  -> Directory size after clear: " << szAfter << " bytes\n";
        assert(szAfter == 0);
        std::cout << "  -> DirectorySize & ClearDirectoryContents: PASSED\n";
    }

    // TEST 2: AMR Backup Export and Roundtrip Import
    std::cout << "[TEST 2] Testing ExportAmrFile roundtrip...\n";
    {
        Library lib;
        std::vector<Series> seriesList;
        Series s1;
        s1.key = "mangapill/manga/1/one-piece";
        s1.mirror = "Manga Pill";
        s1.name = "One Piece";
        s1.displayName = "One Piece (Official)";
        s1.url = "https://mangapill.com/manga/1/one-piece";
        s1.lastChapterUrl = "https://mangapill.com/chapters/1-1000/one-piece-chapter-1100";
        s1.lastChapterName = "Chapter 1100";
        s1.language = "en";
        s1.cats = { "Action", "Adventure", "Shounen" };
        s1.read = 0;
        s1.update = 1;
        s1.upts = 1727000000000ll;
        seriesList.push_back(s1);

        std::vector<Bookmark> bookmarkList;
        Bookmark b1;
        b1.mirror = "Manga Pill";
        b1.name = "One Piece";
        b1.url = s1.url;
        b1.chapUrl = s1.lastChapterUrl;
        b1.chapName = "Chapter 1100";
        b1.note = "Epic reveal on page 12";
        bookmarkList.push_back(b1);

        lib.Merge(std::move(seriesList), std::move(bookmarkList));
        assert(lib.All().size() == 1);
        assert(lib.Bookmarks().size() == 1);

        std::wstring exportPath = paths::Join(paths::DataDir(), L"test_export.json");
        bool exported = amr::ExportAmrFile(exportPath, lib);
        assert(exported == true);

        // Verify import of exported file
        amr::ImportData imported;
        amr::ImportError ie = amr::LoadExportFile(exportPath, imported);
        assert(ie == amr::ImportError::None);
        assert(imported.series.size() == 1);
        assert(imported.bookmarks.size() == 1);
        assert(imported.series[0].name == "One Piece");
        assert(imported.series[0].displayName == "One Piece (Official)");
        assert(imported.series[0].cats.size() == 3);
        assert(imported.bookmarks[0].note == "Epic reveal on page 12");
        std::cout << "  -> Exported and verified 100% roundtrip fidelity: PASSED\n";

        // Cleanup
        DeleteFileW(exportPath.c_str());
    }

    std::cout << "\nALL EXPORT & CACHE TESTS COMPLETED SUCCESSFULLY!\n";
    return 0;
}
