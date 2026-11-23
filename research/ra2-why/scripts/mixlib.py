"""Westwood MIX container toolkit for Red Alert 2 / Yuri's Revenge.

Format facts verified against OpenRA's MixFile reader (GPLv3) and the
original game files. Hash algorithms: PackageEntry.HashFilename
(Classic = RA1/TD rotate-left accumulate; CRC32 = TS/RA2 padded CRC).
Encrypted headers: 80-byte key block at offset 4, RSA-decrypted with
Westwood's public key (OpenRA BlowfishKeyProvider), yielding a 56-byte
Blowfish key; the header itself is Blowfish-ECB encrypted.
"""
import base64
import struct
import zlib

PUBLIC_KEY_B64 = "AihRvNoIbTn85FZRYNZRcT+i6KpU+maCsEqr3Q5q+LDB5tH7Tz2qQ38V"
RSA_E = 0x10001


# ---------------------------------------------------------------- hashes

def classic_hash(name: str) -> int:
    """RA1/TD filename hash. Name upper-cased, zero-padded to a multiple
    of 4, read as little-endian u32 words: rol(res,1) + word."""
    name = name.upper()
    pad = (4 - len(name) % 4) % 4
    data = name.encode("ascii") + b"\0" * pad
    res = 0
    for i in range(0, len(data), 4):
        word = struct.unpack_from("<I", data, i)[0]
        res = (((res << 1) | (res >> 31)) & 0xFFFFFFFF)
        res = (res + word) & 0xFFFFFFFF
    return res


def crc_hash(name: str) -> int:
    """TS/RA2 filename hash. Upper-cased; if the length is not a multiple
    of 4, append the remainder digit, then pad with the last full-block
    character; standard CRC-32 of the ASCII bytes."""
    name = name.upper()
    length = len(name)
    rounded = length // 4 * 4
    pad = (4 - length % 4) % 4
    out = name
    if length != rounded:
        out += chr(length - rounded)
        for _ in range(1, pad):
            out += name[rounded] if rounded < length else "\0"
    return zlib.crc32(out.encode("ascii")) & 0xFFFFFFFF


# ------------------------------------------------- blowfish key recovery

def _parse_public_modulus() -> int:
    der = base64.b64decode(PUBLIC_KEY_B64)
    assert der[0] == 0x02, "not an ASN.1 INTEGER"
    j = 1
    if der[j] & 0x80:
        n_len_bytes = der[j] & 0x7F
        keylen = int.from_bytes(der[j + 1:j + 1 + n_len_bytes], "big")
        j += n_len_bytes + 1
    else:
        keylen = der[j]
        j += 1
    return int.from_bytes(der[j:j + keylen], "big")


_N = _parse_public_modulus()


def _bitlen(n: int) -> int:
    return n.bit_length()


def recover_blowfish_key(keyblock: bytes) -> bytes:
    """RSA-decrypt the 80-byte key block of an encrypted MIX header.

    Mirrors OpenRA's BlowfishKeyProvider: the block is processed in
    (a+1)-byte chunks as little-endian bignums; each chunk is raised to
    e mod n; the low `a` bytes of each result (little-endian) concatenate
    to the key; the first 56 bytes are the Blowfish key.
    """
    a = (_bitlen(_N) - 1 - 1) // 8
    pre_len = (55 // a + 1) * (a + 1)
    dest = b""
    src_off = 0
    while a + 1 <= pre_len:
        chunk = keyblock[src_off:src_off + a + 1]
        c = int.from_bytes(chunk, "little")
        m = pow(c, RSA_E, _N)
        dest += (m & ((1 << (8 * a)) - 1)).to_bytes(a, "little")
        pre_len -= a + 1
        src_off += a + 1
    return dest[:56]


# ------------------------------------------------------------- mix parse

class MixEntry:
    __slots__ = ("hash", "offset", "size")

    def __init__(self, hash_, offset, size):
        self.hash = hash_
        self.offset = offset  # relative to data start
        self.size = size

    def abs_offset(self, data_start):
        return data_start + self.offset


class MixFile:
    """Parsed MIX archive. .data may be bytes or an open file object with
    read()/seek()."""

    def __init__(self, source):
        if isinstance(source, (bytes, bytearray)):
            self.data = bytes(source)
            self.name = "<memory>"
        else:
            self.name = getattr(source, "name", str(source))
            with open(source, "rb") as f:
                self.data = f.read()
        self._parse()

    def _parse(self):
        d = self.data
        if len(d) < 6:
            raise ValueError(f"{self.name}: too small for a MIX header")
        first, flags = struct.unpack_from("<HH", d, 0)
        self.cnc_format = first != 0
        self.encrypted = False
        self.checksum = None
        if self.cnc_format:
            count = first
            body = struct.unpack_from("<I", d, 2)[0]
            raw = d[6:6 + 12 * count]
            self.header_end = 6 + 12 * count
        else:
            if flags & 0x2:  # encrypted header
                self.encrypted = True
                key = recover_blowfish_key(d[4:84])
                self.blowfish_key = key
                count, body, hdr_len, raw = self._decrypt_header(d[4:], key)
                self.header_end = 4 + 80 + hdr_len
            else:
                count, body = struct.unpack("<HI", d[4:10])
                raw = d[10:10 + 12 * count]
                self.header_end = 10 + 12 * count
        self.count = count
        self.body_size = body
        self.entries = [MixEntry(*e) for e in struct.iter_unpack("<III", raw)]
        self.data_start = self.header_end
        for e in self.entries:
            if self.data_start + e.offset + e.size > len(d):
                raise ValueError(
                    f"{self.name}: entry {e.hash:08X} overruns file "
                    f"(off {e.offset}+{e.size}, data_start {self.data_start}, size {len(d)})")
        self.hashes = {e.hash: e for e in self.entries}
        # checksum (flags bit 0) = CRC32 after the index, 4 bytes
        if not self.cnc_format and (flags & 0x1) and not self.encrypted:
            self.checksum_bytes = d[self.header_end:self.header_end + 4]

    @staticmethod
    def _decrypt_header(blob, key):
        import warnings
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            dec = Cipher(algorithms.Blowfish(key), modes.ECB()).decryptor()
            # first block to learn the count
            first = dec.update(blob[80:88]) + dec.finalize()
            count = struct.unpack_from("<H", first, 0)[0]
            n_bytes = 6 + 12 * count
            blocks = (n_bytes + 7) // 8
            dec = Cipher(algorithms.Blowfish(key), modes.ECB()).decryptor()
            raw = dec.update(blob[80:80 + blocks * 8]) + dec.finalize()
        count2, body = struct.unpack_from("<HI", raw, 0)
        # entries begin after the 6-byte count+dataSize prefix
        return count2, body, blocks * 8, raw[6:6 + 12 * count2]

    # ------------------------------------------------------------ names

    def name_for(self, filename):
        """Return the MixEntry for a real filename, or None."""
        for h in (crc_hash(filename), classic_hash(filename)):
            e = self.hashes.get(h)
            if e:
                return e
        return None

    def resolve_with(self, names):
        """Attach .filename to entries matched from an iterable of names."""
        for name in names:
            for h in (crc_hash(name), classic_hash(name)):
                e = self.hashes.get(h)
                if e is not None and not hasattr(e, "filename"):
                    e.filename = name

    def read(self, entry) -> bytes:
        off = self.data_start + entry.offset
        return self.data[off:off + entry.size]

    def local_database(self):
        """Return XCC local mix database names if this mix carries one."""
        for h in (classic_hash("local mix database.dat"),
                  crc_hash("local mix database.dat")):
            e = self.hashes.get(h)
            if e:
                blob = self.read(e)
                return parse_xcc_database(blob)
        return None


def parse_xcc_database(blob: bytes):
    """'XCC by Olaf van der Spek' local mix database: 48-byte header,
    i32 count, then count NUL-terminated names."""
    if len(blob) < 52 or not blob.startswith(b"XCC by Olaf van der Spek"):
        return None
    count = struct.unpack_from("<i", blob, 48)[0]
    if count < 0 or count > 200000:
        return None
    names, pos = [], 52
    for _ in range(count):
        end = blob.find(b"\0", pos)
        if end < 0:
            break
        names.append(blob[pos:end].decode("ascii", "replace"))
        pos = end + 1
    return names
