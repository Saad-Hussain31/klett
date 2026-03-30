# API Reference

Complete reference for all public classes, interfaces, functions, and types in the `conc` concurrency framework.

All types are in the `conc` namespace. Header paths are relative to the `include/` directory.

---

## Table of Contents

- [Common Utilities](#common-utilities) -- `concurrency/Common.h`
- [IQueue\<T\>](#iqueuet) -- `concurrency/queue/IQueue.h`
- [MPMCQueue\<T\>](#mpmcqueuet) -- `concurrency/queue/MPMCQueue.h`
- [BackpressureStrategy / BackpressureConfig / BackpressuredQueue\<T\>](#backpressure) -- `concurrency/backpressure/BackpressurePolicy.h`
- [IExecutor](#iexecutor) -- `concurrency/executor/IExecutor.h`
- [Task](#task) -- `concurrency/executor/Task.h`
- [ThreadPoolConfig / ThreadPoolExecutor](#threadpoolexecutor) -- `concurrency/executor/ThreadPoolExecutor.h`
- [Event System (EventBase, Event\<T\>, EventPtr)](#event-system) -- `concurrency/event/Event.h`
- [IEventLoop](#ieventloop) -- `concurrency/event/IEventLoop.h`
- [EventLoopConfig / EventLoop](#eventloop) -- `concurrency/event/EventLoop.h`
- [AsyncTask\<T\>](#asynctaskt) -- `concurrency/async/AsyncTask.h`
- [Promise\<T\>](#promiset) -- `concurrency/async/AsyncTask.h`
- [Scheduler](#scheduler) -- `concurrency/scheduler/Scheduler.h`
- [Metrics (Counter, Gauge, LatencyTracker, MetricsSink, MetricsRegistry)](#metrics) -- `concurrency/metrics/Metrics.h`

---

## Common Utilities

**Header**: `concurrency/Common.h`

### Constants

```cpp
inline constexpr std::size_t CACHE_LINE_SIZE = 64;
```

Hardware cache line size in bytes. Used for `alignas` padding to prevent false sharing. 64 bytes is correct for x86-64 and most ARM64 processors.

### Functions

#### `next_power_of_two`

```cpp
constexpr std::size_t next_power_of_two(std::size_t v);
```

Rounds `v` up to the nearest power of two. Used for ring buffer sizing so that bitmask indexing can replace modulo.

- **Parameters**: `v` -- value to round up.
- **Returns**: Smallest power of two >= `v`. If `v` is 0, returns 0.

#### `is_power_of_two`

```cpp
constexpr bool is_power_of_two(std::size_t v);
```

- **Parameters**: `v` -- value to check.
- **Returns**: `true` if `v` is a power of two and nonzero.

---

## IQueue\<T\>

**Header**: `concurrency/queue/IQueue.h`

Abstract interface for concurrent queues. All methods are safe to call from multiple threads concurrently.

```cpp
template <typename T>
class IQueue {
public:
    virtual ~IQueue() = default;
    virtual bool try_push(T item) = 0;
    virtual std::optional<T> try_pop() = 0;
    virtual std::size_t size_approx() const = 0;
    virtual bool empty() const = 0;
    virtual std::size_t capacity() const = 0;
};
```

### Template Parameters

- `T` -- Element type. Must be move-constructible.

### Methods

#### `try_push`

```cpp
virtual bool try_push(T item) = 0;
```

Attempt to enqueue an element.

- **Parameters**: `item` -- the element to enqueue (moved in).
- **Returns**: `true` if enqueued, `false` if the queue is full.
- **Thread safety**: Safe from multiple threads concurrently.
- **Blocking**: Non-blocking for lock-free implementations.

#### `try_pop`

```cpp
virtual std::optional<T> try_pop() = 0;
```

Attempt to dequeue an element.

- **Returns**: The element if available, `std::nullopt` if the queue is empty.
- **Thread safety**: Safe from multiple threads concurrently.
- **Blocking**: Non-blocking for lock-free implementations.

#### `size_approx`

```cpp
virtual std::size_t size_approx() const = 0;
```

- **Returns**: Approximate number of elements currently in the queue.
- **Note**: For lock-free queues, this value may be stale by the time the caller reads it. Do not use for correctness decisions.

#### `empty`

```cpp
virtual bool empty() const = 0;
```

- **Returns**: `true` if the queue appears empty (approximate).

#### `capacity`

```cpp
virtual std::size_t capacity() const = 0;
```

- **Returns**: Maximum capacity of the queue. 0 indicates an unbounded queue.

---

## MPMCQueue\<T\>

**Header**: `concurrency/queue/MPMCQueue.h`

Lock-free bounded multi-producer/multi-consumer queue implemented as a ring buffer, based on Dmitry Vyukov's design.

```cpp
template <typename T>
class MPMCQueue final : public IQueue<T>;
```

### Template Parameters

- `T` -- Element type. Must be nothrow-move-constructible (enforced by `static_assert`).

### Thread Safety

All methods are safe to call from any number of concurrent producers and consumers.

### Constructor

```cpp
explicit MPMCQueue(std::size_t min_capacity);
```

- **Parameters**: `min_capacity` -- minimum number of slots. Actual capacity is rounded up to the next power of two. Minimum of 2.
- **Postcondition**: `capacity()` returns the actual (rounded-up) capacity.
- **Allocation**: Allocates the slot array once. No further allocations occur.

### Destructor

```cpp
~MPMCQueue() override;
```

Drains remaining elements (calling destructors) and frees the slot array.

### Deleted Operations

```cpp
MPMCQueue(const MPMCQueue&) = delete;
MPMCQueue& operator=(const MPMCQueue&) = delete;
```

### Methods

All methods are inherited from `IQueue<T>` and documented there. The lock-free implementation guarantees:

- `try_push()`: Wait-free in practice (bounded CAS retries under normal load). Returns `false` if the queue is full.
- `try_pop()`: Wait-free in practice. Returns `std::nullopt` if the queue is empty.
- `size_approx()`: Returns `head_ - tail_` (may be momentarily stale).
- `empty()`: Returns `size_approx() == 0`.
- `capacity()`: Returns the power-of-two capacity.

### Usage Example

```cpp
conc::MPMCQueue<int> queue(1024);

// Producer
queue.try_push(42);

// Consumer
if (auto val = queue.try_pop()) {
    process(*val);
}
```

---

## Backpressure

**Header**: `concurrency/backpressure/BackpressurePolicy.h`

### BackpressureStrategy

```cpp
enum class BackpressureStrategy {
    Drop,    // Discard the item silently
    Block,   // Spin-wait (with backoff) until space is available
    Reject   // Return false immediately
};
```

### BackpressureConfig

```cpp
struct BackpressureConfig {
    BackpressureStrategy strategy = BackpressureStrategy::Reject;
    std::chrono::microseconds block_timeout{1000};
    std::size_t spin_count = 64;
};
```

| Field | Type | Default | Description |
|---|---|---|---|
| `strategy` | `BackpressureStrategy` | `Reject` | What to do when the queue is full |
| `block_timeout` | `microseconds` | 1000 (1ms) | Max time to spin in `Block` mode. 0 = spin forever. |
| `spin_count` | `size_t` | 64 | Number of spin iterations (with `PAUSE` hint) before yielding in `Block` mode |

### BackpressuredQueue\<T\>

```cpp
template <typename T>
class BackpressuredQueue;
```

Non-owning wrapper that applies a backpressure policy to any `IQueue<T>`. The caller must ensure the underlying queue outlives this object.

**Thread safety**: Same guarantees as the underlying queue.

#### Constructor

```cpp
BackpressuredQueue(IQueue<T>& queue, BackpressureConfig config);
```

- **Parameters**:
  - `queue` -- reference to the underlying queue (not owned).
  - `config` -- backpressure configuration.

#### `push`

```cpp
bool push(T item);
```

Push with backpressure policy applied.

- **Parameters**: `item` -- element to enqueue (moved in).
- **Returns**: `true` if enqueued. `false` if dropped (Drop), timed out (Block), or rejected (Reject).
- **Behavior by strategy**:
  - `Drop`: Calls `try_push()` once. Returns `true` if enqueued, `true` is NOT returned if dropped (returns `false`).
  - `Block`: Spin phase (up to `spin_count` iterations with CPU PAUSE hint), then yield phase (up to `block_timeout`). Returns `false` on timeout.
  - `Reject`: Calls `try_push()` once, returns the result.

#### `queue`

```cpp
IQueue<T>& queue();
const IQueue<T>& queue() const;
```

Direct access to the underlying queue (for pop operations).

#### `config`

```cpp
const BackpressureConfig& config() const;
```

Returns the current backpressure configuration.

#### `set_config`

```cpp
void set_config(BackpressureConfig config);
```

Update the backpressure configuration at runtime.

### Usage Example

```cpp
conc::MPMCQueue<int> raw(256);
conc::BackpressuredQueue<int> bq(raw, {
    .strategy = conc::BackpressureStrategy::Block,
    .block_timeout = std::chrono::microseconds(5000),
    .spin_count = 128
});

bq.push(42);                       // Push with backpressure
auto item = bq.queue().try_pop();  // Pop from underlying queue
```

---

## IExecutor

**Header**: `concurrency/executor/IExecutor.h`

Abstract interface for task executors.

```cpp
class IExecutor {
public:
    virtual ~IExecutor() = default;
    virtual void submit(std::function<void()> task) = 0;
    template <typename F, typename R = std::invoke_result_t<std::decay_t<F>>>
    std::future<R> submit_async(F&& func);
    virtual std::size_t worker_count() const = 0;
    virtual std::size_t pending_tasks() const = 0;
    virtual void shutdown() = 0;
    virtual bool is_shutdown() const = 0;
};
```

### Thread Safety

`submit()` and `submit_async()` are safe to call from any thread concurrently.

### Methods

#### `submit`

```cpp
virtual void submit(std::function<void()> task) = 0;
```

Submit a fire-and-forget task for asynchronous execution.

- **Parameters**: `task` -- callable to execute. Must be move-constructible.
- **Thread safety**: Safe from any thread.

#### `submit_async`

```cpp
template <typename F, typename R = std::invoke_result_t<std::decay_t<F>>>
std::future<R> submit_async(F&& func);
```

Submit a task and get a future for its result.

- **Template parameters**:
  - `F` -- callable type.
  - `R` -- return type of `F` (deduced automatically).
- **Parameters**: `func` -- callable to execute.
- **Returns**: `std::future<R>` that will hold the result when the task completes.
- **Note**: This is a non-virtual template method. The default implementation wraps `func` in a `std::packaged_task<R()>`.

#### `worker_count`

```cpp
virtual std::size_t worker_count() const = 0;
```

- **Returns**: Number of worker threads. 0 if not applicable.

#### `pending_tasks`

```cpp
virtual std::size_t pending_tasks() const = 0;
```

- **Returns**: Approximate number of pending tasks.

#### `shutdown`

```cpp
virtual void shutdown() = 0;
```

Initiate graceful shutdown. No new tasks accepted. Blocks until all pending tasks complete.

#### `is_shutdown`

```cpp
virtual bool is_shutdown() const = 0;
```

- **Returns**: `true` if `shutdown()` has been called.

---

## Task

**Header**: `concurrency/executor/Task.h`

Type-erased callable with guaranteed small buffer optimization (SBO). Fits in exactly one cache line (64 bytes). Callables up to 48 bytes are stored inline; larger ones are heap-allocated. Move-only; not copyable. One-shot: invoking clears the stored callable.

```cpp
class Task {
public:
    static constexpr std::size_t SMALL_BUFFER_SIZE = 48;

    Task() noexcept;

    template <typename F,
              typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, Task>>>
    Task(F&& f);

    Task(Task&& other) noexcept;
    Task& operator=(Task&& other) noexcept;

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    ~Task();

    void operator()();
    explicit operator bool() const noexcept;
};
```

### Thread Safety

NOT thread-safe. A single `Task` should be owned by one thread at a time. The queue/executor handles the thread-safe handoff between threads.

### Constructor (from callable)

```cpp
template <typename F> Task(F&& f);
```

Construct from any callable. If `sizeof(F) <= 48`, `alignof(F) <= alignof(max_align_t)`, and `F` is nothrow-move-constructible, the callable is stored inline. Otherwise, it is heap-allocated.

### `operator()`

```cpp
void operator()();
```

Invoke the held callable. After invocation, the Task is empty (`operator bool()` returns `false`). Calling on an empty Task is a no-op.

### `operator bool`

```cpp
explicit operator bool() const noexcept;
```

- **Returns**: `true` if a callable is held.

### Usage Example

```cpp
conc::Task task([] { std::cout << "hello\n"; });
if (task) {
    task();  // Prints "hello"; task is now empty
}
// task is empty; calling task() again is a no-op
```

---

## ThreadPoolExecutor

**Header**: `concurrency/executor/ThreadPoolExecutor.h`

Fixed-size thread pool with per-worker queues and work stealing.

### ThreadPoolConfig

```cpp
struct ThreadPoolConfig {
    std::size_t num_threads = 0;
    std::size_t worker_queue_size = 1024;
    std::size_t global_queue_size = 4096;
};
```

| Field | Type | Default | Description |
|---|---|---|---|
| `num_threads` | `size_t` | 0 | Number of worker threads. 0 = `hardware_concurrency()` (fallback: 4). |
| `worker_queue_size` | `size_t` | 1024 | Per-worker MPMC queue capacity (rounded up to power of two). |
| `global_queue_size` | `size_t` | 4096 | Global overflow queue capacity (rounded up to power of two). |

### ThreadPoolExecutor

```cpp
class ThreadPoolExecutor final : public IExecutor;
```

#### Constructor

```cpp
explicit ThreadPoolExecutor(ThreadPoolConfig config = {});
```

Creates the worker threads and their queues. Workers begin executing immediately.

#### Destructor

```cpp
~ThreadPoolExecutor() override;
```

Calls `shutdown()` if not already called.

#### `submit`

```cpp
void submit(std::function<void()> task) override;
```

Submit a task for execution. The task is routed to a worker queue via round-robin. If the target worker's queue is full, the task goes to the global overflow queue. If both are full, the task is dropped.

- **Parameters**: `task` -- callable to execute.
- **Thread safety**: Safe from any thread.
- **Post-shutdown behavior**: Tasks are silently dropped.

#### `worker_count`

```cpp
std::size_t worker_count() const override;
```

- **Returns**: Number of worker threads.

#### `pending_tasks`

```cpp
std::size_t pending_tasks() const override;
```

- **Returns**: Approximate total of tasks across all worker queues plus the global queue.

#### `shutdown`

```cpp
void shutdown() override;
```

Sets the stop flag, wakes all workers, and blocks until all threads join. Workers drain their queues before exiting.

#### `is_shutdown`

```cpp
bool is_shutdown() const override;
```

- **Returns**: `true` after `shutdown()` is called.

### Thread Safety

- `submit()`: Safe from any thread.
- `shutdown()`: Should be called from a single managing thread. Blocks until completion.

### Usage Example

```cpp
conc::ThreadPoolExecutor pool({.num_threads = 4, .worker_queue_size = 2048});

pool.submit([] { do_work(); });

auto future = pool.submit_async([] { return compute(); });
int result = future.get();

pool.shutdown();
```

---

## Event System

**Header**: `concurrency/event/Event.h`

### EventTypeId

```cpp
using EventTypeId = std::uint64_t;
```

Unique identifier for each event type. Generated automatically via `detail::event_type_id<T>()`.

### EventBase

```cpp
class EventBase {
public:
    virtual ~EventBase() = default;
    EventTypeId type_id() const;
protected:
    explicit EventBase(EventTypeId id);
};
```

Base class for all events. Provides runtime type identification without C++ RTTI.

#### `type_id`

```cpp
EventTypeId type_id() const;
```

- **Returns**: The runtime type ID assigned to this event's concrete type.

### Event\<Derived\>

```cpp
template <typename Derived>
class Event : public EventBase;
```

CRTP helper that automatically assigns a unique type ID to each concrete event type.

#### Static method: `type_id`

```cpp
static EventTypeId type_id();
```

- **Returns**: The unique type ID for `Derived`.

#### How to define an event

```cpp
struct MyEvent : conc::Event<MyEvent> {
    int data;
    MyEvent(int d) : data(d) {}
};
```

### EventPtr

```cpp
using EventPtr = std::shared_ptr<EventBase>;
```

Type-erased event wrapper for queue storage. Uses `shared_ptr` for safe multi-handler dispatch.

### make_event

```cpp
template <typename E, typename... Args>
EventPtr make_event(Args&&... args);
```

Helper to create an event.

- **Template parameters**: `E` -- concrete event type.
- **Parameters**: `args` -- forwarded to `E`'s constructor.
- **Returns**: `EventPtr` (i.e., `shared_ptr<EventBase>`) holding the new event.

---

## IEventLoop

**Header**: `concurrency/event/IEventLoop.h`

Abstract interface for event-driven dispatch systems.

```cpp
class IEventLoop {
public:
    virtual ~IEventLoop() = default;

    template <typename E>
    HandlerId on(std::function<void(const E&)> handler);

    virtual void off(HandlerId id) = 0;
    virtual void dispatch(EventPtr event) = 0;

    template <typename E, typename... Args>
    void emit(Args&&... args);

    virtual void run() = 0;
    virtual bool run_one() = 0;
    virtual void run_for(std::chrono::milliseconds duration) = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;
    virtual std::size_t pending_events() const = 0;

protected:
    virtual HandlerId on_impl(EventTypeId type_id, EventHandler handler) = 0;
};
```

### Type Aliases

```cpp
using EventHandler = std::function<void(const EventBase&)>;
using HandlerId = std::uint64_t;
```

### Thread Safety

- `dispatch()` and `emit()`: Safe from any thread.
- `on()` / `off()`: Should be called before `run()` or from within handlers.
- `run()` / `stop()`: Should be called from a single managing thread.

### Methods

#### `on<E>`

```cpp
template <typename E>
HandlerId on(std::function<void(const E&)> handler);
```

Register a handler for events of type `E`.

- **Template parameters**: `E` -- concrete event type (must derive from `Event<E>`).
- **Parameters**: `handler` -- callback invoked when an event of type `E` is dispatched.
- **Returns**: `HandlerId` for deregistration via `off()`.

#### `off`

```cpp
virtual void off(HandlerId id) = 0;
```

Remove a previously registered handler.

- **Parameters**: `id` -- the `HandlerId` returned by `on()`.

#### `dispatch`

```cpp
virtual void dispatch(EventPtr event) = 0;
```

Dispatch an event for asynchronous processing. The event is enqueued and processed by the event loop thread.

- **Parameters**: `event` -- the event to dispatch.
- **Thread safety**: Safe from any thread.

#### `emit<E>`

```cpp
template <typename E, typename... Args>
void emit(Args&&... args);
```

Convenience method: constructs an event of type `E` with the given arguments and dispatches it.

- **Template parameters**: `E` -- concrete event type.
- **Parameters**: `args` -- forwarded to `E`'s constructor.

#### `run`

```cpp
virtual void run() = 0;
```

Start the event loop. Blocks the calling thread until `stop()` is called.

#### `run_one`

```cpp
virtual bool run_one() = 0;
```

Process a single event. Non-blocking if no event is ready.

- **Returns**: `true` if an event was processed, `false` if the queue was empty.

#### `run_for`

```cpp
virtual void run_for(std::chrono::milliseconds duration) = 0;
```

Process events for the given duration, then return.

- **Parameters**: `duration` -- how long to process events.

#### `stop`

```cpp
virtual void stop() = 0;
```

Signal the event loop to stop. Safe to call from any thread.

#### `running`

```cpp
virtual bool running() const = 0;
```

- **Returns**: `true` if the loop is currently running.

#### `pending_events`

```cpp
virtual std::size_t pending_events() const = 0;
```

- **Returns**: Approximate number of events in the queue.

---

## EventLoop

**Header**: `concurrency/event/EventLoop.h`

Concrete event loop implementation with a lock-free event queue and handler dispatch.

### EventLoopConfig

```cpp
struct EventLoopConfig {
    std::size_t queue_size = 4096;
};
```

| Field | Type | Default | Description |
|---|---|---|---|
| `queue_size` | `size_t` | 4096 | Event queue capacity (rounded up to power of two). |

### EventLoop

```cpp
class EventLoop final : public IEventLoop;
```

#### Constructor

```cpp
explicit EventLoop(EventLoopConfig config = {});
```

Creates the event loop with the specified queue capacity.

#### Destructor

```cpp
~EventLoop() override;
```

Calls `stop()` if the loop is running.

#### Deleted Operations

```cpp
EventLoop(const EventLoop&) = delete;
EventLoop& operator=(const EventLoop&) = delete;
```

All methods are inherited from `IEventLoop` and documented there. Implementation details:

- `dispatch()`: Lock-free push into `MPMCQueue<EventPtr>`. If the queue is full, the event is silently dropped.
- `run()`: Dequeues events, matches handlers under a `shared_lock`, invokes handlers outside the lock. Sleeps on a condition variable (1ms timeout) when idle.
- `run_for()`: Same as `run()` but exits after the given duration.
- `process_one()`: Snapshots matching handlers under a reader lock, then invokes them. Exceptions in handlers are caught and swallowed.

### Usage Example

```cpp
struct Ping : conc::Event<Ping> {
    int seq;
    Ping(int s) : seq(s) {}
};

conc::EventLoop loop({.queue_size = 1024});

auto id = loop.on<Ping>([](const Ping& e) {
    std::cout << "Ping " << e.seq << "\n";
});

std::thread t([&loop] { loop.run(); });

loop.emit<Ping>(1);
loop.emit<Ping>(2);

std::this_thread::sleep_for(std::chrono::milliseconds(100));
loop.stop();
t.join();
```

---

## AsyncTask\<T\>

**Header**: `concurrency/async/AsyncTask.h`

Async task handle representing a future result. Supports `.then()` continuations, unlike `std::future`.

```cpp
template <typename T>
class AsyncTask;
```

Specializations exist for `T` and `void`.

### Thread Safety

`get()` and `then()` should be called from a single consumer thread. The result is set from the producer thread via the associated `Promise`.

### Methods

#### `get`

```cpp
T get();       // For AsyncTask<T>
void get();    // For AsyncTask<void>
```

Block until the result is available and return it.

- **Returns**: The result value (for non-void specialization).
- **Throws**: Rethrows any exception set by the producer via `Promise::set_exception()`.
- **Blocking**: Yes, blocks on a condition variable until `ready`.

#### `is_ready`

```cpp
bool is_ready() const;
```

Check if the result is available without blocking.

- **Returns**: `true` if the result (or an exception) has been set.

#### `then`

```cpp
template <typename F, typename U = std::invoke_result_t<F, T>>
AsyncTask<U> then(F&& func);
```

Chain a continuation that runs when the result is ready.

- **Template parameters**:
  - `F` -- callable taking `T` (or no argument for `void` specialization) and returning `U`.
  - `U` -- return type of the continuation (deduced).
- **Parameters**: `func` -- the continuation function.
- **Returns**: `AsyncTask<U>` for the continuation's result.
- **Behavior**:
  - If the result is already available, `func` runs immediately on the calling thread.
  - If not yet available, `func` is stored and runs on the producer's thread when `set_value()` is called.
- **Exception handling**: Exceptions thrown by `func` are captured and propagated to the returned `AsyncTask<U>`.

### Usage Example

```cpp
conc::Promise<int> promise;
auto task = promise.get_task();

auto doubled = task.then([](int v) { return v * 2; });
auto as_string = doubled.then([](int v) { return std::to_string(v); });

promise.set_value(21);

std::string result = as_string.get();  // "42"
```

---

## Promise\<T\>

**Header**: `concurrency/async/AsyncTask.h`

Producer-side handle to set the result of an `AsyncTask`.

```cpp
template <typename T>
class Promise;
```

Specializations exist for `T` and `void`.

### Constructor

```cpp
Promise();
```

Creates a new `Promise` with a fresh `SharedState`.

### Methods

#### `get_task`

```cpp
AsyncTask<T> get_task();
```

Get the consumer-side handle.

- **Returns**: `AsyncTask<T>` that shares state with this promise.
- **Note**: Can be called multiple times; each call returns a new `AsyncTask` sharing the same state (but only one consumer should use `get()` or `then()`).

#### `set_value`

```cpp
void set_value(T value);     // For Promise<T>
void set_value();             // For Promise<void>
```

Set the result value. Wakes any blocked `get()` call and runs the continuation (if registered) on the calling thread.

- **Parameters**: `value` -- the result to store (for non-void specialization).
- **Postcondition**: `AsyncTask::is_ready()` returns `true`.

#### `set_exception`

```cpp
void set_exception(std::exception_ptr ex);
```

Set an exception instead of a value. The exception will be rethrown by `AsyncTask::get()`.

- **Parameters**: `ex` -- exception pointer (typically from `std::current_exception()`).

### Usage Example

```cpp
conc::Promise<std::string> promise;
auto task = promise.get_task();

// On producer thread:
try {
    std::string result = compute();
    promise.set_value(std::move(result));
} catch (...) {
    promise.set_exception(std::current_exception());
}

// On consumer thread:
try {
    std::string value = task.get();
} catch (const std::exception& e) {
    // Handle error
}
```

---

## Scheduler

**Header**: `concurrency/scheduler/Scheduler.h`

Timer-based task scheduling for delayed and periodic execution. Uses a dedicated thread with a min-heap priority queue keyed by deadline.

```cpp
class Scheduler;
```

### Type Aliases

```cpp
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration = Clock::duration;
using ScheduleId = std::uint64_t;
```

### Thread Safety

- `schedule_once()`, `schedule_periodic()`, `cancel()`: Safe from any thread.
- `start()` / `shutdown()`: Should be called from a single managing thread.

### Constructor

```cpp
Scheduler();
```

Creates the scheduler. Does not start the scheduler thread (call `start()` separately).

### Destructor

```cpp
~Scheduler();
```

Calls `shutdown()` if the scheduler is running.

### Methods

#### `schedule_once`

```cpp
ScheduleId schedule_once(Duration delay, std::function<void()> task);
```

Schedule a task to run once after a delay.

- **Parameters**:
  - `delay` -- time from now until the task should execute.
  - `task` -- callable to execute.
- **Returns**: `ScheduleId` that can be passed to `cancel()`.
- **Thread safety**: Safe from any thread.

#### `schedule_periodic`

```cpp
ScheduleId schedule_periodic(Duration initial_delay,
                             Duration interval,
                             std::function<void()> task);
```

Schedule a task to run repeatedly at a fixed interval.

- **Parameters**:
  - `initial_delay` -- delay before first execution.
  - `interval` -- time between subsequent executions. Measured from the previous deadline (not from completion) to prevent drift.
  - `task` -- callable to execute.
- **Returns**: `ScheduleId` that can be passed to `cancel()`.

#### `cancel`

```cpp
bool cancel(ScheduleId id);
```

Cancel a scheduled task.

- **Parameters**: `id` -- the `ScheduleId` returned by `schedule_once` or `schedule_periodic`.
- **Returns**: `false` in the current implementation (cancellation is a known limitation of `std::priority_queue`; the task may still fire).
- **Note**: A production implementation would use a custom heap that supports efficient removal.

#### `start`

```cpp
void start();
```

Start the scheduler thread. No-op if already running.

#### `shutdown`

```cpp
void shutdown();
```

Stop the scheduler. Blocks until the scheduler thread joins. Pending tasks are discarded.

#### `pending_count`

```cpp
std::size_t pending_count() const;
```

- **Returns**: Number of pending scheduled tasks.
- **Thread safety**: Safe from any thread (acquires internal mutex).

### Usage Example

```cpp
conc::Scheduler scheduler;
scheduler.start();

// One-shot: fire after 1 second
auto id1 = scheduler.schedule_once(
    std::chrono::seconds(1),
    [] { std::cout << "One-shot fired\n"; }
);

// Periodic: fire every 500ms, starting after 2s
auto id2 = scheduler.schedule_periodic(
    std::chrono::seconds(2),
    std::chrono::milliseconds(500),
    [] { std::cout << "Periodic tick\n"; }
);

// Later:
scheduler.shutdown();
```

---

## Metrics

**Header**: `concurrency/metrics/Metrics.h`

Lightweight instrumentation for monitoring framework components.

### Counter

```cpp
class Counter;
```

Thread-safe atomic counter. Uses relaxed memory ordering for maximum performance (compiles to plain MOV on x86).

**Thread safety**: All methods are safe from any thread.

#### Methods

| Method | Signature | Description |
|---|---|---|
| `increment` | `void increment(uint64_t n = 1)` | Add `n` to the counter |
| `value` | `uint64_t value() const` | Read current value |
| `reset` | `void reset()` | Set to 0 |

### Gauge

```cpp
class Gauge;
```

Thread-safe gauge (value that can go up and down).

**Thread safety**: All methods are safe from any thread.

#### Methods

| Method | Signature | Description |
|---|---|---|
| `set` | `void set(int64_t v)` | Set to an absolute value |
| `increment` | `void increment(int64_t n = 1)` | Add `n` |
| `decrement` | `void decrement(int64_t n = 1)` | Subtract `n` |
| `value` | `int64_t value() const` | Read current value |

### LatencyTracker

```cpp
class LatencyTracker;
```

Thread-safe latency tracker recording min/max/sum/count.

**Thread safety**: `record()` is safe from any thread (uses lock-free CAS for min/max updates).

#### Methods

##### `record`

```cpp
void record(Duration d);
```

Record a latency sample.

- **Parameters**: `d` -- duration in nanoseconds (`std::chrono::nanoseconds`).

##### `snapshot`

```cpp
Snapshot snapshot() const;
```

Get a point-in-time snapshot of the aggregated statistics.

- **Returns**: `Snapshot` struct with fields:

| Field | Type | Description |
|---|---|---|
| `count` | `uint64_t` | Number of samples recorded |
| `min_ns` | `int64_t` | Minimum latency in nanoseconds |
| `max_ns` | `int64_t` | Maximum latency in nanoseconds |
| `sum_ns` | `int64_t` | Sum of all latencies in nanoseconds |
| `avg_ns()` | `double` | Average latency (`sum_ns / count`) |

##### `reset`

```cpp
void reset();
```

Reset all aggregated values to initial state.

#### LatencyTracker::ScopeTimer

```cpp
class ScopeTimer;
```

RAII scope timer. Records the duration from construction to destruction.

##### Constructor

```cpp
explicit ScopeTimer(LatencyTracker& tracker);
```

- **Parameters**: `tracker` -- the tracker to record to on destruction.

##### Usage

```cpp
conc::LatencyTracker& lat = registry.latency("task.latency");
{
    conc::LatencyTracker::ScopeTimer timer(lat);
    do_work();
}  // Duration recorded automatically here
```

### MetricsSink

```cpp
class MetricsSink {
public:
    virtual ~MetricsSink() = default;
    virtual void on_counter(const std::string& name, uint64_t value) = 0;
    virtual void on_gauge(const std::string& name, int64_t value) = 0;
    virtual void on_latency(const std::string& name,
                            const LatencyTracker::Snapshot& snap) = 0;
};
```

Abstract callback interface for exporting metrics to external monitoring systems (Prometheus, StatsD, etc.).

### MetricsRegistry

```cpp
class MetricsRegistry;
```

Central registry for all framework metrics. Components register named metrics here. Thread-safe.

#### Methods

##### `counter`

```cpp
Counter& counter(const std::string& name);
```

Get or create a named counter.

- **Parameters**: `name` -- unique name for the counter.
- **Returns**: Reference to the `Counter`. The registry owns the counter; the reference is valid for the registry's lifetime.
- **Thread safety**: Safe from any thread (acquires internal mutex).

##### `gauge`

```cpp
Gauge& gauge(const std::string& name);
```

Get or create a named gauge.

- **Parameters**: `name` -- unique name for the gauge.
- **Returns**: Reference to the `Gauge`.

##### `latency`

```cpp
LatencyTracker& latency(const std::string& name);
```

Get or create a named latency tracker.

- **Parameters**: `name` -- unique name for the tracker.
- **Returns**: Reference to the `LatencyTracker`.

##### `report`

```cpp
void report(MetricsSink& sink) const;
```

Export all registered metrics to a sink.

- **Parameters**: `sink` -- the `MetricsSink` to receive all metric values.

##### `dump`

```cpp
std::vector<std::pair<std::string, std::string>> dump() const;
```

Get a snapshot of all metrics as name-value string pairs. Useful for simple logging. For latency trackers, produces separate entries for `.count`, `.avg_ns`, `.min_ns`, `.max_ns`.

- **Returns**: Vector of (name, value) pairs.

### Usage Example

```cpp
conc::MetricsRegistry registry;

auto& submitted = registry.counter("tasks.submitted");
auto& active = registry.gauge("tasks.active");
auto& latency = registry.latency("tasks.latency");

submitted.increment();
active.increment();

{
    conc::LatencyTracker::ScopeTimer timer(latency);
    do_work();
}

active.decrement();

// Dump all metrics
for (auto& [name, value] : registry.dump()) {
    std::cout << name << " = " << value << "\n";
}
```
