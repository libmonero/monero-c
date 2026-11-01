# MoneroC.xcframework

The macOS libraries of monero-c as an XCFramework, for Swift and C programs on macOS. It has `libmonero_c.dylib` for x86-64 and Apple silicon in one universal file, the headers, a module map that makes `import MoneroC` work in Swift, and libusb next to the library.

This is an alpha version. The C API can still change between versions.

## Contents

The zip `MoneroC-<version>.xcframework.zip` has `MoneroC.xcframework/`:

| Path | What |
| --- | --- |
| `macos-arm64_x86_64/libmonero_c.dylib` | The library, universal. macOS 11.0 or newer |
| `macos-arm64_x86_64/libusb-1.0.0.dylib` | libusb, which the library loads from its own folder (`@loader_path`) |
| `macos-arm64_x86_64/Headers/` | The public headers and `module.modulemap` (module `MoneroC`) |
| `LICENSE`, `THIRD-PARTY-NOTICES.md`, `licenses/` | The licenses. libusb is LGPL-2.1 and its source archive is in `licenses/source/` |

Only macOS is in it. There is no iOS slice: the library links libusb as a shared library, and an iOS app cannot load one outside a framework, so iOS needs another build of the library.

## Use

In a Swift package, add the zip as a binary target. The checksum is the SHA-256 of the zip, which `SHA256SUMS.txt` of the release lists, and `swift package compute-checksum` gives the same:

```swift
.binaryTarget(
    name: "MoneroC",
    url: "https://github.com/libmonero/monero-c/releases/download/v<version>/MoneroC-<version>.xcframework.zip",
    checksum: "<sha256 of the zip>"
)
```

```swift
import MoneroC

var major: UInt32 = 0, minor: UInt32 = 0, patch: UInt32 = 0
if monero_utils_get_abi_version(&major, &minor, &patch) == MONERO_OK {
    print("monero_c \(major).\(minor).\(patch)")
}
```

Call `monero_utils_get_abi_version()` after the program starts and compare the major and minor versions with the ones your code was written for. While the major version is 0, a different minor version can break callers.

A program that is not a Swift package links the library in the folder of the slice: `-I <slice>/Headers -L <slice> -lmonero_c -Wl,-rpath,<slice>`. The check of the CI does it with `cc` and with `swiftc`, in [`smoke/`](smoke).

## Limits

* A dynamic library is not copied into an app bundle by Xcode as a framework is. A Mac app has to copy `libmonero_c.dylib` and `libusb-1.0.0.dylib` into the folder of its bundle where its executable finds them (`@rpath`), and sign both. This is not tested.
* The libraries are signed ad hoc, not with a Developer ID, and they are not notarized.

## Notes for callers

* Strings are UTF-8. A string or buffer that a function returns in an `out_*` parameter is released with `monero_utils_free()`, and the seed, the private keys and the tx key with `monero_utils_free_secret()`.
* `monero_last_error()` and `monero_last_error_code()` are of the calling thread. Read them right after the failing call, before an `await`, because the continuation can run on another thread.
* Callbacks run on threads of monero-c. An exception or a Swift error that leaves a callback ends the process.
* Don't use one handle from two threads at once, and keep one native listener per wallet. See [Thread Safety](../../README.md#thread-safety) and the headers.
* The library sets `SSL_CERT_FILE` and `SSL_CERT_DIR` in your process when it is loaded, to the CA certificates of the system, if neither is set and OpenSSL doesn't find them by itself.

## Build

The Build workflow makes the zip from the two macOS builds, and the release workflow adds it to the draft release. On a Mac, with the builds of both architectures staged as the workflow does:

```sh
packaging/apple/build-xcframework.sh <version> packaging/apple/packages <x86_64 folder> <arm64 folder>
packaging/apple/check-xcframework.sh packaging/apple/packages/MoneroC-<version>.xcframework.zip 0.2.1
```

The version is the one of [`Version.props`](../dotnet-core/Version.props), and the last argument is the ABI version of the header. The licenses and the source archive of libusb must be in `packaging/common/licenses/`: the CI takes them from the Linux build.

## Licenses

monero-c is MIT licensed. The XCFramework includes the licenses of the native code it contains, listed in `THIRD-PARTY-NOTICES.md`.
