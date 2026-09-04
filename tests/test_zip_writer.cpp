#include <gtest/gtest.h>

#include "engine_fixture.hpp"
#include "zip_writer.hpp"
#include "unzip.h"

TEST(ZipWriter, OutputOpensWithUnzip) {
    q3::test::EnsureEngineInitialised();
    q3::test::TempDir dir;
    std::string zip_content = q3::test::write_zip({
        {"test1.txt", "Hello World!"},
        {"scripts/test.shader", "shader1"}
    });

    dir.write("test.pk3", zip_content);
    std::string pk3_path = (dir.path() / "test.pk3").string();

    unzFile uf = unzOpen(pk3_path.c_str());
    ASSERT_NE(uf, nullptr);
    EXPECT_EQ(unzGoToFirstFile(uf), UNZ_OK);

    // Read first entry
    EXPECT_EQ(unzLocateFile(uf, "test1.txt", 2), UNZ_OK);
    EXPECT_EQ(unzOpenCurrentFile(uf), UNZ_OK);
    char buf[64] = {0};
    int bytes = unzReadCurrentFile(uf, buf, sizeof(buf) - 1);
    EXPECT_EQ(bytes, 12);
    buf[bytes] = '\0';
    EXPECT_STREQ(buf, "Hello World!");
    EXPECT_EQ(unzCloseCurrentFile(uf), UNZ_OK);

    // Read second entry
    EXPECT_EQ(unzLocateFile(uf, "scripts/test.shader", 2), UNZ_OK);
    EXPECT_EQ(unzOpenCurrentFile(uf), UNZ_OK);
    bytes = unzReadCurrentFile(uf, buf, sizeof(buf) - 1);
    EXPECT_EQ(bytes, 7);
    buf[bytes] = '\0';
    EXPECT_STREQ(buf, "shader1");
    EXPECT_EQ(unzCloseCurrentFile(uf), UNZ_OK);

    EXPECT_EQ(unzClose(uf), UNZ_OK);
}
