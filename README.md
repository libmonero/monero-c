# Monero C ABI

[![Build](https://github.com/libmonero/monero-c/actions/workflows/build.yml/badge.svg)](https://github.com/libmonero/monero-c/actions/workflows/build.yml)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/b86089ef2a1c46769d6931d0057a8f6c)](https://app.codacy.com/gh/libmonero/monero-c/dashboard?utm_source=gh&utm_medium=referral&utm_content=&utm_campaign=Badge_grade)

> [!WARNING]
>
> monero-c is currently under maintenance and unfunded, expect bugs and breaking changes.

A C ABI for creating Monero applications using RPC and FFI bindings to [monero v0.18.5.3 'Fluorine Fermi'](https://github.com/monero-project/monero/tree/v0.18.5.3).

* Supports wallet and daemon RPC clients.
* Supports client-side wallets using native bindings.
* Supports multisig, view-only, and offline wallets.
* Wallet types are interchangeable behind a common interface.
* Uses a clearly defined [data model and API specification](https://libmonero.github.io/monero-c/monero__c__utils_8h.html) intended to be intuitive and robust.
* Query wallet transactions, transfers, and outputs by their properties.
* Fetch and process binary data from the daemon (e.g. raw blocks).
* Receive notifications when blocks are added to the chain or when wallets sync, send, or receive.

## Architecture

<p align="center">
	<img width="85%" height="auto" src="docs/architecture.png"/><br>
	<i>Build
     applications using FFI bindings to <a href="https://github.com/monero-project/monero">monero-project/monero</a>.  Wallet implementations are interchangeable by conforming to a common interface, <a href="https://woodser.github.io/monero-cpp/doxygen/classmonero_1_1monero__wallet.html">monero_wallet.h</a>.</i>
</p>

## Sample code

```c
#include <stdio.h>
#include "monero_c.h"

if (!monero_utils_is_valid_address(address, MONERO_UTILS_NETWORK_MAINNET)) {
  // invalid
}

uint64_t atomic_units;
if (monero_utils_xmr_to_atomic_units(0.25, &atomic_units) != MONERO_OK) {
  fprintf(stderr, "%s\n", monero_last_error());
}
// atomic_units == 250000000000

char* json = NULL;
if (monero_utils_get_integrated_address(MONERO_UTILS_NETWORK_STAGENET, address, "", &json) == MONERO_OK) {
  printf("%s\n", json);
  monero_utils_free(json);
}
```

## Documentation

* [API documentation](https://libmonero.github.io/monero-c/)

## Building monero-c from source

### Linux and macOS

1. Clone the project repository:
   ```bash
   git clone --recurse-submodules https://github.com/libmonero/monero-c.git
   ```
2. Build monero-project, monero-cpp and monero-c. Extra arguments are passed to cmake, e.g. `-DBUILD_TESTS=ON`:
   ```bash
   cd monero-c
   ./bin/build_libmonero_c.sh -DBUILD_TESTS=ON
   ```

Flags are cached in `./build/CMakeCache.txt`, so pass them on every build to change them. To sync submodules after pulling new changes, run `./bin/update_submodules.sh`.

### Windows

1. Download and install [MSYS2](https://www.msys2.org/).
2. Press the Windows button and launch `MSYS2 MINGW64`.
3. Update packages: `pacman -Syu` and confirm at prompts.
4. Relaunch MSYS2 (if necessary) and install dependencies:
   ```
   pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-openssl mingw-w64-x86_64-zeromq mingw-w64-x86_64-libsodium mingw-w64-x86_64-hidapi mingw-w64-x86_64-unbound mingw-w64-x86_64-protobuf mingw-w64-x86_64-libusb mingw-w64-x86_64-expat mingw-w64-x86_64-ntldd git make gettext base-devel wget
   wget https://repo.msys2.org/mingw/mingw64/mingw-w64-x86_64-icu-75.1-2-any.pkg.tar.zst
   pacman -U mingw-w64-x86_64-icu-75.1-2-any.pkg.tar.zst
   wget https://repo.msys2.org/mingw/mingw64/mingw-w64-x86_64-boost-1.87.0-3-any.pkg.tar.zst
   pacman -U mingw-w64-x86_64-boost-1.87.0-3-any.pkg.tar.zst
   ```
5. Clone the repo: `git clone --recurse-submodules https://github.com/libmonero/monero-c.git`
6. Build monero-project, monero-cpp and monero-c. Extra arguments are passed to cmake, e.g. `-DBUILD_TESTS=ON`:
   ```
   cd monero-c
   ./bin/build_libmonero_c.sh -DBUILD_TESTS=ON
   ```

## Running tests

```
ctest --test-dir build --output-on-failure
```

The unit tests in [tests/unit](tests/unit/) need nothing else. The integration tests in [tests/integration](tests/integration/) call a regtest `monerod`, so they are skipped unless `MONERO_C_TEST_DAEMON_URI` points at one. The compose file starts the same node image that monero-python uses:

```
docker compose -f tests/integration/docker-compose.yml up -d
MONERO_C_TEST_DAEMON_URI=http://127.0.0.1:18081 ctest --test-dir build --output-on-failure
docker compose -f tests/integration/docker-compose.yml down -v
```

All the tests include only the public header and call only exported functions, the same way a real FFI consumer would.

## Memory Ownership

Any `out_*` parameter that receives a string or byte buffer (`char**`, `uint8_t**`) is heap-allocated by monero_c and must be freed by the caller with `monero_utils_free()`, as shown above -- the sample's `monero_utils_free(json)` call is not optional. Fixed-size out-params (e.g. `uint8_t out_payment_id[32]`) write into a buffer the caller already owns, so there's nothing to free. See [src/utils/monero_c_utils.h](src/utils/monero_c_utils.h) for the one exception (`monero_last_error()`). Handles, such as the `monero_daemon` returned by `monero_daemon_connect()`, are released with their matching `*_free()` function.

## Thread Safety

> [!WARNING]
>
> Thread safety is incomplete. Some concurrent uses are still unsafe.

What holds today:

- monero-cpp serializes calls on one `monero_daemon` handle, so they don't run in parallel.
- monero-cpp locks a `monero_rpc_connection`, so one handle can be shared by several threads and by the daemons and RPC wallets created from it.
- Listener callbacks run on a thread owned by monero-cpp. Inside a callback, `monero_daemon_remove_listener()` and `monero_daemon_remove_listeners()` are safe.
- Different listeners on one daemon can be managed from different threads.
- `monero_daemon_wait_for_next_block_header()` blocks only its calling thread, and other calls on the handle run meanwhile.
- `monero_last_error()` is per thread: it returns the last error of the calling thread.

## Related projects

* [monero-cpp](https://github.com/woodser/monero-cpp)
* [monero-java](https://github.com/woodser/monero-java)
* [monero-ts](https://github.com/woodser/monero-ts)
* [monero-python](https://github.com/everoddandeven/monero-python)
* [monero-csharp](https://github.com/libmonero/monero-csharp)

## License

This project is licensed under MIT.

## Donations

If this library has been valuable to you, please consider donating to support its continued development. 🙏

<p align="center">
  <code>XMR</code><br>
	<img src="donate.png" style="margin-top: 5px" width="115" height="115"/><br>
	<code>852punAC8Ub7tLqrA2L72p3VVL9ZdjSAxUgryqcoiFSmLZnE7UJiHENCRYZboz6bETEmznh9tBSkGGWiPLWdigVm8AYgFcJ</code>
</p>