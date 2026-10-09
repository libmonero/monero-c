# Third-party notices

`Monero.Native` contains native code from this project and its dependencies.
The license texts listed below are included in the package at the indicated
paths. The applicable set of linked components can differ by RID and build
configuration.

| Component | License | License text |
| --- | --- | --- |
| monero-c | MIT | `LICENSE` at the package root |
| monero-cpp | MIT | `licenses/monero-cpp-MIT.txt` |
| Monero Project | BSD-3-Clause, with MIT-licensed portions | `licenses/monero-project-BSD-3-Clause.txt` |
| Monero epee | BSD-3-Clause | `licenses/epee-BSD-3-Clause.txt` |
| RandomX | BSD-3-Clause | `licenses/randomx-BSD-3-Clause.txt` |
| LMDB | OLDAP-2.8 | `licenses/lmdb-OLDAP-2.8.txt` |
| RapidJSON | MIT | `licenses/rapidjson-MIT.txt` |
| Easylogging++ | MIT | `licenses/easyloggingpp-MIT.txt` |
| QR Code generator by Project Nayuki | MIT | `licenses/qrcodegen-MIT.txt` |
| OpenAES | BSD-3-Clause | `licenses/openaes-BSD-3-Clause.txt` |
| Boost | BSL-1.0 | `licenses/boost-BSL-1.0.txt` |
| OpenSSL | Apache-2.0 | `licenses/openssl-Apache-2.0.txt` |
| Expat | MIT | `licenses/expat-MIT.txt` |
| Unbound | BSD-3-Clause | `licenses/unbound-BSD-3-Clause.txt` |
| libsodium | ISC | `licenses/libsodium-ISC.txt` |
| Protocol Buffers | BSD-3-Clause | `licenses/protobuf-BSD-3-Clause.txt` |
| HIDAPI (BSD-style license selected) | BSD-3-Clause | `licenses/hidapi-BSD-3-Clause.txt` |
| libusb | LGPL-2.1-or-later | `licenses/libusb-LGPL-2.1.txt` |
| libevent, if linked | BSD-3-Clause | `licenses/libevent-BSD-3-Clause.txt` |

The build disables Trezor support (`USE_DEVICE_TREZOR=OFF`), so the LGPL-3.0
Trezor common code is not intended to be part of these binaries.

## LGPL component distribution

Each supported RID links to a separately packaged shared libusb runtime, next
to `libmonero_c`, so the native loader can locate it from the package directory.
The package also includes the exact `libusb-1.0.27` source archive at
`licenses/source/libusb-1.0.27.tar.bz2` and the corresponding LGPL license
text. The CI checks that every RID has the shared runtime and that libusb is
dynamically linked. Recipients can replace the shared library with a compatible
modified build.

This inventory is based on the checked-in CMake link configuration and CI
dependency sources. The package workflow stages these license texts from the
Linux x64 build and verifies the libusb runtime assets for every RID. Review
the complete dependency closure and distribution obligations before
publication; this file is not legal advice.
