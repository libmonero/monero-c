#!/usr/bin/env bash
# Stages the package for pub.dev and archives it as monero_native-<version>.tar.gz. The package downloads the archives of the release, so it
# gets checksums.txt with their SHA-256, and the hook checks every download against it. The archives must be in the release folder, one for
# each platform, so run it after packaging/common/pack-archive.sh.
#
# usage: build.sh <version> <release folder>
# Publish the folder of the archive with: dart pub publish
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "usage: $0 <version> <release folder>" >&2
  exit 2
fi
version=$1
release=$2

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)

pubspec_version=$(sed -n 's/^version: *//p' "$here/pubspec.yaml")
if [ "$pubspec_version" != "$version" ]; then
  echo "pubspec.yaml has version $pubspec_version and the package is $version: set the version in pubspec.yaml" >&2
  exit 1
fi

release=$(cd "$release" && pwd)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
package=$stage/monero_native
mkdir -p "$package"
cp -R "$here/pubspec.yaml" "$here/README.md" "$here/CHANGELOG.md" "$here/hook" "$here/lib" "$here/test" "$package/"
cp "$root/LICENSE" "$package/"

archives="linux-x86_64.tar.gz linux-arm64.tar.gz mac-x86_64.tar.gz mac-arm64.tar.gz windows.zip android-arm64.tar.gz android-x86_64.tar.gz"
: > "$package/checksums.txt"
for archive in $archives; do
  name=monero-c-$version-$archive
  [ -s "$release/$name" ] || { echo "no $name in $release" >&2; exit 1; }
  (cd "$release" && sha256sum "$name") >> "$package/checksums.txt"
done

epoch=${SOURCE_DATE_EPOCH:-$(git -C "$root" log -1 --format=%ct 2>/dev/null || date +%s)}
tar -C "$stage" --sort=name --mtime="@$epoch" --owner=0 --group=0 --numeric-owner -czf "$release/monero_native-$version.tar.gz" monero_native
echo "$release/monero_native-$version.tar.gz"
