#include <windows.h>
#include <iostream>
#include <cassert>
#include "../src/engine/CbzWriter.h"
#include "../src/engine/ArchiveReader.h"
#include "../src/core/Paths.h"
#include "../src/core/FileIo.h"
#include "../src/core/Log.h"

using namespace gm;

int main() {
    std::cout << "=============================================\n";
    std::cout << "  GhostManga M6 Automated Verification Tests \n";
    std::cout << "=============================================\n";

    std::wstring err;
    paths::Init(&err);
    log::Init();

    std::wstring tempDir = paths::DataDir() + L"\\test_m6_temp";
    paths::EnsureDir(tempDir);

    // Create 3 mock image files
    std::wstring img1 = tempDir + L"\\0001.png";
    std::wstring img2 = tempDir + L"\\0002.jpg";
    std::wstring img3 = tempDir + L"\\0003.webp";

    std::string fakeData1 = "\x89PNG\r\n\x1a\nMock PNG Data 1";
    std::string fakeData2 = "\xFF\xD8\xFF\xE0Mock JPEG Data 2";
    std::string fakeData3 = "RIFF\x00\x00\x00\x00WEBPVP8 Mock WebP Data 3";

    fileio::WriteAtomic(img1, fakeData1);
    fileio::WriteAtomic(img2, fakeData2);
    fileio::WriteAtomic(img3, fakeData3);

    std::wstring cbzPath = tempDir + L"\\test_manga.cbz";
    DeleteFileW(cbzPath.c_str());

    // TEST 1: CbzWriter::CreateCbz
    std::cout << "[TEST 1] Testing CbzWriter::CreateCbz...\n";
    {
        std::vector<std::wstring> files = { img1, img2, img3 };
        bool ok = engine::CbzWriter::CreateCbz(cbzPath, files);
        assert(ok);
        assert(GetFileAttributesW(cbzPath.c_str()) != INVALID_FILE_ATTRIBUTES);
        std::cout << "  -> CbzWriter::CreateCbz: PASSED\n";
    }

    // TEST 2: ArchiveReader::IsArchive
    std::cout << "[TEST 2] Testing ArchiveReader::IsArchive...\n";
    {
        assert(engine::ArchiveReader::IsArchive(cbzPath) == true);
        assert(engine::ArchiveReader::IsArchive(img1) == false);
        assert(engine::ArchiveReader::IsArchive(tempDir + L"\\nonexistent.cbz") == false);
        std::cout << "  -> ArchiveReader::IsArchive: PASSED\n";
    }

    // TEST 3: ArchiveReader::ExtractImages
    std::cout << "[TEST 3] Testing ArchiveReader::ExtractImages...\n";
    {
        std::wstring extractDir = tempDir + L"\\extracted";
        paths::EnsureDir(extractDir);

        int count = engine::ArchiveReader::ExtractImages(cbzPath, extractDir);
        assert(count == 3);

        std::string readData1, readData2, readData3;
        assert(fileio::ReadText(extractDir + L"\\0001.png", 1024, readData1) == fileio::ReadResult::Ok);
        assert(fileio::ReadText(extractDir + L"\\0002.jpg", 1024, readData2) == fileio::ReadResult::Ok);
        assert(fileio::ReadText(extractDir + L"\\0003.webp", 1024, readData3) == fileio::ReadResult::Ok);

        assert(readData1 == fakeData1);
        assert(readData2 == fakeData2);
        assert(readData3 == fakeData3);

        std::cout << "  -> ArchiveReader::ExtractImages: PASSED (all 3 files extracted & matched byte-for-byte)\n";
    }

    // TEST 4: ArchiveReader::PrepareArchiveForReading
    std::cout << "[TEST 4] Testing ArchiveReader::PrepareArchiveForReading...\n";
    {
        std::wstring readerDir = engine::ArchiveReader::PrepareArchiveForReading(cbzPath);
        assert(!readerDir.empty());
        assert(GetFileAttributesW((readerDir + L"\\.complete").c_str()) != INVALID_FILE_ATTRIBUTES);

        // Second call must return immediately from cache
        std::wstring readerDirCached = engine::ArchiveReader::PrepareArchiveForReading(cbzPath);
        assert(readerDirCached == readerDir);

        std::cout << "  -> ArchiveReader::PrepareArchiveForReading: PASSED\n";
    }

    // Cleanup
    DeleteFileW(img1.c_str());
    DeleteFileW(img2.c_str());
    DeleteFileW(img3.c_str());
    DeleteFileW((tempDir + L"\\extracted\\0001.png").c_str());
    DeleteFileW((tempDir + L"\\extracted\\0002.jpg").c_str());
    DeleteFileW((tempDir + L"\\extracted\\0003.webp").c_str());
    RemoveDirectoryW((tempDir + L"\\extracted").c_str());
    DeleteFileW(cbzPath.c_str());
    RemoveDirectoryW(tempDir.c_str());

    std::cout << "\nALL M6 VERIFICATION TESTS COMPLETED SUCCESSFULLY!\n";
    return 0;
}
