#!/bin/sh
# Compile the Ghidra dump script against the Ghidra install.
set -e
JDK=/path/to/user/rea-tools/jdk-21.0.13+13/bin
JDK=/path/to/user/rea-tools/jdk-21.0.13+9/bin
G=/path/to/user/ghidra_12.1.4_PUBLIC
G=/path/to/user/rea-tools/ghidra_12.1.4_PUBLIC
CP=""
add() { CP="$CP:$G/$1"; }
add "Ghidra/Framework/Utility/lib/Utility.jar"
add "Ghidra/Framework/Generic/lib/Generic.jar"
add "Ghidra/Framework/Project/lib/Project.jar"
add "Ghidra/Framework/SoftwareModeling/lib/SoftwareModeling.jar"
add "Ghidra/Framework/Docking/lib/Docking.jar"
add "Ghidra/Features/Base/lib/Base.jar"
add "Ghidra/Features/Decompiler/lib/Decompiler.jar"
CP=${CP#:}
mkdir -p /tmp/jcheck
"$JDK/javac" -cp "$CP" -d /tmp/jcheck "$1"
echo JAVAC_OK
