#!/usr/bin/env bash
set -euo pipefail

source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
data_dir="${XDG_DATA_HOME:-$HOME/.local/share}"
target="$data_dir/ra2-linux"
command_path="$HOME/.local/bin/ra2-linux"
desktop="$data_dir/applications/ra2-linux.desktop"
expected_hash=73288c03b58d370be268ca6d156b4e33bfdb2066dc980359467d8852ff3b00df
required_assets=(game.exe ra2.mix language.mix maps01.mix maps02.mix multi.mix movies01.mix movies02.mix theme.mix)

die() { printf 'Error: %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<'HELP'
Usage: ./install.sh [--game-dir PATH] [--jobs COUNT]
       ./install.sh --uninstall [--purge]

Install Red Alert 2 in ~/.local/share/ra2-linux and add a desktop entry.
Use --game-dir to supply your Red Alert 2 folder without a prompt.
Use --purge with --uninstall to remove settings and saves too.
HELP
}

game_dir=
jobs=${RA2_BUILD_JOBS:-}
uninstall=0
purge=0
while (( $# )); do
    case "$1" in
        --game-dir) (( $# >= 2 )) || die 'Give a path after --game-dir.'; game_dir=$2; shift ;;
        --jobs) (( $# >= 2 )) || die 'Give a count after --jobs.'; jobs=$2; shift ;;
        --uninstall) uninstall=1 ;;
        --purge) purge=1 ;;
        --help|-h) usage; exit 0 ;;
        *) die "Unknown option $1. See --help." ;;
    esac
    shift
done

# Remove only the links and files that this installer owns.
if (( uninstall )); then
    if [[ ! -f "$target/.ra2-linux-install" ]]; then
        printf 'No Red Alert 2 Linux installation at %s.\n' "$target"
        exit 0
    fi
    if [[ -L "$command_path" && $(readlink -- "$command_path") == "$target/play.sh" ]]; then
        rm -- "$command_path"
    fi
    rm -f -- "$desktop" "$target/play.sh" "$target/ra2"
    if (( purge )); then
        rm -rf -- "$target"
        printf 'Uninstalled Red Alert 2 Linux and removed its local game data.\n'
    else
        if [[ -f "$target/.installed-assets" ]]; then
            while IFS= read -r name; do
                [[ -n "$name" && "$name" != .* && "$name" != */* ]] || continue
                rm -f -- "$target/game/$name"
            done < "$target/.installed-assets"
            rm -- "$target/.installed-assets"
        fi
        printf 'Uninstalled Red Alert 2 Linux. Settings and saves remain in %s/game.\n' "$target"
    fi
    exit 0
fi
(( ! purge )) || die 'Use --purge with --uninstall.'
[[ $(uname -s) == Linux && $(uname -m) =~ ^(x86_64|i[3-6]86)$ ]] ||
    die 'This build needs x86 Linux with 32-bit program support.'
[[ -z "$jobs" || "$jobs" =~ ^[1-9][0-9]*$ ]] || die 'The job count must be a positive integer.'
[[ ! -e "$target" || ( -d "$target" && -f "$target/.ra2-linux-install" ) ]] ||
    die "The install path already exists and belongs to another installation: $target"
[[ ! -e "$command_path" && ! -L "$command_path" ||
   ( -L "$command_path" && $(readlink -- "$command_path") == "$target/play.sh" ) ]] ||
    die "The start command already exists: $command_path"

missing=()
for tool in gcc cmake ninja pkg-config python3 fc-list; do
    command -v "$tool" >/dev/null || missing+=("$tool")
done
if command -v gcc >/dev/null &&
   ! printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o /dev/null 2>/dev/null; then
    missing+=('32-bit GCC and C library headers')
fi

# Exclude default 64-bit pkg-config directories on x86-64.
pc_dirs="${RA2_DEPS_PC:+$RA2_DEPS_PC:}/usr/lib/i386-linux-gnu/pkgconfig:/usr/lib32/pkgconfig"
if [[ $(uname -m) != x86_64 || ( -d /usr/lib64 && ! -L /usr/lib64 ) ]]; then
    pc_dirs+=:/usr/lib/pkgconfig
fi
if command -v pkg-config >/dev/null; then
    for lib in sdl2 SDL2_ttf libavformat libavcodec libavutil libswresample; do
        PKG_CONFIG_PATH= PKG_CONFIG_LIBDIR="$pc_dirs" pkg-config --exists "$lib" ||
            missing+=("$lib (32-bit)")
    done
fi
if command -v python3 >/dev/null &&
   ! python3 -c 'import capstone, pefile' 2>/dev/null &&
   ! python3 -m venv --help >/dev/null 2>&1; then
    missing+=('Python venv or the capstone and pefile modules')
fi
if command -v fc-list >/dev/null && [[ -z $(fc-list 'Liberation Sans' family) ]]; then
    missing+=('Liberation Sans fonts')
fi
if (( ${#missing[@]} )); then
    printf 'Missing dependencies:\n' >&2
    printf '  %s\n' "${missing[@]}" >&2
    cat >&2 <<'PACKAGES'
Debian and Ubuntu
  Enable i386 with dpkg --add-architecture i386 and refresh the package list.
  gcc-multilib cmake ninja-build pkg-config python3-venv fonts-liberation
  libsdl2-dev:i386 libsdl2-ttf-dev:i386 libavformat-dev:i386
  libavcodec-dev:i386 libavutil-dev:i386 libswresample-dev:i386
Fedora
  gcc glibc-devel.i686 libgcc.i686 libatomic.i686 cmake ninja-build pkgconf
  python3 python3-pip liberation-sans-fonts sdl2-compat-devel.i686
  SDL2_ttf-devel.i686 libavformat-free-devel.i686 libavcodec-free-devel.i686
  libavutil-free-devel.i686 libswresample-free-devel.i686
Arch Linux
  Enable the multilib repository in pacman.conf.
  gcc cmake ninja pkgconf python python-pip ttf-liberation
  lib32-glibc lib32-gcc-libs lib32-sdl2-compat lib32-sdl2_ttf
  Get lib32-ffmpeg from the Arch User Repository.
PACKAGES
    exit 1
fi

if [[ -z "$game_dir" ]]; then
    read -r -e -p 'Path to your Red Alert 2 folder: ' game_dir ||
        die 'Supply the game folder with --game-dir PATH.'
fi
[[ -d "$game_dir" ]] || die "Game folder does not exist: $game_dir"
game_dir=$(cd -- "$game_dir" && pwd -P)
[[ "$game_dir" != "$target/game" ]] || die 'Choose your original game folder.'
declare -A assets=()
while IFS= read -r -d '' name; do
    key=${name,,}
    if [[ ! -v "assets[$key]" || "$name" == "$key" ]]; then
        assets[$key]=$name
    fi
done < <(find -L "$game_dir" -maxdepth 1 -type f -printf '%f\0')
[[ -v 'assets[game.exe]' ]] || die 'The folder must contain game.exe from base Red Alert 2.'
read -r hash _ < <(sha256sum < "$game_dir/${assets[game.exe]}")
[[ "$hash" == "$expected_hash" ]] || die "Unsupported game.exe SHA-256: $hash. The installer needs the supported Red Alert 2 1.006 executable."
for name in "${required_assets[@]}"; do
    [[ -v "assets[$name]" ]] || die "Game file missing: $name"
done

mkdir -p -- "$data_dir"
stage=$(mktemp -d "$data_dir/.ra2-linux-build.XXXXXXXX")
trap 'rm -rf -- "$stage"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir -- "$stage/game" "$stage/src" "$stage/pcrecomp"
cp -- "$source_dir"/{CMakeLists.txt,run_lift.py,build-linux.sh} "$stage/"
cp -R -- "$source_dir/cmake" "$stage/"
cp -R -- "$source_dir"/src/{linux,runtime} "$stage/src/"
cp -R -- "$source_dir"/pcrecomp/{runtime,tools} "$stage/pcrecomp/"
touch "$stage/.installed-assets"
for name in "${!assets[@]}"; do
    [[ "$name" != *$'\n'* && "$name" != .* ]] || continue
    case "$name" in
        gamemd*|*md.mix|*md[0-9][0-9].mix) continue ;;
        game.exe|*.mix|*.mmx|conquer.dat|ra2.dsk|00000409.016|00000409.256)
            cp --reflink=auto -- "$game_dir/${assets[$name]}" "$stage/game/$name"
            printf '%s\n' "$name" >> "$stage/.installed-assets"
            ;;
    esac
done
read -r copied_hash _ < <(sha256sum < "$stage/game/game.exe")
[[ "$copied_hash" == "$expected_hash" ]] || die 'game.exe changed during the copy.'

cd -- "$stage"
python=python3
if ! "$python" -c 'import capstone, pefile' 2>/dev/null; then
    "$python" -m venv .venv || die 'Install the Python venv package and try again.'
    python="$stage/.venv/bin/python"
    "$python" -m pip install 'capstone==5.0.7' 'pefile==2024.8.26'
fi
unset PCRECOMP
export CC=gcc
export PKG_CONFIG_LIBDIR="$pc_dirs"
printf 'Find the functions in game.exe.\n'
mkdir -p work/game
"$python" pcrecomp/tools/cpp/rtti.py game/game.exe -o work/game/rtti.json --seeds work/game/rtti_seeds.json
"$python" pcrecomp/tools/disasm/disasm32.py game/game.exe -o work/game/functions.json \
    --seed-functions work/game/rtti_seeds.json
printf 'Convert the game code into C.\n'
"$python" run_lift.py --target game --all
"$python" -c 'import json; s = json.load(open("work/game/lift_stats.json")); raise SystemExit(0 if s["errors"] == s["stubs"] == 0 else "Lift has errors or stubs.")'
printf 'Build the native Linux executable.\n'
BUILD_DIR=build-linux-game CMAKE_ARGS="-DRA2_TARGET=game -DCMAKE_BUILD_TYPE=Release" \
    ./build-linux.sh --parallel "${jobs:-$(nproc)}"
install -m 755 -- build-linux-game/ra2 "$stage/ra2"
install -m 755 -- "$source_dir/play.sh" "$stage/play.sh"

mkdir -p -- "$target/game" "$(dirname -- "$command_path")" "$(dirname -- "$desktop")"
touch -- "$target/.ra2-linux-install"
# Keep saves and preferences. Replace the build and its game assets.
while IFS= read -r name; do
    mv -fT -- "$stage/game/$name" "$target/game/$name"
done < "$stage/.installed-assets"
mv -fT -- "$stage/.installed-assets" "$target/.installed-assets"
mv -fT -- "$stage/ra2" "$target/ra2"
mv -fT -- "$stage/play.sh" "$target/play.sh"
if [[ ! -e "$target/game/RA2.INI" ]]; then
    printf '[Intro]\r\nPlay=no\r\n\r\n[Video]\r\nScreenWidth=800\r\nScreenHeight=600\r\n' > "$target/game/RA2.INI"
fi
ln -sfn -- "$target/play.sh" "$command_path"
# Escape desktop Exec field syntax, including percent signs in a path.
desktop_exec=${target//\\/\\\\\\\\}
desktop_exec=${desktop_exec//\"/\\\\\"}
desktop_exec=${desktop_exec//\$/\\\\\$}
desktop_exec=${desktop_exec//\`/\\\\\`}
desktop_exec=${desktop_exec//%/%%}
cat > "$desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Red Alert 2 (Native Linux)
Exec="$desktop_exec/play.sh"
Icon=applications-games
Terminal=false
Categories=Game;StrategyGame;
DESKTOP
printf 'Installed. Start Red Alert 2 from your desktop menu or run %s.\n' "$command_path"
