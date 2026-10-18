@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo   GhostManga - MSVC Derleyici Scripti
echo ========================================================

cd /d "%~dp0"

if /i "%~1"=="clean" (
    echo Temizleme baslatiliyor...
    if exist build (
        echo build\ klasoru siliniyor...
        rmdir /s /q build
    )
    if exist bin\*.obj del /q bin\*.obj >nul 2>nul
    if exist bin\*.res del /q bin\*.res >nul 2>nul
    if exist bin\*.ilk del /q bin\*.ilk >nul 2>nul
    if exist bin\*.pdb del /q bin\*.pdb >nul 2>nul
    if exist bin\GhostManga.exe del /q bin\GhostManga.exe >nul 2>nul
    if exist *.obj del /q *.obj >nul 2>nul
    if exist *.res del /q *.res >nul 2>nul
    if exist *.ilk del /q *.ilk >nul 2>nul
    if exist *.pdb del /q *.pdb >nul 2>nul
    echo Temizleme tamamlandi.
    exit /b 0
)

if not exist bin mkdir bin
if not exist build\obj mkdir build\obj

set "VCVARS="
for %%E in (Community Professional Enterprise BuildTools) do (
    if exist "C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" (
        if "!VCVARS!"=="" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
    )
)
if "%VCVARS%"=="" (
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
        set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    )
)
if "%VCVARS%"=="" (
    echo HATA: Visual Studio 2022 C++ araclari bulunamadi!
    exit /b 1
)

echo Visual Studio ortami: %VCVARS%
call "%VCVARS%" >nul

echo.
echo QuickJS derleniyor...
if not exist build\obj\quickjs.obj (
    cl /nologo /c /O2 /MT /utf-8 /std:c11 /experimental:c11atomics /Ithird_party\quickjs third_party\quickjs\quickjs-amalgam.c /Fobuild\obj\quickjs.obj
    if !ERRORLEVEL! NEQ 0 exit /b !ERRORLEVEL!
)

echo.
echo Kaynaklar derleniyor...
rc /nologo /i resources /fo build\obj\GhostManga.res resources\GhostManga.rc
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

echo Derleme basliyor...
cl /nologo /W3 /O2 /Oi /GL /MT /EHsc /std:c++20 /utf-8 /Zc:__cplusplus ^
    /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DLEXBOR_STATIC /D_CRT_SECURE_NO_WARNINGS ^
    /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 /DNTDDI_VERSION=0x0A000006 ^
    /I src /I third_party\quickjs /I third_party\lexbor\source /I third_party\webview2\include /I third_party\miniz ^
    /Fo:build\obj\ ^
    src\main.cpp ^
    src\core\Paths.cpp ^
    src\core\Log.cpp ^
    src\core\Settings.cpp ^
    src\core\I18n.cpp ^
    src\core\FileIo.cpp ^
    src\core\Json.cpp ^
    src\net\CookieJar.cpp ^
    src\net\HttpClient.cpp ^
    src\net\ChallengeDetector.cpp ^
    src\net\TransportRouter.cpp ^
    src\net\Fetch.cpp ^
    src\browser\WebViewHost.cpp ^
    src\engine\HtmlDocument.cpp ^
    src\engine\JsEngine.cpp ^
    src\engine\JsBridge.cpp ^
    src\engine\MirrorEngine.cpp ^
    src\engine\ChapterLoader.cpp ^
    src\engine\CbzWriter.cpp ^
    src\engine\ArchiveReader.cpp ^
    src\engine\Scheduler.cpp ^
    src\engine\AmrImport.cpp ^
    src\engine\Library.cpp ^
    src\engine\LocalSource.cpp ^
    src\engine\PageDecoder.cpp ^
    src\engine\CoverManager.cpp ^
    third_party\miniz\miniz.c ^
    third_party\miniz\miniz_tdef.c ^
    third_party\miniz\miniz_tinfl.c ^
    third_party\miniz\miniz_zip.c ^
    src\ui\Renderer.cpp ^
    src\ui\Icons.cpp ^
    src\ui\Widgets.cpp ^
    src\ui\ReaderView.cpp ^
    src\ui\LibraryView.cpp ^
    src\ui\SettingsView.cpp ^
    src\ui\MainWindow.cpp ^
    src\ui\BrowserView.cpp ^
    build\obj\GhostManga.res ^
    /Fe:bin\GhostManga.exe ^
    /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS ^
    build\obj\quickjs.obj third_party\lexbor\build\Release\lexbor_static.lib third_party\webview2\lib\WebView2LoaderStatic.lib ^
    winhttp.lib d3d11.lib dxgi.lib d2d1.lib dwrite.lib dcomp.lib windowscodecs.lib dwmapi.lib ^
    shlwapi.lib shell32.lib user32.lib gdi32.lib ole32.lib advapi32.lib uuid.lib version.lib

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo DERLEME HATASI! Kod: %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)

rem Calisan GhostManga exe'yi kilitler; kopyalama basarisizsa bunu soyle,
rem basari diye raporlama.
copy /Y bin\GhostManga.exe GhostManga.exe >nul 2>nul
if errorlevel 1 (
    echo.
    echo ========================================================
    echo   DERLEME BASARILI, AMA GhostManga.exe GUNCELLENEMEDI
    echo   Uygulama acik ve dosyayi kilitliyor. Tepsiden Cik'i
    echo   secip build.bat'i yeniden calistir. Guncel exe:
    echo   bin\GhostManga.exe
    echo ========================================================
    exit /b 2
)
echo.
echo ========================================================
echo   DERLEME BASARILI  -^>  GhostManga.exe
echo ========================================================
