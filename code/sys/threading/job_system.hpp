#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace q3::threading {

enum class Priority {
    High = 0,
    Normal = 1,
    Background = 2,
};

class CancelToken {
public:
    void cancel() noexcept {
        cancelled_.store(true, std::memory_order_release);
    }

    bool is_cancelled() const noexcept {
        return cancelled_.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> cancelled_{false};
};

struct JobState {
    std::atomic<bool> done{false};
    std::shared_ptr<CancelToken> cancel_token = std::make_shared<CancelToken>();
    std::exception_ptr exception{nullptr};
    std::mutex mutex;
    std::condition_variable cv;
};

class JobHandle {
public:
    JobHandle() = default;
    explicit JobHandle(std::shared_ptr<JobState> state) : state_(std::move(state)) {}

    void wait();

    bool is_done() const noexcept {
        return state_ ? state_->done.load(std::memory_order_acquire) : true;
    }

    void cancel() noexcept {
        if (state_ && state_->cancel_token) {
            state_->cancel_token->cancel();
        }
    }

    bool is_cancelled() const noexcept {
        return state_ && state_->cancel_token && state_->cancel_token->is_cancelled();
    }

    // Both accessors gate on done. The worker writes exception and then release-stores done,
    // so a reader that observes done is guaranteed to see the write; reading before then is a
    // data race, which is what these two used to be. Reporting "no exception" for a job that
    // has not finished is the honest answer, and it needs no lock.
    bool has_exception() const noexcept {
        return state_ && state_->done.load(std::memory_order_acquire) &&
               state_->exception != nullptr;
    }

    std::exception_ptr get_exception() const noexcept {
        if (!state_ || !state_->done.load(std::memory_order_acquire)) {
            return nullptr;
        }
        return state_->exception;
    }

    std::shared_ptr<CancelToken> cancel_token() const noexcept {
        return state_ ? state_->cancel_token : nullptr;
    }

private:
    std::shared_ptr<JobState> state_;
};

class JobSystem {
public:
    static JobSystem &instance() noexcept;

    // Start workers with worker_count (if 0, auto-detects based on hardware concurrency)
    void init(std::size_t worker_count = 0);

    // Resize the thread pool
    void resize(std::size_t worker_count);

    // Stop all workers and wait for existing jobs
    void shutdown();

    // Dispatch a job with priority, optional completion callback, and optional cancel token
    JobHandle dispatch(Priority priority,
                       std::function<void()> body,
                       std::function<void()> on_main_complete = nullptr,
                       std::shared_ptr<CancelToken> cancel_token = nullptr);

    // Convenience overloads
    JobHandle dispatch(std::function<void()> body,
                       std::function<void()> on_main_complete = nullptr) {
        return dispatch(Priority::Normal, std::move(body), std::move(on_main_complete));
    }

    // Parallel-for over [begin, end) with chunk grain size
    JobHandle parallel_for(std::size_t begin,
                           std::size_t end,
                           std::size_t grain,
                           std::function<void(std::size_t i)> fn);

    std::size_t worker_count() const noexcept;

    // Decision T-a: 0 means auto, clamp(cores - 2, 1, 8) on the client to leave the main and
    // render threads room, and clamp(cores - 1, 1, 4) on q3ded, which has no render thread and
    // does not benefit past four. DEDICATED is a run-time property since checklist 01, so the
    // caller decides which one applies; code/sys does not read engine cvars.
    static std::size_t auto_worker_count(bool dedicated = false) noexcept;

private:
    JobSystem() = default;
    ~JobSystem();
    JobSystem(const JobSystem &) = delete;
    JobSystem &operator=(const JobSystem &) = delete;

    struct JobItem {
        std::function<void()> body;
        std::function<void()> on_main_complete;
        std::shared_ptr<CancelToken> cancel_token;
        std::shared_ptr<JobState> state;
    };

    void worker_loop(std::size_t index);

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::thread> workers_;
    std::deque<std::shared_ptr<JobItem>> queues_[3];
    bool stopping_{false};
    bool initialized_{false};
};

} // namespace q3::threading
