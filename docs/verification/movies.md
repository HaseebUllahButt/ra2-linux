# Bink movie verification

All 83 Bink movies in the tested installation play to the end and wrap with the original frame counts. The archive test plays two full cycles of each movie.

Two damaged clips contain data that FFmpeg refuses. The native player repeats the last good picture for that frame and keeps the frame count and timing. The original reads beyond the picture buffer there, so the affected pixels depend on memory outside the picture.

The final build also passes both startup runs under CPU load. The game reaches skirmish setup after about 263 seconds in each run, with three busy processes on its CPU core.

The release candidate replays all six selected movie-bearing missions after the fix. Soviet mission 7 passes its radar movie and reaches defeat at frame 7,902 with the same scenario random states and sampled units as the Wine recording. Audio timing against the original was outside these checks.
