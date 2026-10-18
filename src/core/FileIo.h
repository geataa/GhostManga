#pragma once
//
// Kucuk dosya yardimcilari. Diske yazan her sey WriteAtomic'ten gecer:
// once .tmp'ye yazilir, sonra tek hamlede yerine konur; yarida kesilen
// yazma mevcut dosyayi bozmaz.
//
#include <cstdint>
#include <string>

namespace gm::fileio {

enum class ReadResult { Ok, Missing, TooBig, Failed };

// Tamamini okur. UTF-8 BOM atilir; UTF-16 LE BOM'lu dosya UTF-8'e cevrilir.
ReadResult ReadText(const std::wstring& path, uint64_t maxBytes, std::string& out);

// keepBackup: varsa eski dosya once <yol>.bak olarak kopyalanir.
bool WriteAtomic(const std::wstring& path, const std::string& data, bool keepBackup = false);

} // namespace gm::fileio
