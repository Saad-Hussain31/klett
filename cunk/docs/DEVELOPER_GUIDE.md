# Developer Guide

This guide covers building the `conc` concurrency framework, using each component, extending the system, and best practices for multithreaded application development.

---

## Table of Contents

1. [Building the Project](#building-the-project)
2. [Using the Queue (MPMCQueue)](#using-the-queue-mpmcqueue)
3. [Using the Backpressure System](#using-the-backpressure-system)
4. [Using the Thread Pool Executor](#using-the-thread-pool-executor)
5. [Using AsyncTask and Promise](#using-asynctask-and-promise)
6. [Using the Event System](#using-the-event-system)
7. [Using the Scheduler](#using-the-scheduler)
8. [Using Metrics](#using-metrics)
9. [Integration Patterns](#integration-patterns)
10. [Extending the Framework](#extending-the-framework)
11. [Best Practices](#best-practices)

---

## Building the Project

### Requirements

- C++17-compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- CMake 3.16+
- pthreads (Linux/macOS) or Windows threads

### Basic Build

```bash
cd concurrency
mkdir build && cd build
cmake ..
cmake --build .
```

### Build Options

```bash
# Build without tests
cmake .. -DCONC_BUILD_TESTS=OFF

# Build without examples
cmake .. -DCONC_BUILD_EXAMPLES=OFF

# Release build with optimizations
cmake .. -DCMAKE_BUILD_TYPE=Release

# Debug build with sanitizers (GCC/Clang)
cmake .. -DCMAKE_BUILD_TYPE=Debug \
         -DCMAKE_CXX_FLAGS="-fsanitize=thread -g"
```

### Running Tests

```bash
cd build
ctest --output-on-failure

# Or run individual tests
./queue_test
./executor_test
./event_test
./async_test
./scheduler_test
./stress_test
```

### Installing

```bash
cmake --install . --prefix /usr/local
```

After installation, consume via CMake:

```cmake
find_package(concurrency REQUIRED)
target_link_libraries(your_target PRIVATE conc::concurrency)
```

### Using as a Subdirectory

```cmake
add_subdirectory(path/to/concurrency)
target_link_libraries(your_target PRIVATE concurrency)
```

---

## Using the Queue (MPMCQueue)

`MPMCQueue<T>` is a lock-free, bounded, multi-producer/multi-consumer ring buffer. It is the foundational data structure used throughout the framework.

### Basic Usage

```cpp
#include <concurrency/queue/MPMCQueue.h>

// Create a queue. Capacity is rounded up to the next power of two.
// Requesting 1000 gives you a queue of 1024 slots.
conc::MPMCQueue<int> queue(1024);

// Producer thread
bool ok = queue.try_push(42);
if (!ok) {
    // Queue is full
}

// Consumer thread
auto result = queue.try_pop();
if (result.has_value()) {
    int value = *result;
    // Process value
}
```

### Querying State

```cpp
std::size_t approx = queue.size_approx();  // Approximate count
bool is_empty = queue.empty();             // Approximate emptiness check
std::size_t cap = queue.capacity();        // Actual capacity (power-of-two)
```

**Important**: `size_approx()` and `empty()` are approximate for lock-free queues. Never use them for correctness decisions (e.g., "if not empty, then pop" is a race condition). Always use the return value of `try_push()`/`try_pop()`.

### Type Requirements

The element type `T` must be nothrow-move-constructible. This is enforced by a `static_assert` at compile time.

```cpp
// Works: int, std::string, std::unique_ptr, std::function
conc::MPMCQueue<std::string> string_queue(256);

// Will not compile: a type with a throwing move constructor
```

---

## Using the Backpressure System

`BackpressuredQueue<T>` wraps any `IQueue<T>` with a configurable overflow policy.

### Drop Strategy

Silently discard items when the queue is full. Best for non-critical data like metrics or telemetry.

```cpp
#include <concurrency/backpressure/BackpressurePolicy.h>
#include <concurrency/queue/MPMCQueue.h>

conc::MPMCQueue<int> raw_queue(64);
conc::BackpressuredQueue<int> queue(raw_queue, {
    .strategy = conc::BackpressureStrategy::Drop
});

queue.push(42);  // Returns true if enqueued, true (but dropped) if full
```

### Block Strategy

Spin-wait until space is available, with configurable timeout.

```cpp
conc::BackpressuredQueue<int> queue(raw_queue, {
    .strategy = conc::BackpressureStrategy::Block,
    .block_timeout = std::chrono::microseconds(5000),  // 5ms timeout
    .spin_count = 128  // Spin 128 times before yielding
});

bool ok = queue.push(42);  // Blocks up to 5ms; returns false on timeout
```

### Reject Strategy

Return `false` immediately. The caller decides what to do.

```cpp
conc::BackpressuredQueue<int> queue(raw_queue, {
    .strategy = conc::BackpressureStrategy::Reject
});

if (!queue.push(42)) {
    // Handle rejection: retry, log, increment error counter, etc.
}
```

### Accessing the Underlying Queue for Pop

```cpp
auto item = queue.queue().try_pop();  // Returns std::optional<int>
```

### Changing Policy at Runtime

```cpp
queue.set_config({
    .strategy = conc::BackpressureStrategy::Drop,
    .block_timeout = std::chrono::microseconds(0)
});
```

---

## Using the Thread Pool Executor

`ThreadPoolExecutor` is a fixed-size thread pool with per-worker queues and work stealing.

### Basic Task Submission

```cpp
#include <concurrency/executor/ThreadPoolExecutor.h>

// Create with default config (hardware_concurrency threads)
conc::ThreadPoolExecutor pool;

// Fire-and-forget task
pool.submit([] {
    // This runs on a worker thread
    heavy_computation();
});
```

### Custom Configuration

```cpp
conc::ThreadPoolConfig config;
config.num_threads = 8;            // 8 worker threads
config.worker_queue_size = 2048;   // Per-worker queue capacity
config.global_queue_size = 8192;   // Global overflow queue capacity

conc::ThreadPoolExecutor pool(config);
```

### Getting Results with submit_async

```cpp
// Submit and get a future
auto future = pool.submit_async([] {
    return expensive_computation();
});

// Do other work...

// Block for the result
int result = future.get();
```

### Querying Pool State

```cpp
std::size_t workers = pool.worker_count();    // Number of threads
std::size_t pending = pool.pending_tasks();   // Approximate pending count
bool stopped = pool.is_shutdown();            // Whether shutdown was called
```

### Shutdown

```cpp
pool.shutdown();  // Blocks until all pending tasks complete
// After shutdown, submit() silently drops tasks
```

The destructor calls `shutdown()` automatically if not already called.

---

## Using AsyncTask and Promise

`AsyncTask<T>` and `Promise<T>` provide a continuation-based future/promise mechanism that supports `.then()` chaining.

### Basic Promise/Task Pattern

```cpp
#include <concurrency/async/AsyncTask.h>

// Producer side
conc::Promise<int> promise;
conc::AsyncTask<int> task = promise.get_task();

// On producer thread (e.g., inside a submitted task)
promise.set_value(42);

// Consumer side
int result = task.get();  // Blocks until value is set
```

### Chaining with then()

```cpp
conc::Promise<int> promise;
auto task = promise.get_task();

// Chain a computation: int -> string
auto string_task = task.then([](int value) {
    return std::to_string(value * 2);
});

// Set the original value
promise.set_value(21);

// Get the chained result
std::string result = string_task.get();  // "42"
```

### Non-Blocking Check

```cpp
if (task.is_ready()) {
    int val = task.get();  // Will not block
}
```

### Error Propagation

```cpp
conc::Promise<int> promise;
auto task = promise.get_task();

// Set an exception instead of a value
try {
    throw std::runtime_error("computation failed");
} catch (...) {
    promise.set_exception(std::current_exception());
}

try {
    task.get();  // Rethrows the exception
} catch (const std::runtime_error& e) {
    // Handle error
}
```

### Void Tasks

```cpp
conc::Promise<void> promise;
auto task = promise.get_task();

auto next = task.then([] {
    return 42;  // void -> int
});

promise.set_value();
int result = next.get();  // 42
```

---

## Using the Event System

The event system provides type-safe, asynchronous event dispatch with handler registration.

### Defining Events

```cpp
#include <concurrency/event/Event.h>
#include <concurrency/event/EventLoop.h>

// Define event types using the CRTP helper
struct UserLoggedIn : conc::Event<UserLoggedIn> {
    std::string username;
    UserLoggedIn(std::string name) : username(std::move(name)) {}
};

struct OrderPlaced : conc::Event<OrderPlaced> {
    int order_id;
    double amount;
    OrderPlaced(int id, double amt) : order_id(id), amount(amt) {}
};
```

### Registering Handlers and Running the Loop

```cpp
conc::EventLoop loop({.queue_size = 4096});

// Register handlers (type-safe, no casting needed)
auto login_handler = loop.on<UserLoggedIn>([](const UserLoggedIn& e) {
    std::cout << "User logged in: " << e.username << "\n";
});

auto order_handler = loop.on<OrderPlaced>([](const OrderPlaced& e) {
    std::cout << "Order " << e.order_id << ": $" << e.amount << "\n";
});

// Start the event loop on a dedicated thread
std::thread loop_thread([&loop] { loop.run(); });
```

### Dispatching Events

```cpp
// From any thread:
loop.dispatch(conc::make_event<UserLoggedIn>("alice"));

// Shorthand:
loop.emit<OrderPlaced>(1001, 99.99);
```

### Removing Handlers

```cpp
loop.off(login_handler);
```

### Processing Modes

```cpp
// Process a single event (non-blocking)
bool processed = loop.run_one();

// Process events for a fixed duration
loop.run_for(std::chrono::milliseconds(100));
```

### Stopping

```cpp
loop.stop();        // Signal stop (safe from any thread)
loop_thread.join(); // Wait for the loop thread to exit
```

---

## Using the Scheduler

The `Scheduler` provides delayed and periodic task execution.

### One-Shot Delayed Tasks

```cpp
#include <concurrency/scheduler/Scheduler.h>

conc::Scheduler scheduler;
scheduler.start();

// Execute once after 500ms
auto id = scheduler.schedule_once(
    std::chrono::milliseconds(500),
    [] { std::cout << "Fired after 500ms\n"; }
);
```

### Periodic Tasks

```cpp
// Execute every 1 second, starting after an initial 2-second delay
auto id = scheduler.schedule_periodic(
    std::chrono::seconds(2),    // initial delay
    std::chrono::seconds(1),    // interval
    [] { std::cout << "Tick\n"; }
);
```

### Cancellation

```cpp
// Note: cancel() is a best-effort operation in the current implementation.
// The task may still fire once after cancellation.
bool cancelled = scheduler.cancel(id);
```

### Querying State

```cpp
std::size_t pending = scheduler.pending_count();
```

### Shutdown

```cpp
scheduler.shutdown();  // Stops the scheduler thread; pending tasks are discarded
```

---

## Using Metrics

The metrics system provides lightweight instrumentation for monitoring.

### Counters

```cpp
#include <concurrency/metrics/Metrics.h>

conc::MetricsRegistry registry;

// Get or create a counter
conc::Counter& tasks_submitted = registry.counter("executor.tasks_submitted");
conc::Counter& tasks_completed = registry.counter("executor.tasks_completed");

tasks_submitted.increment();
tasks_completed.increment();

uint64_t total = tasks_submitted.value();
```

### Gauges

```cpp
conc::Gauge& queue_depth = registry.gauge("executor.queue_depth");

queue_depth.increment();    // +1
queue_depth.decrement();    // -1
queue_depth.set(42);        // Set absolute value

int64_t current = queue_depth.value();
```

### Latency Tracking

```cpp
conc::LatencyTracker& task_latency = registry.latency("executor.task_latency");

// Manual recording
auto start = std::chrono::steady_clock::now();
do_work();
auto end = std::chrono::steady_clock::now();
task_latency.record(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start));

// RAII scope timer (records on destruction)
{
    conc::LatencyTracker::ScopeTimer timer(task_latency);
    do_work();  // Duration is recorded when timer goes out of scope
}

// Read aggregated stats
auto snap = task_latency.snapshot();
// snap.count, snap.min_ns, snap.max_ns, snap.sum_ns, snap.avg_ns()
```

### Exporting Metrics

```cpp
// Simple dump (for logging)
auto pairs = registry.dump();
for (auto& [name, value] : pairs) {
    std::cout << name << " = " << value << "\n";
}

// Custom sink (for Prometheus, StatsD, etc.)
class PrometheusSink : public conc::MetricsSink {
public:
    void on_counter(const std::string& name, uint64_t value) override {
        // Push to Prometheus
    }
    void on_gauge(const std::string& name, int64_t value) override {
        // Push to Prometheus
    }
    void on_latency(const std::string& name,
                    const conc::LatencyTracker::Snapshot& snap) override {
        // Push to Prometheus
    }
};

PrometheusSink sink;
registry.report(sink);
```

---

## Integration Patterns

### Pattern 1: Executor + Event Loop (Task Completion Events)

Use the thread pool for CPU-bound work and the event loop for coordination.

```cpp
struct TaskCompleted : conc::Event<TaskCompleted> {
    int task_id;
    std::string result;
    TaskCompleted(int id, std::string r)
        : task_id(id), result(std::move(r)) {}
};

conc::ThreadPoolExecutor pool;
conc::EventLoop loop;

// Register handler for completion events
loop.on<TaskCompleted>([](const TaskCompleted& e) {
    std::cout << "Task " << e.task_id << " done: " << e.result << "\n";
});

// Start event loop on its own thread
std::thread loop_thread([&loop] { loop.run(); });

// Submit work that emits events on completion
for (int i = 0; i < 100; ++i) {
    pool.submit([i, &loop] {
        std::string result = compute(i);
        loop.emit<TaskCompleted>(i, std::move(result));
    });
}

// Cleanup
pool.shutdown();
loop.stop();
loop_thread.join();
```

### Pattern 2: Scheduler + Executor (Scheduled Task Offloading)

Use the scheduler for timing and the executor for execution, keeping the scheduler thread light.

```cpp
conc::ThreadPoolExecutor pool;
conc::Scheduler scheduler;
scheduler.start();

// Every 5 seconds, submit a batch job to the thread pool
scheduler.schedule_periodic(
    std::chrono::seconds(0),   // Start immediately
    std::chrono::seconds(5),   // Every 5 seconds
    [&pool] {
        pool.submit([] {
            process_batch();   // Runs on a pool worker, not the scheduler thread
        });
    }
);
```

### Pattern 3: Executor + AsyncTask (Pipeline)

Chain async computations across the thread pool.

```cpp
conc::ThreadPoolExecutor pool;

auto stage1 = pool.submit_async([] {
    return fetch_data();  // Returns std::string
});

// Note: submit_async returns std::future, not AsyncTask.
// For then() chaining, use Promise/AsyncTask directly:

conc::Promise<std::string> promise;
auto task = promise.get_task();

pool.submit([p = std::move(promise)]() mutable {
    p.set_value(fetch_data());
});

auto final_task = task
    .then([](std::string data) { return parse(data); })
    .then([](ParsedData pd) { return transform(pd); });

auto result = final_task.get();
```

### Pattern 4: Full Integration (Executor + EventLoop + Scheduler + Metrics)

```cpp
conc::MetricsRegistry metrics;
conc::ThreadPoolExecutor pool({.num_threads = 4});
conc::EventLoop loop;
conc::Scheduler scheduler;

auto& submitted = metrics.counter("tasks.submitted");
auto& completed = metrics.counter("tasks.completed");
auto& latency = metrics.latency("tasks.latency");

struct WorkDone : conc::Event<WorkDone> {
    int id;
    WorkDone(int i) : id(i) {}
};

loop.on<WorkDone>([&](const WorkDone& e) {
    completed.increment();
});

std::thread loop_thread([&] { loop.run(); });
scheduler.start();

// Submit work with metrics
for (int i = 0; i < 1000; ++i) {
    submitted.increment();
    pool.submit([i, &loop, &latency] {
        conc::LatencyTracker::ScopeTimer timer(latency);
        do_work(i);
        loop.emit<WorkDone>(i);
    });
}

// Schedule periodic metrics reporting
scheduler.schedule_periodic(
    std::chrono::seconds(10),
    std::chrono::seconds(10),
    [&metrics] {
        for (auto& [name, value] : metrics.dump()) {
            std::cout << name << " = " << value << "\n";
        }
    }
);

// Cleanup
pool.shutdown();
scheduler.shutdown();
loop.stop();
loop_thread.join();
```

---

## Extending the Framework

### Adding a New Queue Implementation

Implement the `IQueue<T>` interface.

```cpp
#include <concurrency/queue/IQueue.h>

template <typename T>
class SPSCQueue final : public conc::IQueue<T> {
public:
    explicit SPSCQueue(std::size_t capacity);

    bool try_push(T item) override;
    std::optional<T> try_pop() override;
    std::size_t size_approx() const override;
    bool empty() const override;
    std::size_t capacity() const override;
};
```

The new queue works immediately with `BackpressuredQueue`, since `BackpressuredQueue` depends only on the `IQueue<T>` interface.

### Adding a New Executor Implementation

Implement the `IExecutor` interface.

```cpp
#include <concurrency/executor/IExecutor.h>

// An executor that runs tasks on the calling thread (useful for testing)
class InlineExecutor final : public conc::IExecutor {
public:
    void submit(std::function<void()> task) override {
        task();  // Execute immediately
    }

    std::size_t worker_count() const override { return 0; }
    std::size_t pending_tasks() const override { return 0; }
    void shutdown() override { stopped_ = true; }
    bool is_shutdown() const override { return stopped_; }

private:
    bool stopped_ = false;
};
```

This can be swapped in wherever `IExecutor&` is accepted, enabling dependency injection and testing.

### Adding a New Event Type

Define a struct that inherits from `Event<YourType>` using CRTP.

```cpp
struct NetworkPacket : conc::Event<NetworkPacket> {
    std::vector<uint8_t> payload;
    std::string source_ip;
    uint16_t port;

    NetworkPacket(std::vector<uint8_t> data, std::string ip, uint16_t p)
        : payload(std::move(data)), source_ip(std::move(ip)), port(p) {}
};

// Register and use
loop.on<NetworkPacket>([](const NetworkPacket& e) {
    process_packet(e.payload, e.source_ip, e.port);
});

loop.emit<NetworkPacket>(data, "192.168.1.1", 8080);
```

### Adding a Custom Metrics Sink

Implement the `MetricsSink` interface.

```cpp
class JsonFileSink : public conc::MetricsSink {
public:
    explicit JsonFileSink(const std::string& path) : path_(path) {}

    void on_counter(const std::string& name, uint64_t value) override {
        entries_.push_back({name, std::to_string(value)});
    }

    void on_gauge(const std::string& name, int64_t value) override {
        entries_.push_back({name, std::to_string(value)});
    }

    void on_latency(const std::string& name,
                    const conc::LatencyTracker::Snapshot& snap) override {
        entries_.push_back({name + ".avg_ns",
                           std::to_string(static_cast<int64_t>(snap.avg_ns()))});
    }

    void flush() {
        // Write entries_ to JSON file at path_
    }

private:
    std::string path_;
    std::vector<std::pair<std::string, std::string>> entries_;
};
```

---

## Best Practices

### Threading Model

1. **Do not block in submitted tasks.** Blocking a worker thread reduces pool throughput. If you must perform blocking I/O, use a separate thread pool dedicated to I/O.

2. **Keep event handlers fast.** The event loop processes events serially on a single thread. A slow handler delays all subsequent events. Offload heavy work to the executor.

3. **Do not call `shutdown()` from within a submitted task or event handler.** This can deadlock because `shutdown()` joins worker threads, and you are running on one.

4. **Use `schedule_periodic` for timing, then `submit()` for work.** Keep the scheduler thread light; use it only for timing decisions. Delegate actual computation to the thread pool.

### Queue Sizing

1. **Size for burst, not average.** If your average throughput is 10K tasks/sec but you see 50K/sec bursts lasting 100ms, you need at least 5,000 slots of buffer.

2. **Powers of two are free.** Since capacity is rounded up anyway, choose round powers of two (256, 512, 1024, etc.) to avoid wasting the rounded-up space.

3. **Monitor queue depth.** Use `size_approx()` or metrics gauges to track queue fill levels. Sustained high fill indicates you need more consumers or a larger buffer.

### Error Handling

1. **Tasks should catch their own exceptions.** While the framework catches and swallows exceptions to prevent worker death, this means exceptions are silently lost. Catch within the task, log, and increment an error metric.

2. **Use `AsyncTask` for error propagation.** `Promise::set_exception()` and `AsyncTask::get()` properly propagate exceptions to the consumer.

### Performance

1. **Prefer `Task` over `std::function<void()>` for hot paths.** `Task` has a guaranteed 48-byte SBO, avoiding heap allocation for most lambdas. However, the current `ThreadPoolExecutor` uses `std::function<void()>` for compatibility.

2. **Avoid capturing large objects in lambdas.** Lambdas larger than 48 bytes (approximately 6 captured pointers) will trigger heap allocation in `Task`, and always heap-allocate inside `std::function`.

3. **Batch small work items.** Submitting millions of tiny tasks incurs per-task overhead (queue push, cache miss on steal, condition variable wakeup). Group small items into batches when possible.

4. **Use `BackpressureStrategy::Reject` for latency-sensitive paths.** `Block` introduces unpredictable latency. `Reject` lets the caller make an immediate decision.
