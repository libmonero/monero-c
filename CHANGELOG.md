# Changelog

## 0.1.1-alpha.1 (not released yet)

A fix for building `Monero.Native` from the source in an IDE. The functions and the JSON are the ones of 0.1.0-alpha.1, and the ABI version
is 0.1.1.

* `native/Monero.Native.csproj` doesn't build the library while an IDE loads the project. Rider and Visual Studio run a design-time build to
  sync a solution, and that build started the native build, which takes hours. A Build Solution still builds it.

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
