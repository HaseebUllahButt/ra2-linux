#!/bin/sh
# One-shot Ghidra headless dump for gamemd.exe.
export JAVA_HOME=/path/to/user/rea-tools/jdk-21.0.13+9
export PATH="$JAVA_HOME/bin:$PATH"
GHIDRA_HOME=/path/to/user/rea-tools/ghidra_12.1.4_PUBLIC
exec "$GHIDRA_HOME/support/analyzeHeadless" \
  /path/to/user/ra2/analysis ghdump \
  -import /path/to/user/ra2/game/gamemd.exe \
  -scriptPath /path/to/user/ra2/scripts \
  -postScript DumpRa.java
