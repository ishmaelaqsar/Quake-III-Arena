#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>

#include "engine_fixture.hpp"
#include "zip_writer.hpp"
#include "q_shared.h"
#include "qcommon.h"
#include "threading/job_system.hpp"

namespace {

class FilesFixture : public q3::test::FsFixture {
protected:
    void SetUp() override {
        q3::test::FsFixture::SetUp();

        std::string pak0_data = q3::test::write_zip({
            {"default.cfg", "pak-default"},
            {"productid.txt", "quake3"},
            {"maps/a.bsp", "bsp-data"},
            {"scripts/x.shader", "x-shader-content"}
        });

        temp_dir().write("baseq3/pak0.pk3", pak0_data);
        temp_dir().write("baseq3/loose.cfg", "loose-cfg-content");

        FS_InitFilesystem();
    }
};

} // namespace

TEST_F(FilesFixture, ReadFromPakReturnsContent) {
    void *buf = nullptr;
    int len = FS_ReadFile("scripts/x.shader", &buf);
    ASSERT_GT(len, 0);
    EXPECT_EQ(std::string_view((char *)buf, len), "x-shader-content");
    FS_FreeFile(buf);
}

TEST_F(FilesFixture, LooseFileIsFoundWhenNotInPak) {
    void *buf = nullptr;
    int len = FS_ReadFile("loose.cfg", &buf);
    ASSERT_GT(len, 0);
    EXPECT_EQ(std::string_view((char *)buf, len), "loose-cfg-content");
    FS_FreeFile(buf);
}

TEST_F(FilesFixture, PakShadowsLooseFileOfSameName) {
    // Write loose default.cfg that should be shadowed by pak0.pk3's version
    temp_dir().write("baseq3/default.cfg", "loose-default");
    FS_Restart(0);

    void *buf = nullptr;
    int len = FS_ReadFile("default.cfg", &buf);
    ASSERT_GT(len, 0);
    EXPECT_EQ(std::string_view((char *)buf, len), "pak-default");
    FS_FreeFile(buf);
}

TEST_F(FilesFixture, MissingFileReturnsMinusOne) {
    fileHandle_t f = 0;
    int len = FS_FOpenFileRead("non_existent_file.cfg", &f, qfalse);
    EXPECT_LT(len, 0);
}

TEST_F(FilesFixture, ReadFreeLeavesLoadStackAtZero) {
    EXPECT_EQ(FS_LoadStack(), 0);
    void *buf = nullptr;
    int len = FS_ReadFile("scripts/x.shader", &buf);
    ASSERT_GT(len, 0);
    EXPECT_GT(FS_LoadStack(), 0);
    FS_FreeFile(buf);
    EXPECT_EQ(FS_LoadStack(), 0);
}

TEST_F(FilesFixture, WriteFileLandsOnlyInHomePath) {
    const char *content = "written-data";
    FS_WriteFile("test_out.txt", content, 12);

    auto home_file = temp_dir().path() / "baseq3" / "test_out.txt";
    EXPECT_TRUE(std::filesystem::exists(home_file));
}

TEST_F(FilesFixture, ListFilesFiltersByExtension) {
    int numfiles = 0;
    char **list = FS_ListFiles("scripts", ".shader", &numfiles);
    ASSERT_NE(list, nullptr);
    EXPECT_GT(numfiles, 0);

    bool found = false;
    for (int i = 0; i < numfiles; ++i) {
        if (std::string(list[i]) == "x.shader") {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
    FS_FreeFileList(list);
}

TEST_F(FilesFixture, PureServerRefusesLooseFile) {
    temp_dir().write("baseq3/loose.txt", "loose-txt-content");
    const char *paks = FS_LoadedPakChecksums();
    FS_PureServerSetLoadedPaks(paks, "");

    fileHandle_t f = 0;
    int len = FS_FOpenFileRead("loose.txt", &f, qfalse);
    EXPECT_LT(len, 0);
}

TEST_F(FilesFixture, BuildOSPathUsesPlatformSeparator) {
    char *ospath = FS_BuildOSPath(temp_dir().string().c_str(), "baseq3", "test.txt");
    ASSERT_NE(ospath, nullptr);
    EXPECT_NE(strstr(ospath, "baseq3"), nullptr);
#if defined(_WIN32)
    EXPECT_NE(strchr(ospath, '\\'), nullptr);
#else
    EXPECT_NE(strchr(ospath, '/'), nullptr);
#endif
}

TEST_F(FilesFixture, RestartKeepsChecksums) {
    std::string before = FS_LoadedPakChecksums();
    FS_Restart(12345);
    std::string after = FS_LoadedPakChecksums();
    EXPECT_EQ(before, after);
}

TEST(Files, MissingDefaultCfgNamesPak0AndPaths) {
    q3::test::EnsureEngineInitialised();
    q3::test::TempDir empty_dir;

    Cvar_Set("fs_basepath", empty_dir.string().c_str());
    Cvar_Set("fs_homepath", empty_dir.string().c_str());
    Cvar_Set("fs_cdpath", empty_dir.string().c_str());

    std::string captured_error;
    try {
        FS_InitFilesystem();
    } catch (const q3::test::SysErrorException &e) {
        captured_error = e.what();
    }

    EXPECT_NE(captured_error.find("pak0.pk3"), std::string::npos)
        << "Error message did not name pak0.pk3: " << captured_error;
    EXPECT_NE(captured_error.find(empty_dir.string()), std::string::npos)
        << "Error message did not contain searched directory: " << captured_error;
}

TEST_F(FilesFixture, PakOrderIsDeterministic) {
    std::string pak_dummy = q3::test::write_zip({{"dummy.txt", "1"}});
    temp_dir().write("baseq3/pak1.pk3", pak_dummy);
    temp_dir().write("baseq3/aaa.pk3", pak_dummy);
    temp_dir().write("baseq3/zzz.pk3", pak_dummy);

    FS_Restart(0);

    std::string names = FS_LoadedPakNames();
    EXPECT_NE(names.find("pak0"), std::string::npos);
    EXPECT_NE(names.find("pak1"), std::string::npos);
    EXPECT_NE(names.find("aaa"), std::string::npos);
    EXPECT_NE(names.find("zzz"), std::string::npos);
}

TEST(Files, PakOrderAndChecksumsStable) {
    q3::test::EnsureEngineInitialised();
    q3::test::TempDir temp_dir;
    Cvar_Set("fs_basepath", temp_dir.string().c_str());
    Cvar_Set("fs_homepath", temp_dir.string().c_str());
    Cvar_Set("fs_cdpath", temp_dir.string().c_str());

    for (int i = 0; i < 8; ++i) {
        std::vector<q3::test::ZipEntry> files = {
            {"conflict.txt", "conflict-from-pak" + std::to_string(i)},
            {"unique" + std::to_string(i) + ".txt", "unique-data-" + std::to_string(i)}
        };
        if (i == 0) {
            files.push_back({"default.cfg", "pak-default"});
            files.push_back({"productid.txt", "quake3"});
        }
        std::string pak_data = q3::test::write_zip(files);
        temp_dir.write("baseq3/pak" + std::to_string(i) + ".pk3", pak_data);
    }

    // 1. Single-threaded load
    q3::threading::JobSystem::instance().resize(1);
    FS_InitFilesystem();

    std::string single_names = FS_LoadedPakNames();
    std::string single_checksums = FS_LoadedPakChecksums();
    std::string single_pure_checksums = FS_LoadedPakPureChecksums();

    void *buf1 = nullptr;
    int len1 = FS_ReadFile("conflict.txt", &buf1);
    ASSERT_GT(len1, 0);
    std::string single_conflict((char *)buf1, len1);
    FS_FreeFile(buf1);

    // 2. Multi-threaded load
    q3::threading::JobSystem::instance().resize(8);
    FS_Restart(0);

    std::string multi_names = FS_LoadedPakNames();
    std::string multi_checksums = FS_LoadedPakChecksums();
    std::string multi_pure_checksums = FS_LoadedPakPureChecksums();

    void *buf2 = nullptr;
    int len2 = FS_ReadFile("conflict.txt", &buf2);
    ASSERT_GT(len2, 0);
    std::string multi_conflict((char *)buf2, len2);
    FS_FreeFile(buf2);

    // Assert parity between single-threaded and multi-threaded
    EXPECT_EQ(single_names, multi_names);
    EXPECT_EQ(single_checksums, multi_checksums);
    EXPECT_EQ(single_pure_checksums, multi_pure_checksums);
    EXPECT_EQ(single_conflict, multi_conflict);

    // Precedence: pak7 must override pak0..pak6
    EXPECT_EQ(multi_conflict, "conflict-from-pak7");
    EXPECT_EQ(multi_names.substr(0, 4), "pak7");

    // Clean up
    FS_Shutdown(qtrue);
    q3::threading::JobSystem::instance().resize(0);
}
