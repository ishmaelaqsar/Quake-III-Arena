#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

namespace q3::threading {

struct FixedTask {
    void (*fn)(void *payload) = nullptr;
    alignas(std::max_align_t) char payload[48] = {0};
};

static_assert(sizeof(FixedTask) <= 64, "FixedTask must be at most 64 bytes");

class MainThreadQueue {
public:
    static constexpr std::size_t kLossyCapacity = 1024;
    static constexpr std::size_t kReliableHighWater = 4096;

    static MainThreadQueue &instance() noexcept;

    // Reliable lane: unbounded, mutex-protected, high-water warning at 4096.
    void post(std::function<void()> task);

    // Lossy lane: 1024-slot ring buffer. Never blocks caller, drops on overflow.
    bool post_lossy(const FixedTask &task);

    // Drains tasks on the main thread within the specified time budget.
    void drain(std::chrono::milliseconds budget);

    // Drains all queued tasks until both lanes are empty.
    void drain_all();

    // Test and diagnostic accessors
    std::size_t dropped_count() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

    std::size_t lossy_count() const noexcept;
    std::size_t reliable_count() const noexcept;

    void reset_for_testing();

private:
    MainThreadQueue() = default;
    ~MainThreadQueue() = default;
    MainThreadQueue(const MainThreadQueue &) = delete;
    MainThreadQueue &operator=(const MainThreadQueue &) = delete;

    // Reliable lane state
    mutable std::mutex reliable_mutex_;
    std::vector<std::function<void()>> reliable_tasks_;
    std::deque<std::function<void()>> pending_reliable_;
    bool high_water_warned_ = false;

    // Lossy lane state
    mutable std::mutex lossy_mutex_;
    FixedTask lossy_ring_[kLossyCapacity];
    std::size_t lossy_head_ = 0;
    std::size_t lossy_tail_ = 0;
    std::size_t lossy_count_ = 0;
    std::atomic<std::size_t> dropped_{0};
};

}  // namespace q3::threading
