add_executable(tarkov_tests
    main.cpp
    test_screenshot_name.cpp
    test_log_parser.cpp
    test_heading.cpp
    test_projection.cpp
)

target_link_libraries(tarkov_tests PRIVATE tarkov_core)

target_compile_definitions(tarkov_tests PRIVATE
    TARKOV_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    TARKOV_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
)

add_test(NAME tarkov_tests COMMAND tarkov_tests)
