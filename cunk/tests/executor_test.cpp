/// @file executor_test.cpp
/// @brief Tests for ThreadPoolExecutor.

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
#include <future>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Submit and execute a single task
// ---------------------------------------------------------------------------
TEST(test_submit_single_task)
{
    conc::ThreadPoolConfig cfg;
    cfg.num_threads = 2;
    conc::ThreadPoolExecutor pool(cfg);

    std::promise<int> p;
    auto f = p.get_future();

    pool.submit([&p]()
                { p.set_value(42); });

    auto status = f.wait_for(std::chrono::seconds(2));
    ASSERT_TRUE(status == std::future_status::ready);
    ASSERT_EQ(f.get(), 42);

    pool.shutdown();
}

// ---------------------------------------------------------------------------
// submit_async returning a future
// ---------------------------------------------------------------------------
TEST(test_submit_async)
{
    conc::ThreadPoolConfig cfg;
    cfg.num_threads = 2;
    conc::ThreadPoolExecutor pool(cfg);

    auto future = pool.submit_async([]() -> int
                                    { return 7 * 6; });

    auto status = future.wait_for(std::chrono::seconds(2));
    ASSERT_TRUE(status == std::future_status::ready);
    ASSERT_EQ(future.get(), 42);

    pool.shutdown();
}

// ---------------------------------------------------------------------------
// submit_async with string result
// ---------------------------------------------------------------------------
TEST(test_submit_async_string)
{
    conc::ThreadPoolConfig cfg;
    cfg.num_threads = 2;
    conc::ThreadPoolExecutor pool(cfg);

    auto future = pool.submit_async([]() -> std::string
                                    { return "hello concurrency"; });

    auto result = future.get();
    ASSERT_EQ(result, std::string("hello concurrency"));

    pool.shutdown();
}

// ---------------------------------------------------------------------------
// Worker count reflects configuration
// ---------------------------------------------------------------------------
TEST(test_worker_count)
{
    conc::ThreadPoolConfig cfg;
    cfg.num_threads = 4;
    conc::ThreadPoolExecutor pool(cfg);

    ASSERT_EQ(pool.worker_count(), 4u);

    pool.shutdown();
}

// ---------------------------------------------------------------------------
// Graceful shutdown: pending tasks are completed
// ---------------------------------------------------------------------------
TEST(test_graceful_shutdown)
{
    conc::ThreadPoolConfig cfg;
    cfg.num_threads = 2;
    conc::ThreadPoolExecutor pool(cfg);

    std::atomic<int> counter{0};
    constexpr int TASK_COUNT = 100;

    for (int i = 0; i < TASK_COUNT; ++i)
    {
        pool.submit([&counter]()
                    { counter.fetch_add(1, std::memory_order_relaxed); });
    }

    // shutdown() should block until all pending tasks complete.
    pool.shutdown();

    ASSERT_TRUE(pool.is_shutdown());
    ASSERT_EQ(counter.load(std::memory_order_acquire), TASK_COUNT);
}

// ---------------------------------------------------------------------------
// Tasks rejected after shutdown
// ---------------------------------------------------------------------------
TEST(test_reject_after_shutdown)
{
    conc::ThreadPoolConfig cfg;
    cfg.num_threads = 2;
    conc::ThreadPoolExecutor pool(cfg);

    pool.shutdown();
    ASSERT_TRUE(pool.is_shutdown());

    // Submit after shutdown -- the task should be silently rejected.
    std::atomic<bool> ran{false};
    pool.submit([&ran]()
                { ran.store(true); });

    // Give a small window for any erroneous execution.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(!ran.load());
}

// ---------------------------------------------------------------------------
// Multiple concurrent task submissions
// ---------------------------------------------------------------------------
TEST(test_concurrent_submissions)
{
    conc::ThreadPoolConfig cfg;
    cfg.num_threads = 4;
    cfg.worker_queue_size = 256;
    cfg.global_queue_size = 1024;
    conc::ThreadPoolExecutor pool(cfg);

    constexpr int SUBMITTERS = 8;
    constexpr int TASKS_PER_SUBMITTER = 500;
    std::atomic<int> total{0};

    std::vector<std::thread> submitters;
    for (int s = 0; s < SUBMITTERS; ++s)
    {
        submitters.emplace_back([&]()
                                {
            for (int i = 0; i < TASKS_PER_SUBMITTER; ++i)
            {
                pool.submit([&total]()
                {
                    total.fetch_add(1, std::memory_order_relaxed);
                });
            } });
    }

    for (auto &t : submitters)
        t.join();

    pool.shutdown();

    ASSERT_EQ(total.load(std::memory_order_acquire), SUBMITTERS * TASKS_PER_SUBMITTER);
}

// ---------------------------------------------------------------------------
// submit_async with multiple futures
// ---------------------------------------------------------------------------
TEST(test_multiple_futures)
{
    conc::ThreadPoolConfig cfg;
    cfg.num_threads = 4;
    conc::ThreadPoolExecutor pool(cfg);

    constexpr int N = 50;
    std::vector<std::future<int>> futures;
    futures.reserve(N);

    for (int i = 0; i < N; ++i)
    {
        futures.push_back(pool.submit_async([i]() -> int
                                            { return i * i; }));
    }

    for (int i = 0; i < N; ++i)
    {
        auto status = futures[i].wait_for(std::chrono::seconds(5));
        ASSERT_TRUE(status == std::future_status::ready);
        ASSERT_EQ(futures[i].get(), i * i);
    }

    pool.shutdown();
}

// ---------------------------------------------------------------------------
// Exception in task does not crash the pool
// ---------------------------------------------------------------------------
TEST(test_exception_safety)
{
    conc::ThreadPoolConfig cfg;
    cfg.num_threads = 2;
    conc::ThreadPoolExecutor pool(cfg);

    // Submit a task that throws.
    pool.submit([]()
                { throw std::runtime_error("boom"); });

    // Submit a task after to verify pool is still alive.
    auto future = pool.submit_async([]() -> int
                                    { return 123; });

    auto status = future.wait_for(std::chrono::seconds(2));
    ASSERT_TRUE(status == std::future_status::ready);
    ASSERT_EQ(future.get(), 123);

    pool.shutdown();
}

// ---------------------------------------------------------------------------
// Destructor triggers shutdown
// ---------------------------------------------------------------------------
TEST(test_destructor_shutdown)
{
    std::atomic<int> counter{0};
    {
        conc::ThreadPoolConfig cfg;
        cfg.num_threads = 2;
        conc::ThreadPoolExecutor pool(cfg);

        for (int i = 0; i < 50; ++i)
        {
            pool.submit([&counter]()
                        { counter.fetch_add(1, std::memory_order_relaxed); });
        }
        // Destructor should call shutdown and drain tasks.
    }
    ASSERT_EQ(counter.load(), 50);
}

// ===========================================================================
int main()
{
    std::cout << "executor_test" << std::endl;

    RUN_TEST(test_submit_single_task);
    RUN_TEST(test_submit_async);
    RUN_TEST(test_submit_async_string);
    RUN_TEST(test_worker_count);
    RUN_TEST(test_graceful_shutdown);
    RUN_TEST(test_reject_after_shutdown);
    RUN_TEST(test_concurrent_submissions);
    RUN_TEST(test_multiple_futures);
    RUN_TEST(test_exception_safety);
    RUN_TEST(test_destructor_shutdown);

    std::cout << "All executor tests passed." << std::endl;
    return 0;
}
