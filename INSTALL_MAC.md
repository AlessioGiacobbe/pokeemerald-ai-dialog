# Building natively on macOS

This fork builds the PC port as a **native macOS binary** (Apple Silicon or
Intel) — no emulator, no cross-compiler, no devkitARM.

## Prerequisites

```sh
xcode-select --install          # Apple clang + system libcurl
brew install sdl2 libpng
```

(`python3` and `perl` ship with macOS; both are used by the build.)

## Build

```sh
make -f Makefile_mac -j8
```

The first build compiles the tools, converts all graphics/audio, and takes
a few minutes; incremental builds are fast. The result is `pokeemerald_mac`
in the repo root:

```sh
./pokeemerald_mac
```

Saves go to `pokeemerald.sav` in the working directory. For the AI dialog
mod configuration, see [AI_DIALOG.md](AI_DIALOG.md).

## How the macOS build works (for the curious)

The upstream PC port (`Makefile_pc`) targets 64-bit Windows via a mingw
cross toolchain. Getting the same code to build as native Mach-O required
a handful of translations, all contained in `Makefile_mac` and
`tools/machopfx/`:

| Problem | Solution |
|---|---|
| Data assembly symbols have no `_` prefix, Mach-O C symbols do | `tools/machopfx/machopfx.py` rewrites every assembled object's symbol table (the Mach-O equivalent of `objcopy --prefix-symbols=_`) |
| `;` is a comment on Darwin's assembler, a statement separator on GNU as | a `perl` stage splits statements on unquoted `;` |
| Symbols starting with `L` are discarded as temporary labels on Mach-O | assembled with `-Wa,-L` to keep them |
| ELF `.section .rodata`-style directives | translated to `.data` |
| Absolute relocations are not allowed in `__TEXT` | all data assembly is placed in `__DATA` |
| LLVM MC can't fold `(.L2 - .L1) / 2` with forward labels, `undef == undef`, or expression chains on undefined symbols | the few asm macros relying on GNU as leniencies were rewritten portably (see `asm/macros/battle_anim_script.inc`, `asm/macros/event.inc`, `constants/tms_hms.inc`) |
| ELF `alias` attribute, ELF section names in C | `#ifdef __APPLE__` variants in `src/pokemon.c`, `src/rom_header_gf.c` |

Everything else — the C engine, the software GPU, the audio mixer — is the
upstream PC port compiled with Apple clang, unmodified.
