# Audio reader

`audio.h` returns signed 16-bit interleaved PCM. `Bag::open` takes the
contents of `AUDIO.IDX` and `AUDIO.BAG`. Read these through the MIX filesystem.
`decode_wav` reads PCM and Microsoft IMA ADPCM WAVE files, including the WAVE
fact sample count. `decode_aud` currently reads uncompressed AUD only.
Unsupported formats and malformed data throw `std::runtime_error`.

The index header and record sizes follow `0x004011C0`. Bag offsets and sizes
follow `0x004016F0`. Channel, width, compression and block fields follow
`0x00401640`. The WAVE format and ADPCM block size follow `0x00408610`.
The original delegates WAVE ADPCM to the Windows audio codec; this decoder
implements the corresponding IMA block format in portable C++.

The last stereo block of the installed `grexselb` sample is short. It has
four unmatched left-channel bytes. The reader emits 40,921 complete stereo
frames and drops those unmatched bytes. `0x00401C00` reads the stored byte
count, including short final blocks. Windows performed the original ADPCM
conversion; its treatment of unmatched channel bytes remains unverified.
FFmpeg 6.1.1 produces the same frame count from a WAVE wrapper around this
sample and reports an invalid final packet. Its decoded values differ slightly
from this reader. The exact Windows rounding rule remains unverified.

`find_sound` reads the named event's `Sounds` list. `find_theme` reads
`Sound`, `Scenario`, `Normal` and `Repeat`. Both accept the existing `IniFile`.
The caller controls random selection, event volume, priority, and playback.
The lookup functions do not implement all event and theme settings.
Compressed Westwood AUD formats remain unsupported. The installed voice bags
and tested EVA and music files use PCM or WAVE IMA ADPCM.

Run synthetic checks:

    c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g rebuild/src/audio/audio.cpp rebuild/src/audio/test_main.cpp -o /tmp/ra2_audio_test
    /tmp/ra2_audio_test

Run checks against the installed files:

    c++ -std=c++17 -O2 -Wall -Wextra -Werror -I rebuild/src/fs rebuild/src/audio/{audio,lookup,integration_test}.cpp rebuild/src/rules/ini_file.cpp rebuild/src/fs/{filename_hash,mix_archive,mix_bootstrap}.cpp -o /tmp/ra2_audio_integration
    /tmp/ra2_audio_integration /path/to/user/ra2/game
