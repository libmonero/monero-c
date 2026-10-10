# headers shared by the unit and integration tests
add_library(monero_c_test_common INTERFACE)
target_include_directories(monero_c_test_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/common)

add_subdirectory(unit)
add_subdirectory(integration)

# the exported functions must match abi/monero_c.symbols
if (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR APPLE)
  add_test(NAME monero_c_abi_symbols
    COMMAND ${CMAKE_COMMAND} -DLIBRARY=$<TARGET_FILE:monero_c> -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/abi/monero_c.symbols
            -P ${CMAKE_CURRENT_SOURCE_DIR}/abi/check_symbols.cmake)
endif()
