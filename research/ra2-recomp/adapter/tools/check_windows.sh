#!/usr/bin/env bash
# Run API checks and compare every RGB565 colour. The caller owns the hidden display.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root/build"
wine test_ddraw.exe
wine test_display.exe
export WINEDLLOVERRIDES='dsound=n;winepulse.drv,winealsa.drv=d'
export RA2_AUDIO_CAPTURE=synthetic-hle-audio.pcm
rm -f "$RA2_AUDIO_CAPTURE"
wine test_dsound.exe
python3 "$root/tools/check_audio_capture.py" "$RA2_AUDIO_CAPTURE"
unset RA2_AUDIO_CAPTURE
wine test_bink.exe binkw32.dll WESTLOGO.BIK
wine test_bink.exe binkw32.dll S13_F01E.BIK
"$root/tests/capture_color.sh" wine
"$root/tests/capture_color.sh" adapter
convert color-wine.png -depth 8 rgba:color-wine.rgba
convert color-adapter.png -depth 8 rgba:color-adapter.rgba
python3 "$root/tools/check_color_pattern.py" color-wine.rgba color-adapter.rgba
