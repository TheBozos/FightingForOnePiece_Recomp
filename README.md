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

**Playable route on BozoEngine, window untested.** The first port ran on PS2Recomp (the pictures above, and
[RECOMP_NOTES.md](RECOMP_NOTES.md)). On BozoEngine there is no emulation: no GS, VIF or VU1 model, only native code.
The game boots, plays the logo and opening movies, and draws the title, the menus, character select, the VS screen and
the fights (stage, fighters with their ink outlines, hit effects, HUD) natively, with music, sound effects and voices.
Fighting Mode, VS Fight (against the CPU or a second pad), Training and Option run on the eight stages, with the pause
menu, the Continue screen, the results and the memory card's saves and loads. Gamble (the bet, five fights, the payout
and the ending), Recording (a fight against the CPU or a second pad, saved to the card) and Replay (that fight loaded
and played back) run too. The headless runs are checked by rendering their draw lists in software. The window build
has not been run yet.

| Milestone | |
|---|:---:|
| Build on BozoEngine, with a symbol map for the stripped ELF | ✅ |
| Boot: kernel, CD reads, sound modules (native IRX), memory card, pads | ✅ |
| Logos and movies (`MOVIES/*.PSS`): native libmpeg, sound | ✅ |
| 2D: title, menus, character select, VS screen, HUD | ✅ |
| VU0 microprograms in C++ | ✅ |
| 3D: rigid and skinned meshes, outlines (VU1 programs by hand) | ✅ |
| Music, sound effects, voices | ✅ |
| Text library: the memory card messages | ✅ |
| Modes: Fighting, VS Fight, Training, Option; pause menu, Continue, results, memory card | ✅ |
| Modes: Gamble, Recording, Replay; the ending | ✅ |
| The game in a window (`-DFFOP_WINDOW=ON`) | 🔧 |

✅ works · 🔧 in progress

The frame-buffer effects the code holds (the afterimage at 0x19AC78, the radial blur at 0x19AF00, the fades at 0x19B610,
0x19BC40 and 0x19C190) have no caller in the ELF and run in none of the modes; nothing reads the frame buffer back.

A recording keeps the pads, not the random generator, which the game never seeds: Recording's character select draws one
number that Replay's path does not, so a replay against the CPU can part from the recorded fight. A fight between two
pads replays hit for hit.

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
- `runtime/` holds what is specific to the game:
  - the frame boundary at the main loop's frame wait (the sync-V callback at the vblank);
  - the DMA driver, readbacks, display registers and timers (the movie player's T0 pacing, the frame meter's T3)
    answered natively;
  - the movies on the renderer's backdrop;
  - the 2D sprite system (textures decoded from EE memory through one GS page's addressing, TEST's alpha test with
    its fail mode), and the text library's glyphs (the memory card messages);
  - the VU0 microprograms in C++;
  - the 3D draw lists, rigid and skinned meshes with the VU1 programs' arithmetic written by hand, textures from the
    bank uploads;
  - the sound modules' wiring.

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

For the window, configure with `-DFFOP_WINDOW=ON` and build the `ffop` target. It takes `--iso`, `--pad-script` and
`--capture-dir` / `--capture-frames`.

The run prints what the game logs and why it stopped. `--wav file` writes the mixer's output, `--profile` adds the library calls by name and the device
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
