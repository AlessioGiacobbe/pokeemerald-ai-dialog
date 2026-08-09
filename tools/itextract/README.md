# itextract — authentic-Italian text extractor

Pulls fixed-index name tables (Pokémon, moves, items) from a **Pokémon Emerald
(Italy)** ROM you own and writes them into the decomp's text files, so the
native build shows the official Italian names.

## Usage
```sh
IT_ROM="/path/to/Pokemon - Versione Smeraldo (Italy).gba" python3 generate_it.py
make -f Makefile_mac -j8
```

## How it works
- `gba_charmap.py` builds byte↔char tables from the game's own `charmap.txt`,
  so decoding matches the ROM font and re-encoding round-trips exactly.
- Name tables are located by anchoring on invariant names (e.g. `BULBASAUR`)
  and confirming the fixed record stride — no hardcoded per-version offsets.

## ⚠️ Copyright
The extracted text is Nintendo/Game Freak material. It is written into
`src/data/text/move_names.h` and `src/data/items.h`, which are kept **local**
via `git update-index --skip-worktree` and must **not** be committed/published.
Only this tool (no game text) belongs in the repo.
