# CPack settings for the .deb and .rpm packages: build with -DCMAKE_INSTALL_PREFIX=/usr, then run cpack -G DEB or cpack -G RPM in the
# build folder. A version with a prerelease, such as 0.2.0-alpha.1, is written 0.2.0~alpha.1 for both, which sorts it before 0.2.0.

string(REPLACE "-" "~" monero_c_package_version "${MONERO_C_VERSION}")

set(CPACK_PACKAGE_NAME monero-c)
set(CPACK_PACKAGE_VENDOR Libmonero)
set(CPACK_PACKAGE_CONTACT Libmonero)
set(CPACK_PACKAGE_HOMEPAGE_URL https://github.com/libmonero/monero-c)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "C ABI of monero-cpp for wallets and daemons of Monero")
set(CPACK_PACKAGE_DESCRIPTION "monero-c is a shared library with a C interface to the wallet and the daemon of monero-cpp, for languages that load C libraries.")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)

set(CPACK_DEBIAN_PACKAGE_VERSION ${monero_c_package_version})
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION libs)
if (MONERO_C_STATIC_DEPENDENCIES AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # the libraries of the system that a static build still links, and libusb, which is built from source and has no package to find
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.34), libstdc++6 (>= 12), libgcc-s1, libusb-1.0-0")
else()
  # the packages the library needs are found from the libraries that it links
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()

set(CPACK_RPM_PACKAGE_VERSION ${monero_c_package_version})
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE MIT)
set(CPACK_RPM_PACKAGE_GROUP "System Environment/Libraries")
set(CPACK_RPM_PACKAGE_RELEASE 1)

include(CPack)
