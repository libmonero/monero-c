/// The native runtime of monero-c.
///
/// This package has no bindings. Its build hook gives the build the library `libmonero_c`, as the code asset [moneroCAssetId], and a
/// binding declares its functions with `@Native(assetId: moneroCAssetId)`.
library;

import 'dart:ffi';

import 'package:ffi/ffi.dart';

/// The id of the code asset of libmonero_c, for the `assetId` of `@Native`.
const String moneroCAssetId = 'package:monero_native/monero_c.dart';

@Native<Int32 Function(Pointer<Uint32>, Pointer<Uint32>, Pointer<Uint32>)>(
  assetId: moneroCAssetId,
  symbol: 'monero_utils_get_abi_version',
)
external int _getAbiVersion(Pointer<Uint32> major, Pointer<Uint32> minor, Pointer<Uint32> patch);

/// Get the ABI version of the library that the build bundled.
///
/// While the major version is 0, a different minor version can break callers, so a binding compares it with the one it was written for.
({int major, int minor, int patch}) abiVersion() {
  return using((arena) {
    final major = arena<Uint32>();
    final minor = arena<Uint32>();
    final patch = arena<Uint32>();
    if (_getAbiVersion(major, minor, patch) != 0) {
      throw StateError('monero_utils_get_abi_version() failed');
    }
    return (major: major.value, minor: minor.value, patch: patch.value);
  });
}
