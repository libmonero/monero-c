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

Unit tests need no daemon. The integration test loads the package and calls `monero_utils_get_ring_size()`.

## Package contents

CI packages `linux-x64`, `linux-arm64`, `win-x64`, `osx-x64` and `osx-arm64`, and runs the managed tests on Ubuntu 24.04 and Debian 12 (x64 and arm64), Windows x64 and both macOS architectures. The [package README](../packaging/dotnet-core/README.md) lists the minimum version of each platform. Dependencies are linked statically except libusb, which is bundled as a shared library with its LGPL license and source archive. The Windows build disables Trezor support, so `monero_c.dll` does not import libusb. libusb is built with `--disable-udev`, so it does not need `libudev`.

For native CMake support in Rider, open the repository's [`CMakeLists.txt`](../CMakeLists.txt).

## Publish a prerelease

The version of the package is in [`Version.props`](../packaging/dotnet-core/Version.props), and the package project and the test projects read it from there. To release:

1. Set `MoneroNativeVersion` in `Version.props` and merge the change.
2. Tag the commit as `v` plus the version, for example `v0.1.0-alpha.2`. CI fails if the tag and the version differ.
3. After CI passes for the tag, download the `monero-native-nuget` artifact of that run and push it to NuGet.org:

```sh
dotnet nuget push Monero.Native.<version>.nupkg \
  --api-key "$NUGET_API_KEY" \
  --source https://api.nuget.org/v3/index.json
```

Create the API key on NuGet.org and keep it out of the repository. Each package version can only be published once.
