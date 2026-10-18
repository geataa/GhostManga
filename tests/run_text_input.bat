@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if not exist build\review\obj mkdir build\review\obj
if not exist build\review\tests\portable_data mkdir build\review\tests\portable_data
cl /nologo /W3 /Od /MT /EHsc /std:c++20 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DLEXBOR_STATIC /I src /I third_party\quickjs /I third_party\lexbor\source /I third_party\webview2\include /Fo:build\review\obj\ tests\test_text_input.cpp /Fe:build\review\tests\test_text_input.exe /link /LTCG /SUBSYSTEM:CONSOLE build\obj\*.obj third_party\lexbor\build\Release\lexbor_static.lib third_party\webview2\lib\WebView2LoaderStatic.lib winhttp.lib d3d11.lib dxgi.lib d2d1.lib dwrite.lib dcomp.lib windowscodecs.lib dwmapi.lib shlwapi.lib shell32.lib user32.lib gdi32.lib ole32.lib advapi32.lib uuid.lib version.lib
if errorlevel 1 exit /b 1
build\review\tests\test_text_input.exe
if errorlevel 1 exit /b 1
build\review\tests\test_text_input.exe clipboard
exit /b %errorlevel%
