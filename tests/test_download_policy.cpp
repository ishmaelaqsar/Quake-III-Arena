/*
Tests for the download filename policy (checklist 06 step N1.5).

A server controls these strings, so the negative cases matter more than the positive one. Each
group below records a bypass that the previous blacklist allowed. The only legitimate shape is
`<gamedir>/<name>.pk3`, because FS_ComparePaks builds the download list from the server's
referenced pak names, which are `<gamedir>/<basename>` with `.pk3` appended.
*/

#include <gtest/gtest.h>

#include <string>

#include "engine_init.hpp"
#include "q_shared.h"
#include "qcommon.h"
#include "../code/sys/sys_api.h"

namespace {

class DownloadPolicy : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        EnsureEngineInitialised();
        Cvar_Set("fs_game", "");  // plain baseq3 unless a case sets otherwise
    }
};

}  // namespace

TEST_F(DownloadPolicy, AcceptsAPakInTheBaseGame) {
    EXPECT_TRUE(Sys_SanitizeDownloadFilename("baseq3/pak9-custom.pk3"));
    EXPECT_TRUE(Sys_SanitizeDownloadFilename("baseq3/my-map.pk3"));
    EXPECT_TRUE(Sys_SanitizeDownloadFilename("baseq3/Map_2.PK3"));
}

TEST_F(DownloadPolicy, AcceptsThePakOfTheRunningMod) {
    Cvar_Set("fs_game", "defrag");
    EXPECT_TRUE(Sys_SanitizeDownloadFilename("defrag/maps-pack.pk3"));
    EXPECT_FALSE(Sys_SanitizeDownloadFilename("someothermod/maps-pack.pk3"))
        << "only the base game and the running mod may be written to";
    Cvar_Set("fs_game", "");
}

// Bypass 1: the extension compare was case-sensitive, so an upper-case variant passed and, on a
// case-insensitive filesystem, satisfied a later dlopen of the lower-case name.
TEST_F(DownloadPolicy, RejectsCaseVariantsOfExecutableExtensions) {
    for (const char *name : {"baseq3/evil.SO", "baseq3/evil.So", "baseq3/trojan.DLL",
                             "baseq3/trojan.Dll", "baseq3/x.DYLIB", "baseq3/x.EXE"}) {
        EXPECT_FALSE(Sys_SanitizeDownloadFilename(name)) << name;
    }
}

// Bypass 2: the config check compared the whole string, so any directory prefix evaded it.
TEST_F(DownloadPolicy, RejectsConfigFilesWithAPathPrefix) {
    for (const char *name : {"baseq3/autoexec.cfg", "baseq3/q3config.cfg", "baseq3/default.cfg",
                             "baseq3/AUTOEXEC.CFG", "maps/autoexec.cfg"}) {
        EXPECT_FALSE(Sys_SanitizeDownloadFilename(name)) << name;
    }
}

// Bypass 3: the config check sat inside a branch taken only when the name had an extension, so
// an extensionless name skipped every content check.
TEST_F(DownloadPolicy, RejectsNamesWithoutAnExtension) {
    for (const char *name : {"baseq3/autoexec", "baseq3/noextension", "baseq3/pak0"}) {
        EXPECT_FALSE(Sys_SanitizeDownloadFilename(name)) << name;
    }
}

// Bypass 4: a blacklist can only reject what it lists. These were all accepted.
TEST_F(DownloadPolicy, RejectsAnythingThatIsNotAPk3) {
    for (const char *name : {"baseq3/x.dylib.1", "baseq3/x.so.2", "baseq3/run.command",
                             "baseq3/s.py", "baseq3/s.pl", "baseq3/s.zsh", "baseq3/x.pk3.exe",
                             "baseq3/.pk3"}) {
        EXPECT_FALSE(Sys_SanitizeDownloadFilename(name)) << name;
    }
}

TEST_F(DownloadPolicy, RejectsTraversalAndAbsolutePaths) {
    for (const char *name : {"../autoexec.cfg", "..\\q3config.cfg", "/etc/passwd",
                             "c:\\windows\\system32\\cmd.exe", "baseq3/../secret.pk3",
                             "/baseq3/x.pk3", "baseq3//x.pk3", "baseq3/./x.pk3",
                             "../baseq3/x.pk3"}) {
        EXPECT_FALSE(Sys_SanitizeDownloadFilename(name)) << name;
    }
}

// Percent-encoding is not decoded anywhere on this path, so these arrive literally and the
// character set rejects them. The test pins that, so a future decode step cannot open a hole
// unnoticed.
TEST_F(DownloadPolicy, RejectsEncodedAndUnusualCharacters) {
    for (const char *name : {"baseq3/%2e%2e/x.pk3", "baseq3/a b.pk3", "baseq3/a\tb.pk3",
                             "baseq3/a;b.pk3", "baseq3/a|b.pk3", "baseq3/a$b.pk3",
                             "baseq3/a\nb.pk3"}) {
        EXPECT_FALSE(Sys_SanitizeDownloadFilename(name)) << name;
    }
}

// A server must not be able to replace the paks the game shipped with.
TEST_F(DownloadPolicy, RefusesToOverwriteStockPaks) {
    for (const char *name : {"baseq3/pak0.pk3", "baseq3/pak8.pk3", "baseq3/PAK0.PK3"}) {
        EXPECT_FALSE(Sys_SanitizeDownloadFilename(name)) << name;
    }
    Cvar_Set("fs_game", "missionpack");
    EXPECT_FALSE(Sys_SanitizeDownloadFilename("missionpack/pak0.pk3"));
    EXPECT_FALSE(Sys_SanitizeDownloadFilename("missionpack/pak3.pk3"));
    EXPECT_TRUE(Sys_SanitizeDownloadFilename("missionpack/pak4.pk3"))
        << "missionpack ships four paks, so pak4 onward is a legitimate download";
    Cvar_Set("fs_game", "");
}

TEST_F(DownloadPolicy, RejectsEmptyAndOverlongNames) {
    EXPECT_FALSE(Sys_SanitizeDownloadFilename(nullptr));
    EXPECT_FALSE(Sys_SanitizeDownloadFilename(""));
    EXPECT_FALSE(Sys_SanitizeDownloadFilename("baseq3/"));
    EXPECT_FALSE(Sys_SanitizeDownloadFilename("/x.pk3"));
    EXPECT_FALSE(Sys_SanitizeDownloadFilename(std::string("baseq3/" + std::string(MAX_QPATH, 'a') + ".pk3").c_str()));
}
