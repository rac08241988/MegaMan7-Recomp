# Mega Man 7 (SNES) PC port — notes

Unofficial Windows host for **Mega Man 7** (USA), built on the
[snesrecomp](https://github.com/magnopaz/snesrecomp) framework. The project
tree was seeded from the previous SNES Recomp v1.0.0 host template and every
name, label, and GitHub link was retargeted to
[rac08241988/MegaMan7-Recomp](https://github.com/rac08241988/MegaMan7-Recomp).

## ROM identity

| | |
|---|---|
| File | `Mega Man 7 (USA).sfc` (headered or headerless) |
| Header title | `MEGAMAN 7` |
| Mapping | HiROM FastROM (`$31`), 2 MB, no SRAM (type `$00`) |
| Region | USA |
| CRC32 | `2d947536` |
| SHA-1 | `6e7c9c9dd397f771303ee4aec29d106b9f86c832` |
| SHA-256 | `a255fec32453739903a1954149f19bc9658f4a415600b44badf1d4e5e13a16f9` |

Digests are of the bare image. A 512-byte copier header is stripped before the
check. The cartridge has no battery SRAM; the game uses passwords. Use save
states in this port.

## Host features carried in this tree

- One-click builder: the player picks their own ROM through the real file
  picker; Generate & rebuild produces the playable exe locally. No ROM and no
  `src/gen/` ever ship in the zip or the git repo.
- Side-panel Options menu (Esc / F12 / Guide) with Video, Display, Audio,
  Controls, Shaders and System. Live shader preview. All launcher video
  settings live in this menu.
- Full libretro slang + GLSL preset pack, including **Super Eagle**.
- Opt-in 16:9 widescreen (default Off) in Options > Video, implemented in
  `src/dc_widescreen.c` / `src/dc_display.c`. The gameplay gate widens
  scrolling stage playfields and keeps title, menus and static screens on
  side art. Capped at 1080p and 60 fps. 120 Hz motion stays PC-only behind
  `#if !defined(__SWITCH__)`.
- Original SNES front box art as the exe, launcher and Switch hand-off icon
  (`assets/icon/`, `src/win32/app.rc`).
- In-menu **Check for Updates** queries the GitHub latest release of
  `rac08241988/MegaMan7-Recomp`.

## Current limitations

- The first release runs on the interpreter. Native (ahead-of-time) codegen
  is not promoted yet.
- No battery save (the cartridge has none). Use save states.
- Sprites the original game culls at the 256-column edge can still pop in at
  the 16:9 edges (no generated-code cull hook yet).

## Switch hand-off

Shared files for a later Switch port: this source tree, `assets/icon/icon_512.png`,
and `/workspace/megaman7/icon/switch_icon_256.jpg`. The 120 Hz menu option must
stay hidden on Switch.
