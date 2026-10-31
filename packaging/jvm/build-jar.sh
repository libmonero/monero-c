#!/usr/bin/env bash
# Builds the JAR of the library for the JVM. It has no classes: the libraries of each platform are resources in a folder named as the
# resource prefix of JNA (linux-x86-64, linux-aarch64, darwin-x86-64, darwin-aarch64, win32-x86-64), with the libusb that the library needs
# next to it, and the licenses are under META-INF/monero-native/.
#
# usage: build-jar.sh <version> <output folder> <platform>=<folder with the build of the platform>...
# The folder has the files that the Build workflow stages: libmonero_c.so and libusb-1.0.so.0 on Linux, libmonero_c.dylib and
# libusb-1.0.0.dylib on macOS, libmonero_c.dll and libusb-1.0.dll on Windows. The DLL is renamed monero_c.dll, as in the NuGet package.
set -euo pipefail

if [ "$#" -lt 3 ]; then
  echo "usage: $0 <version> <output folder> <platform>=<folder>..." >&2
  exit 2
fi
version=$1
out=$2
shift 2

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
common=$root/packaging/common
artifact=monero-native-jvm

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
jar=$stage/jar
mkdir -p "$jar/META-INF"

for entry in "$@"; do
  platform=${entry%%=*}
  folder=${entry#*=}
  case $platform in
    linux-x86-64 | linux-aarch64) files="libmonero_c.so libusb-1.0.so.0" ;;
    darwin-x86-64 | darwin-aarch64) files="libmonero_c.dylib libusb-1.0.0.dylib" ;;
    win32-x86-64) files="libmonero_c.dll:monero_c.dll libusb-1.0.dll" ;;
    *) echo "unknown platform $platform" >&2; exit 1 ;;
  esac
  mkdir -p "$jar/$platform"
  for file in $files; do
    source=${file%%:*}
    target=${file#*:}
    [ -s "$folder/$source" ] || { echo "no $source in $folder for $platform" >&2; exit 1; }
    cp "$folder/$source" "$jar/$platform/$target"
  done
done

notices=$jar/META-INF/monero-native
mkdir -p "$notices"
cp "$root/LICENSE" "$common/THIRD-PARTY-NOTICES.md" "$notices/"
cp -R "$common/licenses" "$notices/"
printf 'Manifest-Version: 1.0\nCreated-By: monero-c packaging/jvm\nAutomatic-Module-Name: io.github.libmonero.monero_native\n' > "$jar/META-INF/MANIFEST.MF"

# fixed times and sorted names give the same bytes for the same inputs
epoch=${SOURCE_DATE_EPOCH:-$(git -C "$root" log -1 --format=%ct 2>/dev/null || date +%s)}
export TZ=UTC
find "$jar" -exec touch -h -d "@$epoch" {} +

mkdir -p "$out"
out=$(cd "$out" && pwd)
rm -f "$out/$artifact-$version.jar"
(cd "$jar" && { echo META-INF/MANIFEST.MF; find . -type f ! -path ./META-INF/MANIFEST.MF | sed 's#^\./##' | LC_ALL=C sort; } | zip -qX "$out/$artifact-$version.jar" -@)

sed -e "s#@ARTIFACT@#$artifact#g" -e "s#@VERSION@#$version#g" -e "s#@PACKAGING@#jar#g" \
  -e "s#@DESCRIPTION@#The native runtime of monero-c for the JVM: libmonero_c and libusb for Linux, macOS and Windows, as resources. It has no classes.#" \
  "$common/pom.xml.in" > "$out/$artifact-$version.pom"

echo "$out/$artifact-$version.jar"
