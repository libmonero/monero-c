import 'dart:io';

import 'package:monero_native/monero_native.dart';
import 'package:test/test.dart';

void main() {
  test('the bundled library has the ABI version of the header', () {
    final version = abiVersion();
    final actual = '${version.major}.${version.minor}.${version.patch}';
    // the CI sets it from src/utils/monero_c_utils.h
    final expected = Platform.environment['MONERO_C_ABI_VERSION'];
    if (expected != null) {
      expect(actual, expected);
    } else {
      expect(version.major, 0);
    }
  });
}
