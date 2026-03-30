/// @file basic_usage.cpp
/// @brief Quick-start tutorial for the concurrency framework.
///
/// Demonstrates every major component in a single program:
///   1. MetricsRegistry  -- counters, gauges, latency trackers
///   2. ThreadPoolExecutor -- fire-and-forget + async tasks with futures
///   3. MPMCQueue         -- lock-free bounded multi-producer/multi-consumer queue
///   4. BackpressuredQueue -- queue wrapper with Block backpressure policy
///   5. EventLoop         -- register handlers, emit custom events, run briefly
///   6. Promise/AsyncTask -- then() continuation chaining
///   7. Scheduler         -- one-shot and periodic scheduled tasks
///   8. LatencyTracker::ScopeTimer -- RAII latency measurement
///   9. Metrics dump      -- print everything at the end
///
/// Build (assuming the include path is set to concurrency/include):
///   g++ -std=c++17 -pthread -I../include basic_usage.cpp ../src/*.cpp -o basic_usage
///
/// The program runs for roughly 1-2 seconds and exits cleanly.

#include <Concurrency.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace conc;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// 1. Define two custom event types for the EventLoop demo.
//    Any struct that inherits Event<Derived> automatically gets a unique type ID.
// ---------------------------------------------------------------------------

struct ChatMessage : Event<ChatMessage>
{
    std::string sender;
    std::string text;
    ChatMessage(std::string s, std::string t)
        : Event<ChatMessage>(), sender(std::move(s)), text(std::move(t)) {}
};

struct SystemAlert : Event<SystemAlert>
{
    int code;
    std::string message;
    SystemAlert(int c, std::string m)
        : Event<SystemAlert>(), code(c), message(std::move(m)) {}
};

// ---------------------------------------------------------------------------
// Helper: thread-safe print (avoids interleaved output from concurrent threads)
// ---------------------------------------------------------------------------

static std::mutex g_print_mutex;

template <typename... Args>
void safe_print(Args &&...args)
{
    std::lock_guard<std::mutex> lock(g_print_mutex);
    (std::cout << ... << std::forward<Args>(args)) << "\n";
}

// ===========================================================================
int main()
{
    std::cout << "=== conc framework -- basic_usage demo ===\n\n";

    // -----------------------------------------------------------------------
    // 1. METRICS REGISTRY
    //    Create a central registry and pre-register some named metrics.
    //    Components will bump these as work is done.
    // -----------------------------------------------------------------------
    std::cout << "--- [1] MetricsRegistry ---\n";

    MetricsRegistry registry;

    Counter &tasks_submitted = registry.counter("tasks.submitted");
    Counter &tasks_completed = registry.counter("tasks.completed");
    Counter &queue_pushes = registry.counter("queue.pushes");
    Counter &queue_pops = registry.counter("queue.pops");
    Gauge &active_threads = registry.gauge("threads.active");
    LatencyTracker &task_lat = registry.latency("task.latency");

    std::cout << "  Registered counters: tasks.submitted, tasks.completed, "
                 "queue.pushes, queue.pops\n";
    std::cout << "  Registered gauge:    threads.active\n";
    std::cout << "  Registered latency:  task.latency\n\n";

    // -----------------------------------------------------------------------
    // 2. THREAD POOL EXECUTOR
    //    Create a pool with 4 workers. Submit fire-and-forget tasks, then
    //    submit an async task and retrieve its result via std::future.
    // -----------------------------------------------------------------------
    std::cout << "--- [2] ThreadPoolExecutor ---\n";

    ThreadPoolConfig pool_cfg;
    pool_cfg.num_threads = 4;
    pool_cfg.worker_queue_size = 256;
    pool_cfg.global_queue_size = 1024;
    ThreadPoolExecutor pool(pool_cfg);

    std::cout << "  Created pool with " << pool.worker_count() << " workers\n";

    // 2a. Fire-and-forget tasks
    for (int i = 0; i < 8; ++i)
    {
        pool.submit([&, i]()
                    {
            active_threads.increment();
            tasks_submitted.increment();

            // Simulate a small unit of work
            std::this_thread::sleep_for(10ms);
            safe_print("  [pool] fire-and-forget task ", i, " done");

            tasks_completed.increment();
            active_threads.decrement(); });
    }

    // 2b. Async task -- returns a future
    auto future = pool.submit_async([&]() -> int
                                    {
        tasks_submitted.increment();
        std::this_thread::sleep_for(20ms);
        tasks_completed.increment();
        return 42; });

    int answer = future.get();
    std::cout << "  Async task returned: " << answer << "\n\n";

    // -----------------------------------------------------------------------
    // 3. MPMC QUEUE
    //    Spin up producer and consumer threads that push/pop integers through
    //    a lock-free bounded queue.
    // -----------------------------------------------------------------------
    std::cout << "--- [3] MPMCQueue ---\n";

    MPMCQueue<int> mpmc(64); // capacity rounds up to next power of 2

    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    constexpr int NUM_ITEMS = 200;

    // Two producer threads
    auto producer = [&](int id)
    {
        for (int i = id; i < NUM_ITEMS; i += 2)
        {
            while (!mpmc.try_push(i))
            {
                std::this_thread::yield(); // queue full, back off
            }
            queue_pushes.increment();
            produced.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // Two consumer threads
    auto consumer = [&]()
    {
        while (consumed.load(std::memory_order_relaxed) < NUM_ITEMS)
        {
            auto val = mpmc.try_pop();
            if (val.has_value())
            {
                queue_pops.increment();
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                std::this_thread::yield(); // queue empty, back off
            }
        }
    };

    std::thread p1(producer, 0), p2(producer, 1);
    std::thread c1(consumer), c2(consumer);

    p1.join();
    p2.join();
    c1.join();
    c2.join();

    std::cout << "  Produced: " << produced.load() << "  Consumed: " << consumed.load() << "\n\n";

    // -----------------------------------------------------------------------
    // 4. BACKPRESSURED QUEUE
    //    Wrap an MPMCQueue with a Block policy so pushes spin-wait instead of
    //    dropping items when the queue is full.
    // -----------------------------------------------------------------------
    std::cout << "--- [4] BackpressuredQueue (Block policy) ---\n";

    MPMCQueue<std::string> raw_q(4); // tiny capacity to trigger backpressure

    BackpressureConfig bp_cfg;
    bp_cfg.strategy = BackpressureStrategy::Block;
    bp_cfg.block_timeout = 500ms;
    bp_cfg.spin_count = 32;

    BackpressuredQueue<std::string> bp_queue(raw_q, bp_cfg);

    // Fill the queue beyond its capacity from one thread; a consumer drains it.
    std::atomic<bool> bp_done{false};
    int bp_pushed = 0;

    std::thread bp_consumer([&]()
                            {
        int count = 0;
        while (!bp_done.load(std::memory_order_relaxed) || !bp_queue.queue().empty()) {
            auto val = bp_queue.queue().try_pop();
            if (val.has_value()) {
                ++count;
            } else {
                std::this_thread::yield();
            }
        }
        safe_print("  [bp] consumer drained ", count, " items"); });

    for (int i = 0; i < 20; ++i)
    {
        if (bp_queue.push("msg-" + std::to_string(i)))
        {
            ++bp_pushed;
        }
    }
    bp_done.store(true, std::memory_order_relaxed);
    bp_consumer.join();

    std::cout << "  Pushed (with blocking): " << bp_pushed << " / 20\n\n";

    // -----------------------------------------------------------------------
    // 5. EVENT LOOP
    //    Register handlers for ChatMessage and SystemAlert, emit a few events,
    //    then run the loop for a short duration.
    // -----------------------------------------------------------------------
    std::cout << "--- [5] EventLoop ---\n";

    EventLoopConfig el_cfg;
    el_cfg.queue_size = 256;
    EventLoop loop(el_cfg);

    // Register a handler for ChatMessage events
    loop.on<ChatMessage>([](const ChatMessage &e)
                         { safe_print("  [event] Chat from ", e.sender, ": ", e.text); });

    // Register a handler for SystemAlert events
    loop.on<SystemAlert>([](const SystemAlert &e)
                         { safe_print("  [event] ALERT code=", e.code, " -- ", e.message); });

    // Emit some events (they are enqueued, not processed yet)
    loop.emit<ChatMessage>("Alice", "Hello, world!");
    loop.emit<ChatMessage>("Bob", "Framework looks great.");
    loop.emit<SystemAlert>(503, "Service temporarily unavailable");
    loop.emit<ChatMessage>("Alice", "Agreed, nice design.");
    loop.emit<SystemAlert>(200, "All systems nominal");

    // Process events for 200 ms (enough to drain the queue)
    loop.run_for(200ms);

    std::cout << "  Pending events after run_for: " << loop.pending_events() << "\n\n";

    // -----------------------------------------------------------------------
    // 6. PROMISE / ASYNC TASK  with then() continuation chaining
    //    Build a pipeline: produce a value -> double it -> convert to string.
    // -----------------------------------------------------------------------
    std::cout << "--- [6] Promise / AsyncTask with then() ---\n";

    Promise<int> promise;
    AsyncTask<int> task = promise.get_task();

    // Chain two continuations: int -> int -> string
    AsyncTask<std::string> final_task =
        task
            .then([](int v) -> int
                  {
                safe_print("  [then1] received ", v, ", doubling...");
                return v * 2; })
            .then([](int v) -> std::string
                  {
                safe_print("  [then2] received ", v, ", converting to string...");
                return "result=" + std::to_string(v); });

    // Fulfill the promise from another thread (simulating async work)
    std::thread([&promise]()
                {
        std::this_thread::sleep_for(50ms);
        promise.set_value(21); })
        .detach();

    // Block until the entire chain completes
    std::string chain_result = final_task.get();
    std::cout << "  Chain output: " << chain_result << "\n\n";

    // -----------------------------------------------------------------------
    // 7. SCHEDULER
    //    Schedule a one-shot task (fires after 100 ms) and a periodic task
    //    (fires every 150 ms). Let them run for about 600 ms, then shut down.
    // -----------------------------------------------------------------------
    std::cout << "--- [7] Scheduler ---\n";

    Scheduler scheduler;
    scheduler.start();

    std::atomic<int> periodic_count{0};

    // One-shot: runs once after 100 ms
    scheduler.schedule_once(100ms, []()
                            { safe_print("  [sched] one-shot fired!"); });

    // Periodic: runs every 150 ms
    ScheduleId periodic_id = scheduler.schedule_periodic(150ms, 150ms, [&]()
                                                         {
        int n = periodic_count.fetch_add(1, std::memory_order_relaxed) + 1;
        safe_print("  [sched] periodic tick #", n); });

    // Let the scheduler run for ~600 ms
    std::this_thread::sleep_for(600ms);

    scheduler.cancel(periodic_id);
    scheduler.shutdown();

    std::cout << "  Periodic task fired " << periodic_count.load() << " times\n\n";

    // -----------------------------------------------------------------------
    // 8. LATENCY TRACKER with ScopeTimer
    //    Measure the latency of a simulated operation using RAII.
    // -----------------------------------------------------------------------
    std::cout << "--- [8] LatencyTracker::ScopeTimer ---\n";

    for (int i = 0; i < 5; ++i)
    {
        LatencyTracker::ScopeTimer timer(task_lat); // starts measuring
        // Simulate varying work
        std::this_thread::sleep_for(std::chrono::milliseconds(5 + i * 3));
        // ~timer records the elapsed time automatically
    }

    auto snap = task_lat.snapshot();
    std::cout << "  Samples: " << snap.count << "\n";
    std::cout << "  Min:     " << snap.min_ns / 1'000'000 << " ms\n";
    std::cout << "  Max:     " << snap.max_ns / 1'000'000 << " ms\n";
    std::cout << "  Avg:     " << static_cast<int64_t>(snap.avg_ns()) / 1'000'000 << " ms\n\n";

    // -----------------------------------------------------------------------
    // 9. DUMP ALL METRICS
    //    Let pending pool tasks finish, then print the full metrics snapshot.
    // -----------------------------------------------------------------------
    std::cout << "--- [9] Metrics Dump ---\n";

    // Give pool tasks a moment to finish before we read counters
    std::this_thread::sleep_for(100ms);

    auto metrics = registry.dump();
    for (auto &[name, value] : metrics)
    {
        std::cout << "  " << name << " = " << value << "\n";
    }

    // -----------------------------------------------------------------------
    // Clean shutdown
    // -----------------------------------------------------------------------
    std::cout << "\nShutting down thread pool...\n";
    pool.shutdown();

    std::cout << "Done. All components demonstrated successfully.\n";
    return 0;
}
