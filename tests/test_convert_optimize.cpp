#include "engine/Library.h"
#include "core/FileIo.h"
#include "core/Paths.h"
#include <cassert>
#include <iostream>
#include <windows.h>

using namespace gm;

int main() {
    std::wstring testDb = L"tests_temp_library.json";
    DeleteFileW(testDb.c_str());
    DeleteFileW((testDb + L".bak").c_str());

    // Write a realistic dirty JSON file that needs conversion & optimization
    std::string dirtyJson = R"JSON({
  "mangas": [
    {
      "name": "  Solo &amp; Leveling  ",
      "displayName": "  Hunter&#039;s Journey  ",
      "mirror": "Asura Comics",
      "url": "//asuracomics.com/manga/solo-leveling",
      "key": "asura/solo-leveling",
      "cats": ["Action", "Fantasy", "Action"],
      "upts": 1700000002000,
      "lastReadTs": 1700000000500
    },
    {
      "name": "Solo Leveling",
      "mirror": "Asura Comics",
      "url": "https://asuracomics.com/manga/solo-leveling/",
      "key": "asura/solo-leveling-dup",
      "cats": ["Action", "Shounen"],
      "upts": 1700000003000,
      "lastReadTs": 1700000000100
    },
    {
      "name": "  Jujutsu &amp; Kaisen  ",
      "mirror": "MangaPlus",
      "url": "//mangaplus.shueisha.co.jp/titles/100034",
      "key": "mangaplus/jjk",
      "cats": ["Supernatural", "Action", "Supernatural"]
    },
    {
      "name": "One Piece",
      "mirror": "TCB",
      "url": "https://tcbscans.com/manga/one-piece",
      "key": "tcb/one-piece",
      "cats": ["Action", "Adventure"]
    }
  ],
  "bookmarks": [
    {
      "name": "Solo Leveling",
      "url": "https://asuracomics.com/manga/solo-leveling",
      "chapUrl": "https://asuracomics.com/manga/solo-leveling/chapter-1"
    },
    {
      "name": "Solo Leveling",
      "url": "https://asuracomics.com/manga/solo-leveling",
      "chapUrl": "https://asuracomics.com/manga/solo-leveling/chapter-1"
    }
  ]
})JSON";

    fileio::WriteAtomic(testDb, dirtyJson);

    Library lib;
    lib.Load(testDb);

    std::cout << "Loaded initial dirty library: " << lib.All().size() << " series, " << lib.Bookmarks().size() << " bookmarks.\n";
    assert(lib.All().size() == 4);
    assert(lib.Bookmarks().size() == 2);

    int fixedCount = 0;
    bool ok = lib.ConvertAndOptimize(fixedCount);
    assert(ok);
    std::cout << "Optimized! Fixed/merged count: " << fixedCount << "\n";
    std::cout << "After optimize: " << lib.All().size() << " series, " << lib.Bookmarks().size() << " bookmarks.\n";

    // Deduplication check:
    // Solo Leveling (s1 & s2) merged into 1
    // Jujutsu Kaisen cleaned & kept
    // One Piece kept
    // Total series: 3
    assert(lib.All().size() == 3);
    // Duplicate bookmark pruned: 1 bookmark remaining
    assert(lib.Bookmarks().size() == 1);

    const Series* foundSolo = nullptr;
    const Series* foundJjk = nullptr;
    for (const auto& s : lib.All()) {
        if (s.name == "Solo & Leveling") foundSolo = &s;
        if (s.name == "Jujutsu & Kaisen") foundJjk = &s;
    }
    assert(foundSolo != nullptr);
    assert(foundSolo->displayName == "Hunter's Journey");
    assert(foundSolo->url == "https://asuracomics.com/manga/solo-leveling");
    assert(foundSolo->upts == 1700000003000); // max upts kept
    assert(foundSolo->lastReadTs == 1700000000500); // max lastReadTs kept

    assert(foundJjk != nullptr);
    assert(foundJjk->url == "https://mangaplus.shueisha.co.jp/titles/100034"); // scheme prepended
    assert(foundJjk->cats.size() == 2); // Action, Supernatural deduplicated

    // Verify category merge: Action, Fantasy, Shounen
    bool hasAction = false, hasFantasy = false, hasShounen = false;
    for (const auto& c : foundSolo->cats) {
        if (c == "Action") hasAction = true;
        if (c == "Fantasy") hasFantasy = true;
        if (c == "Shounen") hasShounen = true;
    }
    assert(hasAction && hasFantasy && hasShounen);
    std::cout << "Solo Leveling categories verified: Action, Fantasy, Shounen.\n";

    // Test RemoveSeries using URL
    assert(lib.RemoveSeries("https://tcbscans.com/manga/one-piece"));
    assert(lib.All().size() == 2);
    std::cout << "Removed One Piece by URL. Remaining series: " << lib.All().size() << "\n";

    // Clean up
    DeleteFileW(testDb.c_str());
    DeleteFileW((testDb + L".bak").c_str());

    std::cout << "ALL TESTS PASSED SUCCESSFULLY!\n";
    return 0;
}
