#!/usr/bin/env bash
# Builds the Android archive (AAR) of the library. The library of each ABI is under jni/, where the Android Gradle plugin takes it from, and
# the headers and the libraries are also a Prefab package, for the apps that build native code against monero_c. The archive has no Java
# code, only the license files in its classes.jar, so that they reach the APK of an app.
#
# usage: build-aar.sh <version> <output folder> <abi>=<library>...
# The environment variable MONERO_C_NDK is the major version of the NDK that built the libraries.
set -euo pipefail

if [ "$#" -lt 3 ]; then
  echo "usage: $0 <version> <output folder> <abi>=<library>..." >&2
  exit 2
fi
version=$1
out=$2
shift 2

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
common=$root/packaging/common
artifact=monero-native-android
# the minimum version of Android that the libraries are built for, see build-android.yml
api=28
ndk=${MONERO_C_NDK:-30}

# Prefab takes numbers only
prefab_version=${version%%-*}
case $prefab_version in
  "" | *[!0-9.]*) echo "the version $version has no numeric part for Prefab" >&2; exit 1 ;;
esac

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
aar=$stage/aar
module=$aar/prefab/modules/monero_c
mkdir -p "$aar/jni" "$module/include"

for entry in "$@"; do
  abi=${entry%%=*}
  library=${entry#*=}
  case $abi in
    arm64-v8a | armeabi-v7a | x86_64 | x86) ;;
    *) echo "unknown ABI $abi" >&2; exit 1 ;;
  esac
  [ -s "$library" ] || { echo "no library $library for $abi" >&2; exit 1; }
  mkdir -p "$aar/jni/$abi" "$module/libs/android.$abi"
  cp "$library" "$aar/jni/$abi/libmonero_c.so"
  cp "$library" "$module/libs/android.$abi/libmonero_c.so"
  printf '{"abi":"%s","api":%s,"ndk":%s,"stl":"c++_static","static":false}\n' "$abi" "$api" "$ndk" > "$module/libs/android.$abi/abi.json"
done

# the headers, in the layout that monero_c.h includes them in
while read -r header; do
  mkdir -p "$module/include/$(dirname "$header")"
  cp "$root/src/$header" "$module/include/$header"
done < "$common/headers.txt"

printf '{"schema_version":2,"name":"monero_c","version":"%s","dependencies":[]}\n' "$prefab_version" > "$aar/prefab/prefab.json"
printf '{"export_libraries":[],"library_name":"libmonero_c"}\n' > "$module/module.json"

cat > "$aar/AndroidManifest.xml" <<MANIFEST
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="io.github.libmonero.monero_native">
    <uses-sdk android:minSdkVersion="$api" />
</manifest>
MANIFEST
: > "$aar/R.txt"

# classes.jar has the manifest first, then the licenses under a folder of its own so that they don't meet the ones of other libraries
notices=$stage/jar/META-INF/monero-native
mkdir -p "$notices"
cp "$root/LICENSE" "$common/THIRD-PARTY-NOTICES.md" "$notices/"
cp -R "$common/licenses" "$notices/"
# the libusb source archive goes with the libusb libraries, and the Android libraries link none
rm -rf "$notices/licenses/source"
printf 'Manifest-Version: 1.0\nCreated-By: monero-c packaging/android\n' > "$stage/jar/META-INF/MANIFEST.MF"

# the same files at the root of the archive, for a person who unpacks it
cp "$root/LICENSE" "$common/THIRD-PARTY-NOTICES.md" "$aar/"
cp -R "$common/licenses" "$aar/"
rm -rf "$aar/licenses/source"

# fixed times and sorted names give the same bytes for the same inputs
epoch=${SOURCE_DATE_EPOCH:-$(git -C "$root" log -1 --format=%ct 2>/dev/null || date +%s)}
export TZ=UTC
find "$stage/jar" -exec touch -h -d "@$epoch" {} +
(cd "$stage/jar" && { echo META-INF/MANIFEST.MF; find META-INF -type f ! -name MANIFEST.MF | LC_ALL=C sort; } | zip -qX "$aar/classes.jar" -@)
find "$aar" -exec touch -h -d "@$epoch" {} +

mkdir -p "$out"
out=$(cd "$out" && pwd)
rm -f "$out/$artifact-$version.aar"
(cd "$aar" && find . -type f | sed 's#^\./##' | LC_ALL=C sort | zip -qX "$out/$artifact-$version.aar" -@)

sed -e "s#@ARTIFACT@#$artifact#g" -e "s#@VERSION@#$version#g" -e "s#@PACKAGING@#aar#g" \
  -e "s#@DESCRIPTION@#The native runtime of monero-c for Android: libmonero_c.so for each ABI, with a Prefab package of the headers. It has no Java code.#" \
  "$common/pom.xml.in" > "$out/$artifact-$version.pom"

echo "$out/$artifact-$version.aar"
