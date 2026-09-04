#include "job_system.hpp"
#include "main_thread_queue.hpp"
#include "thread_affinity.hpp"
#include "threading_api.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>

namespace q3::threading {

void JobHandle::wait() {
    if (!state_ || is_done()) {
        return;
    }
    if (q3::threading::is_main_thread()) {
        while (!is_done()) {
            q3::threading::MainThreadQueue::instance().drain(std::chrono::milliseconds(1));
            std::this_thread::yield();
        }
    } else {
        std::unique_lock<std::mutex> lock(state_->mutex);
        state_->cv.wait(lock, [this]() { return is_done(); });
    }
}

JobSystem &JobSystem::instance() noexcept {
    static JobSystem s_instance;
    return s_instance;
}

JobSystem::~JobSystem() {
    shutdown();
}

std::size_t JobSystem::auto_worker_count() noexcept {
    unsigned int hw = std::thread::hardware_concurrency();
    if (hw == 0) {
        hw = 4;
    }
    // Default: reserve main and render threads (clamp to 1..8)
    int count = static_cast<int>(hw) - 2;
    return static_cast<std::size_t>(std::clamp(count, 1, 8));
}

void JobSystem::init(std::size_t worker_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        return;
    }

    if (worker_count == 0) {
        worker_count = auto_worker_count();
    }

    stopping_ = false;
    initialized_ = true;
    workers_.reserve(worker_count);
    for (std::size_t i = 0; i < worker_count; ++i) {
        workers_.emplace_back([this, i]() {
            worker_loop(i);
        });
    }
}

void JobSystem::shutdown() {
    std::vector<std::thread> workers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return;
        }
        stopping_ = true;
        initialized_ = false;
        cv_.notify_all();
        workers.swap(workers_);
    }

    for (auto &w : workers) {
        if (w.joinable()) {
            w.join();
        }
    }

    // Clear remaining queues
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &q : queues_) {
        q.clear();
    }
}

void JobSystem::resize(std::size_t worker_count) {
    shutdown();
    init(worker_count);
}

std::size_t JobSystem::worker_count() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return workers_.size();
}

JobHandle JobSystem::dispatch(Priority priority,
                             std::function<void()> body,
                             std::function<void()> on_main_complete,
                             std::shared_ptr<CancelToken> cancel_token) {
    if (!body) {
        return JobHandle();
    }

    auto state = std::make_shared<JobState>();
    if (cancel_token) {
        state->cancel_token = cancel_token;
    }

    auto item = std::make_shared<JobItem>();
    item->body = std::move(body);
    item->on_main_complete = std::move(on_main_complete);
    item->cancel_token = state->cancel_token;
    item->state = state;

    int prio_idx = static_cast<int>(priority);
    if (prio_idx < 0 || prio_idx > 2) {
        prio_idx = 1;
    }

    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!initialized_) {
            lock.unlock();
            init(0);
            lock.lock();
        }
        queues_[prio_idx].push_back(std::move(item));
        cv_.notify_one();
    }

    return JobHandle(state);
}

JobHandle JobSystem::parallel_for(std::size_t begin,
                                  std::size_t end,
                                  std::size_t grain,
                                  std::function<void(std::size_t i)> fn) {
    if (begin >= end || !fn) {
        auto empty_state = std::make_shared<JobState>();
        empty_state->done.store(true, std::memory_order_relaxed);
        return JobHandle(empty_state);
    }

    if (grain == 0) {
        grain = 1;
    }

    std::size_t total = end - begin;
    std::size_t num_chunks = (total + grain - 1) / grain;

    auto combined_state = std::make_shared<JobState>();
    auto remaining = std::make_shared<std::atomic<std::size_t>>(num_chunks);

    for (std::size_t c = 0; c < num_chunks; ++c) {
        std::size_t chunk_start = begin + c * grain;
        std::size_t chunk_end = std::min(chunk_start + grain, end);

        dispatch(Priority::Normal, [chunk_start, chunk_end, fn, remaining, combined_state]() {
            if (!combined_state->cancel_token->is_cancelled()) {
                for (std::size_t i = chunk_start; i < chunk_end; ++i) {
                    fn(i);
                }
            }
            if (remaining->fetch_sub(1, std::memory_order_acq_rel) == 1) {
                combined_state->done.store(true, std::memory_order_release);
                std::lock_guard<std::mutex> lock(combined_state->mutex);
                combined_state->cv.notify_all();
            }
        }, nullptr, combined_state->cancel_token);
    }

    return JobHandle(combined_state);
}

void JobSystem::worker_loop(std::size_t index) {
    std::string name = "q3-job-" + std::to_string(index);
    q3::threading::set_current_thread_name(name.c_str());

    while (true) {
        std::shared_ptr<JobItem> item;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() {
                return stopping_ || !queues_[0].empty() || !queues_[1].empty() || !queues_[2].empty();
            });

            if (stopping_ && queues_[0].empty() && queues_[1].empty() && queues_[2].empty()) {
                break;
            }

            for (int p = 0; p < 3; ++p) {
                if (!queues_[p].empty()) {
                    item = queues_[p].front();
                    queues_[p].pop_front();
                    break;
                }
            }
        }

        if (item) {
            if (!item->cancel_token || !item->cancel_token->is_cancelled()) {
                try {
                    item->body();
                } catch (...) {
                    item->state->exception = std::current_exception();
                }
            }

            if (item->on_main_complete) {
                q3::threading::MainThreadQueue::instance().post(item->on_main_complete);
            }

            item->state->done.store(true, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lock(item->state->mutex);
                item->state->cv.notify_all();
            }
        }
    }
}

} // namespace q3::threading

namespace {

std::mutex s_c_handles_mutex;
std::unordered_map<int, q3::threading::JobHandle> s_c_handles;
std::atomic<int> s_c_next_handle{1};

} // namespace

extern "C" {

int Sys_JobSubmit(void (*fn)(void *), void *ctx, void (*done)(void *), int priority) {
    if (!fn) {
        return 0;
    }

    int id = s_c_next_handle.fetch_add(1);
    auto prio = static_cast<q3::threading::Priority>(std::clamp(priority, 0, 2));

    auto handle = q3::threading::JobSystem::instance().dispatch(
        prio,
        [fn, ctx]() {
            fn(ctx);
        },
        [done, ctx, id]() {
            if (done) {
                done(ctx);
            }
            std::lock_guard<std::mutex> lock(s_c_handles_mutex);
            s_c_handles.erase(id);
        });

    {
        std::lock_guard<std::mutex> lock(s_c_handles_mutex);
        s_c_handles[id] = handle;
    }

    return id;
}

void Sys_JobWait(int handle) {
    q3::threading::JobHandle h;
    {
        std::lock_guard<std::mutex> lock(s_c_handles_mutex);
        auto it = s_c_handles.find(handle);
        if (it != s_c_handles.end()) {
            h = it->second;
        }
    }
    h.wait();
}

void Sys_JobCancel(int handle) {
    std::lock_guard<std::mutex> lock(s_c_handles_mutex);
    auto it = s_c_handles.find(handle);
    if (it != s_c_handles.end()) {
        it->second.cancel();
    }
}

} // extern "C"
