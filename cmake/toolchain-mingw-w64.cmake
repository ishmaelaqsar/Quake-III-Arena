# CMake toolchain file for cross-compiling Windows x64 binaries with MinGW-w64.
#
# Used by docker/Dockerfile.mingw to build the dependencies and by `make win-build` to build the
# project. The POSIX thread variant of the compilers provides std::thread and std::mutex.
# CMAKE_CROSSCOMPILING_EMULATOR makes ctest and gtest_discover_tests run the Windows test
# executables under Wine.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)

set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-gcc-posix)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++-posix)
set(CMAKE_RC_COMPILER  ${TOOLCHAIN_PREFIX}-windres)
set(CMAKE_AR           ${TOOLCHAIN_PREFIX}-ar)
set(CMAKE_RANLIB       ${TOOLCHAIN_PREFIX}-ranlib)

# Where to look for headers, libraries, and CMake packages: the cross prefix that the image
# populates, then the MinGW sysroot. Never search the host root for libraries or headers.
set(CMAKE_FIND_ROOT_PATH
    "$ENV{MINGW_PREFIX}"
    /opt/mingw-deps
    /usr/${TOOLCHAIN_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_CROSSCOMPILING_EMULATOR wine)
