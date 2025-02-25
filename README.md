# Tanuki3DS

[![](https://github.com/burhanr13/Tanuki3DS/actions/workflows/ci.yml/badge.svg)](https://github.com/burhanr13/Tanuki3DS/actions/workflows/ci.yml)

<img src=images/logo.png width=100>

Tanuki3DS is a 3DS emulator for MacOS, Linux and Windows written in C which aims to be simple, fast, and compatible. Currently it can play a decent number of games with reasonable graphics and audio at full speed. It also supports some nice features like controller input, fast-forward, video upscaling, and free camera.

If you want to ask questions or discuss the emulator, join our discord:

[![](https://discord.com/api/guilds/1309172325203054612/widget.png?style=banner2)](https://discord.gg/6ya65fvD3g)

<img src=images/oot3d.png width=200><img src=images/mk7.png width=200><img src=images/pokemon.png width=200><img src=images/pmdgti.png width=200>

## Download

You can download a stable release from the releases tab, or the latest build below:

| Platform | Download |
| -------- | -------- |
| Linux | [Binary](https://nightly.link/burhanr13/Tanuki3DS/workflows/ci/master/Tanuki3DS-linux-binary.zip) <br> [AppImage](https://nightly.link/burhanr13/Tanuki3DS/workflows/ci/master/Tanuki3DS-linux-appimage.zip) |
| MacOS | [x86_64 App](https://nightly.link/burhanr13/Tanuki3DS/workflows/ci/master/Tanuki3DS-macos-x86_64.zip) <br> [arm64 App](https://nightly.link/burhanr13/Tanuki3DS/workflows/ci/master/Tanuki3DS-macos-arm64.zip) |
| Windows | [Windows Download](https://nightly.link/burhanr13/Tanuki3DS/workflows/ci/master/Tanuki3DS-windows.zip) |

## Usage
Launching the app should give you a prompt to select the game file. You can also start a game by dropping its file onto the window. The supported formats are:

- .cci/.3ds
- .cxi/.app
- .elf/.axf
- .3dsx

All games must be decrypted.

You can modify emulator settings in the generated `ctremu.ini` file.

You can also run the executable in the command line with the rom file as the argument or pass `-h` to see other options.

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
| Pause/Resume | `F5` |
| Mute/Unmute | `F6` |
| Toggle fast-forward | `Tab` |
| Reset | `F1` |
| Switch game | `F2` |
| Toggle freecam | `F7` |

The touch screen can be used with the mouse.

You can also connect a controller to use controller input. When using the
controller the right shoulder button can be used to tap the touch screen
at the current mouse location.

Freecam controls (regular keyboard input is disabled):
- WASD: move
- RF: move vertically
- arrow keys: look
- QE: roll
- Left Shift (hold): move slower
- Right Shift (hold): move faster

Note that the freecam only works when hw shaders are enabled.

## Building
You need the following dependencies installed to build and run:
- sdl3
- glew
- capstone
- xxhash
- cglm
- fdk-aac
- xbyak (x86_64 only)
- xbyak_aarch64 (arm64 only)

To build use `make`. You can pass some options to make, `USER=1` to compile a user build with lto, and `DEBUG=1` for unoptimized build with debug symbols. You need a compiler which supports C23 such as `clang-19`. To compile on Windows, you need to install Msys2 and mingw-w64 and compile within the mingw64 shell.


## Compatibility

Many games work, but many will suffer from a range of bugs from graphical glitches to crashes. We are always looking to improve the emulator and would appreciate any bugs to reported as a github issue so they can be fixed.

## Acknowledgements

- [3DBrew](https://www.3dbrew.org) is the main source of documentation on the 3DS
- [GBATEK](https://www.problemkaputt.de/gbatek.htm) is used for low level hardware documentation
- [libctru](https://github.com/devkitPro/libctru) and [citro3d](https://github.com/devkitPro/citro3d) are libraries for developing homebrew software on the 3DS and are useful as documentation on the operating system and GPU respectively
- [Panda3DS](https://github.com/wheremyfoodat/Panda3DS), [Citra](https://github.com/PabloMK7/citra), and [3dmoo](https://github.com/plutooo/3dmoo) are HLE 3DS emulators which served as a reference at various points, as well as inspiration for this project
- [citra_system_archives](https://github.com/B3n30/citra_system_archives) is used for generating system file replacements
