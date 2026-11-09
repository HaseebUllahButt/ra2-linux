# Portable INI and General rules reader

Clean C++17 for Linux g++, with no game binary, Windows ABI, original object
layout, or third-party library dependency. All new source and test artifacts
live in this directory. Inputs under `rebuild/decomp/`, `analysis/`, and the
extracted config directory are read-only.

## Build and run

From `/tmp/r2` (the workspace symlink):

```sh
TMPDIR=/tmp/r2/rebuild/src/rules g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror \
  -I rebuild/src/rules \
  rebuild/src/rules/ini_file.cpp rebuild/src/rules/rules_reader.cpp \
  rebuild/src/rules/test_main.cpp -o rebuild/src/rules/rules_test
rebuild/src/rules/rules_test --verify-stock
```

Running with no arguments also validates the stock `rulesmd.ini`. An explicit
path parses another INI without stock-specific expectations. Add
`--list-deferred` to enumerate General entries outside the typed slice. The
executable always runs parser and overlay regression checks, then prints the
requested file's key counts. Exit codes: 0 success, 1 I/O/check/argument failure,
2 invalid supported General values. Build products can be removed after testing;
they are not inputs to the reader. `TMPDIR` keeps compiler scratch writes here.

Verified output for `analysis/extracted/config/rulesmd.ini`:

```text
section headers=1478 nonempty sections=1452 keys=23176
[General] keys=378
[InfantryTypes] keys=65
[VehicleTypes] keys=80
[AircraftTypes] keys=12
[BuildingTypes] keys=403
[Warheads] keys=105
[SuperWeaponTypes] keys=12
General schema=253 loaded=239 retained=14 invalid=0 deferred=139
```

The file has 26 empty sections, which the original text reader discards when
comment retention is disabled. Counts include actual nonempty entries, rather
than INI comments or blank assignments. This fixture contains no repeated
sections or keys. An independent Python scan of the extracted bytes established
the counts before the C++ implementation was written.

## API

```cpp
#include "ini_file.h"
#include "rules_reader.h"

const auto ini = ra2::rules::IniFile::from_file("analysis/extracted/config/rulesmd.ini");
ra2::rules::RulesReader rules;
const auto report = rules.init(ini);
const auto repair_step = rules.get<std::int32_t>("RepairStep"); // optional: 8
const auto repair_cost = rules.get<double>("RepairPercent");   // optional: .15
const auto bays = ini.read_list("General", "RepairBay");      // names, unresolved
// rules.read_file(mode_ini); then rules.read_file(map_ini); retain absent values.
```

`IniFile` owns section/entry strings and preserves file order, source line numbers,
and duplicate blocks. It exposes counts, lookup, ordered key access, string,
integer, boolean, percentage/real, and comma-list reads. Returned entry pointers
remain valid until the document is replaced. File/stream I/O failures throw and
leave the prior document intact. Parsing a new document replaces prior content;
overlay behavior belongs to `RulesReader`, after each INI is parsed separately.

`RulesReader::general_schema()` is the reviewable inventory: exact INI key,
reader kind, original Rules-object byte offset, recovered constructor default,
and fallback-field offset. Offsets are metadata for tracing schema entries;
there is no access to addresses in the old executable. The fields comprise
103 integers, 1 byte, 16 booleans, 24 floats, 67 doubles, and 42 integer lists.
Both floating field kinds are exposed as `double`; float fields are narrowed
after percentage scaling to preserve their original storage precision.

`RuleValue` uses `monostate` when a constructor default cannot be established:
`FillEarliestTeamProbability`, `TotalAITeamCap`, `AICaptureLowMoneyMark`,
`AICaptureWoundedMark`, and `TiberiumTransmogrify`. Typed getters return an empty
optional for those values until supplied. Other defaults come from literal
assignments in `0x665650`: paired little-endian words were decoded to IEEE
numbers, not inferred from the shipped INI. `init()` restores this constructor
slice; `read_file()` applies the General slice without resetting it. A missing
General section changes nothing. Invalid/overflowing conversions retain the
previous value and are reported. Decimal atoi-style garbage still becomes zero,
as in the source. `retained` counts fields taking their fallback, including
invalid fields; `loaded` counts successfully converted present schema entries.

The apparent original bug where `CMislAcceleration` falls back to the already
loaded `DMislAcceleration` is preserved, even when the CMisl key is absent.
`EngineerCaptureLevel` exposes the first physical field at `+0x17F8`; the
original also reads the same key into `+0x17FC`, which is not separately modeled.

## Address map and evidence

Community names were fetched read-only on 2026-11-08 from these YRpp headers:

- [RulesClass.h](https://raw.githubusercontent.com/Phobos-developers/YRpp/master/RulesClass.h)
- [CCINIClass.h](https://raw.githubusercontent.com/Phobos-developers/YRpp/master/CCINIClass.h)

These mutable `master` URLs supply names, not the implementation. The local
`analysis/community-crosscheck.md` corroborates the Ares YRpp names and corrects
Rules/Scenario pointer confusion. `analysis/engine-loop-network.md` supplies
engine context and cautions about incomplete indexes and recovered prototypes;
no loop or networking code is modified or required by this layer.

Every cleaned address, including partially represented entry points, maps below.
Each function's implementation evidence is
`rebuild/decomp/AAAA/FUN_AAAAAAAA.c`, where `AAAA` is its first four address digits.
Data storage addresses have no corresponding function file.

| Original VA | Community/descriptive name | New file and counterpart | Extent |
| --- | --- | --- | --- |
| `0x004741F0` | `CCINIClass::ReadCCFile` | `ini_file.cpp`: `from_file`, `load_file` | Text-loading path only; digest validation omitted |
| `0x00525A60` | Underlying INI stream parser | `ini_file.cpp`: `parse`, `parse_into` | Sections, entries, comments, whitespace, empty-entry filtering |
| `0x0065D5C0` | Physical line reader | `ini_file.cpp`: `parse_into` | LF records, discarded CR bytes; safe unbounded storage |
| `0x00727CF0` | ASCII/control-space trim | `ini_file.cpp`: `trim` | Leading/trailing bytes at or below `0x20` |
| `0x00526810` | `INIClass::GetSection` | `ini_file.cpp`: `find_section` | Exact name lookup |
| `0x00526960` | `INIClass::GetKeyCount` | `ini_file.cpp`: `key_count` | Entry count per selected section |
| `0x00526CC0` | `INIClass::GetKeyName` | `ini_file.cpp`: `key_at` | Ordered entry access with bounds checking |
| `0x00528A10` | `INIClass::ReadString` | `ini_file.cpp`: `read_string` | Trimmed owning string; no fixed destination buffer |
| `0x005276D0` | `INIClass::ReadInteger` | `ini_file.cpp`: `integer_value`, `read_integer` | Decimal prefix, `$` hex, trailing `h`/`H` hex |
| `0x005283D0` | `INIClass::ReadDouble` | `ini_file.cpp`: `double_value`, `read_double` | Float scan, promotion, percent scaling |
| `0x005295F0` | `INIClass::ReadBool` | `ini_file.cpp`: `bool_value`, `read_bool` | First-character recognition and default |
| `0x00475D70` | Decimal integer-list read | `rules_reader.cpp`: `read_general` IntegerList branch | 511-character payload, comma tokens, decimal atoi conversion |
| `0x00665650` | `RulesClass` constructor | `rules_reader.cpp`: schema defaults and `RulesReader()` | Defaults for the supported General fields only |
| `0x006686C0` | `RulesClass::Init` | `rules_reader.cpp`: `init` | First-file reset/load for this slice only |
| `0x00668BF0` | `RulesClass::Read_File` | `rules_reader.cpp`: `read_file` | General dispatch only |
| `0x0066D530` | `RulesClass::Read_General` | `rules_reader.cpp`: `general_schema`, `read_general` | 253 typed keys with retained defaults and a deferred-key report |
| `0x008871E0` | `RulesClass::Instance` pointer storage | `rules_reader.cpp`: `instance` | Function-local portable instance; original address is never accessed |

Public declarations for each counterpart are in the matching `.h` file.
`test_main.cpp` supplies the standalone executable and regression checks.

Direct edges were checked in `analysis/ghidra-dump/call_edges.csv` and the
corresponding functions were read, rather than assigning names by proximity:

```text
004741F0 -> 00525A60 -> 0065D5C0 / 00727CF0
006686C0 -> 00668BF0 -> 0066D530
0066D530 -> 00526810 / 00528A10 / 005276D0 / 005283D0 / 005295F0 / 00475D70
```

The parser's name lookup uses byte-sensitive CRC calls to `0x004A1DE0` without
case folding; the new implementation uses exact string indexes. The original
checksum implementation was inspected to establish this behavior, but its CRC
and collision behavior are not ported. Long string symbols in the decompilation
are truncated at 31 characters; full keys in the schema are expanded using the
extracted INI and the community Rules field names.

## Proven behavior and remaining boundaries

The text reader accepts preamble/noise, trims control/ASCII whitespace around
assignments, splits at the first `=`, strips `;` comments even inside quotes,
retains quotes and literal `#`, ignores empty keys/values, supports CRLF/LF and
an unterminated final line, and preserves spelling. Whitespace inside section
brackets is part of the name. It implements no escaping, interpolation, or
section inheritance. Boolean first characters are `0/F/N` and `1/T/Y`, ignoring
ASCII case. A real value is scanned as a float before promotion; `.7` therefore
becomes `double(float(.7))`. Any `%` in the value scales it by `.01`.

Duplicate sections/keys remain ordered and are counted. Lookup deliberately
chooses the first occurrence. The original stores duplicate CRC-index entries
then sorts/binary-searches them, so precedence for equal hashes is not established
by this dump. This deterministic policy is an explicit extension, not a claim of
binary equivalence. Exact string comparison also avoids the original CRC aliases.
The new reader does not reproduce fixed line-buffer truncation, allocator failure
paths, comment-retention mode, nonfinite/malformed float UB, integer overflow UB,
or digest/hash checking. It accepts arbitrarily long physical lines safely.
Integer-list payloads retain the proven 512-byte ReadString bound.

`ReadCCFile` actually returns an integer status in the dump, despite YRpp's
pointer-shaped declaration. With optional verification it removes `[Digest]`,
computes/checks a 20-byte digest, and may return status 2. This layer implements
the ordinary text path with C++ exceptions for I/O, and makes no digest claim.

The original `Init` destroys global type arrays, reads Maximums/Players, invokes
Read_File, and may load LANGRULE.INI and a scenario overlay. Original Read_File
also loads colors, countries and type-name lists, other rules sections, and
session-dependent settings. None of those side effects is represented by these
slice wrappers. `0x887048` (CCINI rules pointer) and `0xA8B230` (Scenario pointer)
are distinct from Rules storage `0x8871E0`.

Type-reference resolution, prerequisites, animations, particles, and other
registry-dependent General fields are deferred. The type-list call edges were
followed to `0x672280`, `0x672360`, `0x6723D0`, `0x672440`, and `0x672660`;
these allocate/resolve game objects and are inspected boundaries, not cleaned
routines. Related General reference helpers at `0x67BAC0`, `0x67BB10`, and
`0x67BD30` likewise depend on registries. Their raw names/lists remain readable
through `IniFile`, e.g. `RepairBay=GADEPT,NADEPT,CAOUTP` after comment stripping.
The deferred report can also include keys the original General function does
not read; it is not a list of diagnosed errors.

Conversions at `0x474620` are deferred: CameraRange, TiberiumShortScan,
TiberiumLongScan, SlaveMinerShortScan, SlaveMinerSlaveScan, SlaveMinerLongScan,
SlaveMinerScanCorrection, AISuperDefenseDistance, Stray, RelaxedStray,
GuardModeStray, and CloseEnough. The dump loses the FP operands preceding its
integer conversion, so a scale/rounding policy cannot be proven here. The same
reason defers PrismSupportModifier, V3RocketRaiseRate, DMislRaiseRate,
CMislRaiseRate, and TalkBubbleTime inside Read_General. No guessed conversions
are exposed as engine values.

## Validation performed

The strict g++ build above and an AddressSanitizer/UndefinedBehaviorSanitizer
build both passed the stock fixture and parser/overlay regression checks.
An independent scanner additionally compared **every ordered section/key/value
triple**, byte for byte, with the C++ parser for all five extracted INIs:

| Fixture | Nonempty sections | Nonempty entries |
| --- | ---: | ---: |
| `keyboard.ini` | 1 | 110 |
| `public-key.ini` | 2 | 2 |
| `spawn.ini` | 162 | 966 |
| `soundmd.ini` | 821 | 3,765 |
| `rulesmd.ini` | 1,452 | 23,176 |

The regression executable covers comments, CRLF/EOF, case-sensitive names,
empty entries/sections, duplicate diagnostics and deterministic precedence,
ordered access, numeric/percentage/boolean/list conversion, long lines,
transactional I/O failure, constructor defaults, invalid-value fallback,
overlays, reset behavior, byte narrowing, and the cross-field missile fallback.
These are source/fixture checks; no original executable was accessed or run.
