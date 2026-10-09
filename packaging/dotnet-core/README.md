# Monero.Native

The native runtime package of the [monero-c](https://github.com/libmonero/monero-c) .NET binding. It holds the monero-c C library and a shared libusb for each supported platform, and no managed assembly. Load it with `DllImport("monero_c")`, and .NET picks the library of the current runtime identifier from `runtimes/<rid>/native/`.

This is an alpha version. The C API can still change between versions.

## Supported platforms

| Runtime identifier | Tested on | Needs |
| --- | --- | --- |
| `linux-x64` | Debian 12, Ubuntu 24.04 | glibc 2.36 and libstdc++ 3.4.29 (GCC 11) or newer |
| `linux-arm64` | Debian 12, Ubuntu 24.04 | glibc 2.36 and libstdc++ 3.4.29 (GCC 11) or newer |
| `win-x64` | GitHub `windows-latest` | Windows 8 or newer, the DLL imports the `api-ms-win-core-synch-l1-2-0` API set |
| `osx-x64` | macOS 15 | macOS 11.0 or newer |
| `osx-arm64` | macOS 15 | macOS 11.0 or newer |

Not supported: Linux distributions with an older glibc (Ubuntu 22.04 and RHEL 9 have 2.35 and 2.34), musl (Alpine), 32-bit systems, Windows ARM64, Android and iOS.

## Notes for callers

* Strings are UTF-8. On Windows, `CharSet.Ansi` uses the system code page, so declare string parameters as `UnmanagedType.LPUTF8Str`.
* A string or buffer that a function returns in an `out_*` parameter must be released with `monero_utils_free()`, not with `Marshal.FreeHGlobal()` or `Marshal.FreeCoTaskMem()`.
* `monero_last_error()` returns the error of the calling thread.

## Licenses

monero-c is MIT licensed. The package includes the licenses of the native code it contains in `licenses/`, listed in `THIRD-PARTY-NOTICES.md`. libusb is LGPL-2.1 and ships as a separate shared library next to `monero_c`, with its source archive in `licenses/source/`.

See the [.NET build guide](https://github.com/libmonero/monero-c/blob/main/docs/dotnet.md) to build the package.
