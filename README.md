# BlockOut (1989) — decompilation & multiplatform port

BlockOut is a 3D Tetris-like game for DOS (California Dreams / P.Z. Karen Co., 1989).
This repo holds the original DOS release and a reverse-engineered port of it to
portable C + SDL2, so it runs natively on Linux, Windows and macOS, and in a browser
via WebAssembly.

The goal has two halves:

1. **Game logic: decompiled faithfully.** Piece sets, the random number generator and
   how it is seeded and used, fall speed and level progression, scoring, layer clears,
   rotation and movement rules, and all timings, expressed in the original timer ticks.
   This becomes a deterministic, platform-independent C core. Given the same seed and
   inputs, it should behave exactly like the original under DOSBox.
2. **Presentation: modernized.** Rendering, input and audio are written fresh on top of
   the core. The original EGA/CGA/Hercules drawing code is not ported. It is only read
   where it reveals game rules (e.g. what is shown when).

## Layout

| Path        | Contents |
|-------------|----------|
| `original/` | Untouched DOS release (version `05-26-89`, EGA/CGA/Tandy/Hercules build) |
| `re/`       | Reverse-engineering notes, scripts, Ghidra exports |
| `port/`     | The port: `core/` (decompiled game logic, no I/O) + SDL2 frontend (native and Emscripten) |

## The original

- `BL.EXE` is a launcher (Turbo C 2.0). It shows the logo and the graphics-mode menu,
  then loads the game.
- `BL2.OVL` is the game itself. It's a standalone Turbo C MZ executable. This is what
  gets decompiled.
- `*.LBM` are IFF/ILBM images per video mode: `_H` = EGA 640×350, `_L` = 320×200
  (EGA low/CGA/Tandy), `_M` = Hercules 720×348.
- `BLOCKOUT.SET` holds saved settings. `BLSCORE.DAT` / `BLSCORE.IDX` (Hall of Fame)
  are created by the game.

## Tooling

| Tool | Used for | Install |
|------|----------|---------|
| DOSBox-X | Running the original as a reference; its debugger is used to inspect game state | `apt install dosbox-x` |
| Ghidra (headless) | Disassembly/decompilation of `BL2.OVL` (x86 16-bit real mode) | zip from GitHub into `~/tools/`, needs `openjdk-21-jdk` |
| Python 3 + capstone, Pillow | Ad-hoc disassembly, ILBM decoding, scripted screenshots | `apt install python3-capstone python3-pil` |
| Xvfb | Headless DOSBox-X runs for automated screenshots | `xvfb-run` |
| gcc + SDL2 + CMake | Native build of the port | `apt install libsdl2-dev cmake` |
| Emscripten (emsdk) | WebAssembly build of the port | `git clone emsdk` into `~/tools/emsdk` |

## Building the port

_Coming soon._
