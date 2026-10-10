cmake_minimum_required(VERSION 3.20)
project(monero_c LANGUAGES CXX C)

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_definitions(-DBOOST_UUID_DISABLE_ALIGNMENT) # match monero-project/monero-cpp so shared struct layouts agree

option(BUILD_TESTS "Build monero-c tests" OFF)
option(ENABLE_COVERAGE "Enable coverage" OFF)

if(ENABLE_COVERAGE)
  message(STATUS "Coverage enabled")
  add_compile_options(--coverage -O0 -g -fprofile-update=atomic)
  add_link_options(--coverage)
endif()

if (WIN32)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

##############################################################################
# monero-cpp / monero-project
##############################################################################

set(MONERO_CPP "${CMAKE_SOURCE_DIR}/external/monero-cpp" CACHE PATH "monero-cpp source directory")
set(MONERO_CPP_BUILD "${MONERO_CPP}/build" CACHE PATH "monero-cpp build directory (holds libmonero-cpp.*)")
set(MONERO_PROJECT "${MONERO_CPP}/external/monero-project" CACHE PATH "monero-project source directory")
set(MONERO_PROJECT_SRC "${MONERO_PROJECT}/src")
set(MONERO_PROJECT_BUILD "${MONERO_PROJECT}/build/release" CACHE PATH "monero-project release build directory")

list(APPEND CMAKE_MODULE_PATH "${MONERO_PROJECT}/cmake")

add_library(monero-cpp STATIC IMPORTED)
set_target_properties(monero-cpp PROPERTIES IMPORTED_LOCATION
    ${MONERO_CPP_BUILD}/libmonero-cpp.a)

add_library(wallet STATIC IMPORTED)
set_target_properties(wallet PROPERTIES IMPORTED_LOCATION
    ${MONERO_PROJECT_BUILD}/lib/libwallet.a)

# libwallet-crypto.a provides x86_64 asm for some wallet functions
if (EXISTS ${MONERO_PROJECT_BUILD}/src/crypto/wallet/libwallet-crypto.a)
  add_library(wallet_crypto_lib STATIC IMPORTED)
  set_target_properties(wallet_crypto_lib PROPERTIES IMPORTED_LOCATION
      ${MONERO_PROJECT_BUILD}/src/crypto/wallet/libwallet-crypto.a)
  set(wallet_crypto wallet_crypto_lib)
endif()

add_library(lmdb STATIC IMPORTED)
set_target_properties(lmdb PROPERTIES IMPORTED_LOCATION
    ${MONERO_PROJECT_BUILD}/external/db_drivers/liblmdb/liblmdb.a)

add_library(epee STATIC IMPORTED)
set_target_properties(epee PROPERTIES IMPORTED_LOCATION
    ${MONERO_PROJECT_BUILD}/contrib/epee/src/libepee.a)

add_library(rpc_base STATIC IMPORTED)
set_target_properties(rpc_base PROPERTIES IMPORTED_LOCATION
    ${MONERO_PROJECT_BUILD}/src/rpc/librpc_base.a)

add_library(net STATIC IMPORTED)
set_target_properties(net PROPERTIES IMPORTED_LOCATION
    ${MONERO_PROJECT_BUILD}/src/net/libnet.a)

add_library(hardforks STATIC IMPORTED)
set_target_properties(hardforks PROPERTIES IMPORTED_LOCATION
    ${MONERO_PROJECT_BUILD}/src/hardforks/libhardforks.a)

add_library(easylogging STATIC IMPORTED)
set_target_properties(easylogging PROPERTIES IMPORTED_LOCATION
    ${MONERO_PROJECT_BUILD}/external/easylogging++/libeasylogging.a)

add_library(cryptonote_core STATIC IMPORTED)
set_target_properties(cryptonote_core PROPERTIES IMPORTED_LOCATION
    ${MONERO_PROJECT_BUILD}/src/cryptonote_core/libcryptonote_core.a)

add_library(cryptonote_protocol STATIC IMPORTED)
set_target_properties(cryptonote_protocol PROPERTIES IMPORTED_LOCATION
    ${MONERO_PROJECT_BUILD}/src/cryptonote_protocol/libcryptonote_protocol.a)

add_library(cryptonote_basic STATIC IMPORTED)
set_target_properties(cryptonote_basic PROPERTIES IMPORTED_LOCATION
    ${MONERO_PROJECT_BUILD}/src/cryptonote_basic/libcryptonote_basic.a)

add_library(cryptonote_format_utils_basic STATIC IMPORTED)
set_target_properties(cryptonote_format_utils_basic PROPERTIES IMPORTED_LOCATION
    ${MONERO_PROJECT_BUILD}/src/cryptonote_basic/libcryptonote_format_utils_basic.a)

add_library(mnemonics STATIC IMPORTED)
set_target_properties(mnemonics PROPERTIES IMPORTED_LOCATION
    ${MONERO_PROJECT_BUILD}/src/mnemonics/libmnemonics.a)

add_library(common STATIC IMPORTED)
set_target_properties(common PROPERTIES IMPORTED_LOCATION
    ${MONERO_PROJECT_BUILD}/src/common/libcommon.a)

add_library(cncrypto STATIC IMPORTED)
set_target_properties(cncrypto PROPERTIES IMPORTED_LOCATION
    ${MONERO_PROJECT_BUILD}/src/crypto/libcncrypto.a)

add_library(ringct STATIC IMPORTED)
set_target_properties(ringct PROPERTIES IMPORTED_LOCATION
    ${MONERO_PROJECT_BUILD}/src/ringct/libringct.a)

add_library(ringct_basic STATIC IMPORTED)
set_target_properties(ringct_basic PROPERTIES IMPORTED_LOCATION
    ${MONERO_PROJECT_BUILD}/src/ringct/libringct_basic.a)

add_library(blockchain_db STATIC IMPORTED)
set_target_properties(blockchain_db PROPERTIES IMPORTED_LOCATION
    ${MONERO_PROJECT_BUILD}/src/blockchain_db/libblockchain_db.a)

add_library(blocks STATIC IMPORTED)
set_target_properties(blocks PROPERTIES IMPORTED_LOCATION
    ${MONERO_PROJECT_BUILD}/src/blocks/libblocks.a)

add_library(checkpoints STATIC IMPORTED)
set_target_properties(checkpoints PROPERTIES IMPORTED_LOCATION
    ${MONERO_PROJECT_BUILD}/src/checkpoints/libcheckpoints.a)

add_library(device STATIC IMPORTED)
set_target_properties(device PROPERTIES IMPORTED_LOCATION
    ${MONERO_PROJECT_BUILD}/src/device/libdevice.a)

add_library(device_trezor STATIC IMPORTED)
set_target_properties(device_trezor PROPERTIES IMPORTED_LOCATION
    ${MONERO_PROJECT_BUILD}/src/device_trezor/libdevice_trezor.a)

add_library(multisig STATIC IMPORTED)
set_target_properties(multisig PROPERTIES IMPORTED_LOCATION
    ${MONERO_PROJECT_BUILD}/src/multisig/libmultisig.a)

add_library(version STATIC IMPORTED)
set_target_properties(version PROPERTIES IMPORTED_LOCATION
    ${MONERO_PROJECT_BUILD}/src/libversion.a)

add_library(randomx STATIC IMPORTED)
set_target_properties(randomx PROPERTIES IMPORTED_LOCATION
    ${MONERO_PROJECT_BUILD}/external/randomx/librandomx.a)

##############################################################################
# monero-project's external dependencies
##############################################################################

option(MONERO_C_STATIC_DEPENDENCIES "Prefer static third-party dependencies other than libusb" OFF)

if (MONERO_C_STATIC_DEPENDENCIES)
  set(Boost_USE_STATIC_LIBS ON)
  set(OPENSSL_USE_STATIC_LIBS ON)
  set(Protobuf_USE_STATIC_LIBS ON)
  if (APPLE)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a .dylib .tbd)
  elseif (WIN32)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a .lib)
  else()
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
  endif()
endif()

find_package(Threads REQUIRED)

# MSYS2's boost package ships regex only as a CMake config package, not a legacy-findable lib file.
if (NOT WIN32)
  set(Boost_NO_BOOST_CMAKE 1)
endif()
set(Boost_USE_MULTITHREADED ON)
set(BOOST_COMPONENTS chrono date_time filesystem locale program_options regex serialization thread)

set(BOOST_MIN_VER 1.62)
find_package(Boost ${BOOST_MIN_VER} QUIET REQUIRED)

# Boost System is header-only since 1.69
if (Boost_VERSION_STRING VERSION_LESS 1.69.0)
  list(APPEND BOOST_COMPONENTS system)
endif()

find_package(Boost ${BOOST_MIN_VER} QUIET REQUIRED COMPONENTS ${BOOST_COMPONENTS})

find_package(OpenSSL REQUIRED)
if (WIN32)
  list(APPEND OPENSSL_LIBRARIES ws2_32 crypt32 bcrypt)
elseif (MONERO_C_STATIC_DEPENDENCIES AND UNIX)
  list(APPEND OPENSSL_LIBRARIES ${CMAKE_DL_LIBS})
endif()

find_package(Unbound REQUIRED)

if (MONERO_C_STATIC_DEPENDENCIES)
  # static libunbound dependencies
  foreach(dependency expat event event_core nghttp2)
    string(TOUPPER "${dependency}" dependency_upper)
    find_library(${dependency_upper}_LIBRARY NAMES ${dependency}
      HINTS /opt/homebrew/opt/${dependency}/lib /usr/local/opt/${dependency}/lib)
    if (${dependency_upper}_LIBRARY)
      list(APPEND UNBOUND_LIBRARIES "${${dependency_upper}_LIBRARY}")
    endif()
  endforeach()
endif()

if (MONERO_C_STATIC_DEPENDENCIES)
  set(MONERO_C_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
  if (WIN32)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .dll.a .lib)
  elseif (APPLE)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .dylib)
  else()
    set(CMAKE_FIND_LIBRARY_SUFFIXES .so)
  endif()
  find_library(MONERO_C_LIBUSB_SHARED_LIBRARY NAMES usb-1.0 libusb usb)
  set(CMAKE_FIND_LIBRARY_SUFFIXES ${MONERO_C_FIND_LIBRARY_SUFFIXES})
  if (NOT MONERO_C_LIBUSB_SHARED_LIBRARY OR
      NOT EXISTS "${MONERO_C_LIBUSB_SHARED_LIBRARY}")
    message(FATAL_ERROR "MONERO_C_STATIC_DEPENDENCIES requires a shared libusb library")
  endif()
  set(LibUSB_LIBRARIES ${MONERO_C_LIBUSB_SHARED_LIBRARY})
else()
  find_library(usb_LIBRARY NAMES usb-1.0 libusb usb)
  if (usb_LIBRARY)
    set(LibUSB_LIBRARIES ${usb_LIBRARY})
  endif()
endif()

find_package(HIDAPI REQUIRED)

# hidapi's macOS backend needs these directly (see monero-project's own CMakeLists.txt).
if (APPLE)
  find_library(COREFOUNDATION_LIBRARY CoreFoundation REQUIRED)
  find_library(APPKIT_LIBRARY AppKit REQUIRED)
  find_library(IOKIT_LIBRARY IOKit REQUIRED)
  list(APPEND HIDAPI_LIBRARIES ${COREFOUNDATION_LIBRARY} ${APPKIT_LIBRARY} ${IOKIT_LIBRARY})
endif()

# hidapi's Windows backend needs setupapi.
if (WIN32)
  list(APPEND HIDAPI_LIBRARIES setupapi)
endif()

find_library(SODIUM_LIBRARY sodium REQUIRED)

if (NOT APPLE)
  include(FindProtobuf)
  find_package(Protobuf)
endif()

##############################################################################
# monero_c library
##############################################################################

set(MONERO_C_SRC_FILES
    src/common/monero_c_common.cpp
    src/common/monero_c_rpc_connection.cpp
    src/utils/monero_c_utils.cpp
    src/daemon/monero_c_daemon.cpp
    src/wallet/monero_c_wallet.cpp
)

# compile once, build both shared and static from the objects (same as monero-cpp's own CMakeLists.txt)
add_library(monero_c-obj OBJECT ${MONERO_C_SRC_FILES})
# hide internal helpers: only MONERO_EXPORT functions are exported from these objects
set_target_properties(monero_c-obj PROPERTIES POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden C_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
target_compile_definitions(monero_c-obj PRIVATE MONERO_C_BUILD)

target_include_directories(monero_c-obj
    PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE
      "${MONERO_CPP}/src"
      "${MONERO_PROJECT}/contrib/epee/include"
      "${MONERO_PROJECT}/external"
      "${MONERO_PROJECT}/external/easylogging++"
      "${MONERO_PROJECT}/external/rapidjson/include"
      "${MONERO_PROJECT_SRC}"
      "${MONERO_PROJECT_SRC}/crypto"
      "${MONERO_PROJECT_SRC}/crypto/crypto_ops_builder/include"
      "${MONERO_CPP}/external/libsodium/include/sodium"
      ${Boost_INCLUDE_DIR}
      ${OPENSSL_INCLUDE_DIR}
      ${HIDAPI_INCLUDE_DIR}
      ${Protobuf_INCLUDE_DIR}
      ${UNBOUND_INCLUDE_DIR}
)

add_library(monero_c SHARED $<TARGET_OBJECTS:monero_c-obj>)
add_library(monero_c-static STATIC $<TARGET_OBJECTS:monero_c-obj>)
set_target_properties(monero_c-static PROPERTIES OUTPUT_NAME monero_c)
if (APPLE)
  set_target_properties(monero_c PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "@loader_path")
elseif (UNIX)
  set_target_properties(monero_c PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN")
endif()

# $<TARGET_OBJECTS:...> doesn't propagate monero_c-obj's PUBLIC include dirs, so repeat them here.
target_include_directories(monero_c PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(monero_c-static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(MONERO_C_LINK_LIBS
    monero-cpp
    wallet
    rpc_base
    net
    lmdb
    easylogging
    cryptonote_core
    cryptonote_protocol
    cryptonote_basic
    cryptonote_format_utils_basic
    mnemonics
    ringct
    ringct_basic
    common
    blockchain_db
    blocks
    checkpoints
    device
    device_trezor
    multisig
    version
    cncrypto
    randomx
    epee
    hardforks
    ${wallet_crypto}

    Threads::Threads
    ${Boost_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    ${Protobuf_LIBRARIES}
    ${SODIUM_LIBRARY}
    ${HIDAPI_LIBRARIES}
    ${LibUSB_LIBRARIES}
    ${UNBOUND_LIBRARIES}
)

target_link_libraries(monero_c PRIVATE ${MONERO_C_LINK_LIBS})
target_link_libraries(monero_c-static PRIVATE ${MONERO_C_LINK_LIBS})

if (WIN32)
  target_link_libraries(monero_c PRIVATE ws2_32 iphlpapi)
  target_link_libraries(monero_c-static PRIVATE ws2_32 iphlpapi)
  target_link_options(monero_c PRIVATE "-Wl,-Bstatic,--whole-archive" -lwinpthread "-Wl,--no-whole-archive,-Bdynamic")
  target_link_options(monero_c PRIVATE -static-libstdc++ -static-libgcc)
endif()

# monero-project's ge25519 asm lacks .note.GNU-stack. Keep the stack non-executable (GNU ld/lld only)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_options(monero_c PRIVATE -Wl,-z,noexecstack)
  # export only the C ABI, not the symbols of the static archives (monero-project, boost, OpenSSL)
  target_link_options(monero_c PRIVATE -Wl,--exclude-libs,ALL)
  # bind every symbol at load time, so the GOT is read-only after it (full RELRO)
  target_link_options(monero_c PRIVATE -Wl,-z,relro,-z,now)
  # the static lib has no link step, so its consumers must get the flag
  target_link_options(monero_c-static INTERFACE -Wl,-z,noexecstack)
endif()

# export only the C ABI on macOS too, from the list that the tests check. C symbols start with an underscore there
if (APPLE)
  set(MONERO_C_SYMBOLS ${CMAKE_CURRENT_SOURCE_DIR}/tests/c/abi/monero_c.symbols)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MONERO_C_SYMBOLS})
  file(STRINGS ${MONERO_C_SYMBOLS} monero_c_exports)
  list(TRANSFORM monero_c_exports PREPEND "_")
  list(JOIN monero_c_exports "\n" monero_c_exports)
  set(MONERO_C_EXPORT_LIST ${CMAKE_CURRENT_BINARY_DIR}/exported_symbols.txt)
  file(GENERATE OUTPUT ${MONERO_C_EXPORT_LIST} CONTENT "${monero_c_exports}\n")
  set_property(TARGET monero_c APPEND PROPERTY LINK_DEPENDS ${MONERO_C_EXPORT_LIST})
  target_link_options(monero_c PRIVATE "-Wl,-exported_symbols_list,${MONERO_C_EXPORT_LIST}")
endif()

##############################################################################
# tests
##############################################################################

if (BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
