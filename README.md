# Mega Man 7 Recompiled (SNES)

> **Shaders only work on the Vulkan and OpenGL renderers.**
>
> This project ships no game code or ROM data. You supply your own cartridge dump.

An unofficial Windows PC port of **Mega Man 7** (SNES, USA),
built on the [snesrecomp](https://github.com/magnopaz/snesrecomp) framework
from the [Demon's Crest SNESRecomp](https://github.com/rac08241988/DemonsCrestSNESRecomp)
v1.1.0 template. Release v1.0.0 includes opt-in 16:9 widescreen (see below). It boots, plays, and has the full
Options menu, but see [Known limitations](#known-limitations).

## Features

- Native Windows x64 exe, built on your own PC from your own ROM.
- One-click builder: pick your ROM, it builds the game and launches it.
- In-game Options side panel (Esc / F12 / pad Guide): Video, Display,
  Audio, Controls, Shaders and System tabs, with a live preview and an Apply
  button. Keyboard, Xbox and PlayStation hint bar.
- Full shader set: the complete libretro slang preset pack (258 entries, plus
  Super Eagle) run through librashader, plus the libretro GLSL presets
  (including Super Eagle). CRT, scanline, LCD, smoothing and xBR/xBRZ
  scalers. **Shaders only work on Vulkan and OpenGL.**
- Opt-in **16:9 widescreen** in Options > Video (default Off keeps 4:3 with side art).
  16:9 shows extra stage beside the original 256-column view. Title, menus and
  any scene whose edges would glitch fall back to side art. The widescreen path
  is capped at 1080p and 60 fps.
- 120 Hz motion option on PC (game logic stays at 60 Hz; widescreen locks presentation
  to 60 fps; not shown on Switch), save states (10 quick slots plus a browser), rewind,
  turbo, recording, fullscreen and window scaling.
- In-menu **Check for Updates** looks up the latest GitHub release of
  [MegaMan7-Recomp](https://github.com/rac08241988/MegaMan7-Recomp/releases)
  and can open the release page.
- Xbox / XInput controller support with a positional SNES layout.

## Play on Windows (no coding needed)

1. Download `MegaMan7-SNESRecomp-windows-x64-v1.0.0.zip` from the
   [Releases](https://github.com/rac08241988/MegaMan7-Recomp/releases)
   page and unzip it anywhere.
2. Open `MegaMan7SNESRecomp.exe`.
3. Click **Browse For ROM** and pick your own dump:
   `Mega Man 7 (USA).sfc`. Headered (512-byte copier
   header) and headerless `.smc`/`.sfc` files both work; the header is
   stripped before the check.
4. Let it build. The first time it downloads a portable compiler and builds
   the game on your PC, which takes a few minutes.
5. Click **Play**. Next time, just open the exe.

The zip has no game code and no ROM. It holds the setup exe, its DLLs, the
shaders, the prebuilt `snesrecomp-analyze.exe` (so no Rust install is needed)
and the build tools/configs. The first run downloads a portable toolchain
(about 800 MB) into `toolchain\` beside the exe and builds the game into
`build\`; after that, opening the setup exe hands off to the built game. The
zip root is flat with a single exe, so launchers such as Quiver Launcher pick
it up automatically.

> **Shaders work on Vulkan and OpenGL renderers only.** New configs default to
> OpenGL. On another renderer (Direct3D, software) the Shader row says so
> instead of applying one; switch Video > Renderer to OpenGL or Vulkan.

## Controls & Shortcuts

Xbox layout. In menus, **A** confirms and **B** goes back. **Esc**, **F12**
or the pad **Guide** button opens the Options menu. "View" is the Xbox
View/Back button, which is SNES Select.

| Action | Xbox controller | Keyboard |
|---|---|---|
| D-pad | D-pad | Arrow keys |
| SNES A / B / X / Y | B / A / Y / X | X / Z / S / A |
| SNES L / R | LB / RB | C / V |
| Start / Select | Menu (Start) / View (Select) | Enter / Right Shift |
| Options menu (pauses the game) | Guide | F12 or Esc |
| Menu: move / change / confirm / back | D-pad / Left-Right / A / B | Arrows / Left-Right / Enter or Space / Backspace or Esc |
| Menu: previous / next tab | LB / RB | Q / E (or Tab, PgUp / PgDn) |
| Menu: Apply | X or Start | F5 (or the Apply button) |
| Save-state browser | hold View (Select) + press RB | F11 |
| Quick load / quick save slot 1-10 | - | F1-F10 / Shift+F1-F10 |
| Rewind | hold View (Select) + press R3 | Shift+F12 |
| Open the launcher | hold View (Select) + press L3 | Ctrl+L |
| Fullscreen | - | Alt+Enter |
| Reset | - | Ctrl+R |
| Pause / pause dimmed | - | Shift+P / P |
| Turbo (fast-forward) | - | Tab |
| Window bigger / smaller | - | Ctrl+Up / Ctrl+Down |
| Volume up / down | - | Keypad + / Keypad - |
| Show performance / toggle renderer | - | F / R |
| Start/stop a recording | - | Alt+R |

The gameplay pad map is positional: SNES B = Xbox A, SNES Y = Xbox X,
SNES A = Xbox B, SNES X = Xbox Y. The Options menu reads the pad only while
it is open; gameplay input is never re-routed.

## Shaders

In game: Esc / F12 / Guide, then **Shaders**. Left/Right picks a preset (it
previews live behind the panel) and Apply keeps it. The choice is saved and
defaults to Off. **Super Eagle** is in the list (slang and GLSL).

- The full libretro `slang-shaders` preset set (258 entries, plus Super Eagle)
  compiled at runtime by librashader on the Vulkan and OpenGL renderers.
- The libretro `glsl-shaders` `.glslp` presets for the OpenGL renderer,
  including Super Eagle.

**Shaders only work on Vulkan and OpenGL.** See [`SHADERS.md`](SHADERS.md) for
the pinned commits and the license audit.

## Tested On

The project owner's PC:

- CPU: AMD Ryzen 7 5800X3D
- GPU: NVIDIA GeForce RTX 3070 Ti (8 GB)
- RAM: 32 GB dual-channel
- Storage: NVMe SSD
- OS: Windows and AtlasOS
- Renderers: Vulkan and OpenGL

## Known limitations

- **Runs on the interpreter for now.** No native (ahead-of-time) code is
  generated yet: every routine runs on the framework's interpreter tier. It
  plays, but it is not the fast path yet.
- **Widescreen is opt-in.** Options > Video > Widescreen defaults to Off (4:3
  with side art). 16:9 widens stage gameplay; title, menus and any scene whose
  edges would show garbage tiles stay on side art. Sprites the original game
  culls at the 256-column edge can still pop in at the new edges (there is no
  generated-code cull hook for this title yet).
- **No battery save.** The cartridge has no SRAM (the game uses passwords), so
  there is no in-game battery save to keep; use save states. Save states come
  from the template and have not been fully re-tested here.
- Not played through to the end yet.

## Build from source (Linux host, Windows exe)

```sh
git clone --recursive https://github.com/rac08241988/MegaMan7-Recomp
cd MegaMan7-Recomp
scripts/setup.sh --rom "/path/to/Mega Man 7 (USA).sfc"
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/mingw-w64.cmake
cmake --build build-win -j
```

`src/gen/` and `recomp/funcs.h` are generated from your ROM and are never
committed.

## ROM identity

| | |
|---|---|
| File | `Mega Man 7 (USA).sfc` (headered or headerless) |
| Header title | `MEGAMAN 7` |
| Mapping | HiROM (FastROM), 2 MB, no SRAM |
| CRC32 | `2d947536` |
| SHA-1 | `6e7c9c9dd397f771303ee4aec29d106b9f86c832` |
| SHA-256 | `a255fec32453739903a1954149f19bc9658f4a415600b44badf1d4e5e13a16f9` |

The hashes are of the headerless ROM; a 512-byte copier header is stripped
before the check.

## Credits

- [snesrecomp](https://github.com/magnopaz/snesrecomp) (upstream:
  [RetroPortingToolKit/snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp))
  by Matthew Stanley and contributors: the SNES static-recompilation framework.
- [recomp-ui](https://github.com/magnopaz/recomp-ui): the launcher.
- [Demon's Crest SNESRecomp](https://github.com/magnopaz/DemonsCrestSNESRecomp)
  by Magno Paz, and this account's
  [fork](https://github.com/rac08241988/DemonsCrestSNESRecomp): the original
  template. Host, menu, shader and widescreen code follows that line.
- libretro slang/GLSL shaders and librashader.

## License

Same terms as the Demon's Crest fork. This project's own source is under the
MIT license in [`LICENSE`](LICENSE); the options menu and shader parts ported
from the Super Ryu World 1 port and the snesrecomp patch are PolyForm
Noncommercial 1.0.0 (see [`NOTICE.md`](NOTICE.md)). The framework and launcher
carry their own terms: see `snesrecomp/LICENSE`,
`snesrecomp/THIRD_PARTY_ATTRIBUTION.md` and `recomp-ui/LICENSE`. Because
snesrecomp is PolyForm Noncommercial, binaries are for non-commercial use
only. None of this covers the game, which is not distributed here.
Mega Man 7 is a trademark of its respective owners; this project
is not affiliated with them.
