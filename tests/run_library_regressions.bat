@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
for %%T in (test_m7_export_cache test_convert_optimize) do (
    cl /nologo /W3 /Od /MT /EHsc /std:c++20 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I src /Fo:build\review\obj\ tests\%%T.cpp /Fe:build\review\tests\%%T.exe /link build\review\obj\Library.obj build\review\obj\AmrImport.obj build\review\obj\Paths.obj build\review\obj\FileIo.obj build\review\obj\Json.obj build\review\obj\Log.obj shlwapi.lib shell32.lib ole32.lib user32.lib
    if errorlevel 1 exit /b 1
    pushd build\review\tests
    %%T.exe
    if errorlevel 1 exit /b 1
    popd
)
