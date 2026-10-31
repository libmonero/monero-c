#!/usr/bin/env bash
# Builds the XCFramework of the macOS libraries and zips it. The libraries of the two architectures become one universal file each, and
# the XCFramework has libmonero_c.dylib with the headers and a module map, so Swift can import MoneroC. libusb goes next to the library in
# the folder of the XCFramework, because libmonero_c finds it at @loader_path. Run it on macOS, which has lipo, codesign and xcodebuild.
#
# usage: build-xcframework.sh <version> <output folder> <x86_64 build folder> <arm64 build folder>
# The build folders are the ones that the Build workflow uploads for macOS: libmonero_c.dylib and libusb-1.0.0.dylib.
set -euo pipefail

if [ "$#" -ne 4 ]; then
  echo "usage: $0 <version> <output folder> <x86_64 build folder> <arm64 build folder>" >&2
  exit 2
fi
version=$1
out=$2
x86_64=$3
arm64=$4

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
common=$root/packaging/common
name=MoneroC

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/universal" "$work/include"

for library in libmonero_c.dylib libusb-1.0.0.dylib; do
  for folder in "$x86_64" "$arm64"; do
    [ -s "$folder/$library" ] || { echo "no $library in $folder" >&2; exit 1; }
  done
  lipo -create "$x86_64/$library" "$arm64/$library" -output "$work/universal/$library"
  # the signature of each slice stays valid, and the file is signed as a whole again
  codesign --force --sign - "$work/universal/$library"
done

# the headers, in the layout that monero_c.h includes them in
while read -r header; do
  mkdir -p "$work/include/$(dirname "$header")"
  cp "$root/src/$header" "$work/include/$header"
done < "$common/headers.txt"
cat > "$work/include/module.modulemap" <<'MODULEMAP'
module MoneroC {
    header "monero_c.h"
    export *
}
MODULEMAP

xcodebuild -create-xcframework -library "$work/universal/libmonero_c.dylib" -headers "$work/include" -output "$work/$name.xcframework"
slice=$(ls -d "$work/$name.xcframework"/macos-*)
cp "$work/universal/libusb-1.0.0.dylib" "$slice/"

# the licenses, with the source archive of libusb
cp "$root/LICENSE" "$common/THIRD-PARTY-NOTICES.md" "$work/$name.xcframework/"
cp -R "$common/licenses" "$work/$name.xcframework/licenses"

mkdir -p "$out"
out=$(cd "$out" && pwd)
rm -f "$out/$name-$version.xcframework.zip"
ditto -c -k --sequesterRsrc --keepParent "$work/$name.xcframework" "$out/$name-$version.xcframework.zip"

echo "$out/$name-$version.xcframework.zip"
echo "checksum for a binaryTarget: $(shasum -a 256 "$out/$name-$version.xcframework.zip" | awk '{print $1}')"
