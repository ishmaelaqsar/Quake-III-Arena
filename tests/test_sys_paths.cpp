#include <gtest/gtest.h>
#include <cstdlib>
#include <string>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include "q_shared.h"
#include "qcommon.h"

class TempDir {
public:
    std::string path;
    TempDir() {
        char tmpl[] = "/tmp/q3_test_paths_XXXXXX";
        char *d = mkdtemp(tmpl);
        if (d) {
            path = d;
        }
    }
    ~TempDir() {
        if (!path.empty()) {
            std::string cmd = "rm -rf " + path;
            if (system(cmd.c_str())) {}
        }
    }
};

class SysPaths : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        Com_InitSmallZoneMemory();
        Com_InitZoneMemory();
    }
};

TEST_F(SysPaths, HomePathUsesHomeEnv) {
    TempDir tmp;
    ASSERT_FALSE(tmp.path.empty());

    const char *oldHome = getenv("HOME");
    std::string oldHomeStr = oldHome ? oldHome : "";

    setenv("HOME", tmp.path.c_str(), 1);
    char *home = Sys_DefaultHomePath();

    ASSERT_NE(home, nullptr);
    std::string homeStr = home;

#ifdef __APPLE__
    EXPECT_TRUE(homeStr.find("Library/Application Support/Quake3") != std::string::npos) << homeStr;
#elif !defined(_WIN32)
    EXPECT_TRUE(homeStr.find(".q3a") != std::string::npos) << homeStr;
#endif

#ifndef _WIN32
    // The directory has to exist afterwards, parents included. On macOS the path is nested
    // three deep, so a non-recursive mkdir fails with ENOENT and the function quietly returns
    // the install path instead; asserting on the string alone would not notice.
    struct stat st;
    ASSERT_EQ(stat(home, &st), 0) << "home path was not created: " << homeStr;
    EXPECT_TRUE(S_ISDIR(st.st_mode)) << homeStr << " is not a directory";
    EXPECT_EQ(st.st_mode & 0777, 0700u) << "home path should be private to the user";
#endif

    if (!oldHomeStr.empty()) {
        setenv("HOME", oldHomeStr.c_str(), 1);
    }
}

TEST_F(SysPaths, HomePathFallsBackWhenMkdirFails) {
    TempDir tmp;
    ASSERT_FALSE(tmp.path.empty());

    // A regular file, so that creating anything beneath it fails with ENOTDIR everywhere.
    const std::string blocker = tmp.path + "/blocker";
    std::ofstream(blocker) << "not a directory";

    const char *oldHome = getenv("HOME");
    std::string oldHomeStr = oldHome ? oldHome : "";
    setenv("HOME", blocker.c_str(), 1);

    char *home = Sys_DefaultHomePath();

    ASSERT_NE(home, nullptr);
    EXPECT_STREQ(home, Sys_DefaultInstallPath())
        << "an uncreatable home path should fall back to the install path";

    if (!oldHomeStr.empty()) {
        setenv("HOME", oldHomeStr.c_str(), 1);
    } else {
        unsetenv("HOME");
    }
}

TEST_F(SysPaths, InstallPathIsExecutableDir) {
    char *installPath = Sys_DefaultInstallPath();
    ASSERT_NE(installPath, nullptr);
    ASSERT_GT(strlen(installPath), 0u);

    // The test binary lives under the build directory, so the path derived from the running
    // executable must sit inside it. Asserting only non-empty would accept the current
    // directory, which is what the function returns when it cannot work the path out.
    EXPECT_NE(std::string(installPath).find(Q3_TEST_BUILD_DIR), std::string::npos)
        << "install path " << installPath << " is not under " << Q3_TEST_BUILD_DIR;

    struct stat st;
    ASSERT_EQ(stat(installPath, &st), 0) << installPath << " does not exist";
    EXPECT_TRUE(S_ISDIR(st.st_mode)) << installPath << " is not a directory";
}

TEST_F(SysPaths, ListFilesFiltersByExtension) {
    TempDir tmp;
    ASSERT_FALSE(tmp.path.empty());

    std::ofstream(tmp.path + "/a.cfg") << "data";
    std::ofstream(tmp.path + "/b.txt") << "data";
    std::ofstream(tmp.path + "/c.cfg") << "data";

    int count = 0;
    char **files = Sys_ListFiles(tmp.path.c_str(), ".cfg", nullptr, &count, qfalse);

    EXPECT_EQ(count, 2);
    ASSERT_NE(files, nullptr);

    Sys_FreeFileList(files);
}
