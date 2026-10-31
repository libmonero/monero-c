#!/usr/bin/env bash
# Packs the library of a platform into the archive of a release: a folder monero-c-<version>-<platform> with the library, the libusb that
# goes with it, the public headers under include/ and the licenses, as a .zip on Windows and a .tar.gz on the other platforms.
#
# usage: pack-archive.sh <version> <platform> <build folder> <licenses folder> <output folder>
# The build folder is the one that the Build workflow uploads for the platform, and the files lib* of it are packed.
set -euo pipefail

if [ "$#" -ne 5 ]; then
  echo "usage: $0 <version> <platform> <build folder> <licenses folder> <output folder>" >&2
  exit 2
fi
version=$1
platform=$2
build=$3
licenses=$4
out=$5

root=$(cd "$(dirname "$0")/../.." && pwd)
name="monero-c-$version-$platform"

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/$name/include"
cp "$build"/lib* "$stage/$name/"
cp -R "$licenses" "$stage/$name/licenses"
# the public headers, in the layout that monero_c.h includes them in
(cd "$root/src" && cp --parents $(cat "$root/packaging/common/headers.txt") "$stage/$name/include/")
cp "$root/LICENSE" "$root/README.md" "$stage/$name/"

mkdir -p "$out"
out=$(cd "$out" && pwd)
if [ "$platform" = windows ]; then
  (cd "$stage" && zip -qr "$out/$name.zip" "$name")
  echo "$out/$name.zip"
else
  tar -C "$stage" -czf "$out/$name.tar.gz" "$name"
  echo "$out/$name.tar.gz"
fi
