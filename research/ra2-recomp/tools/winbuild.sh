#!/bin/sh
# Build rc.dll on the test-host (lift all + compile, 12 cores) and copy it to test-host.
# usage: tools/winbuild.sh [out-dir=build/win] [extra RC_CFLAGS]
R=$(cd "$(dirname "$0")/.." && pwd); O=${1:-build/win}
"$R/tools/test-host_run.sh" "python3 tools/liftall.py 12 | head -2 && RC_CFLAGS='$2' RC_JOBS=12 python3 tools/mkdll.py $O 2>&1 | tail -2 && remote tool copy why $O/rc.dll --target-folder $R/$O >/dev/null && remote tool copy why $O/rclaunch.exe --target-folder $R/$O >/dev/null && remote tool copy why $O/funcs.txt --target-folder $R/$O >/dev/null && echo FETCHED" 2>&1 | grep -v "Direct WebRTC"
