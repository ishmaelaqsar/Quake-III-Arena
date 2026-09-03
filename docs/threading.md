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
   - Fixed 1024-slot multi-producer single-consumer ring buffer.
   - Accepts 64-byte `FixedTask` objects without memory allocation.
   - If the ring buffer fills, incoming tasks are dropped and an atomic counter increments.
   - When drained, prints a coalesced diagnostic message with the drop count.

The main thread drains both lanes in `Sys_SubsystemFrame` before updating timer events. The engine drains all remaining tasks during shutdown in `Sys_SubsystemShutdown`.
