# Threading model

This document describes the engine threading model, thread ownership rules, and synchronization mechanisms.

## Main thread ownership

The main thread owns the core engine state:

- Console variables (`cvar_t`) and command processing (`Cmd_*`, `Cbuf_*`).
- Console text buffer and display lines (`CL_ConsolePrint`).
- Zone memory (`Z_Malloc`, `Z_Free`) and hunk allocators (`Hunk_Alloc`, `Hunk_AllocateTempMemory`).
- File handles and search paths (`FS_*`).
- Virtual machines (`VM_*`) and module execution.
- Client state (`clientActive_t`) and server state (`serverStatic_t`).
- Renderer import callbacks (`refimport_t`).

Any direct access to these systems from background threads is forbidden. Non-release builds enforce this rule with `Q3_ASSERT_MAIN_THREAD()`.

## Legal operations off the main thread

Worker threads and background tasks may perform these operations:

- Pure computation on caller-owned memory buffers.
- String and vector math utilities in `q_shared.h` and `q3::math`.
- Checksum calculations (`Com_BlockChecksum`, MD4).
- Image compression and decompression with thread-local buffers.
- Reading archive contents on privately opened `unzFile` handles.
- Direct operating system file access (`fopen`, `fread`) on paths that the main thread supplies.
- Logging via `LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, and `LOG_ERROR`.
- Posting tasks to the main thread through `Sys_PostToMainThread` or `q3::threading::MainThreadQueue`.
- Standard C++ containers and objects owned exclusively by the worker thread.

## Forbidden operations off the main thread

Worker threads must not execute these operations:

- Any function protected by `Q3_ASSERT_MAIN_THREAD()`.
- Functions that use static buffers, such as `va()` and `FS_BuildOSPath()`.
- Error recovery with `Com_Error` (this function calls `longjmp`).
- Calls to `ri.*` interface functions.
- OpenGL API calls (`qgl*`).
- SDL window, video, or audio manipulation.

## Main-thread queue

The engine provides `q3::threading::MainThreadQueue` to pass data and tasks from worker threads to the main thread.

The queue provides two delivery lanes:

1. **Reliable lane:**
   - Unbounded queue backed by `std::vector` and mutex synchronization.
   - Tasks execute on the main thread in first-in, first-out order per thread.
   - Triggers a high-water warning if queued items exceed 4096.

2. **Lossy lane:**
   - Fixed 1024-slot ring buffer, guarded by a mutex. The design calls for a lock-free
     multi-producer single-consumer ring; the implementation is not there yet, so a producer can
     contend briefly with the drain. It never waits for space, which is the property the logger
     needs.
   - Accepts 64-byte `FixedTask` objects without memory allocation.
   - If the ring buffer fills, incoming tasks are dropped and an atomic counter increments.
   - When drained, prints a coalesced diagnostic message with the drop count.

The main thread drains both lanes in `Sys_SubsystemFrame` before updating timer events. The engine drains all remaining tasks during shutdown in `Sys_SubsystemShutdown`.

## Job system

`q3::threading::JobSystem` is a singleton pool of worker threads named `q3-job-0` upward. Work
goes into one of three queues, `High`, `Normal`, and `Background`, each a `std::deque` under a
single mutex and condition variable. There is no work stealing: at this granularity it would
cost more than it saves, and the interface leaves room to add it.

Submit work with `dispatch(priority, body, on_main_complete, cancel_token)`. `body` runs on a
worker. `on_main_complete`, if given, is posted to the main-thread queue rather than called on
the worker, so a completion may touch main-thread state. `parallel_for(begin, end, grain, fn)`
splits a range into chunked jobs behind a counting latch and returns one handle for all of them.

`JobHandle` carries the result:

- `wait()` blocks until the job is done. On the main thread it drains the main-thread queue
  while it waits, so a job whose completion is queued cannot deadlock against it, and it drains
  once more before returning, so the completion has run by then.
- `cancel()` sets the job's `CancelToken`. A cancelled job that has not started skips its body.
  A running job sees `is_cancelled()` and is expected to return early. **The completion still
  runs**, so a completion must not assume the body did.
- `has_exception()` and `get_exception()` report an exception the body threw. Both read as empty
  until the job is done, because that is the point at which the worker's write is visible. An
  exception never escapes a worker and never reaches `Com_Error`.

### Thread count

The cvar `com_jobThreads` (archived, latched) sets the pool size. `0` means automatic:

| Build | Automatic count | Reason |
|---|---|---|
| Client | `clamp(cores - 2, 1, 8)` | leaves the main and render threads a core each |
| `q3ded` | `clamp(cores - 1, 1, 4)` | no render thread, and a server gains nothing past four |

The pool starts in `Sys_SubsystemInit` with the client count, because `q3config.cfg` has not run
and `com_dedicated` does not exist yet. The end of `Com_Init` corrects it: an explicit
`com_jobThreads` wins, and otherwise a dedicated server is resized to its own count.

### C interface

`Sys_JobSubmit(fn, ctx, done, priority)` returns an `int` handle for C callers, with
`Sys_JobWait(int)` and `Sys_JobCancel(int)`. The handle is released when the completion runs on
the main thread, so a C caller that never drains the queue leaks handles.

### Determinism

A job may not change what the engine computes. `FS_Startup` indexes pk3 files in parallel
(`FS_ScanZipFile` on a worker, `FS_BuildPackFromIndex` on the main thread) and still builds
`fs_searchpaths` in the order `paksort` gives, so the search order and the pure-server checksums
are byte-identical to the serial path whatever `com_jobThreads` is set to.
