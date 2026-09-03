# Logging

The engine logs through `q3::log::Logger` in `code/sys/logger/logger.hpp`. This page is the
policy: it says which level to use, where logging belongs, and where it must not go.

## Levels

| Macro | Use it for | Compiled in Release? |
|---|---|---|
| `LOG_ERROR` | A failure the user must know about, usually just before an error path. | Yes |
| `LOG_WARN` | A rejected input, a fallback taken, or a condition that will surprise someone later. | Yes |
| `LOG_INFO` | One line per lifecycle event: a subsystem started, a map loaded, a module chosen. | No |
| `LOG_DEBUG` | One line per operation: a file opened, a cvar created, a packet reassembled. | No |

`LOG_DEBUG` and `LOG_INFO` compile to nothing when `Q3_LOG_STRIP_VERBOSE` is defined, which
`CMakeLists.txt` does for `Release` builds only. In `Debug` and `RelWithDebInfo` they are
compiled and filtered at run time.

## Turning it on

`com_logLevel` selects the lowest level that is delivered: `0` debug, `1` info, `2` warn,
`3` error. It defaults to `1` in a debug build and `2` in an optimised one, and it is archived.
`developer 1` forces debug, so you do not have to remember both.

```
\com_logLevel 0
```

A filtered call costs one relaxed atomic load and a branch. Nothing is formatted, and no string
is built, until the level check passes.

## Where logging belongs

Log at decisions and lifecycle boundaries, which is where a developer reading a log wants to
know what the engine chose and why:

- what was found and what was picked: search paths, pak files, module type, resolved paths;
- inputs that were rejected, always with the value that was rejected;
- fallbacks: say what failed and what was used instead;
- state transitions: connect, spawn, restart, shutdown.

## Where it must not go

- **Per-frame and per-field code.** Nothing in `Com_Frame`, `SV_Frame`, `CL_Frame`, the renderer
  back end, `msg.cpp`, or `huffman.cpp`. A branch per call is cheap; thousands per frame are not,
  and a log line per field would swamp the console.
- **Per-trace collision code.** `cm_trace.cpp`, `cm_patch.cpp`, and `cm_polylib.cpp` run many
  times per frame. Error conditions only.
- **`INFO` for anything repeated.** The first version of this layer logged at `INFO` on every
  cvar change and every file read, which made the console unusable. If a line can appear more
  than once per lifecycle event, it is `DEBUG`.
- **Off the main thread, expecting immediate output.** The console sink is only called on the
  main thread; a line logged from a worker is queued and delivered by `flush_queued()` from
  `Sys_SubsystemFrame`. Logging from a worker is safe, but do not rely on ordering with
  main-thread lines.

## C and C++

The logger is a C++ header, so it is available in `code/qcommon`, `code/sys`, `code/null`, and
the two shared game files. The rest of the engine is still C and has no equivalent; it uses
`Com_Printf` and `Com_DPrintf`. Instrument each directory as checklist 04 converts it to C++,
rather than adding a second logging idiom that the migration would then have to unpick.
