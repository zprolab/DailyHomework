file(GLOB DHW_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")

add_executable(dhw_tests dhw_test.cpp ${DHW_TEST_SOURCES})
target_link_libraries(dhw_tests PRIVATE dhw_core)
target_compile_definitions(dhw_tests PRIVATE DHW_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/fixtures")
dhw_set_warnings(dhw_tests)

add_test(NAME dhw_tests COMMAND dhw_tests)
