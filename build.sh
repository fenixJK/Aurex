#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")" && pwd)
exec cmake "$@" -P "$root/packaging/build.cmake"
