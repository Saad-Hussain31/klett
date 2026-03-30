/// @file event_test.cpp
/// @brief Tests for EventLoop and the Event type system.

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
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Test event types
// ---------------------------------------------------------------------------
struct ClickEvent : conc::Event<ClickEvent>
{
    int x, y;
    ClickEvent(int x_, int y_) : conc::Event<ClickEvent>(), x(x_), y(y_) {}
};

struct KeyEvent : conc::Event<KeyEvent>
{
    char key;
    explicit KeyEvent(char k) : conc::Event<KeyEvent>(), key(k) {}
};

struct ResizeEvent : conc::Event<ResizeEvent>
{
    int width, height;
    ResizeEvent(int w, int h) : conc::Event<ResizeEvent>(), width(w), height(h) {}
};

// ---------------------------------------------------------------------------
// Event type IDs are unique per type
// ---------------------------------------------------------------------------
TEST(test_event_type_ids_unique)
{
    auto click_id = ClickEvent::type_id();
    auto key_id = KeyEvent::type_id();
    auto resize_id = ResizeEvent::type_id();

    ASSERT_TRUE(click_id != key_id);
    ASSERT_TRUE(click_id != resize_id);
    ASSERT_TRUE(key_id != resize_id);

    // Stable across calls.
    ASSERT_EQ(ClickEvent::type_id(), click_id);
}

// ---------------------------------------------------------------------------
// make_event helper
// ---------------------------------------------------------------------------
TEST(test_make_event)
{
    auto evt = conc::make_event<ClickEvent>(10, 20);
    ASSERT_TRUE(evt != nullptr);
    ASSERT_EQ(evt->type_id(), ClickEvent::type_id());

    auto *click = static_cast<ClickEvent *>(evt.get());
    ASSERT_EQ(click->x, 10);
    ASSERT_EQ(click->y, 20);
}

// ---------------------------------------------------------------------------
// Register handler, dispatch event, verify handler called
// ---------------------------------------------------------------------------
TEST(test_register_dispatch_handler)
{
    conc::EventLoop loop;

    int received_x = -1;
    int received_y = -1;

    loop.on<ClickEvent>([&](const ClickEvent &e)
                        {
        received_x = e.x;
        received_y = e.y; });

    loop.dispatch(conc::make_event<ClickEvent>(42, 99));

    // Process the event.
    bool processed = loop.run_one();
    ASSERT_TRUE(processed);
    ASSERT_EQ(received_x, 42);
    ASSERT_EQ(received_y, 99);
}

// ---------------------------------------------------------------------------
// Multiple event types with distinct handlers
// ---------------------------------------------------------------------------
TEST(test_multiple_event_types)
{
    conc::EventLoop loop;

    int click_count = 0;
    char last_key = '\0';
    int resize_w = 0;

    loop.on<ClickEvent>([&](const ClickEvent &)
                        { click_count++; });

    loop.on<KeyEvent>([&](const KeyEvent &e)
                      { last_key = e.key; });

    loop.on<ResizeEvent>([&](const ResizeEvent &e)
                         { resize_w = e.width; });

    loop.dispatch(conc::make_event<ClickEvent>(1, 2));
    loop.dispatch(conc::make_event<KeyEvent>('A'));
    loop.dispatch(conc::make_event<ResizeEvent>(1920, 1080));
    loop.dispatch(conc::make_event<ClickEvent>(3, 4));

    // Process all four events.
    for (int i = 0; i < 4; ++i)
    {
        loop.run_one();
    }

    ASSERT_EQ(click_count, 2);
    ASSERT_EQ(last_key, 'A');
    ASSERT_EQ(resize_w, 1920);
}

// ---------------------------------------------------------------------------
// Multiple handlers for the same event type
// ---------------------------------------------------------------------------
TEST(test_multiple_handlers_same_type)
{
    conc::EventLoop loop;

    int handler1_count = 0;
    int handler2_count = 0;

    loop.on<ClickEvent>([&](const ClickEvent &)
                        { handler1_count++; });
    loop.on<ClickEvent>([&](const ClickEvent &)
                        { handler2_count++; });

    loop.dispatch(conc::make_event<ClickEvent>(0, 0));
    loop.run_one();

    ASSERT_EQ(handler1_count, 1);
    ASSERT_EQ(handler2_count, 1);
}

// ---------------------------------------------------------------------------
// Deregister handler (off)
// ---------------------------------------------------------------------------
TEST(test_deregister_handler)
{
    conc::EventLoop loop;

    int count = 0;
    auto id = loop.on<ClickEvent>([&](const ClickEvent &)
                                  { count++; });

    // First event handled.
    loop.dispatch(conc::make_event<ClickEvent>(0, 0));
    loop.run_one();
    ASSERT_EQ(count, 1);

    // Deregister.
    loop.off(id);

    // Second event not handled.
    loop.dispatch(conc::make_event<ClickEvent>(0, 0));
    loop.run_one();
    ASSERT_EQ(count, 1); // unchanged
}

// ---------------------------------------------------------------------------
// Deregister one handler, other handlers for same type remain
// ---------------------------------------------------------------------------
TEST(test_deregister_selective)
{
    conc::EventLoop loop;

    int count_a = 0;
    int count_b = 0;

    auto id_a = loop.on<ClickEvent>([&](const ClickEvent &)
                                    { count_a++; });
    loop.on<ClickEvent>([&](const ClickEvent &)
                        { count_b++; });

    loop.dispatch(conc::make_event<ClickEvent>(0, 0));
    loop.run_one();
    ASSERT_EQ(count_a, 1);
    ASSERT_EQ(count_b, 1);

    loop.off(id_a);

    loop.dispatch(conc::make_event<ClickEvent>(0, 0));
    loop.run_one();
    ASSERT_EQ(count_a, 1); // not called again
    ASSERT_EQ(count_b, 2); // still called
}

// ---------------------------------------------------------------------------
// run_for with timeout
// ---------------------------------------------------------------------------
TEST(test_run_for)
{
    conc::EventLoop loop;

    std::atomic<int> count{0};
    loop.on<ClickEvent>([&](const ClickEvent &)
                        { count.fetch_add(1, std::memory_order_relaxed); });

    // Dispatch some events.
    for (int i = 0; i < 5; ++i)
    {
        loop.dispatch(conc::make_event<ClickEvent>(i, i));
    }

    auto start = std::chrono::steady_clock::now();
    loop.run_for(std::chrono::milliseconds(100));
    auto elapsed = std::chrono::steady_clock::now() - start;

    // All events should have been processed.
    ASSERT_EQ(count.load(), 5);

    // run_for should have run for roughly the specified time (at least 50ms,
    // allowing tolerance for scheduling).
    ASSERT_TRUE(elapsed >= std::chrono::milliseconds(50));
}

// ---------------------------------------------------------------------------
// run() and stop() from another thread
// ---------------------------------------------------------------------------
TEST(test_run_and_stop)
{
    conc::EventLoop loop;

    std::atomic<int> count{0};
    loop.on<ClickEvent>([&](const ClickEvent &)
                        { count.fetch_add(1, std::memory_order_relaxed); });

    // Run the loop in a background thread.
    std::thread loop_thread([&loop]()
                            { loop.run(); });

    // Give the loop a moment to start.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(loop.running());

    // Dispatch events while running.
    for (int i = 0; i < 10; ++i)
    {
        loop.dispatch(conc::make_event<ClickEvent>(i, i));
    }

    // Give time for events to be processed.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    loop.stop();
    loop_thread.join();

    ASSERT_TRUE(!loop.running());
    ASSERT_EQ(count.load(), 10);
}

// ---------------------------------------------------------------------------
// No event ready: run_one returns false
// ---------------------------------------------------------------------------
TEST(test_run_one_empty)
{
    conc::EventLoop loop;

    bool processed = loop.run_one();
    ASSERT_TRUE(!processed);
}

// ---------------------------------------------------------------------------
// emit() convenience helper
// ---------------------------------------------------------------------------
TEST(test_emit_helper)
{
    conc::EventLoop loop;

    int received_x = -1;
    loop.on<ClickEvent>([&](const ClickEvent &e)
                        { received_x = e.x; });

    loop.emit<ClickEvent>(55, 66);
    loop.run_one();

    ASSERT_EQ(received_x, 55);
}

// ===========================================================================
int main()
{
    std::cout << "event_test" << std::endl;

    RUN_TEST(test_event_type_ids_unique);
    RUN_TEST(test_make_event);
    RUN_TEST(test_register_dispatch_handler);
    RUN_TEST(test_multiple_event_types);
    RUN_TEST(test_multiple_handlers_same_type);
    RUN_TEST(test_deregister_handler);
    RUN_TEST(test_deregister_selective);
    RUN_TEST(test_run_for);
    RUN_TEST(test_run_and_stop);
    RUN_TEST(test_run_one_empty);
    RUN_TEST(test_emit_helper);

    std::cout << "All event tests passed." << std::endl;
    return 0;
}
