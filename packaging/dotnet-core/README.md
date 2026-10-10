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
* A C `bool` is one byte. Declare `bool` parameters and return values with `UnmanagedType.U1`, because the default `bool` of the marshaller is four bytes.
* A string or buffer that a function returns in an `out_*` parameter must be released with `monero_utils_free()`, not with `Marshal.FreeHGlobal()` or `Marshal.FreeCoTaskMem()`. Declare such a parameter as `IntPtr`: a `string` output of `DllImport` or `LibraryImport` is freed by the marshaller with `FreeCoTaskMem()`, which is not the allocator of the library on Windows.
* Call `monero_utils_get_abi_version()` after loading the library and compare the major and minor versions with the ones your code was written for. While the major version is 0, a different minor version can break callers. The patch version never changes the ABI.
* `monero_last_error()` returns the error of the calling thread. Read it right after the failing call, before any `await`, because the continuation can run on another thread.
* A callback is a function pointer. Get it with `Marshal.GetFunctionPointerForDelegate()` from a delegate declared with `[UnmanagedFunctionPointer(CallingConvention.Cdecl)]`, and keep the delegate referenced until the listener is freed. Callbacks run on threads of monero-c, and an exception that leaves one ends the process. The `percent_done` of a sync goes from 0 to 1.
* A wallet callback runs while the sync holds the lock of the wallet, so `monero_wallet_get_balance()`, `monero_wallet_get_unlocked_balance()`, `monero_wallet_get_txs()`, `monero_wallet_get_outputs()` and `monero_wallet_get_accounts()` called on that wallet from it never return. Copy the arguments and make those calls from another thread.
* A call can succeed with a NULL output when there is no result, for example a block that the daemon doesn't have. The docs of the function say so, and the pointer has to be checked before the string is read.
* An optional parameter is a pointer, and NULL means none. For a `ref ulong` parameter, pass `ref Unsafe.NullRef<ulong>()`.
* Don't use one handle from two threads at once. A finalizer can call `monero_wallet_free()` on a wallet that still syncs. To save a wallet, call `monero_wallet_close(wallet, true)` before freeing it.
* A JSON argument can't nest deeper than 64 levels, a mnemonic can't be longer than 4096 bytes, and `monero_daemon_get_blocks_by_range()` takes at most 100000 blocks.
* On Linux and macOS the library sets `SSL_CERT_FILE` and `SSL_CERT_DIR` in your process when it is loaded, to the CA certificates of the system, if neither is set and OpenSSL doesn't find them by itself. A connection to an `https` daemon with `sslVerify` on needs them.

## Licenses

monero-c is MIT licensed. The package includes the licenses of the native code it contains in `licenses/`, listed in `THIRD-PARTY-NOTICES.md`. libusb is LGPL-2.1 and ships as a separate shared library next to `monero_c`, with its source archive in `licenses/source/`.

See the [.NET build guide](https://github.com/libmonero/monero-c/blob/main/docs/dotnet.md) to build the package.
