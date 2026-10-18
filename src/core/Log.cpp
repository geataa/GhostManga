#include "core/Log.h"
#include "core/Paths.h"
#include "core/Utf8.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace gm::log {
namespace {

std::mutex g_mu;
std::wstring g_path;
constexpr LONGLONG kMaxBytes = 1024 * 1024;

void RotateIfNeeded() {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(g_path.c_str(), GetFileExInfoStandard, &fad)) return;
    LARGE_INTEGER size{};
    size.HighPart = (LONG)fad.nFileSizeHigh;
    size.LowPart = fad.nFileSizeLow;
    if (size.QuadPart < kMaxBytes) return;
    const std::wstring old = g_path + L".1";
    MoveFileExW(g_path.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
}

} // namespace

void Init() {
    std::lock_guard<std::mutex> lk(g_mu);
    const std::wstring dir = paths::Join(paths::DataDir(), L"logs");
    if (!paths::EnsureDir(dir)) return;
    g_path = paths::Join(dir, L"ghostmanga.log");
}

void Write(const wchar_t* fmt, ...) {
    wchar_t msg[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(msg, _TRUNCATE, fmt, ap);
    va_end(ap);

    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t line[2200];
    _snwprintf_s(line, _TRUNCATE, L"%04u-%02u-%02u %02u:%02u:%02u.%03u  %s\r\n",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                 st.wMilliseconds, msg);

    std::lock_guard<std::mutex> lk(g_mu);
    if (g_path.empty()) return;
    RotateIfNeeded();
    HANDLE h = CreateFileW(g_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    const std::string u8 = ToUtf8(line);
    DWORD written = 0;
    WriteFile(h, u8.data(), (DWORD)u8.size(), &written, nullptr);
    CloseHandle(h);
}

} // namespace gm::log
