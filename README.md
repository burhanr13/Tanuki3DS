# ctremu

ctremu is a new WIP HLE 3DS emulator. It can currently run a handful of games, and maybe even play some. However, many games will crash, much is still not implemented, and performance is not that good yet.

<img src=images/oot3d.png width=300><img src=images/mk7.png width=300>

## Building

This project depends on SDL2, GLEW, and Xbyak to build and run. To build use `make` or `make release` to build the release version or `make debug` for debugging symbols. I have tested on both Linux with gcc and MacOS with Apple clang (porting to windows is possible but nontrivial and has not been done yet).

## Usage

You can run the executable in the command line with the rom file as the argument (currently supports .elf, .3ds, .cci, .cxi files, roms must be decrypted). There are also some flags you can pass, like `-sN` to enable video upscaling.

The keyboard controls are as follows:

| Control | Key |
| --- | --- |
| `A` | `L` |
| `B` | `K` |
| `X` | `O` |
| `Y` | `I` |
| `L` | `Q` |
| `R` | `P` |
| `Circlepad` | `WASD` |
| `Dpad` | `Arrow keys` |
| `Start` | `Return` |
| `Select` | `RShift` |
| Pause/Unpause | `ctrl-P` |
| Toggle speedup | `ctrl-Tab` |

The touch screen can be used with the mouse.

You can also connect a controller prior to starting the emulator.

## Credits

- [3DBrew](https://www.3dbrew.org)
- [GBATEK](https://www.problemkaputt.de/gbatek.htm)
- [libctru](https://github.com/devkitPro/libctru) and [citro3d](https://github.com/devkitPro/citro3d)
- [Citra](https://github.com/PabloMK7/citra)
- [3dmoo](https://github.com/plutooo/3dmoo)
- [Panda3DS](https://github.com/wheremyfoodat/Panda3DS)
- [citra_system_archives](https://github.com/B3n30/citra_system_archives)
