#!/usr/bin/env bash
# Checks the contents of the JAR: the libraries and libusb of each platform, that each one is built for its platform, and the licenses.
#
# usage: check-jar.sh <jar> <platform>...
set -u

if [ "$#" -lt 2 ]; then
  echo "usage: $0 <jar> <platform>..." >&2
  exit 2
fi
jar=$1
shift

here=$(cd "$(dirname "$0")" && pwd)
common=$here/../common
failures=0
fail() { echo "FAIL  $*"; failures=$((failures + 1)); }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
unzip -q "$jar" -d "$work" || { echo "FAIL  $jar is not an archive"; exit 1; }

[ "$(unzip -Z1 "$jar" | head -1)" = META-INF/MANIFEST.MF ] || fail "the manifest is not the first entry"
grep -q '^Automatic-Module-Name: ' "$work/META-INF/MANIFEST.MF" || fail "the manifest has no Automatic-Module-Name"

for platform in "$@"; do
  before=$failures
  case $platform in
    linux-x86-64) files="libmonero_c.so libusb-1.0.so.0"; type='ELF.*x86-64' ;;
    linux-aarch64) files="libmonero_c.so libusb-1.0.so.0"; type='ELF.*ARM aarch64' ;;
    darwin-x86-64) files="libmonero_c.dylib libusb-1.0.0.dylib"; type='Mach-O.*x86_64' ;;
    darwin-aarch64) files="libmonero_c.dylib libusb-1.0.0.dylib"; type='Mach-O.*arm64' ;;
    win32-x86-64) files="monero_c.dll libusb-1.0.dll"; type='PE32\+ executable.*DLL.*x86-64' ;;
    *) fail "unknown platform $platform"; continue ;;
  esac
  for file in $files; do
    path=$work/$platform/$file
    if [ ! -s "$path" ]; then
      fail "no $platform/$file"
    elif ! file -b "$path" | grep -Eq "$type"; then
      fail "$platform/$file is not built for $platform: $(file -b "$path")"
    fi
  done
  [ "$failures" -eq "$before" ] && echo "ok    $platform"
done

# a platform that is not asked for must not be there
for dir in "$work"/*/; do
  name=$(basename "$dir")
  [ "$name" = META-INF ] && continue
  case " $* " in *" $name "*) ;; *) fail "unexpected folder $name" ;; esac
done

for notice in LICENSE THIRD-PARTY-NOTICES.md; do
  [ -s "$work/META-INF/monero-native/$notice" ] || fail "no $notice"
done
# every platform here links libusb as a shared library, so the source of it goes with the license texts
for license in $(find "$common/licenses" -type f -printf '%P\n'); do
  [ -s "$work/META-INF/monero-native/licenses/$license" ] || fail "no licenses/$license"
done
[ -s "$work/META-INF/monero-native/licenses/source/libusb-1.0.27.tar.bz2" ] || fail "no source archive of libusb"

[ "$failures" -eq 0 ] || { echo "$failures check(s) failed"; exit 1; }
echo "the JAR is as expected"
