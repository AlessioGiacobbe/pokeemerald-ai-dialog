#!/usr/bin/env python3
"""Authentic-dialog walker.

EN (reference, built from pret) and IT (retail) share identical script
bytecode STRUCTURE — same opcodes/lengths, only pointer VALUES differ
(pointing to EN vs IT data). So we walk both ROMs' map/script graph in
lockstep; at every 4-byte pointer slot whose EN value is a known EN text
symbol, the IT value at the same slot is that string's authentic Italian.

Outputs {en_symbol: italian_text}. Extracted text is Nintendo material —
keep local, do not commit.
"""
import struct, re, sys, json, os
sys.path.insert(0, os.path.dirname(__file__))
from gba_charmap import decode

EN_ROM = "/Users/alessiogiacobbe/pokeemerald-en-ref/pokeemerald_modern.gba"
EN_ELF = "/Users/alessiogiacobbe/pokeemerald-en-ref/pokeemerald_modern.elf"
EN_INC = "/Users/alessiogiacobbe/pokeemerald-en-ref/asm/macros/event.inc"
IT_ROM = "/Users/alessiogiacobbe/Downloads/Pokemon - Versione Smeraldo (Italy).gba"
EN_GMG = 0x0896963c
IT_GMG = 0x84832bc

en = open(EN_ROM, "rb").read()
it = open(IT_ROM, "rb").read()
B = 0x08000000

def u8(rom, a):  return rom[a - B]
def u16(rom, a): return struct.unpack_from("<H", rom, a - B)[0]
def u32(rom, a): return struct.unpack_from("<I", rom, a - B)[0]
def isrom(p): return B <= p < B + len(en)

# --- 1. command length table (opcode -> (length, [ptr_offsets])) from event.inc
def parse_cmd_table(path):
    txt = open(path).read()
    blocks = re.findall(r'\.macro\s+(\w+)([^\0]*?)\.endm', txt)
    table = {}
    opcode = -1
    dbg = {}
    for name, body in blocks:
        lines = [l.split("@")[0].strip() for l in body.splitlines()]
        # primitive command = body emits exactly the opcode via `.byte SCR_OP_...`
        if not any(re.match(r'\.byte\s+SCR_OP_', l) for l in lines):
            continue
        opcode += 1
        dbg[name] = opcode
        joined = "\n".join(lines)
        variable = (".if" in joined or ".elseif" in joined or ".rept" in joined
                    or ".irp" in joined)
        length = 0; ptr_offs = []; ok = True
        for d in lines:
            if not d: continue
            if d.startswith(".byte"):   length += 1
            elif d.startswith(".2byte") or d.startswith(".short"): length += 2
            elif d.startswith(".4byte"): ptr_offs.append(length); length += 4
            elif d.startswith(".endm"): pass
            elif d.startswith("."): ok = False
        table[opcode] = ((length if (ok and not variable) else None), ptr_offs)
    # sanity: loadword must be opcode 0x0F
    assert dbg.get("loadword") == 0x0F, f"loadword opcode = {dbg.get('loadword')} (expected 15)"
    return table

CMD = parse_cmd_table(EN_INC)
CALL, GOTO, GOTO_IF, CALL_IF = 0x04, 0x05, 0x06, 0x07

# --- 2. EN text symbols: address -> symbol name
def load_text_syms(elf):
    out = {}
    for line in os.popen(f"arm-none-eabi-nm '{elf}'"):
        parts = line.split()
        if len(parts) == 3 and ("_Text_" in parts[2] or parts[2].startswith("gText_")):
            out[int(parts[0], 16)] = parts[2]
    return out

TEXTSYMS = load_text_syms(EN_ELF)
print(f"cmd table: {len(CMD)} opcodes | text symbols: {len(TEXTSYMS)}")

# --- control-code aware decoder producing decomp .string source (or None if it
#     hits a code we don't safely reproduce, so we skip and leave English) ---
FD_TOKENS = {0x01: "{PLAYER}", 0x02: "{STR_VAR_1}", 0x03: "{STR_VAR_2}",
             0x04: "{STR_VAR_3}", 0x06: "{RIVAL}", 0x07: "{VERSION}"}
from gba_charmap import BYTE2CHAR

def decode_source(off):
    out = []
    i = off
    for _ in range(1000):
        b = it[i]; i += 1
        if b == 0xFF: return "".join(out)
        if b == 0xFE: out.append("\\n"); continue
        if b == 0xFA: out.append("\\l"); continue
        if b == 0xFB: out.append("\\p"); continue
        if b == 0xFD:
            tok = FD_TOKENS.get(it[i]); i += 1
            if tok is None: return None       # unknown placeholder -> skip
            out.append(tok); continue
        if b == 0xFC: return None             # extended control code -> skip
        c = BYTE2CHAR.get(b)
        if c is None: return None             # unknown byte -> skip
        if c == '"': out.append('\\"')
        elif c == '\\': out.append('\\\\')
        else: out.append(c)
    return None

# --- 3. parallel walk
result = {}          # en_symbol -> italian text
visited = set()

def walk_script(en_a, it_a, depth=0):
    if depth > 40 or en_a in visited or not isrom(en_a) or not isrom(it_a):
        return
    visited.add(en_a)
    p = 0
    for _ in range(4000):
        try:
            op = u8(en, en_a + p)
        except Exception:
            return
        if op == 0x02 or op == 0x03:  # end / return
            return
        spec = CMD.get(op)
        if spec is None or spec[0] is None:
            return  # unknown/variable command: stop this branch safely
        length, ptr_offs = spec
        for off in ptr_offs:
            en_ptr = u32(en, en_a + p + off)
            it_ptr = u32(it, it_a + p + off)
            if en_ptr in TEXTSYMS and isrom(it_ptr):
                txt = decode_source(it_ptr - B)
                if txt is not None:
                    result[TEXTSYMS[en_ptr]] = txt
            elif op in (CALL, GOTO, GOTO_IF, CALL_IF) and isrom(en_ptr) and isrom(it_ptr):
                walk_script(en_ptr, it_ptr, depth + 1)
        if op == GOTO:      # unconditional jump: follow and stop linear
            return
        p += length

def walk_all():
    for g in range(34):
        eng = u32(en, EN_GMG + g*4); itg = u32(it, IT_GMG + g*4)
        if not (isrom(eng) and isrom(itg)):
            continue
        # each group: array of map-header pointers until a non-rom entry
        i = 0
        while True:
            enh = u32(en, eng + i*4)
            if not isrom(enh):
                break
            ith = u32(it, itg + i*4)
            if not isrom(ith):
                break
            walk_map(enh, ith)
            i += 1

def walk_map(enh, ith):
    # map header: events@+4, mapScripts@+8
    en_ev = u32(en, enh + 4); it_ev = u32(it, ith + 4)
    en_ms = u32(en, enh + 8); it_ms = u32(it, ith + 8)
    if isrom(en_ms) and isrom(it_ms):
        walk_map_scripts(en_ms, it_ms)
    if not (isrom(en_ev) and isrom(it_ev)):
        return
    e = en_ev; ie = it_ev
    nobj, nwarp, ncoord, nbg = u8(en, e), u8(en, e+1), u8(en, e+2), u8(en, e+3)
    en_obj = u32(en, e+4);  it_obj = u32(it, ie+4)
    en_co  = u32(en, e+12); it_co  = u32(it, ie+12)
    en_bg  = u32(en, e+16); it_bg  = u32(it, ie+16)
    for t in range(nobj):   # object events: size 24, script @ +16
        if isrom(en_obj):
            walk_script(u32(en, en_obj+t*24+16), u32(it, it_obj+t*24+16))
    for t in range(ncoord): # coord events: size 16, script @ +12
        if isrom(en_co):
            walk_script(u32(en, en_co+t*16+12), u32(it, it_co+t*16+12))
    for t in range(nbg):    # bg events: size 12, union @ +8 (script when kind<5)
        if isrom(en_bg):
            kind = u8(en, en_bg+t*12+5)
            if kind < 5:
                walk_script(u32(en, en_bg+t*12+8), u32(it, it_bg+t*12+8))

def walk_map_scripts(en_a, it_a):
    # map scripts: list of {u8 type; u32 ptr} entries, terminated by type 0
    p = 0
    for _ in range(20):
        t = u8(en, en_a + p)
        if t == 0:
            return
        en_ptr = u32(en, en_a + p + 1); it_ptr = u32(it, it_a + p + 1)
        # types 2..7 point either to scripts or to (condition-table) structs; try as script
        if isrom(en_ptr) and isrom(it_ptr):
            walk_script(en_ptr, it_ptr)
        p += 5

walk_all()
print(f"mapped symbols: {len(result)}")
out = os.path.join(os.path.dirname(__file__), "it_dialog.json")
json.dump(result, open(out, "w", encoding="utf-8"), ensure_ascii=False)
# sample
for k in list(result)[:8]:
    print(f"  {k} = {result[k][:60]!r}")
