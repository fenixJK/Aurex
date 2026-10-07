#!/bin/bash
set -euo pipefail
export LC_ALL=C
root=${1:?Specify the dependency directory}
version=3.6.3
mkdir -p "$root"
root=$(cd "$root" && pwd)
archive="$root/openssl-$version.tar.gz"
if [[ ! -f "$archive" ]]; then
  curl -fL "https://github.com/openssl/openssl/releases/download/openssl-$version/openssl-$version.tar.gz" -o "$archive"
fi
expected=243a86649cf6f23eeb6a2ff2456e09e5d77dd9018a54d3d96b0c6bdd6ba6c7f1
actual=$(shasum -a 256 "$archive")
[[ ${actual%% *} == "$expected" ]] || { echo "OpenSSL checksum mismatch" >&2; exit 1; }
if [[ ! -d "$root/openssl-$version" ]]; then tar -xzf "$archive" -C "$root"; fi
cd "$root/openssl-$version"
case $(uname -m) in
  arm64) target=darwin64-arm64-cc ;;
  x86_64) target=darwin64-x86_64-cc ;;
  *) exit 1 ;;
esac
./Configure "$target" no-shared no-tests no-apps no-docs no-module no-legacy no-engine --prefix="$root/openssl-install" -mmacosx-version-min=14.0
make -j3 build_libs
make install_dev
