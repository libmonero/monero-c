# monero-native-jvm

The native runtime package of monero-c for the JVM. It holds `libmonero_c` and the libusb that it needs for each supported platform, as resources of a JAR, and no classes. A binding for Kotlin or Java loads the library from it, with JNA, with the foreign function API of Java 22 or with a JNI bridge of its own.

This is an alpha version. The C API can still change between versions.

## Supported platforms

| Folder in the JAR | System | Library tested on | Needs |
| --- | --- | --- | --- |
| `linux-x86-64` | Linux, x86-64 | Debian 12, Ubuntu 22.04 and 24.04 | glibc 2.34 and libstdc++ 3.4.29 (GCC 11) or newer |
| `linux-aarch64` | Linux, ARM64 | Debian 12, Ubuntu 22.04 and 24.04 | glibc 2.34 and libstdc++ 3.4.29 (GCC 11) or newer |
| `darwin-x86-64` | macOS, Intel | macOS 15 | macOS 11.0 or newer |
| `darwin-aarch64` | macOS, Apple silicon | macOS 15 | macOS 11.0 or newer |
| `win32-x86-64` | Windows, x64 | GitHub `windows-latest` | Windows 8 or newer |

The libraries are those of the NuGet package, which the Tests workflow runs on these systems. The CI loads the JAR itself on Ubuntu 24.04 x86-64 only. The folders are named as `Platform.RESOURCE_PREFIX` of [JNA](https://github.com/java-native-access/jna). Not supported: older glibc (Ubuntu 20.04 and Debian 11 have 2.31), musl (Alpine), 32-bit systems and Windows ARM64. For Android use [monero-native-android](../android/README.md).

Each folder has the same files as `runtimes/<rid>/native/` of the `Monero.Native` NuGet package: `libmonero_c.so` and `libusb-1.0.so.0` on Linux, `libmonero_c.dylib` and `libusb-1.0.0.dylib` on macOS, `monero_c.dll` and `libusb-1.0.dll` on Windows.

## Use

The package is not on a registry yet. Take `monero-native-jvm-<version>.jar` and its POM from the files of the GitHub release, and add the JAR to your Gradle project:

```kotlin
dependencies {
    runtimeOnly(files("libs/monero-native-jvm-<version>.jar"))
}
```

Its coordinates, for when it is on a registry, are `io.github.libmonero:monero-native-jvm:<version>`.

A JVM loads a library from a file, so a program extracts the files of its platform from the JAR into one directory and loads `libmonero_c` from there. Extract all of them: `libmonero_c` finds libusb next to itself (`$ORIGIN` on Linux, `@loader_path` on macOS). JNA extracts only the library that it loads, so extract the folder yourself and point `jna.library.path` at it. [`smoke/Smoke.java`](smoke/Smoke.java) does it with the foreign function API and reads the ABI version of the library. It is the check of the CI, and a reference for a loader.

## Notes for callers

* Strings are UTF-8. A string or buffer that a function returns in an `out_*` parameter is released with `monero_utils_free()`, and the seed, the private keys and the tx key with `monero_utils_free_secret()`.
* A C `bool` is one byte.
* Call `monero_utils_get_abi_version()` after loading the library and compare the major and minor versions with the ones your code was written for. While the major version is 0, a different minor version can break callers.
* `monero_last_error()` and `monero_last_error_code()` are of the calling thread. A virtual thread can move to another carrier thread, so read them right after the failing call, without a blocking call in between.
* Callbacks run on threads of monero-c, which are not attached to the JVM. An upcall from native code attaches its thread by itself, and an exception that leaves one ends the process.
* Don't use one handle from two threads at once, and keep one native listener per wallet. See [Thread Safety](../../README.md#thread-safety) and the headers.
* On Linux and macOS the library sets `SSL_CERT_FILE` and `SSL_CERT_DIR` in your process when it is loaded, to the CA certificates of the system, if neither is set and OpenSSL doesn't find them by itself.
* `System.load()` and `SymbolLookup.libraryLookup()` need the native access of the program: on Java 22 and newer give `--enable-native-access=ALL-UNNAMED`, or the module of your binding.

## Build

The Build workflow makes the JAR after the desktop libraries, and the release workflow adds it to the draft release. To make it by hand, stage the libraries of each platform as the Build workflow does (the library and libusb in one folder), then:

```sh
packaging/jvm/build-jar.sh <version> packaging/jvm/packages \
  linux-x86-64=<folder> darwin-aarch64=<folder> win32-x86-64=<folder>
packaging/jvm/check-jar.sh packaging/jvm/packages/monero-native-jvm-<version>.jar linux-x86-64 darwin-aarch64 win32-x86-64
java --enable-preview --source 21 --enable-native-access=ALL-UNNAMED packaging/jvm/smoke/Smoke.java \
  packaging/jvm/packages/monero-native-jvm-<version>.jar 0.2.0
```

The version is the one of [`Version.props`](../dotnet-core/Version.props), and the last argument of the smoke test is the ABI version of the header. Strip the Linux libraries first, as the CI does, because the JAR keeps them as they are. The licenses and the source archive of libusb must be in `packaging/common/licenses/`: the CI takes them from the Linux build.

## Licenses

monero-c is MIT licensed. The JAR includes the licenses of the native code it contains in `META-INF/monero-native/`, listed in `THIRD-PARTY-NOTICES.md`. libusb is LGPL-2.1 and ships as a separate shared library next to `libmonero_c`, with its source archive in `META-INF/monero-native/licenses/source/`.
