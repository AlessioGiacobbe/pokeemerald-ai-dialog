import sys, re
sys.path.insert(0, '.')
from gba_charmap import decode, encode

import os
ROM = os.environ.get('IT_ROM') or (sys.argv[1] if len(sys.argv)>1 else None)
if not ROM:
    sys.exit('Usage: IT_ROM=path/to/italian_emerald.gba python3 generate_it.py  (or pass the path as arg1)')
rom = open(ROM, 'rb').read()

base_sp = rom.find(encode('BULBASAUR')) - 11
base_mv = base_sp + 412 * 11

def dump(base, stride, n):
    return [decode(rom[base+i*stride: base+i*stride+stride]) for i in range(n)]

def regen(path, strings):
    src = open(path, encoding='utf-8').read()
    it = iter(strings)
    def repl(m):
        return '_("' + next(it).replace('\\', '\\\\').replace('"', '\\"') + '")'
    # replace each _("...") in order
    out = re.sub(r'_\("(?:[^"\\]|\\.)*"\)', repl, src)
    open(path, 'w', encoding='utf-8').write(out)
    print(f'{path}: sostituite {len(strings)} stringhe')

moves = dump(base_mv, 13, 355)
regen('../../src/data/text/move_names.h', moves)

# --- items: replace only .name = _("...") ---
import re as _re
def regen_items(path, names):
    src = open(path, encoding='utf-8').read()
    it = iter(names)
    def repl(m):
        s = next(it).replace('\\','\\\\').replace('"','\\"')
        return '.name = _("' + s + '")'
    out = _re.sub(r'\.name = _\("(?:[^"\\]|\\.)*"\)', repl, src)
    open(path,'w',encoding='utf-8').write(out)
    print(f'{path}: sostituiti {len(names)} nomi oggetto')

items = [decode(rom[(0x580038-44)+i*44:(0x580038-44)+i*44+14]) for i in range(377)]
regen_items('../../src/data/items.h', items)
