/// @file stress_test.cpp
/// @brief Stress tests for the concurrency framework.

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
#include <set>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Thread pool under high load: 100,000 tasks across many threads
// ---------------------------------------------------------------------------
TEST(test_threadpool_high_load)
{
    constexpr int NUM_TASKS = 100000;
    constexpr int NUM_SUBMITTERS = 8;
    constexpr int TASKS_PER_SUBMITTER = NUM_TASKS / NUM_SUBMITTERS;

    conc::ThreadPoolConfig cfg;
    cfg.num_threads = 8;
    cfg.worker_queue_size = 16384;
    cfg.global_queue_size = 65536;
    conc::ThreadPoolExecutor pool(cfg);

    std::atomic<int> completed{0};

    auto start = std::chrono::steady_clock::now();

    std::vector<std::thread> submitters;
    for (int s = 0; s < NUM_SUBMITTERS; ++s)
    {
        submitters.emplace_back([&]()
                                {
            for (int i = 0; i < TASKS_PER_SUBMITTER; ++i)
            {
                pool.submit([&completed]()
                {
                    // Simulate a tiny amount of work.
                    volatile int x = 0;
                    for (int j = 0; j < 10; ++j)
                        x += j;
                    (void)x;
                    completed.fetch_add(1, std::memory_order_relaxed);
                });
            } });
    }

    for (auto &t : submitters)
        t.join();

    pool.shutdown();

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    ASSERT_EQ(completed.load(std::memory_order_acquire), NUM_TASKS);

    std::cout << " (" << ms << "ms)";
}

// ---------------------------------------------------------------------------
// MPMC queue contention: 8 producers, 8 consumers
// ---------------------------------------------------------------------------
TEST(test_mpmc_contention)
{
    constexpr int NUM_PRODUCERS = 8;
    constexpr int NUM_CONSUMERS = 8;
    constexpr int ITEMS_PER_PRODUCER = 50000;
    constexpr int TOTAL = NUM_PRODUCERS * ITEMS_PER_PRODUCER;

    conc::MPMCQueue<int> q(4096);

    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::atomic<bool> done_producing{false};

    // Produce.
    std::vector<std::thread> producers;
    for (int p = 0; p < NUM_PRODUCERS; ++p)
    {
        producers.emplace_back([&, p]()
                               {
            for (int i = 0; i < ITEMS_PER_PRODUCER; ++i)
            {
                int val = p * ITEMS_PER_PRODUCER + i;
                while (!q.try_push(val))
                {
                    std::this_thread::yield();
                }
                produced.fetch_add(1, std::memory_order_relaxed);
            } });
    }

    // Consume.
    // Each consumer tracks values it popped to verify no duplicates.
    std::vector<std::vector<int>> per_consumer(NUM_CONSUMERS);
    std::vector<std::thread> consumers;
    for (int c = 0; c < NUM_CONSUMERS; ++c)
    {
        consumers.emplace_back([&, c]()
                               {
            while (true)
            {
                auto val = q.try_pop();
                if (val.has_value())
                {
                    per_consumer[c].push_back(*val);
                    consumed.fetch_add(1, std::memory_order_relaxed);
                }
                else
                {
                    if (done_producing.load(std::memory_order_acquire) &&
                        consumed.load(std::memory_order_acquire) >= TOTAL)
                    {
                        break;
                    }
                    std::this_thread::yield();
                }
            } });
    }

    for (auto &t : producers)
        t.join();
    done_producing.store(true, std::memory_order_release);

    for (auto &t : consumers)
        t.join();

    // Verify totals.
    ASSERT_EQ(consumed.load(), TOTAL);

    // Verify no duplicates -- merge all per-consumer vectors into a set.
    std::set<int> all;
    for (auto &v : per_consumer)
    {
        for (int val : v)
        {
            auto [_, inserted] = all.insert(val);
            ASSERT_TRUE(inserted); // no duplicate
        }
    }
    ASSERT_EQ(static_cast<int>(all.size()), TOTAL);
}

// ---------------------------------------------------------------------------
// Event loop burst traffic
// ---------------------------------------------------------------------------

struct StressPingEvent : conc::Event<StressPingEvent>
{
    int seq;
    explicit StressPingEvent(int s) : conc::Event<StressPingEvent>(), seq(s) {}
};

TEST(test_event_loop_burst)
{
    constexpr int NUM_EVENTS = 10000;
    constexpr int NUM_DISPATCHERS = 4;
    constexpr int EVENTS_PER_DISPATCHER = NUM_EVENTS / NUM_DISPATCHERS;

    conc::EventLoopConfig elcfg;
    elcfg.queue_size = 16384;
    conc::EventLoop loop(elcfg);

    std::atomic<int> handled_count{0};

    loop.on<StressPingEvent>([&](const StressPingEvent &)
                             { handled_count.fetch_add(1, std::memory_order_relaxed); });

    // Run the loop in a background thread.
    std::thread loop_thread([&loop]()
                            { loop.run(); });

    // Give the loop a moment to start.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // Burst-dispatch from multiple threads.
    std::vector<std::thread> dispatchers;
    for (int d = 0; d < NUM_DISPATCHERS; ++d)
    {
        dispatchers.emplace_back([&, d]()
                                 {
            for (int i = 0; i < EVENTS_PER_DISPATCHER; ++i)
            {
                loop.dispatch(conc::make_event<StressPingEvent>(d * EVENTS_PER_DISPATCHER + i));
            } });
    }

    for (auto &t : dispatchers)
        t.join();

    // Wait for all events to be processed.
    for (int i = 0; i < 500; ++i)
    {
        if (handled_count.load(std::memory_order_acquire) >= NUM_EVENTS)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    loop.stop();
    loop_thread.join();

    ASSERT_EQ(handled_count.load(), NUM_EVENTS);
}

// ---------------------------------------------------------------------------
// Metrics under concurrent updates
// ---------------------------------------------------------------------------
TEST(test_metrics_concurrent)
{
    constexpr int THREADS = 8;
    constexpr int OPS_PER_THREAD = 100000;

    // Counter stress test.
    conc::Counter counter;
    {
        std::vector<std::thread> threads;
        for (int t = 0; t < THREADS; ++t)
        {
            threads.emplace_back([&]()
                                 {
                for (int i = 0; i < OPS_PER_THREAD; ++i)
                {
                    counter.increment();
                } });
        }
        for (auto &t : threads)
            t.join();
    }
    ASSERT_EQ(counter.value(), static_cast<std::uint64_t>(THREADS) * OPS_PER_THREAD);

    // Gauge stress test (increment/decrement).
    conc::Gauge gauge;
    {
        std::vector<std::thread> threads;
        for (int t = 0; t < THREADS; ++t)
        {
            threads.emplace_back([&, t]()
                                 {
                for (int i = 0; i < OPS_PER_THREAD; ++i)
                {
                    if (t % 2 == 0)
                        gauge.increment();
                    else
                        gauge.decrement();
                } });
        }
        for (auto &t : threads)
            t.join();
    }
    // Equal threads incrementing and decrementing, so net should be 0.
    ASSERT_EQ(gauge.value(), 0);

    // LatencyTracker stress test.
    conc::LatencyTracker tracker;
    {
        std::vector<std::thread> threads;
        for (int t = 0; t < THREADS; ++t)
        {
            threads.emplace_back([&]()
                                 {
                for (int i = 0; i < OPS_PER_THREAD; ++i)
                {
                    tracker.record(std::chrono::nanoseconds(100 + (i % 50)));
                } });
        }
        for (auto &t : threads)
            t.join();
    }
    auto snap = tracker.snapshot();
    ASSERT_EQ(snap.count, static_cast<std::uint64_t>(THREADS) * OPS_PER_THREAD);
    ASSERT_TRUE(snap.min_ns >= 100);
    ASSERT_TRUE(snap.max_ns <= 150);
    ASSERT_TRUE(snap.avg_ns() >= 100.0);
    ASSERT_TRUE(snap.avg_ns() <= 150.0);
}

// ---------------------------------------------------------------------------
// MetricsRegistry under concurrent access
// ---------------------------------------------------------------------------
TEST(test_metrics_registry_concurrent)
{
    conc::MetricsRegistry registry;

    constexpr int THREADS = 4;
    constexpr int OPS_PER_THREAD = 10000;

    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; ++t)
    {
        threads.emplace_back([&, t]()
                             {
            // Each thread works on the same named counter.
            auto &c = registry.counter("ops");
            auto &g = registry.gauge("active");
            auto &l = registry.latency("task_time");

            for (int i = 0; i < OPS_PER_THREAD; ++i)
            {
                c.increment();
                g.increment();
                g.decrement();
                l.record(std::chrono::nanoseconds(50));
            } });
    }

    for (auto &t : threads)
        t.join();

    auto &c = registry.counter("ops");
    ASSERT_EQ(c.value(), static_cast<std::uint64_t>(THREADS) * OPS_PER_THREAD);

    auto &g = registry.gauge("active");
    ASSERT_EQ(g.value(), 0); // increments and decrements cancel out

    auto &l = registry.latency("task_time");
    auto snap = l.snapshot();
    ASSERT_EQ(snap.count, static_cast<std::uint64_t>(THREADS) * OPS_PER_THREAD);
}

// ---------------------------------------------------------------------------
// LatencyTracker ScopeTimer
// ---------------------------------------------------------------------------
TEST(test_scope_timer)
{
    conc::LatencyTracker tracker;

    {
        conc::LatencyTracker::ScopeTimer timer(tracker);
        // Do some work.
        volatile int sum = 0;
        for (int i = 0; i < 1000; ++i)
            sum += i;
        (void)sum;
    }

    auto snap = tracker.snapshot();
    ASSERT_EQ(snap.count, 1u);
    ASSERT_TRUE(snap.min_ns > 0);
}

// ---------------------------------------------------------------------------
// Metrics dump
// ---------------------------------------------------------------------------
TEST(test_metrics_dump)
{
    conc::MetricsRegistry registry;

    registry.counter("requests").increment(100);
    registry.gauge("connections").set(42);
    registry.latency("latency").record(std::chrono::nanoseconds(500));

    auto dump = registry.dump();
    ASSERT_TRUE(dump.size() >= 2); // at least counter + gauge entries

    // Verify the counter entry exists.
    bool found_requests = false;
    for (auto &[name, val] : dump)
    {
        if (name == "requests")
        {
            found_requests = true;
            ASSERT_EQ(val, std::string("100"));
        }
    }
    ASSERT_TRUE(found_requests);
}

// ---------------------------------------------------------------------------
// Counter reset
// ---------------------------------------------------------------------------
TEST(test_counter_reset)
{
    conc::Counter c;
    c.increment(50);
    ASSERT_EQ(c.value(), 50u);
    c.reset();
    ASSERT_EQ(c.value(), 0u);
}

// ---------------------------------------------------------------------------
// Thread pool with futures under load
// ---------------------------------------------------------------------------
TEST(test_threadpool_futures_stress)
{
    constexpr int NUM_TASKS = 10000;

    conc::ThreadPoolConfig cfg;
    cfg.num_threads = 8;
    cfg.worker_queue_size = 2048;
    cfg.global_queue_size = 8192;
    conc::ThreadPoolExecutor pool(cfg);

    std::vector<std::future<int>> futures;
    futures.reserve(NUM_TASKS);

    for (int i = 0; i < NUM_TASKS; ++i)
    {
        futures.push_back(pool.submit_async([i]() -> int
                                            { return i * 2; }));
    }

    // Verify all futures.
    for (int i = 0; i < NUM_TASKS; ++i)
    {
        auto status = futures[i].wait_for(std::chrono::seconds(10));
        ASSERT_TRUE(status == std::future_status::ready);
        ASSERT_EQ(futures[i].get(), i * 2);
    }

    pool.shutdown();
}

// ===========================================================================
int main()
{
    std::cout << "stress_test" << std::endl;

    RUN_TEST(test_threadpool_high_load);
    RUN_TEST(test_mpmc_contention);
    RUN_TEST(test_event_loop_burst);
    RUN_TEST(test_metrics_concurrent);
    RUN_TEST(test_metrics_registry_concurrent);
    RUN_TEST(test_scope_timer);
    RUN_TEST(test_metrics_dump);
    RUN_TEST(test_counter_reset);
    RUN_TEST(test_threadpool_futures_stress);

    std::cout << "All stress tests passed." << std::endl;
    return 0;
}
