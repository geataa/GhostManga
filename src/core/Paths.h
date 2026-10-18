#pragma once
//
// Tasinabilir veri klasoru. Varsayilan: exe'nin yanindaki portable_data\.
// Orasi yazilamiyorsa (CD, yazma korumali USB) %LOCALAPPDATA%\GhostManga'ya
// duser ve bunu kullaniciya soyleriz (FR-DATA-001).
//
#include <cstdint>
#include <string>

namespace gm::paths {

bool Init(std::wstring* err);

const std::wstring& ExeDir();
const std::wstring& DataDir();
bool IsPortable();

std::wstring Join(const std::wstring& a, const std::wstring& b);
bool EnsureDir(const std::wstring& dir);

// Ayni veri klasorunu kullanan orneklerin ortak kimligi. Surucu harfi degisse
// de (USB evde E:, iste G:) ayni kalsin diye birim GUID'li yoldan uretilir.
uint64_t InstanceHash();

std::wstring FileName(const std::wstring& path);
std::wstring ParentDir(const std::wstring& path);
std::wstring CacheDir();

uint64_t DirectorySize(const std::wstring& dir);
bool ClearDirectoryContents(const std::wstring& dir);

} // namespace gm::paths
