#!/usr/bin/env python3
"""Write authentic Italian text extracted from an owned IT Emerald ROM into the
decomp's text files. See README.md. Extracted text is Nintendo material and is
kept local (skip-worktree) — do not commit it.

Usage: IT_ROM="/path/to/Pokemon - Versione Smeraldo (Italy).gba" python3 generate_it.py
"""
import os, re, sys, struct
sys.path.insert(0, os.path.dirname(__file__))
from gba_charmap import decode, encode

ROM_PATH = os.environ.get("IT_ROM") or (sys.argv[1] if len(sys.argv) > 1 else None)
if not ROM_PATH:
    sys.exit("Set IT_ROM=path/to/italian_emerald.gba (or pass it as arg1)")
rom = open(ROM_PATH, "rb").read()
N = len(rom)
REPO = os.path.join(os.path.dirname(__file__), "..", "..")

# Match _( "..." "..." ) possibly spanning lines with concatenated literals.
_TOKEN = re.compile(r'_\(\s*(?:"(?:[^"\\]|\\.)*"\s*)+\)')

def _esc(s):
    return s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")

def regen(relpath, strings):
    path = os.path.join(REPO, relpath)
    src = open(path, encoding="utf-8").read()
    it = iter(strings)
    n = [0]
    def repl(m):
        n[0] += 1
        return '_("' + _esc(next(it)) + '")'
    out = _TOKEN.sub(repl, src)
    open(path, "w", encoding="utf-8").write(out)
    assert n[0] == len(strings), f"{relpath}: matched {n[0]} tokens, expected {len(strings)}"
    print(f"{relpath}: {n[0]} strings")

def fixed(base, stride, n):
    return [decode(rom[base + i*stride: base + i*stride + stride]) for i in range(n)]

def ptrs(base, n, span=200):
    out = []
    for i in range(n):
        p = struct.unpack_from("<I", rom, base + i*4)[0] - 0x08000000
        out.append(decode(rom[p:p + span]) if 0 <= p < N else "")
    return out

# --- offsets located by anchoring on invariant names / pointer-array scans ---
BASE_SPECIES = rom.find(encode("BULBASAUR")) - 11        # gSpeciesNames, 11B
BASE_MOVES   = BASE_SPECIES + 412 * 11                    # gMoveNames, 13B
BASE_ITEMS   = 0x580038 - 44                              # gItems[], 44B, name@0
BASE_ABILNM  = rom.find(encode("LEVITAZIONE")) - 26 * 13  # gAbilityNames, 13B
PTR_ABILDESC = 0x31b4d4                                   # gAbilityDescriptionPointers
PTR_MOVEDESC = 0x619038                                   # gMoveDescriptionPointers (by move-1)

# --- names ---
regen("src/data/text/move_names.h", fixed(BASE_MOVES, 13, 355))
regen("src/data/items.h", fixed(BASE_ITEMS, 44, 377))     # replaces .name only (single-line)

# --- abilities: 78 description statics, then 78 gAbilityNames (file order) ---
ab_names = fixed(BASE_ABILNM, 13, 78)
ab_descs = ptrs(PTR_ABILDESC, 78)
regen("src/data/text/abilities.h", ab_descs + ab_names)

# --- move descriptions: file statics are sNull, then move #1.. (355 total) ---
move_descs = [""] + ptrs(PTR_MOVEDESC, 354)
regen("src/data/text/move_descriptions.h", move_descs)

print("done")
