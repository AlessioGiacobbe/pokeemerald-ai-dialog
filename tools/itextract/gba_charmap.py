# Byte<->char tables derived directly from the game's own charmap.txt, so
# decoding matches the ROM's font exactly and re-encoding via preproc round-trips.
import os, re

_CHARMAP = os.path.join(os.path.dirname(__file__), "..", "..", "charmap.txt")
BYTE2CHAR = {}
_line_re = re.compile(r"^'(.)'\s*=\s*([0-9A-Fa-f]{2})\s*$")

with open(_CHARMAP, encoding="utf-8") as f:
    for line in f:
        m = _line_re.match(line.rstrip("\n"))
        if not m:
            continue
        ch, hexb = m.group(1), int(m.group(2), 16)
        # First mapping wins -> Latin entries (which precede the Japanese ones).
        if hexb not in BYTE2CHAR:
            BYTE2CHAR[hexb] = ch

BYTE2CHAR[0x00] = " "
BYTE2CHAR[0xFE] = "\n"  # newline control code

CHAR2BYTE = {}
for b, c in BYTE2CHAR.items():
    if c not in CHAR2BYTE:
        CHAR2BYTE[c] = b

def encode(s):
    return bytes(CHAR2BYTE[c] for c in s)

def decode(raw):
    out = []
    for b in raw:
        if b == 0xFF:
            break
        c = BYTE2CHAR.get(b)
        if c is not None:
            out.append(c)
        else:
            out.append("?")
    return "".join(out)
