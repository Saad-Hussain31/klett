/// @file scheduler_test.cpp
/// @brief Tests for Scheduler (delayed and periodic task scheduling).

#include <cassert>
#include <iostream>
#include <cstdlib>

#define TEST(name) static void name()
#define RUN_TEST(name)                               \
    do                                               \
    {                                                \
        std::cout << "  " #name "..." << std::flush; \
        name();                                      \
        std::cout << " PASSED" << std::endl;         \
    } while (0)
#define ASSERT_TRUE(x)                                                                              \
    do                                                                                              \
    {                                                                                               \
        if (!(x))                                                                                   \
        {                                                                                           \
            std::cerr << "\n    FAILED: " #x << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::abort();                                                                           \
        }                                                                                           \
    } while (0)
#define ASSERT_EQ(a, b)                                                                                       \
    do                                                                                                        \
    {                                                                                                         \
        if ((a) != (b))                                                                                       \
        {                                                                                                     \
            std::cerr << "\n    FAILED: " #a " != " #b << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::abort();                                                                                     \
        }                                                                                                     \
    } while (0)

#include <Concurrency.h>

#include <atomic>
#include <chrono>
#include <thread>

// ---------------------------------------------------------------------------
// schedule_once fires after delay
// ---------------------------------------------------------------------------
TEST(test_schedule_once)
{
    conc::Scheduler sched;
    sched.start();

    std::atomic<bool> fired{false};
    auto t0 = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point fire_time;

    sched.schedule_once(std::chrono::milliseconds(50), [&]()
                        {
        fire_time = std::chrono::steady_clock::now();
        fired.store(true, std::memory_order_release); });

    // Wait for the task to fire (with generous timeout).
    for (int i = 0; i < 200 && !fired.load(std::memory_order_acquire); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    ASSERT_TRUE(fired.load());

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(fire_time - t0);
    // Should have fired after at least ~40ms (some tolerance).
    ASSERT_TRUE(elapsed.count() >= 30);

    sched.shutdown();
}

// ---------------------------------------------------------------------------
// schedule_once with zero delay fires almost immediately
// ---------------------------------------------------------------------------
TEST(test_schedule_once_zero_delay)
{
    conc::Scheduler sched;
    sched.start();

    std::atomic<bool> fired{false};

    sched.schedule_once(std::chrono::milliseconds(0), [&]()
                        { fired.store(true, std::memory_order_release); });

    for (int i = 0; i < 100 && !fired.load(std::memory_order_acquire); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    ASSERT_TRUE(fired.load());

    sched.shutdown();
}

// ---------------------------------------------------------------------------
// schedule_periodic fires multiple times
// ---------------------------------------------------------------------------
TEST(test_schedule_periodic)
{
    conc::Scheduler sched;
    sched.start();

    std::atomic<int> count{0};

    sched.schedule_periodic(
        std::chrono::milliseconds(10), // initial delay
        std::chrono::milliseconds(20), // interval
        [&]()
        {
            count.fetch_add(1, std::memory_order_relaxed);
        });

    // Wait for several firings.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    sched.shutdown();

    int final_count = count.load(std::memory_order_acquire);
    // With 10ms initial + 20ms interval over 200ms, expect at least 5 firings.
    // Being conservative to avoid flakiness.
    ASSERT_TRUE(final_count >= 3);
}

// ---------------------------------------------------------------------------
// Multiple schedule_once tasks
// ---------------------------------------------------------------------------
TEST(test_multiple_once_tasks)
{
    conc::Scheduler sched;
    sched.start();

    std::atomic<int> sum{0};

    sched.schedule_once(std::chrono::milliseconds(10), [&]()
                        { sum.fetch_add(1, std::memory_order_relaxed); });
    sched.schedule_once(std::chrono::milliseconds(20), [&]()
                        { sum.fetch_add(10, std::memory_order_relaxed); });
    sched.schedule_once(std::chrono::milliseconds(30), [&]()
                        { sum.fetch_add(100, std::memory_order_relaxed); });

    // Wait for all tasks.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    sched.shutdown();

    ASSERT_EQ(sum.load(), 111);
}

// ---------------------------------------------------------------------------
// shutdown stops periodic execution
// ---------------------------------------------------------------------------
TEST(test_shutdown_stops_execution)
{
    conc::Scheduler sched;
    sched.start();

    std::atomic<int> count{0};

    sched.schedule_periodic(
        std::chrono::milliseconds(5),
        std::chrono::milliseconds(10),
        [&]()
        {
            count.fetch_add(1, std::memory_order_relaxed);
        });

    // Let it fire a few times.
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    sched.shutdown();

    int count_at_shutdown = count.load(std::memory_order_acquire);
    ASSERT_TRUE(count_at_shutdown > 0);

    // After shutdown, no more firings should happen.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    int count_after = count.load(std::memory_order_acquire);
    ASSERT_EQ(count_at_shutdown, count_after);
}

// ---------------------------------------------------------------------------
// pending_count tracks scheduled tasks
// ---------------------------------------------------------------------------
TEST(test_pending_count)
{
    conc::Scheduler sched;
    // Don't start yet -- tasks should accumulate in the queue.

    sched.schedule_once(std::chrono::milliseconds(100), []() {});
    sched.schedule_once(std::chrono::milliseconds(200), []() {});

    ASSERT_EQ(sched.pending_count(), 2u);

    sched.start();

    // Wait for tasks to fire.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    sched.shutdown();
}

// ---------------------------------------------------------------------------
// schedule_once returns unique IDs
// ---------------------------------------------------------------------------
TEST(test_unique_ids)
{
    conc::Scheduler sched;

    auto id1 = sched.schedule_once(std::chrono::milliseconds(100), []() {});
    auto id2 = sched.schedule_once(std::chrono::milliseconds(100), []() {});
    auto id3 = sched.schedule_periodic(std::chrono::milliseconds(100),
                                       std::chrono::milliseconds(100), []() {});

    ASSERT_TRUE(id1 != id2);
    ASSERT_TRUE(id2 != id3);
    ASSERT_TRUE(id1 != id3);

    // Clean up -- start and let tasks time out or just shutdown.
    sched.start();
    sched.shutdown();
}

// ---------------------------------------------------------------------------
// Destructor calls shutdown
// ---------------------------------------------------------------------------
TEST(test_destructor_shutdown)
{
    std::atomic<int> count{0};
    {
        conc::Scheduler sched;
        sched.start();
        sched.schedule_periodic(
            std::chrono::milliseconds(5),
            std::chrono::milliseconds(10),
            [&]()
            {
                count.fetch_add(1, std::memory_order_relaxed);
            });
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        // Destructor should call shutdown.
    }

    int count_at_destruction = count.load(std::memory_order_acquire);
    ASSERT_TRUE(count_at_destruction > 0);

    // Verify no more increments.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_EQ(count.load(), count_at_destruction);
}

// ---------------------------------------------------------------------------
// Scheduling from multiple threads
// ---------------------------------------------------------------------------
TEST(test_concurrent_scheduling)
{
    conc::Scheduler sched;
    sched.start();

    std::atomic<int> total{0};
    constexpr int THREADS = 4;
    constexpr int TASKS_PER_THREAD = 25;

    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; ++t)
    {
        threads.emplace_back([&]()
                             {
            for (int i = 0; i < TASKS_PER_THREAD; ++i)
            {
                sched.schedule_once(std::chrono::milliseconds(5 + i), [&]()
                {
                    total.fetch_add(1, std::memory_order_relaxed);
                });
            } });
    }

    for (auto &t : threads)
        t.join();

    // Wait for all tasks to fire.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    sched.shutdown();

    ASSERT_EQ(total.load(), THREADS * TASKS_PER_THREAD);
}

// ===========================================================================
int main()
{
    std::cout << "scheduler_test" << std::endl;

    RUN_TEST(test_schedule_once);
    RUN_TEST(test_schedule_once_zero_delay);
    RUN_TEST(test_schedule_periodic);
    RUN_TEST(test_multiple_once_tasks);
    RUN_TEST(test_shutdown_stops_execution);
    RUN_TEST(test_pending_count);
    RUN_TEST(test_unique_ids);
    RUN_TEST(test_destructor_shutdown);
    RUN_TEST(test_concurrent_scheduling);

    std::cout << "All scheduler tests passed." << std::endl;
    return 0;
}
