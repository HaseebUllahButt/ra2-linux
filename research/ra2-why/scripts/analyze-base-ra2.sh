#!/bin/sh
# Keep base RA2 evidence separate from the existing Yuri's Revenge analysis.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TOOLS="${TOOLS:-$HOME/rea-tools}"
export JAVA_HOME="$TOOLS/jdk-21.0.13+9"
export PATH="$JAVA_HOME/bin:$PATH"
mkdir -p "$ROOT/analysis/base-ra2"
if [ -f "$ROOT/analysis/base-ra2/base-ra2.gpr" ]; then
    set -- -process game.exe -noanalysis
else
    set -- -import "$ROOT/game/game.exe"
fi
"$TOOLS/ghidra_12.1.4_PUBLIC/support/analyzeHeadless" \
    "$ROOT/analysis/base-ra2" base-ra2 \
    "$@" \
    -scriptPath "$ROOT/scripts" \
    -postScript DumpRa.java "$ROOT/analysis/base-ra2/indexes" \
    -postScript ExportAll.java "$ROOT/rebuild/decomp-base"
test -s "$ROOT/analysis/base-ra2/indexes/functions.csv"
test -s "$ROOT/rebuild/decomp-base/index.csv"
