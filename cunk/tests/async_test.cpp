/// @file async_test.cpp
/// @brief Tests for AsyncTask and Promise.

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

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

// ---------------------------------------------------------------------------
// Basic promise.set_value / task.get
// ---------------------------------------------------------------------------
TEST(test_basic_set_get)
{
    conc::Promise<int> promise;
    auto task = promise.get_task();

    ASSERT_TRUE(!task.is_ready());

    promise.set_value(42);

    ASSERT_TRUE(task.is_ready());
    ASSERT_EQ(task.get(), 42);
}

// ---------------------------------------------------------------------------
// String result
// ---------------------------------------------------------------------------
TEST(test_string_result)
{
    conc::Promise<std::string> promise;
    auto task = promise.get_task();

    promise.set_value("hello async");
    ASSERT_EQ(task.get(), std::string("hello async"));
}

// ---------------------------------------------------------------------------
// Set value from another thread
// ---------------------------------------------------------------------------
TEST(test_cross_thread_set_get)
{
    conc::Promise<int> promise;
    auto task = promise.get_task();

    std::thread producer([&promise]()
                         {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        promise.set_value(99); });

    // get() blocks until the value is set.
    int result = task.get();
    ASSERT_EQ(result, 99);

    producer.join();
}

// ---------------------------------------------------------------------------
// then() continuation - value already set
// ---------------------------------------------------------------------------
TEST(test_then_already_ready)
{
    conc::Promise<int> promise;
    auto task = promise.get_task();

    // Set value before attaching continuation.
    promise.set_value(10);

    auto doubled = task.then([](int v) -> int
                             { return v * 2; });

    ASSERT_TRUE(doubled.is_ready());
    ASSERT_EQ(doubled.get(), 20);
}

// ---------------------------------------------------------------------------
// then() continuation - value set after
// ---------------------------------------------------------------------------
TEST(test_then_deferred)
{
    conc::Promise<int> promise;
    auto task = promise.get_task();

    // Attach continuation before value is set.
    auto doubled = task.then([](int v) -> int
                             { return v * 2; });

    ASSERT_TRUE(!doubled.is_ready());

    promise.set_value(15);

    ASSERT_EQ(doubled.get(), 30);
}

// ---------------------------------------------------------------------------
// Chained continuations
// ---------------------------------------------------------------------------
TEST(test_chained_then)
{
    conc::Promise<int> promise;
    auto task = promise.get_task();

    auto step1 = task.then([](int v) -> int
                           { return v + 10; });

    auto step2 = step1.then([](int v) -> std::string
                            { return "result=" + std::to_string(v); });

    promise.set_value(5);

    ASSERT_EQ(step2.get(), std::string("result=15"));
}

// ---------------------------------------------------------------------------
// then() with type change (int -> string)
// ---------------------------------------------------------------------------
TEST(test_then_type_change)
{
    conc::Promise<int> promise;
    auto task = promise.get_task();

    auto str_task = task.then([](int v) -> std::string
                              { return std::to_string(v); });

    promise.set_value(42);
    ASSERT_EQ(str_task.get(), std::string("42"));
}

// ---------------------------------------------------------------------------
// Exception propagation: set_exception
// ---------------------------------------------------------------------------
TEST(test_exception_propagation)
{
    conc::Promise<int> promise;
    auto task = promise.get_task();

    promise.set_exception(std::make_exception_ptr(std::runtime_error("test error")));

    ASSERT_TRUE(task.is_ready());

    bool caught = false;
    try
    {
        task.get();
    }
    catch (const std::runtime_error &e)
    {
        caught = true;
        ASSERT_EQ(std::string(e.what()), std::string("test error"));
    }
    ASSERT_TRUE(caught);
}

// ---------------------------------------------------------------------------
// Exception propagation from another thread
// ---------------------------------------------------------------------------
TEST(test_exception_cross_thread)
{
    conc::Promise<int> promise;
    auto task = promise.get_task();

    std::thread producer([&promise]()
                         {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        promise.set_exception(std::make_exception_ptr(std::logic_error("bad logic"))); });

    bool caught = false;
    try
    {
        task.get();
    }
    catch (const std::logic_error &e)
    {
        caught = true;
        ASSERT_EQ(std::string(e.what()), std::string("bad logic"));
    }
    ASSERT_TRUE(caught);

    producer.join();
}

// ---------------------------------------------------------------------------
// Void specialization: basic set/get
// ---------------------------------------------------------------------------
TEST(test_void_basic)
{
    conc::Promise<void> promise;
    auto task = promise.get_task();

    ASSERT_TRUE(!task.is_ready());

    promise.set_value();

    ASSERT_TRUE(task.is_ready());
    // get() should not throw.
    task.get();
}

// ---------------------------------------------------------------------------
// Void specialization: exception
// ---------------------------------------------------------------------------
TEST(test_void_exception)
{
    conc::Promise<void> promise;
    auto task = promise.get_task();

    promise.set_exception(std::make_exception_ptr(std::runtime_error("void error")));

    bool caught = false;
    try
    {
        task.get();
    }
    catch (const std::runtime_error &e)
    {
        caught = true;
        ASSERT_EQ(std::string(e.what()), std::string("void error"));
    }
    ASSERT_TRUE(caught);
}

// ---------------------------------------------------------------------------
// Void specialization: then() continuation
// ---------------------------------------------------------------------------
TEST(test_void_then)
{
    conc::Promise<void> promise;
    auto task = promise.get_task();

    auto next = task.then([]() -> int
                          { return 123; });

    promise.set_value();

    ASSERT_EQ(next.get(), 123);
}

// ---------------------------------------------------------------------------
// Void specialization: then() with void return
// ---------------------------------------------------------------------------
TEST(test_void_then_void)
{
    conc::Promise<void> promise;
    auto task = promise.get_task();

    bool ran = false;
    auto next = task.then([&ran]()
                          { ran = true; });

    promise.set_value();

    next.get();
    ASSERT_TRUE(ran);
}

// ---------------------------------------------------------------------------
// Cross-thread: producer sets, consumer calls get with then
// ---------------------------------------------------------------------------
TEST(test_cross_thread_then)
{
    conc::Promise<int> promise;
    auto task = promise.get_task();

    auto doubled = task.then([](int v) -> int
                             { return v * 2; });

    std::thread producer([&promise]()
                         {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        promise.set_value(50); });

    ASSERT_EQ(doubled.get(), 100);

    producer.join();
}

// ---------------------------------------------------------------------------
// Exception in then() continuation is captured
// ---------------------------------------------------------------------------
TEST(test_exception_in_continuation)
{
    conc::Promise<int> promise;
    auto task = promise.get_task();

    auto bad = task.then([](int) -> int
                         {
                             throw std::runtime_error("continuation failed");
                             return 0; // unreachable
                         });

    promise.set_value(1);

    bool caught = false;
    try
    {
        bad.get();
    }
    catch (const std::runtime_error &e)
    {
        caught = true;
        ASSERT_EQ(std::string(e.what()), std::string("continuation failed"));
    }
    ASSERT_TRUE(caught);
}

// ===========================================================================
int main()
{
    std::cout << "async_test" << std::endl;

    RUN_TEST(test_basic_set_get);
    RUN_TEST(test_string_result);
    RUN_TEST(test_cross_thread_set_get);
    RUN_TEST(test_then_already_ready);
    RUN_TEST(test_then_deferred);
    RUN_TEST(test_chained_then);
    RUN_TEST(test_then_type_change);
    RUN_TEST(test_exception_propagation);
    RUN_TEST(test_exception_cross_thread);
    RUN_TEST(test_void_basic);
    RUN_TEST(test_void_exception);
    RUN_TEST(test_void_then);
    RUN_TEST(test_void_then_void);
    RUN_TEST(test_cross_thread_then);
    RUN_TEST(test_exception_in_continuation);

    std::cout << "All async tests passed." << std::endl;
    return 0;
}
