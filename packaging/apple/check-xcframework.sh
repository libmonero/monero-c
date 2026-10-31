#!/usr/bin/env bash
# Checks the zip of the XCFramework: that the library and libusb are universal, that libmonero_c links libusb next to itself, the headers, the
# module map and the licenses. Then it builds a C program and a Swift program against it, and they have to print the ABI version.
# Run it on macOS.
#
# usage: check-xcframework.sh <zip> <ABI version, such as 0.2.0>
set -u

if [ "$#" -ne 2 ]; then
  echo "usage: $0 <zip> <ABI version>" >&2
  exit 2
fi
zip=$1
abi=$2

here=$(cd "$(dirname "$0")" && pwd)
common=$here/../common
failures=0
fail() { echo "FAIL  $*"; failures=$((failures + 1)); }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
ditto -x -k "$zip" "$work" || { echo "FAIL  $zip is not an archive"; exit 1; }
framework=$work/MoneroC.xcframework
[ -d "$framework" ] || { echo "FAIL  no MoneroC.xcframework at the top of the zip"; exit 1; }
plutil -lint "$framework/Info.plist" > /dev/null || fail "Info.plist is not valid"
slice=$(ls -d "$framework"/macos-* 2> /dev/null | head -1)
[ -n "$slice" ] || { echo "FAIL  no macOS slice"; exit 1; }

for library in libmonero_c.dylib libusb-1.0.0.dylib; do
  archs=$(lipo -archs "$slice/$library" 2> /dev/null)
  case " $archs " in *" x86_64 "*) ;; *) fail "$library has no x86_64 slice ($archs)" ;; esac
  case " $archs " in *" arm64 "*) ;; *) fail "$library has no arm64 slice ($archs)" ;; esac
done
otool -L "$slice/libmonero_c.dylib" | grep -q '@loader_path/libusb-1.0.0.dylib' || fail "libmonero_c does not link libusb at @loader_path"

while read -r header; do
  [ -s "$slice/Headers/$header" ] || fail "no header $header"
done < "$common/headers.txt"
grep -q 'module MoneroC' "$slice/Headers/module.modulemap" || fail "no module map"

for file in LICENSE THIRD-PARTY-NOTICES.md; do
  [ -s "$framework/$file" ] || fail "no $file"
done
for license in $(cd "$common/licenses" && find . -type f | sed 's#^\./##'); do
  [ -s "$framework/licenses/$license" ] || fail "no licenses/$license"
done

# the programs link the library of the XCFramework and run on the architecture of this system
cc -I "$slice/Headers" "$here/smoke/smoke.c" -L "$slice" -lmonero_c -Wl,-rpath,"$slice" -o "$work/smoke-c" || fail "the C program does not build"
"$work/smoke-c" "$abi" || fail "the C program does not run, or has another ABI version"
xcrun swiftc -I "$slice/Headers" "$here/smoke/smoke.swift" -L "$slice" -lmonero_c -Xlinker -rpath -Xlinker "$slice" -o "$work/smoke-swift" || fail "the Swift program does not build"
"$work/smoke-swift" "$abi" || fail "the Swift program does not run, or has another ABI version"

[ "$failures" -eq 0 ] || { echo "$failures check(s) failed"; exit 1; }
echo "the XCFramework is as expected"
