# Checklist 08: renderer, graphics, UI, and UX

## Purpose

Make the existing OpenGL renderer correct on modern windowing (R1), replace the fixed-function
pipeline with a real streaming VBO and GLSL path and move to a core profile (R2), and fix the
user-facing defects in the HUD, console, menus, input, and first run (U1). Every documented
renderer claim becomes true or is removed from the docs.

**Status:** Not started

## Prerequisites

- Checklist `00-environment.md` is complete. Every A/B screenshot gate runs in the container
  under Mesa `llvmpipe` with `xvfb-run`.
- Checklist `01-build-portability.md` is complete. In particular the SDL-based GL loader must
  provide a full function table. Add these declarations to `code/renderer/qgl.h:157-215` next
  to the existing VBO and GLSL pointers, resolved through `SDL_GL_GetProcAddress` with the
  `APPLE`-suffixed vertex array object fallbacks bound under the unsuffixed names: GL 3.3 core
  entry points, `glGetStringi`, `glVertexAttribPointer`, `glEnableVertexAttribArray`,
  `glDisableVertexAttribArray`, `glBindAttribLocation`, `glGetShaderiv`, `glGetShaderInfoLog`,
  `glGetProgramiv`, `glGetProgramInfoLog`, `glUniformMatrix4fv`, `glUniform4f`, `glUniform2f`,
  `glUniform1i`, `glUniform1f`, `glGenFramebuffers`, `glBindFramebuffer`,
  `glFramebufferTexture2D`, `glFramebufferRenderbuffer`, `glGenRenderbuffers`,
  `glBindRenderbuffer`, `glRenderbufferStorage`, `glRenderbufferStorageMultisample`,
  `glBlitFramebuffer`, `glCheckFramebufferStatus`, `glDeleteFramebuffers`,
  `glDeleteRenderbuffers`, `glGenQueries`, `glBeginQuery`, `glEndQuery`, `glGetQueryObjectuiv`,
  `glDebugMessageCallback`, `glMapBufferRange`, `glUnmapBuffer`, `glDeleteBuffers`,
  `glDeleteVertexArrays`, `glDeleteShader`, `glDeleteProgram`, `glClipPlane` (compat only).
- Checklist `04-cxx-migration.md` PR 3 has converted `code/renderer` to C++. Every new file in
  this checklist is `.cpp`.
- Checklist `05-threading.md` step T1 is complete before R1, and step T3 (render backend
  thread) is complete before R2. R2 code is written against `backEnd.cvars`, `RB_Error`, and
  `RB_Printf` from T3, never against `cvar_t` or `ri.Error` directly.

## Owner decisions this file relies on

| # | Decision | Default the plan proceeds on |
|---|---|---|
| R1 | GL profile strategy | Two stage. R1 and R2 develop on an explicit compatibility 2.1 context behind `r_vbo` and `r_glsl` so both paths run in one binary for A/B comparison. `r_glCoreProfile` (latch) requests 3.3 core (3.2 on macOS, which yields 4.1) and forces `r_glsl 1 r_vbo 1`. At R2 exit the default flips to core and fixed-function code is deleted. macOS offers only 2.1 compat or 3.2 and later core, so a 3.x compat request fails there. |
| R2 | Minimum GL | End state GL 3.3 core. Migration state GL 2.1 with `ARB_vertex_buffer_object` and GLSL 1.20. |
| R3 | Vulkan model | In-tree backend vtable `rb_backend_t` selected by latched `cl_renderer`. Not separate renderer libraries. Checklist 09 owns it; this checklist creates the seams. |
| R4 | Vulkan on macOS | MoltenVK. Checklist 09. |
| R5 | Default video config | `r_mode -2` (desktop native), `r_fullscreen 1` (borderless desktop), `r_swapInterval 1`, `r_allowHighDPI 1`. |
| R6 | Console scaling semantics | `con_scale` multiplies the 640x480 virtual scale. Default `1` gives the same size as HUD text and is identical to today at 640x480. |
| R7 | HUD and FOV | `cg_wideScreenHUD 1` and `cg_horplus 1` by default, with the cvars as opt-out. |
| R8 | sRGB | Do not enable sRGB framebuffers or textures. Quake III art and lightmaps are authored for a non-linear pipeline. |
| R9 | `r_primitives` 1 and 3 (`glArrayElement` strips) | Remove with the VBO path. |
| R10 | Master server hosts | `sv_master1 master.ioquake3.org`, `sv_master2 master.maverickservers.com`, `sv_master3` to `sv_master5` empty. |

## Background

Ground truth from the audit of 1 September 2026. Re-verify every anchor before you edit,
because checklists 04 and 05 move code.

### Renderer

- No VBO or vertex array object exists. `qglGenBuffers`, `qglBufferData`, `qglBufferSubData`,
  `qglGenVertexArrays`, and `qglBindVertexArray` are declared (`code/renderer/qgl.h:178-186`),
  resolved (`code/sys/sys_sdl.cpp:198-206`), and never called. `tr_shade.c:170` uses
  `qglBindBuffer` only as a truthiness probe. Geometry goes out as client arrays:
  `qglVertexPointer` at `tr_shade.c:261`, `qglColorPointer` at `:624`, `qglTexCoordPointer`
  at `:627`, and `R_DrawElements` at `tr_shade.c:163-197`. The four xyz array sites are
  `tr_shade.c:261,1067,1162,1226`. Immediate mode remains at `tr_backend.c:776,994`,
  `tr_shade.c:70,103,131,294`, `tr_surface.c:328,1077`, `tr_sky.c:372`,
  `tr_shadows.c:81,127,285`, and `tr_main.c:1401,1411`.
- No GLSL use. Pointers are resolved at `sys_sdl.cpp:208-221` with zero call sites and no
  shader files in the tree.
- `GLimp_Init` (`sys_sdl.cpp:139-153`) sets no `SDL_GL_CONTEXT_PROFILE_MASK` or version, so
  SDL returns a legacy compatibility context. Fixed-function use is pervasive (66 call sites):
  `qglTexEnvf` at `tr_backend.c:180-189`, `qglMatrixMode` at `tr_backend.c:413-415,694-698`
  and `tr_flares.c:428,444`, client state throughout `tr_shade.c`.
- Extension detection was deleted with `linux_glimp.c` (not built). `glConfig.vendor_string`,
  `renderer_string`, `version_string`, and `extensions_string` are never set (`tr_init.c:214`
  copies an empty string; `GfxInfo_f` at `tr_init.c:774-777` and the Driver Info screen at
  `ui_video.c:100-102` are blank). `maxActiveTextures` is 0. `textureCompression` is never set
  (`tr_image.c:601` dead, and it uses the old S3 enum). `textureEnvAddAvailable` is 0, so
  `tr_shader.c:1793` refuses the GL_ADD collapse and lightmapped shaders take extra passes.
  `glConfig.isFullscreen` is never assigned, so `tr_image.c:2137-2141` forces
  `overbrightBits` to 0 and `r_overBrightBits` and `r_mapOverBrightBits` have no effect.
  `deviceSupportsGamma` is hardcoded `qtrue` at `sys_sdl.cpp:187` and ignores
  `r_ignorehwgamma`. `GLimp_SetGamma` (`sys_sdl.cpp:257-267`) uses `SDL_SetWindowGammaRamp`
  and ignores its result; the call fails on Wayland and on macOS windowed.
  `r_allowExtensions`, `r_ext_multitexture`, `r_ext_compiled_vertex_array`,
  `r_ext_texture_env_add`, and `r_ext_gamma_control` are registered (`tr_init.c:863-871`) and
  never read.
- No MSAA (`SDL_GL_MULTISAMPLE*`), no anisotropic filtering, and no sRGB anywhere in `code/`.
  Texture filtering is limited to the six legacy modes at `tr_image.c:66-73` through
  `GL_TextureMode` (`tr_image.c:103-140`).
- VSync is off despite the intent: `tr_init.c:933` registers `r_swapInterval "0"` before
  `sys_sdl.cpp:111` asks for `"1"` and gets the existing cvar. It is applied once at init
  (`sys_sdl.cpp:172`) and never at runtime. `r_customwidth` and `r_customheight` are registered
  twice with different defaults (`tr_init.c:892-893` 1600x1024 with `CVAR_LATCH`;
  `sys_sdl.cpp:117-118` 1024x768). Two mode tables exist (`tr_init.c:292-330` and a `switch` at
  `sys_sdl.cpp:122-137`). `r_mode` default `"3"` (`tr_init.c:890`) gives a 640x480 first run.
  `SDL_WINDOW_FULLSCREEN_DESKTOP` (`sys_sdl.cpp:148`) ignores the requested size.
- High DPI: `SDL_GL_GetDrawableSize` (`sys_sdl.cpp:177`) sets `glConfig.vidWidth` in pixels,
  but `SDL_WINDOW_ALLOW_HIGHDPI` is not requested, so Retina renders at half resolution. The
  window is `SDL_WINDOW_RESIZABLE` (`sys_sdl.cpp:146`) with no `SDL_WINDOWEVENT` handling in
  `Sys_SendKeyEvents` (`sys_sdl.cpp:405-479`). `SDL_QUIT` injects `K_ESCAPE`
  (`sys_sdl.cpp:475`). Mouse deltas (`sys_sdl.cpp:421-424`) are in points. Audit correction:
  `UI_MouseEvent` (`ui_atoms.c:880-899`) adds raw deltas in 640x480 virtual units, so menu
  cursor speed is already DPI independent.
- `tr_flares.c:256` reads back one depth pixel per flare per frame (`r_flares` default 0).
  `cg_view.c:485-530` derives `fov_y` from the render aspect, which is vert- on wide screens.
  The gun offset at `cg_weapons.c:1413` assumes 4:3.
- `code/renderer/vulkan/vk_backend.{hpp,cpp}` is 78 lines with no Vulkan header. Checklist 09
  owns it.

### UI and UX

- Widescreen: `cl_scrn.c:59-82` and `ui_atoms.c:1076-1105` are correct (uniform scale plus a
  centring bias). The cgame HUD is still stretched: the correction sits inside `#if 0` at
  `cg_drawtools.c:35-40`, `CG_AdjustFrom640` uses `cgs.screenXScale` and `screenYScale`
  computed as `vidWidth/640` and `vidHeight/480` (`cg_main.c:1892-1894`), and
  `cgs.screenXBias` is read at `cg_drawtools.c:612,722` but never assigned.
  `cg_scoreboard.c:216,376-389,440,474` uses raw 640 constants. Team Arena
  `code/ui/ui_shared.c:3554-3560` has the bias commented out, but `code/ui` is not built.
- The pillarbox is never cleared: `cl_scrn.c:440-442` is an empty block after commit
  `abbcbfd`. `RB_DrawBuffer` clears only with `r_clear` (`tr_backend.c:945-948`).
- Console: line width is a compile-time 78 columns (`cl_console.c:253`) and `Con_CheckResize`
  returns early (`:255-256`). `SCR_DrawSmallChar` (`cl_scrn.c:155`) draws 8x16 raw pixels.
  `con.xadjust` is now the bias (`cl_console.c:598-599`) while the download line anchors to
  `vidWidth` (`:625`). Related functions: `Con_DrawInput` (`:477`), `Con_DrawNotify` (`:502`),
  `Con_DrawSolidConsole` (`:580-690`).
- `ui_video.c` is vanilla: 12 legacy modes plus 856x480 (`:762-777`) writing `r_mode`
  (`:485`); dead "GL Driver" (`:487`, `s_drivers[]` at `:241`) and "GL Extensions"
  (`:484,894`); no VSync, fullscreen mode, anisotropy, MSAA, FOV, or max FPS controls.
- Controller: five hardcoded buttons to keycodes at `sys_sdl.cpp:458-473`; no
  `SDL_CONTROLLERAXISMOTION`; `IN_Frame` and `IN_JoyMove` are empty (`sys_sdl.cpp:397-398`);
  `ui_controls2.c:1538-1548` writes `in_joystick` and `joy_threshold`, which nothing reads.
  `CL_JoystickEvent` and `CL_JoystickMove` exist (`cl_input.c:371,383`) and consume
  `SE_JOYSTICK_AXIS`.
- Mouse: `SDL_SetRelativeMouseMode(SDL_TRUE)` at `sys_sdl.cpp:222,369`, released only in
  `IN_Shutdown` (`:391`). No focus handling. `SDL_MOUSEWHEEL` is not handled, so `K_MWHEELUP`
  and `K_MWHEELDOWN` are never generated. `Sys_GetClipboardData` is X11 code in
  `unix_main.c:1152` (checklist 01 replaces it).
- Master server: `qcommon.h:237` `MASTER_SERVER_NAME "master.quake3arena.com"` is dead.
  `CL_GlobalServers_f` (`cl_main.c:2885-2930`) hardcodes it for both master indices.
  `ui_servers2.c:100-106` offers Local, Internet, Favorites; `:434` prints the no-response
  string.

### Structural facts

- The renderer is a static library called through `GetRefAPI` (`cl_main.c:2252`). There is no
  renderer DLL split. `sys_sdl.cpp` already touches `glConfig` and `ri`.
- `R_Register` runs before `GLimp_Init` (`R_Init` → `R_Register` → `InitOpenGL` → `GLimp_Init`),
  so the platform layer reads the renderer's cvars instead of registering duplicates.
- All geometry reaches GL through `tess` (`shaderCommands_t`, `tr_local.h:1266-1292`): `xyz`
  (vec4 stride 16), `svars.colors`, `svars.texcoords[2]`, `indexes`. `R_DrawElements` is the
  single draw funnel. Fixed-function matrix use is at `tr_backend.c:413-415,503,625,654,694-698`,
  `tr_sky.c:711-712,821-827`, `tr_flares.c:426-445`. State without a core equivalent: alpha
  test in `GL_State` (`tr_backend.c:360-383`), `GL_TexEnv` (`tr_backend.c:167-195`),
  `glClipPlane` for portals (`tr_backend.c:503-507`), unsized internal formats and `GL_CLAMP`
  in `tr_image.c` and `tr_backend.c:754-758`.
- Gamma: `R_SetColorMappings` (`tr_image.c:2126-2200`) zeroes `overbrightBits` unless
  `deviceSupportsGamma && isFullscreen`. The software path `R_LightScaleTexture`
  (`tr_image.c:296-330`) bakes `s_gammatable` into textures when `!deviceSupportsGamma`. The
  hardware ramp shifted by `overbrightBits` (`tr_image.c:2173-2185`), so a post-process pass
  must render at identity light and multiply back.
- cgame and UI snapshot `glconfig` once at init (`cg_main.c:1892`, `ui_atoms.c:1073`), so a
  window resize needs `vid_restart`, which is also ioquake3 behaviour.

## Gates used by the Verify lines

- **G1 A/B screenshot gate.** In the container:
  `make smoke -- +set <cvar> <value> +set r_mode -1 +set r_customwidth 640
  +set r_customheight 480 +set r_fullscreen 0 +set s_initsound 0 +set cl_avidemo 10
  +timedemo 1 +demo four`. Compare frame N of run A and run B with
  `compare -metric AE a.tga b.tga /dev/null`. The result must be `0` where the gate says
  "pixel identical" and a PSNR of at least 45 dB where it says "visually identical".
- **G2 apitrace gate.** `apitrace trace -o t.trace ./quake3_modern ...` then
  `apitrace dump t.trace | grep -cE "glBegin|glMatrixMode|glTexEnv|glAlphaFunc|glArrayElement"`
  must print `0` where the gate says "no fixed function".
- **G3 error gate.** The run has `+set r_ignoreGLErrors 0` and completes the timedemo without
  `GL_CheckErrors` firing.
- **Test map set** for R2 visual checks: q3dm1 (sky, `deformVertexes` flames, alpha-tested
  grates, jump pads), a fog map (fog pass and `CGEN_FOG`), q3dm0 (mirror, portal clip plane), a
  dlight scene (rocket launcher), `cg_shadows 2` (stencil), `r_lightmap 1`, `r_vertexLight 1`,
  `r_showtris 1`, `r_flares 1`, a RoQ cinematic (`cinematic idlogo.RoQ`), the console and menus
  (2D ortho path).

## Steps

### Phase R1: make the existing GL renderer correct (about 2 weeks)

- [ ] **R1.1 Create the context explicitly and fill `glConfig`.**
  Files: `code/sys/sys_sdl.cpp` (`GLimp_Init`), `code/renderer/tr_init.c` (`InitOpenGL`,
  `GfxInfo_f`, `R_Register`), `code/renderer/qgl.h`, `code/renderer/tr_local.h`, new
  `code/renderer/tr_glext.cpp`.
  - Before `SDL_CreateWindow`, set `SDL_GL_CONTEXT_PROFILE_MASK`, `SDL_GL_CONTEXT_MAJOR_VERSION`,
    and `SDL_GL_CONTEXT_MINOR_VERSION` from `r_glCoreProfile` (new, `CVAR_ARCHIVE | CVAR_LATCH`,
    default `"0"` until R2.4): core 3.3, or 3.2 on `__APPLE__`; otherwise compatibility 2.1.
    Fallback chain: requested, then compat, then SDL default, each logged with `ri.Printf`. On
    failure with MSAA (R1.6), retry without MSAA.
  - After the loader runs, fill `glConfig.vendor_string`, `renderer_string`, `version_string`
    from `qglGetString`; `extensions_string` from `GL_EXTENSIONS` (compat) or a join of
    `glGetStringi(GL_EXTENSIONS, i)` (core), truncated to `BIG_INFO_STRING`. Fill `colorBits`,
    `depthBits`, `stencilBits` from `SDL_GL_GetAttribute` (replace the hardcoded 32, 24, 8 at
    `sys_sdl.cpp:184-186`), `isFullscreen` from `SDL_GetWindowFlags`, `displayFrequency` from
    `SDL_GetWindowDisplayMode`, `driverType = GLDRV_ICD`, `hardwareType = GLHW_GENERIC`.
  - Add a small `glRefConfig_t` in `tr_local.h` with `coreProfile`, `glslMajor`, `glslMinor`
    (from `GL_SHADING_LANGUAGE_VERSION`) for R2.
  - New `R_InitExtensions()` in `tr_glext.cpp`, called from `InitOpenGL` right after
    `GLimp_Init` (`tr_init.c:212`). It honours `r_allowExtensions`; `r_ext_multitexture` (sets
    `maxActiveTextures` from `GL_MAX_TEXTURE_UNITS`, clears `qglActiveTextureARB` when
    disabled; `GL_SetDefaultState` already guards on that pointer);
    `r_ext_compiled_vertex_array` (clears `qglLockArraysEXT` when disabled, absent, or core);
    `r_ext_texture_env_add` (extension or GL 1.3 and later sets `textureEnvAddAvailable`; drop
    the `#ifdef __linux__` default at `tr_init.c:868-872`); `r_ext_compressed_textures`
    (`GL_EXT_texture_compression_s3tc` sets `TC_S3TC`; fix `tr_image.c:601` to use
    `GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0` and `GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3`);
    anisotropy and multisample (R1.6). Add `GLimp_HaveExtension(const char *name)` that
    tokenises the extension string, or uses `glGetStringi` under core.
  - `GfxInfo_f` prints the profile and GLSL version, and the "rendering primitives" text
    (`tr_init.c:806-813`) matches the real `R_DrawElements` decision.
  **Tests:** new `tests/test_glext.cpp` (`quake3_tests`). Cases:
  `GlExt.HaveExtensionTokenisesSyntheticString` (`"GL_A GL_AB GL_B"` finds `GL_A` and `GL_B`
  and not `GL_` or `GL_ABC`), `GlExt.S3tcEnumSelectionForRgbAndRgba`,
  `GlExt.ExtensionsDisabledClearsPointers` (with `r_allowExtensions 0` the resolved pointers
  are cleared by `R_InitExtensions` given a fake function table).
  **Verify:** `gfxinfo` shows vendor, renderer, version, extensions, correct bit depths,
  `texenv add: enabled`, `multitexture: enabled`. `shaderlist` shows fewer stages for
  lightmapped shaders because the GL_ADD collapse at `tr_shader.c:1793` now works. The Driver
  Info menu shows real strings. `r_allowExtensions 0; vid_restart` degrades cleanly.
  `r_glCoreProfile 1; vid_restart` creates a core context and renders nothing useful until R2,
  which is expected.

- [ ] **R1.2 Make gamma and overbright work on Wayland and macOS.**
  Files: `code/sys/sys_sdl.cpp` (`GLimp_SetGamma`, `GLimp_Init`, `GLimp_Shutdown`),
  `code/renderer/tr_image.c` (`R_SetColorMappings`), `code/renderer/tr_cmds.c` (`RE_BeginFrame`
  gamma block at `:365-371`).
  - `GLimp_Init` sets `deviceSupportsGamma = !r_ignorehwgamma->integer && isFullscreen &&
    SDL_GetWindowGammaRamp(...) == 0` (a probe that fails on Wayland and macOS windowed).
  - `GLimp_SetGamma` returns `qboolean`, checks the `SDL_SetWindowGammaRamp` result, and on
    failure clears `deviceSupportsGamma` and calls `R_SetColorMappings` again so the software
    table is used. Restore the original ramp in `GLimp_Shutdown` and on focus loss (R1.5).
  - When `!deviceSupportsGamma`, `R_LightScaleTexture` already bakes gamma into textures. In
    `RE_BeginFrame`, if `r_gamma->modified && !glConfig.deviceSupportsGamma && !tr.gammaPostPass`,
    print `r_gamma change requires vid_restart`.
  - Keep the "never overbright in windowed mode" rule (`tr_image.c:2138`) for the ramp path
    only. R2.5 lifts it.
  **Tests:** none, because it depends on the window system. Covered by the manual check.
  **Verify:** on Wayland and on macOS no SDL error spam. `r_gamma 1.5; vid_restart` brightens.
  `gfxinfo` reports `GAMMA: software w/ 0 overbright bits` in a window and `hardware w/ 1` in
  X11 or Windows fullscreen. Screenshots respect the `R_GammaCorrect` gating
  (`tr_init.c:400,422`).

- [ ] **R1.3 Apply VSync at init and at runtime.**
  Files: `code/sys/sys_sdl.cpp`, `code/sys/sys_sdl.hpp`, `code/renderer/tr_init.c:933`,
  `code/renderer/tr_cmds.c` (`RE_BeginFrame`), `code/renderer/tr_local.h`.
  Delete the `r_swapInterval` `Cvar_Get` at `sys_sdl.cpp:111`. The renderer owns the cvar with
  default `"1"` (decision R5). Add `void GLimp_SetSwapInterval(int)` that calls
  `SDL_GL_SetSwapInterval` and falls back from `-1` (adaptive) to `1` on failure. Call it at
  the end of `GLimp_Init` and from `RE_BeginFrame` when `r_swapInterval->modified`, using the
  same pattern as the `r_textureMode` block at `tr_cmds.c:356-360`. After checklist 05 T3 this
  becomes the `RC_SET_SWAP_INTERVAL` command.
  **Tests:** none, because it is a window-system call. `cvarlist r_swapInterval` shows the
  single registration, which the R1.4 test `ModeTable.NoDuplicateCvarRegistration` checks
  indirectly by grepping `sys_sdl.cpp` for `Cvar_Get("r_`.
  **Verify:** `r_swapInterval 0` gives an unbounded frame rate with `com_maxfps 0`;
  `r_swapInterval 1` locks to the display refresh; toggling mid-game applies on the next frame
  without `vid_restart`.

- [ ] **R1.4 One mode table, a native mode, and no duplicate cvars.**
  Files: `code/renderer/tr_init.c` (`r_vidModes`, `R_GetModeInfo`, `R_ModeList_f`,
  `R_Register`), `code/sys/sys_sdl.cpp` (`GLimp_Init`), `code/renderer/tr_public.h` or
  `sys_sdl.hpp` (export `R_GetModeInfo`).
  - Extend `r_vidModes` (`tr_init.c:292-306`) with 1280x720, 1280x800, 1366x768, 1440x900,
    1600x900, 1680x1050, 1920x1080, 1920x1200, 2560x1440, 2560x1600, 3440x1440, 3840x2160.
  - `R_GetModeInfo` handles `mode == -2` (desktop size through a new
    `GLimp_GetDesktopMode(&w, &h, &hz)` that wraps `SDL_GetDesktopDisplayMode`) and `-1`
    (`r_customwidth`, `r_customheight`). `modelist` prints `-2 desktop` and `-1 custom`.
  - In `GLimp_Init` delete the `switch` at `sys_sdl.cpp:122-137` and the duplicate `Cvar_Get`
    calls at `sys_sdl.cpp:107-118`. Read `r_mode`, `r_fullscreen`, `r_depthbits`,
    `r_stencilbits`, `r_colorbits` with `Cvar_VariableIntegerValue`, call `R_GetModeInfo`, fall
    back to mode `-2` when invalid, and `ri.Cvar_Set("r_mode", ...)` to the effective value.
    Defaults in `R_Register` (`tr_init.c:890-891`) per decision R5.
  - Fullscreen: `r_fullscreen 1` is `SDL_WINDOW_FULLSCREEN_DESKTOP`. Use exclusive
    `SDL_WINDOW_FULLSCREEN` with `SDL_SetWindowDisplayMode` only when the new cvar
    `r_fullscreenExclusive` is `1` (default `"0"`). Otherwise log that desktop fullscreen ignores
    `r_mode` until R2.5 allows internal-resolution scaling.
  - Publish `r_availableModes` (`CVAR_ROM`, `"WxH WxH ..."`) from `SDL_GetNumDisplayModes` and
    `SDL_GetDisplayMode`, deduplicated. U1.4 parses it.
  **Tests:** new `tests/test_mode_table.cpp` (`quake3_tests`). Make `GLimp_GetDesktopMode`
  injectable through a function pointer for tests. Cases: `ModeTable.EveryRowRoundTrips`
  (each `r_vidModes` index returns its width, height, and aspect), `ModeTable.CustomMode`
  (`-1` returns `r_customwidth` and `r_customheight`), `ModeTable.DesktopMode` (`-2` returns
  the injected desktop size), `ModeTable.InvalidModeReturnsFalse`,
  `ModeTable.AvailableModesStringIsDeduplicated` (a synthetic list with duplicates yields
  unique `WxH` tokens), `ModeTable.NoDuplicateCvarRegistration` (grep of `sys_sdl.cpp` for
  `Cvar_Get("r_` returns zero matches).
  **Verify:** first run without a config opens native borderless fullscreen. `r_mode 3;
  vid_restart` in windowed mode gives 640x480. `modelist` lists everything.
  `r_availableModes` is non-empty. The `gfxinfo` MODE line is correct including Hz.

- [ ] **R1.5 Handle resize, focus, and high DPI.**
  Files: `code/sys/sys_sdl.cpp` (`GLimp_Init`, `Sys_SendKeyEvents`, `IN_Frame`).
  - Add `SDL_WINDOW_ALLOW_HIGHDPI` when `r_allowHighDPI` (new, `CVAR_ARCHIVE | CVAR_LATCH`,
    default `"1"`). `glConfig.vidWidth` and `vidHeight` keep coming from
    `SDL_GL_GetDrawableSize` (pixels). Keep mouse deltas unscaled.
  - Handle `SDL_WINDOWEVENT`: `SDL_WINDOWEVENT_SIZE_CHANGED` (windowed only; compare the
    drawable size with `glConfig`) sets `r_customwidth` and `r_customheight` in window points,
    sets `r_mode -1`, and arms a 1 s debounce timer checked in `IN_Frame`; when it expires,
    `Cbuf_ExecuteText(EXEC_APPEND, "vid_restart\n")`. Do not patch `glConfig` in place;
    cgame and UI hold stale copies and the restart is the fix. `SDL_WINDOWEVENT_FOCUS_GAINED`
    and `FOCUS_LOST` set `s_windowFocused`, call `Key_ClearStates()` on loss, and restore or
    reapply the gamma ramp. `MINIMIZED` and `RESTORED` set a `com_minimized` cvar for checklist
    02 frame pacing if that track wants it.
  - `SDL_QUIT` (`sys_sdl.cpp:475`) calls `Cbuf_ExecuteText(EXEC_NOW, "quit\n")` instead of
    injecting `K_ESCAPE`.
  **Tests:** none, because it is event handling against SDL. Covered by the manual check.
  **Verify:** drag-resize the window and after about 1 s the game restarts at the new size with
  UI and HUD scaled correctly. Alt-tab releases the mouse and no key stays stuck. A Retina Mac
  shows the pixel resolution in `gfxinfo`. Closing the window quits cleanly.

- [ ] **R1.6 Add MSAA and anisotropic filtering.**
  Files: `code/sys/sys_sdl.cpp`, `code/renderer/tr_glext.cpp`, `code/renderer/tr_image.c`
  (`GL_TextureMode` at `:103-140`, `Upload32` filter setup near `:673-700`),
  `code/renderer/tr_init.c` (`R_Register`).
  Cvars with ioquake3 names: `r_ext_multisample` (`CVAR_ARCHIVE | CVAR_LATCH`, values 0, 2, 4,
  8) sets `SDL_GL_MULTISAMPLEBUFFERS` and `SDL_GL_MULTISAMPLESAMPLES` before window creation
  and retries without on failure; `qglEnable(GL_MULTISAMPLE)` in `GL_SetDefaultState`.
  `r_ext_texture_filter_anisotropic` (`CVAR_ARCHIVE | CVAR_LATCH`, default `"1"`) and
  `r_ext_max_anisotropy` (`CVAR_ARCHIVE`, default `"2"`): detect
  `GL_EXT_texture_filter_anisotropic`, query `GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT`, apply
  `GL_TEXTURE_MAX_ANISOTROPY_EXT` in the per-image loop of `GL_TextureMode` and in `Upload32`
  for mipmapped images, and reapply when `r_ext_max_anisotropy->modified` through the existing
  `r_textureMode->modified` block in `RE_BeginFrame`.
  **Tests:** `tests/test_glext.cpp` case `GlExt.AnisotropyClampedToDeviceMax` (requested 16
  with a device max of 8 applies 8).
  **Verify:** `gfxinfo` shows samples and anisotropy. Screenshots of the q3dm1 floor at a
  grazing angle sharpen with `r_ext_max_anisotropy 16`. `r_ext_multisample 4` smooths edges in
  a `screenshotJPEG` comparison. (Manual, real GPU, from a CI artifact.)

- [ ] **R1.7 Small correctness items.**
  Files: `code/renderer/tr_flares.c`, `code/renderer/tr_init.c:862`, `code/sys/sys_sdl.cpp`,
  `docs/renderer.md`, `docs/architecture.md:10-11`.
  Leave `r_flares` default 0 and note the per-flare `glReadPixels` at `tr_flares.c:256` in the
  docs; R2.4 offers occlusion queries. Keep `r_glDriver` registered because configs reference
  it, but stop using `OPENGL_DRIVER_NAME`. Window title becomes `Quake III Arena`. Optionally
  set the icon from `code/unix/quake3.xpm` with `SDL_SetWindowIcon` (move the file under
  `code/sys/` because checklist 01 deletes `code/unix`). Update the two docs to describe reality
  at R1 exit.
  **Tests:** none, because these are cosmetic and documentation changes.
  **Verify:** the window title reads `Quake III Arena`; `docs/renderer.md` no longer claims VBO
  streaming.

### Phase R2: VBO, vertex array objects, and GLSL for real (4 to 6 weeks)

Order matters: R2.1 (buffers) is A/B tested under fixed function; R2.2 (shaders) builds on
R2.1; R2.3 removes the last `glBegin`; R2.4 makes core profile the default; R2.5 adds the FBO
post-pass. Checklist 05 T3 (render thread) is complete before R2.1, so every GL call in this
phase runs on the render thread and reads `backEnd.cvars`.

- [ ] **R2.1 Stream `tess` through a VBO and IBO ring (`r_vbo`).**
  Files: new `code/renderer/tr_vbo.cpp`; `code/renderer/tr_shade.c`, `tr_local.h`, `tr_init.c`
  (cvars, init and shutdown hooks), `qgl.h`.
  Design: `tess` stays the CPU staging area (`RB_DeformTessGeometry`, `ComputeColors`, and
  `ComputeTexCoords` keep writing to it). Per `RB_EndSurface`, geometry is appended into a ring:
  one `GL_ARRAY_BUFFER` of 16 MB and one `GL_ELEMENT_ARRAY_BUFFER` of 4 MB, `GL_STREAM_DRAW`,
  orphaned with `glBufferData(NULL)` when the write cursor would wrap, filled with
  `glBufferSubData` (or `glMapBufferRange` with `GL_MAP_UNSYNCHRONIZED_BIT` where available).
  API: `void R_VBO_Init(void)`, `void R_VBO_Shutdown(void)`,
  `GLintptr R_VBO_Upload(const void *data, GLsizeiptr bytes)`,
  `GLintptr R_IBO_Upload(const glIndex_t *indexes, int n)`,
  `void R_BindArrays(GLintptr xyzOfs, GLintptr colorOfs, GLintptr tc0Ofs, GLintptr tc1Ofs)`
  (the single place that issues `glVertexPointer`, `glColorPointer`, `glTexCoordPointer` in
  compat or `glVertexAttribPointer` under GLSL; `-1` disables that array), and
  `R_DrawElements(numIndexes, indexes)` (`tr_shade.c:163`) gaining the IBO path that uploads
  indexes and calls `glDrawElements` with an offset. Under `r_vbo 0` the helpers pass client
  pointers so the legacy path survives for A/B.
  Call sites: upload `tess.xyz` once per `RB_EndSurface` in each stage iterator
  (`tr_shade.c:1067,1162,1226` and `DrawTris` at `:261`); upload `svars.colors` and
  `svars.texcoords[b]` after `ComputeColors` and `ComputeTexCoords` in `RB_IterateStagesGeneric`
  (`:959-960`) and pass offsets to `R_BindArrays`; `DrawMultitextured` (`:362,378`);
  `ProjectDlightTexture` (`:591,594`, the local `texCoordsArray` and `colorArray` are uploaded);
  `RB_FogPass` (`:624,627`); the vertex-lit path (`:1160-1162`; `tess.texCoords[0][0]` has
  stride 16, so upload the interleaved block and use stride 16 with offset 8 for the lightmap
  set); the lightmapped multitexture path (`:1234,1244,1258`, `constantColor255`). Remove
  `qglLockArraysEXT` calls when `r_vbo`. One vertex array object is created in `R_VBO_Init`
  when `qglGenVertexArrays` resolves (mandatory under core, optional in compat, the `APPLE`
  variant on macOS 2.1), bound once, and mutated by `R_BindArrays`.
  Cvar: `r_vbo` (`CVAR_ARCHIVE | CVAR_LATCH`, default `"1"`). `GfxInfo_f` prints
  `vertex buffers: streaming ring, VAO yes/no`.
  **Tests:** none as unit tests, because it is drawing. Gate G1 pixel identical between
  `r_vbo 0` and `r_vbo 1`; gate G3.
  **Verify:** G1 with `r_vbo` prints `0` differing pixels for every captured frame.
  `apitrace dump t.trace | grep -c glBufferSubData` is greater than 0 and `glVertexPointer`
  calls show buffer offsets. `r_speeds 1` shows unchanged surface and vertex counts.

- [ ] **R2.2 Replace fixed function with GLSL programs (`r_glsl`).**
  Files: new `code/renderer/tr_glsl.cpp` and `tr_glsl.h`; new
  `code/renderer/glsl/generic.vert.glsl`, `generic.frag.glsl`, `post.vert.glsl`,
  `post.frag.glsl`, embedded by CMake into a generated `glsl_embed.cpp` (`file(READ)` and
  `configure_file`, the ioquake3 pattern) with an `ri.FS_ReadFile("glsl/<name>")` override for
  live iteration; `tr_backend.c` (`GL_State`, `GL_TexEnv`, `SetViewportAndScissor`,
  `RB_BeginDrawingView`, `RB_RenderDrawSurfList`, `RB_SetGL2D`), `tr_shade.c`, `tr_sky.c`,
  `tr_flares.c`, `tr_image.c`, `tr_init.c`.
  Shader set (two programs suffice because colour and texcoord generation stay on the CPU):
  - `generic`: attributes `a_position` (vec3, stride 16), `a_color` (ubyte4 normalised),
    `a_texcoord0`, `a_texcoord1` (vec2). Uniforms `u_mvp` (mat4), `u_tex0`, `u_tex1`,
    `u_texEnv1` (0 off, 1 modulate, 2 add, 3 replace), `u_alphaTest` (0 none, 1 GT0, 2 LT80,
    3 GE80), `u_clipPlane` (vec4), `u_useClipPlane`. Fragment: `c = tex0 * v_color; c = env(c,
    tex1); alpha test; out = c`. Header `#version 120` with `attribute`, `varying`,
    `texture2D`, `gl_FragColor` shims for compat; `#version 150 core` for core (the ioquake3
    `GLSL_GetShaderHeader` pattern). Clip with `gl_ClipVertex` (compat) or `gl_ClipDistance[0]`
    plus `GL_CLIP_DISTANCE0` (core).
  - `post`: a fullscreen triangle sampling the scene FBO with uniforms `u_gamma` and
    `u_overbrightScale` (R2.5).
  Matrix plumbing: add `GL_SetProjectionMatrix(const float *)` and
  `GL_SetModelviewMatrix(const float *)` in `tr_backend.c` that store into `glState` and mark
  the MVP dirty; `R_DrawElements` uploads `u_mvp` (computed with `myGlMultMatrix` from
  `tr_main.c`) when dirty. Replace every `qglMatrixMode`, `qglLoadMatrixf`, `qglLoadIdentity`,
  `qglOrtho`, `qglPushMatrix`, `qglPopMatrix`, and `qglTranslatef` at
  `tr_backend.c:413-415,503,625,654,694-698`, `tr_sky.c:711-712,821-827`, and
  `tr_flares.c:426-445` (build the ortho matrix for 2D; compose the sky translate into the
  modelview).
  State plumbing under GLSL: `GL_State` alpha-test bits set `u_alphaTest` instead of
  `qglAlphaFunc`; `GL_TexEnv` sets `u_texEnv1` and stays a cache; `qglEnable(GL_TEXTURE_2D)` and
  `qglColor*` become no-ops; portals set `u_clipPlane`. `GL_Bind` and `GL_SelectTexture` are
  unchanged (sampler units 0 and 1). Texture upload: internal formats `3` and `4` become
  `GL_RGB8` and `GL_RGBA8`, and `GL_CLAMP` becomes `GL_CLAMP_TO_EDGE` in the `tr_image.c`
  upload paths and at `tr_backend.c:757-758,799-800`.
  Program lifecycle: compile at `R_Init` after `InitOpenGL`, delete in `RE_Shutdown`, log
  `glGetShaderInfoLog` and `glGetProgramInfoLog` on failure, fall back to `r_glsl 0` in compat
  or `ERR_FATAL` in core, and run `GL_CheckErrors` after link.
  Cvars: `r_glsl` (`CVAR_ARCHIVE | CVAR_LATCH`, default `"1"` once this step passes; requires
  `r_vbo`), `r_glDebug` (`glDebugMessageCallback` when `KHR_debug` or GL 4.3 is present;
  Linux and Windows only).
  **Tests:** new `tests/test_glsl_headers.cpp` (`quake3_tests`). Cases:
  `GlslHeaders.CompatHeaderIsVersion120WithShims`,
  `GlslHeaders.CoreHeaderIsVersion150Core`,
  `GlslHeaders.UniformNamesInSourceMatchTable` (parse the embedded `generic` source for
  `uniform` declarations and compare with the uniform table used by `tr_glsl.cpp`),
  `GlslHeaders.AttributeLocationsAreStable`.
  **Verify:** G1 visually identical (PSNR at least 45 dB; small alpha-test precision
  differences allowed) between `r_glsl 0` and `r_glsl 1` on the whole test map set. G2 prints
  `0` under `r_glsl 1`. G3 clean. In RenderDoc (Linux or Windows, manual) every draw has the
  `generic` program bound.

- [ ] **R2.3 Remove the last immediate-mode drawing.**
  Files: `tr_backend.c` (`RE_StretchRaw` at `:776`, `RB_ShowImages` at `:994`), `tr_shade.c`
  (delete `R_DrawStripElements` and `R_ArrayElementDiscrete` at `:40-150`; `DrawNormals` at
  `:294`), `tr_sky.c` (`DrawSkySide` at `:372`), `tr_shadows.c` (`RB_ShadowTessEnd` at
  `:81,127`, `RB_ShadowFinish` at `:285`), `tr_surface.c` (`RB_SurfaceBeam` at `:328`,
  `RB_SurfaceAxis` at `:1077`), `tr_main.c` (`R_DebugPolygon` at `:1401,1411`).
  Add `RB_InstantQuad(vec4_t quadVerts[4], vec2_t texCoords[4])` and `RB_InstantLines(...)`
  helpers in `tr_shade.c` that fill `tess` and go through `R_BindArrays` and `R_DrawElements`
  with the bound texture and `GL_State` (the ioquake3 `RB_InstantQuad2` pattern).
  `DrawSkySide` fills `tess.xyz` and `tess.texCoords` from `s_skyPoints` and `s_skyTexCoords`,
  emits triangle indexes, and draws. Stencil shadows already hold projected copies in
  `tess.xyz[i + numVertexes]`; build the quad-strip indexes into a local array and draw them.
  `R_DrawElements` drops `r_primitives` 1 and 3 (decision R9) and gains a `mode` parameter for
  `GL_LINES` debug draws (line width forced to 1 under core).
  **Tests:** none, because it is drawing. Gates G1 and G2.
  **Verify:** `grep -rn "qglBegin\|qglVertex[23]f\|qglTexCoord2f\|qglColor[34]f"
  code/renderer` returns hits only in `qgl.h`. Visual check of sky, `cg_shadows 2`, the railgun
  beam, `r_shownormals 1`, `r_showImages 1`, and cinematics. G2 shows no `glBegin`.

- [ ] **R2.4 Make core profile the default.**
  Files: `tr_init.c` (`R_Register` default `r_glCoreProfile "1"`), `tr_glext.cpp` (extension
  query through `glGetStringi`), `tr_flares.c` (optional: replace the `glReadPixels` depth probe
  with `GL_ARB_occlusion_query` and `GL_SAMPLES_PASSED`, read the result one frame later),
  `docs/renderer.md`.
  Flip the default, run the full R2.2 and R2.3 verification set under core on all three
  platforms (Linux in the container, macOS and Windows from CI), then delete the compat-only
  code (client state calls, `GL_TexEnv` GL calls, `r_ext_compiled_vertex_array`,
  `qglLockArraysEXT`, the strip renderer) in a separate, reviewable commit. Keep
  `r_glCoreProfile 0` only as a loader fallback that still requires GLSL (decision R2).
  **Tests:** `tests/test_glext.cpp` case `GlExt.CoreProfileExtensionListFromGetStringi` with a
  fake `glGetStringi`.
  **Verify:** macOS `gfxinfo` shows `GL_VERSION: 4.1 ... core`. `r_glDebug 1` prints no errors
  on Mesa. G3 clean for a full timedemo.

- [ ] **R2.5 Add the scene FBO and the gamma and overbright post-pass.**
  Files: new `code/renderer/tr_fbo.cpp`; `tr_backend.c` (`RB_BeginDrawingView`,
  `RB_SwapBuffers`), `tr_image.c` (`R_SetColorMappings`), `tr_init.c` (`RB_TakeScreenshot*`
  read from the resolved colour).
  `r_fbo` (`CVAR_ARCHIVE | CVAR_LATCH`, default `"1"` when `ARB_framebuffer_object` or GL 3.0).
  Create an RGBA8 colour target and a D24S8 depth-stencil target at `glConfig.vidWidth` by
  `vidHeight`, or at the requested internal `r_mode` size in desktop fullscreen (this is what
  makes `r_mode` meaningful under `SDL_WINDOW_FULLSCREEN_DESKTOP`). Use multisampled
  renderbuffers when `r_ext_multisample`, resolved with `glBlitFramebuffer` into a
  single-sample texture. At `RB_SwapBuffers`, bind the default framebuffer and draw the `post`
  program with `u_gamma = r_gamma` and `u_overbrightScale = 1 << tr.overbrightBits`.
  `R_SetColorMappings` gains a third mode `tr.gammaPostPass = qtrue`: do not zero
  `overbrightBits` for windowed or no-ramp, do not bake gamma into textures, and let `r_gamma`
  changes apply at once. If the flare depth probe still uses `glReadPixels`, it reads from the
  bound FBO.
  **Tests:** none, because it is drawing. Gate G1 and a brightness comparison.
  **Verify:** windowed mode with `r_overBrightBits 1` matches fullscreen brightness (compare
  with an X11 hardware-ramp screenshot from R1.2). The `r_gamma` slider in the Display menu is
  live. `r_mode 6` in desktop fullscreen renders 1024x768 upscaled. MSAA, FBO, and screenshots
  stay consistent. `apitrace dump` shows one `glBlitFramebuffer` and one final fullscreen draw
  per frame.

- [ ] **R2.6 Follow-on, not in the R2 exit criterion: static world VBO.**
  Upload `srfSurfaceFace_t`, `srfGridMesh_t`, and `srfTriangles_t` (`tr_local.h:568-625`)
  into one static VBO and IBO at `RE_LoadWorldMap` and draw world surfaces without copying into
  `tess`. This pays off only when `ComputeColors` and `ComputeTexCoords` for the common cases
  move to shader uniforms. That is the multi-month "opengl2" project. Record it in
  `docs/renderer.md` as planned.
  **Tests:** none.
  **Verify:** the docs list it under planned work.

### Phase U1: UI and UX (about 1 to 2 weeks, in parallel with R1)

U1.1, U1.2, U1.3, U1.7, and U1.8 are independent of R1 and can start at once. U1.4 depends on
R1.4 (`r_availableModes`). U1.5 and U1.6 edit `sys_sdl.cpp` with R1.1 and R1.5; land R1.5
first or do them in the same session.

- [ ] **U1.1 Center the cgame HUD on wide screens.**
  Files: `code/cgame/cg_main.c:1892-1894`, `code/cgame/cg_drawtools.c:33-45`,
  `code/cgame/cg_local.h:991-993`, `code/cgame/cg_scoreboard.c:216,376-389,440,474`,
  `code/cgame/cg_draw.c` (full-width fills), `code/cgame/cg_info.c` (loading screen backdrop).
  Compute `cgs.screenXScale = cgs.screenYScale = vidHeight / 480.0f` and
  `cgs.screenXBias = wide ? 0.5f * (vidWidth - vidHeight * 640 / 480) : 0`, the same formula
  as `ui_atoms.c:1076-1085`, gated by `cg_wideScreenHUD` (new, `CVAR_ARCHIVE`, default `"1"`;
  `0` keeps today's stretched values). `CG_AdjustFrom640` becomes `*x = *x * scale + bias`
  (delete the `#if 0`). Add `CG_FillRectFullWidth` or `CG_AdjustFrom640Stretch` for backgrounds
  that must span the screen: the scoreboard dialog at `cg_scoreboard.c:474`, team backgrounds
  at `:376-389`, the loading-screen backdrop. The `640 - SB_SCORELINE_X` widths at `:216` stay
  inside the 4:3 box. `cg_drawtools.c:612,722` already use `screenXBias`. Team Arena
  `ui_shared.c:3554-3560` is not built; leave a comment.
  **Tests:** new `tests/test_adjust640.cpp` (`quake3_tests`; extract the scale-and-bias
  arithmetic into a shared inline in `q_shared.h` or a small header so cgame, UI, and client
  use one function). Cases: `Adjust640.ScaleAndBiasAt640x480` (scale 1, bias 0),
  `Adjust640.ScaleAndBiasAt1920x1080` (scale 2.25, bias 240),
  `Adjust640.ScaleAndBiasAt1280x1024` (bias 0, scale from height),
  `Adjust640.ScaleAndBiasAt3440x1440` (bias 760), `Adjust640.CgameUiAndClientAgree` (the three
  entry points give the same result for the same input).
  **Verify:** at 1920x1080 the crosshair is centred, the HUD sits in the middle 4:3 box, the
  scoreboard background spans the width, and no stale edges remain. At 1280x1024 nothing
  changes. `cg_wideScreenHUD 0` reproduces the stretched look.

- [ ] **U1.2 Clear the pillarbox.**
  Files: `code/client/cl_scrn.c:440-442`.
  When `cls.state != CA_ACTIVE` (menus, connect, loading, cinematic) fill the whole framebuffer
  black first: `re.SetColor(g_color_table[0]); re.DrawStretchPic(0, 0, vidWidth, vidHeight, 0,
  0, 0, 0, cls.whiteShader); re.SetColor(NULL);`. This is the ioquake3 approach and also covers
  letterbox. The in-game ESC menu is not fullscreen and needs no fill.
  **Tests:** none, because it is drawing. Gate G1 with a menu screenshot at 16:9 shows black
  side bars.
  **Verify:** launch at 16:9 and the main menu shows black pillars. Alt-tab or resize leaves no
  stale frames at the edges.

- [ ] **U1.3 Scale the console and fix its layout.**
  Files: `code/client/cl_console.c` (`Con_CheckResize` at `:245-290`, `Con_DrawInput` at
  `:477`, `Con_DrawNotify` at `:502`, `Con_DrawSolidConsole` at `:580-690`),
  `code/client/cl_scrn.c` (`SCR_DrawSmallChar` at `:155`; add
  `SCR_DrawSmallCharExt(x, y, w, h, ch)`), `code/client/cl_main.c:2152`
  (`g_console_field_width`), `code/client/client.h` (`con_scale`), `code/client/cl_keys.c`
  (`Field_Draw` char width parameter).
  Add `con_scale` (`CVAR_ARCHIVE`, default `"1"`). Compute `con.charWidth = SMALLCHAR_WIDTH *
  con_scale * (vidHeight / 480.0f)` (floor 8) and `con.charHeight` likewise, once per frame in
  `Con_CheckResize`; `width = vidWidth / con.charWidth - 2` replaces the fixed
  `SCREEN_WIDTH / SMALLCHAR_WIDTH` at `:253` so the early return fires only when unchanged.
  Every `SMALLCHAR_*` use in `cl_console.c` becomes `con.charWidth` or `con.charHeight`.
  `SCR_DrawSmallChar` draws at that size. The background at `:603` uses
  `re.DrawStretchPic(0, 0, vidWidth, y * scale, ...)` directly so it spans the window, and
  `con.xadjust` drops the bias so text left-aligns with the version string at `:625` and the
  download line. `g_console_field_width` uses the same formula.
  **Tests:** `tests/test_adjust640.cpp` cases `Console.CharSizeAt640x480IsEightBySixteen`,
  `Console.ColumnsAt640x480Is78`, `Console.ColumnsAt1920x1080WithScale1`,
  `Console.ScaleTwoDoublesCharSize` (extract the formula into a testable inline).
  **Verify:** at 640x480 the console is byte identical to today. At 1080p text is readable and
  wraps at the window edge, and the background spans the width. `con_scale 2` doubles the
  glyphs. `condump` is unaffected. Resize plus `vid_restart` reflows.

- [ ] **U1.4 Rebuild the video and display menus.**
  Files: `code/q3_ui/ui_video.c` (`s_graphicsoptions`, `resolutions[]` at `:762-777`,
  `GraphicsOptions_GetInitialVideo` at `:330`, `ApplyChanges` at `:466-532`, `SetMenuItems` at
  `:632`, `GraphicsOptions_MenuInit` around `:740-1035`), `code/q3_ui/ui_display.c`
  (brightness slider; add FOV), `code/q3_ui/ui_local.h`.
  Replace the static `resolutions[]` with a list parsed from `r_availableModes` (R1.4) plus
  `Desktop` (`-2`) and `Custom` (`-1` with two `MTYPE_FIELD` entries for `r_customwidth` and
  `r_customheight`); port the ioquake3 q3_ui `GraphicsOptions_GetResolutions` and
  `GraphicsOptions_FindBuiltinResolution` logic. Apply maps a chosen `WxH` back to an
  `r_vidModes` index if one matches, else `r_mode -1`. Remove the GL Driver and GL Extensions
  controls and their `s_ivo` fields and templates (`:241-245`, `:883-900`, `:484-487`). Add spin
  controls: Fullscreen (Off, Borderless, Exclusive through `r_fullscreen` and
  `r_fullscreenExclusive`), VSync (`r_swapInterval` 0, 1, -1), Anti-aliasing
  (`r_ext_multisample` 0, 2, 4, 8), Anisotropic filtering (`r_ext_max_anisotropy` 0, 2, 4, 8,
  16), Max FPS (`com_maxfps` 0, 60, 125, 144, 250, applied at once). Add an FOV slider
  (`cg_fov` 80 to 130) in the Display menu next to Brightness. `ApplyChanges` keeps the
  "restart only if a latched value changed" logic, extended to the new latched cvars. The
  Driver Info screen works once R1.1 fills the strings; keep the 40-entry truncation.
  **Tests:** none, because it is menu code inside the UI module. Covered by the manual check.
  **Verify:** the menu lists real modes for the current display. Choosing 1920x1080 plus
  Borderless and Apply restarts into it. VSync and Max FPS change without a restart. Custom
  accepts 2560x1080. Settings persist in `q3config.cfg`.

- [ ] **U1.5 Fix mouse grab, wheel, focus, clipboard, and keys.**
  Files: `code/sys/sys_sdl.cpp` (`Sys_SendKeyEvents`, `IN_Frame`, `IN_Init`,
  `TranslateSDLKey`), `code/client/cl_main.c` (register `in_nograb`). `Sys_GetClipboardData`
  moves to `SDL_GetClipboardText` in checklist 01; if that has not landed, do it here in
  `sys_sdl.cpp` and leave a stub for the dedicated build.
  - `SDL_MOUSEWHEEL` queues `K_MWHEELUP` or `K_MWHEELDOWN` down-and-up pairs from
    `event.wheel.y` (honour `SDL_MOUSEWHEEL_FLIPPED`). Console scrolling (`cl_keys.c:567-576`)
    and weapon cycling start working.
  - `IN_Frame` implements the ioquake3 policy: deactivate relative mode when
    `(cls.keyCatchers & KEYCATCH_CONSOLE) && !glConfig.isFullscreen`, when `!s_windowFocused`,
    or when `in_nograb`; otherwise activate. Menus keep the grab because they consume deltas.
    Remove the unconditional grabs at `sys_sdl.cpp:222,369`. Set hints before
    `SDL_InitSubSystem(SDL_INIT_VIDEO)`: `SDL_HINT_MOUSE_RELATIVE_MODE_WARP=0`,
    `SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH=1`, `SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS=0`.
  - `TranslateSDLKey` adds `SDLK_PRINTSCREEN`, `SDLK_SCROLLLOCK`, `SDLK_NUMLOCKCLEAR`,
    `SDLK_LGUI`, `SDLK_RGUI`, and a scancode fallback for non-ASCII layouts
    (`SDL_GetKeyFromScancode` to ASCII, else world keys). The console toggle uses
    `SDL_SCANCODE_GRAVE` so it works on every layout.
  **Tests:** none for the SDL event handling. The keycode table gets
  `tests/test_controller_map.cpp` case `KeyMap.EveryTranslatedKeyHasAName` shared with U1.6.
  **Verify:** the wheel scrolls the console and cycles weapons. Opening the console in a window
  frees the cursor and closing it re-grabs. Alt-tab releases. `Ctrl+V` pastes into the console.
  AZERTY and German layouts can open the console.

- [ ] **U1.6 Add controller axes and rebindable buttons.**
  Files: `code/sys/sys_sdl.cpp` (controller cases at `:436-473`, `IN_Init`),
  `code/client/cl_input.c` (`CL_JoystickMove` at `:383`; add `j_yaw`, `j_pitch`, `j_forward`,
  `j_side`, `j_up` speed cvars from ioquake3), `code/client/cl_main.c` (register `in_joystick`,
  `in_joystickThreshold`, `in_joystickUseAnalog`, `j_*`), `code/ui/keycodes.h` and
  `code/client/cl_keys.c` key names (add `K_PAD0_A` to `K_PAD0_RIGHTTRIGGER` per ioquake3
  before `K_LAST_KEY`), `code/q3_ui/ui_controls2.c:1538-1548` (`joy_threshold` becomes
  `in_joystickThreshold`).
  Replace the five hardcoded cases with a table mapping every `SDL_GameControllerButton` to a
  `K_PAD0_*` key (15 buttons plus guide). `SDL_CONTROLLERAXISMOTION` queues
  `Sys_QueEvent(SE_JOYSTICK_AXIS, axis, value)` for the sticks (`LEFTX` to `AXIS_SIDE`, `LEFTY`
  to `AXIS_FORWARD`, `RIGHTX` to `AXIS_YAW`, `RIGHTY` to `AXIS_PITCH`) after the
  `in_joystickThreshold` deadzone, and the triggers become `K_PAD0_LEFTTRIGGER` and
  `K_PAD0_RIGHTTRIGGER` key events with hysteresis. Gate everything on `in_joystick`. On the
  first controller connect with no `PAD0_*` binds, exec an embedded default binding string
  (fire, jump, weapon next and previous, scoreboard, menu). Load `gamecontrollerdb.txt` with
  `SDL_GameControllerAddMappingsFromFile` when it exists in `fs_homepath`.
  **Tests:** new `tests/test_controller_map.cpp` (`quake3_tests`; put the table and the
  deadzone and hysteresis functions in a header without SDL calls). Cases:
  `ControllerMap.EveryButtonHasAKey` (every `SDL_GameControllerButton` value below
  `SDL_CONTROLLER_BUTTON_MAX` maps to a `K_PAD0_*` key), `ControllerMap.DeadzoneZeroesSmallValues`,
  `ControllerMap.TriggerHysteresis` (press at 0.6, release at 0.4, no chatter at 0.5),
  `KeyMap.EveryTranslatedKeyHasAName`.
  **Verify:** `bind PAD0_A +moveup` works from the console and the Controls menu shows `PAD0_A`.
  Sticks move and look with an adjustable `in_joystickThreshold`. Hot-plug adds and removes
  without a crash. `in_joystick 0` silences everything. (Manual, from a CI artifact.)

- [ ] **U1.7 Add master server cvars.**
  Files: `code/client/cl_main.c` (`CL_GlobalServers_f` at `:2885-2930`, `CL_Init`),
  `code/qcommon/qcommon.h:237` (keep `MASTER_SERVER_NAME` as one default), `code/server/sv_init.c`
  (`sv_master1` default at `:606`), `code/q3_ui/ui_servers2.c` (master items, `globalservers`
  call; optionally add `Internet 2`).
  Register `sv_master1` to `sv_master5` (`CVAR_ARCHIVE`) as ioquake3 does with the defaults from
  decision R10. `globalservers <0-4> <protocol> [keywords]` resolves `sv_master(N+1)`; an empty
  cvar prints an error. Keep `cls.masterNum`. Handle the ioquake3 extended
  `getserversResponse` (same format for protocol 68). The status string at `ui_servers2.c:434`
  includes the host name.
  **Tests:** new `tests/test_master_cvars.cpp` (`quake3_tests`). Cases:
  `MasterCvars.IndexMapsToCvarName` (0 maps to `sv_master1`, 4 to `sv_master5`),
  `MasterCvars.OutOfRangeIndexRejected`, `MasterCvars.EmptyHostProducesErrorString`.
  **Verify:** `globalservers 0 68` populates the Internet browser with live servers. Setting
  `sv_master1` to garbage yields the no-response message with the host rather than a hang.

- [ ] **U1.8 Add Hor+ field of view.**
  Files: `code/cgame/cg_view.c` (`CG_CalcFov` at `:485-530`), `code/cgame/cg_main.c`
  (register `cg_horplus`, `CVAR_ARCHIVE`, default `"1"`).
  After computing `fov_x` (including the zoom lerp), when `cg_horplus` treat it as the 4:3
  horizontal FOV: `fov_y = 2 * atan(tan(fov_x * M_PI / 360) * 0.75) * 360 / M_PI;` then
  `fov_x = 2 * atan(tan(fov_y * M_PI / 360) * cg.refdef.width / cg.refdef.height) * 360 / M_PI;`
  clamped to 170. This replaces the block at `:524-526`. `cg_weapons.c:1413` keeps using
  `cg_fov` (a 4:3 value), so the gun offset stays correct.
  **Tests:** new `tests/test_fov.cpp` (`quake3_tests`; extract the math into an inline in
  `bg_public.h` or a small header). Cases: `Fov.HorPlusAt16x9KeepsVerticalExtent` (`cg_fov 90`
  at 1920x1080 gives the same `fov_y` as at 1024x768), `Fov.HorPlusWidensHorizontal`
  (`fov_x` at 16:9 is greater than 90), `Fov.ClampAt170`, `Fov.HorPlusOffReproducesVertMinus`
  (the old formula result at 16:9).
  **Verify:** at 16:9 with `cg_fov 90` the vertical extent matches a 1024x768 run (same top and
  bottom geometry visible, more on the sides). `cg_horplus 0` reproduces the old cropping. Zoom
  still lerps smoothly.

- [ ] **U1.9 Coordination notes.**
  `--help`, missing-pak messaging, the CD-key gate, and frame pacing belong to checklist 02.
  This checklist supplies the `com_minimized` and focus hooks if that track asks. Record any
  cross-track hand-off you make in `docs/plans/README.md`.
  **Tests:** none.
  **Verify:** the README index reflects the hand-offs.

## Test map

| Test file | Binary | Cases | Added by |
|---|---|---|---|
| `tests/test_glext.cpp` | `quake3_tests` | HaveExtensionTokenisesSyntheticString, S3tcEnumSelectionForRgbAndRgba, ExtensionsDisabledClearsPointers, AnisotropyClampedToDeviceMax, CoreProfileExtensionListFromGetStringi | R1.1, R1.6, R2.4 |
| `tests/test_mode_table.cpp` | `quake3_tests` | EveryRowRoundTrips, CustomMode, DesktopMode, InvalidModeReturnsFalse, AvailableModesStringIsDeduplicated, NoDuplicateCvarRegistration | R1.4 |
| `tests/test_glsl_headers.cpp` | `quake3_tests` | CompatHeaderIsVersion120WithShims, CoreHeaderIsVersion150Core, UniformNamesInSourceMatchTable, AttributeLocationsAreStable | R2.2 |
| `tests/test_adjust640.cpp` | `quake3_tests` | ScaleAndBiasAt640x480, ScaleAndBiasAt1920x1080, ScaleAndBiasAt1280x1024, ScaleAndBiasAt3440x1440, CgameUiAndClientAgree, Console.CharSizeAt640x480IsEightBySixteen, Console.ColumnsAt640x480Is78, Console.ColumnsAt1920x1080WithScale1, Console.ScaleTwoDoublesCharSize | U1.1, U1.3 |
| `tests/test_controller_map.cpp` | `quake3_tests` | EveryButtonHasAKey, DeadzoneZeroesSmallValues, TriggerHysteresis, KeyMap.EveryTranslatedKeyHasAName | U1.5, U1.6 |
| `tests/test_master_cvars.cpp` | `quake3_tests` | IndexMapsToCvarName, OutOfRangeIndexRejected, EmptyHostProducesErrorString | U1.7 |
| `tests/test_fov.cpp` | `quake3_tests` | HorPlusAt16x9KeepsVerticalExtent, HorPlusWidensHorizontal, ClampAt170, HorPlusOffReproducesVertMinus | U1.8 |

Gates that are not GoogleTest: G1 A/B screenshots for `r_vbo`, `r_glsl`, `r_glCoreProfile`,
and `r_fbo`; G2 apitrace call counts; G3 GL error run. Scripts live under `ci/smoke/`.

## Out of scope and follow-ons

- R2.6 static world VBO with GPU colour and texcoord generation.
- Team Arena UI (`code/ui`) is not built and is not fixed.
- Vulkan backend: checklist 09.
- Frame pacing, `--help`, missing-pak message, CD-key gate: checklist 02.

## Done criteria

- R1 exit: `gfxinfo` shows real driver strings and capabilities; overbright and gamma work in a
  window on Wayland and macOS; VSync applies at runtime; first run is native borderless
  fullscreen; resize and focus behave; MSAA and anisotropy are available.
- R2 exit: zero fixed-function GL calls (G2 prints `0`); core profile is the default on all
  three platforms; `r_vbo` and `r_glsl` are removed or `CVAR_ROM`; G1 pixel identical for the
  VBO step and visually identical for the GLSL step across the whole test map set; G3 clean;
  `docs/renderer.md` describes the ring-buffer streaming and the two programs.
- U1 exit: at 1920x1080 the HUD sits in the centred 4:3 box and the scoreboard background spans
  the width; menus show black pillars; the console is readable at 1080p; the video menu lists
  real modes and the new controls; the wheel scrolls; the mouse releases on console and focus
  loss; a controller moves and aims; `globalservers 0 68` returns servers; `cg_fov 90` at 16:9
  keeps the 4:3 vertical extent.
- Every row of the test map exists and passes under `ctest --preset dev` and
  `ctest --preset asan` in the container.

## Last step

- [ ] Delete this file and remove its row from `docs/plans/README.md`.
