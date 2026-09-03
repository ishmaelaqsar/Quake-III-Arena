#include "thread_affinity.hpp"
#include "threading_api.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

namespace q3::threading {

namespace {

std::atomic<bool> g_main_thread_marked{false};
std::thread::id g_main_thread_id{};
thread_local char g_thread_name[32] = {0};
thread_local bool g_thread_name_set = false;

}  // namespace

void mark_main_thread() noexcept {
    g_main_thread_id = std::this_thread::get_id();
    g_main_thread_marked.store(true, std::memory_order_release);
    set_current_thread_name("Main");
}

std::thread::id main_thread_id() noexcept {
    return g_main_thread_id;
}

bool is_main_thread() noexcept {
    if (!g_main_thread_marked.load(std::memory_order_acquire)) {
        return true;
    }
    return std::this_thread::get_id() == g_main_thread_id;
}

void set_current_thread_name(const char *name) {
    if (!name) {
        name = "unnamed";
    }
    std::strncpy(g_thread_name, name, sizeof(g_thread_name) - 1);
    g_thread_name[sizeof(g_thread_name) - 1] = '\0';
    g_thread_name_set = true;

#if defined(__linux__)
    char linux_name[16];
    std::strncpy(linux_name, name, sizeof(linux_name) - 1);
    linux_name[sizeof(linux_name) - 1] = '\0';
    pthread_setname_np(pthread_self(), linux_name);
#elif defined(__APPLE__)
    pthread_setname_np(name);
#elif defined(_WIN32)
    using SetThreadDescriptionFn = HRESULT(WINAPI *)(HANDLE, PCWSTR);
    static auto pfnSetThreadDescription = reinterpret_cast<SetThreadDescriptionFn>(
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetThreadDescription"));
    if (pfnSetThreadDescription != nullptr) {
        wchar_t wname[32];
        MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 32);
        wname[31] = L'\0';
        pfnSetThreadDescription(GetCurrentThread(), wname);
    }
#endif
}

const char *get_current_thread_name() noexcept {
    return g_thread_name_set ? g_thread_name : "unnamed";
}

void reset_main_thread_for_testing() noexcept {
    g_main_thread_marked.store(false, std::memory_order_release);
    g_main_thread_id = std::thread::id{};
    g_thread_name_set = false;
    g_thread_name[0] = '\0';
}

}  // namespace q3::threading

extern "C" void Sys_AssertMainThread(const char *file, int line) {
    if (q3::threading::is_main_thread()) {
        return;
    }
    const char *thread_name = q3::threading::get_current_thread_name();
    char msg[512];
    std::snprintf(msg, sizeof(msg),
        "\nFATAL: Thread affinity assertion failed in %s:%d\n"
        "Expected main thread, called from thread '%s'\n",
        file ? file : "unknown", line,
        thread_name ? thread_name : "unnamed");
#if defined(_WIN32)
    _write(2, msg, static_cast<unsigned int>(std::strlen(msg)));
#else
    ssize_t ret = write(2, msg, std::strlen(msg));
    (void)ret;
#endif
    std::abort();
}
