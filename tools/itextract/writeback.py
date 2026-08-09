import json, os, re, glob
REPO = os.path.join(os.path.dirname(__file__), "..", "..")
data = json.load(open(os.path.join(os.path.dirname(__file__), "it_dialog.json"), encoding="utf-8"))

files = glob.glob(os.path.join(REPO, "data/maps/*/scripts.inc")) + \
        glob.glob(os.path.join(REPO, "data/scripts/*.inc")) + \
        [os.path.join(REPO, "data/event_scripts.s")]

label_re = re.compile(r'^(\w+)::?\s*$')
str_re   = re.compile(r'^\s*\.string\b')
written = 0; touched = []

for path in files:
    if not os.path.exists(path): continue
    lines = open(path, encoding="utf-8").read().split("\n")
    out = []; i = 0; changed = False
    while i < len(lines):
        m = label_re.match(lines[i])
        if m and m.group(1) in data and i+1 < len(lines) and str_re.match(lines[i+1]):
            out.append(lines[i])                     # keep label
            j = i + 1
            while j < len(lines) and str_re.match(lines[j]):
                j += 1
            out.append('\t.string "' + data[m.group(1)] + '$"')
            written += 1; changed = True
            i = j
        else:
            out.append(lines[i]); i += 1
    if changed:
        open(path, "w", encoding="utf-8").write("\n".join(out))
        touched.append(os.path.relpath(path, REPO))

print(f"written {written} dialogs across {len(touched)} files")
print("\n".join("  " + t for t in touched[:12]))
