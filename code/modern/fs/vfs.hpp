#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <filesystem>
#include <fstream>
#include <cstdint>

namespace q3::fs {

class ScopedFile {
public:
    explicit ScopedFile(std::filesystem::path path, std::ios::openmode mode = std::ios::in | std::ios::binary);
    ~ScopedFile();

    ScopedFile(const ScopedFile&) = delete;
    ScopedFile& operator=(const ScopedFile&) = delete;

    ScopedFile(ScopedFile&& other) noexcept;
    ScopedFile& operator=(ScopedFile&& other) noexcept;

    bool is_open() const noexcept { return stream_.is_open(); }
    std::size_t size() const;

    std::vector<uint8_t> read_all_bytes();
    std::string read_all_text();

    bool write_bytes(const uint8_t* data, std::size_t size);
    bool write_text(std::string_view text);

    std::fstream& stream() noexcept { return stream_; }

private:
    std::filesystem::path path_;
    std::fstream stream_;
};

class VirtualFileSystem {
public:
    static VirtualFileSystem& instance() noexcept {
        static VirtualFileSystem vfs;
        return vfs;
    }

    void mount_search_path(const std::filesystem::path& dir, bool prepend = false);
    void unmount_all();

    bool file_exists(std::string_view relative_path) const;
    std::optional<std::filesystem::path> resolve_path(std::string_view relative_path) const;

    std::optional<std::vector<uint8_t>> read_binary(std::string_view relative_path) const;
    std::optional<std::string> read_text(std::string_view relative_path) const;

    bool write_binary(std::string_view relative_path, const uint8_t* data, std::size_t size, std::string_view base_dir = "");
    bool write_text(std::string_view relative_path, std::string_view text, std::string_view base_dir = "");

    std::vector<std::string> list_files(std::string_view relative_dir, std::string_view extension = "") const;

private:
    VirtualFileSystem() = default;
    std::vector<std::filesystem::path> search_paths_;
};

} // namespace q3::fs
