# monero-native-android

The native runtime package of monero-c for Android. It holds `libmonero_c.so` for each ABI and the C headers, and no Java code. Load the library with `System.loadLibrary("monero_c")`, with JNA (`Native.load("monero_c", ...)`) or from your own native code.

This is an alpha version. The C API can still change between versions.

## Contents

| Path in the archive | What |
| --- | --- |
| `jni/arm64-v8a/libmonero_c.so`, `jni/x86_64/libmonero_c.so` | The libraries. The Android Gradle plugin packs them into an app |
| `prefab/` | The headers and the libraries as a [Prefab](https://developer.android.com/build/native-dependencies) package `monero_c`, for the apps that build native code against it |
| `META-INF/monero-native/` in `classes.jar` | The licenses, which the plugin copies into the APK |
| `LICENSE`, `THIRD-PARTY-NOTICES.md`, `licenses/` | The same licenses, for a person who unpacks the archive |

The ABIs are `arm64-v8a` for devices and `x86_64` for the emulator. 32-bit ABIs (`armeabi-v7a`, `x86`) are not supported.

## Requirements

* Android 9 (API 28) or newer. The manifest of the archive sets `minSdkVersion` to 28, so the manifest merger refuses an app with a lower one.
* The libraries link their dependencies statically and the C++ library of the NDK too (`c++_static`). They need only `libc`, `libm`, `libdl` and `liblog` of the system.
* Every segment is aligned to 16 KB, so the libraries load on devices with 16 KB pages, which Google Play requires of apps that target Android 15.

## Use

The package is not on a registry yet. Take `monero-native-android-<version>.aar` and its POM from the files of the GitHub release, and add the archive to your Gradle project:

```kotlin
dependencies {
    implementation(files("libs/monero-native-android-<version>.aar"))
}
```

Its coordinates, for when it is on a registry, are `io.github.libmonero:monero-native-android:<version>`.

To build native code against it with CMake, turn Prefab on in the module and link the package:

```kotlin
android {
    buildFeatures { prefab = true }
}
```

```cmake
find_package(monero_c REQUIRED CONFIG)
target_link_libraries(app monero_c::monero_c)
```

## Notes for callers

* The library has no Trezor support, so it links no libusb. An app reaches a USB device through the API of Android.
* The library sets `SSL_CERT_DIR` to `/system/etc/security/cacerts` when it is loaded, if neither `SSL_CERT_FILE` nor `SSL_CERT_DIR` is set, so a connection to an `https` daemon with `sslVerify` on finds the certificates of the system.
* Strings are UTF-8. A string or buffer that a function returns in an `out_*` parameter is released with `monero_utils_free()`, and the seed, the private keys and the tx key with `monero_utils_free_secret()`.
* Call `monero_utils_get_abi_version()` after loading the library and compare the major and minor versions with the ones your code was written for. While the major version is 0, a different minor version can break callers.
* Don't use one handle from two threads at once, and keep one native listener per wallet. See [Thread Safety](../../README.md#thread-safety) and the headers.
* Callbacks run on threads of monero-c, which are not attached to the JVM. A JNI bridge attaches the thread of a callback with `AttachCurrentThread()`, and an exception that leaves a callback ends the process.

## Build

The Build workflow makes the archive after the Android libraries, and the release workflow adds it to the draft release. To make it by hand from libraries that were built with the NDK:

```sh
packaging/android/build-aar.sh <version> packaging/android/packages \
  arm64-v8a=<path>/libmonero_c.so x86_64=<path>/libmonero_c.so
packaging/android/check-aar.sh packaging/android/packages/monero-native-android-<version>.aar arm64-v8a x86_64
```

The version is the one of [`Version.props`](../dotnet-core/Version.props). Strip the libraries first (`llvm-strip --strip-unneeded`), as the CI does, because the archive keeps them as they are. `MONERO_C_NDK` is the major version of the NDK that built them.

## Licenses

monero-c is MIT licensed. The archive includes the licenses of the native code it contains, listed in `THIRD-PARTY-NOTICES.md`. It has no libusb, so it has no source archive of it.
