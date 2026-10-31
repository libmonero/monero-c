# CMake, pkg-config and Linux packages

The install rules of monero-c, for C and C++ programs that use the shared library from an install, and the settings of CPack for `.deb` and `.rpm` packages.

`cmake --install` puts these in the prefix:

| Path | What |
| --- | --- |
| `lib/libmonero_c.so` (`monero_c.dll` in `bin/` on Windows) | The shared library |
| `include/monero_c/` | The public headers, listed in [`headers.txt`](../common/headers.txt) |
| `lib/cmake/monero_c/` | `find_package(monero_c)` |
| `lib/pkgconfig/monero_c.pc` | `pkg-config monero_c` |
| `share/doc/monero_c/` | `LICENSE`, `THIRD-PARTY-NOTICES.md` and the license texts of `MONERO_C_LICENSES_DIR`, if it is set |

The paths follow `GNUInstallDirs`, so on Debian a prefix of `/usr` gives `lib/<multiarch>/`. The static library is not installed: it holds only the objects of monero-c, and a program that links it needs the archives of monero-cpp, monero-project and the other dependencies too.

The rules are on by default and `-DMONERO_C_INSTALL=OFF` leaves them out.

## Use

```sh
cmake -S . -B build -DMONERO_C_STATIC_DEPENDENCIES=ON ...   # as in the README of the repository
cmake --build build
cmake --install build --prefix /opt/monero-c
```

With CMake:

```cmake
find_package(monero_c 0.2 REQUIRED)
target_link_libraries(app PRIVATE monero_c::monero_c)
```

The version of the package is the ABI version of `monero_utils_get_abi_version()`. The release is 0.x, so `find_package(monero_c 0.2)` accepts 0.2.x and refuses 0.3. A program that adds the repository with `add_subdirectory()` links the same name, `monero_c::monero_c`.

With pkg-config, which finds the prefix from the place of the `.pc` file, so an install can be moved:

```sh
cc app.c $(pkg-config --cflags --libs monero_c)
```

Include the header as `#include "monero_c.h"`. The headers include each other by their path under `include/monero_c/`, which is why the include path is that folder.

[`test/`](test) is a program that does both, and the Linux build runs it on the install of every build.

## Packages

Configure with the prefix and the library directory of the system, build, then run CPack in the build folder:

```sh
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr ...
cmake --build build
(cd build && cpack -G DEB)   # monero-c_<version>_<arch>.deb
(cd build && cpack -G RPM)   # monero-c-<version>.<arch>.rpm
```

A version with a prerelease, such as `0.2.0-alpha.1`, is `0.2.0~alpha.1` in the package, which sorts it before `0.2.0`. A build with `MONERO_C_STATIC_DEPENDENCIES` links libusb as a shared library and the rest statically, so the `.deb` says that it depends on `libc6`, `libstdc++6`, `libgcc-s1` and `libusb-1.0-0`; another build gets the dependencies from the libraries that it links. Pass `-DMONERO_C_LICENSES_DIR=<folder>` with the license texts of the statically linked dependencies, as the CI does, to put them in the package.

The Build workflow makes the `.deb` for x86-64 and ARM64 on Debian 12 and adds them to the draft release. The `.rpm` settings are not run in the CI.

## Limits

* The library has the soname `libmonero_c.so`, without a version. The ABI can still change between minor versions, so a distribution should not ship it as a shared library for other packages before 1.0, when the soname can carry the major version.
* The packages are a single package with the library and the files for developers.
