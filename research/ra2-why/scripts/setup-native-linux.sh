#!/bin/sh
# Native Linux Red Alert 2 via OpenRA + the community RA2 mod.
# Runs on x86 and ARM64 Linux. Needs: git, curl, make, python3, rsync.
# Your original game files must be in $GAME_DIR.
set -eu

GAME_DIR="${GAME_DIR:-$HOME/ra2/game}"
ROOT="${ROOT:-$HOME/ra2/openra}"
TOOLS="${TOOLS:-$HOME/rea-tools}"

echo "1/6 .NET 8 SDK (local, no root)"
mkdir -p "$TOOLS"
curl -fsSL https://dot.net/v1/dotnet-install.sh -o /tmp/dotnet-install.sh
chmod +x /tmp/dotnet-install.sh
"/tmp/dotnet-install.sh" --channel 8.0 --install-dir "$TOOLS/dotnet"
"/tmp/dotnet-install.sh" --channel 6.0 --install-dir "$TOOLS/dotnet"
export PATH="$TOOLS/dotnet:$PATH"
export DOTNET_ROOT="$TOOLS/dotnet"
export DOTNET_CLI_TELEMETRY_OPTOUT=1

echo "2/6 RA2 mod repo (pins its own OpenRA engine)"
mkdir -p "$ROOT"
[ -d "$ROOT/ra2" ] || git clone --depth 1 https://github.com/OpenRA/ra2.git "$ROOT/ra2"
cd "$ROOT/ra2"

echo "3/6 Fetch pinned engine + build"
./fetch-engine.sh
make all

echo "4/6 Content folder from your game files"
# OpenRA on Linux uses ~/.config/openra when it exists, else ~/.openra.
# Create the links in BOTH locations and skip files the install lacks
# (a broken link makes the content check fail).
for CONTENT in "${HOME}/.config/openra/Content/ra2" "${HOME}/.openra/Content/ra2"; do
    mkdir -p "$CONTENT"
    for f in ra2.mix language.mix ra2md.mix langmd.mix thememd.mix theme.mix; do
        if [ -f "$GAME_DIR/$f" ]; then
            ln -sfn "$GAME_DIR/$f" "$CONTENT/$f"
        else
            rm -f "$CONTENT/$f"
        fi
    done
done

echo "5/6 Done. Play:"
echo "   cd $ROOT/ra2 && ./launch-game.sh"
echo "6/6 Dedicated server:"
echo "   cd $ROOT/ra2 && Mod=ra2 ./launch-dedicated.sh"
