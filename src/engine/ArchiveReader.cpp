#include "ArchiveReader.h"
#include "miniz.h"
#include "core/FileIo.h"
#include "core/Paths.h"
#include "core/Log.h"
#include "core/Utf8.h"
#include "engine/LocalSource.h"

#include <windows.h>
#include <shlwapi.h>
#include <iomanip>
#include <sstream>

namespace gm::engine {

bool ArchiveReader::IsArchive(const std::wstring& filePath) {
    const DWORD attr = GetFileAttributesW(filePath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        return false;
    }
    const wchar_t* ext = PathFindExtensionW(filePath.c_str());
    if (!ext) return false;
    return (_wcsicmp(ext, L".cbz") == 0 || _wcsicmp(ext, L".zip") == 0);
}

int ArchiveReader::ExtractImages(const std::wstring& archivePath, const std::wstring& destFolder) {
    paths::EnsureDir(destFolder);

    FILE* fp = _wfopen(archivePath.c_str(), L"rb");
    if (!fp) {
        log::Write(L"[ArchiveReader] Failed to open archive for reading: %s", archivePath.c_str());
        return 0;
    }

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_cfile(&zip, fp, 0, 0)) {
        log::Write(L"[ArchiveReader] Failed to initialize miniz reader for: %s", archivePath.c_str());
        fclose(fp);
        return 0;
    }

    mz_uint numFiles = mz_zip_reader_get_num_files(&zip);
    int extractedCount = 0;

    for (mz_uint i = 0; i < numFiles; ++i) {
        mz_zip_archive_file_stat fileStat;
        if (!mz_zip_reader_file_stat(&zip, i, &fileStat)) continue;
        if (fileStat.m_is_directory) continue;

        std::wstring entryName = FromUtf8(fileStat.m_filename);
        if (!local::IsImageFile(entryName)) continue;

        if (fileStat.m_uncomp_size == 0 || fileStat.m_uncomp_size > 256ULL * 1024 * 1024) {
            log::Write(L"[ArchiveReader] Skipping entry with invalid or excessive size (%llu): %s",
                       static_cast<unsigned long long>(fileStat.m_uncomp_size), entryName.c_str());
            continue;
        }

        size_t uncompSize = 0;
        void* pData = mz_zip_reader_extract_to_heap(&zip, i, &uncompSize, 0);
        if (!pData) continue;

        // Sanitize entryName for file system flattening
        std::wstring safeName = entryName;
        for (wchar_t& c : safeName) {
            if (c == L'/' || c == L'\\' || c == L':' || c == L'*' ||
                c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|') {
                c = L'_';
            }
        }

        std::wstring outPath = destFolder + L"\\" + safeName;
        try {
            if (fileio::WriteAtomic(outPath, std::string(reinterpret_cast<const char*>(pData), uncompSize))) {
                extractedCount++;
            }
        } catch (const std::exception& ex) {
            log::Write(L"[ArchiveReader] Exception writing extracted file %s: %S", safeName.c_str(), ex.what());
        }
        mz_free(pData);
    }

    mz_zip_reader_end(&zip);
    fclose(fp);

    return extractedCount;
}

std::wstring ArchiveReader::PrepareArchiveForReading(const std::wstring& archivePath) {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(archivePath.c_str(), GetFileExInfoStandard, &fad)) {
        log::Write(L"[ArchiveReader] Cannot get file attributes for: %s", archivePath.c_str());
        return L"";
    }

    // FNV-1a 64-bit hash of path, size, and last write time
    uint64_t hash = 14695981039346656037ull;
    std::string uPath = ToUtf8(archivePath);
    for (char c : uPath) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 1099511628211ull;
    }
    hash ^= static_cast<uint64_t>(fad.nFileSizeLow);
    hash *= 1099511628211ull;
    hash ^= static_cast<uint64_t>(fad.ftLastWriteTime.dwLowDateTime);
    hash *= 1099511628211ull;

    std::wstringstream ss;
    ss << std::hex << std::setfill(L'0') << std::setw(16) << hash;

    std::wstring cacheDir = paths::DataDir() + L"\\cache\\archives\\" + ss.str();
    paths::EnsureDir(cacheDir);

    std::wstring marker = cacheDir + L"\\.complete";
    if (GetFileAttributesW(marker.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return cacheDir;
    }

    int count = ExtractImages(archivePath, cacheDir);
    if (count > 0) {
        fileio::WriteAtomic(marker, "ok");
        log::Write(L"[ArchiveReader] Extracted %d images from %s into %s", count, archivePath.c_str(), cacheDir.c_str());
        return cacheDir;
    }

    log::Write(L"[ArchiveReader] No images extracted from: %s", archivePath.c_str());
    return L"";
}

} // namespace gm::engine
