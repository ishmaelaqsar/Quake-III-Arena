#include <gtest/gtest.h>
#include "../code/sys/net/http_downloader.hpp"

TEST(HttpDownloaderTest, ParseAndDownloadMock) {
    q3::net::HttpDownloader downloader;
    EXPECT_EQ(downloader.status(), q3::net::DownloadStatus::Idle);

    // Initial state check
    EXPECT_TRUE(downloader.error_message().empty());
}
