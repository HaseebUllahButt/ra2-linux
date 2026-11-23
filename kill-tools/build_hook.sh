#!/bin/bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "${1:-$here/..}" && pwd)
export RA2_SOURCE_DIR="$root"
backup=$(mktemp "$root/build-linux-game/ra2-keep.XXXXXX")
cp --reflink=auto -p "$root/build-linux-game/ra2" "$backup"
cleanup() {
  mv -f "$backup" "$root/build-linux-game/ra2"
  python3 "$here/install_hook.py" --remove
  rm -f "$root/src/linux/kill_hook.c"
}
trap cleanup EXIT
python3 "$here/install_hook.py"
cmake --build "$root/build-linux-game" --parallel "${RA2_BUILD_JOBS:-2}"
mv "$root/build-linux-game/ra2" "$root/build-linux-game/ra2-kill"
