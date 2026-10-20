"""Generates tests/data/vfs_vectors.txt and tests/data/lzo_vectors.txt.

Independent implementations produce the expected values:
- the MIX filename hash is computed straight from the format description,
- Blowfish comes from pycryptodome,
- modular exponentiation comes from Python integers,
- LZO streams come from the lzallright compressor.

Needs: pip install pycryptodome lzallright
Run:   python tools/gen_test_vectors.py
"""
import base64
import random
import struct
import zlib
from pathlib import Path

from Crypto.Cipher import Blowfish
from lzallright import LZOCompressor

DATA = Path(__file__).resolve().parent.parent / "tests" / "data"
RNG = random.Random(20261021)

# The Westwood public key: 02 28 <40-byte big-endian modulus>.
import base64
_raw = base64.b64decode("AihRvNoIbTn85FZRYNZRcT+i6KpU+maCsEqr3Q5q+LDB5tH7Tz2qQ38V")
MODULUS = int.from_bytes(_raw[2:2 + _raw[1]], "big")


def mix_id(name: str) -> int:
    s = name.upper().encode("latin-1")
    n = len(s)
    whole = n >> 2
    if n & 3:
        s += bytes([n - (whole << 2)])
        s += bytes([s[whole << 2]]) * (3 - (n & 3))
    return zlib.crc32(s) & 0xFFFFFFFF


def lcw_encode(data: bytes) -> bytes:
    """A valid LCW stream: fill commands for runs of 4 or more equal bytes, literal runs otherwise."""
    out = bytearray()
    literal = bytearray()

    def flush():
        for i in range(0, len(literal), 63):
            part = literal[i:i + 63]
            out.append(0x80 | len(part))
            out.extend(part)
        literal.clear()

    i = 0
    while i < len(data):
        run = 1
        while i + run < len(data) and data[i + run] == data[i] and run < 65535:
            run += 1
        if run >= 4:
            flush()
            out += bytes([0xFE]) + struct.pack("<H", run) + bytes([data[i]])
            i += run
        else:
            literal.append(data[i])
            i += 1
    flush()
    out.append(0x80)
    return bytes(out)


def chunked(data: bytes, compress, size: int) -> bytes:
    """Splits data into chunks of at most `size` bytes: {u16 compressed, u16 original, payload}."""
    out = bytearray()
    for i in range(0, len(data), size):
        part = data[i:i + size]
        packed = compress(part)
        out += struct.pack("<HH", len(packed), len(part)) + packed
    return bytes(out)


def b64_lines(data: bytes, width: int = 70) -> list:
    text = base64.b64encode(data).decode()
    return [f"{i // width + 1}={text[i:i + width]}" for i in range(0, len(text), width)]


# The cells, overlays and preview written into tests/data/test_map.map.
TEST_CELLS = [(10, 20, 5, 0, 3), (11, 20, 5, 1, 3), (12, 21, 0xFFFF, 0, 0), (100, 7, 123456, 9, 14)]
TEST_OVERLAYS = {(5, 6): (24, 3), (200, 300): (1, 15), (511, 511): (7, 0)}
TEST_PREVIEW_SIZE = (6, 4)


def write_test_map(comp):
    cells = b"".join(struct.pack("<HHiBBB", x, y, tile & 0xFFFFFFFF if tile < 0x80000000 else tile - (1 << 32), sub, height, 0)
                     for x, y, tile, sub, height in TEST_CELLS)
    overlay = bytearray([0xFF]) * (512 * 512)
    data = bytearray(512 * 512)
    for (x, y), (ov, frame) in TEST_OVERLAYS.items():
        overlay[y * 512 + x] = ov
        data[y * 512 + x] = frame
    pw, ph = TEST_PREVIEW_SIZE
    preview = bytes((i * 7) & 0xFF for i in range(pw * ph * 3))

    lines = ["[Basic]", "Name=Test Map", "NewINIFormat=4", "",
             "[Map]", "Theater=TEMPERATE", "Size=0,0,60,60", "LocalSize=2,4,56,50", "",
             "[Preview]", f"Size=0,0,{pw},{ph}", "",
             "[PreviewPack]", *b64_lines(chunked(preview, lambda d: bytes(comp.compress(d)), 8192)), "",
             "[IsoMapPack5]", *b64_lines(chunked(cells, lambda d: bytes(comp.compress(d)), 8192)), "",
             "[OverlayPack]", *b64_lines(chunked(bytes(overlay), lcw_encode, 32768)), "",
             "[OverlayDataPack]", *b64_lines(chunked(bytes(data), lcw_encode, 32768)), "",
             "[Waypoints]", "0=1010", "; a comment", "1=2020 ; trailing comment", ""]
    (DATA / "test_map.map").write_text("\n".join(lines), newline="\n")


def main():
    lines = ["# Reference values from independent implementations (tools/gen_test_vectors.py)."]

    names = ["RULES.INI", "rulesmd.ini", "ART.INI", "A", "AB", "ABC", "ABCD", "ABCDE", "local.mix", "conqmd.mix",
             "audio.idx", "key.ini", "ra2md.csf", "SOUNDMD.INI", "theme.mix", "MP29U2.MAP", "x", "xy", "xyz",
             "local mix database.dat", "unittem.pal", "GAMEMD.EXE", "e1.shp", "htnk.vxl", "htnk.hva"]
    for n in names:
        lines.append(f"id {n.replace(' ', '_')} {mix_id(n):08X}")

    for _ in range(40):
        size = RNG.choice([4, 5, 7, 8, 16, 31, 32, 55, 56])  # pycryptodome needs at least 4 bytes
        key = bytes(RNG.randrange(256) for _ in range(size))
        block = bytes(RNG.randrange(256) for _ in range(8))
        cipher = Blowfish.new(key, Blowfish.MODE_ECB).encrypt(block)
        lines.append(f"blowfish {key.hex()} {block.hex()} {cipher.hex()}")

    mod_le = MODULUS.to_bytes(40, "little")
    for _ in range(12):
        base = RNG.randrange(1, MODULUS)
        out = pow(base, 65537, MODULUS)
        lines.append(f"modpow {base.to_bytes(40, 'little').hex()} 65537 {mod_le.hex()} {out.to_bytes(40, 'little').hex()}")
    for exp in (1, 2, 3, 17, 255):
        base = RNG.randrange(1, MODULUS)
        lines.append(f"modpow {base.to_bytes(40, 'little').hex()} {exp} {mod_le.hex()} {pow(base, exp, MODULUS).to_bytes(40, 'little').hex()}")

    (DATA / "vfs_vectors.txt").write_text("\n".join(lines) + "\n", newline="\n")

    # LZO: data with many shapes, so every opcode form and long counts get exercised.
    comp = LZOCompressor()
    samples = [b"", b"a", b"abcabcabcabc" * 10, bytes(range(256)), bytes(1000), b"\xff" * 4000]
    for size in (3, 4, 15, 16, 17, 18, 100, 255, 256, 257, 700, 2000, 5000):
        samples.append(bytes(RNG.randrange(256) for _ in range(size)))
    for size in (50, 400, 3000, 8000):
        words = [bytes(RNG.randrange(256) for _ in range(RNG.randrange(1, 9))) for _ in range(12)]
        samples.append(b"".join(RNG.choice(words) for _ in range(size // 4)))
    for _ in range(6):
        # long runs and far matches
        chunk = bytes(RNG.randrange(256) for _ in range(RNG.randrange(20, 300)))
        samples.append(chunk + bytes(RNG.randrange(256) for _ in range(RNG.randrange(5000, 17000))) + chunk * 3)
    out = ["# LZO1X vectors: <compressed hex> <original hex>, from the lzallright compressor."]
    for s in samples:
        out.append(f"{bytes(comp.compress(s)).hex()} {s.hex() if s else '-'}")
    (DATA / "lzo_vectors.txt").write_text("\n".join(out) + "\n", newline="\n")
    write_test_map(comp)
    print("wrote vfs_vectors.txt, lzo_vectors.txt and test_map.map")


if __name__ == "__main__":
    main()
