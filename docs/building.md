# Building Quake III Arena

This document describes how to build and test the project.

## Build in the container

You can build and test the project on Linux without local package installations. The project uses Docker and Docker Compose to provide the build tools and dependencies.

### Requirements

- Docker Engine
- Docker Compose plugin
- GNU Make

### Common targets

Run these Make commands from the root directory of the repository:

- `make build`: build the Linux binaries in `build/`.
- `make test`: run the test suite with `ctest`.
- `make asan`: build and run tests with AddressSanitizer and UndefinedBehaviorSanitizer.
- `make tsan`: build and run tests with ThreadSanitizer.
- `make smoke`: run the gate G1 headless screenshot comparison test.
- `make bot-match`: run a 60-second dedicated server test with four bots.
- `make shell`: start an interactive Bash session inside the container.
- `make clean`: delete the default Linux build directory.
- `make distclean`: remove all build directories and container volumes.

### Game data files

Integration tests and smoke tests require original game data files (`pak0.pk3` through `pak8.pk3`). Place your pak files in `docker/paks/`, or set the `Q3_PAKS` environment variable to their directory:

```sh
make smoke Q3_PAKS=/path/to/paks
```

## Native builds

Checklist `01-build-portability.md` completes this section.
