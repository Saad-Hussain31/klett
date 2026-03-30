/// @file queue_test.cpp
/// @brief Tests for MPMCQueue and BackpressuredQueue.

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
#include <set>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Basic push/pop
// ---------------------------------------------------------------------------
TEST(test_basic_push_pop)
{
    conc::MPMCQueue<int> q(16);

    ASSERT_TRUE(q.try_push(42));
    ASSERT_TRUE(q.try_push(99));
    ASSERT_EQ(q.size_approx(), 2u);

    auto v1 = q.try_pop();
    ASSERT_TRUE(v1.has_value());
    ASSERT_EQ(*v1, 42);

    auto v2 = q.try_pop();
    ASSERT_TRUE(v2.has_value());
    ASSERT_EQ(*v2, 99);

    auto v3 = q.try_pop();
    ASSERT_TRUE(!v3.has_value());
    ASSERT_TRUE(q.empty());
}

// ---------------------------------------------------------------------------
// Capacity and bounded behavior
// ---------------------------------------------------------------------------
TEST(test_capacity_and_bounded)
{
    // Requested capacity 5 rounds up to 8 (next power of two).
    conc::MPMCQueue<int> q(5);
    ASSERT_EQ(q.capacity(), 8u);

    // Fill to capacity.
    for (int i = 0; i < 8; ++i)
    {
        ASSERT_TRUE(q.try_push(i));
    }
    ASSERT_EQ(q.size_approx(), 8u);

    // Queue is full -- push must fail.
    ASSERT_TRUE(!q.try_push(100));

    // Pop one and push again should succeed.
    auto v = q.try_pop();
    ASSERT_TRUE(v.has_value());
    ASSERT_EQ(*v, 0);
    ASSERT_TRUE(q.try_push(100));
}

// ---------------------------------------------------------------------------
// Minimum capacity edge case
// ---------------------------------------------------------------------------
TEST(test_min_capacity)
{
    // Capacity < 2 should be clamped to at least 2.
    conc::MPMCQueue<int> q(1);
    ASSERT_TRUE(q.capacity() >= 2u);
    ASSERT_TRUE(conc::is_power_of_two(q.capacity()));
}

// ---------------------------------------------------------------------------
// Power-of-two capacity
// ---------------------------------------------------------------------------
TEST(test_power_of_two_capacity)
{
    conc::MPMCQueue<int> q1(1024);
    ASSERT_EQ(q1.capacity(), 1024u);

    conc::MPMCQueue<int> q2(1000);
    ASSERT_EQ(q2.capacity(), 1024u);

    conc::MPMCQueue<int> q3(17);
    ASSERT_EQ(q3.capacity(), 32u);
}

// ---------------------------------------------------------------------------
// Multi-threaded MPMC: 4 producers, 4 consumers, 10000 items each
// ---------------------------------------------------------------------------
TEST(test_mpmc_multithreaded)
{
    constexpr int NUM_PRODUCERS = 4;
    constexpr int NUM_CONSUMERS = 4;
    constexpr int ITEMS_PER_PRODUCER = 10000;
    constexpr int TOTAL_ITEMS = NUM_PRODUCERS * ITEMS_PER_PRODUCER;

    conc::MPMCQueue<int> q(1024);

    std::atomic<int> produced_count{0};
    std::atomic<int> consumed_count{0};
    std::atomic<bool> all_produced{false};

    // Each consumer accumulates what it popped.
    std::vector<std::vector<int>> consumed(NUM_CONSUMERS);

    // Producer threads.
    std::vector<std::thread> producers;
    for (int p = 0; p < NUM_PRODUCERS; ++p)
    {
        producers.emplace_back([&, p]()
                               {
            for (int i = 0; i < ITEMS_PER_PRODUCER; ++i)
            {
                int value = p * ITEMS_PER_PRODUCER + i;
                while (!q.try_push(value))
                {
                    std::this_thread::yield();
                }
                produced_count.fetch_add(1, std::memory_order_relaxed);
            } });
    }

    // Consumer threads.
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
                    consumed[c].push_back(*val);
                    consumed_count.fetch_add(1, std::memory_order_relaxed);
                }
                else
                {
                    if (all_produced.load(std::memory_order_acquire) &&
                        consumed_count.load(std::memory_order_acquire) >= TOTAL_ITEMS)
                    {
                        break;
                    }
                    std::this_thread::yield();
                }
            } });
    }

    // Wait for producers to finish.
    for (auto &t : producers)
        t.join();
    all_produced.store(true, std::memory_order_release);

    // Wait for consumers to finish.
    for (auto &t : consumers)
        t.join();

    // Verify: every value produced was consumed exactly once.
    std::set<int> all_consumed;
    for (auto &v : consumed)
    {
        for (int val : v)
        {
            all_consumed.insert(val);
        }
    }
    ASSERT_EQ(static_cast<int>(all_consumed.size()), TOTAL_ITEMS);
    ASSERT_EQ(*all_consumed.begin(), 0);
    ASSERT_EQ(*all_consumed.rbegin(), TOTAL_ITEMS - 1);
}

// ---------------------------------------------------------------------------
// BackpressuredQueue - Drop policy
// ---------------------------------------------------------------------------
TEST(test_backpressure_drop)
{
    conc::MPMCQueue<int> q(4); // capacity rounds to 4
    conc::BackpressureConfig cfg;
    cfg.strategy = conc::BackpressureStrategy::Drop;
    conc::BackpressuredQueue<int> bq(q, cfg);

    // Fill the queue.
    for (int i = 0; i < 4; ++i)
    {
        ASSERT_TRUE(bq.push(i));
    }

    // Overflow item is silently dropped (returns false).
    bool pushed = bq.push(999);
    ASSERT_TRUE(!pushed);

    // Original items are intact.
    ASSERT_EQ(q.size_approx(), 4u);
    auto v = q.try_pop();
    ASSERT_TRUE(v.has_value());
    ASSERT_EQ(*v, 0);
}

// ---------------------------------------------------------------------------
// BackpressuredQueue - Reject policy
// ---------------------------------------------------------------------------
TEST(test_backpressure_reject)
{
    conc::MPMCQueue<int> q(4);
    conc::BackpressureConfig cfg;
    cfg.strategy = conc::BackpressureStrategy::Reject;
    conc::BackpressuredQueue<int> bq(q, cfg);

    // Fill queue.
    for (int i = 0; i < 4; ++i)
    {
        ASSERT_TRUE(bq.push(i));
    }

    // Reject returns false immediately.
    ASSERT_TRUE(!bq.push(999));

    // After draining one slot, push succeeds.
    q.try_pop();
    ASSERT_TRUE(bq.push(999));
}

// ---------------------------------------------------------------------------
// BackpressuredQueue - Block policy
// ---------------------------------------------------------------------------
TEST(test_backpressure_block)
{
    conc::MPMCQueue<int> q(4);
    conc::BackpressureConfig cfg;
    cfg.strategy = conc::BackpressureStrategy::Block;
    cfg.block_timeout = std::chrono::microseconds(50000); // 50ms
    cfg.spin_count = 8;
    conc::BackpressuredQueue<int> bq(q, cfg);

    // Fill queue.
    for (int i = 0; i < 4; ++i)
    {
        ASSERT_TRUE(bq.push(i));
    }

    // Launch a thread that will drain the queue after a short delay.
    std::thread drainer([&]()
                        {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        q.try_pop(); });

    // Block push should succeed once the drainer frees a slot.
    bool pushed = bq.push(42);
    ASSERT_TRUE(pushed);

    drainer.join();
}

// ---------------------------------------------------------------------------
// BackpressuredQueue - Block policy timeout
// ---------------------------------------------------------------------------
TEST(test_backpressure_block_timeout)
{
    conc::MPMCQueue<int> q(4);
    conc::BackpressureConfig cfg;
    cfg.strategy = conc::BackpressureStrategy::Block;
    cfg.block_timeout = std::chrono::microseconds(1000); // 1ms very short
    cfg.spin_count = 4;
    conc::BackpressuredQueue<int> bq(q, cfg);

    // Fill queue.
    for (int i = 0; i < 4; ++i)
    {
        ASSERT_TRUE(bq.push(i));
    }

    // No drainer, so this should time out and return false.
    auto start = std::chrono::steady_clock::now();
    bool pushed = bq.push(999);
    auto elapsed = std::chrono::steady_clock::now() - start;
    ASSERT_TRUE(!pushed);
    // Should have waited at least close to the timeout period.
    ASSERT_TRUE(elapsed >= std::chrono::microseconds(500));
}

// ---------------------------------------------------------------------------
// BackpressuredQueue - config update
// ---------------------------------------------------------------------------
TEST(test_backpressure_config_update)
{
    conc::MPMCQueue<int> q(4);
    conc::BackpressureConfig cfg;
    cfg.strategy = conc::BackpressureStrategy::Reject;
    conc::BackpressuredQueue<int> bq(q, cfg);

    ASSERT_EQ(bq.config().strategy, conc::BackpressureStrategy::Reject);

    cfg.strategy = conc::BackpressureStrategy::Drop;
    bq.set_config(cfg);
    ASSERT_EQ(bq.config().strategy, conc::BackpressureStrategy::Drop);
}

// ---------------------------------------------------------------------------
// Move-only types in the queue
// ---------------------------------------------------------------------------
TEST(test_move_only_type)
{
    conc::MPMCQueue<std::unique_ptr<int>> q(8);

    auto p = std::make_unique<int>(42);
    ASSERT_TRUE(q.try_push(std::move(p)));
    ASSERT_TRUE(p == nullptr); // moved from

    auto result = q.try_pop();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(**result, 42);
}

// ===========================================================================
int main()
{
    std::cout << "queue_test" << std::endl;

    RUN_TEST(test_basic_push_pop);
    RUN_TEST(test_capacity_and_bounded);
    RUN_TEST(test_min_capacity);
    RUN_TEST(test_power_of_two_capacity);
    RUN_TEST(test_mpmc_multithreaded);
    RUN_TEST(test_backpressure_drop);
    RUN_TEST(test_backpressure_reject);
    RUN_TEST(test_backpressure_block);
    RUN_TEST(test_backpressure_block_timeout);
    RUN_TEST(test_backpressure_config_update);
    RUN_TEST(test_move_only_type);

    std::cout << "All queue tests passed." << std::endl;
    return 0;
}
