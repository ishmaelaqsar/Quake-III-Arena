#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <numeric>
#include <thread>
#include <vector>

#include "../code/sys/threading/job_system.hpp"
#include "../code/sys/threading/main_thread_queue.hpp"
#include "../code/sys/threading/thread_affinity.hpp"

class JobsFixture : public ::testing::Test {
protected:
    void SetUp() override {
        q3::threading::mark_main_thread();
        q3::threading::MainThreadQueue::instance().reset_for_testing();
        auto &jobs = q3::threading::JobSystem::instance();
        jobs.init(4);
    }

    void TearDown() override {
        q3::threading::JobSystem::instance().shutdown();
        q3::threading::MainThreadQueue::instance().drain_all();
    }
};

TEST_F(JobsFixture, CompleteExactlyOnce) {
    constexpr int kNumJobs = 100;
    std::atomic<int> counter{0};
    std::vector<q3::threading::JobHandle> handles;
    handles.reserve(kNumJobs);

    for (int i = 0; i < kNumJobs; ++i) {
        handles.push_back(q3::threading::JobSystem::instance().dispatch(
            [&counter]() {
                counter.fetch_add(1, std::memory_order_relaxed);
            }));
    }

    for (auto &h : handles) {
        h.wait();
        EXPECT_TRUE(h.is_done());
    }

    EXPECT_EQ(counter.load(), kNumJobs);
}

TEST_F(JobsFixture, CompletionRunsOnDrainingThread) {
    std::thread::id completion_thread_id{};
    std::thread::id expected_thread_id = std::this_thread::get_id();

    auto handle = q3::threading::JobSystem::instance().dispatch(
        []() {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        },
        [&completion_thread_id]() {
            completion_thread_id = std::this_thread::get_id();
        });

    handle.wait();
    q3::threading::MainThreadQueue::instance().drain_all();

    EXPECT_EQ(completion_thread_id, expected_thread_id);
}

TEST_F(JobsFixture, CancelBeforeStartSkipsBody) {
    auto token = std::make_shared<q3::threading::CancelToken>();
    token->cancel();

    std::atomic<bool> executed{false};
    auto handle = q3::threading::JobSystem::instance().dispatch(
        q3::threading::Priority::Background,
        [&executed]() {
            executed.store(true, std::memory_order_relaxed);
        },
        nullptr,
        token);

    handle.wait();
    EXPECT_FALSE(executed.load());
    EXPECT_TRUE(handle.is_cancelled());
}

TEST_F(JobsFixture, CancelMidRunObservedViaToken) {
    auto token = std::make_shared<q3::threading::CancelToken>();
    std::atomic<bool> started{false};
    std::atomic<bool> observed_cancel{false};

    auto handle = q3::threading::JobSystem::instance().dispatch(
        q3::threading::Priority::Normal,
        [&started, &observed_cancel, token]() {
            started.store(true, std::memory_order_release);
            while (!token->is_cancelled()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            observed_cancel.store(true, std::memory_order_release);
        },
        nullptr,
        token);

    while (!started.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    token->cancel();
    handle.wait();

    EXPECT_TRUE(observed_cancel.load());
}

TEST_F(JobsFixture, ExceptionCapturedWorkerSurvives) {
    auto throwing_handle = q3::threading::JobSystem::instance().dispatch([]() {
        throw std::runtime_error("simulated job failure");
    });

    throwing_handle.wait();
    EXPECT_TRUE(throwing_handle.has_exception());

    bool caught_expected = false;
    try {
        std::rethrow_exception(throwing_handle.get_exception());
    } catch (const std::runtime_error &e) {
        if (std::string(e.what()) == "simulated job failure") {
            caught_expected = true;
        }
    }
    EXPECT_TRUE(caught_expected);

    // Ensure worker pool is still functional
    std::atomic<bool> subsequent_executed{false};
    auto subsequent_handle = q3::threading::JobSystem::instance().dispatch([&subsequent_executed]() {
        subsequent_executed.store(true, std::memory_order_relaxed);
    });

    subsequent_handle.wait();
    EXPECT_TRUE(subsequent_executed.load());
}

TEST_F(JobsFixture, ParallelForSumsOneMillion) {
    constexpr std::size_t kCount = 1000000;
    std::atomic<int64_t> sum{0};

    auto handle = q3::threading::JobSystem::instance().parallel_for(
        0, kCount, 10000, [&sum](std::size_t i) {
            sum.fetch_add(static_cast<int64_t>(i), std::memory_order_relaxed);
        });

    handle.wait();

    int64_t expected_sum = static_cast<int64_t>(kCount - 1) * static_cast<int64_t>(kCount) / 2;
    EXPECT_EQ(sum.load(), expected_sum);
}

TEST_F(JobsFixture, WaitOnMainDoesNotDeadlock) {
    std::atomic<bool> complete_ran{false};

    auto handle = q3::threading::JobSystem::instance().dispatch(
        []() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        },
        [&complete_ran]() {
            complete_ran.store(true, std::memory_order_relaxed);
        });

    // Wait on the main/draining thread; wait() will drain the queue
    handle.wait();

    EXPECT_TRUE(handle.is_done());
    EXPECT_TRUE(complete_ran.load());
}

TEST_F(JobsFixture, ShutdownJoinsWithin2s) {
    for (int i = 0; i < 50; ++i) {
        q3::threading::JobSystem::instance().dispatch([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        });
    }

    auto start = std::chrono::steady_clock::now();
    q3::threading::JobSystem::instance().shutdown();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start);

    EXPECT_LT(elapsed.count(), 2);
}

// Decision T-a: 0 means auto, and the two profiles differ. The client reserves the main and
// render threads and caps at eight; q3ded has no render thread, reserves one core, and caps at
// four. Assert the invariants rather than the arithmetic, so the case says what the decision
// promises instead of restating the formula.
TEST(Jobs, DedicatedAutoCountReservesOneCoreAndCapsAtFour) {
    const std::size_t client = q3::threading::JobSystem::auto_worker_count(false);
    const std::size_t dedicated = q3::threading::JobSystem::auto_worker_count(true);
    const unsigned int cores = std::thread::hardware_concurrency();

    EXPECT_GE(client, 1u);
    EXPECT_LE(client, 8u);
    EXPECT_GE(dedicated, 1u);
    EXPECT_LE(dedicated, 4u);

    // Below three cores both clamp to one and there is nothing left to reserve.
    if (cores >= 3) {
        EXPECT_LE(dedicated, cores - 1u) << "q3ded must leave the main thread a core";
        EXPECT_LE(client, cores - 2u) << "the client must leave the main and render threads room";
        EXPECT_GE(dedicated, client) << "q3ded reserves one core, the client reserves two";
    }
}

// The exception is written by the worker and read through the handle. Reading it before done is
// observed is a data race, which is what the accessors used to do; they now gate on done. This
// case exercises that read from another thread while the job completes, so the ThreadSanitizer
// leg is what makes it meaningful. Without a sanitizer it only proves the value arrives.
TEST_F(JobsFixture, ExceptionIsVisibleOnlyThroughDone) {
    std::atomic<bool> reader_saw_exception{false};

    auto handle = q3::threading::JobSystem::instance().dispatch(
        []() { throw std::runtime_error("job failed"); });

    std::thread reader([&handle, &reader_saw_exception]() {
        while (!handle.is_done()) {
            // Racy under the old accessors: has_exception() read the pointer with no ordering.
            (void)handle.has_exception();
            std::this_thread::yield();
        }
        reader_saw_exception.store(handle.has_exception(), std::memory_order_relaxed);
    });
    reader.join();

    EXPECT_TRUE(handle.is_done());
    EXPECT_TRUE(handle.has_exception());
    EXPECT_TRUE(reader_saw_exception.load(std::memory_order_relaxed));
}
