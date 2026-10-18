//
// GhostManga giris noktasi.
//
// Ayni veri klasorunden (portable_data) yalniz bir ornek calisir: ikinci
// ornek varsa mevcut pencereyi one getirir, varsa klasor argumanini ona
// iletir ve cikar (FR-DATA-005). Kilit Global\ ad alaninda: Hizli Kullanici
// Degistirme ve iki RDP oturumu ayni USB'yi acinca da tutsun (07 R14c).
//
#include "core/Log.h"
#include "core/Paths.h"
#include "ui/MainWindow.h"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <string>

namespace {

struct FindCtx {
    uint64_t hash;
    HWND found;
};

BOOL CALLBACK FindInstance(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<FindCtx*>(lp);
    wchar_t cls[128];
    if (!GetClassNameW(hwnd, cls, (int)std::size(cls))) return TRUE;
    if (wcscmp(cls, gm::MainWindow::kClassName) != 0) return TRUE;
    if ((uint64_t)(uintptr_t)GetPropW(hwnd, gm::MainWindow::kInstanceProp) != ctx->hash) return TRUE;
    ctx->found = hwnd;
    return FALSE;
}

bool HasArgument(const wchar_t* flag) {
    int argc=0; LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc); bool found=false;
    if(argv) {for(int i=1;i<argc;++i) if(wcscmp(argv[i],flag)==0) found=true;LocalFree(argv);}
    return found;
}

bool ForwardToRunningInstance(const std::wstring& openPath) {
    FindCtx ctx{ gm::paths::InstanceHash(), nullptr };
    EnumWindows(&FindInstance, (LPARAM)&ctx);
    if (!ctx.found) return false;
    if (HasArgument(L"--startup") && openPath.empty()) return true;

    DWORD pid = 0;
    GetWindowThreadProcessId(ctx.found, &pid);
    if (pid) AllowSetForegroundWindow(pid);
    if (!openPath.empty()) {
        COPYDATASTRUCT cds{};
        cds.dwData = gm::MainWindow::kCopyOpenPath;
        cds.cbData = (DWORD)((openPath.size() + 1) * sizeof(wchar_t));
        cds.lpData = (void*)openPath.c_str();
        DWORD_PTR result = 0;
        SendMessageTimeoutW(ctx.found, WM_COPYDATA, 0, (LPARAM)&cds, SMTO_ABORTIFHUNG, 3000, &result);
    }
    PostMessageW(ctx.found, gm::MainWindow::ActivateMessage(), 0, 0);
    return true;
}

std::wstring FirstArgument() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring arg;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (argv[i][0] != L'-') { arg = argv[i]; break; }
        }
        LocalFree(argv);
    }
    if (arg.empty()) return arg;
    // Goreli yol baska surece iletilince onun calisma dizinine gore cozulur:
    // burada, kendi calisma dizinimize gore mutlak yola cevir.
    wchar_t full[MAX_PATH * 4];
    const DWORD n = GetFullPathNameW(arg.c_str(), (DWORD)std::size(full), full, nullptr);
    if (n > 0 && n < std::size(full)) arg.assign(full, n);
    return arg;
}

} // namespace

int WINAPI wWinMain(_In_ HINSTANCE inst, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // Bos kart okuyucu / optik surucu yolu denetlenirken Windows'un "Disk
    // yok" kutusu cizim ortasinda acilmasin; cagri yalniz hata dondursun.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);

    std::wstring err;
    if (!gm::paths::Init(&err)) {
        MessageBoxW(nullptr, err.c_str(), L"GhostManga", MB_ICONERROR | MB_OK);
        return 1;
    }
    gm::log::Init();

    const std::wstring openPath = FirstArgument();

    // Kilit gercek bir sahiplik: ilk ornek mutex'i sahiplenir ve cikana kadar
    // tutar. Ikinci ornek pencereyi bulup ona iletir; pencere henuz yoksa
    // (ilk ornek hala aciliyor) kisa aralarla yeniden arar; o da olmazsa
    // mutex'in birakilmasini bekler (onceki ornek kapanmak uzereydi).
    wchar_t mutexName[64];
    swprintf_s(mutexName, L"Global\\GhostManga-%016llx", (unsigned long long)gm::paths::InstanceHash());
    HANDLE mutex = CreateMutexW(nullptr, TRUE, mutexName);
    const DWORD mutexErr = GetLastError();
    const wchar_t* kOtherSession = L"GhostManga bu veri klasoruyle baska bir Windows oturumunda acik.";
    if (!mutex) {
        if (mutexErr == ERROR_ACCESS_DENIED) {
            MessageBoxW(nullptr, kOtherSession, L"GhostManga", MB_ICONINFORMATION | MB_OK);
            return 0;
        }
        gm::log::Write(L"Tek ornek kilidi olusturulamadi (hata %lu); kilitsiz devam.", mutexErr);
    } else if (mutexErr == ERROR_ALREADY_EXISTS) {
        bool owned = false;
        for (int i = 0; i < 60 && !owned; ++i) {           // en fazla ~3 sn
            if (ForwardToRunningInstance(openPath)) {
                CloseHandle(mutex);
                return 0;
            }
            const DWORD w = WaitForSingleObject(mutex, 50);
            owned = (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED);
        }
        if (!owned) {
            // Pencere yok ama kilit tutuluyor: ayni kullanicinin baska oturumu.
            MessageBoxW(nullptr, kOtherSession, L"GhostManga", MB_ICONINFORMATION | MB_OK);
            CloseHandle(mutex);
            return 0;
        }
    }

    const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    gm::log::Write(L"GhostManga basliyor. Veri: %s (%s)", gm::paths::DataDir().c_str(),
                   gm::paths::IsPortable() ? L"tasinabilir" : L"yerel");

    int exitCode = 0;
    {
        gm::MainWindow win;
        if (!win.Create(inst, openPath, &err)) {
            gm::log::Write(L"MainWindow::Create basarisiz: %s", err.c_str());
            // Kuyrukta WM_QUIT kalmissa MessageBox hemen kapanir: temizle.
            MSG q;
            while (PeekMessageW(&q, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
            MessageBoxW(nullptr, err.empty() ? L"Bilinmeyen hata." : err.c_str(), L"GhostManga", MB_ICONERROR | MB_OK);
            if (SUCCEEDED(hrCo)) CoUninitialize();
            if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); }
            return 1;
        }
        win.Show(HasArgument(L"--startup") ? SW_HIDE : nCmdShow);

        MSG msg{};
        for (;;) {
            bool quit = false;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    exitCode = (int)msg.wParam;
                    quit = true;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (quit) break;
            if (!win.Alive()) continue; // WM_QUIT siradaki turda gelir

            // Tick kare cizdiyse Present zaten vsync'e bagladi; cizmediyse
            // bir sonraki olaya kadar bekle, bosta CPU sifira yakin kalsin.
            const DWORD wait = win.Tick();
            if (wait) MsgWaitForMultipleObjectsEx(0, nullptr, wait, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
    }

    if (SUCCEEDED(hrCo)) CoUninitialize();
    if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); }
    return exitCode;
}
