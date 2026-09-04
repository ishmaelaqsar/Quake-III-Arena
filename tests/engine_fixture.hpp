#pragma once

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "q_shared.h"
#include "qcommon.h"

extern std::string g_sysPrintBuffer;

namespace q3::test {

class SysErrorException : public std::runtime_error {
public:
    explicit SysErrorException(const std::string &what) : std::runtime_error(what) {}
};

class TempDir {
public:
    TempDir() {
        static std::atomic<uint64_t> s_counter{0};
#if defined(_WIN32)
        int pid = _getpid();
#else
        int pid = getpid();
#endif
        dir_ = std::filesystem::temp_directory_path() /
               ("q3t-" + std::to_string(pid) + "-" + std::to_string(++s_counter));
        std::filesystem::create_directories(dir_);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;

    const std::filesystem::path &path() const noexcept { return dir_; }
    std::string string() const { return dir_.string(); }

    void write(const std::filesystem::path &relative, std::string_view bytes) {
        auto full_path = dir_ / relative;
        std::filesystem::create_directories(full_path.parent_path());
        std::ofstream out(full_path, std::ios::binary);
        out.write(bytes.data(), bytes.size());
    }

private:
    std::filesystem::path dir_;
};

inline void EnsureEngineInitialised() {
    static bool initialised = false;
    if (initialised) {
        return;
    }
    initialised = true;

    Com_InitSmallZoneMemory();
    Cvar_Init();
    Cmd_Init();
    Com_InitZoneMemory();
    Com_InitHunkMemory();
}

class EngineFixture : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        EnsureEngineInitialised();
    }

    void SetUp() override {
        g_sysPrintBuffer.clear();
    }
};

class FsFixture : public EngineFixture {
protected:
    void SetUp() override {
        EngineFixture::SetUp();
        temp_dir_ = std::make_unique<TempDir>();
        Cvar_Set("fs_basepath", temp_dir_->string().c_str());
        Cvar_Set("fs_homepath", temp_dir_->string().c_str());
        Cvar_Set("fs_cdpath", temp_dir_->string().c_str());
    }

    void TearDown() override {
        FS_Shutdown(qtrue);
        temp_dir_.reset();
    }

    TempDir &temp_dir() { return *temp_dir_; }

private:
    std::unique_ptr<TempDir> temp_dir_;
};

} // namespace q3::test
