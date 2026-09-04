#include "main_thread_queue.hpp"
#include "threading_api.h"

#include <chrono>
#include <cstdio>
#include <iostream>

namespace q3::threading {

MainThreadQueue &MainThreadQueue::instance() noexcept {
    static MainThreadQueue s_instance;
    return s_instance;
}

void MainThreadQueue::post(std::function<void()> task) {
    if (!task) {
        return;
    }
    std::lock_guard<std::mutex> lock(reliable_mutex_);
    reliable_tasks_.push_back(std::move(task));
    if (reliable_tasks_.size() >= kReliableHighWater && !high_water_warned_) {
        high_water_warned_ = true;
        std::fprintf(stderr, "[threading] warning: reliable queue high-water mark reached (4096 tasks)\n");
    }
}

bool MainThreadQueue::post_lossy(const FixedTask &task) {
    if (!task.fn) {
        return false;
    }
    std::lock_guard<std::mutex> lock(lossy_mutex_);
    if (lossy_count_ >= kLossyCapacity) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    lossy_ring_[lossy_tail_] = task;
    lossy_tail_ = (lossy_tail_ + 1) % kLossyCapacity;
    ++lossy_count_;
    return true;
}

void MainThreadQueue::drain(std::chrono::milliseconds budget) {
    const std::size_t drops = dropped_.exchange(0, std::memory_order_relaxed);
    if (drops > 0) {
        std::fprintf(stderr, "[threading] %zu messages dropped\n", drops);
    }

    const bool has_budget = (budget.count() > 0 && budget != std::chrono::milliseconds::max());
    const auto start_time = std::chrono::steady_clock::now();

    // 1. Drain lossy lane
    while (true) {
        FixedTask task{};
        {
            std::lock_guard<std::mutex> lock(lossy_mutex_);
            if (lossy_count_ == 0) {
                break;
            }
            task = lossy_ring_[lossy_head_];
            lossy_ring_[lossy_head_] = FixedTask{};
            lossy_head_ = (lossy_head_ + 1) % kLossyCapacity;
            --lossy_count_;
        }
        if (task.fn != nullptr) {
            task.fn(task.payload);
        }
        if (has_budget && (std::chrono::steady_clock::now() - start_time) >= budget) {
            return;
        }
    }

    // 2. Drain reliable lane
    while (true) {
        if (pending_reliable_.empty()) {
            std::vector<std::function<void()>> batch;
            {
                std::lock_guard<std::mutex> lock(reliable_mutex_);
                batch.swap(reliable_tasks_);
                if (reliable_tasks_.size() < kReliableHighWater) {
                    high_water_warned_ = false;
                }
            }
            if (batch.empty()) {
                break;
            }
            for (auto &t : batch) {
                pending_reliable_.push_back(std::move(t));
            }
        }

        auto task = std::move(pending_reliable_.front());
        pending_reliable_.pop_front();
        if (task) {
            task();
        }

        if (has_budget && (std::chrono::steady_clock::now() - start_time) >= budget) {
            return;
        }
    }
}

void MainThreadQueue::drain_all() {
    while (true) {
        drain(std::chrono::milliseconds::max());
        bool empty = false;
        {
            std::lock_guard<std::mutex> lock1(lossy_mutex_);
            std::lock_guard<std::mutex> lock2(reliable_mutex_);
            empty = (lossy_count_ == 0 && reliable_tasks_.empty() && pending_reliable_.empty());
        }
        if (empty) {
            break;
        }
    }
}

std::size_t MainThreadQueue::lossy_count() const noexcept {
    std::lock_guard<std::mutex> lock(lossy_mutex_);
    return lossy_count_;
}

std::size_t MainThreadQueue::reliable_count() const noexcept {
    std::lock_guard<std::mutex> lock(reliable_mutex_);
    return reliable_tasks_.size() + pending_reliable_.size();
}

void MainThreadQueue::reset_for_testing() {
    std::lock_guard<std::mutex> lock1(lossy_mutex_);
    std::lock_guard<std::mutex> lock2(reliable_mutex_);
    lossy_head_ = 0;
    lossy_tail_ = 0;
    lossy_count_ = 0;
    dropped_.store(0, std::memory_order_relaxed);
    reliable_tasks_.clear();
    pending_reliable_.clear();
    high_water_warned_ = false;
}

}  // namespace q3::threading

extern "C" {

void Sys_PostToMainThread(void (*fn)(void *), void *ctx) {
    if (fn == nullptr) {
        return;
    }
    q3::threading::MainThreadQueue::instance().post([fn, ctx]() {
        fn(ctx);
    });
}

void Sys_MainThreadQueueDrain(int max_ms) {
    if (max_ms <= 0) {
        q3::threading::MainThreadQueue::instance().drain_all();
    } else {
        q3::threading::MainThreadQueue::instance().drain(std::chrono::milliseconds(max_ms));
    }
}

}  // extern "C"
