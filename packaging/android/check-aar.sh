#!/usr/bin/env bash
# Checks the contents of an Android archive: the manifest, the library of each ABI under jni/ and in the Prefab package, the headers, the
# metadata of Prefab and the licenses in classes.jar.
#
# usage: check-aar.sh <aar> <abi>...
set -u

if [ "$#" -lt 2 ]; then
  echo "usage: $0 <aar> <abi>..." >&2
  exit 2
fi
aar=$1
shift

here=$(cd "$(dirname "$0")" && pwd)
common=$here/../common
failures=0
fail() { echo "FAIL  $*"; failures=$((failures + 1)); }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
unzip -q "$aar" -d "$work/aar" || { echo "FAIL  $aar is not an archive"; exit 1; }
aar=$work/aar
unzip -q "$aar/classes.jar" -d "$work/jar" || fail "classes.jar is not an archive"

for file in AndroidManifest.xml classes.jar LICENSE THIRD-PARTY-NOTICES.md prefab/prefab.json prefab/modules/monero_c/module.json; do
  [ -s "$aar/$file" ] || fail "no $file"
done
# empty, because there are no resources
[ -e "$aar/R.txt" ] || fail "no R.txt"

grep -q 'android:minSdkVersion="28"' "$aar/AndroidManifest.xml" || fail "the manifest doesn't set minSdkVersion 28"
grep -q 'package="[A-Za-z0-9_.]*"' "$aar/AndroidManifest.xml" || fail "the manifest has no package"

jq -e '.schema_version == 2 and .name == "monero_c" and (.version | test("^[0-9]+(\\.[0-9]+){0,3}$"))' "$aar/prefab/prefab.json" > /dev/null || fail "prefab.json is not valid"
jq -e '.library_name == "libmonero_c"' "$aar/prefab/modules/monero_c/module.json" > /dev/null || fail "module.json is not valid"

while read -r header; do
  [ -s "$aar/prefab/modules/monero_c/include/$header" ] || fail "no header $header in the Prefab package"
done < "$common/headers.txt"

declare -A machines=([arm64-v8a]="AArch64" [x86_64]="Advanced Micro Devices X86-64" [armeabi-v7a]="ARM" [x86]="Intel 80386")
for abi in "$@"; do
  before=$failures
  jni=$aar/jni/$abi/libmonero_c.so
  prefab=$aar/prefab/modules/monero_c/libs/android.$abi/libmonero_c.so
  [ -s "$jni" ] || { fail "no $jni"; continue; }
  cmp -s "$jni" "$prefab" || fail "the library of $abi in the Prefab package is not the one in jni/"
  jq -e --arg abi "$abi" '.abi == $abi and .api == 28 and .stl == "c++_static" and .static == false' \
    "$aar/prefab/modules/monero_c/libs/android.$abi/abi.json" > /dev/null || fail "abi.json of $abi is not valid"
  readelf -h "$jni" | grep -q "Machine: *${machines[$abi]}" || fail "the library in jni/$abi is not for $abi"
  [ "$(readelf -d "$jni" | awk '/SONAME/ {print $NF}')" = "[libmonero_c.so]" ] || fail "the soname of $abi is not libmonero_c.so"
  [ "$failures" -eq "$before" ] && echo "ok    $abi"
done

# an ABI that is not asked for must not be there
for dir in "$aar"/jni/*; do
  abi=$(basename "$dir")
  case " $* " in *" $abi "*) ;; *) fail "unexpected ABI $abi" ;; esac
done

for notice in LICENSE THIRD-PARTY-NOTICES.md; do
  [ -s "$work/jar/META-INF/monero-native/$notice" ] || fail "classes.jar has no $notice"
done
for license in $(find "$common/licenses" -type f ! -path '*/source/*' -printf '%P\n'); do
  [ -s "$work/jar/META-INF/monero-native/licenses/$license" ] || fail "classes.jar has no licenses/$license"
done
[ ! -e "$aar/licenses/source" ] && [ ! -e "$work/jar/META-INF/monero-native/licenses/source" ] || fail "the libusb source archive is in the archive, and the libraries link no libusb"

[ "$failures" -eq 0 ] || { echo "$failures check(s) failed"; exit 1; }
echo "the archive is as expected"
