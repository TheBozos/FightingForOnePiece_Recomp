<div align="center">

# FightingForOnePiece_Recomp

**A native Linux port of *Fighting for One Piece* (NTSC-J, `SLPS-25545`), made by statically recompiling the game's PlayStation 2 code to C++.**<br>
Not an emulator: the game's own MIPS R5900 code, rebuilt as a native program, with Sony's libraries answered natively.

![Platform](https://img.shields.io/badge/platform-Linux-1b8cff)
![Compiler](https://img.shields.io/badge/compiler-Clang-1b8cff)
![Based on](https://img.shields.io/badge/based%20on-BozoEngine-17a35b)
![Status](https://img.shields.io/badge/status-bring--up-c0392b)

<img src="docs/screenshots/fight.jpg" width="49%"> <img src="docs/screenshots/title_screen.jpg" width="49%">
<img src="docs/screenshots/main_menu.jpg" width="49%"> <img src="docs/screenshots/character_select.jpg" width="49%">
<img src="docs/screenshots/fight_intro.jpg" width="49%"> <img src="docs/screenshots/special_move.jpg" width="49%">

<sub>Captured from the first port on PS2Recomp, which reached the fights. The BozoEngine port does not draw yet.</sub>

</div>

> [!IMPORTANT]
> No game files are included: no ELF, IRX modules, data archives, or code generated from them.
> You need **your own copy** of the Japanese release, PS2 ID **`SLPS-25545`**. The port is built against that disc's executable:
>
> | File | SHA-1 |
> |---|---|
> | `SLPS_255.45` | `e445f2af578e3780bcd7f59cf57c57b491f3c747` |

## Status

**Bring-up on BozoEngine.** The first port ran on PS2Recomp up to the fights (the pictures above, and
[RECOMP_NOTES.md](RECOMP_NOTES.md)). It is moving to BozoEngine, with no emulation: no GS, VIF or VU1 model, only
native code. On BozoEngine the game boots, brings up its sound modules and the memory card, loads the logo's data and
stops at its first GS readback. Nothing is drawn yet.

| Milestone | |
|---|:---:|
| Build on BozoEngine, with a symbol map for the stripped ELF | ✅ |
| Boot: kernel, CD reads, sound modules (native IRX), memory card, pads | ✅ |
| Logo load | ✅ |
| Native drawing (the game's draw lists to the renderer, its readback) | ⏳ |
| Logos and movies (`MOVIES/*.PSS`) | ⏳ |
| Title screen, menus, fights | ⏳ |

✅ works · 🔧 in progress · ⏳ not reached yet

## How it works

```
disc ELF ──► bozo-recomp (recomp/ffop_symbols.txt) ──► C++ in the build tree
disc IRX ──► BozoEngine tools/native_irx ───────────► C++ in the build tree
                                                            │
   BozoEngine: native kernel · native Sony libraries ───────┼─► ffop-headless
   runtime/:   FFOP's overrides, frame boundary, sound modules ┘
```

- The ELF is stripped. `recomp/ffop_symbols.txt` names its Sony library functions: BozoEngine's
  `tools/symbolmap/bozo_symbolmap.py` matched them against executables that keep their symbols, and
  `recomp/ffop_reviewed.txt` holds the names confirmed by reading the code.
- `bozo-recomp` turns the game's code into C++. BozoEngine answers the named library functions natively.
- The disc's sound modules (LIBSD, SDRDRV, MODHSYN, MODMSIN, STRADPCM, SKSOUND, SKHSYNTH, SKMSIN, SKSADPCM) are
  translated at build time and run on BozoEngine's compiled-library host.
- `runtime/` holds what is specific to the game: the frame boundary at its main loop's frame wait, the CD mode setter,
  and the sound modules' wiring.

The engine is the `BozoEngine/` git submodule: [TheBozos/BozoEngine](https://github.com/TheBozos/BozoEngine). Fixes made
for this port that are not specific to the game go there.

## Requirements

- Linux (developed on Arch)
- CMake 3.21+, Ninja, Clang with C++20, Python 3, and bsdtar (libarchive) to extract the disc
- Your own copy of `SLPS-25545` as a disc image (the extracted ELF is checked against the hash above)

## Build and run

```bash
git clone --recurse-submodules https://github.com/TheBozos/FightingForOnePiece_Recomp.git
cd FightingForOnePiece_Recomp
scripts/extract_iso.sh "/path/to/Fighting for One Piece (Japan).iso"   # disc files -> game/
cmake -S . -B build/bozo -G Ninja -DFFOP_ELF="$PWD/game/SLPS_255.45"
cmake --build build/bozo --target ffop-headless
build/bozo/ffop-headless --iso "/path/to/Fighting for One Piece (Japan).iso" --frames 3000 --print
```

The run prints what the game logs and why it stopped. `--profile` adds the library calls by name and the device
accesses by site; `--pad-script` takes a frame-keyed pad script. The sound modules are read from `MODULES/` next to
the ELF (`-DFFOP_IRX_DIR` names another folder). `scripts/dis.sh` disassembles the ELF between two addresses, and the
`scripts/irx*` helpers do the same for the IOP modules.

## Disc facts

| | |
|---|---|
| Boot ELF | `SLPS_255.45`, entry `0x10001C`, stripped |
| Video mode | NTSC (`SYSTEM.CNF` `VER = 1.01`) |
| IOP modules | `MODULES/*.IRX`: CDVDSTM, LIBSD, MCMAN, MCSERV, MODHSYN, MODMSIN, PADMAN, SDRDRV, SIO2MAN, SKHSYNTH, SKMSIN, SKSADPCM, SKSOUND, STRADPCM |
| IOP image | `MODULES/IOPRP300.IMG` |
| Data | `FILES.FOA` (256 MiB archive) |
| Movies | `MOVIES/ONEP_OP3.PSS`, `MOVIES/FO_1.PSS` (SCE MPEG-2 PSS) |

## Repository layout

| Path | Contents |
|---|---|
| `BozoEngine/` | The recompiler and native runtime (git submodule, TheBozos/BozoEngine) |
| `CMakeLists.txt`, `cmake/` | Recompile the ELF and the sound modules, and build `ffop-headless` |
| `bozo/` | The headless runner |
| `recomp/` | The symbol map of the stripped ELF and its reviewed names |
| `runtime/` | FFOP's overrides: the frame boundary, the CD mode setter, the sound modules' wiring |
| `tools/` | The override list, the sound modules' image list and the symbol map's reference helper |
| `scripts/` | Disc extraction; disassembly and IRX helpers |
| `docs/screenshots/` | Pictures of the first port |
| `RECOMP_NOTES.md` | How the first port, on PS2Recomp, was brought up: its fixes and findings |
| `game/`, `build/` | Ignored by git: disc files, builds |

Everything derived from the game (disc files, generated code, builds, logs) stays outside this repository.
Never commit or redistribute it.

## Credits

- **The Bozos**: boubou666 and Arzaroth ([RecompWatcher](https://thebozos.github.io/RecompWatcher/) shows our projects).
- [BozoEngine](https://github.com/TheBozos/BozoEngine), The Bozos' MIT recompiler and native runtime, this port's engine.
- [PS2Recomp](https://github.com/ran-j/PS2Recomp) by ran-j and contributors (GPL-3.0), on which the port's first bring-up was built.
- *Fighting for One Piece* © Eiichiro Oda / Shueisha, Toei Animation, Bandai. This is an unofficial fan project, not affiliated with or endorsed by them.
