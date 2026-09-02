# Checklist 09: Vulkan backend

## Purpose

Replace the 78-line Vulkan stub with a real Vulkan backend that renders the game through the
same command list the OpenGL backend executes. The backend is selected at runtime by a latched
cvar and shares the render thread, the streaming buffer model, and the post-pass design with
the OpenGL path.

**Status:** Not started

## Prerequisites

- Checklist `08-renderer-ui.md` is complete through step R2.4. Fixed-function GL is gone, the
  renderer runs on core profile, and every GL-specific call is confined to the seams named in
  this file.
- Checklist `05-threading.md` step T3 (render backend thread) is complete. The Vulkan backend
  runs on that thread and never touches `cvar_t` or `ri.Error` directly; it uses
  `backEnd.cvars`, `RB_Error`, and `RB_Printf`.
- Checklist `00-environment.md` provides Mesa `lavapipe` (software Vulkan) in the container.
- Checklist `04-cxx-migration.md` PR 3 has converted the renderer to C++.

## Owner decisions this file relies on

| # | Decision | Default the plan proceeds on |
|---|---|---|
| R3 | Integration model | In-tree backend vtable `rb_backend_t` with about 15 hooks, selected by the latched cvar `cl_renderer` (`opengl` or `vulkan`). Not the ioquake3 model of separate `renderer_*.so` libraries. The renderer is already a static mixed C and C++ library, and the R2 seams are the vtable points. |
| R4 | macOS | Support through MoltenVK. SDL2 `SDL_WINDOW_VULKAN` works with it. If the owner later declares macOS GL only, drop the MoltenVK items from M0 and M4. |
| R11 | CMake | `find_package(Vulkan)` is optional behind `Q3_VULKAN` (default `ON` when found). Load the API through `volk` (single file, vendored) or `SDL_Vulkan_GetVkGetInstanceProcAddr`. Default: `volk`. |
| R12 | Memory | Start with a simple linear-plus-free-list allocator per memory type. Adopt the Vulkan Memory Allocator library only if fragmentation shows up in M4. |

## Background

- `code/renderer/vulkan/vk_backend.cpp` is 38 lines and `vk_backend.hpp` is 40 lines. Neither
  includes a Vulkan header. `init()` hardcodes `device_name = "Vulkan Physical Device
  (Software/Hardware Abstract)"`, `api_version = 130`, and `discrete_gpu = true`
  (`vk_backend.cpp:11-14`), then logs `"VulkanBackend: Successfully initialized Vulkan 1.3
  context"` (`:19`). `begin_frame()` increments a counter and `end_frame()` is empty.
- The only references outside its directory are `CMakeLists.txt:271` (compiled into
  `q3renderer`) and `tests/test_vulkan_backend.cpp:2,5`, which asserts the hardcoded literal
  `130`. No engine code, cvar, or switch reaches it.
- `docs/renderer.md:8-10` and `docs/architecture.md:11` describe a working Vulkan 1.3 backend.
  Checklist 10 rewrites them; until M2 lands they say "planned".
- After R2.4 the GL-specific surface of the renderer is: context and window (`GLimp_*`),
  `R_VBO_*`, `R_BindArrays`, `R_DrawElements`, `GL_State`, `GL_Cull`, `GL_TexEnv` uniform
  set, depth range, clip plane, viewport and scissor, `GL_Bind` and texture upload (`Upload32`,
  `RE_UploadCinematic`), `GL_SetProjectionMatrix` and `GL_SetModelviewMatrix`, clears,
  `RB_SwapBuffers`, `glReadPixels` (screenshots, levelshots, overdraw), and the FBO post-pass.
- Reference implementations: Quake3e `vk.c` (about 12k lines, written over more than a year)
  and vkQuake3.

## Honest cost

Roughly 10 to 15 thousand new lines and 3 to 5 months part time for one developer, plus the
ongoing cost of two backends. The benefit for stock content is modest because the game is CPU
bound in the front end. The justification is platform longevity on macOS, where OpenGL is
deprecated, and the owner's decision to implement the documented feature.

## Steps

### Preparation

- [ ] **V0.1 Delete the stub and its test.**
  Files: `code/renderer/vulkan/vk_backend.hpp`, `vk_backend.cpp`, `tests/test_vulkan_backend.cpp`,
  `CMakeLists.txt:271`, `tests/CMakeLists.txt`.
  Remove the three files and their CMake entries. Rewrite `docs/renderer.md` so the Vulkan
  section reads "planned; see docs/plans/09-vulkan.md" (checklist 10 owns the wording).
  **Tests:** none. The tautology test is deleted on purpose.
  **Verify:** `grep -rn VulkanBackend code tests` prints nothing and the build is green.

- [ ] **V0.2 Define the backend vtable.**
  Files: `code/renderer/tr_local.h` (new `rb_backend_t`), new `code/renderer/tr_backend_gl.cpp`
  (fills the vtable with the R2 code), `code/renderer/tr_backend.c`
  (`RB_ExecuteRenderCommands` dispatches through the vtable), `code/renderer/tr_init.c`
  (`R_Init` picks the backend).
  The vtable is a plain struct of function pointers, not a virtual class, so GL and Vulkan
  translation units fill it and a null backend can record calls in tests:

  ```c
  typedef struct rb_backend_s {
      qboolean (*init)(void);                 /* after the window exists, on the render thread */
      void (*shutdown)(void);
      void (*begin_frame)(void);
      void (*end_frame)(void);                /* present */
      void (*set_projection)(const float m[16]);
      void (*set_modelview)(const float m[16]);
      void (*set_state)(unsigned long stateBits, cullType_t cull, qboolean polygonOffset);
      void (*set_viewport_scissor)(int x, int y, int w, int h);
      void (*set_depth_range)(float zNear, float zFar);
      void (*set_clip_plane)(const float plane[4], qboolean enabled);
      void (*bind_texture)(int unit, image_t *image, int texEnv);
      void (*upload_image)(image_t *image, const byte *rgba, int w, int h, qboolean mipmap, qboolean picmip, int wrap);
      void (*upload_sub_image)(image_t *image, int x, int y, int w, int h, const byte *rgba);
      void (*draw)(const shaderCommands_t *tess, int numIndexes, qboolean lines);
      void (*clear)(unsigned bits, const float color[4], float depth, int stencil);
      void (*read_pixels)(int x, int y, int w, int h, byte *rgb);
      void (*set_swap_interval)(int interval);
      void (*set_post_params)(float gamma, float overbrightScale);
      const char *(*gfx_info)(void);
  } rb_backend_t;
  ```

  `cl_renderer` (`CVAR_ARCHIVE | CVAR_LATCH`, default `"opengl"`) selects the table in
  `R_Init`. `GLimp_Init` gains a Vulkan branch that creates the window with `SDL_WINDOW_VULKAN`
  and no GL attributes, and exposes `SDL_Vulkan_CreateSurface` and
  `SDL_Vulkan_GetInstanceExtensions` to the backend.
  **Tests:** new `tests/test_rb_backend_null.cpp` (`quake3_tests`; shared with checklist 08 if
  it lands first). A null backend records every call. Cases:
  `RbBackend.FixedDrawSurfListProducesExpectedCallSequence` (a hand-built `drawSurf_t` list of
  two surfaces with two stages produces set_state, bind_texture, draw in the expected order),
  `RbBackend.BeginAndEndFrameBracketEveryFrame`, `RbBackend.UnknownRendererNameFallsBackToOpenGl`.
  **Verify:** `cl_renderer opengl` renders as before with G1 pixel identical against the
  pre-vtable build.

### M0: real device and clear (1 to 2 weeks)

- [ ] **M0.1 CMake and loader.**
  Files: `CMakeLists.txt`, new `code/third_party/volk/volk.{h,c}` (vendored, pinned),
  `THIRD_PARTY_LICENSES.md` (checklist 10).
  `option(Q3_VULKAN "Build the Vulkan backend" ON)`, `find_package(Vulkan)` for headers only,
  `volk` for function loading with `VK_NO_PROTOTYPES`. New OBJECT library `q3renderer_vk` with
  `code/renderer/vulkan/*.cpp`, compiled only when `Q3_VULKAN`. Add `glslangValidator` (or
  `glslc`) to the container image and a CMake rule that compiles `code/renderer/glsl/*.glsl`
  to SPIR-V at build time and embeds the bytes.
  **Tests:** none. Build configuration.
  **Verify:** `cmake -LH` shows `Q3_VULKAN`; the container build produces `.spv` outputs in the
  build tree.

- [ ] **M0.2 Instance, device, queue.**
  Files: new `code/renderer/vulkan/vk_instance.cpp`, `vk_device.cpp`, `vk_backend.hpp`.
  Create the instance with `SDL_Vulkan_GetInstanceExtensions`, `VK_KHR_portability_enumeration`
  on macOS, and `VK_LAYER_KHRONOS_validation` in debug builds with a debug messenger that routes
  to `RB_Printf`. Pick a physical device (prefer discrete, then integrated, then CPU for
  `lavapipe`), one graphics queue with present support, and a logical device with the swapchain
  extension (and `VK_KHR_portability_subset` on MoltenVK). Fill `glConfig.renderer_string`,
  `vendor_string`, and `version_string` from `VkPhysicalDeviceProperties` with the real
  `VK_API_VERSION_*` decoding.
  **Tests:** new `tests/test_vk_backend.cpp` (`quake3_tests`), skipped with
  `GTEST_SKIP()` when `vkCreateInstance` fails or no physical device exists. Cases:
  `VkBackend.InstanceCreates`, `VkBackend.SelectsADevice`,
  `VkBackend.ApiVersionIsDecodedNotLiteral` (major is 1, minor between 0 and 4).
  **Verify:** `gfxinfo` with `cl_renderer vulkan` prints the real device name and API version
  under `lavapipe` in the container.

- [ ] **M0.3 Swapchain, render pass, frames in flight, clear, and present.**
  Files: new `code/renderer/vulkan/vk_swapchain.cpp`, `vk_frame.cpp`.
  Swapchain with `VK_PRESENT_MODE_FIFO_KHR` for `r_swapInterval 1` and `MAILBOX` or
  `IMMEDIATE` otherwise; one render pass with a colour attachment and a D24S8 (or D32S8
  fallback) attachment; two or three frames in flight with command buffers, fences, and
  semaphores; `begin_frame` acquires, `clear` records a clear, `end_frame` submits and presents;
  `vid_restart` and `SDL_WINDOWEVENT_SIZE_CHANGED` recreate the swapchain; `VK_ERROR_DEVICE_LOST`
  reports through `RB_Error`.
  **Tests:** `tests/test_vk_backend.cpp` case `VkBackend.SwapchainRecreatesOnResize` when a
  headless surface is available (`VK_EXT_headless_surface` under `lavapipe`); otherwise skipped.
  **Verify:** a coloured clear on Linux (container, `lavapipe`), macOS (MoltenVK, CI artifact),
  and Windows (CI artifact); validation layer output is empty.

### M1: 2D path (2 to 3 weeks)

- [ ] **M1.1 Buffers and pipelines.**
  Files: new `code/renderer/vulkan/vk_buffers.cpp`, `vk_pipeline.cpp`, `vk_shaders.cpp`.
  Host-visible ring vertex and index buffers mirroring `R_VBO_Upload` (one per frame in
  flight). The `generic` shader compiled to SPIR-V. A pipeline family keyed by `(GLS state
  bits, cullType, polygonOffset, depth-range hack, lines or triangles, clip plane)` cached in a
  hash map. Push constants carry the MVP, `texEnv1`, and `alphaTest`.
  **Tests:** `tests/test_vk_backend.cpp` case `VkBackend.PipelineCacheKeyIsStable` (same state
  bits produce the same key, different cull types produce different keys) using the key
  function without a device.
  **Verify:** validation clean while drawing a coloured quad.

- [ ] **M1.2 Textures, samplers, descriptors.**
  Files: new `code/renderer/vulkan/vk_textures.cpp`.
  `upload_image` creates a `VkImage`, uploads through a staging buffer, generates mips with
  blits, and picks a sampler keyed by filter, wrap, and anisotropy. Descriptor sets per texture,
  or one descriptor array with a push-constant index. `upload_sub_image` covers
  `RE_UploadCinematic`.
  **Tests:** none, because it needs a device; the smoke covers it.
  **Verify:** menus, console, and `cinematic idlogo.RoQ` render under `cl_renderer vulkan`.
  Screenshots read back with `vkCmdCopyImageToBuffer`.

### M2: world and entities (3 to 4 weeks)

- [ ] **M2.1 Stage iterators and special paths.**
  Files: `code/renderer/vulkan/vk_draw.cpp`.
  Everything `RB_StageIteratorGeneric`, the vertex-lit path, the lightmapped multitexture
  path, `ProjectDlightTexture`, and `RB_FogPass` need: two texture units, colour and texcoord
  streams, and the full `GL_State` bit set mapped to pipeline state. Sky uses viewport min and
  max depth for the depth-range hack. Portals and mirrors use a clip distance in the vertex
  shader. Stencil shadows (`cg_shadows 2`) use stencil pipelines. Flares use occlusion queries
  or stay disabled.
  **Tests:** none, because it is drawing. Gate: the checklist 08 G1 A/B screenshot set against
  the GL backend, visually identical (PSNR at least 45 dB).
  **Verify:** the R2 test map set (q3dm1, fog map, q3dm0 mirror, dlights, `cg_shadows 2`,
  `r_lightmap 1`, `r_vertexLight 1`, `r_showtris 1`, RoQ, menus) renders under `lavapipe` with
  PSNR at least 45 dB against the GL screenshots.

### M3: parity features (1 to 2 weeks)

- [ ] **M3.1 Post-pass, MSAA, anisotropy, internal resolution, gfxinfo.**
  Files: `code/renderer/vulkan/vk_post.cpp`, `vk_swapchain.cpp`, `vk_textures.cpp`.
  Offscreen colour attachment plus a fullscreen pass for gamma and overbright; multisampled
  attachments with a resolve; anisotropy in the samplers; `r_mode` internal resolution with a
  final blit; `gfx_info` returns device, driver, and API version.
  **Tests:** none. Gate: brightness comparison against the GL post-pass screenshot.
  **Verify:** `r_gamma` is live, `r_ext_multisample 4` smooths edges, `r_mode 6` upscales.

### M4: hardening (1 to 2 weeks)

- [ ] **M4.1 Validation, device loss, memory, CI.**
  Files: `code/renderer/vulkan/vk_memory.cpp`, `ci/vk_golden.sh`, `.github/workflows/ci.yml`.
  Validation clean on Linux, Windows, and MoltenVK; device-lost recovery through `vid_restart`;
  a `vid_restart` loop of 20 without leaks (`VK_LAYER_KHRONOS_validation` object tracking); the
  memory allocator per decision R12; a CI job that renders one `timedemo` frame with
  `lavapipe` and compares with a golden image.
  **Tests:** `tests/test_vk_backend.cpp` case `VkBackend.AllocatorReusesFreedBlocks`.
  **Verify:** `ci/vk_golden.sh` passes in the container; the macOS and Windows CI artifacts run
  with `cl_renderer vulkan` without validation errors.

## Test map

| Test file | Binary | Cases | Added by |
|---|---|---|---|
| `tests/test_rb_backend_null.cpp` | `quake3_tests` | FixedDrawSurfListProducesExpectedCallSequence, BeginAndEndFrameBracketEveryFrame, UnknownRendererNameFallsBackToOpenGl | V0.2 |
| `tests/test_vk_backend.cpp` | `quake3_tests` | InstanceCreates, SelectsADevice, ApiVersionIsDecodedNotLiteral, SwapchainRecreatesOnResize, PipelineCacheKeyIsStable, AllocatorReusesFreedBlocks (all skipped without an installable client driver) | M0.2, M0.3, M1.1, M4.1 |

Gates that are not GoogleTest: the checklist 08 G1 screenshot set run with `cl_renderer vulkan`
under `lavapipe`, and `ci/vk_golden.sh`.

## Out of scope and follow-ons

- Ray tracing, bindless everything, or any feature beyond parity with the GL backend.
- Removing the GL backend. Both backends stay.
- The static world VBO (checklist 08 R2.6) applies to both backends later.

## Done criteria

- `cl_renderer vulkan` renders the whole R2 test map set with PSNR at least 45 dB against the
  GL backend under `lavapipe`, and the macOS and Windows CI artifacts run it without validation
  errors.
- `gfxinfo` reports the real device and API version.
- Every row of the test map exists and passes (or skips with a reason) under
  `ctest --preset dev` in the container.
- `docs/renderer.md` describes the two backends and how to select them (checklist 10).

## Last step

- [ ] Delete this file and remove its row from `docs/plans/README.md`.
