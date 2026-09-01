# Modern Renderer Documentation

## OpenGL Subsystem
- **Dynamic VBO / VAO Streaming**: Geometry tessellation buffers (`tess.xyz`, `tess.svars.colors`, `tess.svars.texcoords`, `tess.indexes`) are buffered directly onto GPU memory via `glBufferData` / `glBufferSubData` (`GL_STREAM_DRAW`).
- **GLSL Extension Support**: Dynamic procedure resolution via `SDL_GL_GetProcAddress()` for GLSL shader compilation (`glCreateShader`, `glCompileShader`, `glCreateProgram`, `glUseProgram`).
- **Widescreen UI Scaling**: `SCR_AdjustFrom640()` and `UI_AdjustFrom640()` center 4:3 menus on 16:9 and 21:9 displays without stretching.

## Vulkan Backend Prototype
- Located in `code/renderer/vulkan/vk_backend.hpp` and `code/renderer/vulkan/vk_backend.cpp`.
- Provides low-overhead Vulkan 1.3 pipeline initialization, physical device querying, and frame lifecycle handlers (`begin_frame`, `end_frame`).
