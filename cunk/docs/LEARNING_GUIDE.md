# Learning Guide

This guide explains the core concepts behind the `conc` concurrency framework in beginner-friendly terms. No prior concurrency experience is required. Each section builds on the previous one, so reading top to bottom is recommended.

---

## Table of Contents

1. [What Is a Thread Pool?](#what-is-a-thread-pool)
2. [What Is Lock-Free Programming?](#what-is-lock-free-programming)
3. [How Does a Ring Buffer Work?](#how-does-a-ring-buffer-work)
4. [What Is False Sharing and Why Does It Matter?](#what-is-false-sharing-and-why-does-it-matter)
5. [What Is Work Stealing?](#what-is-work-stealing)
6. [Why Are Event-Driven Systems Used?](#why-are-event-driven-systems-used)
7. [How This System Works Internally](#how-this-system-works-internally)
8. [Glossary of Key Terms](#glossary-of-key-terms)

---

## What Is a Thread Pool?

### The Problem

Imagine you run a restaurant. Every time a customer places an order, you hire a new chef, they cook the dish, and then you fire them. Hiring and firing takes time and paperwork (overhead). If 100 orders come in at once, you have 100 chefs crammed into one kitchen bumping into each other (resource exhaustion).

Creating a thread is like hiring a chef. The operating system must allocate a stack (typically 1-8 MB of memory), set up CPU register state, and add the thread to the scheduler. Destroying it reverses all of that. On Linux, creating a thread takes roughly 10-50 microseconds. If your task itself takes 1 microsecond, you are spending 10-50x more time on overhead than on actual work.

### The Solution

A **thread pool** is a fixed set of worker threads that are created once and reused for many tasks. Instead of creating a thread per task, you submit tasks to a queue, and idle workers pick them up.

```
                     Task Queue
                   +---+---+---+---+
   submit(task) -->| T | T | T | T |---> Worker 1 picks a task, executes it
                   +---+---+---+---+     Worker 2 picks a task, executes it
                                         Worker 3 picks a task, executes it
                                         Worker 4 picks a task, executes it
                                         (then they loop back for more)
```

**Benefits**:
- **No per-task creation overhead**: Threads are created once, at startup.
- **Bounded resource usage**: You control exactly how many threads exist. No risk of creating 10,000 threads and exhausting OS resources.
- **Better cache behavior**: A worker thread that repeatedly processes similar tasks keeps relevant data hot in its CPU cache.

### In This Framework

`ThreadPoolExecutor` creates N worker threads (defaults to your CPU's core count). Each worker has its own private queue. When you call `submit(task)`, the task is placed into one of the worker queues via round-robin distribution. Workers continuously pop tasks from their queue and execute them.

---

## What Is Lock-Free Programming?

### The Traditional Approach: Locks (Mutexes)

When two threads need to access the same data, the simplest solution is a **mutex** (mutual exclusion lock). A thread locks the mutex before accessing the data and unlocks it afterward. If another thread tries to lock while it is held, that thread sleeps until the lock is released.

```
Thread A               Thread B
   |                      |
   | lock(mutex)          |
   | [acquired]           | lock(mutex)
   | write data           | [blocked - sleeping]
   | unlock(mutex)        |
   |                      | [wakes up - acquired]
   |                      | read data
   |                      | unlock(mutex)
```

**Problems with locks**:
- **Blocking**: A sleeping thread does zero useful work while waiting.
- **Context switch overhead**: Putting a thread to sleep and waking it up costs thousands of CPU cycles (the OS must save/restore register state, flush caches, etc.).
- **Priority inversion**: A high-priority thread can be stuck waiting for a low-priority thread that holds the lock.
- **Deadlocks**: If Thread A holds Lock 1 and waits for Lock 2, while Thread B holds Lock 2 and waits for Lock 1, both are stuck forever.

### The Lock-Free Approach: Atomic Operations

Instead of locks, lock-free programming uses **atomic operations** -- special CPU instructions that read-modify-write a memory location in a single, indivisible step. The most important one is **Compare-And-Swap (CAS)**:

```
CAS(memory_location, expected_value, new_value):
    atomically:
        if *memory_location == expected_value:
            *memory_location = new_value
            return true
        else:
            return false  (and tell the caller what the actual value was)
```

This is a single CPU instruction (`CMPXCHG` on x86). No thread ever sleeps. If the CAS fails (because another thread modified the value first), the thread simply retries with the updated value. This is called a **CAS retry loop**.

**Benefits**:
- **No blocking**: Threads never sleep. Even under contention, every thread is making progress or retrying.
- **No deadlocks**: There are no locks to hold in a conflicting order.
- **Predictable latency**: No context switch overhead from sleeping/waking.

**Trade-offs**:
- **Harder to reason about**: You must carefully consider memory ordering (when does a write by Thread A become visible to Thread B?).
- **CAS retries under extreme contention**: If 64 threads CAS the same variable, most will fail and retry, wasting CPU cycles. (In practice, the design of this framework avoids this by giving each worker its own queue.)

### In This Framework

`MPMCQueue` is entirely lock-free. Producers and consumers coordinate using CAS operations on atomic indices (`head_` and `tail_`) and per-slot atomic sequence numbers. No mutex is ever acquired on the hot path.

---

## How Does a Ring Buffer Work?

### The Concept

A **ring buffer** (or circular buffer) is a fixed-size array where the indices wrap around. When you reach the end, you go back to the beginning. Think of it as an array bent into a circle.

```
Capacity = 8, indices 0-7

  Write position (head) = 5
  Read position (tail) = 2
                                  +---------+
                                  |         |
                         Index:   0  1  2  3  4  5  6  7
                         Data:   [ ][ ][A][B][C][ ][ ][ ]
                                        ^tail    ^head

  Readable: indices 2,3,4 (3 items)
  Writable: indices 5,6,7,0,1 (5 slots)
```

When head reaches 7 and we push another item, it wraps to index 0 (overwriting data that was already read).

### Why Not Just Use an Array and Shift?

In a naive array-based queue, when you pop from the front, you shift all remaining elements left. For N elements, that is O(N) work per pop. A ring buffer pops in O(1): just increment the tail index.

### The Power-of-Two Trick

If the capacity is a power of two (e.g., 1024 = 2^10), you can compute the array index using a bitwise AND instead of modulo:

```
index = position & (capacity - 1)

Example: capacity = 8 (binary: 1000), mask = 7 (binary: 0111)
  position 13: 13 & 7 = 1101 & 0111 = 0101 = 5    (same as 13 % 8)
```

Bitwise AND takes approximately 1 CPU cycle. Integer division (modulo) takes 20-40 cycles. When every operation on the queue uses this index calculation, the savings add up significantly.

### In This Framework

`MPMCQueue` is a ring buffer with power-of-two capacity. The `next_power_of_two()` function in `Common.h` rounds up the requested capacity. The `mask_` field stores `capacity_ - 1` for bitmask indexing. Each slot in the ring buffer has an atomic sequence number that coordinates producers and consumers without locks.

---

## What Is False Sharing and Why Does It Matter?

### How CPU Caches Work (Simplified)

Modern CPUs do not read memory one byte at a time. They read in **cache lines**, which are typically 64 bytes. When a CPU core reads a single integer, it pulls the entire 64-byte block containing that integer into its L1 cache (the fastest, smallest cache closest to the core).

```
Memory:  [........64 bytes........][........64 bytes........]
                Cache Line 0               Cache Line 1
```

### The False Sharing Problem

Suppose two variables, `counter_A` and `counter_B`, happen to sit next to each other in memory, within the same 64-byte cache line. Thread 1 (on Core 0) updates `counter_A`. Thread 2 (on Core 1) updates `counter_B`.

Even though they are modifying **different** variables, the CPU cache coherence protocol sees that the same cache line was modified. It invalidates Core 1's copy of the cache line and forces it to re-fetch from a shared cache level (L3 or main memory). Then when Thread 2 writes `counter_B`, it invalidates Core 0's copy. This bouncing happens on every write and is called **false sharing**.

```
Without padding:
  [counter_A][counter_B]   <-- same cache line!

  Core 0 writes counter_A -> invalidates Core 1's cache line
  Core 1 writes counter_B -> invalidates Core 0's cache line
  (Ping-pong on every write, ~100 cycles per access instead of ~4)

With padding:
  [counter_A][...padding...][counter_B][...padding...]
   <--- cache line 0 --->   <--- cache line 1 --->

  Core 0 writes counter_A -> only affects cache line 0
  Core 1 writes counter_B -> only affects cache line 1
  (No interference, ~4 cycles per access)
```

The performance difference can be **10-100x** for hot variables under contention.

### In This Framework

The framework uses `alignas(CACHE_LINE_SIZE)` (where `CACHE_LINE_SIZE = 64`) on every hot atomic variable:

- `MPMCQueue::head_` and `MPMCQueue::tail_` -- on separate cache lines so producers and consumers do not interfere.
- `ThreadPoolExecutor::stop_` and `ThreadPoolExecutor::next_worker_` -- on separate cache lines.
- Every `Counter::value_`, `Gauge::value_`, and every field in `LatencyTracker` -- individually padded so concurrent metric updates from different threads do not thrash each other.

---

## What Is Work Stealing?

### The Problem with a Single Shared Queue

If all worker threads pop from the same queue, every pop operation contends with every other worker. With 16 workers, you have 16 threads CAS-ing on the same tail index. Most CAS operations fail and retry, wasting CPU time.

### The Solution: Per-Worker Queues

Give each worker its own queue. When you submit a task, put it in one worker's queue (round-robin). Now each worker mostly accesses its own queue, and there is very little contention.

But what if Worker 3 has 50 tasks and Workers 0, 1, 2 have none? Workers 0-2 would sit idle while Worker 3 is overloaded.

### Work Stealing

**Work stealing** solves the imbalance. When a worker's own queue is empty, it looks at other workers' queues and "steals" a task.

```
Before stealing:          After stealing:

Worker 0: []              Worker 0: [T]   <-- stolen from Worker 2
Worker 1: []              Worker 1: [T]   <-- stolen from Worker 2
Worker 2: [T][T][T][T]    Worker 2: [T][T]
Worker 3: []              Worker 3: []
```

**Why it works well**:
- In the common case (balanced load), each worker processes its own queue with minimal contention.
- In the unbalanced case, idle workers automatically rebalance by stealing.
- No centralized scheduler is needed. The balancing is fully decentralized.

### In This Framework

`ThreadPoolExecutor::worker_loop()` follows this priority:

1. Pop from own queue (fast: cache-local, low contention).
2. Steal from other workers (round-robin victim selection).
3. Pop from the global overflow queue.
4. Sleep on a condition variable (100-microsecond timeout).

`try_steal()` iterates over all other workers starting from `(thief_id + 1) % N`, checking each worker's queue for a stealable task.

---

## Why Are Event-Driven Systems Used?

### The Polling Approach

Imagine a security camera monitor room. The naive approach: a guard walks to Camera 1, checks it, walks to Camera 2, checks it, ..., walks to Camera 100, checks it, then starts over. Most checks will find nothing interesting. This is **polling**: repeatedly checking for something that rarely happens.

In code, polling looks like:

```cpp
while (true) {
    if (has_new_network_data()) process_network();
    if (has_new_file_data())    process_file();
    if (has_user_input())       process_input();
    // Wastes CPU checking things that usually have not changed
}
```

### The Event-Driven Approach

Instead of checking, you set up alerts. Camera 1 sends a notification when it detects motion. You only respond to notifications. No wasted checks.

```cpp
event_loop.on<NetworkData>([](const NetworkData& e) {
    process_network(e);
});
event_loop.on<FileReady>([](const FileReady& e) {
    process_file(e);
});
event_loop.on<UserInput>([](const UserInput& e) {
    process_input(e);
});
event_loop.run();  // Only wakes up when an event arrives
```

**Benefits**:
- **Efficiency**: No CPU time wasted on empty polls.
- **Decoupling**: The producer of an event does not need to know who handles it. Producers call `dispatch()`; consumers register handlers. They do not reference each other.
- **Composability**: You can add new event types and handlers without modifying existing code.
- **Deterministic ordering**: Events are processed in the order they arrive (FIFO from the queue).

### In This Framework

`EventLoop` maintains an `MPMCQueue<EventPtr>` for incoming events and a handler registry (map of event type ID to handler functions). Any thread can call `dispatch()` to enqueue an event (lock-free push). The event loop thread dequeues events and invokes the matching handlers. When no events are available, it sleeps on a condition variable to avoid busy-waiting.

Events are type-safe: you define a struct that inherits from `Event<YourStruct>`, and the CRTP base automatically assigns a unique type ID without using C++ RTTI.

---

## How This System Works Internally

This section walks through the complete lifecycle of a task, from submission to completion, explaining what happens at each step.

### Step 1: Submitting a Task

You call `pool.submit(myLambda)`. Here is what happens:

```cpp
void ThreadPoolExecutor::submit(std::function<void()> task) {
    // 1. Check the stop flag (relaxed atomic load, ~1 cycle)
    if (stop_.load(std::memory_order_relaxed)) return;

    // 2. Pick a worker via round-robin (atomic fetch_add, ~1 cycle on x86)
    size_t idx = next_worker_.fetch_add(1) % workers_.size();

    // 3. Try to push into that worker's queue (lock-free CAS, ~10-50 cycles)
    if (workers_[idx]->queue.try_push(std::move(task))) {
        workers_[idx]->task_count.fetch_add(1);
    } else {
        // Worker queue full - try global overflow queue
        global_queue_.try_push(std::move(task));
    }

    // 4. Wake a sleeping worker (condition variable notify)
    wake_cv_.notify_one();
}
```

Total cost: roughly 50-200 nanoseconds (no locks, no allocation, no syscall in the common path).

### Step 2: Inside the MPMCQueue Push

The `try_push()` in `MPMCQueue` works as follows:

1. Load the current `head_` position (the next slot to write).
2. Look at the slot at `head_ & mask_` (bitmask indexing to the ring buffer position).
3. Check the slot's atomic sequence number:
   - If `sequence == head_position`: the slot is free. CAS `head_` from `position` to `position + 1`. If CAS succeeds, write the data and update the sequence to `position + 1` (signaling consumers that data is ready).
   - If `sequence < head_position`: the queue is full. Return `false`.
   - If `sequence > head_position`: another producer advanced `head_` ahead of us. Reload and retry.

The per-slot sequence number eliminates the ABA problem (where the head wraps around and a CAS succeeds on a stale value) without needing a separate version counter.

### Step 3: Worker Picks Up the Task

Each worker runs `worker_loop()` in a loop:

```
while not stopped:
    1. Try pop from my own queue          <-- hot path, best cache locality
    2. If empty, try stealing from others  <-- cold path, but rebalances load
    3. If nothing to steal, try global     <-- fallback for overflow tasks
    4. If all empty, sleep (100us timeout) <-- avoid busy-waiting
```

When the worker pops a task from its own queue, the data is likely still in its L1/L2 cache (because the round-robin submission assigned it to this worker's queue). This is why per-worker queues are faster than a single shared queue: better cache locality.

### Step 4: Task Execution

The worker calls `task()`. This invokes the lambda (or callable) that was submitted. The framework wraps this in a try/catch to prevent exceptions from killing the worker thread.

### Step 5: Returning a Result (Optional)

If the task was submitted via `submit_async()`, a `std::packaged_task` wraps the callable. When the callable returns, the result is stored in the associated `std::future`. The caller can retrieve it via `future.get()`.

For `AsyncTask`/`Promise`, the flow is:
1. Producer calls `promise.set_value(result)`.
2. `set_value()` stores the result in `SharedState`, sets the `ready` flag, notifies the condition variable, and runs any registered continuation.
3. Consumer calls `task.get()`, which blocks on the condition variable until `ready` is true, then returns the stored value.

### Step 6: Shutdown

When `pool.shutdown()` is called:
1. The `stop_` flag is set to `true` (atomic store with release ordering).
2. All workers are notified via `wake_cv_.notify_all()`.
3. Each worker exits its main loop, then drains its own queue and the global queue (executing remaining tasks).
4. The main thread calls `join()` on each worker thread, blocking until all are done.

---

## Glossary of Key Terms

| Term | Definition |
|---|---|
| **Atomic operation** | A CPU operation that completes in a single, indivisible step. Other threads either see the old value or the new value, never a partial update. Implemented via special CPU instructions like `CMPXCHG` (CAS) and `XADD` (fetch-add). |
| **ABA problem** | A concurrency bug where a value changes from A to B and back to A between two reads. A CAS comparing against A would succeed even though the state changed. Solved in this framework via per-slot sequence numbers. |
| **Backpressure** | A mechanism for a consumer to signal a producer to slow down when it cannot keep up. Without backpressure, the producer can overwhelm the consumer, leading to unbounded memory growth or data loss. |
| **Bounded queue** | A queue with a maximum capacity. Once full, pushes either fail, block, or drop data (depending on the backpressure policy). Prevents unbounded memory growth. |
| **Cache line** | The smallest unit of data transferred between main memory and CPU cache. Typically 64 bytes on modern x86-64 and ARM64 processors. |
| **CAS (Compare-And-Swap)** | An atomic instruction: "If this memory location equals X, set it to Y, else tell me the current value." The foundation of most lock-free algorithms. |
| **Condition variable** | A synchronization primitive that allows threads to sleep until another thread signals them. Avoids busy-waiting but incurs context-switch overhead. |
| **Continuation** | A function that runs after another computation completes. The `.then()` method on `AsyncTask` registers a continuation. |
| **Context switch** | The OS saving the state of one thread (registers, stack pointer, etc.) and loading the state of another. Costs thousands of CPU cycles. Lock-free programming avoids context switches. |
| **CRTP (Curiously Recurring Template Pattern)** | A C++ pattern where a class inherits from a template parameterized by itself: `class Derived : public Base<Derived>`. Used in `Event<T>` to automatically assign type IDs. |
| **Deadlock** | A situation where two or more threads are each waiting for a resource held by another, and none can proceed. The framework avoids deadlocks by never holding two locks simultaneously. |
| **Event loop** | A thread that continuously waits for events and dispatches them to registered handlers. Used for reactive, push-based architectures. |
| **False sharing** | When two threads write to different variables that share a cache line, causing the cache line to bounce between cores. Solved by padding variables to 64-byte alignment. |
| **Fire-and-forget** | A pattern where the caller submits a task and does not wait for or check its result. The opposite of request-response. |
| **Future/Promise** | A pair of objects: the promise is used by the producer to set a result, and the future (or `AsyncTask`) is used by the consumer to retrieve it. Bridges asynchronous computation across threads. |
| **Gauge** | A metric that can go up or down (e.g., current queue depth, active connections). Contrast with a counter, which only goes up. |
| **Lock-free** | A progress guarantee: at least one thread is always making progress, even if other threads are delayed, crashed, or preempted. No thread can block another indefinitely. |
| **Memory ordering** | Rules that control when writes by one thread become visible to other threads. Ranges from `relaxed` (cheapest, fewest guarantees) to `seq_cst` (most expensive, strongest guarantees). This framework uses `acquire`/`release` for correctness and `relaxed` for metrics. |
| **Min-heap** | A data structure where the smallest element is always at the top. Used by the scheduler to efficiently find the task with the earliest deadline. Insert and remove are O(log n). |
| **MPMC (Multi-Producer Multi-Consumer)** | A queue that supports multiple threads pushing and multiple threads popping concurrently. More general (and harder to implement correctly) than SPSC (single-producer single-consumer). |
| **Mutex** | A lock that provides mutual exclusion: only one thread can hold it at a time. Simple but can cause blocking, deadlocks, and priority inversion. |
| **Priority inversion** | When a high-priority thread waits for a lock held by a low-priority thread, while medium-priority threads run freely. The high-priority thread effectively runs at low priority. |
| **Ring buffer** | A fixed-size array used as a circular queue. Read and write positions wrap around when they reach the end. Provides O(1) push and pop without shifting elements. |
| **Round-robin** | A distribution strategy that assigns items to recipients in order: item 0 to recipient 0, item 1 to recipient 1, ..., item N to recipient 0, etc. Simple and fair. |
| **SBO (Small Buffer Optimization)** | Storing small objects inline (within the containing object) instead of heap-allocating them. The `Task` class uses a 48-byte inline buffer for this purpose. Avoids the overhead of `new`/`delete` for common cases. |
| **Shared mutex** | A lock that allows multiple concurrent readers but only one writer. Also called a read-write lock. Used for the event handler registry where reads (handler lookup) are frequent and writes (registration) are rare. |
| **Steady clock** | A monotonic clock that never goes backward (unlike wall-clock time which can be adjusted by NTP or daylight saving). Used by the scheduler for reliable timing. |
| **Wait-free** | A stronger guarantee than lock-free: every thread completes its operation in a bounded number of steps, regardless of other threads. `MPMCQueue` is wait-free under normal load (bounded CAS retries). |
| **Work stealing** | A load-balancing technique where idle threads take (steal) tasks from busy threads' queues. Provides decentralized, automatic load balancing without a central scheduler. |
