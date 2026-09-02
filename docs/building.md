# Building Quake III Arena

This document describes how to configure, build, and test the modernized Quake III Arena engine across supported platforms.

## Prerequisites

You can build the project through containers on Linux, or natively on Linux, macOS, and Windows.

### Linux container build (recommended)

The container image includes all compilers, libraries, and tools:

- Docker or Podman
- Docker Compose plugin (optional for Makefile wrappers)
- GNU Make

### Linux native build

Install development packages from your distribution repository:

- `cmake` (version 3.16 or newer)
- `ninja` or GNU `make`
- `gcc` or `clang`
- `libsdl2-dev`
- `libgl-dev`
- `libluajit-5.1-dev`
- `libcurl4-openssl-dev`

### macOS native build

- Xcode Command Line Tools (`xcode-select --install`)
- CMake and Ninja

You can install dependencies through Homebrew:

```sh
brew install cmake ninja sdl2 luajit
```

Alternatively, configure with `-DQ3_FETCH_DEPS=ON` (or run `make native-build`), which downloads and compiles SDL2, LuaJIT, and GoogleTest automatically without host installations.

### Windows native build

- Visual Studio 2022 (version 17 or newer) with C++ Desktop Development workload
- CMake (version 3.21 or newer)
- vcpkg package manager

Install required libraries via vcpkg:

```cmd
vcpkg install sdl2 luajit gtest curl:x64-windows
```

## CMake presets

The project provides standard configuration presets in `CMakePresets.json`:

- `dev`: Linux and macOS development preset. Uses Ninja, `RelWithDebInfo`, and builds unit tests in `build/`.
- `debug`: Debug build with debug symbols and assertions in `build-debug/`.
- `release`: Release build with optimizations enabled in `build-release/`.
- `asan`: AddressSanitizer and UndefinedBehaviorSanitizer build in `build-asan/`.
- `tsan`: ThreadSanitizer build in `build-tsan/`.
- `msvc`: Windows 64-bit build targeting Visual Studio 2022 with vcpkg integration.
- `mingw`: Cross-compilation preset for MinGW-w64 in `build-win64/`.

List available presets with:

```sh
cmake --list-presets
```

Configure, build, and test using presets:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

## Game data files

Original game data pak files (`pak0.pk3` through `pak8.pk3`) are required to run the game.

You can place pak files in:

1. `build/baseq3/` next to compiled modules
2. Any directory referenced with `+set fs_basepath /path/to/paks`
3. `docker/paks/` when using container Make targets

## Home paths per platform

User configuration, screenshots, and downloaded maps are stored in platform-standard directories:

- Linux: `~/.q3a/`
- macOS: `~/Library/Application Support/Quake3/`
- Windows: `%APPDATA%\Quake3\`

## Module naming scheme

Dynamic game modules follow an architecture and platform convention:

```text
<module><arch><ext>
```

- Module names: `qagame`, `cgame`, `ui`
- Architectures: `x86_64`, `arm64`, `x86`
- Extensions: `.so` (Linux), `.dylib` (macOS), `.dll` (Windows)

Examples:

- Linux x86_64: `baseq3/qagamex86_64.so`
- macOS Apple Silicon: `baseq3/qagamearm64.dylib`
- Windows x64: `baseq3/qagamex86_64.dll`

## Pinned dependencies

When system packages are not detected, CMake fetches pinned source dependencies:

- LuaJIT wrapper: `https://github.com/zhaozg/luajit-cmake` at commit `94444a6c9bde77a768822f5cd00139161c9de412`.
- GoogleTest: release `v1.14.0` (SHA-256: `1f357c27ca988c3f7c6b4bf68a9395005ac6761f034046e9dde0896e3aba00e4`).
- SDL2: release `2.32.8` (under `-DQ3_FETCH_DEPS=ON`).
