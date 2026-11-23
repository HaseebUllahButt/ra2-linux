#!/bin/bash
# Build the private sampler binary.  The release binary build-linux-game/ra2 is
# kept byte for byte: it is copied aside before the build and moved back after.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "${1:-$here/..}" && pwd)
export RA2_SOURCE_DIR="$root"
want=$(sha256sum "$root/build-linux-game/ra2" | cut -d " " -f 1)
backup=$(mktemp "$root/build-linux-game/ra2-keep.XXXXXX")
cp --reflink=auto -p "$root/build-linux-game/ra2" "$backup"
cleanup() {
  mv -f "$backup" "$root/build-linux-game/ra2"
  python3 "$here/install_sampler.py" --remove
  rm -f "$root/src/linux/sampler_hook.c"
  if [ -n "$want" ]; then
    got=$(sha256sum "$root/build-linux-game/ra2" | awk '{print $1}')
    if [ "$got" != "$want" ]; then
      echo "RELEASE BINARY CHANGED: $got != $want" >&2
      exit 1
    fi
    echo "release ra2 restored, sha256 $got"
  fi
}
trap cleanup EXIT
python3 "$here/install_sampler.py"
cmake --build "$root/build-linux-game" --parallel "${RA2_BUILD_JOBS:-2}"
mv "$root/build-linux-game/ra2" "$root/build-linux-game/ra2-scenario-sampler"
sha256sum "$root/build-linux-game/ra2-scenario-sampler"
echo "sampler binary: $root/build-linux-game/ra2-scenario-sampler"
