# .NET bindings

The solution includes C# unit and integration tests and the native ABI sources for browsing in Rider. The native NuGet package is built from [`monero.pkgproj`](../packaging/dotnet-core/monero.pkgproj).

## Build and test

Build shared libusb 1.0.27 before configuring `monero-c`. Stage the native library and libusb under `runtimes/<rid>/native/`, then pack and test:

```sh
cmake -S . -B build-dotnet -DBUILD_TESTS=ON -DMONERO_C_STATIC_DEPENDENCIES=ON -DMONERO_C_LIBUSB_SHARED_LIBRARY=/usr/local/lib/libusb-1.0.so
cmake --build build-dotnet -j1
mkdir -p packaging/dotnet-core/runtimes/linux-x64/native packaging/dotnet-core/packages
cp build-dotnet/libmonero_c.so /usr/local/lib/libusb-1.0.so.0 packaging/dotnet-core/runtimes/linux-x64/native/
dotnet pack packaging/dotnet-core/monero.pkgproj --configuration Release
dotnet test monero-c.sln --configuration Release
```

Unit tests need no daemon. The integration test loads the package and calls `monero_utils_get_ring_size()`. NuGet keeps a package by id and version in `~/.nuget/packages`, so after rebuilding the library under the same version, delete `~/.nuget/packages/monero.native/<version>` before `dotnet test`, or the tests run on the old library.

## Package contents

The Build workflow packages `linux-x64`, `linux-arm64`, `win-x64`, `osx-x64` and `osx-arm64`, and the Tests workflow runs the managed tests on that package on Ubuntu 22.04, Ubuntu 24.04 and Debian 12 (x64 and arm64), Windows x64 and both macOS architectures. The [package README](../packaging/dotnet-core/README.md) lists the minimum version of each platform. Dependencies are linked statically except libusb, which is bundled as a shared library with its LGPL license and source archive. The Windows build disables Trezor support, so `monero_c.dll` does not import libusb. libusb is built with `--disable-udev`, so it does not need `libudev`.

For native CMake support in Rider, open the repository's [`CMakeLists.txt`](../CMakeLists.txt).

## Publish a release

[`RELEASING.md`](../RELEASING.md) describes it: the version in [`Version.props`](../packaging/dotnet-core/Version.props), the tag, the draft GitHub release that CI makes with the package, and the push of the package to nuget.org and to GitHub Packages when the draft is published. No API key is created or stored.

## Install from GitHub Packages

Publishing a draft release also pushes `Monero.Native` to the GitHub Packages registry of the `libmonero` organization (see [`publish-nuget.yml`](../.github/workflows/publish-nuget.yml)). GitHub Packages asks for a login even for public packages, so create a personal access token (classic) with the `read:packages` scope, add the source once and install the package:

```sh
dotnet nuget add source https://nuget.pkg.github.com/libmonero/index.json \
  --name github-libmonero \
  --username <github-user> \
  --password <token> \
  --store-password-in-clear-text

dotnet add package Monero.Native --version <version>
```

`--store-password-in-clear-text` is needed on Linux and macOS, where NuGet cannot encrypt the password.
