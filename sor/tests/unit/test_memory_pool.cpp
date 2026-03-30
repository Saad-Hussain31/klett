// Unit tests for MemoryPool and SPSCQueue.

#include <catch2/catch_test_macros.hpp>

#include "core/memory_pool.h"
#include "core/spsc_queue.h"
#include "core/order.h"

#include <vector>
#include <thread>
#include <set>

using namespace sor;

// ============================================================================
// MemoryPool tests
// ============================================================================

// Use a small, testable struct that satisfies the size requirement.
struct TestObj
{
    uint64_t a{0};
    uint64_t b{0};
};

static_assert(sizeof(TestObj) >= sizeof(std::atomic<uint32_t>),
              "TestObj must be large enough for pool free-list node");

TEST_CASE("MemoryPool: capacity and initial available", "[memory_pool]")
{
    constexpr size_t N = 16;
    MemoryPool<TestObj, N> pool;
    REQUIRE(pool.capacity() == N);
    REQUIRE(pool.available() == N);
}

TEST_CASE("MemoryPool: allocate up to capacity", "[memory_pool]")
{
    constexpr size_t N = 8;
    MemoryPool<TestObj, N> pool;

    std::vector<TestObj *> ptrs;
    ptrs.reserve(N);

    for (size_t i = 0; i < N; ++i)
    {
        TestObj *p = pool.allocate();
        REQUIRE(p != nullptr);
        ptrs.push_back(p);
    }

    CHECK(pool.available() == 0);

    // All pointers should be unique.
    std::set<TestObj *> unique_ptrs(ptrs.begin(), ptrs.end());
    REQUIRE(unique_ptrs.size() == N);
}

TEST_CASE("MemoryPool: returns nullptr when exhausted", "[memory_pool]")
{
    constexpr size_t N = 4;
    MemoryPool<TestObj, N> pool;

    // Exhaust the pool.
    for (size_t i = 0; i < N; ++i)
    {
        REQUIRE(pool.allocate() != nullptr);
    }

    REQUIRE(pool.available() == 0);
    REQUIRE(pool.allocate() == nullptr);
    REQUIRE(pool.allocate() == nullptr); // multiple calls still return nullptr
}

TEST_CASE("MemoryPool: deallocate and reallocate", "[memory_pool]")
{
    constexpr size_t N = 4;
    MemoryPool<TestObj, N> pool;

    TestObj *p1 = pool.allocate();
    TestObj *p2 = pool.allocate();
    REQUIRE(pool.available() == 2);

    pool.deallocate(p1);
    REQUIRE(pool.available() == 3);

    TestObj *p3 = pool.allocate();
    REQUIRE(p3 != nullptr);
    REQUIRE(pool.available() == 2);

    pool.deallocate(p2);
    pool.deallocate(p3);
    REQUIRE(pool.available() == 4);
}

TEST_CASE("MemoryPool: available count tracking", "[memory_pool]")
{
    constexpr size_t N = 8;
    MemoryPool<TestObj, N> pool;

    std::vector<TestObj *> ptrs;

    for (size_t i = 0; i < N; ++i)
    {
        ptrs.push_back(pool.allocate());
        CHECK(pool.available() == N - i - 1);
    }

    for (size_t i = 0; i < N; ++i)
    {
        pool.deallocate(ptrs[i]);
        CHECK(pool.available() == i + 1);
    }
}

TEST_CASE("MemoryPool: multiple allocate/deallocate cycles", "[memory_pool]")
{
    constexpr size_t N = 4;
    MemoryPool<TestObj, N> pool;

    for (int cycle = 0; cycle < 10; ++cycle)
    {
        std::vector<TestObj *> ptrs;
        for (size_t i = 0; i < N; ++i)
        {
            TestObj *p = pool.allocate();
            REQUIRE(p != nullptr);
            // Placement-new to actually use the memory.
            new (p) TestObj{static_cast<uint64_t>(cycle), static_cast<uint64_t>(i)};
            ptrs.push_back(p);
        }
        REQUIRE(pool.available() == 0);
        REQUIRE(pool.allocate() == nullptr);

        for (auto *p : ptrs)
        {
            p->~TestObj();
            pool.deallocate(p);
        }
        REQUIRE(pool.available() == N);
    }
}

TEST_CASE("MemoryPool: works with Order type", "[memory_pool]")
{
    constexpr size_t N = 4;
    MemoryPool<Order, N> pool;

    Order *o = pool.allocate();
    REQUIRE(o != nullptr);

    new (o) Order{};
    o->id = 42;
    o->quantity = 100;
    CHECK(o->id == 42);
    CHECK(o->quantity == 100);

    o->~Order();
    pool.deallocate(o);
    REQUIRE(pool.available() == N);
}

// ============================================================================
// SPSCQueue tests
// ============================================================================

TEST_CASE("SPSCQueue: empty on construction", "[spsc_queue]")
{
    SPSCQueue<int, 16> q;
    REQUIRE(q.empty());
    REQUIRE(q.size() == 0);
}

TEST_CASE("SPSCQueue: push and pop single item", "[spsc_queue]")
{
    SPSCQueue<int, 16> q;

    REQUIRE(q.try_push(42));
    REQUIRE_FALSE(q.empty());
    REQUIRE(q.size() == 1);

    int val = 0;
    REQUIRE(q.try_pop(val));
    REQUIRE(val == 42);
    REQUIRE(q.empty());
}

TEST_CASE("SPSCQueue: pop from empty returns false", "[spsc_queue]")
{
    SPSCQueue<int, 16> q;
    int val = 0;
    REQUIRE_FALSE(q.try_pop(val));
}

TEST_CASE("SPSCQueue: fill to capacity", "[spsc_queue]")
{
    // Capacity 16 means usable capacity = 15 (one slot is sentinel).
    SPSCQueue<int, 16> q;
    REQUIRE(q.capacity() == 15);

    for (int i = 0; i < 15; ++i)
    {
        REQUIRE(q.try_push(i));
    }
    REQUIRE(q.size() == 15);

    // Queue should be full.
    REQUIRE_FALSE(q.try_push(999));
}

TEST_CASE("SPSCQueue: FIFO ordering preserved", "[spsc_queue]")
{
    SPSCQueue<int, 32> q;

    for (int i = 0; i < 20; ++i)
    {
        REQUIRE(q.try_push(i));
    }

    for (int i = 0; i < 20; ++i)
    {
        int val = -1;
        REQUIRE(q.try_pop(val));
        REQUIRE(val == i);
    }

    REQUIRE(q.empty());
}

TEST_CASE("SPSCQueue: interleaved push and pop", "[spsc_queue]")
{
    SPSCQueue<int, 8> q;

    REQUIRE(q.try_push(1));
    REQUIRE(q.try_push(2));

    int val = 0;
    REQUIRE(q.try_pop(val));
    REQUIRE(val == 1);

    REQUIRE(q.try_push(3));
    REQUIRE(q.try_push(4));

    REQUIRE(q.try_pop(val));
    REQUIRE(val == 2);

    REQUIRE(q.try_pop(val));
    REQUIRE(val == 3);

    REQUIRE(q.try_pop(val));
    REQUIRE(val == 4);

    REQUIRE(q.empty());
}

TEST_CASE("SPSCQueue: move semantics", "[spsc_queue]")
{
    SPSCQueue<std::string, 8> q;

    std::string s = "hello world";
    REQUIRE(q.try_push(std::move(s)));

    std::string out;
    REQUIRE(q.try_pop(out));
    REQUIRE(out == "hello world");
}

TEST_CASE("SPSCQueue: wrap-around behaviour", "[spsc_queue]")
{
    // Small capacity to force quick wrapping.
    SPSCQueue<int, 4> q; // usable capacity = 3

    // Fill and drain several times to exercise index wrapping.
    for (int round = 0; round < 10; ++round)
    {
        for (int i = 0; i < 3; ++i)
        {
            REQUIRE(q.try_push(round * 100 + i));
        }
        REQUIRE_FALSE(q.try_push(999)); // full

        for (int i = 0; i < 3; ++i)
        {
            int val = -1;
            REQUIRE(q.try_pop(val));
            REQUIRE(val == round * 100 + i);
        }
        REQUIRE(q.empty());
    }
}

TEST_CASE("SPSCQueue: concurrent producer-consumer", "[spsc_queue]")
{
    constexpr int NUM_ITEMS = 10000;
    SPSCQueue<int, 1024> q;

    std::thread producer([&]()
                         {
        for (int i = 0; i < NUM_ITEMS; ++i)
        {
            while (!q.try_push(i))
            {
                // spin
            }
        } });

    std::vector<int> received;
    received.reserve(NUM_ITEMS);

    std::thread consumer([&]()
                         {
        int count = 0;
        while (count < NUM_ITEMS)
        {
            int val;
            if (q.try_pop(val))
            {
                received.push_back(val);
                ++count;
            }
        } });

    producer.join();
    consumer.join();

    REQUIRE(received.size() == NUM_ITEMS);
    for (int i = 0; i < NUM_ITEMS; ++i)
    {
        REQUIRE(received[i] == i);
    }
}
