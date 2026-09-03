# Build and test

Everything runs in a container, so nothing is installed on the host. The `Makefile` at the
repository root wraps the containers defined in `compose.yaml`. Run `make help` for the target
list.

## Linux, in the container

```sh
make build      # configure and compile
make test       # build, then run the unit tests
make shell      # a prompt inside the container
```

The first `make` builds the image from `docker/Dockerfile`, which takes a few minutes. Later
runs reuse it, and `ccache` persists in a named volume.

Prerequisites on the host: Docker with the Compose plugin. Nothing else. Do not call
`docker compose` directly: the `Makefile` exports `Q3_UID` and `Q3_GID` so that files the
container writes belong to you, and the compose file now refuses to start without them.

## Game data

The paks are not in the repository. Copy `pak0.pk3` to `pak8.pk3` from a Quake III Arena
installation into `docker/paks/`, or point `Q3_PAKS` at a directory holding them:

```sh
Q3_PAKS=/path/to/quake3/baseq3 make smoke
```

Unit tests do not need the paks. The rendering and server gates do.

## Gates

```sh
make smoke                # gate G1: render a fixed demo frame, compare with the golden image
make smoke-update-golden  # accept the current frame as the new golden image
make bot-match            # 60 seconds of the dedicated server with four bots
make apitrace             # record the smoke run and count GL calls
```

`ci/smoke/README.md` describes what each gate checks and why. The golden image does not exist
yet; until it does, `make smoke` saves a candidate and exits non-zero.

## Sanitizers

```sh
make asan   # AddressSanitizer and UndefinedBehaviorSanitizer
make tsan   # ThreadSanitizer
```

Both go through the `Q3_SANITIZE` CMake option, so a local run and the continuous integration
legs build identically. The alignment check is excluded, because the zone allocator hands out
four-byte-aligned blocks by design; leak detection is off, because the zone and hunk never free
at exit.

## Windows, cross-compiled

```sh
make win-build   # cross-compile with MinGW-w64
make win-test    # run the Windows unit tests under Wine
```

This image builds SDL2, curl, and LuaJIT from source, so the first build is slow. It is
`linux/amd64`, so on an Apple Silicon host it runs under emulation and the compiler has been
seen to crash there; build it on an x86_64 machine. Microsoft Visual C++ is the Windows gate in
continuous integration, not this leg.

## macOS, natively

macOS cannot run in a container, so this is the one native path:

```sh
make native-build
make native-test
```

These pass `-DQ3_FETCH_DEPS=ON`, which fetches SDL2, LuaJIT, and GoogleTest into the build tree,
so no Homebrew package is needed. You need Xcode, CMake, and Ninja. The `macos-arm64` continuous
integration leg is the primary macOS check, and its artifacts can be downloaded for a real-GPU
run.

## Cleaning

```sh
make clean       # the default build directory
make distclean   # every build directory, gate output, and the container volumes
```
