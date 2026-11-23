Run from the repository root:

```sh
python3 tools/xverify/driver.py prepare extend_x87 calibrate build execute
```

The driver uses at most eight build or test workers. It writes generated game-derived C and raw results to `build/xverify/`.
The empty `.trackerignore` file excludes that output from tracking.
The reference trees and both source runtimes stay unchanged.

The function list comes from `all-addresses.txt`. The x87 set includes every detected x87 function.
The integer sample uses a fixed seed and 1,000 functions from the safe-closure frame.
A closure must contain at most eight functions.
The integer sample limits each member to 400 instructions.
The x87 set has no instruction-count limit and accepts known internal jump tables.
The filter excludes unknown jumps, unknown calls, incomplete decoding, thread segments, and system instructions.
The manifest records all selected addresses and exclusions.
A second scan checks x87 depth across branches and calls.
Unknown recursive depth means cannot test.

Each function gets 200 repeatable states under each precision setting.
The native code and both C bodies receive the same registers, stack words, scratch contents, and x87 inputs.
Scratch words cycle through zeros, finite floats, pointers, and small integers.
These are memory-valid inputs. They do not establish valid game objects or all function preconditions.

The adapter sends calls from either lifter to original native bytes.
The reference resolver always returns a native bridge for calls allowed by the closure filter.
The closure filter checks the callees. Callees are not part of the function under test.
This measures each emitted function with native callees. It does not measure complete lifted closures.

A separate process tests each function. An alternate signal stack catches synchronous faults.
A 20 ms CPU-time call limit prevents loops from holding a worker. Timeouts mean cannot test.
The signal number and signal code define a matching fault.
Memory faults must also have the same access address. Matching faults never count as exact.
The process resets scratch, guest stack, data, registers, and FPU state before each call.
It compares EAX, EDX, occupied x87 slots as ten-byte values, scratch hashes, and changed-data hashes.
Write protection records changed data pages.
The data hash covers those pages and their offsets, against a shared starting image.
Every 64th returned snapshot also checks the tracking against the full data range.
It covers the same data range as `special_main.c`: 0x7c9000 through 0xb2d000.
It does not compare guest stack output, flags, FPU status, or other registers.

Function counts use this order: any difference; any matching fault; any timeout or unavailable body; otherwise exact.
Thus an exact function returns and agrees on every state under both precision settings.
Raw state counts remain in `results.json`. Each run log includes the first three differences per lifter.

Run `python3 tools/xverify/summarize.py` to check state totals and print counts for each precision.
