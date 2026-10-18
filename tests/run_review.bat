@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if not exist build\review\tests\portable_data mkdir build\review\tests\portable_data
if not exist build\review\obj mkdir build\review\obj
cl /nologo /W3 /Od /Zi /MT /EHsc /std:c++20 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DLEXBOR_STATIC /D_CRT_SECURE_NO_WARNINGS /I src /I third_party\quickjs /I third_party\lexbor\source /I third_party\webview2\include /Fo:build\review\obj\ tests\test_review.cpp src\engine\Library.cpp src\engine\AmrImport.cpp src\core\Paths.cpp src\core\FileIo.cpp src\core\Json.cpp src\core\Log.cpp src\engine\MirrorEngine.cpp src\engine\JsEngine.cpp src\engine\JsBridge.cpp src\engine\HtmlDocument.cpp src\engine\Scheduler.cpp src\engine\ChapterLoader.cpp src\engine\LocalSource.cpp src\net\Fetch.cpp src\net\CookieJar.cpp src\net\TransportRouter.cpp src\net\ChallengeDetector.cpp src\net\HttpClient.cpp src\browser\WebViewHost.cpp /Fe:build\review\tests\test_review.exe /link build\obj\quickjs.obj third_party\lexbor\build\Release\lexbor_static.lib third_party\webview2\lib\WebView2LoaderStatic.lib windowscodecs.lib uuid.lib shlwapi.lib shell32.lib ole32.lib user32.lib version.lib advapi32.lib
if errorlevel 1 exit /b 1
build\review\tests\test_review.exe %*
exit /b %errorlevel%
