"""Fast mode for the ORIGINAL game.exe 1.006: remove the frame wait, keep all game logic.

How the original game waits (export FUN_0053fbd0 + FUN_00540960):
  - each pass of the main loop sets a frame timer: start = timeGetTime()>>4 (16 ms ticks),
    length = Game Speed (int at 0x00a40b18; skirmish only, campaign forces 2).
  - FUN_00540960 then Sleep()s until that many 16 ms ticks have passed.
  - Speed 3 = 3 ticks = 48 ms per frame = 20.8 frames per second (REFERENCE.md measured 20.75).
  - Speed 0 = length 0 = no wait (the skirmish slider shows 6 - speed, so 0 is the slider's "fastest" end).
No other code reads 0x00a40b18 during a skirmish battle (other readers: option screens, INI save,
network frame-rate setup, stats packet). So writing 0 there removes only the wait.

Measured frame rate (the second real-time input):
  FUN_00540960 counts frames per 60 ticks (~0.96 s) and stores the count at 0x00a6eb1c (code 00540BB5).
  FUN_0053d840 turns "low frame rate" on below 15 and off again at 20 or more. When it is on, effect code
  (FUN_005ddb90, FUN_00533c80, FUN_0047f600, FUN_006a7e80) skips work. Some of that work uses the shared random
  numbers, so the match changes. So the original game already plays differently when the machine is slow
  (REFERENCE.md saw 17-18 fps under load), and fast mode (hundreds of fps) would never be "low".
  pin_fps() replaces the store at 00540BB5 with 5 NOPs and writes 20, the count a Speed 3 game measures
  on a machine that keeps up. Then the match does not depend on real time at any speed.

Random seed: FUN_00515710 seeds the game from GetTickCount() unless the int at 0x00a40d40 is not 0.
Write a value there before Start Game to get the same match every time. 0x00a40d3c holds the seed used.
"""
GAME_SPEED = 0xa40b18
CUSTOM_SEED = 0xa40d40
SEED = 0xa40d3c
NORMAL_SPEED = 3
FPS_SPEED3 = 1000 / 48          # nominal game frames per second at Game Speed 3


def set_fast(m, on=True, normal=NORMAL_SPEED):
    """m: orig_census Mem of our own game.exe child."""
    m.write_i32(GAME_SPEED, 0 if on else normal)
    return m.i32(GAME_SPEED)


FPS_STORE = 0x540bb5            # a3 1c eb a6 00 : mov ds:0xa6eb1c, eax
FPS_STORE_BYTES = bytes.fromhex('a31ceba600')
FPS = 0xa6eb1c
FPS_LOW_FLAG = 0xa6eb28


def pin_fps(m, value=20):
    cur = m.read(FPS_STORE, 5)
    if cur == FPS_STORE_BYTES:
        m.f.seek(FPS_STORE); m.f.write(b'\x90' * 5)
    elif cur != b'\x90' * 5:
        raise RuntimeError(f'unexpected code at {FPS_STORE:#x}: {cur.hex()}')
    m.write_i32(FPS, value)
    m.f.seek(FPS_LOW_FLAG); m.f.write(b'\0')
    return m.read(FPS_STORE, 5) == b'\x90' * 5 and m.i32(FPS) == value
