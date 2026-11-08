# MIX filesystem reconstruction

**Status: encrypted-header verification passes on the real installation.**
The default build contains Westwood's 40-byte RSA public modulus recovered
from the authorized local OpenRA `BlowfishKeyProvider.cs`. Its ASN.1 INTEGER
is identical to `[PublicKey] 1` in the 151-byte shipped INI described in
`analysis/RE-REPORT-2.md` (entry ID `0x763c81dd`). There is no build-time
modulus override or external crypto dependency.

Linux C++17 replacement of the gamemd.exe 1.001 MIX layer. All implementation,
fixtures, build outputs, and this document belong in `rebuild/src/fs/`.
Game files are opened in binary read-only mode. No original executable is run.

Build from the repository root (or the staged workspace root):

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -I rebuild/src/fs \
  rebuild/src/fs/filename_hash.cpp rebuild/src/fs/mix_archive.cpp \
  rebuild/src/fs/mix_bootstrap.cpp rebuild/src/fs/test_main.cpp \
  -o rebuild/src/fs/mix_test
rebuild/src/fs/mix_test --self-test
rebuild/src/fs/mix_test /path/to/user/ra2/game
```

No third-party libraries or linker flags are needed. `test_main.cpp` is the
only `main`; compiling `rebuild/src/fs/*.cpp` also works. The test prints raw
entry counts, including hashes without known filenames. Archive payloads are
read on demand; opening a movie archive does not load its data into memory.

## Format and lookup

* A nonzero first little-endian 16-bit word denotes a classic header. Otherwise
  the first four bytes are flags: SHA-1 trailer `0x00010000`, encrypted header
  `0x00020000`. Unknown extended flags are rejected.
* A directory contains a little-endian `uint16` count and `uint32` data size,
  followed by count records of `(uint32 id, uint32 offset, uint32 size)`.
  Offsets are relative to the data section, never the physical file start.
* Encrypted directories have an 80-byte RSA-wrapped key after the flags. Two
  little-endian 40-byte RSA blocks yield 39 bytes each; the first 56 combined
  bytes form the Blowfish key. RSA uses exponent 65537. Blowfish uses ECB with
  standard big-endian block words. The six-byte header and directory are
  rounded up together to eight bytes; data starts after that padding.
* Classic filename hashes fold ASCII to uppercase, rotate the accumulated
  value left one bit and add each zero-padded little-endian four-byte word.
  TS/RA2 CRC hashes fold ASCII to uppercase and use reflected IEEE CRC-32.
  A partial last word appends its byte count, then copies of its first byte.
  Hashing preserves directory separators and does not infer a hash mode from
  the archive flags. RA2 filesystem lookup defaults to CRC; classic lookup is
  explicit through `FilenameHash::Classic`.
* Every directory and payload read is bounded to its containing archive.
  Declared data sizes, integer arithmetic, truncated keys/headers, checksum
  trailer presence, and entry extents are checked. The checksum flag and
  20-byte trailer are supported structurally; SHA-1 contents are not verified.
  Trailing bytes and overlapping/duplicate directory entries are accepted.
  Duplicate hashes resolve to the first directory record. A separate lookup
  index accepts either signed or unsigned on-disk hash ordering.
* The memory-backed factory supports independent tests. File-backed nested
  archives retain a bounded physical range and share backing ownership.
  Reopening the backing file on each read avoids a shared mutable seek cursor;
  callers must keep the backing file unchanged for the archive's lifetime.

## Mounting and compatibility decisions

`MixFileSystem::bootstrap()` mounts existing `EXPANDMD99.MIX` down through
`EXPANDMD00.MIX`, followed by `RA2MD.MIX`, `RA2.MIX`, `CACHEMD.MIX`, `CACHE.MIX`,
`LOCALMD.MIX`, and `LOCAL.MIX`, in exactly that order. The latter six are
required. An archive can itself be found inside a previously mounted archive.

The linked-list insertion in `0x005B3C20`, the sentinel layout in `0x0052ACE0`,
and the traversal in `0x005B4430` establish first-mounted-wins precedence.
`locate()` follows that order. The higher-level `read_file()` and `mount()`
first check loose files, reflecting `0x00473D10`'s disk-availability check.
Case-insensitive path components are resolved on Linux; ambiguous case
collisions are reported. Absolute paths and parent components are rejected
by the relative filesystem APIs. `MixArchive::open()` accepts explicit paths.

`mount_overlays()` implements the separate `0x00530000` stage: `ECACHE*.MIX`
then `ELOCAL*.MIX`. The startup caller `0x0052BA60` invokes this after bootstrap
and before the resource stage. It is not silently moved ahead of expansion
or base archives. The original cache functions `0x005B43E0` and `0x005B43F0`
are unconditional-success stubs in the supplied executable.

`mount_resources()` mounts `CONQMD`, `GENERMD`, `GENERIC`, `ISOGENMD`, `ISOGEN`,
`CONQUER`, `CAMEOMD`, `CAMEO`, maps, `MULTIMD`, the available theme archive,
and movies. Required versus optional archives follow the visible return
checks. Installed mode discovers maps and movie groups on disk; media mode
uses the numbered names. Movie probes retain the original last-successful
probe preference, including `MIXFILES/` locations. The first map/movie archive
occupies a generic slot; later ones occupy the corresponding vector.

Windows wildcard order is unspecified. Linux discovery sorts ASCII-insensitive
names ascending for reproducibility. Hidden/system/archive attributes from
Win32 are not reproduced; regular files are considered. Wildcard literals
represented only as `DAT_...` in the decompilation are inferred as `MAPSMD*`,
`MAPS*`, and `MOVMD*` from their numbered siblings and branch behavior. The
maps fallback accepts base-game maps when no expansion maps exist. This
repairs an apparent pointer-slot inconsistency in the supplied decompilation.
Media filenames use the clearly identified formatted buffer, repairing
Ghidra's overlapping stack-array artifacts. These are documented portability
and reconstruction decisions, not claims of byte-for-byte equivalence.

`mixes()`, `array()`, `maps()`, `movies()`, and `generics()` expose modern owned
containers corresponding to the original global storage. Language, audio,
theater, side, and loading-screen mounting belong to other startup functions;
this module permits explicit `mount()` calls for those archives and does not
invent their order within the three reconstructed stages. CD prompts, COM,
Windows messages, theme-duration probing, and process shutdown are outside
this filesystem layer.

## Original address to new file/function

Addresses are preferred virtual addresses, not RVAs. Rows marked "adapter"
replace infrastructure relevant to the cleaned MIX functions, not unrelated
behavior of the original general-purpose class. New validation, test helpers,
and classic compatibility hashing do not claim recovered original addresses.

| Original VA | Reconstructed responsibility | New location |
| --- | --- | --- |
| `0x005301A0` | MIX bootstrap and descending expansion loop | `mix_bootstrap.cpp`: `MixFileSystem::bootstrap` |
| `0x00530000` | ECACHE/ELOCAL wildcard mounting | `mix_bootstrap.cpp`: `MixFileSystem::mount_overlays` |
| `0x00530460` | Generic, maps, theme and movie archive mounting | `mix_bootstrap.cpp`: `MixFileSystem::mount_resources` |
| `0x005B3C20` | MIX construction, flags, directory and data start | `mix_archive.cpp`: `MixArchive::open`, constructor, `parse`; registration in `mix_bootstrap.cpp`: `mount` |
| `0x005B4430` | Hash lookup across mounted MIXes | `mix_bootstrap.cpp`: `MixFileSystem::locate`; `mix_archive.cpp`: `MixArchive::find` |
| `0x005B3F50` | MIX destruction and list removal | `mix_archive.h/.cpp`, `mix_bootstrap.h`: RAII ownership of directories/backing and mounting containers |
| `0x005B4590` | Detach list node before insertion | `mix_bootstrap.cpp`: `mount` with owned ordered containers; no intrusive nodes |
| `0x0052ACE0` | First/last linked-list sentinels | `mix_bootstrap.h`: `mixes_` ordered vector |
| `0x005B43E0`, `0x005B43F0` | Unconditional-success cache stubs | `mix_bootstrap.cpp`: no-op behavior in bootstrap/overlay/resource mounting |
| `0x004739F0`, `0x0065CA80` | Named CC/raw-file construction (adapter) | `mix_archive.cpp`: `MixArchive::open`; `mix_bootstrap.cpp`: `loose_path` |
| `0x00473C50` | File/archive availability (adapter) | `mix_bootstrap.cpp`: `mount`, `locate` |
| `0x00473D10` | Loose or nested read-only opening (adapter) | `mix_bootstrap.cpp`: `mount`, `read_file`; `mix_archive.cpp`: `open_nested` |
| `0x00473B10` | Read archive-backed bytes (adapter) | `mix_archive.cpp`: `read`, `read_range` |
| `0x00473BA0` | Bounded archive-relative seek (adapter) | `mix_archive.cpp`: `read_range`, `open_nested`, data offsets |
| `0x007BA530` | File-to-stream reading (adapter) | `mix_archive.cpp`: `read_range` |
| `0x0065CA00`, `0x007BA590`, `0x006C9850` | File/stream close and detach (adapter) | `mix_archive.cpp`: scoped `std::ifstream`, shared backing ownership |
| `0x00633050`, `0x006330C0`, `0x00633110` | Encrypted stream construction, attachment and key setup | `mix_archive.cpp`: `parse`, `decode_key`, `Blowfish` |
| `0x00632870` | RSA block decoding for the symmetric key | `mix_archive.cpp`: `decode_key` |
| `0x005C8500` | Modular exponentiation (MIX-used subset) | `mix_archive.cpp`: fixed-width modular arithmetic and `decode_key` |
| `0x00438210` | Read/decrypt eight-byte stream blocks | `mix_archive.cpp`: `Blowfish::decrypt`, `parse` |
| `0x00437FD0`, `0x00438030` | COM key setup/decrypt forwarding | `mix_archive.cpp`: standalone `Blowfish` constructor/decrypt |
| `0x004A1DE0` | CRC hash including partial-word padding | `filename_hash.cpp`: `crc_filename_hash` |
| `0x004A1FB0` | CRC-32 update | `filename_hash.cpp`: `crc_byte` |
| `0x007DCFC4` | Uppercase conversion (ASCII path) | `filename_hash.cpp`: `upper` |
| `0x0047AF70`, `0x0047B0C0`, `0x0047B130` | Wildcard enumeration and handle lifetime | `mix_bootstrap.cpp`: `discover` with scoped directory iterators |

| Original storage address | Community name | New owner |
| --- | --- | --- |
| `0x00884D90` | `MixFileClass::Array` | `mix_bootstrap.h`: `array_` |
| `0x00ABEFD8` | `MixFileClass::MIXes` | `mix_bootstrap.h`: `mixes_` |
| `0x00884DA8` | `MixFileClass::Maps` | `mix_bootstrap.h`: `maps_` |
| `0x00884DE0` | `MixFileClass::Movies` | `mix_bootstrap.h`: `movies_` |
| `0x00884DF8` | `MixFileClass::Generics` | `mix_bootstrap.h`: `generics_` |

## Evidence and boundaries

Read `analysis/community-crosscheck.md`, `analysis/engine-loop-network.md`,
the named decompilations and MIX-relevant dependencies. The engine-loop report
warns that indexed direct calls omit virtual dispatch; accordingly encrypted
stream handling and nested-file lookup were also followed through their
adjacent decompiled virtual methods. `call_edges_reviewed.csv` records the
indexed edges of the four primary functions and the additional lookup,
decryption, RSA and hash functions, including CRT and external boundaries.
CRT allocation/string/formatting and Windows shutdown dependencies were read
but are not presented as rebuilt engine functions.

Fetched read-only from the permitted host:

* https://raw.githubusercontent.com/Ares-Developers/YRpp/master/MixFileClass.h
* https://raw.githubusercontent.com/Ares-Developers/YRpp/master/GenericList.h
* https://raw.githubusercontent.com/Ares-Developers/YRpp/master/FileSystem.h

YRpp confirms the community names, six requested addresses, header record,
node layout, generic slots, and the constructor's extra key argument.
The supplied signature at `0x005B3C20` is visibly misrecovered; callsites and
YRpp establish the filename and key inputs. Fields labeled Blowfish/Encryption
by YRpp are interpreted from actual flag bits, not assumed semantic names.

Format offsets and encrypted-header padding were cross-checked against
`openra/ra2/engine/OpenRA.Mods.Cnc/FileSystem/MixFile.cs`. The code here is a
fresh C++ reconstruction rather than a translation of the OpenRA reader.
Blowfish's initial words are standard hexadecimal pi digits, generated locally
with Chudnovsky at 10200 decimal digits, with first/last-word checks.

## Current verification

* The documented strict C++17 build passes, with `-Werror`.
* `mix_test --self-test` passes hash vectors, both header formats, every
  truncation of small fixtures, malformed offsets/sizes/flags, checksum trailer
  presence, and nested archive lifetime/bounds.
* The standalone primitive test passes the all-zero Blowfish key/plaintext
  known answer, modular arithmetic identities across the full 320-bit width,
  and rejection of wrapped key lengths other than 80 bytes. Build separately
  because it includes the implementation:

  ```sh
  g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -I rebuild/src/fs \
    rebuild/src/fs/crypto_test.cc rebuild/src/fs/filename_hash.cpp \
    -o rebuild/src/fs/crypto_test
  rebuild/src/fs/crypto_test
  ```

* `mix_test --archive PATH` opens and prints one archive's directory count.
  The requested encrypted archives produce:

  | Archive | Entries | Data start |
  | --- | ---: | ---: |
  | ra2.mix | 21 | 348 |
  | ra2md.mix | 25 | 396 |
  | expandmd01.mix | 33 | 492 |
  | multimd.mix | 173 | 2172 |

* `mix_test /path/to/user/ra2/game` passes all 16 top-level MIX archives
  (13 encrypted), 21 mounts, and reads the 743,215-byte `RULESMD.INI`.
  The integration harness explicitly mounts `LANGMD.MIX` and `LANGUAGE.MIX`
  before the resources stage to supply their nested cameo archives. This
  supplies language initialization for the test without changing bootstrap
  or resource mounting semantics.
* `python3 rebuild/src/fs/verify_encrypted.py` independently decodes the
  extracted INI's base64 ASN.1 public modulus, checks it against OpenRA's
  `PublicKeyString`, and recovers all four 56-byte Blowfish keys using Python's
  arbitrary-precision `pow`. Each key matches the C++ output byte for byte.
  The test also reads and compares the actual embedded `[PublicKey]` values
  from `ra2.mix`, `ra2md.mix`, and `expandmd01.mix`, then checks each archive's
  expected count and padded data start. It never uses the INI's private value.

The modulus is 319 significant bits in 40 big-endian bytes:

```text
51bcda086d39fce4565160d651713fa2e8aa54fa6682b04aabdd0e6af8b0c1e6d1fb4f3daa437f15
```

The exponent is 65537. OpenRA's `pubkey.Len = bit_length - 1` followed by
`a = (pubkey.Len - 1) / 8` gives `a = 39`: decrypt two 40-byte little-endian
integers, concatenate the low 39 little-endian result bytes from each, and
keep the first 56 bytes. OpenRA's `Blowfish.cs` confirms the standard
big-endian block words and key schedule already implemented here.

An optional independent reference harness compiles the **unmodified** local
GPLv3-or-later OpenRA `BlowfishKeyProvider.cs` and `Blowfish.cs` by reference.
No OpenRA source files were copied into this directory. The C++ reader does
not link this harness or require .NET. Reproduce the direct comparison with:

```sh
DOTNET_CLI_HOME="$PWD/rebuild/src/fs/openra-reference/.dotnet" \
  NUGET_PACKAGES="$PWD/rebuild/src/fs/openra-reference/.nuget" \
  DOTNET_GENERATE_ASPNET_CERTIFICATE=false DOTNET_CLI_TELEMETRY_OPTOUT=1 \
  dotnet build rebuild/src/fs/openra-reference/Reference.csproj -c Release \
  --configfile rebuild/src/fs/openra-reference/NuGet.Config
python3 rebuild/src/fs/verify_encrypted.py \
  --openra-reference rebuild/src/fs/openra-reference/bin/Release/net8.0/Reference.dll
```

The optional reference requires the local .NET 8 SDK and uses no package
sources. Override MSBuild's `OpenRaFormats` property for a different local
OpenRA source directory, and `verify_encrypted.py --repo` for different input
roots. All four reference keys and decrypted header counts match the C++
reader and INI-based Python calculation. Actual output is in `verification.txt`.
