#include "CbzWriter.h"
#include "miniz.h"
#include "core/FileIo.h"
#include "core/Paths.h"
#include "core/Log.h"
#include "core/Utf8.h"
#include "engine/LocalSource.h"

#include <windows.h>
#include <algorithm>

namespace gm::engine {

bool CbzWriter::PackFolder(const std::wstring& folderPath, const std::wstring& cbzPath) {
    std::vector<std::wstring> images = local::ListImages(folderPath);
    if (images.empty()) {
        log::Write(L"[CbzWriter] No images found to pack in %s", folderPath.c_str());
        return false;
    }
    return CreateCbz(cbzPath, images);
}

bool CbzWriter::CreateCbz(const std::wstring& cbzPath, const std::vector<std::wstring>& imageFiles) {
    if (imageFiles.empty()) return false;

    std::wstring tmpPath = cbzPath + L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64());
    FILE* fp = _wfopen(tmpPath.c_str(), L"wb");
    if (!fp) {
        log::Write(L"[CbzWriter] Failed to open temporary archive for writing: %s", tmpPath.c_str());
        return false;
    }

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_cfile(&zip, fp, 0)) {
        log::Write(L"[CbzWriter] Failed to initialize miniz zip writer");
        fclose(fp);
        DeleteFileW(tmpPath.c_str());
        return false;
    }

    bool allOk = true;
    for (size_t i = 0; i < imageFiles.size(); ++i) {
        const std::wstring& filePath = imageFiles[i];
        std::string fileData;
        if (fileio::ReadText(filePath, 128 * 1024 * 1024, fileData) != fileio::ReadResult::Ok) {
            log::Write(L"[CbzWriter] Could not read file: %s", filePath.c_str());
            allOk = false;
            break;
        }

        std::wstring wName = paths::FileName(filePath);
        std::string zipEntryName = ToUtf8(wName);

        if (!mz_zip_writer_add_mem(&zip, zipEntryName.c_str(), fileData.data(), fileData.size(), MZ_DEFAULT_COMPRESSION)) {
            log::Write(L"[CbzWriter] Failed to add entry to zip: %s", wName.c_str());
            allOk = false;
            break;
        }
    }

    if (allOk) {
        if (!mz_zip_writer_finalize_archive(&zip)) {
            log::Write(L"[CbzWriter] Failed to finalize archive: %s", tmpPath.c_str());
            allOk = false;
        }
    }

    mz_zip_writer_end(&zip);
    fclose(fp);

    if (!allOk) {
        DeleteFileW(tmpPath.c_str());
        return false;
    }

    if (!MoveFileExW(tmpPath.c_str(), cbzPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        log::Write(L"[CbzWriter] Failed to move temporary archive to target %s", cbzPath.c_str());
        DeleteFileW(tmpPath.c_str());
        return false;
    }

    log::Write(L"[CbzWriter] Successfully created CBZ archive with %zu pages: %s",
               imageFiles.size(), cbzPath.c_str());
    return true;
}

} // namespace gm::engine
