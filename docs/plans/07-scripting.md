# Checklist 07: Lua scripting

## Purpose

Turn the `ScriptEngine` into a real, sandboxed, server-side Lua hook system. Scripts load from
`scripts/*.lua` inside the game directory or a pk3, receive a documented set of engine events,
and call a small `q3` API. The legacy string DSL, the entity-property store, and every
documented function that does not exist are removed.

**Status:** Not started

## Prerequisites

- Checklist `00-environment.md` is complete. Build and test in the container.
- Checklist `01-build-portability.md` has made LuaJIT `REQUIRED` (with the `luajit-cmake`
  fallback). `sol.hpp` includes `lua.h` unconditionally (`code/sys/scripting/sol/sol.hpp:2955-2961`),
  and today `CMakeLists.txt:35-38` never checks the result of `pkg_check_modules`.
- Checklist `02-stability.md` step B5 (`sys_api.cpp` hardening with `Q3_NOEXCEPT_BOUNDARY` and
  `Sys_SubsystemShutdown`) is complete.
- Checklist `04-cxx-migration.md` step C-P0 is complete so that `g_public.h`, `g_local.h`, and
  `server.h` carry `extern "C"` guards.

## Owner decisions this file relies on

| # | Decision | Default the plan proceeds on |
|---|---|---|
| S1 | Where the Lua state lives | In the engine (`q3sys`), with server semantics. `q3ded` gets scripting, and scripts ship in pk3s. |
| S2 | Legacy DSL and entity properties | Remove both. |
| S3 | `eval` | Keep, implemented as `return <expr>` through Lua. |
| S4 | `sv_scriptEnable` default | `1`. The owner might prefer `0`. Change only the default. |
| S5 | Client-side scripting | Not now. Server only. |

## Background

Anchors from the audit of 1 September 2026. Re-verify before you edit.

- `lua_enabled_` is hardcoded `true` (`code/sys/scripting/script_engine.cpp:11`) and never read.
  The header implies a fallback (`script_engine.hpp:75,82`) that does not exist.
- `open_libraries` includes `sol::lib::package` and other libraries (`script_engine.cpp:15`),
  which gives every script `load`, `loadstring`, `dofile`, `loadfile`, `require`, and
  `package.cpath`. That is arbitrary file and code access.
- The constructor overwrites Lua `print` with a lambda that builds a string and returns it, and
  the wrapper discards every return value (`script_engine.cpp:22-38` and `:234-243`). The
  documented `print` example outputs nothing.
- `execute()` runs the whole text through `lua_.script()` first, swallows the error with
  `catch (...)`, then runs a legacy token DSL (`script_engine.cpp:107-118`). A partially valid
  Lua script executes twice, and every diagnostic is lost.
- `catch (...)` is used as flow control to test whether a token is a number
  (`script_engine.cpp:144-151`, `:159-166`, `:174-181`, `:195-203`).
- `eval()` never calls Lua (`script_engine.cpp:189-204`). `eval("1+2")` returns `"1+2"`.
- `register_function` computes a return value and returns `sol::nil` (`script_engine.cpp:229-243`).
  A C++ exception in a registered function unwinds through LuaJIT C frames.
- `dispatch_event` iterates a C++ handler map that nothing in `code/` subscribes to
  (`script_engine.cpp:250-257`). The only engine dispatch is `Sys_ScriptEvent("game_init", server)`
  at `code/server/sv_init.c:362`. `player_spawn`, `map_change`, and `on_player_frag` are never
  dispatched.
- `update_timers` is documented and implemented as a delta (`script_engine.cpp:263-264`) but the
  header names the parameter `current_time_seconds` (`script_engine.hpp:46`).
- `docs/scripting.md:95-108` documents `set_gravity`, `enable_quad_damage`, `on_game_init`,
  `on_player_frag`, and a `scripts/match_rules.lua` file. None exist. Nothing loads `*.lua` from
  disk.

Facts that shape the design:

- Game modules load as native shared objects first (`code/qcommon/vm.c:67-70,482-491`) and
  fall back to the id QVM in `pak0.pk3`. A new syscall in `g_public.h` works with the
  CMake-built module and is inert with the id QVM.
- The engine already sees map load, client connect, client begin, client disconnect, and game
  shutdown at the `VM_Call(gvm, ...)` sites (`code/server/sv_client.c:423,509,634`,
  `sv_init.c:362`, `code/server/sv_game.c:877,905`).
- `Sys_SubsystemFrame` runs in `Com_Frame` (`code/qcommon/common.c:2685`) for client and
  dedicated server. Timers fire there on the main thread.

## Windows blocker, found and fixed on 3 September 2026

The three `ModernScriptingTest` cases failed on the `windows-x64` leg with:

```
unknown file: error: SEH exception with code 0xe24c4a02 thrown in the test body.
```

They were first assumed to be failing because they exercise the legacy token syntax that step
S1.1 deletes. They were not. The cause was `script_engine.cpp` calling `lua_.script()`, the
**unprotected** form, which raises a Lua error on input that is not valid Lua. LuaJIT unwinds
that error with a structured exception, and the Microsoft Visual C++ build uses `/EHsc`, under
which `catch (...)` does not catch one, so the error escaped the engine and took the test process
with it. On Linux and macOS the same error was caught, which is why it only ever showed on
Windows.

The call is now `lua_.safe_script(code, sol::script_pass_on_error)`, which routes through
`lua_pcall` and returns the failure rather than raising it, so no unwinding crosses the LuaJIT
frames on any platform. The error text is logged at `DEBUG` rather than discarded, because
invalid Lua is the expected path for the legacy syntax until S1.1 removes it.

Two notes for S1.1:

- **The other half of the bridge was already in place.** `sol::state`'s constructor reaches
  `sol::set_default_state`, which calls `stack::luajit_exception_handler`
  (`code/sys/scripting/sol/sol.hpp:27140`), so a C++ exception thrown by a registered function
  is converted to a Lua error instead of unwinding through LuaJIT. Nothing extra is needed, and
  `SOL_EXCEPTIONS_SAFE_PROPAGATION` should stay undefined: it is for a Lua built as C++, not for
  LuaJIT.
- **Keep `/EHsc`.** Switching to `/EHa` would make `catch (...)` swallow structured exceptions,
  including access violations, which hides defects rather than fixing them. The rule that keeps
  this working is the one S1.1 already states: every call into Lua goes through `safe_script` or
  `protected_function`. `execute()` was the only unprotected call; the remaining interactions are
  `open_libraries`, table assignment, and `set_function`, none of which raise.

## Steps## Steps

### Phase S1: scripting (about 5.5 days)

- [ ] **S1.0 Confirm the architecture.**
  The Lua state stays in `q3sys`, created in `Sys_SubsystemInit`, with server semantics. The
  engine dispatches the events it can see. The two events only the game module sees
  (`player_spawn`, `player_death`) arrive through one new syscall `G_SCRIPT_EVENT`, appended
  after `G_FS_SEEK` in `code/game/g_public.h:230`. The value stays below `BOTLIB_SETUP` (200),
  so no renumbering. If the id `qagame.qvm` is loaded, those two events do not fire and the rest
  still work.
  Rejected: embedding Lua inside `qagame`. It needs a second Lua state per module, links LuaJIT
  into the shared object, and does not work on a dedicated server that runs the QVM.
  **Tests:** none, because this is a design record.
  **Verify:** `docs/scripting.md` (checklist 10) states this architecture.

- [ ] **S1.1 Sandbox and rewrite the engine class.**
  Files: `code/sys/scripting/script_engine.hpp` and `script_engine.cpp` (rewrite),
  `CMakeLists.txt`.
  - Add `target_compile_definitions(q3sys PUBLIC SOL_ALL_SAFETIES_ON=1 SOL_LUAJIT=1 SOL_PRINT_ERRORS=0)`.
  - `open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table)` only.
    Then set `dofile`, `loadfile`, and `loadstring` to `nil` in `_G`. Wrap `load` so the mode
    argument is forced to `"t"`, because loading LuaJIT bytecode escapes the sandbox. Keep
    `collectgarbage`. No `package`, `io`, `os`, `debug`, `ffi`, or `jit` tables.
  - Replace `print` with a C++ function that joins arguments with tabs, appends `\n`, and calls
    a `print_sink` callback that `sys_api.cpp` sets to `Com_Printf`.
  - Instruction limit: `lua_sethook(L, hook, LUA_MASKCOUNT, 1000000)`; the hook raises
    `"script instruction limit exceeded"`. Configure the limit with the cvar
    `sv_scriptMaxInstructions` (default `"10000000"`). Note that a count hook disables the JIT
    for hooked code, which is acceptable for server hooks.
  - Every call uses `sol::protected_function` or `safe_script`. The error handler formats
    `"Lua error in <chunkname>: <message>\n<traceback>"` and passes it to a `report_error`
    callback (set to `Com_Printf(S_COLOR_RED "%s")` by `sys_api.cpp`). Remove every
    `catch (...)`. A C++ exception inside a registered function becomes a Lua error, which
    `SOL_ALL_SAFETIES_ON` provides.
  - Delete the legacy DSL: `tokenize_line`, the `set`/`let`/`emit` parsing, `functions_`,
    `variables_`, and the `add`, `multiply`, `sqrt`, `sin`, `cos` bindings.
    `bool execute(std::string_view code, std::string_view chunkname)` is Lua only and returns
    `false` on error after reporting it.
  - `eval(expr)` becomes `safe_script("return " + expr)` mapped to `ScriptValue`.
  - `set_variable` and `get_variable` map to `lua_[name]`.
  - `register_function` returns the C++ result to Lua with `sol::make_object`.
  - Rename the `update_timers` parameter to `delta_seconds` in `script_engine.hpp:46`.
  - Delete `set_entity_property` and `get_entity_property`.
  - `lua_enabled_` comes from the constructor argument (the value of `sv_scriptEnable`).
  - Event registry lives in Lua: `q3.on(event, fn)` appends to `q3._handlers[event]`.
    `dispatch_event(name, args)` calls each handler through a `protected_function`, continues
    after an error, and counts errors for `lua_status`. Keep `subscribe_event` in C++ for tests.
  - `reset()` destroys and recreates the state. `SV_SpawnServer` calls it so scripts start clean
    for every map, which matches `qagame` semantics.
  **Tests:** rewrite `tests/test_modern_scripting.cpp` (`q3sys_tests`). The existing cases
  (`:6-84`) rely on the DSL and entity properties and are removed. Install a capturing
  `print_sink` and `report_error` in a fixture. Cases: `Scripting.PrintReachesSink`,
  `Scripting.SyntaxErrorReturnsFalseWithChunkName` (message contains `console:1`),
  `Scripting.SandboxHidesIoOsRequireDofile` (each is `nil`),
  `Scripting.SandboxRefusesBytecode` (`load(string.dump(function() end))` errors),
  `Scripting.InstructionLimitStopsInfiniteLoop` (set a low limit; `while true do end` errors
  with the limit message), `Scripting.HandlersReceiveIntegerArgs`,
  `Scripting.ThrowingHandlerDoesNotStopNextHandler`, `Scripting.EvalReturnsNumber`
  (`eval("1+2")` is `3.0`), `Scripting.SetAndGetVariable`, `Scripting.TimerFiresAfterDelay`
  (`schedule(1.0)` fires after two `update_timers(0.5)` calls), `Scripting.ResetClearsGlobals`.
  **Verify:** `make test -R Scripting` passes.

- [ ] **S1.2 Register the `q3` API.**
  Files: `code/sys/sys_api.cpp` (registration in `Sys_SubsystemInit` after the engine is
  created), `code/sys/sys_api.h`.
  The `q3` table exposes:

  | Lua function | Engine function behind it | Note |
  |---|---|---|
  | `q3.print(...)` | `Com_Printf` | Same as global `print`. |
  | `q3.log(level, msg)` | `Com_Printf` or `Com_DPrintf` | `level` is `"info"`, `"warn"`, `"error"`, or `"debug"`. |
  | `q3.cvar_get(name) -> string` | `Cvar_VariableString` | |
  | `q3.cvar_set(name, value)` | `Cvar_Set` | `Cvar_Set2` enforces ROM and INIT flags. |
  | `q3.exec(text)` | `Cbuf_AddText` | Grants console power to scripts. Document it. |
  | `q3.server_command(clientnum, text)` | `SV_SendServerCommand` | `-1` means all clients. For example `'print "hello\n"'` or `'cp "text"'`. |
  | `q3.client_name(n)`, `q3.client_userinfo(n, key)` | `SV_GetUserinfo` and `Info_ValueForKey` | |
  | `q3.configstring(n)` | `SV_GetConfigstring` | |
  | `q3.max_clients()` | `sv_maxclients->integer` | |
  | `q3.entity_origin(n)`, `q3.entity_type(n)` | `SV_GentityNum(n)->s` | Read only. |
  | `q3.time()` | `svs.time` | |
  | `q3.schedule(delay_s, fn)` | timer queue | Fires from `Sys_SubsystemFrame` on the main thread. |
  | `q3.on(event, fn)` | Lua handler registry | |

  `set_gravity` is not needed: use `q3.cvar_set("g_gravity", 800)`. `enable_quad_damage`
  has no engine equivalent and is removed from the docs. `q3sys` can call server functions
  because `q3server` links into every binary.
  **Tests:** none for the `SV_*` bound functions, because they need a running server. Pure
  wrappers (`q3.cvar_get`, `q3.cvar_set`, `q3.time`) are covered by adding
  `Scripting.CvarGetSetRoundTrip` to `tests/test_modern_scripting.cpp` in `quake3_tests`
  linkage if the fixture can call `Cvar_Init`; otherwise keep them under the integration check.
  **Verify:** in a running server, `lua_exec q3.server_command(-1, 'print "hi\n"')` prints
  `hi` on every client.

- [ ] **S1.3 Dispatch the events and add the syscall.**
  Files: `code/server/sv_init.c`, `code/server/sv_game.c`, `code/server/sv_client.c`,
  `code/game/g_public.h`, `code/game/g_syscalls.c`, `code/game/g_local.h`,
  `code/game/g_client.c`, `code/game/g_combat.c`, `code/sys/sys_api.h` and `sys_api.cpp`.

  | Event | Dispatch site | Arguments |
  |---|---|---|
  | `map_change` | `SV_SpawnServer` in `sv_init.c`, replacing `Sys_ScriptEvent("game_init", server)` at `:362`, before scripts reload | `(old_mapname, new_mapname)` |
  | `game_init` | `sv_game.c` after `VM_Call(gvm, GAME_INIT, ...)` at `:905` | `(mapname, restart)` |
  | `client_connect` | `sv_client.c` after `GAME_CLIENT_CONNECT` at `:423` when not denied; also the bot path (`sv_ccmds.c:305`, `sv_init.c:471`) | `(clientnum, name, is_bot, first_time)` |
  | `client_begin` | `sv_client.c:634` after `GAME_CLIENT_BEGIN` | `(clientnum)` |
  | `player_spawn` | `g_client.c` `ClientSpawn` (`:1051`, at the end) through `trap_ScriptEvent` | `(clientnum)` |
  | `player_death` | `g_combat.c` `player_die` (`:439`, after the obituary) through `trap_ScriptEvent` | `(victim_clientnum, attacker_entnum, means_of_death, inflictor_entnum)`; `attacker` might be `ENTITYNUM_WORLD` |
  | `client_disconnect` | `sv_client.c:509` before `GAME_CLIENT_DISCONNECT` | `(clientnum, name)` |
  | `game_shutdown` | `sv_game.c:877` and `:921` before `GAME_SHUTDOWN` | `(restart)` |

  Syscall plumbing: add `G_SCRIPT_EVENT` after `G_FS_SEEK` in `g_public.h:230`; in
  `g_syscalls.c` add `void trap_ScriptEvent(const char *name, int a, int b, int c, int d) {
  syscall(G_SCRIPT_EVENT, name, a, b, c, d); }`; declare it in `g_local.h`; in
  `SV_GameSystemCalls` add `case G_SCRIPT_EVENT: Sys_ScriptEventInts(VMA(1), args[2], args[3],
  args[4], args[5]); return 0;`. In `sys_api.h` replace `Sys_ScriptEvent(name, arg)` with
  `Sys_ScriptEventV(const char *name, const char *sig, ...)` where `sig` characters are `s`
  (string), `i` (int), and `b` (bool). All dispatch sites use it. A QVM rebuilt from this source
  needs `g_syscalls.asm` regenerated; that is out of scope.
  **Tests:** `tests/test_modern_scripting.cpp` cases `Scripting.DispatchWithSignatureString`
  (`Sys_ScriptEventV("player_death", "iiii", 1, 2, 3, 4)` reaches a Lua handler with four
  integers) and `Scripting.UnknownEventIsNoOp`. The engine-side dispatch sites are covered by
  the integration check.
  **Verify:** `lua_exec q3.on("player_death", function(v, a) q3.print(v, a) end)` then a frag in
  a bot match prints two numbers on the server console.

- [ ] **S1.4 Load scripts from `scripts/*.lua`.**
  Files: `code/sys/sys_api.cpp` (new `Sys_ScriptLoadAll()`), `code/sys/sys_api.h`,
  `code/server/sv_init.c` (call after `FS_Restart` and pure setup in `SV_SpawnServer`, before
  `SV_InitGameProgs`).
  `FS_ListFiles("scripts", ".lua", &n)` searches every search path including pk3s. Sort names
  for determinism. For each, `FS_ReadFile(va("scripts/%s", name), &buf)`, then
  `execute(buf, "scripts/<name>")`, then `FS_FreeFile`. Print `Lua: loaded N scripts`. Errors
  print per file and do not stop the others. Gate with `sv_scriptEnable` (default `"1"`,
  `CVAR_ARCHIVE | CVAR_LATCH`).
  **Tests:** none as a unit test, because it depends on `FS_*` with a search path. Cover it in
  `tests/test_files.cpp` (checklist 03) with a case `Files.ListsLuaScriptsInsidePk3` that
  writes a pk3 with `scripts/a.lua` and `scripts/b.lua` through the ZIP writer fixture and
  asserts `FS_ListFiles` returns both in sorted order.
  **Verify:** put `scripts/example_frag_log.lua` in a pk3 in `baseq3`, start `q3ded +map q3dm1`,
  and the console prints `Lua: loaded 1 scripts`.

- [ ] **S1.5 Add the console commands.**
  Files: `code/server/sv_ccmds.c` (`SV_AddOperatorCommands`).
  `lua_exec <code>` runs `Cmd_Args()` through `Sys_ScriptExecute` with chunk name `console`.
  `lua_reload` calls `reset()` and `Sys_ScriptLoadAll`. `lua_status` prints whether the state
  is alive, the number of loaded scripts, handlers per event, the error count, and the
  instruction limit. Server side so `q3ded` has them.
  **Tests:** none, because they are console commands. `lua_status` output is checked manually.
  **Verify:** `lua_status` lists the loaded scripts and handler counts; `lua_exec io.open`
  reports `nil`.

- [ ] **S1.6 Ship an example script.**
  Files: new `scripts/example_frag_log.lua` in the repository (installed next to the binaries
  by CMake as documentation, not into a pk3).

  ```lua
  -- Logs every frag to the server console.
  q3.on("player_death", function(victim, attacker, mod, inflictor)
    local v = q3.client_name(victim)
    local a = attacker < q3.max_clients() and q3.client_name(attacker) or "the world"
    q3.print(string.format("%s was fragged by %s (mod %d)\n", v, a, mod))
  end)
  ```

  It uses only functions from S1.2.
  **Tests:** `tests/test_modern_scripting.cpp` case `Scripting.ExampleScriptLoadsWithoutError`
  reads the file from the source tree and executes it with a stub `q3` table where the
  engine-bound functions are replaced by lambdas.
  **Verify:** the script appears in `docs/scripting.md` and loads with `lua_status` showing one
  `player_death` handler.

## Test map

| Test file | Binary | Cases | Added by |
|---|---|---|---|
| `tests/test_modern_scripting.cpp` | `q3sys_tests` | PrintReachesSink, SyntaxErrorReturnsFalseWithChunkName, SandboxHidesIoOsRequireDofile, SandboxRefusesBytecode, InstructionLimitStopsInfiniteLoop, HandlersReceiveIntegerArgs, ThrowingHandlerDoesNotStopNextHandler, EvalReturnsNumber, SetAndGetVariable, TimerFiresAfterDelay, ResetClearsGlobals, DispatchWithSignatureString, UnknownEventIsNoOp, ExampleScriptLoadsWithoutError | S1.1, S1.3, S1.6 |
| `tests/test_files.cpp` | `quake3_tests` | ListsLuaScriptsInsidePk3 | S1.4 (file owned by checklist 03) |

Integration checks: `lua_exec q3.server_command(...)` in a running server, a frag in a bot match
reaching a `player_death` handler, and `lua_exec io.open` returning `nil`.

## Out of scope

- Client-side scripting (cgame or UI hooks).
- Entity mutation from Lua. Entity access is read only.
- `set_gravity` and `enable_quad_damage`. Use `q3.cvar_set`.
- Regenerating `g_syscalls.asm` for QVM builds.

## Done criteria

- `lua_status` lists loaded scripts and handler counts.
- A script inside a pk3 prints on `player_death` during a bot match.
- `lua_exec io.open` reports `nil`, and `while true do end` stops with the limit message.
- Every row of the test map exists and passes under `ctest --preset dev` and
  `ctest --preset asan` in the container.
- `docs/scripting.md` describes the sandbox, the event table with arguments, the `q3` API, the
  console commands, the example script, and an explicit "not available" list (checklist 10).

## Last step

- [ ] Delete this file and remove its row from `docs/plans/README.md`.
