#!/usr/bin/env python3
"""Cross-check the real MIX keys with the INI public modulus and Python pow.

Uses only the standard library and the two separately built C++ test binaries.
All archive and reference inputs are opened read-only.
"""

import argparse
import base64
import configparser
from pathlib import Path
import re
import subprocess


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path("/path/to/user/ra2"))
    parser.add_argument("--openra-reference", type=Path,
                        help="optional compiled reference DLL for byte comparison")
    args = parser.parse_args()
    binaries = Path(__file__).resolve().parent
    ini = configparser.ConfigParser()
    with (args.repo / "analysis/extracted/config/public-key.ini").open() as stream:
        ini.read_file(stream)
    public = ini["PublicKey"]["1"]
    source = (args.repo / "openra/ra2/engine/OpenRA.Mods.Cnc/FileFormats/BlowfishKeyProvider.cs").read_text()
    match = re.search(r'PublicKeyString = "([^"]+)"', source)
    check(match is not None and match[1] == public, "INI and OpenRA public keys differ")
    der = base64.b64decode(public, validate=True)
    check(len(der) == 42 and der[:2] == bytes((2, 40)), "Unexpected ASN.1 public modulus")
    modulus = int.from_bytes(der[2:], "big")
    width = (modulus.bit_length() - 2) // 8
    check(width == 39, "Unexpected RSA output width")
    print("Extracted [PublicKey] 1 matches OpenRA PublicKeyString exactly.")
    print(f"RSA modulus (big endian): {modulus:080x}; exponent=65537")
    for name, count, start, embedded in (
        ("ra2.mix", 21, 348, True), ("ra2md.mix", 25, 396, True),
        ("expandmd01.mix", 33, 492, True), ("multimd.mix", 173, 2172, False),
    ):
        archive = args.repo / "game" / name
        with archive.open("rb") as stream:
            flags = int.from_bytes(stream.read(4), "little")
            wrapped = stream.read(80)
        check(flags & 0x20000 and len(wrapped) == 80, f"{name}: encrypted key block missing")
        decoded = b"".join(
            (pow(int.from_bytes(wrapped[offset:offset + width + 1], "little"),
                 65537, modulus) & ((1 << (8 * width)) - 1)).to_bytes(width, "little")
            for offset in range(0, 80, width + 1)
        )[:56]
        result = subprocess.check_output([str(binaries / "crypto_test"), "--key", str(archive)], text=True)
        values = dict(line.split("=", 1) for line in result.splitlines())
        check(bytes.fromhex(values["key"]) == decoded, f"{name}: recovered key differs from INI-based pow")
        if embedded:
            check(values.get("public") == public, f"{name}: embedded [PublicKey] differs")
        output = subprocess.check_output([str(binaries / "mix_test"), "--archive", str(archive)], text=True)
        check(f"{archive}: {count} entries," in output and f", start={start}\n" in output,
              f"{name}: directory count or padding differs")
        reference = ""
        if args.openra_reference:
            result = subprocess.check_output(
                ["dotnet", str(args.openra_reference), str(archive)], text=True).strip()
            check(result == f"{decoded.hex()} {count}", f"{name}: OpenRA key/header differs")
            reference = "; OpenRA key and header match"
        print(f"{name}: {count} entries, start={start}; all 56 key bytes match INI-based RSA{reference}")
    print("Verified all four encrypted archives and the three embedded [PublicKey] INIs.")


if __name__ == "__main__":
    main()
