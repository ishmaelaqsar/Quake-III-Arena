# Fast Async HTTP Asset Downloader

## Overview
`q3::net::HttpDownloader` in `code/sys/net/` provides asynchronous HTTP/HTTPS file downloading for map `.pk3` files and game assets.

## Advantages over Legacy UDP Downloads
- **High Throughput**: Utilizes HTTP TCP streams for maxed-out bandwidth.
- **Async Threading**: Downloads execute in background threads without blocking client frame rates.
- **Progress Tracking**: Progress callbacks report downloaded byte count and `Content-Length`.

## Usage Example
```cpp
q3::net::HttpDownloader downloader;
downloader.start_download(
    "http://mirror.example.com/maps/custom_map.pk3",
    "baseq3/custom_map.pk3",
    [](std::size_t downloaded, std::size_t total) {
        LOG_INFO("Download progress: ", downloaded, " / ", total, " bytes");
    }
);
```
