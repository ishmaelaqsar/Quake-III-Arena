#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "../code/sys/threading/thread_affinity.hpp"
#include "../code/sys/threading/main_thread_queue.hpp"
#include "../code/sys/threading/threading_api.h"

TEST(Affinity, MainThreadIsMarked) {
    q3::threading::mark_main_thread();
    EXPECT_TRUE(q3::threading::is_main_thread());
    EXPECT_STREQ(q3::threading::get_current_thread_name(), "Main");

    std::thread worker([] {
        EXPECT_FALSE(q3::threading::is_main_thread());
    });
    worker.join();
}

TEST(Affinity, AssertFiresOffMain) {
    q3::threading::mark_main_thread();
    // On main thread, Sys_AssertMainThread must be a no-op
    Sys_AssertMainThread(__FILE__, __LINE__);

    // On a background thread, Sys_AssertMainThread must abort
    EXPECT_DEATH({
        std::thread worker([] {
            q3::threading::set_current_thread_name("WorkerThread");
            Sys_AssertMainThread(__FILE__, __LINE__);
        });
        worker.join();
    }, "FATAL: Thread affinity assertion failed");
}

// At file scope because MSVC requires a const local used inside a lambda to be captured
// explicitly when the lambda has no default capture mode, even where the use is a constant
// expression.
constexpr int kProducers = 8;
constexpr int kItemsPerProducer = 10000;

TEST(Queue, EightProducersTenThousandEach) {
    q3::threading::mark_main_thread();
    auto &queue = q3::threading::MainThreadQueue::instance();
    queue.reset_for_testing();

    std::vector<int> last_seen(kProducers, -1);
    bool fifo_order_ok = true;
    int total_executed = 0;

    std::atomic<bool> start_gate{false};
    std::vector<std::thread> producers;
    producers.reserve(kProducers);

    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([p, &queue, &start_gate, &last_seen, &fifo_order_ok, &total_executed]() {
            while (!start_gate.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < kItemsPerProducer; ++i) {
                queue.post([p, i, &last_seen, &fifo_order_ok, &total_executed]() {
                    if (last_seen[p] != i - 1) {
                        fifo_order_ok = false;
                    }
                    last_seen[p] = i;
                    ++total_executed;
                });
            }
        });
    }

    start_gate.store(true, std::memory_order_release);
    for (auto &t : producers) {
        t.join();
    }

    queue.drain_all();

    EXPECT_TRUE(fifo_order_ok);
    EXPECT_EQ(total_executed, kProducers * kItemsPerProducer);
    for (int p = 0; p < kProducers; ++p) {
        EXPECT_EQ(last_seen[p], kItemsPerProducer - 1);
    }
}

TEST(Queue, LossyDropsAbove1024) {
    q3::threading::mark_main_thread();
    auto &queue = q3::threading::MainThreadQueue::instance();
    queue.reset_for_testing();

    static std::atomic<int> s_lossy_executed{0};
    s_lossy_executed = 0;

    for (int i = 0; i < 2048; ++i) {
        q3::threading::FixedTask task;
        task.fn = [](void *) {
            s_lossy_executed.fetch_add(1, std::memory_order_relaxed);
        };
        queue.post_lossy(task);
    }

    EXPECT_EQ(queue.lossy_count(), 1024u);
    EXPECT_EQ(queue.dropped_count(), 1024u);

    queue.drain_all();

    EXPECT_EQ(s_lossy_executed.load(), 1024);
    EXPECT_EQ(queue.lossy_count(), 0u);
    EXPECT_EQ(queue.dropped_count(), 0u);
}

TEST(Queue, BudgetedDrainReturnsEarly) {
    q3::threading::mark_main_thread();
    auto &queue = q3::threading::MainThreadQueue::instance();
    queue.reset_for_testing();

    std::atomic<int> slow_count{0};
    for (int i = 0; i < 10000; ++i) {
        queue.post([&slow_count]() {
            auto start = std::chrono::steady_clock::now();
            while (std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count() < 50) {
            }
            slow_count.fetch_add(1, std::memory_order_relaxed);
        });
    }

    queue.drain(std::chrono::milliseconds(1));

    EXPECT_GT(slow_count.load(), 0);
    EXPECT_LT(slow_count.load(), 10000);

    queue.drain_all();
    EXPECT_EQ(slow_count.load(), 10000);
}

namespace {

static int s_cshim_val = 0;
void CShimCallback(void *ctx) {
    if (ctx) {
        s_cshim_val = *reinterpret_cast<int *>(ctx);
    }
}

}  // namespace

TEST(Queue, CShimRoundTrip) {
    q3::threading::mark_main_thread();
    auto &queue = q3::threading::MainThreadQueue::instance();
    queue.reset_for_testing();

    s_cshim_val = 0;
    int arg = 12345;
    Sys_PostToMainThread(CShimCallback, &arg);

    EXPECT_EQ(s_cshim_val, 0);
    Sys_MainThreadQueueDrain(0);
    EXPECT_EQ(s_cshim_val, 12345);
}
