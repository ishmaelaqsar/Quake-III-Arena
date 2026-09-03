#pragma once

#include <thread>

namespace q3::threading {

void mark_main_thread() noexcept;
std::thread::id main_thread_id() noexcept;
bool is_main_thread() noexcept;

void set_current_thread_name(const char *name);
const char *get_current_thread_name() noexcept;

void reset_main_thread_for_testing() noexcept;

}  // namespace q3::threading
