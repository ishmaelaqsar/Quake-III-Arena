#include "http_downloader.hpp"
#include "net_compat.h"
#include "../logger/logger.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdint>
#include <mutex>

#ifdef _WIN32
static std::once_flag s_wsa_flag;
static void init_wsa_downloader() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
}
#endif

namespace q3::net {

static void close_socket(socket_t s) {
    if (s != Q3_INVALID_SOCKET) {
#ifdef _WIN32
        shutdown(s, SD_BOTH);
#else
        shutdown(s, SHUT_RDWR);
#endif
        q3_closesocket(s);
    }
}

bool HttpDownloader::start_download(std::string_view url, std::string_view output_path, ProgressCallback on_progress) {
#ifdef _WIN32
    std::call_once(s_wsa_flag, init_wsa_downloader);
#endif
    if (status_.load() == DownloadStatus::Downloading) {
        LOG_WARN("HttpDownloader: Download already in progress");
        return false;
    }

    if (worker_.joinable()) {
        worker_.join();
    }

    cancel_requested_ = false;
    status_ = DownloadStatus::Downloading;
    error_.clear();

    worker_ = std::thread(&HttpDownloader::download_thread, this, std::string(url), std::string(output_path), on_progress);
    return true;
}

void HttpDownloader::cancel() {
    cancel_requested_ = true;
    if (worker_.joinable()) {
        worker_.join();
    }
}

void HttpDownloader::download_thread(std::string url, std::string output_path, ProgressCallback on_progress) {
    LOG_INFO("HttpDownloader: Starting async download of ", url, " -> ", output_path);

    // Parse URL (e.g. http://hostname:port/path)
    std::string host;
    std::string path = "/";
    int port = 80;

    std::size_t host_start = 0;
    if (url.find("http://") == 0) {
        host_start = 7;
    } else if (url.find("https://") == 0) {
        host_start = 8;
        port = 443;
    }

    std::size_t path_start = url.find('/', host_start);
    if (path_start != std::string::npos) {
        host = url.substr(host_start, path_start - host_start);
        path = url.substr(path_start);
    } else {
        host = url.substr(host_start);
    }

    std::size_t port_pos = host.find(':');
    if (port_pos != std::string::npos) {
        port = std::stoi(host.substr(port_pos + 1));
        host = host.substr(0, port_pos);
    }

    struct addrinfo hints{}, *res = nullptr;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0 || !res) {
        error_ = "Failed to resolve host " + host;
        LOG_ERROR("HttpDownloader: ", error_);
        status_ = DownloadStatus::Failed;
        return;
    }

    socket_t sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == Q3_INVALID_SOCKET) {
        freeaddrinfo(res);
        error_ = "Failed to create socket";
        LOG_ERROR("HttpDownloader: ", error_);
        status_ = DownloadStatus::Failed;
        return;
    }

#ifdef _WIN32
    DWORD timeout_ms = 5000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout_ms, sizeof(timeout_ms));
#else
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#endif

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        close_socket(sock);
        freeaddrinfo(res);
        error_ = "Failed to connect to " + host;
        LOG_ERROR("HttpDownloader: ", error_);
        status_ = DownloadStatus::Failed;
        return;
    }
    freeaddrinfo(res);

    std::ostringstream request;
    request << "GET " << path << " HTTP/1.1\r\n"
            << "Host: " << host << "\r\n"
            << "User-Agent: Quake3Modern/1.32\r\n"
            << "Connection: close\r\n\r\n";

    std::string req_str = request.str();
    send(sock, req_str.c_str(), req_str.size(), 0);

    std::ofstream out_file(output_path, std::ios::binary);
    if (!out_file.is_open()) {
        close_socket(sock);
        error_ = "Failed to open output file " + output_path;
        LOG_ERROR("HttpDownloader: ", error_);
        status_ = DownloadStatus::Failed;
        return;
    }

    char buffer[8192];
    bool headers_done = false;
    std::size_t total_bytes = 0;
    std::size_t downloaded_bytes = 0;
    std::string header_buffer;

    while (!cancel_requested_) {
        int bytes_read = recv(sock, buffer, sizeof(buffer), 0);
        if (bytes_read <= 0) break;

        if (!headers_done) {
            header_buffer.append(buffer, bytes_read);
            std::size_t header_end = header_buffer.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                headers_done = true;

                // Extract Content-Length
                std::size_t cl_pos = header_buffer.find("Content-Length: ");
                if (cl_pos != std::string::npos) {
                    std::size_t cl_end = header_buffer.find("\r\n", cl_pos);
                    total_bytes = std::stoull(header_buffer.substr(cl_pos + 16, cl_end - (cl_pos + 16)));
                }

                std::size_t body_start = header_end + 4;
                std::size_t body_bytes = header_buffer.size() - body_start;
                out_file.write(header_buffer.data() + body_start, body_bytes);
                downloaded_bytes += body_bytes;
            }
        } else {
            out_file.write(buffer, bytes_read);
            downloaded_bytes += bytes_read;
            if (on_progress) {
                on_progress(downloaded_bytes, total_bytes);
            }
        }
    }

    close_socket(sock);
    out_file.close();

    if (cancel_requested_) {
        LOG_WARN("HttpDownloader: Download cancelled for ", url);
        status_ = DownloadStatus::Failed;
    } else {
        LOG_INFO("HttpDownloader: Downloaded ", downloaded_bytes, " bytes to ", output_path);
        status_ = DownloadStatus::Completed;
    }
}

} // namespace q3::net
