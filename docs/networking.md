# Fast Async HTTP Asset Downloader & FastDL

## Overview
`q3::net::HttpDownloader` in `code/sys/net/` and `CL_BeginDownload()` in `code/client/cl_main.c` provide asynchronous HTTP/HTTPS file downloading for map `.pk3` files and game assets.

---

## FastDL Workflow (`sv_dlURL` & `ws.q3df.org`)

1. **Path Traversal Security Checks (`Sys_SanitizeDownloadFilename`)**:
   - Rejects path traversal characters (`..`, `:`).
   - Rejects absolute paths (leading `/` or `\`).
   - Disallows executable extensions (`.so`, `.dll`, `.dylib`, `.exe`, `.bat`, `.sh`, `.cmd`).
   - Prevents overwriting core engine configurations (`autoexec.cfg`, `q3config.cfg`, `default.cfg`).

2. **FastDL Target Resolution**:
   - Checks server-advertised `sv_dlURL` (e.g., `https://fastdl.myserver.com/baseq3`).
   - Checks client override `cl_cURL_URL`.
   - **Worldspawn DeFRaG Archive Fallback**: If no custom FastDL URL is set, automatically resolves `https://ws.q3df.org/maps/downloads/<mapname>.pk3`.

3. **Asynchronous Non-Blocking Threads**:
   - Downloads run in worker threads using non-blocking TCP sockets without freezing loading screens or UI frame rates.
   - Updates UI cvars (`cl_downloadCount`, `cl_downloadSize`) in real-time.

4. **Dynamic VFS Rescan**:
   - When download completes, `FS_Restart(clc.checksumFeed)` re-scans `baseq3/` searchpaths so new map BSPs, textures, and shaders load immediately without restarting the engine!

---

## C-API Functions (`code/sys/sys_api.h`)
```c
qboolean Sys_SanitizeDownloadFilename(const char *filename);
void Sys_StartHttpDownload(const char *url, const char *outputPath);
int Sys_GetHttpDownloadStatus(void);
```
