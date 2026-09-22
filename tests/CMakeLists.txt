add_executable(tarkov_tests
    main.cpp
    test_screenshot_name.cpp
    test_log_parser.cpp
    test_heading.cpp
    test_hotkey.cpp
    test_raid_state.cpp
    test_hud_format.cpp
    test_tile_math.cpp
    test_map_view.cpp
    test_task_model.cpp
    test_svg_renderer.cpp
    test_quest_glyphs.cpp
    test_quest_needs.cpp
    test_quest_events.cpp
    test_line_splitter.cpp
    test_map_content.cpp
    test_quest_progress.cpp
    test_map_features.cpp
    test_strings.cpp
    test_settings.cpp
    test_settings_view.cpp
    test_projection.cpp
)

find_package(Threads REQUIRED)
target_link_libraries(tarkov_tests PRIVATE tarkov_core tarkov_ui Threads::Threads)

target_compile_definitions(tarkov_tests PRIVATE
    TARKOV_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    TARKOV_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
)

add_test(NAME tarkov_tests COMMAND tarkov_tests)
