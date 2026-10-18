@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if not exist build\source-audit\tool\portable_data mkdir build\source-audit\tool\portable_data
cl /nologo /W3 /O2 /MT /EHsc /std:c++20 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DLEXBOR_STATIC /I src /I third_party\quickjs /I third_party\lexbor\source /I third_party\webview2\include /Fo:build\review\obj\ tests\source_audit.cpp /Fe:build\source-audit\tool\source_audit.exe /link /LTCG /SUBSYSTEM:CONSOLE build\obj\*.obj third_party\lexbor\build\Release\lexbor_static.lib third_party\webview2\lib\WebView2LoaderStatic.lib winhttp.lib d3d11.lib dxgi.lib d2d1.lib dwrite.lib dcomp.lib windowscodecs.lib dwmapi.lib shlwapi.lib shell32.lib user32.lib gdi32.lib ole32.lib advapi32.lib uuid.lib version.lib
exit /b %errorlevel%
