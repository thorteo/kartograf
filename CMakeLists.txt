cmake_minimum_required(VERSION 3.20)
# C is needed for plutovg, lunasvg's rasteriser backend. Without it CMake
# silently drops every .c file from the build and the link fails with hundreds
# of undefined references.
project(tarkov_help LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug)
endif()

option(TARKOV_BUILD_TESTS "Build the test suite" ON)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# maps.json is compiled in rather than shipped beside the executable, so a
# release is one file and cannot be half-updated.
set(TARKOV_EMBEDDED_MAPS ${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedMaps.cpp)
set(TARKOV_EMBEDDED_MAP_IDS ${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedMapIds.cpp)
add_custom_command(
    OUTPUT ${TARKOV_EMBEDDED_MAPS}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_file.py
            ${CMAKE_CURRENT_SOURCE_DIR}/data/maps.json
            ${TARKOV_EMBEDDED_MAPS}
            tarkov::data kMapsJson
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/data/maps.json
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_file.py
    COMMENT "Embedding maps.json"
    VERBATIM
)

add_custom_command(
    OUTPUT ${TARKOV_EMBEDDED_MAP_IDS}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_file.py
            ${CMAKE_CURRENT_SOURCE_DIR}/data/map-ids.json
            ${TARKOV_EMBEDDED_MAP_IDS}
            tarkov::data kMapIdsJson
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/data/map-ids.json
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_file.py
    COMMENT "Embedding map-ids.json"
    VERBATIM
)

# ---------------------------------------------------------------------------
# tarkov_core -- portable. Must never include <windows.h>; see CLAUDE.md.
# ---------------------------------------------------------------------------
add_library(tarkov_core STATIC
    src/core/game/ScreenshotName.cpp
    src/core/game/LogParser.cpp
    src/core/game/RaidState.cpp
    src/core/input/Hotkey.cpp
    src/core/data/TaskModel.cpp
    src/core/data/MapIds.cpp
    ${TARKOV_EMBEDDED_MAP_IDS}
    src/core/map/TileMath.cpp
    src/core/map/SvgRenderer.cpp
    src/core/map/SvgPlacement.cpp
    ${TARKOV_EMBEDDED_MAPS}
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

target_link_libraries(tarkov_core PUBLIC tarkov_lunasvg)

if(MSVC)
    target_compile_options(tarkov_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(tarkov_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# tarkov_lunasvg -- vendored SVG rasteriser (MIT, with its plutovg backend).
#
# The abstract maps tarkov.dev publishes are SVG, and three maps have no raster
# tiles at all. Rendering them needs a real rasteriser; lunasvg is small,
# dependency-free, and handles the <use>, transform and CSS-class features these
# files actually use. System font loading is disabled: the map SVGs contain no
# text, and labels are drawn from maps.json instead.
# ---------------------------------------------------------------------------
file(GLOB TARKOV_LUNASVG_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lunasvg/source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lunasvg/plutovg/source/*.c
)
add_library(tarkov_lunasvg STATIC ${TARKOV_LUNASVG_SOURCES})
target_include_directories(tarkov_lunasvg SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lunasvg/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lunasvg/plutovg/include
)
target_include_directories(tarkov_lunasvg PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lunasvg/plutovg/source
)
target_compile_definitions(tarkov_lunasvg
    PUBLIC  LUNASVG_BUILD_STATIC PLUTOVG_BUILD_STATIC
    PRIVATE LUNASVG_BUILD PLUTOVG_BUILD
            LUNASVG_DISABLE_LOAD_SYSTEM_FONTS
)
if(MSVC)
    # Vendored third-party code is not ours to keep warning-clean.
    target_compile_options(tarkov_lunasvg PRIVATE /W0)
else()
    target_compile_options(tarkov_lunasvg PRIVATE -w)
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
add_library(tarkov_ui STATIC src/ui/Hud.cpp src/ui/MapView.cpp)
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
        src/platform/win32/HttpClient.cpp
        src/platform/win32/TileStore.cpp
        src/platform/win32/TaskRepository.cpp
        src/platform/win32/SvgMapStore.cpp
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
        winhttp
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
