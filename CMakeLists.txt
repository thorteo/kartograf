cmake_minimum_required(VERSION 3.20)
project(tarkov_help LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug)
endif()

option(TARKOV_BUILD_TESTS "Build the test suite" ON)

# ---------------------------------------------------------------------------
# tarkov_core -- portable. Must never include <windows.h>; see CLAUDE.md.
# ---------------------------------------------------------------------------
add_library(tarkov_core STATIC
    src/core/game/ScreenshotName.cpp
    src/core/game/LogParser.cpp
    src/core/input/Hotkey.cpp
    src/core/geo/Heading.cpp
    src/core/geo/MapConfig.cpp
    src/core/geo/Projection.cpp
)

target_include_directories(tarkov_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Separate call: the SYSTEM keyword has to precede the first visibility
# specifier, so it cannot be mixed into the one above.
target_include_directories(tarkov_core SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

if(MSVC)
    target_compile_options(tarkov_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(tarkov_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# tarkov_help -- the overlay itself. Windows only.
# ---------------------------------------------------------------------------
if(WIN32)
    add_executable(tarkov_help
        src/app/main.cpp
        src/platform/win32/OverlayWindow.cpp
        src/platform/win32/GameWindow.cpp
    )

    target_link_libraries(tarkov_help PRIVATE
        tarkov_core
        d3d11
        dcomp
        dxgi
        user32
        gdi32
    )

    if(MSVC)
        target_compile_options(tarkov_help PRIVATE /W4 /permissive-)
    endif()
endif()

if(TARKOV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
