#include "vfs.hpp"
#include <algorithm>

namespace q3::fs {

ScopedFile::ScopedFile(std::filesystem::path path, std::ios::openmode mode)
    : path_(std::move(path)), stream_(path_, mode) {}

ScopedFile::~ScopedFile() {
    if (stream_.is_open()) {
        stream_.close();
    }
}

ScopedFile::ScopedFile(ScopedFile&& other) noexcept
    : path_(std::move(other.path_)), stream_(std::move(other.stream_)) {}

ScopedFile& ScopedFile::operator=(ScopedFile&& other) noexcept {
    if (this != &other) {
        if (stream_.is_open()) {
            stream_.close();
        }
        path_ = std::move(other.path_);
        stream_ = std::move(other.stream_);
    }
    return *this;
}

std::size_t ScopedFile::size() const {
    if (!std::filesystem::exists(path_)) return 0;
    return std::filesystem::file_size(path_);
}

std::vector<uint8_t> ScopedFile::read_all_bytes() {
    if (!stream_.is_open()) return {};
    stream_.seekg(0, std::ios::end);
    std::size_t len = stream_.tellg();
    stream_.seekg(0, std::ios::beg);

    std::vector<uint8_t> buf(len);
    stream_.read(reinterpret_cast<char*>(buf.data()), len);
    return buf;
}

std::string ScopedFile::read_all_text() {
    auto bytes = read_all_bytes();
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

bool ScopedFile::write_bytes(const uint8_t* data, std::size_t size) {
    if (!stream_.is_open()) return false;
    stream_.write(reinterpret_cast<const char*>(data), size);
    return stream_.good();
}

bool ScopedFile::write_text(std::string_view text) {
    return write_bytes(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

// ---------------------------------------------------------------------------
// VirtualFileSystem
// ---------------------------------------------------------------------------

void VirtualFileSystem::mount_search_path(const std::filesystem::path& dir, bool prepend) {
    if (prepend) {
        search_paths_.insert(search_paths_.begin(), dir);
    } else {
        search_paths_.push_back(dir);
    }
}

void VirtualFileSystem::unmount_all() {
    search_paths_.clear();
}

bool VirtualFileSystem::file_exists(std::string_view relative_path) const {
    return resolve_path(relative_path).has_value();
}

std::optional<std::filesystem::path> VirtualFileSystem::resolve_path(std::string_view relative_path) const {
    for (const auto& base : search_paths_) {
        auto candidate = base / relative_path;
        if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<uint8_t>> VirtualFileSystem::read_binary(std::string_view relative_path) const {
    auto resolved = resolve_path(relative_path);
    if (!resolved) return std::nullopt;

    ScopedFile file(*resolved, std::ios::in | std::ios::binary);
    if (!file.is_open()) return std::nullopt;
    return file.read_all_bytes();
}

std::optional<std::string> VirtualFileSystem::read_text(std::string_view relative_path) const {
    auto resolved = resolve_path(relative_path);
    if (!resolved) return std::nullopt;

    ScopedFile file(*resolved, std::ios::in);
    if (!file.is_open()) return std::nullopt;
    return file.read_all_text();
}

bool VirtualFileSystem::write_binary(std::string_view relative_path, const uint8_t* data, std::size_t size, std::string_view base_dir) {
    std::filesystem::path target_base;
    if (!base_dir.empty()) {
        target_base = base_dir;
    } else if (!search_paths_.empty()) {
        target_base = search_paths_.front();
    } else {
        target_base = std::filesystem::current_path();
    }

    auto full_path = target_base / relative_path;
    std::filesystem::create_directories(full_path.parent_path());

    ScopedFile file(full_path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;
    return file.write_bytes(data, size);
}

bool VirtualFileSystem::write_text(std::string_view relative_path, std::string_view text, std::string_view base_dir) {
    return write_binary(relative_path, reinterpret_cast<const uint8_t*>(text.data()), text.size(), base_dir);
}

std::vector<std::string> VirtualFileSystem::list_files(std::string_view relative_dir, std::string_view extension) const {
    std::vector<std::string> results;

    for (const auto& base : search_paths_) {
        auto dir = base / relative_dir;
        if (std::filesystem::exists(dir) && std::filesystem::is_directory(dir)) {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (entry.is_regular_file()) {
                    if (extension.empty() || entry.path().extension() == extension) {
                        results.push_back(entry.path().filename().string());
                    }
                }
            }
        }
    }

    // Remove duplicates
    std::sort(results.begin(), results.end());
    results.erase(std::unique(results.begin(), results.end()), results.end());
    return results;
}

} // namespace q3::fs
