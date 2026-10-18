#include <windows.h>
#include <iostream>
#include <cassert>
#include "../src/engine/ChapterLoader.h"
#include "../src/core/Paths.h"
#include "../src/core/Log.h"

using namespace gm;

int main() {
    std::cout << "=============================================\n";
    std::cout << "  GhostManga M5 Automated Verification Tests \n";
    std::cout << "=============================================\n";

    std::wstring err;
    paths::Init(&err);
    log::Init();

    // Test 1: ChapterLoader GetCacheDir
    std::cout << "[TEST 1] Testing ChapterLoader cache directory computation...\n";
    {
        std::string mirror = "Manga Pill";
        std::string mangaUrl = "https://mangapill.com/manga/1/one-piece";
        std::string chapUrl = "https://mangapill.com/chapters/1-1000/one-piece-chapter-1000";

        std::wstring cacheDir = engine::ChapterLoader::Instance().GetCacheDir(mirror, mangaUrl, chapUrl);
        assert(!cacheDir.empty());
        assert(cacheDir.find(L"cache") != std::wstring::npos);
        assert(cacheDir.find(L"Manga Pill") != std::wstring::npos);

        std::wcout << L"  -> Cache Dir: " << cacheDir << L"\n";
        std::cout << "  -> Cache Dir computation: PASSED\n";
    }

    // Test 2: ChapterLoader IsCached
    std::cout << "[TEST 2] Testing ChapterLoader IsCached detection...\n";
    {
        std::wstring testDir = engine::ChapterLoader::Instance().GetCacheDir("TestMirror", "https://manga.test", "https://manga.test/1");
        assert(engine::ChapterLoader::Instance().IsCached(testDir) == false);

        // Create .complete marker
        std::wstring marker = testDir + L"\\.complete";
        HANDLE h = CreateFileW(marker.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        assert(h != INVALID_HANDLE_VALUE);
        CloseHandle(h);

        assert(engine::ChapterLoader::Instance().IsCached(testDir) == true);
        DeleteFileW(marker.c_str());

        std::cout << "  -> IsCached detection: PASSED\n";
    }

    std::cout << "\nALL M5 VERIFICATION TESTS COMPLETED SUCCESSFULLY!\n";
    return 0;
}
