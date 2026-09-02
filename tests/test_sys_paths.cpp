#include <gtest/gtest.h>
#include <cstdlib>
#include <string>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include "q_shared.h"
#include "qcommon.h"

char *Sys_DefaultHomePath(void);
char *Sys_DefaultInstallPath(void);
char **Sys_ListFiles(const char *directory, const char *extension, char *filter, int *numfiles, qboolean wantsubs);
void Sys_FreeFileList(char **list);
}

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
    EXPECT_TRUE(homeStr.find("Library/Application Support/Quake3") != std::string::npos);
#elif !defined(_WIN32)
    EXPECT_TRUE(homeStr.find(".q3a") != std::string::npos);
    struct stat st;
    if (stat(home, &st) == 0) {
        EXPECT_EQ(st.st_mode & 0777, 0700);
    }
#endif

    if (!oldHomeStr.empty()) {
        setenv("HOME", oldHomeStr.c_str(), 1);
    }
}

TEST_F(SysPaths, HomePathFallsBackWhenMkdirFails) {
    const char *oldHome = getenv("HOME");
    std::string oldHomeStr = oldHome ? oldHome : "";

    // Set HOME to a path inside /proc or non-writable path where mkdir fails
    setenv("HOME", "/proc/nonexistent_q3_path", 1);
    char *home = nullptr;
    EXPECT_NO_THROW({
        home = Sys_DefaultHomePath();
    });
    EXPECT_NE(home, nullptr);

    if (!oldHomeStr.empty()) {
        setenv("HOME", oldHomeStr.c_str(), 1);
    }
}

TEST_F(SysPaths, InstallPathIsExecutableDir) {
    char *installPath = Sys_DefaultInstallPath();
    ASSERT_NE(installPath, nullptr);
    EXPECT_GT(strlen(installPath), 0u);
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
