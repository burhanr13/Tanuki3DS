# Tanuki3DS

Tanuki3DS is a new HLE 3DS emulator written in C which aims to be simple, fast, and compatible. Currently it can play a handful of games at full speed and supports some nice features like controller support and video upscaling.

<img src=images/oot3d.png width=300><img src=images/mk7.png width=300>

## Building

This project depends on SDL2, GLEW, and Xbyak to build and run. To build use `make` or `make release` to build the release version or `make debug` for debugging symbols. I have tested on both Linux with gcc and MacOS with Apple clang. Currently Windows is not natively supported, but you should be able to use the emulator on Windows through WSL (Windows Subsystem for Linux).

## Usage

You can run the executable in the command line with the rom file as the argument like `./ctremu path/to/game.3ds` (currently supports .elf, .3ds, .cci, .cxi files, roms must be decrypted). You can also pass `-h` to see other options.

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

## Compatibility

Many games work, but many will suffer from a range of bugs from graphical glitches to crashes. Also we don't have audio support yet. We are always looking to improve the emulator and would appreciate any bugs to reported as a github issue so they can be fixed.

## Credits

- [3DBrew](https://www.3dbrew.org) is the main source of documentation on the 3DS's operating system
- [GBATEK](https://www.problemkaputt.de/gbatek.htm) is used for low level hardware documentation
- [libctru](https://github.com/devkitPro/libctru) and [citro3d](https://github.com/devkitPro/citro3d) are libraries for developing homebrew software on the 3DS and are useful as documentation on the operating system and GPU respectively
- [Citra](https://github.com/PabloMK7/citra), [3dmoo](https://github.com/plutooo/3dmoo), and [Panda3DS](https://github.com/wheremyfoodat/Panda3DS) are HLE 3DS emulators which served as a reference at various points, as well as inspiration for this project
- [citra_system_archives](https://github.com/B3n30/citra_system_archives) is used for generating system file replacements
