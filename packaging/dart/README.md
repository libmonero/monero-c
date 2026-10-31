# monero_native

The native runtime package of monero-c for Dart and Flutter. Its build hook bundles `libmonero_c` for the platform that a program is built for, and the package has no bindings. A binding declares the functions of `monero_c.h` with `@Native` and the asset id `moneroCAssetId`, or generates them with `ffigen` from the header, and depends on this package for the library.

This is an alpha version. The C API can still change between versions.

## Supported platforms

| Target | Library |
| --- | --- |
| Linux x64, arm64 | `libmonero_c.so` and `libusb-1.0.so.0`. glibc 2.34 and libstdc++ 3.4.29 (GCC 11) or newer |
| macOS x64, arm64 | `libmonero_c.dylib` and `libusb-1.0.0.dylib`. macOS 11.0 or newer |
| Windows x64 | `libmonero_c.dll`. Windows 8 or newer |
| Android arm64, x64 | `libmonero_c.so`. Android 9 (API 28) or newer |

Not supported: iOS, older glibc, musl (Alpine), 32-bit systems and Windows ARM64. The hook stops the build with a message for a target that has no library.

## How it works

The package does not carry the libraries. The hook downloads the archive of the target from the GitHub release of the same version (`monero-c-<version>-<platform>.tar.gz`, `.zip` for Windows), checks its SHA-256, extracts the library and the files next to it, and hands them to the build as code assets. A download is kept in the shared output folder of the hooks, so it happens once for each version and target.

The package that is published has `checksums.txt` with the SHA-256 of every archive, taken from the release that made it. A copy from git has none, and the hook reads `SHA256SUMS.txt` of the release instead.

The build needs network access the first time, and the release of the version of the package must be public.

## Use

```yaml
dependencies:
  monero_native: ^0.2.0-alpha.1
```

```dart
import 'package:monero_native/monero_native.dart';

void main() {
  final version = abiVersion();
  print('monero_c ${version.major}.${version.minor}.${version.patch}');
}
```

A binding declares its functions with the asset id:

```dart
@Native<Int32 Function(Pointer<Uint32>, Pointer<Uint32>, Pointer<Uint32>)>(
  assetId: moneroCAssetId,
  symbol: 'monero_utils_get_abi_version',
)
external int monero_utils_get_abi_version(Pointer<Uint32> major, Pointer<Uint32> minor, Pointer<Uint32> patch);
```

Call `abiVersion()` after the program starts and compare the major and minor versions with the ones your code was written for. While the major version is 0, a different minor version can break callers.

## Settings of the hook

The program, or a test, that uses the package can set these in the `pubspec.yaml` of its root package:

```yaml
hooks:
  user_defines:
    monero_native:
      # use the library of this folder, which has the files of the target, and download nothing
      library_dir: ../monero-c/build
      # download from another place than https://github.com/libmonero/monero-c/releases/download, which has v<version>/ below it
      release_url: https://example.org/mirror
```

The hooks are not run again after the settings change, so remove `.dart_tool` when you change them.

## Notes for callers

* Strings are UTF-8. A string or buffer that a function returns in an `out_*` parameter is released with `monero_utils_free()`, and the seed, the private keys and the tx key with `monero_utils_free_secret()`.
* `monero_last_error()` and `monero_last_error_code()` are of the calling thread, so read them right after the failing call, before an `await`. A callback of a wallet or a daemon runs on a thread of monero-c, so a Dart callback has to be a `NativeCallable.listener`, which Dart runs on its isolate, and an exception that leaves a native callback ends the process.
* Don't use one handle from two threads at once, and keep one native listener per wallet. See [Thread Safety](../../README.md#thread-safety) and the headers.
* On Linux, macOS and Android the library sets `SSL_CERT_FILE` and `SSL_CERT_DIR` in your process when it is loaded, if neither is set and OpenSSL doesn't find them by itself.

## Build

The Dart workflow checks the package on pull requests: `dart analyze`, `dart pub publish --dry-run` on the package as the release makes it, and `dart test` on Linux, macOS and Windows with the library downloaded from a local server and taken from a folder. The release workflow makes the package after the archives, so that `checksums.txt` has their SHA-256:

```sh
packaging/common/pack-archive.sh <version> <platform> <build folder> <licenses folder> <release folder>   # for each platform
packaging/dart/build.sh <version> <release folder>
```

`version` in `pubspec.yaml` has to be the version of [`Version.props`](../dotnet-core/Version.props), and `build.sh` fails if it is not.

## Licenses

monero-c is MIT licensed. The archives that the hook downloads include the licenses of the native code in `licenses/`, listed in [`THIRD-PARTY-NOTICES.md`](../common/THIRD-PARTY-NOTICES.md). libusb is LGPL-2.1 and ships as a separate shared library next to `libmonero_c`.
