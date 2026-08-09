import json, os, re, glob
REPO = os.path.join(os.path.dirname(__file__), "..", "..")
data = json.load(open(os.path.join(os.path.dirname(__file__), "it_dialog.json"), encoding="utf-8"))

# --- pass 1: assembler labels  `Symbol:` followed by .string lines (data/) ---
label_re = re.compile(r'^(\w+)::?\s*$'); str_re = re.compile(r'^\s*\.string\b')
inc_files = (glob.glob(os.path.join(REPO, "data/maps/*/scripts.inc")) +
             glob.glob(os.path.join(REPO, "data/scripts/*.inc")) +
             glob.glob(os.path.join(REPO, "data/text/*.inc")) +
             glob.glob(os.path.join(REPO, "data/maps/*/text.inc")) +
             [os.path.join(REPO, "data/event_scripts.s")])
w1 = 0; done = set()
for path in inc_files:
    if not os.path.exists(path): continue
    lines = open(path, encoding="utf-8").read().split("\n"); out = []; i = 0; ch = False
    while i < len(lines):
        m = label_re.match(lines[i])
        if m and m.group(1) in data and i+1 < len(lines) and str_re.match(lines[i+1]):
            out.append(lines[i]); j = i+1
            while j < len(lines) and str_re.match(lines[j]): j += 1
            out.append('\t.string "' + data[m.group(1)] + '$"'); w1 += 1; ch = True
            done.add(m.group(1)); i = j
        else:
            out.append(lines[i]); i += 1
    if ch: open(path, "w", encoding="utf-8").write("\n".join(out))

# --- pass 2: C-style  `Symbol[] = _( ... )`  — single scan per file, dict lookup ---
TOK = r'_\(\s*(?:"(?:[^"\\]|\\.)*"\s*)+\)'
defn = re.compile(r'(\b(\w+)\s*\[\]\s*=\s*)' + TOK)
csrc = (glob.glob(os.path.join(REPO, "src/**/*.c"), recursive=True) +
        glob.glob(os.path.join(REPO, "src/**/*.h"), recursive=True) +
        glob.glob(os.path.join(REPO, "data/**/*.inc"), recursive=True))
w2 = 0
def repl(m):
    global w2
    sym = m.group(2)
    if sym in data and sym not in done:
        done.add(sym); w2 += 1
        return m.group(1) + '_("' + data[sym] + '")'
    return m.group(0)
for path in set(csrc):
    txt = open(path, encoding="utf-8").read()
    new = defn.sub(repl, txt)
    if new != txt:
        open(path, "w", encoding="utf-8").write(new)

print(f"pass1 labels: {w1} | pass2 C-style: {w2} | total written: {len(done)} / {len(data)} mapped")
