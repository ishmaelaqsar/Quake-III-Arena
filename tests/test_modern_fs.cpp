#include <gtest/gtest.h>
#include "../code/modern/fs/vfs.hpp"
#include <filesystem>

using namespace q3::fs;

TEST(ModernFSTest, ScopedFileReadWrite) {
    auto temp_path = std::filesystem::temp_directory_path() / "q3_test_file.txt";

    {
        ScopedFile file(temp_path, std::ios::out | std::ios::trunc);
        ASSERT_TRUE(file.is_open());
        EXPECT_TRUE(file.write_text("Quake 3 Modernization with C++17"));
    }

    {
        ScopedFile file(temp_path, std::ios::in);
        ASSERT_TRUE(file.is_open());
        EXPECT_EQ(file.read_all_text(), "Quake 3 Modernization with C++17");
    }

    std::filesystem::remove(temp_path);
}

TEST(ModernFSTest, VirtualFileSystemMountAndResolve) {
    auto temp_dir = std::filesystem::temp_directory_path() / "q3_vfs_test";
    std::filesystem::create_directories(temp_dir / "scripts");

    auto& vfs = VirtualFileSystem::instance();
    vfs.unmount_all();
    vfs.mount_search_path(temp_dir);

    EXPECT_TRUE(vfs.write_text("scripts/arena.cfg", "map q3dm1\nfraglimit 20"));

    EXPECT_TRUE(vfs.file_exists("scripts/arena.cfg"));
    auto content = vfs.read_text("scripts/arena.cfg");
    ASSERT_TRUE(content.has_value());
    EXPECT_EQ(*content, "map q3dm1\nfraglimit 20");

    auto files = vfs.list_files("scripts", ".cfg");
    EXPECT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0], "arena.cfg");

    std::filesystem::remove_all(temp_dir);
}
