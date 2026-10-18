#include "core/Paths.h"

#include <windows.h>
#include <shlobj.h>
#include <cwctype>

namespace gm::paths {
namespace {

std::wstring g_exeDir;
std::wstring g_dataDir;
bool g_portable = true;
uint64_t g_hash = 0;

bool IsWritableDir(const std::wstring& dir) {
    if (!EnsureDir(dir)) return false;
    const std::wstring probe = Join(dir, L".yazma_testi");
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

std::wstring CanonicalPath(const std::wstring& dir) {
    HANDLE h = CreateFileW(dir.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    std::wstring out;
    if (h != INVALID_HANDLE_VALUE) {
        wchar_t buf[2048];
        DWORD n = GetFinalPathNameByHandleW(h, buf, (DWORD)std::size(buf), VOLUME_NAME_GUID);
        if (n == 0 || n >= std::size(buf)) {
            n = GetFinalPathNameByHandleW(h, buf, (DWORD)std::size(buf), VOLUME_NAME_DOS);
        }
        if (n > 0 && n < std::size(buf)) out.assign(buf, n);
        CloseHandle(h);
    }
    if (out.empty()) out = dir;
    for (auto& c : out) c = (wchar_t)std::towlower(c);
    return out;
}

uint64_t Fnv1a(const std::wstring& s) {
    uint64_t h = 1469598103934665603ull;
    for (wchar_t c : s) {
        h ^= (uint64_t)(uint16_t)c;
        h *= 1099511628211ull;
    }
    return h;
}

} // namespace

bool Init(std::wstring* err) {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
    if (n == 0 || n >= std::size(buf)) {
        if (err) *err = L"Uygulama yolu okunamadi.";
        return false;
    }
    g_exeDir = ParentDir(std::wstring(buf, n));

    std::wstring portable = Join(g_exeDir, L"portable_data");
    const DWORD attrCurrent = GetFileAttributesW(portable.c_str());
    if (attrCurrent == INVALID_FILE_ATTRIBUTES || !(attrCurrent & FILE_ATTRIBUTE_DIRECTORY)) {
        std::wstring parentPortable = Join(ParentDir(g_exeDir), L"portable_data");
        const DWORD attrParent = GetFileAttributesW(parentPortable.c_str());
        if (attrParent != INVALID_FILE_ATTRIBUTES && (attrParent & FILE_ATTRIBUTE_DIRECTORY)) {
            portable = parentPortable;
        }
    }
    if (IsWritableDir(portable)) {
        g_dataDir = portable;
        g_portable = true;
    } else {
        PWSTR local = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)) || !local) {
            if (err) *err = L"Yazilabilir veri klasoru bulunamadi.";
            return false;
        }
        g_dataDir = Join(local, L"GhostManga");
        CoTaskMemFree(local);
        g_portable = false;
        if (!IsWritableDir(g_dataDir)) {
            if (err) *err = L"Yazilabilir veri klasoru bulunamadi.";
            return false;
        }
    }
    g_hash = Fnv1a(CanonicalPath(g_dataDir));
    return true;
}

const std::wstring& ExeDir() { return g_exeDir; }
const std::wstring& DataDir() { return g_dataDir; }
bool IsPortable() { return g_portable; }
uint64_t InstanceHash() { return g_hash; }

std::wstring Join(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    const wchar_t last = a.back();
    if (last == L'\\' || last == L'/') return a + b;
    return a + L"\\" + b;
}

bool EnsureDir(const std::wstring& dir) {
    if (dir.empty()) return false;
    const DWORD attr = GetFileAttributesW(dir.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const int res = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    if (res == ERROR_SUCCESS || res == ERROR_ALREADY_EXISTS || res == ERROR_FILE_EXISTS) return true;
    const std::wstring parent = ParentDir(dir);
    if (!parent.empty() && parent != dir && !EnsureDir(parent)) return false;
    return CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

std::wstring FileName(const std::wstring& path) {
    const size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

std::wstring ParentDir(const std::wstring& path) {
    std::wstring p = path;
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    const size_t i = p.find_last_of(L"\\/");
    if (i == std::wstring::npos) return std::wstring();
    if (i == 2 && p.size() > 1 && p[1] == L':') return p.substr(0, 3); // "C:\"
    return p.substr(0, i);
}

std::wstring CacheDir() {
    return Join(DataDir(), L"cache");
}

uint64_t DirectorySize(const std::wstring& dir) {
    uint64_t total = 0;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileExW(Join(dir, L"*").c_str(), FindExInfoBasic, &fd,
                                FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        const std::wstring child = Join(dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                total += DirectorySize(child);
            }
        } else {
            total += (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return total;
}

static bool RemoveDirRecursive(const std::wstring& dir) {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileExW(Join(dir, L"*").c_str(), FindExInfoBasic, &fd,
                                FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        const std::wstring child = Join(dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                RemoveDirectoryW(child.c_str());
            } else {
                RemoveDirRecursive(child);
            }
        } else {
            SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(child.c_str());
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return RemoveDirectoryW(dir.c_str()) != 0;
}

bool ClearDirectoryContents(const std::wstring& dir) {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileExW(Join(dir, L"*").c_str(), FindExInfoBasic, &fd,
                                FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        const std::wstring child = Join(dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                RemoveDirectoryW(child.c_str());
            } else {
                RemoveDirRecursive(child);
            }
        } else {
            SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(child.c_str());
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return true;
}

} // namespace gm::paths
