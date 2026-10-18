cmake_minimum_required(VERSION 3.20)
project(GhostManga LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Statik CRT: Redistributable gerekmesin (NFR-001).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(GhostManga WIN32
    src/main.cpp
    src/core/Paths.cpp
    src/core/Log.cpp
    src/core/Settings.cpp
    src/core/I18n.cpp
    src/core/FileIo.cpp
    src/core/Json.cpp
    src/engine/AmrImport.cpp
    src/engine/Library.cpp
    src/engine/LocalSource.cpp
    src/engine/PageDecoder.cpp
    src/ui/Renderer.cpp
    src/ui/Icons.cpp
    src/ui/Widgets.cpp
    src/ui/ReaderView.cpp
    src/ui/LibraryView.cpp
    src/ui/SettingsView.cpp
    src/ui/MainWindow.cpp
    resources/GhostManga.rc
)

target_include_directories(GhostManga PRIVATE src)
target_compile_definitions(GhostManga PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    WINVER=0x0A00 _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000006)
target_compile_options(GhostManga PRIVATE /W3 /EHsc /utf-8 /Zc:__cplusplus)
target_link_libraries(GhostManga PRIVATE
    d3d11 dxgi d2d1 dwrite dcomp windowscodecs dwmapi
    shlwapi shell32 user32 gdi32 ole32 advapi32 uuid)
