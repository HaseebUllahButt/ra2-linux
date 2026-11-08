#!/bin/sh
# Add a user launcher for an existing native OpenRA RA2 installation.
set -eu

ROOT="${ROOT:-$HOME/ra2}"
MOD="$ROOT/openra/ra2"
CONTENT="${XDG_CONFIG_HOME:-$HOME/.config}/openra/Content/ra2"
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"

test -f "$MOD/engine/bin/OpenRA.dll"
test -x "$ROOT/runtime/dotnet"
for name in ra2.mix language.mix ra2md.mix langmd.mix thememd.mix; do
    test -s "$ROOT/game/$name"
done
if [ -s "$ROOT/game/THEME.MIX" ]; then
    THEME="$ROOT/game/THEME.MIX"
else
    THEME="$ROOT/game/theme.mix"
    test -s "$THEME"
fi

mkdir -p "$CONTENT" "$APPS" "$HOME/.local/bin"
for name in ra2.mix language.mix ra2md.mix langmd.mix thememd.mix; do
    ln -sfn "$ROOT/game/$name" "$CONTENT/$name"
done
ln -sfn "$THEME" "$CONTENT/theme.mix"

# Quote paths for the shell script, including paths with spaces or quotes.
quote() { printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\\\''/g")"; }
{
    printf '#!/bin/sh\nset -eu\n'
    printf 'ROOT=%s\n' "$(quote "$ROOT")"
    cat <<'LAUNCH'
export DOTNET_ROOT="$ROOT/runtime"
export PATH="$DOTNET_ROOT:$PATH"
export DOTNET_CLI_TELEMETRY_OPTOUT=1
# .NET 6 predates the ICU library in current Linux distributions.
if command -v ldconfig >/dev/null 2>&1; then
    icu_version=$(ldconfig -p 2>/dev/null | sed -n 's/.*libicuuc\.so\.\([0-9][0-9]*\) .*/\1/p' | head -n 1)
    if [ -n "$icu_version" ]; then
        export DOTNET_ICU_VERSION_OVERRIDE="$icu_version"
    fi
fi
exec "$ROOT/openra/ra2/launch-game.sh" "$@"
LAUNCH
} > "$HOME/.local/bin/ra2"
chmod +x "$HOME/.local/bin/ra2"

# Desktop Exec paths use a different quoting rule from shell commands.
LAUNCHER=$(printf '%s' "$HOME/.local/bin/ra2" | sed 's/\\/\\\\/g; s/"/\\"/g; s/`/\\`/g; s/\$/\\$/g; s/%/%%/g')
cat > "$APPS/ra2-native.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Red Alert 2
Comment=Play Red Alert 2 on the native OpenRA engine
Exec="$LAUNCHER"
Icon=$MOD/mods/ra2/icon.png
Terminal=false
Categories=Game;StrategyGame;
StartupNotify=false
DESKTOP
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$APPS"
fi
printf 'Installed Red Alert 2 in the application menu.\n'
