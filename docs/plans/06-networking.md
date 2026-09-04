# Checklist 06: networking, FastDL, Discord, bitstream, and local sessions

## Purpose

Fix the FastDL download path so that a client can join a server that requires a download,
make the HTTP transport secure and correct, replace the Discord Rich Presence stub with a real
IPC client, make the bitstream and transport classes exercise the real wire format, and turn
the multiplayer session manager into a controller slot registry with an honest status.

**Status:** In progress. Step N1.1 done on 2 September 2026 (UDP download restored).

## Prerequisites

- Checklist `00-environment.md` is complete. Every build and test command in this file runs
  inside the container through the Makefile targets.
- Checklist `01-build-portability.md` is complete for the CMake restructure (OBJECT libraries,
  `Q3_USE_CURL` option) and for the Windows socket compatibility header in
  `code/sys/net/net_compat.h`.
- Checklist `02-stability.md` step B4 (logger with a queued console sink) and checklist
  `05-threading.md` step T1 (`MainThreadQueue` and `Sys_PostToMainThread`) are complete. Every
  completion in this file goes through the main-thread queue.
- The owner has approved libcurl as a dependency (decision D1 in this file). If the owner
  rejects it, follow the fallback in step N1.0.

## Owner decisions this file relies on

| # | Decision | Default the plan proceeds on |
|---|---|---|
| D1 | Transport library for HTTP and HTTPS | libcurl, polled with `curl_multi` on the main thread. No worker thread. |
| D2 | Client-side URL override cvar | Remove `cl_cURL_URL`. Add `cl_dlFallbackURL`, default empty. The `ws.q3df.org` host is only contacted when the owner sets it there. |
| D3 | Discord application identifier | The owner creates an application in the Discord developer portal and sets `cl_discordClientId`. The feature is off until both `cl_discordRichPresence 1` and a non-empty identifier are set. |
| D4 | JSON and Discord libraries | Do not add nlohmann/json or the Discord GameSDK. Write a 60-line JSON writer and speak the IPC protocol directly. |
| D5 | Bitstream and transport scope | Option (a): `BitWriter` and `BitReader` become a facade over `msg.c`, and `LoopbackTransport` wraps the engine loopback so that a netchan integration test exists. |
| D6 | Split-screen | Register controller slots now. Defer split-screen rendering and document it as planned with the estimate in step N3.3. |

## Background

The audit of 1 September 2026 found the following. Re-verify each anchor before you edit,
because other checklists move code.

### FastDL is broken end to end

- `CL_BeginDownload` (`code/client/cl_main.c:1378-1426`) no longer sends the reliable
  `download <name>` command. The original line was
  `CL_AddReliableCommand(va("download %s", remoteName));` at line 1396 of commit `1c333d3`
  (`git show 1c333d3:code/client/cl_main.c`). Without it the server never starts the UDP
  transfer, `CL_ParseDownload` (`code/client/cl_parse.c:473-556`) is never entered, and the
  client stalls in `CA_CONNECTED`.
- The client registers `sv_dlURL` itself with `CVAR_SYSTEMINFO` (`cl_main.c:1403`) and
  `cl_cURL_URL` (`cl_main.c:1404`). The server never declares `sv_dlURL`, so the systeminfo
  flag has no effect. Because the cvar is server controlled on connect, a server can point the
  client at any host and port (server-side request forgery). No scheme or host check exists.
- The `ws.q3df.org` fallback (`cl_main.c:1411-1417`) fires with no `cl_allowDownload` check
  and no opt-in cvar. It uses an `https://` URL, which the downloader cannot speak.
- The target path is `"baseq3/%s"` relative to the working directory (`cl_main.c:1421-1424`).
  It ignores `fs_homepath` and `fs_game`. `clc.downloadTempName` is computed at
  `cl_main.c:1391` and never used, so a failed transfer leaves a corrupt file at the final name.
- `Sys_StartHttpDownload` (`code/sys/sys_api.cpp:119-129`) returns at once.
  `Sys_GetHttpDownloadStatus` (`sys_api.cpp:131-139`) has zero callers. `CL_NextDownload`
  proceeds, and `CL_DownloadsComplete` calls `FS_Restart(clc.checksumFeed)` at
  `cl_main.c:1327` while the worker might still write the file.
- The progress callback (`sys_api.cpp:123-128`) calls `Cvar_SetValue` from the worker thread.
- `Sys_SanitizeDownloadFilename` (`sys_api.cpp:82-117`) is a case-sensitive extension
  blacklist (`sys_api.cpp:103-104`), the configuration-file check is an exact full-string
  compare nested inside the has-extension branch (`sys_api.cpp:110`), and the check is not an
  allowlist.
- The legacy UDP protocol is intact on both sides: server handlers for `download`, `nextdl`,
  `stopdl`, and `donedl` (`code/server/sv_client.c:1214-1217`) and `SV_WriteDownloadToClient`
  (`sv_client.c:754`); client `CL_ParseDownload` writes `clc.downloadTempName` with
  `FS_SV_FOpenFileWrite` and renames with `FS_SV_Rename` (`cl_parse.c:473-556`).

### The HTTP client is unsafe

`code/sys/net/http_downloader.cpp`:

- No TLS. An `https://` prefix only sets `port = 443` (`:57-62`) and then sends a plaintext
  `GET` (`:117`).
- Blocking sockets with no timeout (`connect` at `:100`, `recv` at `:135`).
- `std::stoi` on the port (`:74`) and `std::stoull` on `Content-Length` (`:148`) run on
  server-controlled input inside a `std::thread` with no `try`. An exception calls
  `std::terminate`. A server can crash the client.
- No status code and no redirect handling (`:138-155`). A 404 body is written as a `.pk3`.
- `Content-Length:` match is case sensitive (`:145`).
- `error_` is a non-atomic `std::string` written by the worker and read by the main thread.
- `cancel()` (`:41-46`) joins without a socket shutdown, so it can block forever.
- The global `g_httpDownloader` (`sys_api.cpp:10`) owns a `std::thread` and is destroyed at
  process exit.
- IPv4 only (`:80`). The `_WIN32` branch (`:10-12`) has no `WSAStartup` and calls `close()`.
  `send()` return value ignored (`:117`). Progress is not reported for the first body chunk
  (`:160`).

### Discord, bitstream, and session are stubs

- `code/sys/rpc/discord_rpc.cpp` is 29 lines. It stores a string and logs. It never opens a
  socket or a pipe. Only `tests/test_discord_rpc.cpp` calls it.
- `code/sys/net/bitstream.cpp:25-36` writes bits least-significant-bit first with no Huffman
  coding. It cannot read or write a real Quake III packet. Only `tests/test_modern_net.cpp`
  uses it.
- `code/sys/multiplayer/session.hpp:28` embeds a `LoopbackTransport` per slot that is a
  `std::queue` never connected to a socket. No split-screen rendering exists. The engine calls
  only `SessionManager::instance().reset()` (`sys_api.cpp:23`).
- The engine loopback that the transport should wrap is `NET_SendLoopPacket` and
  `NET_GetLoopPacket` (`code/qcommon/net_chan.c:577-615`).

### Dead hosts

`MASTER_SERVER_NAME "master.quake3arena.com"` (`code/qcommon/qcommon.h:237`) and
`AUTHORIZE_SERVER_NAME` (`qcommon.h:240`) are offline. Checklist `08-renderer-ui.md` step U1.7
adds the master server cvars, and checklist `02-stability.md` step B9 removes the authorize
handshake. This file does not touch them.

## Steps

### Phase N1: FastDL correctness and security (about 5 days)

- [ ] **N1.0 Record the TLS decision.**
  The plan proceeds with option A. If the owner chooses otherwise, update this table and the
  steps below before you start.

  | Option | Pros | Cons |
  |---|---|---|
  | A. libcurl (default) | HTTPS, redirects, IPv6, timeouts, chunked encoding, proxies handled. `curl_multi` polls on the main thread, so every threading defect disappears. System package on Linux and macOS, vcpkg on Windows. ioquake3 `cl_curl.c` is a proven reference. | New dependency. |
  | B. mbedTLS plus the in-house client | Small footprint. | You must write correct HTTP/1.1 parsing, redirects, chunked encoding, IPv6, timeouts, and certificate stores on three platforms. This is the code the audit found broken. |
  | C. HTTP only, remove HTTPS from the docs | No dependency. | Most FastDL hosts are HTTPS only. The in-house client still needs a rewrite. |

  Add a CMake option `Q3_USE_CURL` (default `ON`). When `OFF`, the HTTP path compiles out and
  the UDP path remains.
  **Tests:** none, because this is a decision.
  **Verify:** the option appears in `cmake -LH` output.

- [x] **N1.1 Restore the UDP download path.** Done on 2 September 2026.
  Files: `code/client/cl_main.c`.
  In `CL_BeginDownload` (`cl_main.c:1378-1426`) reinstate
  `CL_AddReliableCommand( va("download %s", remoteName) );` as the default action after the
  UI cvars are set. Delete the client-side `Cvar_Get("sv_dlURL", ...)` at `:1403`, the
  `Cvar_Get("cl_cURL_URL", ...)` at `:1404`, the `ws.q3df.org` branch at `:1411-1417`, the
  `targetPath` block at `:1421-1424`, and the `Sys_StartHttpDownload` call. Keep the
  `clc.downloadTempName` assignment at `:1391`. Keep the `Sys_SanitizeDownloadFilename` guard
  for now; step N1.5 replaces it.
  **Tests:** none, because this needs a live server. The integration check below covers it.
  **Verify:** in the container start `q3ded +set dedicated 1 +set sv_allowDownload 1
  +set sv_pure 1 +map <map from a non-id pk3>`. Connect a client without that pk3 with
  `cl_allowDownload 1`. Expect the UDP transfer, `FS_Restart`, `donedl`, and a joined game.
  With `cl_allowDownload 0` the client prints the missing-files warning from
  `CL_InitDownloads` (`cl_main.c:1485-1496`).

- [ ] **N1.2 Declare `sv_dlURL` on the server.**
  Files: `code/server/sv_init.c` (cvar block at `:584-605`), `code/server/server.h`,
  `code/server/sv_main.c:34`.
  Add `sv_dlURL = Cvar_Get("sv_dlURL", "", CVAR_SYSTEMINFO | CVAR_ARCHIVE);` next to
  `sv_pure` so that it travels in `CS_SYSTEMINFO`. Add the `extern` to `server.h`. Keep
  `sv_allowDownload` (`sv_init.c:605`) as the UDP gate.
  **Tests:** none, because this is a cvar registration. Document it in `docs/cvars.md`
  (checklist 10).
  **Verify:** `q3ded +set sv_dlURL http://127.0.0.1:8000 +map q3dm17` and a connected client
  prints the URL in the `Com_DPrintf` you add in N1.6 with `developer 1`.

- [ ] **N1.3 Read `sv_dlURL` on the client and add the policy cvars.**
  Files: `code/client/client.h` (`clientConnection_t` at `:183-191`),
  `code/client/cl_parse.c` (`CL_SystemInfoChanged` at `:327`), `code/client/cl_main.c`
  (`CL_Init` near `:2342`).
  - Add to `clientConnection_t`: `char sv_dlURL[MAX_OSPATH];`, `char downloadRemoteName[MAX_OSPATH];`,
    `qboolean httpDownloading;`, `qboolean httpUsed;`.
  - In `CL_SystemInfoChanged`, after the `sv_referencedPakNames` handling (`cl_parse.c:356-358`),
    add `Q_strncpyz(clc.sv_dlURL, Info_ValueForKey(systemInfo, "sv_dlURL"), sizeof clc.sv_dlURL);`.
  - Define in `client.h`: `DLF_ENABLE 1`, `DLF_NO_REDIRECT 2` (disable HTTP), `DLF_NO_UDP 4`,
    `DLF_NO_DISCONNECT 8` (reserved). `cl_allowDownload` keeps default `"1"` and becomes this
    bitmask.
  - Register in `CL_Init`: `cl_dlMaxSize` (megabytes, default `"512"`, `CVAR_ARCHIVE`),
    `cl_dlTimeout` (seconds, default `"30"`, `CVAR_ARCHIVE`), `cl_dlFallbackURL` (default `""`,
    `CVAR_ARCHIVE`).
  **Tests:** none, because this is plumbing. N1.5 and N1.6 tests cover the consumers.
  **Verify:** `cvarlist cl_dl` lists the three new cvars with their defaults.

- [ ] **N1.4 Rewrite `HttpDownloader` on `curl_multi` with no thread.**

  **This step also owns the downloader tests, reassigned from `03-tests.md` step C7.5 on
  4 September 2026.** C7.5 was ticked without being written; rather than write an in-process
  HTTP server against socket code this step deletes, its content moves here: an in-process
  server thread bound to `127.0.0.1:0` in a new `tests/test_http_server.hpp`, asserting bytes
  received, monotonic progress, and `Completed`; a connection-refused case asserting `Failed`
  with a non-empty error; and a `cancel()` mid-transfer case that returns within 2 seconds.
  Delete the `ParseAndDownloadMock` tautology at `tests/test_http_downloader.cpp:5-11` with the
  code it pretends to cover.

  What the current implementation gets wrong, so the rewrite has a target: an `https://` URL
  sets port 443 and then speaks cleartext (`code/sys/net/http_downloader.cpp:72-75,134-137`);
  there is no connect timeout at all, only `SO_RCVTIMEO` (`:113-121`), so a black-holed host
  blocks the worker in `connect` and `cancel()` blocks the caller behind a `join`; `std::stoi`
  (`:87`) and `std::stoull` (`:171`) run on server-controlled input on a thread with no `catch`,
  which is `std::terminate`; the `Content-Length` parse hardcodes the header length and is
  case-sensitive, so a lower-case header is simply not read; there is no status-code check, so a
  404 body is written to disk and reported `Completed`; and `hints.ai_family = AF_INET` makes it
  IPv4-only. None of this is reachable from the shipping client today, which is why it is a
  latent bug and not an incident.

  Files: `code/sys/net/http_downloader.hpp`, `code/sys/net/http_downloader.cpp` (rewrite),
  `code/sys/sys_api.h`, `code/sys/sys_api.cpp` (replace `:119-139`), `CMakeLists.txt`
  (`find_package(CURL REQUIRED)` under `Q3_USE_CURL`, link `CURL::libcurl` to `q3sys` as
  `PUBLIC`), `tests/CMakeLists.txt` (inherits the link).
  Class sketch:

  ```cpp
  namespace q3::net {
  enum class DownloadStatus { Idle, Downloading, Completed, Failed };
  struct DownloadOptions { std::size_t max_bytes; long timeout_s; std::string user_agent; };
  class HttpDownloader {
  public:
      using WriteSink = std::function<bool(const void*, std::size_t)>;
      bool begin(std::string_view url, WriteSink sink, DownloadOptions opts);
      void poll();                       // curl_multi_perform + curl_multi_info_read, once per frame
      void cancel();
      DownloadStatus status() const;
      std::string error() const;         // owned copy, valid after Failed
      std::size_t downloaded() const;
      std::size_t total() const;
  };
  }
  ```

  Set these options on the easy handle: `CURLOPT_URL`, `CURLOPT_FOLLOWLOCATION` with
  `CURLOPT_MAXREDIRS 5`, `CURLOPT_PROTOCOLS_STR "http,https"`,
  `CURLOPT_REDIR_PROTOCOLS_STR "http,https"`, `CURLOPT_FAILONERROR 1`,
  `CURLOPT_MAXFILESIZE_LARGE opts.max_bytes`, `CURLOPT_CONNECTTIMEOUT 10`,
  `CURLOPT_LOW_SPEED_LIMIT 1` with `CURLOPT_LOW_SPEED_TIME opts.timeout_s`,
  `CURLOPT_NOSIGNAL 1`, `CURLOPT_SSL_VERIFYPEER 1` (default, do not disable),
  `CURLOPT_USERAGENT opts.user_agent` (use `"Quake3Modern/1.32"`), `CURLOPT_WRITEFUNCTION`
  and `CURLOPT_WRITEDATA`. Leave `CURLOPT_IPRESOLVE` at its default so IPv6 works.
  The write callback forwards to the sink and returns `0` when the sink fails or the byte count
  exceeds `max_bytes` (belt and braces for chunked responses). Read progress in `poll()` from
  `CURLINFO_CONTENT_LENGTH_DOWNLOAD_T` and `CURLINFO_SIZE_DOWNLOAD_T`. Call
  `curl_global_init(CURL_GLOBAL_DEFAULT)` once in `Sys_SubsystemInit` and
  `curl_global_cleanup` in `Sys_SubsystemShutdown` (added by checklist 02 step B5).
  Replace the C API in `sys_api.h`:

  ```c
  qboolean Sys_HttpDownloadBegin(const char *url,
                                 qboolean (*write)(const void *data, int len, void *ud),
                                 void *ud, int maxBytes, int timeoutSec);
  int  Sys_HttpDownloadPoll(int *downloaded, int *total, char *err, int errLen); /* 0 idle, 1 busy, 2 done, 3 failed */
  void Sys_HttpDownloadCancel(void);
  ```

  Delete `Sys_StartHttpDownload`, `Sys_GetHttpDownloadStatus`, the `g_httpDownloader` global
  with a thread (`sys_api.cpp:10`), and the socket code and `<winsock2.h>` branch in
  `http_downloader.cpp`. The curl object is a plain static.
  **Tests:** rewrite `tests/test_http_downloader.cpp` (`q3sys_tests`). Add a 40-line in-test
  TCP server on a `std::thread` bound to `127.0.0.1:0`. Cases: `HttpDownloader.RefusesFileScheme`
  (`begin("file:///etc/passwd")` fails synchronously with a protocol error),
  `HttpDownloader.RefusesGopherScheme`, `HttpDownloader.NotFoundFails` (server replies
  `HTTP/1.1 404`, status becomes `Failed`, the sink is never called),
  `HttpDownloader.OkCompletes` (server replies `200` with `Content-Length`, bytes match, status
  `Completed`, progress is monotonic), `HttpDownloader.RedirectToFileFails` (`302` to a
  `file://` URL fails), `HttpDownloader.CancelMidTransfer` (server stalls after headers,
  `cancel()` returns and status is `Failed`). Drive `poll()` in a loop with a 5 s deadline.
  **Verify:** `make test -R HttpDownloader` passes, and `ldd build/quake3_modern`
  lists libcurl.

- [x] **N1.5 Replace the sanitizer with an allowlist and a URL builder.** Done on 3 September 2026, ahead of the rest of phase N1, because the four bypasses were live and untested.

  The rules now live in `Sys_SanitizeDownloadFilename` in `code/sys/sys_api.cpp` rather than in a
  separate `code/sys/net/download_policy.{hpp,cpp}`. Split the file out when the rest of N1
  lands; the behaviour and its tests move unchanged. `BuildDownloadUrl` is still to do.

  What changed: an allowlist replaced the blacklist. The only accepted shape is
  `<gamedir>/<name>.pk3`, which is what `FS_ComparePaks` produces
  (`code/qcommon/files.cpp:2588-2614`). The game directory must be `baseq3` or the running
  `fs_game`, the extension must be `.pk3` compared case-insensitively with a non-empty stem, each
  segment is checked against `[A-Za-z0-9_.-]`, `.` and `..` segments are refused, and the stock
  paks (`pak0` to `pak8` in `baseq3`, `pak0` to `pak3` in `missionpack`) cannot be overwritten.

  The four bypasses this closes, all of which passed before and none of which had a test:
  `evil.SO`, because the extension compare was case-sensitive; `maps/autoexec.cfg`, because the
  config check compared the whole string; any extensionless name, because that check sat inside a
  branch taken only when the name had an extension; and `.command`, `.py`, or `.dylib.1`, because
  a blacklist can only reject what it lists.
  - **Tests:** `tests/test_download_policy.cpp` (binary `quake3_tests`), eleven cases, one group
    per bypass plus traversal, percent-encoded input, stock paks, and the mod directory. The
    sanitizer cases were removed from `tests/test_http_downloader.cpp`, whose positive cases
    (`maps/q3dm17.pk3`, a bare `pak6-custom.pk3`) encoded the old lax shape.
  - **Verify:** `make test CTEST_ARGS='-R DownloadPolicy'` passes, and so does `make asan` over
    the same cases.
  Files: new `code/sys/net/download_policy.hpp` and `download_policy.cpp` (pure functions),
  `code/sys/sys_api.cpp` (`Sys_SanitizeDownloadFilename` at `:82-117` becomes a wrapper),
  `code/sys/sys_api.h`.

  ```cpp
  namespace q3::net {
  bool is_safe_download_name(std::string_view name, std::string_view fs_game);
  std::optional<std::string> build_download_url(std::string_view base, std::string_view remote_name);
  }
  ```

  Rules for `is_safe_download_name`, applied to both `localName` and `remoteName`:
  - Length below `MAX_QPATH`. Characters only `[A-Za-z0-9_./-]`. No `\`, no `:`, no leading
    `/`, no empty segment, no `.` or `..` segment.
  - Exactly two segments `<gamedir>/<file>`. `<gamedir>` equals `baseq3` or `fs_game`
    (`Q_stricmp`).
  - Extension is `.pk3`, case insensitive. `.pk3.exe`, `.so`, `.cfg`, and extensionless names
    fail because they are not `.pk3`.
  - Refuse `pak0.pk3` to `pak8.pk3` under `baseq3` and `pak0.pk3` to `pak3.pk3` under
    `missionpack`. The server already refuses to serve them (`sv_client.c:773`); the client must
    refuse to overwrite them.
  Rules for `build_download_url`: `base` starts with `http://` or `https://`, has a non-empty
  host, contains no `?`, `#`, or whitespace. Strip one trailing `/`. Percent-encode each path
  segment of `remote_name`. Result shorter than 1024 bytes.
  The old configuration-name blacklist becomes redundant. Delete it.
  **Tests:** new `tests/test_download_policy.cpp` (`q3sys_tests`), table driven. Positive:
  `baseq3/map-foo.pk3`, `BASEQ3/X.PK3`, `missionpack/x.pk3` when `fs_game` is `missionpack`.
  Negative: `../x.pk3`, `baseq3/../baseq3/x.pk3`, `baseq3/pak0.pk3`, `missionpack/pak0.pk3`,
  `c:/x.pk3`, `baseq3/x.so`, `baseq3/x.pk3.exe`, `baseq3/x`, `otherdir/x.pk3` with `fs_game`
  `baseq3`, `baseq3//x.pk3`, `/baseq3/x.pk3`, a 65-character name. URL cases:
  `http://h/baseq3` plus `baseq3/a b.pk3` encodes the space; `javascript:x`, `file:///`,
  `http://` with empty host, `http://h/?a=b` all return `nullopt`; a trailing `/` is stripped
  once.
  **Verify:** `make test -R DownloadPolicy` passes.

- [ ] **N1.6 Implement the client download state machine.**
  Files: `code/client/cl_main.c` (`CL_BeginDownload`, `CL_NextDownload` at `:1434-1472`,
  `CL_DownloadsComplete` at `:1321`, `CL_Frame` at `:2025` before `CL_SendCmd()`,
  `CL_Disconnect` at `:736-741`), `code/client/cl_parse.c` (unchanged).
  `CL_BeginDownload(localName, remoteName)`:
  1. Validate both names with `is_safe_download_name`. On failure call
     `Com_Error(ERR_DROP, "Refusing unsafe download '%s'", remoteName)`. A server that offers
     unsafe names is hostile, so dropping is correct (ioquake3 does the same).
  2. Set `clc.downloadName`, `clc.downloadTempName`, `clc.downloadRemoteName`, the UI cvars,
     `clc.downloadBlock = 0`, `clc.downloadCount = 0` as today.
  3. If `(cl_allowDownload->integer & DLF_ENABLE) && !(cl_allowDownload->integer & DLF_NO_REDIRECT)
     && clc.sv_dlURL[0]`, call `CL_HttpBeginDownload(build_download_url(clc.sv_dlURL, remoteName))`.
  4. Else if `clc.sv_dlURL[0] == 0 && cl_dlFallbackURL->string[0] && !Cvar_VariableIntegerValue("sv_allowDownload")`,
     use the fallback URL with the basename only (`strrchr(remoteName, '/') + 1`) and the same
     HTTP path.
  5. Else if `!(cl_allowDownload->integer & DLF_NO_UDP)`, call
     `CL_AddReliableCommand(va("download %s", remoteName))`.
  6. Else `Com_Error(ERR_DROP, "Download of %s is disabled by cl_allowDownload", remoteName)`.
  `CL_HttpBeginDownload(url)`: `clc.download = FS_SV_FOpenFileWrite(clc.downloadTempName)`. On
  failure fall through to step 5. Call `Sys_HttpDownloadBegin(url, CL_HttpWriteSink, NULL,
  cl_dlMaxSize->integer * 1024 * 1024, cl_dlTimeout->integer)`. The sink does
  `FS_Write(data, len, clc.download)` and returns `qtrue` only on a full write. Set
  `clc.httpDownloading = clc.httpUsed = qtrue`. Print `HTTP download: %s`.
  `CL_HttpDownloadFrame()` (new, called from `CL_Frame` when `clc.httpDownloading`): call
  `Sys_HttpDownloadPoll`, mirror into `cl_downloadCount` and `cl_downloadSize` on the main
  thread. On `2`: `FS_FCloseFile(clc.download); clc.download = 0;
  FS_SV_Rename(clc.downloadTempName, clc.downloadName);` clear both names, set
  `cl_downloadName` to empty, `clc.httpDownloading = qfalse`, call `CL_NextDownload()`. On `3`:
  close the handle, remove the temp file (`FS_HomeRemove` if present, otherwise the
  `FS_Remove(FS_BuildOSPath(fs_homepath, temp, ""))` pattern from `FS_SV_Rename`), print the
  curl error, then fall back to UDP with `download <clc.downloadRemoteName>` when
  `!(cl_allowDownload->integer & DLF_NO_UDP)` and the serverinfo `sv_allowDownload` is set;
  otherwise `Com_Error(ERR_DROP, ...)`.
  While `clc.httpDownloading`, set `clc.lastPacketTime = cls.realtime` every frame so
  `cl_timeout` does not drop the idle UDP channel.
  `CL_DownloadsComplete` (`:1321-1335`) is unchanged. It is now reached only after every file is
  renamed, which removes the `FS_Restart` race.
  `CL_Disconnect` (`:736-741`): if `clc.httpDownloading`, call `Sys_HttpDownloadCancel()`,
  clear the flag, close the handle, and remove the temp file.
  **Tests:** none, because the state machine needs a live server. Put the manual script below
  in `docs/networking.md` (checklist 10) and run it as the acceptance check.
  **Verify:** run this six-step script.
  1. In a directory with `baseq3/mymap.pk3` run `python3 -m http.server 8000`. Start
     `q3ded +set sv_dlURL http://127.0.0.1:8000 +set sv_allowDownload 0 +set sv_pure 1 +map mymap`.
     A client without the pk3 joins over HTTP. `fs_homepath/baseq3/mymap.pk3` exists, no `.tmp`
     remains, `FS_Restart` ran once.
  2. Delete the file from the HTTP directory. With `sv_allowDownload 1` the 404 falls back to
     UDP and the client joins. With `sv_allowDownload 0` the client drops with a message.
  3. Set `sv_dlURL https://127.0.0.1:8443` with a self-signed certificate. The download fails
     with a certificate error, which proves verification is on.
  4. Set `sv_dlURL file:///etc`. The URL is rejected before any request.
  5. Kill the HTTP server mid-transfer. The `.tmp` file is removed and the client returns to the
     menu.
  6. `cl_allowDownload 3` uses UDP only. `cl_allowDownload 5` uses HTTP only with no fallback.

- [ ] **N1.7 Clean up.**
  Files: `code/sys/net/http_downloader.cpp`, `tests/test_http_downloader.cpp`,
  `code/sys/sys_api.h`.
  Remove the remaining socket code and platform branches. Remove the `ParseAndDownloadMock`
  test (`tests/test_http_downloader.cpp:5-11`). Remove the unused `Modern_*` alias block from
  `sys_api.h` (grep shows no callers outside the header).
  **Tests:** covered by N1.4 and N1.5.
  **Verify:** `grep -n "socket(\|recv(\|winsock" code/sys/net/http_downloader.cpp` prints
  nothing.

### Phase N2: Discord Rich Presence over IPC (about 3 days, no library)

Protocol summary. Transport is a Unix domain socket or a Windows named pipe. Candidate paths in
order: `$XDG_RUNTIME_DIR`, `$TMPDIR`, `$TMP`, `$TEMP`, `/tmp`, each with `discord-ipc-0` to
`discord-ipc-9`, plus the Flatpak subdirectory `app/com.discordapp.Discord/` and the Snap
subdirectory `snap.discord/` on Linux. On macOS use `$TMPDIR/discord-ipc-N`. On Windows use
`\\.\pipe\discord-ipc-N`. A frame is an 8-byte header of two little-endian `int32` values
(opcode, payload length) followed by UTF-8 JSON. Opcodes: `0` handshake, `1` frame, `2` close,
`3` ping, `4` pong. Handshake: send opcode `0` with `{"v":1,"client_id":"<id>"}`; success is an
opcode `1` frame that contains `"evt":"READY"`; failure is opcode `2`. Activity: opcode `1` with
`{"cmd":"SET_ACTIVITY","args":{"pid":<pid>,"activity":{...}},"nonce":"<counter>"}`. Clear with
`"activity":null`. Reply to opcode `3` with opcode `4` and the same payload. Discord drops
updates faster than one per 15 s, so coalesce.

- [ ] **N2.1 Write the JSON writer.**
  Files: new `code/sys/rpc/json_writer.hpp` (header only, about 60 lines).
  API: `JsonWriter& begin_object()`, `end_object()`, `begin_array()`, `end_array()`,
  `key(std::string_view)`, `value(std::string_view)`, `value(int64_t)`, `value(bool)`,
  `value(std::nullptr_t)`, `std::string str() const`. Escape `"`, `\`, `\b`, `\f`, `\n`, `\r`,
  `\t`, and every code point below `0x20` as `\u00XX`.
  **Tests:** new `tests/test_json_writer.cpp` (`q3sys_tests`). Cases: `JsonWriter.EscapesControlCharacters`,
  `JsonWriter.NestsObjectsAndArrays`, `JsonWriter.WritesNumbersAndBooleansAndNull`,
  `JsonWriter.CommaPlacement`.
  **Verify:** `make test -R JsonWriter` passes.

- [ ] **N2.2 Write the IPC connection class.**
  Files: new `code/sys/rpc/discord_ipc.hpp` and `discord_ipc.cpp`.
  Class `DiscordIpc` with `bool connect(std::string_view client_id)` (iterates the candidate
  paths, POSIX `socket(AF_UNIX)` plus non-blocking `connect` and `poll` with a 2 s timeout),
  `bool send_frame(int op, std::string_view json)`, `std::optional<Frame> read_frame(int timeout_ms)`
  (uses `poll`, rejects a length above 64 KiB), `void close()`. Windows branch under
  `#ifdef _WIN32` uses `CreateFileW` and overlapped `ReadFile` with `WaitForSingleObject` for
  the timeout. The Windows branch is compile-only until the Windows CI leg exists.
  **Tests:** rewrite `tests/test_discord_rpc.cpp` (`q3sys_tests`) part one. A fake server binds
  a Unix socket at `<TempDir>/discord-ipc-9` with `XDG_RUNTIME_DIR` set to the temp dir for the
  test, accepts, checks the handshake JSON has `"v":1` and the client identifier, replies
  `{"cmd":"DISPATCH","evt":"READY"}` with opcode `1`, then asserts the next frame is opcode `1`
  and contains the state string; sends opcode `3` and expects opcode `4` with the same payload.
  Skip on Windows for now.
  **Verify:** `make test -R DiscordIpc` passes.

- [ ] **N2.3 Rewrite the manager with a worker thread and a main-thread mirror.**
  Files: `code/sys/rpc/discord_rpc.hpp` and `discord_rpc.cpp` (rewrite, keep the class name
  `DiscordRpcManager` and the `DiscordPresence` struct).
  - `init(client_id)` starts one `std::thread` named `q3-discord` if not running.
    `shutdown()` sets `stop_`, notifies, sends opcode `2` when connected, and joins. The
    destructor calls `shutdown()`.
  - `update_presence(p)` locks, stores `desired_`, sets `dirty_`, and notifies. It never blocks.
  - Worker state machine: `Disconnected` → `Connecting` → `Connected`. On failure back off
    `1, 2, 4, ..., 60` s with jitter. While connected, `read_frame(250)` handles ping, close,
    and end of file. When `dirty_` and at least 15 s passed since the last send, serialise
    `desired_` and send. On disconnect, set `dirty_` so presence is re-sent after reconnect.
  - Main-thread mirror: `std::atomic<ConnectionState> state_`, `std::atomic<int> sends_`,
    `std::atomic<int> failures_`, and `last_error_` under the mutex. The worker never calls an
    engine function and never uses `LOG_*`. It posts console messages through
    `Sys_PostToMainThread`, and `Sys_SubsystemFrame` prints them with `Com_Printf`.
  **Tests:** `tests/test_discord_rpc.cpp` part two. Cases: `DiscordRpc.ConnectsAndSendsActivity`
  (with the fake server from N2.2), `DiscordRpc.ShutdownWithoutServerReturnsWithinThreeSeconds`,
  `DiscordRpc.RateLimiterCoalesces` (inject a clock through a constructor parameter; two updates
  within 15 s produce one send), `DiscordRpc.ResendsAfterReconnect`.
  **Verify:** `make test -R DiscordRpc` passes.

- [ ] **N2.4 Integrate with the engine.**
  Files: `code/sys/sys_api.h` and `sys_api.cpp` (new C API), `code/client/cl_main.c`
  (`CL_Init` near `:2342`, `CL_Frame`, `CL_Shutdown` at `:2441`, `CL_Disconnect` at `:724`),
  `code/client/cl_cgame.c` (`CL_InitCGame` at `:716-751`), `code/client/cl_parse.c`
  (`CL_ConfigstringModified` for `CS_PLAYERS`).
  C API: `Sys_DiscordInit(const char *clientId)`, `Sys_DiscordShutdown(void)`,
  `Sys_DiscordSetPresence(const char *state, const char *details, const char *map, int cur,
  int max, qboolean inGame)`, `Sys_DiscordStatus(char *buf, int len)`.
  Cvars: `cl_discordRichPresence` (`"0"`, `CVAR_ARCHIVE`), `cl_discordClientId` (`""`,
  `CVAR_ARCHIVE`). No placeholder identifier.
  `CL_Init`: when both are set, call `Sys_DiscordInit`. `CL_Frame`: on `modified` toggle init
  or shutdown. Presence sources: main menu on `CL_Disconnect` and at init; on `CL_InitCGame`
  read `mapname`, `sv_hostname`, `sv_maxclients`, and `g_gametype` from `CS_SERVERINFO`
  (`cl_cgame.c:728`); player count is the number of non-empty `CS_PLAYERS + i` strings;
  details `"<gametype> on <map>"`, state `"<hostname>"` or `"Local game"` when
  `com_sv_running`; party size `[players, maxclients]`; start timestamp at map load. Re-send on
  `CS_PLAYERS` changes (the limiter coalesces). Add the console command `discord_status` in
  `CL_Init` and remove it in `CL_Shutdown`. `q3ded` never calls these.
  **Tests:** none, because it needs the Discord desktop client.
  **Verify:** with Discord open, set `cl_discordClientId <id>` and `cl_discordRichPresence 1`.
  The profile shows the game. `map q3dm17` updates the details within 15 s. Quit Discord and
  `discord_status` shows reconnecting with backoff. Restart Discord and presence returns. Set
  `cl_discordRichPresence 0` and presence clears within 1 s.

### Phase N3: bitstream, transport, and local sessions (about 3 days)

- [ ] **N3.1 Make `BitWriter` and `BitReader` a facade over `msg.c`.**
  Files: `code/sys/net/bitstream.hpp` and `bitstream.cpp` (rewrite).
  `BitWriter` owns a `std::vector<std::uint8_t>` and a `msg_t`. The constructor takes a
  capacity and `bool oob = false` and calls `MSG_Init` or `MSG_InitOOB` (both call
  `MSG_initHuffman()` on first use, `msg.c:44-52`).

  | Facade method | `msg.c` function | Note |
  |---|---|---|
  | `write_bits(value, bits)` | `MSG_WriteBits` | Throw `std::invalid_argument` when `bits` is `0` or outside `[-31, 32]`. `MSG_WriteBits` calls `Com_Error(ERR_DROP)`, which aborts in tests. |
  | `write_byte`, `write_short`, `write_int`, `write_float` | `MSG_WriteByte`, `MSG_WriteShort`, `MSG_WriteLong`, `MSG_WriteFloat` | |
  | `write_string` | `MSG_WriteString` | |
  | `write_data` | `MSG_WriteData` | |
  | `write_vec3` | three `MSG_WriteFloat` | |
  | `write_delta_usercmd(from, to)` | `MSG_WriteDeltaUsercmd` | Real wire format used by `cl_input.c` and `sv_client.c`. |
  | `overflowed()`, `data()`, `byte_size()` | `msg.overflowed`, `msg.data`, `msg.cursize` | Drop `bit_size()` or return `msg.bit`. |
  | `BitReader(data, size)` and `read_*` | `MSG_BeginReading`, `MSG_Read*` | `MSG_ReadString` returns a static buffer. Copy into `std::string`. |
  | `has_remaining()` | `readcount < cursize` | |

  **Tests:** extend `tests/test_modern_net.cpp` (`quake3_tests`, because it links `qcommon`).
  Cases: `Bitstream.RoundTrip`, `Bitstream.ByteIdenticalToRawMsg` (the same sequence written
  through raw `MSG_*` into a `byte[]` equals `BitWriter::data()`),
  `Bitstream.ReaderDecodesRawMsg`, `Bitstream.OobModeMatchesRaw`,
  `Bitstream.DeltaUsercmdRoundTrip`, `Bitstream.InvalidBitCountThrows`. Keep the existing
  `BitWriterAndReaderRoundTrip` expectations.
  **Verify:** `make test -R Bitstream` passes.

- [ ] **N3.2 Wrap the engine loopback and add a netchan integration test.**
  Files: `code/sys/net/transport.hpp` (edit), new `code/sys/net/transport.cpp`, new
  `tests/test_netchan_loopback.cpp`, `tests/CMakeLists.txt`, `tests/test_platform_stubs.cpp`
  (`Sys_SendPacket` stub at `:90` stays a no-op; loopback never reaches it).
  Keep `INetTransport`. `LoopbackTransport(netsrc_t side)` implements `send` as
  `NET_SendLoopPacket(side, len, data, to)` and `receive` as
  `NET_GetLoopPacket(side, &from, &msg)` (`net_chan.c:577-615`). Remove `pipe_to` and the
  private `std::queue`. The engine ring of 16 packets, including drop on overflow, is the real
  behaviour.
  **Tests:** new `tests/test_netchan_loopback.cpp` (`quake3_tests`). Call `Cvar_Init()`,
  `Cmd_Init()`, `Netchan_Init(port)`, then `Netchan_Setup(NS_SERVER, &sv, adrLoopback, qport)`
  and `Netchan_Setup(NS_CLIENT, &cl, adrLoopback, qport)`. Cases:
  `NetchanLoopback.SmallMessage` (100 bytes), `NetchanLoopback.FragmentedMessage` (3000 bytes,
  above `FRAGMENT_SIZE` 1300, uses `Netchan_TransmitNextFragment`),
  `NetchanLoopback.IncomingSequenceIncrements`, `NetchanLoopback.RingOverflowDrops` (17
  packets, the oldest is lost). Pump `NET_GetLoopPacket` and `Netchan_Process` on the peer and
  assert payload equality.
  **Verify:** `make test -R NetchanLoopback` passes.

- [ ] **N3.3 Make `SessionManager` a controller slot registry.**
  Files: `code/sys/multiplayer/session.hpp` and `session.cpp`, `code/sys/sys_sdl.cpp`
  (controller subsystem init at `:372-394` and the event loop), `code/sys/sys_api.cpp`
  (`Sys_SubsystemFrame`), `code/client/cl_main.c` (cvar and command),
  `tests/test_modern_multiplayer.cpp`.
  - Remove the `net::LoopbackTransport transport` field from `LocalPlayerSlot`
    (`session.hpp:28`). Add `SDL_JoystickID controller_id{-1}` and `bool has_controller`.
  - In `sys_sdl.cpp`, on `SDL_CONTROLLERDEVICEADDED` and `SDL_CONTROLLERDEVICEREMOVED` call
    `SessionManager::instance().bind_controller(id)` and `unbind_controller(id)`. Slot 0 is
    always the keyboard and mouse player. Controllers fill slots 1 to 3 when the new cvar
    `cl_splitScreenSlots` (default `"1"`, `CVAR_ARCHIVE`, clamped 1 to 4) allows. Only slot 0
    input reaches `CL_JoystickEvent`; other slots are registered but inert.
  - `update_viewports` takes `cls.glconfig.vidWidth` and `vidHeight` from a new
    `Sys_SessionUpdateViewports(w, h)` called from `CL_InitRenderer`.
  - Console command `session_status` prints slots, controller names
    (`SDL_GameControllerNameForIndex`), viewports, and the line
    `split-screen rendering: not implemented (planned)`.
  Estimate for real split-screen, for the roadmap: multiple `clientActive_t` and netchans to
  the local server (about 1 week), per-view cgame instances because cgame uses globals (about
  2 weeks), `CL_CgameSystemCalls` per instance (about 1 week), `RE_RenderScene` per viewport
  with viewport and scissor in the renderer (about 1 week), per-slot input routing and HUD
  (about 1 week), UI for joining slots (about 1 week). About 6 to 8 weeks including testing.
  **Tests:** update `tests/test_modern_multiplayer.cpp` (`q3sys_tests`). Cases:
  `Session.AddAndRemoveSlot`, `Session.BindControllerFillsSlotsInOrder`,
  `Session.BindControllerRespectsMaxSlots`, `Session.UnbindFreesSlot`,
  `Session.ViewportsForOneToFourPlayers` (unchanged math).
  **Verify:** plug in a controller and `session_status` shows it in slot 1. Unplug it and the
  slot is freed. (Manual, from a CI artifact.)

## Test map

| Test file | Binary | Cases | Added by |
|---|---|---|---|
| `tests/test_http_downloader.cpp` | `q3sys_tests` | RefusesFileScheme, RefusesGopherScheme, NotFoundFails, OkCompletes, RedirectToFileFails, CancelMidTransfer | N1.4 |
| `tests/test_download_policy.cpp` | `q3sys_tests` | Table of positive and negative names, URL builder cases | N1.5 |
| `tests/test_json_writer.cpp` | `q3sys_tests` | EscapesControlCharacters, NestsObjectsAndArrays, WritesNumbersAndBooleansAndNull, CommaPlacement | N2.1 |
| `tests/test_discord_rpc.cpp` | `q3sys_tests` | Fake IPC server handshake, READY, SET_ACTIVITY, PING to PONG, shutdown within 3 s, rate limiter, resend after reconnect | N2.2, N2.3 |
| `tests/test_modern_net.cpp` | `quake3_tests` | RoundTrip, ByteIdenticalToRawMsg, ReaderDecodesRawMsg, OobModeMatchesRaw, DeltaUsercmdRoundTrip, InvalidBitCountThrows | N3.1 |
| `tests/test_netchan_loopback.cpp` | `quake3_tests` | SmallMessage, FragmentedMessage, IncomingSequenceIncrements, RingOverflowDrops | N3.2 |
| `tests/test_modern_multiplayer.cpp` | `q3sys_tests` | AddAndRemoveSlot, BindControllerFillsSlotsInOrder, BindControllerRespectsMaxSlots, UnbindFreesSlot, ViewportsForOneToFourPlayers | N3.3 |

Integration checks that are not GoogleTest: the six-step FastDL script (N1.6) and the Discord
desktop check (N2.4). Both live in `docs/networking.md` and `docs/discord_rpc.md`.

## Out of scope and follow-ons

- Download resume and parallel downloads.
- Split-screen rendering (6 to 8 weeks, see N3.3).
- Master server cvars (checklist 08 step U1.7) and the authorize server removal (checklist 02
  step B9).
- The Windows named-pipe branch of `DiscordIpc` is compile-only until the Windows CI leg runs.

## Done criteria

- A client without a pk3 joins a server over UDP, over HTTP with `sv_dlURL`, and falls back
  from a 404 to UDP. A self-signed HTTPS host fails on the certificate. `file://` is refused. An
  interrupted transfer leaves no `.tmp` file.
- No engine function is called from a worker thread in `code/sys/net` or `code/sys/rpc`
  (ThreadSanitizer run from checklist 05 is clean).
- Presence appears in Discord within 15 s of a map load and clears when the cvar is off.
- `BitWriter` output is byte identical to raw `MSG_*` output.
- Every row of the test map exists and passes under `ctest --preset dev` and
  `ctest --preset asan` in the container.
- `docs/networking.md`, `docs/discord_rpc.md`, `docs/local_multiplayer.md`, and `docs/cvars.md`
  describe the built behaviour (checklist 10).

## Last step

- [ ] Delete this file and remove its row from `docs/plans/README.md`.
