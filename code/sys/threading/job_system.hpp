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

    bool has_exception() const noexcept {
        return state_ && (state_->exception != nullptr);
    }

    std::exception_ptr get_exception() const noexcept {
        return state_ ? state_->exception : nullptr;
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
    static std::size_t auto_worker_count() noexcept;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::thread> workers_;
    std::deque<std::shared_ptr<JobItem>> queues_[3];
    bool stopping_{false};
    bool initialized_{false};
};

} // namespace q3::threading
