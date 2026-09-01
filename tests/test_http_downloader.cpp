#include <gtest/gtest.h>
#include "../code/sys/net/http_downloader.hpp"
#include "../code/sys/sys_api.h"

TEST(HttpDownloaderTest, ParseAndDownloadMock) {
    q3::net::HttpDownloader downloader;
    EXPECT_EQ(downloader.status(), q3::net::DownloadStatus::Idle);

    // Initial state check
    EXPECT_TRUE(downloader.error_message().empty());
}

TEST(HttpDownloaderTest, PathTraversalSanitizer) {
    // Valid filenames
    EXPECT_TRUE(Sys_SanitizeDownloadFilename("maps/q3dm17.pk3"));
    EXPECT_TRUE(Sys_SanitizeDownloadFilename("pak6-custom.pk3"));

    // Path traversal attempts
    EXPECT_FALSE(Sys_SanitizeDownloadFilename("../autoexec.cfg"));
    EXPECT_FALSE(Sys_SanitizeDownloadFilename("..\\q3config.cfg"));
    EXPECT_FALSE(Sys_SanitizeDownloadFilename("/etc/passwd"));
    EXPECT_FALSE(Sys_SanitizeDownloadFilename("c:\\windows\\system32\\cmd.exe"));

    // Dangerous extensions
    EXPECT_FALSE(Sys_SanitizeDownloadFilename("malicious.so"));
    EXPECT_FALSE(Sys_SanitizeDownloadFilename("trojan.dll"));
    EXPECT_FALSE(Sys_SanitizeDownloadFilename("virus.exe"));
    EXPECT_FALSE(Sys_SanitizeDownloadFilename("script.sh"));

    // Config overwrites
    EXPECT_FALSE(Sys_SanitizeDownloadFilename("autoexec.cfg"));
    EXPECT_FALSE(Sys_SanitizeDownloadFilename("q3config.cfg"));
    EXPECT_FALSE(Sys_SanitizeDownloadFilename("default.cfg"));
}
