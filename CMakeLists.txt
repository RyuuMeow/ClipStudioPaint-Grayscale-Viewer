cmake_minimum_required(VERSION 3.20)
project(CSP-Grayscale-Viewer
    VERSION 1.0.0
    DESCRIPTION "Clip Studio Paint Grayscale Viewer"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# ─── Qt6 ──────────────────────────────────────────────────────────────
# Do not hardcode CMAKE_PREFIX_PATH in CMakeLists.txt.
# Provide it via command line: -DCMAKE_PREFIX_PATH="C:/path/to/Qt/6.x.x/msvc2022_64"
# Or set it in your environment variables.
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Widgets)
if(NOT Qt6_FOUND)
    message(FATAL_ERROR "Qt6 not found! Please specify the Qt path using: -DCMAKE_PREFIX_PATH=\"path/to/Qt/6.x/msvc2022_64\"")
endif()

# ─── Source groups ────────────────────────────────────────────────────
set(CORE_SOURCES
    src/core/Application.h
    src/core/Application.cpp
)

set(MAGNIFICATION_SOURCES
    src/magnification/GrayscaleEffect.h
    src/magnification/GrayscaleEffect.cpp
    src/magnification/MagnifierOverlay.h
    src/magnification/MagnifierOverlay.cpp
)

set(WINDOW_SOURCES
    src/window/FocusTracker.h
    src/window/FocusTracker.cpp
    src/window/WindowFinder.h
    src/window/WindowFinder.cpp
)

set(INPUT_SOURCES
    src/input/HotkeyManager.h
    src/input/HotkeyManager.cpp
)

set(UTIL_SOURCES
    src/util/Logger.h
    src/util/Logger.cpp
)

set(GUI_SOURCES
    src/gui/MainWindow.h
    src/gui/MainWindow.cpp
)

# ─── Qt GUI executable ───────────────────────────────────────────────
add_executable(${PROJECT_NAME} WIN32
    src/main.cpp
    ${CORE_SOURCES}
    ${MAGNIFICATION_SOURCES}
    ${WINDOW_SOURCES}
    ${INPUT_SOURCES}
    ${UTIL_SOURCES}
    ${GUI_SOURCES}
    resources/app.manifest
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    magnification
    user32
    shell32
    dwmapi
    winmm
)

# ─── Console fallback (no Qt) ────────────────────────────────────────
add_executable(${PROJECT_NAME}_Console
    src/main_console.cpp
    ${CORE_SOURCES}
    ${MAGNIFICATION_SOURCES}
    ${WINDOW_SOURCES}
    ${INPUT_SOURCES}
    ${UTIL_SOURCES}
    resources/app.manifest
)

target_include_directories(${PROJECT_NAME}_Console PRIVATE src)

target_compile_definitions(${PROJECT_NAME}_Console PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    CSP_CONSOLE_MODE
)

target_link_libraries(${PROJECT_NAME}_Console PRIVATE
    magnification
    user32
    shell32
    dwmapi
    winmm
)
