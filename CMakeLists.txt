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
    src/core/game/RaidState.cpp
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
# tarkov_imgui -- Dear ImGui core. Portable; the platform backends are added
# only to the Windows target, which is what keeps src/ui testable on Linux.
# ---------------------------------------------------------------------------
add_library(tarkov_imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
)
target_include_directories(tarkov_imgui SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
)

# ---------------------------------------------------------------------------
# tarkov_ui -- views. Portable: ImGui draws into a vertex buffer, so the HUD
# can be built and its formatting tested without a Windows build.
# ---------------------------------------------------------------------------
add_library(tarkov_ui STATIC src/ui/Hud.cpp)
target_link_libraries(tarkov_ui PUBLIC tarkov_core tarkov_imgui)

# ---------------------------------------------------------------------------
# tarkov_help -- the overlay itself. Windows only.
# ---------------------------------------------------------------------------
if(WIN32)
    add_executable(tarkov_help WIN32
        src/app/main.cpp
        src/platform/win32/OverlayWindow.cpp
        src/platform/win32/GameWindow.cpp
        src/platform/win32/GameFiles.cpp
        src/platform/win32/GameDataWatcher.cpp
        src/platform/win32/ImGuiLayer.cpp
        third_party/imgui/backends/imgui_impl_win32.cpp
        third_party/imgui/backends/imgui_impl_dx11.cpp
    )

    target_link_libraries(tarkov_help PRIVATE
        tarkov_core
        tarkov_ui
        d3d11
        dcomp
        dxgi
        user32
        gdi32
        shell32
        ole32
        advapi32
        dwmapi
    )

    if(MSVC)
        target_compile_options(tarkov_help PRIVATE /W4 /permissive-)
    endif()

    # WIN32 gives the GUI subsystem, so no console window appears behind the
    # game. The CRT then looks for WinMain; pointing the entry at
    # mainCRTStartup keeps a plain main() with argc/argv instead.
    target_link_options(tarkov_help PRIVATE /ENTRY:mainCRTStartup)
endif()

if(TARKOV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
