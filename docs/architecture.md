# Quake III Arena Modern Architecture

## Overview
Quake III Arena Modern updates the engine codebase to **C++17** and **C11** while preserving compatibility with original Quake III Arena assets, BSP maps, and game logic modules (`qagame`, `cgame`, `ui`).

## Directory Structure
- `code/sys/`: Cross-platform system layer (SDL2 windowing, OpenGL context, audio DMA, input, CvarManager, VFS, ScriptEngine, Discord RPC).
- `code/sys/sys_sdl.cpp`: SDL2 platform implementation for video, audio, input, and OpenGL extension loading via `SDL_GL_GetProcAddress`.
- `code/sys/sys_api.cpp` & `code/sys/sys_api.h`: Unified C-API interface exposing modern subsystem functions to C engine code.
- `code/renderer/`: OpenGL rendering pipeline with dynamic VBO/VAO vertex streaming and GLSL shader extension support.
- `code/renderer/vulkan/`: Low-overhead Vulkan 1.3 rendering backend prototype.
- `tests/`: Automated unit test suite using **GoogleTest** (47 unit tests).

## 64-Bit VM ABI
Virtual machine syscall parameter arrays use `intptr_t*` (8-byte element size on 64-bit platforms) rather than `int*`, avoiding pointer truncation errors in native shared libraries (`qagamex86_64.so`, `cgamex86_64.so`, `uix86_64.so`).
