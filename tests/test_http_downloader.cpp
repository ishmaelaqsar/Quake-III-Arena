#include <gtest/gtest.h>
#include "../code/sys/net/http_downloader.hpp"
#include "../code/sys/sys_api.h"

TEST(HttpDownloaderTest, ParseAndDownloadMock) {
    q3::net::HttpDownloader downloader;
    EXPECT_EQ(downloader.status(), q3::net::DownloadStatus::Idle);

    // Initial state check
    EXPECT_TRUE(downloader.error_message().empty());
}

// The filename policy now lives in tests/test_download_policy.cpp, which covers the shape
// rules and every bypass the previous blacklist allowed.
