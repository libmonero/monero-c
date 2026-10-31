# Installs the shared library, the public headers and the files for find_package(monero_c) and pkg-config, and sets up CPack for .deb and
# .rpm packages. Included by the CMakeLists.txt of the repository unless MONERO_C_INSTALL is OFF.
#
# The static library is not installed. It holds only the objects of monero-c, and a program that links it needs the archives of monero-cpp,
# monero-project and the other dependencies too, which are not part of the install.

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# the ABI version of the header is the version of the CMake package, and the version of the release is the one of the .deb and .rpm
set(MONERO_C_ABI_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/src/utils/monero_c_utils.h)
set(MONERO_C_VERSION_PROPS ${CMAKE_CURRENT_SOURCE_DIR}/packaging/dotnet-core/Version.props)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MONERO_C_ABI_HEADER} ${MONERO_C_VERSION_PROPS})

file(STRINGS ${MONERO_C_ABI_HEADER} monero_c_abi_lines REGEX "^#define MONERO_C_ABI_VERSION_(MAJOR|MINOR|PATCH) ")
foreach (line IN LISTS monero_c_abi_lines)
  if (line MATCHES "MONERO_C_ABI_VERSION_(MAJOR|MINOR|PATCH) +([0-9]+)")
    set(MONERO_C_ABI_${CMAKE_MATCH_1} ${CMAKE_MATCH_2})
  endif()
endforeach()
if ("${MONERO_C_ABI_MAJOR}" STREQUAL "" OR "${MONERO_C_ABI_MINOR}" STREQUAL "" OR "${MONERO_C_ABI_PATCH}" STREQUAL "")
  message(FATAL_ERROR "No ABI version in ${MONERO_C_ABI_HEADER}")
endif()
set(MONERO_C_ABI_VERSION ${MONERO_C_ABI_MAJOR}.${MONERO_C_ABI_MINOR}.${MONERO_C_ABI_PATCH})

file(READ ${MONERO_C_VERSION_PROPS} monero_c_version_props)
if (NOT monero_c_version_props MATCHES "<MoneroNativeVersion>([^<]+)</MoneroNativeVersion>")
  message(FATAL_ERROR "No MoneroNativeVersion in ${MONERO_C_VERSION_PROPS}")
endif()
set(MONERO_C_VERSION ${CMAKE_MATCH_1})

# the same name for a program that adds the repository as a subdirectory and for one that finds the installed package
add_library(monero_c::monero_c ALIAS monero_c)
target_include_directories(monero_c INTERFACE $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/monero_c>)

install(TARGETS monero_c EXPORT monero_cTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# the headers include each other by their path under src/, so they go to a folder of their own and a program adds that folder
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/packaging/common/headers.txt monero_c_headers)
foreach (header IN LISTS monero_c_headers)
  get_filename_component(directory ${header} DIRECTORY)
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/${header} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/monero_c/${directory})
endforeach()

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/packaging/common/THIRD-PARTY-NOTICES.md
  DESTINATION ${CMAKE_INSTALL_DOCDIR})
# the license texts of the dependencies that are linked statically, which the CI collects. A build that links the system libraries needs none
set(MONERO_C_LICENSES_DIR "" CACHE PATH "A folder with the license texts of the dependencies, to install next to the notices")
if (MONERO_C_LICENSES_DIR)
  install(DIRECTORY ${MONERO_C_LICENSES_DIR}/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses)
endif()

# find_package(monero_c): the target monero_c::monero_c. The release is 0.x, so only the same minor version of the ABI matches
set(MONERO_C_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/monero_c)
install(EXPORT monero_cTargets NAMESPACE monero_c:: DESTINATION ${MONERO_C_CMAKE_DIR})
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/monero_cConfigVersion.cmake
  VERSION ${MONERO_C_ABI_VERSION} COMPATIBILITY SameMinorVersion)
configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/packaging/cmake/monero_cConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/monero_cConfig.cmake INSTALL_DESTINATION ${MONERO_C_CMAKE_DIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/monero_cConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/monero_cConfigVersion.cmake
  DESTINATION ${MONERO_C_CMAKE_DIR})

# pkg-config: the prefix is found from the place of the file, so the install can be moved. It is one .. for each folder of libdir/pkgconfig,
# counted by hand because file(RELATIVE_PATH) wants a drive letter on Windows
string(REPLACE "/" ";" monero_c_pc_folders "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
set(monero_c_pc_up "")
foreach (folder IN LISTS monero_c_pc_folders)
  list(APPEND monero_c_pc_up "..")
endforeach()
list(JOIN monero_c_pc_up "/" MONERO_C_PC_PREFIX)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/packaging/cmake/monero_c.pc.in ${CMAKE_CURRENT_BINARY_DIR}/monero_c.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/monero_c.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

include(${CMAKE_CURRENT_SOURCE_DIR}/packaging/cmake/cpack.cmake)
