# Build and test entry points for the Quake III Arena modern fork.
#
# Linux and Windows targets run inside the containers defined in compose.yaml, so nothing is
# installed on the host. The `native-*` targets build on the host with its own compiler and
# fetch every dependency into the build tree; they exist for macOS, which cannot run in a
# container. Run `make help` for the target list.
#
# Variables you can override on the command line:
#   BUILD_DIR   Linux build directory (default: build)
#   BUILD_TYPE  CMake build type (default: RelWithDebInfo)
#   CMAKE_ARGS  extra arguments for the configure step
#   CTEST_ARGS  extra arguments for ctest, for example CTEST_ARGS='-R Cvar'
#   Q3_PAKS     host directory that holds pak0.pk3 to pak8.pk3 (default: docker/paks)
#   CI          set to any value to run compose without a TTY

SHELL := /bin/bash

BUILD_DIR        ?= build
WIN_BUILD_DIR    ?= build-win64
NATIVE_BUILD_DIR ?= build-native
BUILD_TYPE       ?= RelWithDebInfo
CMAKE_ARGS       ?=
CTEST_ARGS       ?=
Q3_PAKS          ?= $(CURDIR)/docker/paks

export Q3_UID  := $(shell id -u)
export Q3_GID  := $(shell id -g)
export Q3_PAKS

COMPOSE   := docker compose
TTY_FLAG  := $(if $(CI),-T,)
RUN       := $(COMPOSE) run --rm $(TTY_FLAG) dev
RUN_WIN   := $(COMPOSE) run --rm $(TTY_FLAG) win
IMAGE     := q3-dev
IMAGE_WIN := q3-dev-mingw

CMAKE_COMMON := -G Ninja \
    -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Sanitizer flags are passed through the CMake flag variables until checklist 01 adds the
# Q3_SANITIZE option. Switch these targets to -DQ3_SANITIZE=... when that option exists.
ASAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize=alignment
TSAN_FLAGS := -fsanitize=thread -fno-omit-frame-pointer
ASAN_ENV   := -e ASAN_OPTIONS=detect_leaks=0:strict_string_checks=1 -e UBSAN_OPTIONS=print_stacktrace=1
TSAN_ENV   := -e TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1

define sanitizer_cmake_args
-DCMAKE_C_FLAGS='$(1)' -DCMAKE_CXX_FLAGS='$(1)' \
-DCMAKE_EXE_LINKER_FLAGS='$(1)' -DCMAKE_SHARED_LINKER_FLAGS='$(1)'
endef

.PHONY: help image image-rebuild configure build test asan tsan smoke smoke-update-golden \
        apitrace bot-match shell clean distclean \
        image-win win-configure win-build win-test win-shell \
        native-configure native-build native-test

help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*?## ' $(MAKEFILE_LIST) | sort | \
	    awk 'BEGIN {FS = ":.*?## "}; {printf "  %-22s %s\n", $$1, $$2}'

# ---------------------------------------------------------------------------------------------
# Linux (container `dev`)
# ---------------------------------------------------------------------------------------------

image: ## Build the Linux development image if it does not exist
	@docker image inspect $(IMAGE) >/dev/null 2>&1 || $(COMPOSE) build dev

image-rebuild: ## Rebuild the Linux development image from scratch
	$(COMPOSE) build --no-cache dev

configure: image ## Configure the Linux build in BUILD_DIR
	$(RUN) cmake -S . -B $(BUILD_DIR) $(CMAKE_COMMON) $(CMAKE_ARGS)

build: configure ## Configure and build for Linux
	$(RUN) cmake --build $(BUILD_DIR)

test: build ## Build and run the unit tests on Linux
	$(RUN) ctest --test-dir $(BUILD_DIR) --output-on-failure $(CTEST_ARGS)

asan: image ## Linux build and tests with AddressSanitizer and UndefinedBehaviorSanitizer
	$(RUN) cmake -S . -B build-asan $(CMAKE_COMMON) $(call sanitizer_cmake_args,$(ASAN_FLAGS)) $(CMAKE_ARGS)
	$(RUN) cmake --build build-asan
	$(COMPOSE) run --rm $(TTY_FLAG) $(ASAN_ENV) dev \
	    ctest --test-dir build-asan --output-on-failure $(CTEST_ARGS)

tsan: image ## Linux build and tests with ThreadSanitizer
	$(RUN) cmake -S . -B build-tsan $(CMAKE_COMMON) $(call sanitizer_cmake_args,$(TSAN_FLAGS)) $(CMAKE_ARGS)
	$(RUN) cmake --build build-tsan
	$(COMPOSE) run --rm $(TTY_FLAG) $(TSAN_ENV) dev \
	    ctest --test-dir build-tsan --output-on-failure $(CTEST_ARGS)

smoke: build ## Gate G1: headless timedemo screenshot compared with ci/smoke/golden
	$(RUN) ci/smoke/run_smoke.sh $(BUILD_DIR)

smoke-update-golden: build ## Regenerate the gate G1 golden image (say why in the commit)
	$(RUN) ci/smoke/run_smoke.sh --update-golden $(BUILD_DIR)

apitrace: build ## Record an apitrace of the smoke run and print GL call counts
	$(RUN) ci/smoke/run_smoke.sh --apitrace $(BUILD_DIR)

bot-match: build ## Run a 60 second dedicated-server bot match and check the log
	$(RUN) ci/smoke/run_bot_match.sh $(BUILD_DIR)

shell: image ## Open a shell in the Linux container
	$(RUN) bash

# ---------------------------------------------------------------------------------------------
# Windows x64 cross-compile with MinGW-w64, tests under Wine (container `win`)
# ---------------------------------------------------------------------------------------------

image-win: ## Build the MinGW cross image if it does not exist (builds SDL2, curl, LuaJIT)
	@docker image inspect $(IMAGE_WIN) >/dev/null 2>&1 || $(COMPOSE) build win

win-configure: image-win ## Configure the Windows cross build in WIN_BUILD_DIR
	$(RUN_WIN) cmake -S . -B $(WIN_BUILD_DIR) $(CMAKE_COMMON) \
	    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64.cmake $(CMAKE_ARGS)

win-build: win-configure ## Cross-compile Windows binaries
	$(RUN_WIN) cmake --build $(WIN_BUILD_DIR)

win-test: win-build ## Run the Windows unit tests under Wine
	$(RUN_WIN) ctest --test-dir $(WIN_BUILD_DIR) --output-on-failure $(CTEST_ARGS)

win-shell: image-win ## Open a shell in the MinGW container
	$(RUN_WIN) bash

# ---------------------------------------------------------------------------------------------
# Native host build (macOS). Uses the host compiler, CMake, and Ninja; every library dependency
# is fetched into the build tree by the Q3_FETCH_DEPS option (checklist 01), so nothing is
# installed on the host. Not for Linux: use the container targets there.
# ---------------------------------------------------------------------------------------------

native-configure: ## Configure a native host build with fetched dependencies (macOS)
	cmake -S . -B $(NATIVE_BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
	    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DQ3_FETCH_DEPS=ON $(CMAKE_ARGS)

native-build: native-configure ## Build natively on the host (macOS)
	cmake --build $(NATIVE_BUILD_DIR)

native-test: native-build ## Run the unit tests natively on the host (macOS)
	ctest --test-dir $(NATIVE_BUILD_DIR) --output-on-failure $(CTEST_ARGS)

# ---------------------------------------------------------------------------------------------
# Cleanup
# ---------------------------------------------------------------------------------------------

clean: ## Remove the default Linux build directory
	rm -rf $(BUILD_DIR)

distclean: clean ## Remove every build directory, smoke output, and the container volumes
	rm -rf build-asan build-tsan $(WIN_BUILD_DIR) $(NATIVE_BUILD_DIR) ci/smoke/out
	-$(COMPOSE) down --volumes --remove-orphans
