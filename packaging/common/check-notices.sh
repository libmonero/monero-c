#!/usr/bin/env bash
# Checks that the third-party notices cover what the native libraries have linked in. A function name or a message of a component in
# the strings of a library means that the component is linked, so its license text has to be in the licenses folder. A stripped library
# has fewer names, so run it on the libraries as they are built.
#
# usage: check-notices.sh <licenses folder> <library>...
set -u

if [ "$#" -lt 2 ]; then
  echo "usage: $0 <licenses folder> <library>..."
  exit 2
fi
licenses=$1
shift

# component | text that its strings have | its license text
covered='
nghttp2|nghttp2_session_|nghttp2-MIT.txt
libevent|do not rely on this behavior in future Libevent versions|libevent-BSD-3-Clause.txt
Unbound|sldns_|unbound-BSD-3-Clause.txt
HIDAPI|hid_enumerate|hidapi-BSD-3-Clause.txt
OpenSSL|EVP_DigestInit_ex|openssl-Apache-2.0.txt
LMDB|mdb_env_create|lmdb-OLDAP-2.8.txt
RandomX|randomx_alloc_cache|randomx-BSD-3-Clause.txt
OpenAES|oaes_key_gen|openaes-BSD-3-Clause.txt
mingw-w64 runtime|Mingw-w64 runtime failure|mingw-w64-runtime-COPYING.txt
LLVM C++ runtime (libc++, libc++abi, libunwind)|unw_init_local|llvm-project-Apache-2.0-LLVM-exception.txt
'

# components with no notice. Finding one is a failure until its license text is in the package and in THIRD-PARTY-NOTICES.md
uncovered='
ZeroMQ|zmq_ctx_new
libcurl|curl_easy_init
xz|lzma_stream_decoder
bzip2|BZ2_bzCompress
Zstandard|ZSTD_compress
miniupnpc|upnpDiscover
OpenPGM|pgm_init
'

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
failures=0

for library in "$@"; do
  if [ ! -s "$library" ]; then
    echo "FAIL  $library: no such library"
    failures=$((failures + 1))
    continue
  fi
  strings -a "$library" > "$work/strings.txt"
  while IFS='|' read -r component text file; do
    [ -n "$component" ] || continue
    if grep -qF -- "$text" "$work/strings.txt"; then
      if [ -s "$licenses/$file" ]; then
        echo "ok    $library: $component, $file"
      else
        echo "FAIL  $library: $component is linked and $file is not in $licenses"
        failures=$((failures + 1))
      fi
    fi
  done <<< "$covered"
  while IFS='|' read -r component text; do
    [ -n "$component" ] || continue
    if grep -qF -- "$text" "$work/strings.txt"; then
      echo "FAIL  $library: $component is linked and has no notice"
      failures=$((failures + 1))
    fi
  done <<< "$uncovered"
done

if [ "$failures" -ne 0 ]; then
  echo "$failures notices are missing"
  exit 1
fi
echo "the notices cover the components that the libraries link"
