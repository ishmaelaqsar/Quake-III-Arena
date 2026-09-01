#pragma once

#include <string>
#include <string_view>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>

namespace q3::net {

enum class DownloadStatus {
    Idle,
    Downloading,
    Completed,
    Failed
};

using ProgressCallback = std::function<void(std::size_t downloaded, std::size_t total)>;

class HttpDownloader {
public:
    HttpDownloader() = default;
    ~HttpDownloader() { cancel(); }

    bool start_download(std::string_view url, std::string_view output_path, ProgressCallback on_progress = nullptr);
    void cancel();

    DownloadStatus status() const noexcept { return status_.load(); }
    std::string_view error_message() const noexcept { return error_; }

private:
    void download_thread(std::string url, std::string output_path, ProgressCallback on_progress);

    std::atomic<DownloadStatus> status_{DownloadStatus::Idle};
    std::atomic<bool> cancel_requested_{false};
    std::string error_;
    std::thread worker_;
};

} // namespace q3::net
