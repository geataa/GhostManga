#pragma once

#include <string>
#include <vector>

namespace gm::engine {

class CbzWriter {
public:
    // Packs an entire folder of image files into a single .cbz archive
    static bool PackFolder(const std::wstring& folderPath, const std::wstring& cbzPath);

    // Creates a .cbz archive containing the specified list of image files
    static bool CreateCbz(const std::wstring& cbzPath, const std::vector<std::wstring>& imageFiles);
};

} // namespace gm::engine
