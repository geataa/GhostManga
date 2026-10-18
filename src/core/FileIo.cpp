#include "core/FileIo.h"
#include "core/Paths.h"
#include "core/Log.h"
#include "core/Utf8.h"

#include <windows.h>


namespace gm::fileio {

ReadResult ReadText(const std::wstring& path, uint64_t maxBytes, std::string& out) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        return (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) ? ReadResult::Missing : ReadResult::Failed;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size)) {
        CloseHandle(h);
        return ReadResult::Failed;
    }
    if ((uint64_t)size.QuadPart > maxBytes) {
        CloseHandle(h);
        return ReadResult::TooBig;
    }
    out.resize((size_t)size.QuadPart);
    DWORD read = 0;
    const BOOL ok = out.empty() || ReadFile(h, out.data(), (DWORD)out.size(), &read, nullptr);
    CloseHandle(h);
    if (!ok) {
        out.clear();
        return ReadResult::Failed;
    }
    out.resize(read);

    if (out.size() >= 3 && (uint8_t)out[0] == 0xEF && (uint8_t)out[1] == 0xBB && (uint8_t)out[2] == 0xBF) {
        out.erase(0, 3);
    } else if (out.size() >= 2 && (uint8_t)out[0] == 0xFF && (uint8_t)out[1] == 0xFE) {
        // Not Defteri ile "Unicode" kaydedilmis dosya
        std::wstring w((out.size() - 2) / 2, L'\0');
        memcpy(w.data(), out.data() + 2, w.size() * sizeof(wchar_t));
        out = ToUtf8(w);
    }
    return ReadResult::Ok;
}

bool WriteAtomic(const std::wstring& path, const std::string& data, bool keepBackup) {
    const std::wstring parent = paths::ParentDir(path);
    if (!parent.empty()) paths::EnsureDir(parent);

    const std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        log::Write(L"Yazilamadi: %s (hata %lu)", tmp.c_str(), GetLastError());
        return false;
    }
    DWORD written = 0;
    bool ok = data.size() <= MAXDWORD &&
              WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr) && written == data.size();
    // Diske aktarilamayan .tmp ile iyi dosyanin ustune yazma. Bazi sanal ve
    // bulut suruculeri flush desteklemez (ERROR_INVALID_FUNCTION): onlari kabul et.
    if (ok && !FlushFileBuffers(h) && GetLastError() != ERROR_INVALID_FUNCTION) {
        log::Write(L"Diske aktarilamadi: %s (hata %lu)", tmp.c_str(), GetLastError());
        ok = false;
    }
    if (!CloseHandle(h)) ok = false;
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (keepBackup && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        // CopyFile kaynaktaki salt okunur/gizli ozniteligini tasir; oyle kalan
        // .bak bir daha guncellenmez. Once ve sonra temizle.
        const std::wstring bak = path + L".bak";
        SetFileAttributesW(bak.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (CopyFileW(path.c_str(), bak.c_str(), FALSE)) SetFileAttributesW(bak.c_str(), FILE_ATTRIBUTE_NORMAL);
        else log::Write(L"Yedek alinamadi: %s (hata %lu)", bak.c_str(), GetLastError());
    }
    // Virus tarayici ya da dizinleyici dosyayi kisa sure acik tutabilir:
    // birkac kez dene.
    for (int attempt = 0;; ++attempt) {
        if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        const DWORD e = GetLastError();
        if (attempt >= 4 || (e != ERROR_ACCESS_DENIED && e != ERROR_SHARING_VIOLATION)) {
            log::Write(L"Dosya yerine konamadi: %s (hata %lu)", path.c_str(), e);
            DeleteFileW(tmp.c_str());
            return false;
        }
        Sleep(60);
    }
}

} // namespace gm::fileio
