# Pokémon Emerald — native PC port + AI dialog

This fork of the [pret](https://pret.github.io/) Pokémon Emerald decompilation
does two things:

1. **Builds as a native desktop binary** — no emulator. macOS (Apple Silicon
   or Intel) via `Makefile_mac`, 64-bit Windows via `Makefile_pc` (from the
   [SDL2 PC port](https://github.com/NTx86/pokeemerald-sdl2pc) this is based on).
2. **Adds an AI dialog mod** — overworld NPC dialog is rewritten on the fly by
   a language model, either the Anthropic API or a local model (Ollama,
   llama.cpp). Every villager talks in character, and the scripted meaning
   (directions, hints, facts) is preserved. Without a backend configured, the
   original scripted text is shown, so the game always plays normally.

## Quick start (macOS)

```sh
xcode-select --install
brew install sdl2 libpng
make -f Makefile_mac -j8
./pokeemerald_mac
```

Then, for AI dialog, copy `ai_dialog.cfg.example` to `ai_dialog.cfg` and pick
a backend (a local model needs no account):

```sh
brew install ollama && ollama pull qwen2.5:1.5b   # local, free
# or set backend=anthropic and export ANTHROPIC_API_KEY for the cloud API
```

## Documentation

- **[INSTALL_MAC.md](INSTALL_MAC.md)** — native macOS build, and how the
  arm64 Mach-O port works (symbol prefixing, unaligned pointers, etc.)
- **[AI_DIALOG.md](AI_DIALOG.md)** — the AI dialog mod: setup, architecture,
  config reference, and how to build your own LLM mods on this base
- **[INSTALL.md](INSTALL.md)** — the original decompilation / Windows setup

## How the AI mod is structured

Everything is isolated under `src/ai/` and `include/ai/` (all behind
`#ifdef PORTABLE`, so GBA ROM builds compile it out). The only change to the
base game is a small hook in `ShowFieldMessage()`. This makes it easy to lift
out, study, or use as a template — see the modder's guide in
[AI_DIALOG.md](AI_DIALOG.md).

## Legal / credits

A ROM is required at build time to supply graphics and audio; **no copyrighted
material is included in this repository**. Based on
[pret/pokeemerald](https://github.com/pret/pokeemerald) and the
[SDL2 PC port](https://github.com/NTx86/pokeemerald-sdl2pc). Pokémon is
© Nintendo / Game Freak / The Pokémon Company. This project is not affiliated
with or endorsed by them.
