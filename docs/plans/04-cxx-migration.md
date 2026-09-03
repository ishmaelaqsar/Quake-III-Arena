# Checklist 04: C to C++17 migration

## Purpose

Move the engine, the renderer, botlib, and the three game modules from C11 to C++17. Phase 1
compiles every built `.c` file as C++ with the structure unchanged. Phase 2 rewrites subsystems
in an idiomatic style behind the existing C application programming interfaces (APIs), one
subsystem per pull request, so the tree stays shippable at every step.

**Status:** In progress. Phase P0 complete and phase P1 step P1.1 complete on 3 September 2026 (qcommon, shared game files, and null stubs build and link as C++). Next: P1.2 server. Phase P0 steps P0.1 to P0.6 done on 2 September 2026 (flags, self-guarding headers, keyword renames, const sweep, qboolean as int, unmangled module symbols). Open: P0.7, P1, P2.

## Prerequisites

- Checklist 00 is complete: the Docker image, the Makefile targets, and the golden-screenshot
  harness under `ci/smoke/` exist.
- Checklist 01 has landed the CMake restructure (OBJECT libraries, `Q3_ARCH`, `Q3_DLL_EXT`), so
  you can gate flags per language with `$<$<COMPILE_LANGUAGE:C>:...>`.
- Checklist 02 step B1 (the 64-bit virtual machine ABI fix) is complete **in C** before any
  rename. It edits the same `*_syscalls.c` files this checklist renames.
- The platform layer files that checklist 01 creates under `code/sys/` (`sys_main`, `sys_dll`,
  `sys_files_*`, `sys_unix`, `sys_win32`, `net/sys_net`) are authored as `.cpp` from day one.
  If they land as `.c`, rename them in PR 4 with the client.

### Owner decisions already taken

| # | Decision | Default the plan uses |
|---|---|---|
| C1 | JPEG library | `stb_image.h` and `stb_image_write.h`. The swap is gated by peak signal-to-noise ratio (PSNR) at or above 45 dB, not by pixel identity. Use libjpeg-turbo only if bit-exact decode becomes a hard requirement. |
| C2 | `-fno-strict-aliasing` | Add it now to every legacy target. Remove it per subsystem in phase 2 after `q3::bit_cast` replaces the type punning. Never remove it for `cm_*`, `msg.c`, `vm_interpreted.c`, or botlib. |
| C3 | String literals into `char*` | Sweep about 15 struct fields and 6 signatures to `const char*`. Do not suppress the warning. MSVC `/permissive-` makes suppression costly. |
| C4 | `qboolean` | Change to `typedef int qboolean; enum { qfalse = 0, qtrue = 1 };` in the prep PR. The ABI is identical (4 bytes on all targets). |
| C5 | Exceptions and run-time type information (RTTI) | On everywhere. Every legacy-to-`code/sys` entry point uses `Q3_NOEXCEPT_BOUNDARY`. `Com_Error` becomes a C++ exception as the first phase-2 step. |
| C6 | Third-party C | Keep `code/qcommon/unzip.c` and `code/qcommon/md4.c` as C. Convert `code/client/snd_adpcm.c`. Leave `code/game/bg_lib.c` untouched (QVM only, not built). |
| C7 | Rename mechanism | `git mv` per directory. One PR per directory with a rename-only commit and a fix commit. Use `set_source_files_properties(... LANGUAGE CXX)` only as an uncommitted scouting tool. |

## Background

The survey ran against commit `ad3705e`. Re-verify an anchor with `grep -n` before you edit,
because other checklists move code.

| Item | Finding |
|---|---|
| Built C today | qcommon 27.4k lines, client 14.8k, server 8.6k, renderer 21.5k, botlib 32.5k, game 43.6k, cgame 25.8k, q3_ui 24.0k (plus `code/ui/ui_syscalls.c`), jpeg-6 21.7k (34 of 51 files built), `code/null` 3 files, `code/unix` 5 files. `code/ui` (Team Arena) is not built. `q_shared.c` and `q_math.c` compile into qcommon and into each of the three modules. |
| Already C++ | `code/sys/**` (9 `.cpp`, Sol2, exceptions used in 17 places), `code/renderer/vulkan/vk_backend.cpp`, all 18 tests. They consume legacy headers through `extern "C" { #include ... }` in `tests/*.cpp`, `code/sys/sys_sdl.hpp`, and `code/sys/sys_sdl.cpp`. |
| Header guards | Only `q_shared.h` and `qcommon.h` have `extern "C"` guards. 60 headers have no include guard at all (`client.h`, `server.h`, `snd_local.h`, `cm_local.h`, `vm_local.h`, all of botlib, most of game). None use `#pragma once`. |
| Keyword collisions | Struct field `orientationr_t or` at `code/renderer/tr_local.h:473`, `:802`, `:863`, plus parameters at `:1061` and `:1258`, about 155 uses across 13 renderer files. `new` in `code/qcommon/common.c:913-969` (the `Z_Free` rover). `operator` as a struct field in `code/botlib/l_precomp.c:1609`, 56 uses. `delete` as a field in `code/q3_ui/ui_removebots.c:43`, 12 uses. `true`/`false` only in the unbuilt `null_net.c` and `mac_net.c`. `and`/`or`/`xor` only in MSVC x86 inline assembly in `q_math.c`, `vm_x86.c`, and `snd_mix.c`, which is dead under x86_64. The botlib `#define qtrue true` sits under `#ifdef SCREWUP` (BSPC only). |
| Implicit `void*` to `T*` | 169 uncast allocation assignments: qcommon 67, renderer 58, botlib 17, server 10, unix 8, client 5, game 4. Plus 247 `VMA(n)` uses that pass `void*` into typed parameters: `code/server/sv_game.c` 119, `code/client/cl_cgame.c` 70, `code/client/cl_ui.c` 58. Plus `dlsym` to function pointer in `code/unix/unix_main.c:785-786` and `code/unix/linux_qgl.c:3038` (both replaced by checklist 01). |
| String literals into `char*` | Table structs with non-const fields: `gitem_t` (`code/game/bg_public.h:627-640`, 249 literal rows in `bg_misc.c`), `cvarTable_t` in `code/cgame/cg_main.c:200`, `code/game/g_main.c:30`, `code/q3_ui/ui_main.c:97`, `netField_t` in `code/qcommon/msg.c:778`, `infoParm_t` in `code/renderer/tr_shader.c:1313`, `menubitmap_s.focuspic`/`errorpic` and `menutext_s.string` in `code/q3_ui/ui_local.h:240-252`, `gentity_t.classname`/`model`/`model2`/`message`/`target` in `code/game/g_local.h:82-122`. About 194 `.name`/`.string`/`.focuspic = "..."` assignments in q3_ui, cgame, and game. Non-const API parameters that receive literals: `va(char *format, ...)` at `code/game/q_shared.h:907`, `COM_ParseError`/`COM_ParseWarning`, `CL_ConsolePrint(char*)`, `SaveJPG(char*)`, `Hunk_AllocDebug`/`Z_*Debug(char *label, char *file)`, `COM_MatchToken(char**, char*)`. |
| `qboolean` | `typedef enum {qfalse, qtrue} qboolean` at `code/game/q_shared.h:335`. Every `x = a && b;` into a `qboolean` is an int-to-enum error in C++ and is not visible to grep. Expect dozens per directory. |
| Type punning | 91 `*(int*)&f` sites: qcommon 69, game 10, renderer 9, client 2, cgame 1. `-fno-strict-aliasing` is not set anywhere in `CMakeLists.txt` today. |
| Absent in this tree | Designated initializers (0), compound literals (0), variable-length arrays (0), `restrict` (0), `_Bool`/`stdbool`/`_Static_assert`/`_Generic` (0), K&R definitions (0), anonymous unions (0; the `union {} dat` in `msg.c:297` is named), `min`/`max` macros (only in jpeg-6 and the unbuilt `code/splines`), function pointers with empty parentheses (0). `goto`: 106, of which 75 are in `vm_interpreted.c` dispatch. `offsetof`: 9, all on C-layout structs. |
| Error unwinding | `Com_Error` calls `longjmp(abortframe)` at `code/qcommon/common.c:286-304`. The matching `setjmp` sites are `Com_Frame` at `:2359` and `Com_Init` at `:2656`. A `longjmp` across frames with non-trivial destructors is undefined behaviour, so RAII below `Com_Frame` is unsafe until `Com_Error` becomes an exception. |
| JPEG | `code/renderer/tr_image.c` uses jpeg-6 through id's memory-source hack `jpeg_stdio_src(cinfo, unsigned char *infile)` at `code/jpeg-6/jdatasrc.c:174` and a custom in-memory destination manager at `tr_image.c:1508-1695`. `LoadJPG` reads RGB and expands to RGBA with alpha 255 (about `tr_image.c:1465`). `SaveJPG` uses quality 95 from `R_ScreenShotJPEG_f` at `code/renderer/tr_init.c:427`. `jdphuff.c` is not built, so progressive JPEG is unsupported today. |
| CMake flags | `add_compile_options` applies `-Wno-implicit-function-declaration -Wno-int-conversion` to both languages. GCC warns "valid for C but not C++" when files become `.cpp`. No `COMPILE_LANGUAGE` gating exists. |
| Existing modern code to reuse | `code/sys/cvar/cvar_manager.hpp` (`q3::CvarManager`), `code/sys/fs/vfs.hpp` (`VirtualFileSystem`), `code/sys/math/vec3.hpp` (`q3::math::Vec3`), `code/sys/logger/logger.hpp`. |

## Ordering relative to other checklists

1. Checklist 01 CMake restructure, then checklist 02 step B1 (VM ABI, in C).
2. C-P0 prep PR (this file), then C-P1 PRs 1 to 4 (qcommon, server, renderer, client).
3. Checklist 05 threading T1 and checklist 08 R1 start only after PR 3 and PR 4, so the renderer
   and the client are already C++. This avoids rename conflicts with those tracks.
4. C-P1 PRs 5 to 8 (botlib, game, cgame, q3_ui).
5. C-P1 PR 9 (JPEG swap) and PR 10 (header close-out) after checklist 01 has deleted `code/unix`.
6. C-P2 step 0 (`Com_Error` as exception) before any other idiomatic step.
7. C-P2 steps 1 to 5 interleave with checklist 08 R2.

## Incompatibility catalogue

Run each grep inside the directory you are converting. Fix by class, with `sed` where the
pattern is unambiguous, otherwise by hand.

| # | Issue | Occurs here | Grep | Fix |
|---|---|---|---|---|
| 1 | Implicit `void*` to `T*` from allocators | Yes, 169 sites | `grep -nE '=\s*(Z_Malloc\|Z_TagMalloc\|S_Malloc\|Hunk_Alloc\|Hunk_AllocateTempMemory\|ri\.Hunk_Alloc\|ri\.Hunk_AllocateTempMemory\|ri\.Malloc\|G_Alloc\|GetMemory\|GetClearedMemory\|GetHunkMemory\|GetClearedHunkMemory\|CopyString\|malloc\|calloc\|realloc)\s*\(' *.c \| grep -vE '=\s*\(\s*\w[\w ]*\*+\s*\)'` | Phase 1: C-style cast to the left-hand type. Add `template<class T> T* Z_New(int n = 1)` and `Hunk_New<T>(n, pref)` in `qcommon.h` inside `#ifdef __cplusplus` and outside the `extern "C"` block. Use them where the left-hand side is a plain `T*`. Keep casts for `ri.Hunk_Alloc` (function pointer). `FS_ReadFile((void**)&buf)` is already explicit. |
| 2 | `void*` from `VM_ArgPtr` | 247 uses | `grep -c 'VMA(' code/server/sv_game.c code/client/cl_cgame.c code/client/cl_ui.c` | `VmArg` proxy (see PR 2). |
| 3 | `void*` to function pointer (`dlsym`) | `code/unix` only | `grep -rn 'dlsym\|GetProcAddress' code` | `reinterpret_cast`. Owned by checklist 01. |
| 4 | C++ keywords as identifiers | `or`, `new`, `operator`, `delete` | See the one-liner in C-P0 step 3 | Rename in C-P0. |
| 5 | `int` to enum (`qboolean` and others) | `qboolean`: dozens per directory. Others: `entity_event_t event = es->event & ~EV_EVENT_BITS`, `weapon_t`/`team_t` from `atoi`, renderer `cullType_t`, `genFunc_t` from parsed ints | Not greppable. Compile with clang and filter `error: cannot initialize a variable of type .* with an rvalue of type 'int'` and `assigning to .* from incompatible type 'int'`. Pre-grep: `grep -nE '\b(weapon_t\|team_t\|netsrc_t\|connstate_t\|entity_event_t\|gametype_t\|cullType_t\|genFunc_t\|deform_t\|alphaGen_t\|colorGen_t\|texCoordGen_t\|leType_t\|itemType_t\|moverState_t\|trType_t\|pmtype_t\|footstep_t\|impactSound_t)\s+\w+\s*=' *.c` | `qboolean`: decision C4 removes the class. Other enums: cast at the site with `(enum_t)`, or change the variable to `int` where it is a bit field (`event & ~EV_EVENT_BITS`). |
| 6 | String literal to `char*` | About 700 sites, fixed by about 15 struct-field consts and 6 signatures | `grep -rnE '(^\|[^a-zA-Z_])char\s*\*\s*\w+(\[\w*\])?\s*=\s*(\{\s*)?"' *.c *.h \| grep -v const` and `grep -rnE '^\s*\{?\s*"' bg_misc.c tr_shader.c be_ai_weap.c` | C-P0 step 4. Then enable `-Werror=write-strings` (GCC) or `-Werror=writable-strings` (clang) per converted directory. |
| 7 | `strchr`/`strstr`/`strrchr` on `const char*` return `const char*` in C++ | 21 candidates | `grep -nE '^\s*\w+\s*=\s*(strchr\|strrchr\|strstr\|memchr)\s*\(' *.c` | Make the left-hand side `const char*` where the input is const. Otherwise cast. |
| 8 | `goto` or `switch` that jumps over an initialization | 106 gotos; `vm_interpreted.c` has 75 in one function | `grep -nw goto *.c`; compile error `jump bypasses variable initialization` | Move the declaration above the label, or brace the `case` body. |
| 9 | Missing `return` in a non-void function | `q_shared.h:159` `BigLong` under a Mac `#if`; others possible | `-Werror=return-type` | Add the `return`. |
| 10 | Designated initializers, compound literals, VLAs, `restrict`, `_Bool`, `_Static_assert`, K&R, empty-parenthesis function pointers, anonymous unions | None (verified) | Greps in Background | Nothing. |
| 11 | Empty-parenthesis prototypes `void F();` | 26 in headers, all called with no arguments | `grep -rnE '\w+\s*\(\s*\)\s*;' *.h` | Leave. Optional `(void)` sweep. |
| 12 | `sizeof` of incomplete types, `offsetof` on non-standard-layout types | 9 `offsetof`, all standard-layout C structs | `grep -rn offsetof` | Nothing in phase 1. Phase 2 rule: never add constructors or virtual functions to `entityState_t`, `playerState_t`, `gentity_t`, `iteminfo_t`, `weaponinfo_t`, `projectileinfo_t`, `md4Frame_t`, `srfSurfaceFace_t`. Netcode and botlib depend on their offsets. |
| 13 | `bool`/`true`/`false` macros | None live (`null_net.c`, `mac_net.c` unbuilt; botlib under `SCREWUP`) | `grep -rn 'define\s*\(bool\|true\|false\)' code` | Delete `code/null/null_net.c`, `mac_net.c`, `null_main.c`, `null_glimp.c` (dead). |
| 14 | `min`/`max` macros versus `<algorithm>` | Only jpeg-6 (`jpegint.h:265`) | None | Goes away with jpeg-6 (PR 9). |
| 15 | `Q_vsnprintf`/`_vsnprintf` | Windows macro in `q_shared.h`; C++ `<cstdio>` provides `vsnprintf` | `grep -rn vsnprintf code/game/q_shared.h` | Keep. |
| 16 | `extern "C"` on `sys_api.h` symbols | Yes | None | Keep the guards until `code/unix` is gone. Then optional. |
| 17 | `VM_DllSyscall(int arg, ...)` with `va_arg(ap, intptr_t)` | `vm.c:333-345`; C++ accepts it | None | Checklist 02 B1 changes it to `intptr_t`. Nothing for phase 1. |
| 18 | Struct tag versus typedef name reuse, duplicate typedefs | C++ allows identical redeclared typedefs | Compile | None expected. |
| 19 | Implicit int-to-float or double narrowing in brace initialization | 10 candidates (float literals in tables, legal) | `-Wnarrowing` errors only for constant float to int | Fix per compiler error. |
| 20 | Inline assembly blocks with `and`/`or`/`xor` | MSVC x86-only `#ifdef` blocks in `q_math.c`, `vm_x86.c`, `snd_mix.c` | `grep -n '__asm' code/game/q_math.c code/client/snd_mix.c` | Delete the `#if id386 && _MSC_VER` blocks. They are dead on x86_64 and MSVC x64 has no inline assembly. |
| 21 | Character literal and `unsigned char` promotion, `%s` with `byte*` | `-Wformat` noise | None | `-Wno-format-security` is already off. Fix the real ones. |
| 22 | C++ linkage of `main` and callbacks passed to C libraries (`qsort` comparators, SDL) | Comparators are plain functions | None | Fine. C++ function pointers are compatible and `qsort` needs no `extern "C"`. |

Per-directory process:

```sh
# 1. scout (do not commit): flip the language, list the error classes
cmake -S . -B build/scout -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang
ninja -C build/scout 2>&1 | grep 'error:' | sed 's/.*error: //' | sort | uniq -c | sort -rn
# 2. git mv, update CMakeLists.txt paths (commit A)
# 3. fix by class (commit B), build clang, then gcc, then push for the MSVC leg
```

| 23 | **File-local forward declarations** of functions that are defined in a translation unit which is still C. The declaration in the renamed `.cpp` file asks for a mangled symbol; the definition emits an unmangled one, so the link fails. This is not a header problem, so the `extern "C"` sweep in P0.2 does not catch it. | Yes: it broke the link after P1.1 with ten symbols | `grep -nE '^[a-z_]+ *\**[A-Za-z_]+\(.*\);$' <renamed>.cpp` and check each name against `grep -rl "<name>" code --include=*.c` | Wrap the declaration in `extern "C"`, and confirm the definition's translation unit includes a header that also gives it C linkage. Do not simply delete the declaration. Applies symmetrically in reverse: after the definition side converts, both sides must agree, so prefer moving the declaration into the guarded header that both include. Sites fixed in P1.1: `code/qcommon/common.cpp:95,1585-1587,2240` and `code/qcommon/cm_patch.cpp:1613`; `code/null/null_input.cpp` now includes `code/sys/sys_local.h` for the same reason. |

| 24 | **Clang rejects what GCC only warns about.** Two families showed up only on the macOS leg. First, the `register` storage class specifier is removed in C++17: GCC emits `-Wregister` but Apple Clang makes it an error. Second, Clang folds discarded `const` qualifiers into `-Wincompatible-pointer-types-discards-qualifiers`, which the build promotes with `-Werror=incompatible-pointer-types`, while GCC keeps them in `-Wdiscarded-qualifiers` and stays quiet. A GCC-clean rename is therefore not portable-clean. | Yes: 1 `register` and 23 qualifier sites after P0 and P1.1 | `grep -rn '\bregister\b' <renamed>.cpp` and build once with `-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -- -k 0` | Drop `register`. For each qualifier site decide which side is wrong: propagate `const` when the callee only reads (`G_ModelIndex`, `G_SoundIndex`, `G_FindConfigstringIndex`, `G_PickTarget`, and the locals in `CG_RegisterItemSounds` and `cg_weapons.c`), or revert the field when it is a write target (`menutext_s.string`). **Build every rename with both compilers before ticking it**, because the continuous integration macOS leg does. |

| 25 | **MSVC decorates global variable names; the Itanium ABI does not.** A file-local `extern int x;` in a C++ file whose definition has C linkage links fine with GCC and Clang, because a namespace-scope variable's symbol is just its name there, and fails on MSVC, which encodes the type into the symbol (`?c_traces@@3HA`). So this is row 23 for variables, and it is invisible on every platform except Windows. Note also that a linkage specification is only allowed at namespace scope: `extern "C"` cannot wrap a declaration inside a function body, so such declarations have to be hoisted. | Yes: `c_traces`, `c_brush_traces`, `c_patch_traces`, `c_pointcontents`, `cl_shownet` twice, `com_fullyInitialized`, `oldsize`, and `stdin_active` | `grep -rnE '^[[:space:]]*extern[[:space:]]+(int\|cvar_t\|qboolean\|float\|char\|unsigned)' code/**/*.cpp \| grep -v 'extern "C"'` | Give the declaration the same linkage as the definition, hoisting it to namespace scope when it sits in a function. Better still, declare the variable once in the header that owns it and delete the local declaration. |

## Compiler flags

Set on the converted legacy OBJECT libraries (qcommon, server, client, renderer, botlib, and the
game modules) with `target_compile_options` and `$<$<COMPILE_LANGUAGE:CXX>:...>`:

- `CMAKE_CXX_STANDARD 17`, `CMAKE_CXX_EXTENSIONS OFF`. Exceptions and RTTI on. Do not use
  `-fno-exceptions` in any translation unit. Sol2 and `code/sys` need both, and mixing them is an
  ABI hazard.
- `-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-sign-compare
  -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable
  -Wno-deprecated-declarations -Wno-format-security -Wno-implicit-fallthrough
  -Wno-class-memaccess -Werror=return-type`. Add `-Werror=write-strings` (GCC) or
  `-Werror=writable-strings` (clang) after C-P0 step 4 has landed.
- `-Wold-style-cast` off in phase 1. The fixes add hundreds of C casts by design.
- `-fno-strict-aliasing` on every legacy target (decision C2). Phase 2 removes it per subsystem
  after `q3::bit_cast<T>` (a `memcpy`-based helper in `code/sys/util/bit_cast.hpp`) replaces the
  punning. Verify each removal with `-Wstrict-aliasing=2 -O2` and the pixel gate. Undefined
  behavior sanitizer does not catch aliasing.
- Keep `-D_GNU_SOURCE`. `ARCH_STRING` comes from checklist 01's `Q3_ARCH`.

Set on `code/sys` and on all new phase-2 code: `-Wall -Wextra -Wpedantic -Wold-style-cast -Wshadow
-Werror`.

MSVC: `/permissive- /Zc:__cplusplus /Zc:preprocessor /W3 /EHsc /GR`, `_CRT_SECURE_NO_WARNINGS`
on legacy targets. `/permissive-` implies `/Zc:strictStrings`, so the const sweep is mandatory for
the Windows leg. Under `/permissive-` the alternative token `or` is a keyword. Expect C4838
narrowing in brace tables. `#pragma warning(disable:4018)` at `code/game/q_shared.h:29` stays.

## Steps

### C-P0 prep PR (tree stays 100% C, about 4 days)

- [x] **P0.1 Gate the C-only flags and add `-fno-strict-aliasing`.** Done on 2 September 2026. In `CMakeLists.txt`, move
  `-Wno-implicit-function-declaration -Wno-int-conversion` (and the `-Werror=` versions that
  checklist 01 adds) behind `$<$<COMPILE_LANGUAGE:C>:...>`. Add `-fno-strict-aliasing` to every
  legacy target.
  **Tests:** none, because this is a flag change; the full ctest run is the check.
  **Verify:** `make build` succeeds with GCC and clang; `ninja -v` shows the C flags
  only on `.c` compile lines.

- [x] **P0.2 Self-guarding headers.** Done on 2 September 2026. Add `#pragma once` to the 60 unguarded headers. Add
  `#ifdef __cplusplus extern "C" {` and the closing `}` to every legacy header that declares
  functions and is consumed by a C++ translation unit now or during landing: `client.h`,
  `server.h`, `snd_local.h`, `snd_public.h`, `keys.h`, `tr_public.h`, `tr_local.h`, `qgl.h`,
  `cm_public.h`, `cm_local.h`, `vm_local.h`, `unzip.h`, `botlib.h`, all `be_*.h`, all `l_*.h`,
  `g_public.h`, `g_local.h`, `bg_public.h`, `cg_public.h`, `cg_local.h`, `ui_public.h`,
  `ui_local.h`. `sys_api.h` already has guards. Then remove the `extern "C" { #include ... }`
  wrappers from `tests/*.cpp`, `code/sys/sys_sdl.hpp`, and `code/sys/sys_sdl.cpp`. With the
  headers owning their linkage, any mix of `.c` and `.cpp` translation units links.
  **Tests:** none, because linkage is checked by the build; `grep -rn 'extern "C"' tests code/sys`
  returns only `sys_api.h` guards.
  **Verify:** `make build && make test` green; the tests binary links without
  the wrappers.

- [x] **P0.3 Keyword renames.** Done on 2 September 2026. Pure C, zero behaviour change. In `code/renderer/tr_local.h`
  rename the `orientationr_t or` field and parameters to `orient`, then `sed` `\.or\b` to
  `.orient` and `&backEnd.or` to `&backEnd.orient` across `code/renderer`. In
  `code/qcommon/common.c:913-969` rename `new` to `newblock`. In `code/botlib/l_precomp.c` rename
  the `operator` field (line 1609) and its 56 uses to `op`. In `code/q3_ui/ui_removebots.c`
  rename the `delete` field (line 43) and its 12 uses to `deleteBtn`. Check the full keyword set
  with comments and strings stripped:

  ```sh
  for f in $(git ls-files 'code/*.c' 'code/*.h'); do
    perl -0777 -pe 's{/\*.*?\*/}{}gs;s{//[^\n]*}{}g;s{"(?:\\.|[^"\\])*"}{""}g' "$f" \
    | grep -nwE 'new|class|this|template|operator|typename|export|bool|true|false|delete|private|public|protected|namespace|friend|virtual|mutable|explicit|using|try|catch|throw|and|or|not|xor|compl|bitand|bitor|nullptr|constexpr|noexcept|decltype|alignas|alignof|static_assert|thread_local|wchar_t|typeid' \
    | sed "s|^|$f:|"
  done
  ```

  The remaining hits must be inside `#if id386 && _MSC_VER` assembly blocks (delete those
  blocks, catalogue row 20) or in unbuilt files.
  **Tests:** none, because the renames are mechanical; the symbol parity check is the test.
  **Verify:** the loop prints nothing outside deleted blocks; `nm --defined-only` of
  `libq3renderer.a` shows no symbol change (fields are not symbols).

- [x] **P0.4 Const sweep (decision C3).** Done on 2 September 2026. Change signatures: `va(const char *format, ...)`
  (`q_shared.h:907` and `q_shared.c`), `COM_ParseError(const char*, ...)`,
  `COM_ParseWarning(const char*, ...)`, `COM_MatchToken(char**, const char*)`,
  `CL_ConsolePrint(const char*)` (`code/client/cl_console.c:370`; drop the `const_cast` in
  `code/sys/sys_api.cpp:15`), `SaveJPG(const char*, ...)`, and the `*Debug(int, const char *label,
  const char *file, int)` allocator variants in `qcommon.h:788-790`, `q_shared.h:454`, and
  `tr_public.h:119`. Change struct fields to `const char *`: `gitem_t` (6 string fields,
  `bg_public.h:627-640`), the three `cvarTable_t` (`cvarName`, `defaultString`), `netField_t.name`
  (`msg.c:778`), `infoParm_t.name` (`tr_shader.c:1313`), `menubitmap_s.focuspic` and `errorpic`,
  `menutext_s.string` (`ui_local.h:240-252`), `gentity_t.classname`, `model`, `model2`, `message`,
  `target` (`g_local.h:82-122`). `G_SpawnString(const char*, const char*, char **)` keeps its
  `char**` output because the level string is writable. Find remaining assignments with
  `grep -rnE '\.(name|string|focuspic|errorpic|cvarName)\s*=\s*"' code/q3_ui code/cgame code/game`
  and `grep -rnE '^\s*char\s*\*\s*\w+\s*;' code/*/*.h`.
  **Tests:** none, because the change is type-only; `-Werror=write-strings` in PR 1 onward is
  the enforcement.
  **Verify:** all-C build still green on GCC and clang; the MSVC CI leg compiles.

- [x] **P0.5 `qboolean` as `int` (decision C4).** Done on 2 September 2026. In `code/game/q_shared.h:335` replace the enum
  typedef with:

  ```c
  typedef int qboolean;
  enum { qfalse = 0, qtrue = 1 };
  ```

  **Tests:** `tests/test_strings.cpp` (quake3_tests) add case `QShared.QbooleanIsFourBytes` with
  `static_assert(sizeof(qboolean) == 4)` and a runtime check that `qtrue == 1`.
  **Verify:** build green; `nm` symbol lists unchanged.

- [x] **P0.6 `Q_EXPORT extern "C"` on module entry points.** Done on 2 September 2026. Coordinate with checklist 02 B1.
  Mark `vmMain` and `dllEntry` in `code/game/g_main.c:203`, `code/game/g_syscalls.c:34`,
  `code/cgame/cg_main.c:46`, `code/cgame/cg_syscalls.c:34`, `code/q3_ui/ui_main.c:43`, and
  `code/ui/ui_syscalls.c:33` with `extern "C"` (under `#ifdef __cplusplus`) and `Q_EXPORT`.
  Without C linkage the names mangle after the rename, `dlsym("vmMain")` fails at run time, and
  the compiler reports nothing. This is the most likely "compiles but nothing loads" trap.
  **Tests:** `tests/test_module_symbols.cpp` (quake3_tests, shared with checklist 01): for each
  module in `build/baseq3/`, `SDL_LoadObject` then `SDL_LoadFunction("vmMain")` and
  `SDL_LoadFunction("dllEntry")` are non-null; add case `ModuleSymbols.NamesAreUnmangled` that
  runs `nm -D` (or reads the export table) and asserts the exact strings `vmMain` and `dllEntry`
  appear.
  **Verify:** `ctest -R ModuleSymbols` passes.

- [ ] **P0.7 Golden screenshots.** With the tree still all C, run the checklist 00 smoke
  harness and store the result as the golden for this checklist:

  ```sh
  make smoke              # writes build/smoke/*.tga
  cp build/smoke/*.tga ci/smoke/golden/
  ```

  Determinism prerequisites are fixed in `ci/smoke/smoke.cfg`: `r_mode -1`, `r_customwidth 640`,
  `r_customheight 480`, `r_fullscreen 0`, `r_picmip 1`, `r_texturebits 32`,
  `r_ext_compressed_textures 0`, `s_initsound 0`, `com_maxfps 0`, `vm_game 0`, `vm_cgame 0`,
  `vm_ui 0`, and the pinned Docker image digest.
  **Tests:** none, because this creates the oracle.
  **Verify:** `compare -metric AE ci/smoke/golden/smoke.tga build/smoke/smoke.tga /dev/null`
  prints `0` on a second run.

  Landed on 3 September 2026 in addition to the original step: `vmMain` and `dllEntry` are now
  declared inside the `extern "C"` blocks of `code/game/g_public.h`, `code/cgame/cg_public.h`,
  and `code/ui/ui_public.h`. `Q_EXPORT` only sets symbol visibility, so without those
  declarations the definitions would mangle the moment `code/game`, `code/cgame`, or
  `code/q3_ui` become C++ in P1.6 to P1.8. `Sys_LoadDll` resolves both names as strings, so the
  engine would have fallen back to the bytecode virtual machine with no diagnostic. Verified by
  compiling a translation unit that includes `g_public.h` as C++ and confirming with `nm` that
  both symbols stay unmangled, and by `ModuleSymbols.EveryModuleExportsUnmangledEntryPoints`.

### C-P1 compile as C++17 (about 5 weeks single, 3 weeks with two people)

Every PR has two commits. Commit A renames files and updates the paths in `CMakeLists.txt` and
`tests/CMakeLists.txt`, nothing else. Commit B holds the code changes. Never squash A into B.
Branch names `cxx/<dir>`. Open and merge each PR within a day or two of branching, because
renames conflict badly with concurrent edits. Gate G1 means: the smoke screenshot compares with
zero differing pixels against `ci/smoke/golden/`.

Shared verify block for PRs 1 to 8 and 10:

```sh
make configure -DCMAKE_CXX_COMPILER=clang++ && make build   # clang
make configure -DCMAKE_CXX_COMPILER=g++ && make build       # gcc
make test                                                             # ctest
make smoke && compare -metric AE ci/smoke/golden/smoke.tga build/smoke/smoke.tga /dev/null   # G1: prints 0
timeout 60 build/q3ded +set dedicated 1 +set sv_pure 0 +set bot_enable 1 +set bot_minplayers 4 +set g_gametype 0 +map q3dm7 > /tmp/bot.log; echo $?   # 124
grep -c 'Loading bot' /tmp/bot.log       # >= 4
grep -cE 'ERROR|Sys_Error' /tmp/bot.log  # 0
# repeat the bot match with +set vm_game 1 to prove the interpreter still loads id qagame.qvm
make asan   # -fsanitize=address,undefined -fno-sanitize=alignment,vptr ; ASAN_OPTIONS=detect_leaks=0
nm -C --defined-only build/<lib>.a | awk '{print $3}' | sort > /tmp/after.txt   # diff against the previous PR: only expected additions
```

- [x] **P1.1 qcommon, shared game files, null stubs.** Done on 3 September 2026. `git mv` the 17 files: all
  `code/qcommon/*.c` except `unzip.c` and `md4.c`, `code/game/q_shared.c`, `code/game/q_math.c`,
  `code/null/null_client.c`, `null_input.c`, `null_snddma.c`. Remove
  `#include "../client/client.h"` from `unzip.c` (it needs only `q_shared.h` and `qcommon.h`).
  Expected error classes: catalogue rows 1 (67 casts; introduce `Z_New<T>`), 5, 7, 8
  (`vm_interpreted.cpp` gotos), 9, 20 (`q_math.c` assembly). Delete `code/null/null_main.c`,
  `null_glimp.c`, `null_net.c`, `mac_net.c` if checklist 01 has not already.
  **Tests:** none, because renames; gate G1 pixel identity plus ctest and the q3ded bot match are
  the tests. Existing `test_math.cpp`, `test_strings.cpp`, `test_msg.cpp`, `test_huffman.cpp`,
  `test_md4.cpp`, `test_cvar_cmd.cpp` must still pass unchanged.
  **Verify:** the shared verify block.

  Notes from the landing:
  - `code/qcommon/vm_ppc.c`, `vm_ppc_new.c`, and `vm_x86.c` stay as `.c`. No build target
    references them, so renaming them would add churn with no benefit. Revisit if a
    just-in-time compiler is ever revived.
  - The rename first broke the link with ten undefined symbols, all from catalogue row 23
    (file-local forward declarations). Read that row before starting P1.2 or P1.4, because the
    same declarations point the other way once the client and the server become C++.
  - Gate G1 could not run: the machine that landed this has no game data. Run
    `make smoke-update-golden` then `make smoke` on the Linux machine before P1.2, so that the
    remaining renames have a pixel baseline. Unit tests and the link were the checks used here.
  - The tree was GCC-clean but not Clang-clean, so the continuous integration macOS leg still
    failed after the link fix. Catalogue row 24 records both families and the fix for each.
    Twenty-four further sites were corrected on 3 September 2026. Configure with
    `-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++` and build with `-- -k 0` to see
    them all at once; one error per build is otherwise all Ninja reports.

- [ ] **P1.2 server.** `git mv code/server/*.c` (10 files). Expected classes: rows 1 (10 casts),
  2 (119 `VMA` uses in `sv_game.cpp`), 5. Introduce the `VmArg` proxy in `qcommon.h` inside
  `#ifdef __cplusplus`:

  ```cpp
  struct VmArg {
      intptr_t v;
      template <class T> operator T*() const { return static_cast<T*>(VM_ArgPtr(v)); }
      explicit operator bool() const { return v != 0; }
  };
  ```

  Redefine `VMA(x)` in `sv_game.cpp` as `VmArg{args[x]}`. Keep explicit casts only where `VMA` is
  used in arithmetic.
  **Tests:** none, because renames; gate G1 and the q3ded bot match are the tests.
  **Verify:** the shared verify block, both bot-match variants.

- [ ] **P1.3 renderer.** `git mv code/renderer/*.c` (23 files). `vk_backend.cpp` is already C++.
  Expected classes: rows 1 (58 casts), 5 (`cullType_t`, `genFunc_t`, `deform_t`, `alphaGen_t`,
  `colorGen_t`, `texCoordGen_t`), 7, 9. `or` is already renamed. This PR unblocks checklist 08
  R2 and checklist 05 T3.
  **Tests:** none, because renames; gate G1 is the test.
  **Verify:** the shared verify block. Additionally `apitrace trace` the smoke run and diff the
  GL call counts against a trace from the previous PR; they must match.

- [ ] **P1.4 client.** `git mv code/client/*.c` (15 files, including `snd_adpcm.c`). Expected
  classes: rows 1 (5 casts), 2 (70 in `cl_cgame.cpp`, 58 in `cl_ui.cpp`; `cl_ui.c:1047`
  `return (intptr_t)strncpy(...)` is correct after checklist 02 B1), 5 (`connstate_t`,
  `netsrc_t`), 20 (`snd_mix.c` assembly). This PR unblocks checklist 05 T1.
  **Tests:** none, because renames; gate G1 is the test.
  **Verify:** the shared verify block.

- [ ] **P1.5 botlib.** `git mv code/botlib/*.c` (28 files). Expected classes: rows 1 (17 casts),
  5, 6 (`be_ai_weap.c` tables). `operator` is already renamed. Botlib is compiled-as-C++ legacy
  and never idiomatic. Add `-Wno-*` allowances to this target only if a class has no cheap fix.
  **Tests:** none, because renames; the bot match is the test.
  **Verify:** the shared verify block, both bot-match variants.

- [ ] **P1.6 game (qagame).** `git mv code/game/*.c` except `bg_lib.c` (35 files). Expected
  classes: rows 1 (4 casts), 5 (`weapon_t`, `team_t`, `entity_event_t`, `moverState_t`,
  `trType_t`, `pmtype_t`), 6 (`gitem_t` rows already const). `vmMain`/`dllEntry` already carry
  `extern "C" Q_EXPORT`.
  **Tests:** `tests/test_module_symbols.cpp` must still find both symbols in `qagame<arch>`.
  **Verify:** the shared verify block, both bot-match variants (`vm_game 1` proves the id QVM
  still loads).

- [ ] **P1.7 cgame.** `git mv code/cgame/*.c` (21 files). Expected classes: rows 5
  (`footstep_t`, `impactSound_t`, `leType_t`, `weapon_t`), 6.
  **Tests:** `tests/test_module_symbols.cpp` for `cgame<arch>`.
  **Verify:** the shared verify block.

- [ ] **P1.8 q3_ui and `code/ui/ui_syscalls.c`.** `git mv code/q3_ui/*.c code/ui/ui_syscalls.c`
  (46 files). Expected classes: rows 5, 6 (menu structs already const). `delete` is already
  renamed.
  **Tests:** `tests/test_module_symbols.cpp` for `ui<arch>`.
  **Verify:** the shared verify block.

- [ ] **P1.9 JPEG swap (decision C1).** Add `code/third_party/stb/stb_image.h`,
  `stb_image_write.h`, and a `VERSION` file with the pinned upstream commit. Add one translation
  unit `code/renderer/tr_image_stb.cpp` that defines `STB_IMAGE_IMPLEMENTATION`,
  `STB_IMAGE_WRITE_IMPLEMENTATION`, `STBI_ONLY_JPEG`, `STBI_ONLY_PNG`, `STBI_NO_STDIO`,
  `STBI_MALLOC(sz) ri.Malloc(sz)`, `STBI_FREE(p) ri.Free(p)`, and an `STBI_REALLOC_SIZED` shim
  (allocate, copy, free; `Z_Malloc` has no realloc). Replace the `LoadJPG` body with
  `stbi_load_from_memory(fbuffer, len, &w, &h, &comp, 4)`, which already returns RGBA. Callers
  free with `ri.Free`, which matches `STBI_FREE`. Add `.png` to `R_LoadImage`. Keep id's own
  TGA, BMP, and PCX loaders. Replace `SaveJPG` with `stbi_write_jpg_to_func(cb, ctx, w, h, 3,
  buffer, 95)` accumulating into a `ri.Hunk_AllocateTempMemory` buffer, then `ri.FS_WriteFile`.
  Keep Q3's own vertical flip in `RB_TakeScreenshotCmd`; do not call
  `stbi_flip_vertically_on_write`. Delete `code/jpeg-6/` and the `q3jpeg` target, and drop the
  `-I code/jpeg-6` include. In `THIRD_PARTY_LICENSES.md` add the stb dual licence (MIT or public
  domain) and remove the IJG notice once jpeg-6 is gone. `libs/jpeg6` (Radiant) stays.
  **Tests:** `tests/test_jpeg_parity.cpp` (quake3_tests). During this PR only, keep jpeg-6 in the
  test target. For every `.jpg` in the mounted pak0 (`Q3_PAKS`), decode with both decoders and
  assert width, height, and max per-channel delta at or below 2. Case `JpegParity.EncodeRoundTrip`
  encodes a 64x64 gradient and decodes it with PSNR at or above 40 dB. The pak-dependent case is
  `GTEST_SKIP`ped when `Q3_PAKS` is unset.
  **Verify:** `compare -metric PSNR ci/smoke/golden/smoke.tga build/smoke/smoke.tga /dev/null`
  prints at least 45; `screenshotJPEG` in a smoke run writes a file `identify` reports as
  baseline JPEG; all three CI legs green.

- [ ] **P1.10 Header close-out.** Run after checklist 01 has deleted `code/unix` and no C
  consumer of the engine headers remains. Remove the `#ifdef __cplusplus extern "C"` blocks from
  every header **except** `unzip.h`, the MD4 prototype in `qcommon.h`, the `vmMain`/`dllEntry`
  declarations in `g_public.h`, `cg_public.h`, `ui_public.h` (`extern "C" Q_EXPORT intptr_t
  vmMain(...)`), and `GetRefAPI`. Keep the guards in `sys_api.h` only if a C consumer still exists.
  Replace `ID_INLINE` with `inline`. Delete `code/splines/` (unbuilt, carries its own `q_shared.h`
  and `min`/`max` macros). Removing C linkage is what allows overloads, templates, and namespaces
  in phase 2.
  **Tests:** none, because link errors here are loud and immediate; ctest and the module-symbol
  test are the checks.
  **Verify:** the shared verify block on all three CI legs; `grep -rln '\.c$' code/qcommon
  code/client code/server code/renderer code/botlib code/game code/cgame code/q3_ui` lists only
  `unzip.c` and `md4.c`.

### C-P2 idiomatic rewrites behind the C APIs (about 14 to 17 weeks, interleavable)

Every PR keeps the C API and the struct layouts (`cvar_t`, `msg_t`, `netchan_t`, `client_t`
offsets used by botlib and the modules through `FOFS`), passes the shared verify block, and is
its own reviewable unit. Write the behaviour tests against the C implementation first, then
refactor under them.

- [ ] **P2.0 `Com_Error` as a C++ exception (1 week, first).** In `code/qcommon/common.cpp`,
  `Com_Error` for `ERR_DROP`, `ERR_SERVERDISCONNECT`, `ERR_DISCONNECT`, and `ERR_NEED_CD` throws
  `q3::ErrorDrop{code, message}`. `Com_Frame` and `Com_Init` catch it where the `setjmp` calls
  were (`common.c:2359`, `:2656`). `ERR_FATAL` still calls `Sys_Error`. `VM_Call` wraps the
  native call, catches `ErrorDrop`, stores it, returns normally, and rethrows after the module
  frame is gone, so no exception crosses a shared-object boundary. Mark `Com_Error`
  `[[noreturn]]`. Botlib reaches `Com_Error` through `botimport.Error`; that unwinds through
  C-shaped frames, which is fine once every frame is compiled as C++.
  **Tests:** `tests/test_error_unwind.cpp` (quake3_tests): case `ErrorUnwind.DropCaughtAtVmCall`
  loads `tests/vm_testmodule` (from checklist 03) whose command 2 triggers a syscall that calls
  `Com_Error(ERR_DROP)`, and asserts `VM_Call` rethrows on the engine side and the VM stays
  usable; case `ErrorUnwind.HunkScopeRestoredOnThrow` opens a `HunkScope`, allocates, throws, and
  asserts `Hunk_MemoryRemaining` is restored.
  **Verify:** `map nonexistent` and the `error` command in a smoke run return to the menu; the
  shared verify block.

- [ ] **P2.1 cvar, cmd, files, memory (about 6 weeks).**
  - `q3::CvarSystem` absorbs `code/sys/cvar/cvar_manager.hpp` (do not keep two). Storage is
    `std::deque<cvar_t>` for stable addresses (modules hold `cvar_t*` and `vmCvar_t.handle`
    indexes into `cvar_indexes[MAX_CVARS]`; keep that array). Lookup is a case-insensitive
    `std::unordered_map<std::string_view, cvar_t*>`. Keep the `cvar_t` layout byte-identical and
    keep `next`/`hashNext` populated for `Cvar_InfoString` and `Cvar_WriteVariables`.
    `Cvar_Get`, `Cvar_Set`, and the rest stay as thin `extern` wrappers.
  - `q3::CommandSystem`: `std::unordered_map<std::string, xcommand_t>`, a tokenizer that fills the
    existing `cmd_argv` buffers so `Cmd_Argv(i)` returns stable `char*`, and `Cbuf` as a string
    ring with the same `MAX_CMD_BUFFER` overflow behaviour.
  - `q3::FileSystem`: `std::vector<SearchPath>` (a variant of `Directory{std::filesystem::path}`
    and `Pak{...}`), a pak index `unordered_map<string_view, FileInPack*>` replacing the per-pak
    hash table, `std::filesystem` for OS paths with Q3's lowercase forward-slash `qpath`
    normalisation kept, and pk3 reading through the retained `unzip.c` behind a `PakReader`
    interface. `fs_checksumFeed` and `FS_LoadedPakChecksums` are wire-visible; test them bit-exact
    against values recorded from pak0 to pak8.
  - Memory keeps `Z_Malloc` and `Hunk_*` semantics (hunk marks are load-bearing for level and
    module lifetimes). Add `q3::ZUnique<T>` (`unique_ptr` with a `Z_Free` deleter), `q3::HunkScope`
    (RAII `Hunk_SetMark`/`Hunk_ClearToMark`), and `q3::TempHunkBuffer`. Replace the 69 punning
    sites in `common.cpp` with `q3::bit_cast`, then remove `-fno-strict-aliasing` from qcommon
    only.
  **Tests:** `tests/test_cvar.cpp` (latch, ROM, cheat, archive semantics; `Cvar_InfoString`
  ordering and length caps; `modificationCount`; `vmCvar_t` handle update; the 1024-cvar limit;
  case-insensitive lookup; callback dispatch; fold `test_modern_cvar.cpp` in).
  `tests/test_cmd.cpp` (quotes, `//` comments, `;` splitting; `Cbuf_ExecuteText` EXEC_NOW,
  INSERT, APPEND ordering; `wait`; argument buffer stability across nested `Cmd_ExecuteString`).
  `tests/test_files.cpp` extensions (search-path order with `fs_game` overrides;
  `FS_FOpenFileRead` precedence pk3 versus directory; pure checksum strings against recorded
  values; path sanitisation `..`, absolute, `.so`; `FS_ListFiles` sort order; a pk3 fixture
  generated by `tests/zip_writer.hpp`). `tests/test_memory.cpp` (hunk mark and clear, temp memory
  LIFO, zone tags, `ZUnique` and `HunkScope` exception safety).
  **Verify:** the shared verify block; connect to a `sv_pure 1` server in the container without
  an "Unpure client" kick.

- [ ] **P2.2 renderer.** Written in C++ by checklist 08 R2 from day one: `Image`, `Shader` and
  `ShaderStage`, `Model` (variant over MD3, MD4, BSP), `BackEnd` behind the `rb_backend_t`
  vtable struct (not a virtual class, so GL and Vulkan translation units can fill it and a null
  backend can test it), `std::vector<drawSurf_t>` sorted with `std::sort` on the existing key.
  Replace `FloatAsInt`-style tricks in `tr_shade_calc.cpp` with `bit_cast`.
  **Tests:** `tests/test_shader_parser.cpp`: parse every `scripts/*.shader` in pak0 and compare
  the resulting `shader_t` fields with a golden JSON dump produced once by the compiled-as-C++
  legacy parser; a null-backend test records the `rb_backend_t` call sequence for a fixed
  `drawSurf_t` list. Skip pak-dependent cases without `Q3_PAKS`.
  **Verify:** owned by checklist 08.

- [ ] **P2.3 client (3 to 4 weeks).** `Netchan` class over `netchan_t` (fragment reassembly
  state machine), snapshot ring as `std::array<clSnapshot_t, PACKET_BACKUP>`, the download state
  machine as `enum class` plus struct (checklist 06 N1 shapes it first), info-string parsing over
  `std::string_view` with the C `Info_ValueForKey` copies kept, `enum class ConnState : int` with
  `static_cast` shims where `clc.state` compares to old enumerators.
  **Tests:** `tests/test_netchan.cpp` (fragment reassembly round trip with packet loss and
  reorder), `tests/test_snapshot.cpp` (delta encode and decode against recorded `.dm_68` frames
  in `tests/fixtures/`), download state machine transitions in `tests/test_download_policy.cpp`
  (checklist 06), info-string edge cases in `tests/test_strings.cpp`.
  **Verify:** the shared verify block plus a LAN connect in the container.

- [ ] **P2.4 server (2 to 3 weeks).** `client_t` gains member functions for the snapshot and
  entity-visibility path, `sv_snapshot.cpp` uses `std::array`, `sv_client.cpp` download and
  challenge as state machines, `netsrc_t` becomes `enum class NetSrc : int` with unchanged wire
  values.
  **Tests:** `tests/test_snapshot.cpp` server side (entity visibility for a recorded world),
  `tests/test_netchan.cpp` server role.
  **Verify:** the shared verify block, both bot-match variants.

- [ ] **P2.5 shared.** `q3::math::Vec3` interop with `vec3_t` (`static_assert(sizeof(Vec3) ==
  sizeof(vec3_t))`, `as_vec3` reinterpret helpers), migrate `q_math.cpp` internals while keeping
  the C signatures. `Com_sprintf` and `va` stay `snprintf`-based (`va()` keeps its rotating static
  buffers). Add `q3::format(...)` returning `std::string` for new code only.
  **Tests:** extend `tests/test_math.cpp` and `tests/test_modern_math.cpp` with interop cases
  (`VectorNormalize` through both types gives identical results on 1000 random vectors).
  **Verify:** the shared verify block.

### Not converted

- Botlib internals: compiled as C++, no idiomatic changes, no `-Wold-style-cast`.
- `msg.c`, `huffman.c`, `net_chan.c` wire format: wrap only (`q3::Msg` view over `msg_t`),
  bit-exact tests against recorded packets.
- `cm_*` collision internals: wrap (`q3::CollisionModel` over `CM_*`), no algorithm edits, keep
  `-fno-strict-aliasing`.
- `vm_interpreted.cpp`: byte-for-byte; it must run id's QVMs.
- `unzip.c`, `md4.c`: stay C.
- Layout invariant: never add constructors or virtual functions to `entityState_t`,
  `playerState_t`, `gentity_t`, `iteminfo_t`, `weaponinfo_t`, `projectileinfo_t`, `md4Frame_t`,
  or `srfSurfaceFace_t`.

### Coding standard for new C++

Namespace `q3::` with sub-namespaces per subsystem (`q3::fs`, `q3::cvar`, `q3::net`,
`q3::render`). `snake_case` functions and variables, `PascalCase` types, as in `code/sys`.
`enum class` for every new enumeration. No raw `new` or `delete`; use `std::unique_ptr` or
`ZUnique`. No exception crosses the `extern "C"` or vtable boundary; wrap with
`Q3_NOEXCEPT_BOUNDARY`:

```cpp
#define Q3_NOEXCEPT_BOUNDARY(body) \
    try { body } catch (const std::exception& e) { Com_Error(ERR_DROP, "%s: %s", __func__, e.what()); } \
    catch (...) { Com_Error(ERR_DROP, "%s: unhandled C++ exception", __func__); }
```

No `std::span` in C++17; use `q3::Span<T>` from `code/sys/util/span.hpp` and `std::string_view`
for text. `q3::bit_cast<T>` for punning. `[[nodiscard]]` on factories. `constexpr` tables where
possible. Apply `.clang-format` (checklist 10) and `clang-tidy modernize-*,bugprone-*` only to
converted files, never to compiled-as-C++ legacy, so `git blame` stays useful.

## Test map

| Test file | Binary | Cases | Added by |
|---|---|---|---|
| `tests/test_module_symbols.cpp` | quake3_tests | `vmMain` and `dllEntry` load for every module; names unmangled | P0.6 (shared with checklist 01) |
| `tests/test_strings.cpp` | quake3_tests | `QShared.QbooleanIsFourBytes` | P0.5 |
| `ci/smoke/` golden and gate G1 | script | pixel identity per rename PR | P0.7, P1.1 to P1.8, P1.10 |
| `tests/test_jpeg_parity.cpp` | quake3_tests | per-image decoder delta at or below 2, encode round trip PSNR at or above 40 dB | P1.9 |
| `tests/test_error_unwind.cpp` | quake3_tests | `DropCaughtAtVmCall`, `HunkScopeRestoredOnThrow` | P2.0 |
| `tests/test_cvar.cpp` | quake3_tests | latch, ROM, cheat, archive, info string, handles, limit, case, callbacks | P2.1 |
| `tests/test_cmd.cpp` | quake3_tests | tokenizer, `Cbuf` ordering, `wait`, argv stability | P2.1 |
| `tests/test_files.cpp` | quake3_tests | precedence, pure checksums, sanitisation, list order | P2.1 (extends checklist 03) |
| `tests/test_memory.cpp` | quake3_tests | hunk marks, temp LIFO, zone tags, RAII exception safety | P2.1 |
| `tests/test_shader_parser.cpp` | quake3_tests | golden JSON parity, null-backend call sequence | P2.2 (with checklist 08) |
| `tests/test_netchan.cpp` | quake3_tests | loss and reorder, fragment reassembly | P2.3, P2.4 (extends checklist 03) |
| `tests/test_snapshot.cpp` | quake3_tests | delta against recorded `.dm_68` frames, server visibility | P2.3, P2.4 |
| `tests/test_math.cpp`, `tests/test_modern_math.cpp` | quake3_tests, q3sys_tests | `Vec3` and `vec3_t` interop | P2.5 |

## Out of scope

- Converting botlib to idiomatic C++.
- Compiling the game modules to QVM bytecode with lcc.
- `code/ui` (Team Arena user interface): not built, not converted.
- Frame-level parallelism (checklist 05 follow-ons).

## Follow-ons

- Replace `unzip.c` with `miniz` behind the `PakReader` interface.
- Remove `-fno-strict-aliasing` from the renderer after checklist 08 R2 completes.
- A `Doxyfile` for the `q3::` namespaces.

## Done criteria

- `grep -rln '\.c$'` under the built directories lists only `code/qcommon/unzip.c` and
  `code/qcommon/md4.c`.
- All three CI legs (Linux GCC and clang in Docker, macOS arm64, Windows MSVC) are green with
  `-Werror` on every converted target.
- The smoke screenshot is pixel-identical to `ci/smoke/golden/` for every PR except the JPEG
  swap, which passes the PSNR gate.
- The `q3ded` 60 s bot match passes with the native module and with `vm_game 1`.
- ASan and UBSan runs of the smoke and the bot match are clean.
- Every row of the test map exists and passes under `ctest --preset dev` and `ctest --preset asan`.
- `Com_Error(ERR_DROP)` is a C++ exception and `tests/test_error_unwind.cpp` passes.

## Last step

- [ ] Delete this file and remove its row from `docs/plans/README.md`.
