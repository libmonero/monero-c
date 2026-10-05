# headers shared by the unit and integration tests
add_library(monero_c_test_common INTERFACE)
target_include_directories(monero_c_test_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/common)

add_subdirectory(unit)
add_subdirectory(integration)
