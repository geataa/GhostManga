#pragma once
#include "core/Paths.h"
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <string>

namespace gm::startup {
inline std::wstring ShortcutPath() {
    PWSTR folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Startup, 0, nullptr, &folder))) return {};
    wchar_t name[80];
    swprintf_s(name, L"GhostManga-%016llx.lnk", (unsigned long long)paths::InstanceHash());
    auto path = paths::Join(folder, name);
    CoTaskMemFree(folder);
    return path;
}
inline bool Enabled() {
    auto path = ShortcutPath();
    return !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}
inline bool SetEnabled(bool enabled, const std::wstring& path = ShortcutPath()) {
    if (path.empty()) return false;
    if (!enabled) return DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
    wchar_t executable[32768];
    DWORD count = GetModuleFileNameW(nullptr, executable, (DWORD)std::size(executable));
    if (!count || count >= std::size(executable)) return false;
    Microsoft::WRL::ComPtr<IShellLinkW> link;
    Microsoft::WRL::ComPtr<IPersistFile> file;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return false;
    if (FAILED(link->SetPath(executable)) || FAILED(link->SetArguments(L"--startup")) ||
        FAILED(link->SetWorkingDirectory(paths::ExeDir().c_str())) ||
        FAILED(link->SetIconLocation(executable, 0)) || FAILED(link.As(&file))) return false;
    link->SetDescription(L"GhostManga - Windows oturumunda arka planda baslat");
    return SUCCEEDED(file->Save(path.c_str(), TRUE));
}
}
