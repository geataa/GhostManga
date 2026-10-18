#pragma once

#include <string>
#include <vector>

namespace gm::engine {

class ArchiveReader {
public:
    // Checks if the given file path is a supported comic archive (.cbz, .zip)
    static bool IsArchive(const std::wstring& filePath);

    // Extracts image files from archive to destination folder
    static int ExtractImages(const std::wstring& archivePath, const std::wstring& destFolder);

    // Prepares the archive in cache and returns the directory ready for ReaderView::Open
    static std::wstring PrepareArchiveForReading(const std::wstring& archivePath);
};

} // namespace gm::engine
