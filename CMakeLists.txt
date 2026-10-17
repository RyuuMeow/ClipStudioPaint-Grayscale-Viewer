cmake_minimum_required(VERSION 3.20)
project(CSP-Grayscale-Viewer VERSION 1.1.0 LANGUAGES CXX RC)
if(NOT WIN32)
    message(FATAL_ERROR "CSP Grayscale Viewer requires Windows.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Portable executable; no Qt or separately installed MSVC runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_library(csp_core STATIC
    src/core/Application.cpp src/core/Settings.cpp
    src/d3d/D3DOverlayRenderer.cpp
    src/input/HotkeyManager.cpp src/input/Hotkey.cpp
    src/magnification/MagnifierOverlay.cpp
    src/util/Logger.cpp src/window/FocusTracker.cpp src/window/WindowFinder.cpp
)
target_include_directories(csp_core PUBLIC src)
target_compile_definitions(csp_core PUBLIC
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00
)
target_link_libraries(csp_core PUBLIC
    magnification user32 shell32 dwmapi winmm d3d11 dxgi d3dcompiler
    dcomp ole32 uuid advapi32 psapi
)
if(MSVC)
    target_compile_options(csp_core PUBLIC /utf-8 /W4 /permissive-)
endif()
add_executable(${PROJECT_NAME} WIN32
    src/main.cpp src/tray/TrayApplication.cpp src/tray/SettingsDialogs.cpp
    resources/app.rc resources/app.manifest
)
target_link_libraries(${PROJECT_NAME} PRIVATE csp_core comctl32)
add_executable(${PROJECT_NAME}_Console src/main_console.cpp resources/app.manifest)
target_link_libraries(${PROJECT_NAME}_Console PRIVATE csp_core)
include(CTest)
if(BUILD_TESTING)
    add_executable(csp_tests tests/CoreTests.cpp)
    target_link_libraries(csp_tests PRIVATE csp_core)
    add_test(NAME core-regressions COMMAND csp_tests)
    # Explicit opt-in: requires an unlocked desktop and a DXGI-capable GPU.
    add_executable(csp_renderer_tests tests/RendererTests.cpp resources/app.manifest)
    target_link_libraries(csp_renderer_tests PRIVATE csp_core gdi32)
endif()
