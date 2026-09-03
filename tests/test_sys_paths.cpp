#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "q_shared.h"
#include "qcommon.h"

#ifdef _WIN32
#include <process.h>    // _getpid
#else
#include <sys/stat.h>   // the POSIX permission check
#include <unistd.h>     // getpid
#endif

namespace {

// std::filesystem rather than mkdtemp and `rm -rf`, so the fixture builds on Windows and does
// not shell out. The process id and the test name keep concurrent ctest processes apart.
class TempDir {
public:
    std::filesystem::path path;

    TempDir() {
        const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
        const std::string name = info != nullptr ? info->name() : "unknown";
        path = std::filesystem::temp_directory_path() /
               ("q3_test_paths_" + std::to_string(
#ifdef _WIN32
                    static_cast<unsigned long>(_getpid())
#else
                    static_cast<unsigned long>(getpid())
#endif
                    ) + "_" + name);
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        std::filesystem::create_directories(path, ec);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    std::string str() const { return path.string(); }
};

void SetEnvVar(const char *name, const char *value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void UnsetEnvVar(const char *name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

}  // namespace

class SysPaths : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        Com_InitSmallZoneMemory();
        Com_InitZoneMemory();
    }
};

TEST_F(SysPaths, HomePathUsesHomeEnv) {
    TempDir tmp;
    ASSERT_FALSE(tmp.str().empty());

    const char *oldHome = getenv("HOME");
    std::string oldHomeStr = oldHome ? oldHome : "";

    SetEnvVar("HOME", tmp.str().c_str());
    char *home = Sys_DefaultHomePath();

    ASSERT_NE(home, nullptr);
    std::string homeStr = home;

#ifdef __APPLE__
    EXPECT_TRUE(homeStr.find("Library/Application Support/Quake3") != std::string::npos) << homeStr;
#elif !defined(_WIN32)
    EXPECT_TRUE(homeStr.find(".q3a") != std::string::npos) << homeStr;
#endif

    // The directory has to exist afterwards, parents included. On macOS the path is nested
    // three deep, so a non-recursive mkdir fails with ENOENT and the function quietly returns
    // the install path instead; asserting on the string alone would not notice.
    ASSERT_TRUE(std::filesystem::is_directory(home)) << "home path was not created: " << homeStr;

#ifndef _WIN32
    struct stat st;
    ASSERT_EQ(stat(home, &st), 0) << homeStr;
    EXPECT_EQ(st.st_mode & 0777, 0700u) << "home path should be private to the user";
#endif

    if (!oldHomeStr.empty()) {
        SetEnvVar("HOME", oldHomeStr.c_str());
    }
}

// POSIX only: Sys_DefaultHomePath reads HOME there, so pointing HOME at a regular file forces
// the creation to fail. The Windows implementation calls SDL_GetPrefPath, which consults no
// variable a test can redirect, so there is no way to provoke the fallback and nothing to assert.
#ifndef _WIN32
TEST_F(SysPaths, HomePathFallsBackWhenMkdirFails) {
    TempDir tmp;
    ASSERT_FALSE(tmp.str().empty());

    // A regular file, so that creating anything beneath it fails with ENOTDIR everywhere.
    const std::string blocker = (tmp.path / "blocker").string();
    std::ofstream(blocker) << "not a directory";

    const char *oldHome = getenv("HOME");
    std::string oldHomeStr = oldHome ? oldHome : "";
    SetEnvVar("HOME", blocker.c_str());

    char *home = Sys_DefaultHomePath();

    ASSERT_NE(home, nullptr);
    EXPECT_STREQ(home, Sys_DefaultInstallPath())
        << "an uncreatable home path should fall back to the install path";

    if (!oldHomeStr.empty()) {
        SetEnvVar("HOME", oldHomeStr.c_str());
    } else {
        UnsetEnvVar("HOME");
    }
}
#endif  // !_WIN32

TEST_F(SysPaths, InstallPathIsExecutableDir) {
    char *installPath = Sys_DefaultInstallPath();
    ASSERT_NE(installPath, nullptr);
    ASSERT_GT(strlen(installPath), 0u);

    // The test binary lives under the build directory, so the path derived from the running
    // executable must sit inside it. Asserting only non-empty would accept the current
    // directory, which is what the function returns when it cannot work the path out.
    //
    // Compare with separators normalised: CMake hands Q3_TEST_BUILD_DIR over with forward
    // slashes, while SDL returns the platform separator, so a direct search fails on Windows
    // for a path that is perfectly correct.
    auto forwardSlashes = [](std::string text) {
        std::replace(text.begin(), text.end(), '\\', '/');
        return text;
    };
    EXPECT_NE(forwardSlashes(installPath).find(forwardSlashes(Q3_TEST_BUILD_DIR)),
              std::string::npos)
        << "install path " << installPath << " is not under " << Q3_TEST_BUILD_DIR;

    EXPECT_TRUE(std::filesystem::is_directory(installPath))
        << installPath << " is not an existing directory";
}

TEST_F(SysPaths, ListFilesFiltersByExtension) {
    TempDir tmp;
    ASSERT_FALSE(tmp.str().empty());

    std::ofstream((tmp.path / "a.cfg").string()) << "data";
    std::ofstream((tmp.path / "b.txt").string()) << "data";
    std::ofstream((tmp.path / "c.cfg").string()) << "data";

    int count = 0;
    char **files = Sys_ListFiles(tmp.str().c_str(), ".cfg", nullptr, &count, qfalse);

    EXPECT_EQ(count, 2);
    ASSERT_NE(files, nullptr);

    Sys_FreeFileList(files);
}
