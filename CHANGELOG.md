# Changelog

## 0.2.0-alpha.1 (not released yet)

The second alpha of the C ABI, version 0.2.0, and of `Monero.Native`. It changes the ABI: a program built with the headers of 0.1.0 has to be
rebuilt, and the callbacks of a wallet are delivered in another way. `monero_utils_get_abi_version()` tells which version a library has, and a
binding should ask for 0.2.x. The JSON is the one of 0.1.0-alpha.1.

### Breaking changes

* **`struct_size`.** `monero_wallet_listener_callbacks` and `monero_daemon_listener_callbacks` start with `uint32_t struct_size`, which the
  program sets to the `sizeof` of its struct. `monero_wallet_listener_create()` and `monero_daemon_listener_create()` copy only that many
  bytes, and fail with `MONERO_ERROR_INVALID_ARGUMENT` if it is 0 or doesn't reach `user_data`. A later version can add callbacks at the end
  of the struct and a program built with these headers keeps working: a callback that its struct doesn't have is NULL.
* **Callbacks of a wallet.** The notifications of a wallet go into a queue of the listener, and a thread of the listener calls the callbacks one
  at a time and in order, with no lock of the wallet held. A callback can call the wallet: `monero_wallet_get_balance()` and the other calls
  that need what a sync holds wait until the sync is done, and the sync doesn't wait for the callback. Until now such a call never returned.
  `monero_wallet_sync()` and `monero_wallet_close()` return after the callbacks they caused, no callback of a listener starts after
  `monero_wallet_remove_listener()` returns, and a callback can remove or free its own listener. A sync progress and a balance that wait are
  replaced by the newer one, more than 100000 waiting new blocks drop the oldest, and an output is never dropped. The listeners of the daemon
  are as they were.

### Added

* `monero_last_error_code()` returns the kind of the last failure of the thread: none, generic, invalid argument, out of memory, RPC or I/O. It
  comes from the type of the exception. The checks of monero-c and the `validate_` functions are invalid arguments, with the same messages.
  What monero-cpp throws as a plain error, a network failure included, is generic.
* `monero_utils_free_secret()` overwrites a string and frees it. Use it for the seed, the private keys and the tx key. Those getters wipe
  the copy that monero-cpp returned, and the passwords, seeds and keys that monero-c copies from its arguments are wiped when the call ends.
* `monero_wallet_wait_for_next_block_timeout()` and `monero_daemon_wait_for_next_block_header_timeout()` wait for a block for at most a number
  of milliseconds, from 1 to 2147483647. A timeout is a success with `out_timed_out` set, so a task that can be cancelled waits again.
* `MONERO_C_STATIC`, to define before the headers when a program links `libmonero_c.a` on Windows, where they declare the functions as
  imported from a DLL otherwise. The target `monero_c-static` of CMake defines it. `monero_c_static_unit_tests` links the archive.
* `monero-native-android`, an Android archive (AAR) with `libmonero_c.so` for `arm64-v8a` and `x86_64`, the headers as a Prefab package and the
  licenses, built by `packaging/android`. The draft release has it with its POM, and the archives `monero-c-<version>-android-arm64` and
  `-android-x86_64` with the library, the headers and the licenses. See [packaging/android](packaging/android/README.md).

The functions go from 291 to 295.

### Changed

* monero-cpp is updated: monero-project v0.18.5.4, and the notes of the txs are written with the cache of the wallet.
* The macOS libraries are built with `openssl@3` and with unbound 1.22.0 built from source, as on Linux, because the unbound of Homebrew is
  built with OpenSSL 4, which Boost 1.85 does not build with.
* `native/Monero.Native.csproj` doesn't build the library while an IDE loads the project. Rider and Visual Studio run a design-time build to
  sync a solution, and that build started the native build, which takes hours. A Build Solution still builds it.

### Known limits

* **Threads.** Don't use one wallet handle from two threads at once. Adding, removing and freeing the same listener from two threads can race,
  so a binding keeps one native listener per wallet. See [Thread Safety](README.md#thread-safety).
* **Cancelling.** `monero_wallet_wait_for_next_block()` and `monero_daemon_wait_for_next_block_header()` can't be stopped, so use the
  functions with a timeout.
* **Errors.** A network failure and most of the validation errors of monero-cpp are `MONERO_ERROR_GENERIC`, since monero-cpp throws one
  type for them. There is no code for "not supported".
* **Secrets.** The JSON text and the parse tree of `monero_wallet_create()`, and the copies that monero-cpp and wallet2 keep, are not wiped.
* **Nodes.** A node that answers with JSON nested thousands of levels deep can crash the client, because monero-cpp parses the answers with a
  recursive parser. Connect to nodes that you trust.
* **Memory.** `monero_wallet_open()` with a wrong password leaks about 200 KB, a query of txs that filters some out leaks about 190 KB
  for 100 txs, and `monero_wallet_get_outputs()` with an `inputQuery` in its `txQuery` leaks about 1.5 KB. All three are in monero-cpp.
* **Transactions.** The ring size of the config is ignored, and the ring is always 16. The multisig test goes as far as the exchange of keys,
  and doesn't sign a multisig tx.
* **Wallets.** A wallet that `monero_wallet_create()` connected to a daemon doesn't see, for tens of seconds, the blocks that the daemon
  gets after that. `monero_wallet_get_multisig_info()` returns whatever the memory had in the threshold and the number of participants of a
  wallet that is not multisig.
* **Platforms.** Linux needs glibc 2.34 or newer. There is no musl, iOS or Linux ARM32, and the Android build is not in the package. On
  Windows, the DLL exports about 1,100 symbols of Boost besides the C ABI, the paths of wallets with characters that are not ASCII are
  not tested, and the build uses the MSYS2 libraries at the version that the repository has the day it builds.

The open items are in [`TODO.md`](TODO.md).

## 0.1.0-alpha.1 (2026-10-16)

The first alpha of the C ABI, version 0.1.0, and of `Monero.Native`, the NuGet package that carries the library for .NET. It is a
prerelease: the C API and the JSON can still change between versions, and `monero_utils_get_abi_version()` tells which one a program has.

### What it has

* 291 functions for the daemon, the full wallet, the wallet that only has the keys, the wallet RPC client, the RPC connections, multisig,
  and the utilities. The headers and the [API documentation](https://libmonero.github.io/monero-c/) describe each one.
* Results with their context, as the bridge of monero-java has them: the txs, the transfers and the outputs come in `{"blocks":[...]}`,
  the calls that make txs return `{"txSets":[...]}`, and the listeners get the output in its tx and its block. See [Results with context](README.md#results-with-context).
* `Monero.Native 0.1.0-alpha.1` for `linux-x64`, `linux-arm64`, `win-x64`, `osx-x64` and `osx-arm64`, with the third-party notices. The
  platforms and what each needs are in [the README of the package](packaging/dotnet-core/README.md).
* Unit tests on every platform, integration tests on a regtest node and a `monero-wallet-rpc` server on Linux x64, the C# tests on
  Ubuntu 22.04 and 24.04, Debian 12 (x86_64 and arm64), Windows and macOS (arm64 and x86_64), and the sanitizers on Linux. Some of the
  unit tests only look for crashes and leaks: every function is called with a NULL in each pointer, with the largest numbers and with long
  texts, the functions that read JSON, hex or binary get mutations of valid inputs, and the functions run from several threads.

### Known limits

* **Threads.** Don't use one wallet handle from two threads at once. Adding, removing and freeing the same listener from two threads can race,
  so a binding keeps one native listener per wallet. A wallet callback runs under the lock of the wallet, so `monero_wallet_get_balance()`,
  `monero_wallet_get_unlocked_balance()`, `monero_wallet_get_txs()`, `monero_wallet_get_outputs()` and `monero_wallet_get_accounts()` called
  from it never return. See [Thread Safety](README.md#thread-safety).
* **Cancelling.** `monero_wallet_wait_for_next_block()` can't be stopped until a block comes.
* **Errors.** A failure is `MONERO_ERROR` and a message, with no code, and the callback structs have no size field. Both will change the ABI before 1.0.
* **Secrets.** The seed and the keys that the library copies are not wiped from its memory when they are released.
* **Nodes.** A node that answers with JSON nested thousands of levels deep can crash the client, because monero-cpp parses the answers with a
  recursive parser. Connect to nodes that you trust.
* **Memory.** `monero_wallet_open()` with a wrong password leaks about 200 KB, a query of txs that filters some out leaks about 190 KB
  for 100 txs, and `monero_wallet_get_outputs()` with an `inputQuery` in its `txQuery` leaks about 1.5 KB. All three are in monero-cpp.
* **Transactions.** The ring size of the config is ignored, and the ring is always 16. The multisig test goes as far as the exchange of keys,
  and doesn't sign a multisig tx.
* **Wallets.** A wallet that `monero_wallet_create()` connected to a daemon doesn't see, for tens of seconds, the blocks that the daemon
  gets after that. `monero_wallet_get_multisig_info()` returns whatever the memory had in the threshold and the number of participants of a
  wallet that is not multisig.
* **Platforms.** Linux needs glibc 2.34 or newer. There is no musl, Android, iOS or Linux ARM32. On Windows, the paths of wallets with
  characters that are not ASCII are not tested, and the Windows build uses the MSYS2 libraries at the version that the repository has the
  day it builds.

The open items are in [`TODO.md`](TODO.md).
