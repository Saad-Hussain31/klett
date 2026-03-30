# Architecture

This document describes the internal architecture of the `conc` concurrency framework: how its components interact, the threading model, memory ownership semantics, synchronization boundaries, and the design decisions that ensure correctness and performance.

---

## Table of Contents

1. [High-Level System Architecture](#high-level-system-architecture)
2. [Component Diagram](#component-diagram)
3. [Threading Model](#threading-model)
4. [Task Lifecycle Sequence Diagram](#task-lifecycle-sequence-diagram)
5. [Class Relationship Diagram](#class-relationship-diagram)
6. [Memory Ownership Model](#memory-ownership-model)
7. [Sync vs Async Boundaries](#sync-vs-async-boundaries)
8. [Performance Design Decisions](#performance-design-decisions)
9. [Concurrency Safety: How the Design Avoids Common Pitfalls](#concurrency-safety-how-the-design-avoids-common-pitfalls)
10. [Edge Cases and Mitigations](#edge-cases-and-mitigations)

---

## High-Level System Architecture

The framework is organized into six cooperating subsystems:

| Subsystem | Responsibility |
|---|---|
| **Queue** (`MPMCQueue`) | Lock-free bounded ring buffer for passing work between threads |
| **Executor** (`ThreadPoolExecutor`) | Fixed-size thread pool with per-worker queues and work stealing |
| **Event System** (`EventLoop`) | Asynchronous event dispatch with type-safe handler registration |
| **Scheduler** (`Scheduler`) | Timer-based delayed and periodic task execution |
| **Async** (`AsyncTask` / `Promise`) | Continuation-based future/promise for composing async results |
| **Metrics** (`MetricsRegistry`) | Lightweight atomic counters, gauges, and latency tracking |

**Data flow summary**: External code submits tasks to the `ThreadPoolExecutor`, which distributes them across per-worker `MPMCQueue` instances. Worker threads pop tasks from their own queues, steal from peers when idle, or fall back to a global overflow queue. The `EventLoop` runs on a dedicated thread, dispatching events from its own `MPMCQueue` to registered handlers. The `Scheduler` runs on another dedicated thread, using a min-heap priority queue to fire tasks at their deadlines. `AsyncTask`/`Promise` provide a continuation mechanism that bridges the gap between fire-and-forget submission and result-oriented computation. `MetricsRegistry` is a cross-cutting concern that any component can use to report telemetry.

---

## Component Diagram

```
+----------------------------------------------------------------------+
|                         User / Application Code                      |
+--------+------------------+------------------+-----------------------+
         |                  |                  |
         v                  v                  v
+----------------+  +---------------+  +----------------+
|   IExecutor    |  |  IEventLoop   |  |   Scheduler    |
|  (interface)   |  |  (interface)  |  |                |
+--------+-------+  +-------+-------+  +--------+-------+
         |                  |                    |
         v                  v                    |
+------------------+ +----------------+          |
|ThreadPoolExecutor| |   EventLoop    |          |
|                  | |                |          |
| +----+ +----+    | |  +---------+  |          |
| | W0 | | W1 |... | |  |MPMCQueue|  |   +------+------+
| +--+-+ +--+-+    | |  | (events)|  |   | min-heap    |
|    |      |      | |  +---------+  |   | priority    |
|    v      v      | |               |   | queue       |
| +----+ +----+    | |  handlers_    |   +-------------+
| |MPMC| |MPMC|    | |  registry     |
| |Qw0 | |Qw1 |   | +---------------+
| +----+ +----+    |
|                  |         +--------------------+
| +-------------+  |         |   MetricsRegistry  |
| |MPMCQueue    |  |         |                    |
| |(global      |  |         | Counter  Gauge     |
| | overflow)   |  |         | LatencyTracker     |
| +-------------+  |         | MetricsSink        |
+------------------+         +--------------------+
                                      ^
        All components can register   |
        metrics via the registry -----+

+-----------------------------------------+
|          BackpressuredQueue<T>           |
|   (non-owning wrapper over IQueue<T>)   |
|   Strategies: Drop | Block | Reject     |
+-----------------------------------------+

+-----------------------------------------+
|     AsyncTask<T> / Promise<T>           |
|   Continuation-based future/promise     |
|   SharedState<T> bridges producer and   |
|   consumer across threads               |
+-----------------------------------------+
```

---

## Threading Model

The framework uses three categories of threads:

### 1. Worker Threads (Thread Pool)

- **Count**: Configurable via `ThreadPoolConfig::num_threads`. Defaults to `std::thread::hardware_concurrency()` (falls back to 4).
- **Lifetime**: Created in `ThreadPoolExecutor` constructor, joined in `shutdown()` or destructor.
- **Behavior**: Each worker runs `worker_loop()` which follows a strict priority order:
  1. Pop from its own per-worker `MPMCQueue` (cache-local, lowest contention).
  2. Steal from other workers' queues (round-robin victim selection).
  3. Pop from the global overflow queue.
  4. Sleep on a condition variable with a 100-microsecond timed wait.
- **Shutdown**: Workers drain their own queue and the global queue before exiting.

### 2. Event Loop Thread

- **Count**: One thread (the caller of `EventLoop::run()`).
- **Behavior**: Continuously pops events from an `MPMCQueue<EventPtr>`, looks up matching handlers under a `shared_mutex` reader lock, and invokes them synchronously.
- **Sleep**: Uses a condition variable with a 1-millisecond timed wait when the event queue is empty.

### 3. Scheduler Thread

- **Count**: One dedicated thread, started via `Scheduler::start()`.
- **Behavior**: Runs `scheduler_loop()`, which sleeps until the earliest deadline in the min-heap, then executes the due task. Periodic tasks are re-enqueued before execution to prevent drift.
- **Execution**: Tasks execute outside the scheduler mutex to avoid blocking scheduling of other tasks.

### Task Queues

```
Submission Thread          Worker Threads            Event Loop Thread
      |                   +---+---+---+                   |
      |  submit()         | W0| W1| W2|                   |
      +---round-robin---->|   |   |   |                   |
      |                   +---+---+---+                   |
      |                   Per-worker MPMCQueues           |
      |                         |                         |
      |  (overflow)             | steal                   |
      +---> Global MPMCQueue <--+                         |
      |                                                   |
      |  dispatch()                                       |
      +---> Event MPMCQueue ----> process_one() ----------+
```

---

## Task Lifecycle Sequence Diagram

```
Caller            ThreadPoolExecutor        Worker[i]          MPMCQueue[i]     Result
  |                      |                     |                    |              |
  |  submit(task)        |                     |                    |              |
  |--------------------->|                     |                    |              |
  |                      |                     |                    |              |
  |                      | idx = next_worker_  |                    |              |
  |                      |   .fetch_add(1) % N |                    |              |
  |                      |                     |                    |              |
  |                      | try_push(task)      |                    |              |
  |                      |-------------------------------------------->|           |
  |                      |                     |                 [CAS on head_]    |
  |                      |                     |                    |              |
  |                      |                     |                    | success      |
  |                      |<--------------------------------------------|           |
  |                      |                     |                    |              |
  |                      | wake_cv_            |                    |              |
  |                      |  .notify_one()      |                    |              |
  |                      |-------------------->|                    |              |
  |                      |                     |                    |              |
  |                      |                     | try_pop()          |              |
  |                      |                     |------------------->|              |
  |                      |                     |                 [CAS on tail_]    |
  |                      |                     |<------------------|              |
  |                      |                     |  task              |              |
  |                      |                     |                    |              |
  |                      |                     | task()             |              |
  |                      |                     |------------------------------------>|
  |                      |                     |                    |         [executed]
  |                      |                     |                    |              |


Alternate path: submit_async with AsyncTask

Caller            IExecutor           Worker           Promise         AsyncTask
  |                  |                  |                 |                |
  | submit_async(f)  |                  |                 |                |
  |----------------->|                  |                 |                |
  |  future<R>       |                  |                 |                |
  |<-----------------|                  |                 |                |
  |                  | submit(wrapper)  |                 |                |
  |                  |----------------->|                 |                |
  |                  |                  | wrapper()       |                |
  |                  |                  |----> f() ------>|                |
  |                  |                  |                 | set_value(r)   |
  |                  |                  |                 |--------------->|
  |                  |                  |                 | cv.notify_all  |
  |  get() / then()  |                  |                 |                |
  |---------------------------------------------------------------->  [returns r]
```

---

## Class Relationship Diagram

```
                        +------------------+
                        |   IQueue<T>      |  (abstract template)
                        |------------------|
                        | + try_push(T)    |
                        | + try_pop()      |
                        | + size_approx()  |
                        | + empty()        |
                        | + capacity()     |
                        +--------+---------+
                                 ^
                                 | implements
                                 |
                        +--------+---------+
                        |  MPMCQueue<T>    |  (final)
                        |------------------|
                        | - slots_[]       |
                        | - head_ (atomic) |
                        | - tail_ (atomic) |
                        | - capacity_      |
                        | - mask_          |
                        +------------------+
                                 ^
                                 | wraps (non-owning)
                                 |
                        +--------+------------+
                        | BackpressuredQueue<T>|
                        |---------------------|
                        | - queue_ : IQueue&  |
                        | - config_           |
                        | + push(T)           |
                        +---------------------+


     +------------------+
     |   IExecutor      |  (abstract)
     |------------------|
     | + submit(fn)     |
     | + submit_async() |  (template, non-virtual)
     | + worker_count() |
     | + pending_tasks()|
     | + shutdown()     |
     | + is_shutdown()  |
     +--------+---------+
              ^
              | implements
              |
     +--------+-------------+
     | ThreadPoolExecutor   |  (final)
     |----------------------|
     | - workers_[]         |
     |   +-> Worker         |
     |       +-> MPMCQueue  |  per-worker queue
     |       +-> task_count |  atomic counter
     | - global_queue_      |  MPMCQueue (overflow)
     | - threads_[]         |
     | - stop_ (atomic)     |
     | - next_worker_       |  atomic round-robin counter
     | - wake_mutex_ / cv_  |
     +----------------------+


     +------------------+              +--------------------+
     | EventBase        |              |   IEventLoop       |  (abstract)
     |------------------|              |--------------------|
     | - type_id_       |              | + on<E>(handler)   |  template
     +--------+---------+              | + off(id)          |
              ^                        | + dispatch(event)  |
              | CRTP                   | + emit<E>(args...) |  template
              |                        | + run()            |
     +--------+---------+              | + run_one()        |
     |   Event<Derived> |              | + run_for(dur)     |
     +------------------+              | + stop()           |
                                       +--------+----------+
                                                ^
                                                | implements
                                                |
                                       +--------+----------+
                                       |    EventLoop      |  (final)
                                       |-------------------|
                                       | - queue_: MPMCQueue<EventPtr>
                                       | - handlers_[]     |
                                       | - handlers_mutex_  |  shared_mutex
                                       | - running_, stop_  |  atomics
                                       +-------------------+


     +------------------+         +------------------+
     | AsyncTask<T>     |<------->|   Promise<T>     |
     |------------------|         |------------------|
     | + get()          |         | + get_task()     |
     | + is_ready()     |         | + set_value(T)   |
     | + then(F)        |         | + set_exception()|
     |                  |         |                  |
     | shared_ptr<      |         | shared_ptr<      |
     |  SharedState<T>> |         |  SharedState<T>> |
     +------------------+         +------------------+

     +------------------+         +------------------+
     |    Scheduler     |         |      Task        |
     |------------------|         |------------------|
     | + schedule_once()|         | 64 bytes total   |
     | + schedule_      |         | 48-byte SBO buf  |
     |    periodic()    |         | Ops* vtable      |
     | + cancel(id)     |         | + operator()()   |
     | + start()        |         | + operator bool  |
     | + shutdown()     |         +------------------+
     | + pending_count()|
     +------------------+

     +------------------+         +------------------+
     | MetricsRegistry  |         |  MetricsSink     |  (abstract)
     |------------------|         |------------------|
     | + counter(name)  |         | + on_counter()   |
     | + gauge(name)    |         | + on_gauge()     |
     | + latency(name)  |         | + on_latency()   |
     | + report(sink)   |         +------------------+
     | + dump()         |
     +------------------+
            |
            | owns
            v
     +------+------+------+
     | Counter | Gauge | LatencyTracker |
     +---------+-------+----------------+
```

---

## Memory Ownership Model

### Ownership Rules

| Object | Owner | Lifetime |
|---|---|---|
| `MPMCQueue<T>` slots array | `MPMCQueue` (raw `operator new`) | Constructor to destructor. Drained in destructor. |
| `Worker` objects | `ThreadPoolExecutor` via `unique_ptr` | Constructor to destructor. |
| `std::thread` objects | `ThreadPoolExecutor` / `Scheduler` / caller of `EventLoop::run()` | Joined in `shutdown()` or destructor. |
| `Task` callable storage | `Task` (inline SBO or heap) | Move-only. One-shot: storage destroyed after invocation. |
| `EventPtr` | `shared_ptr<EventBase>` | Reference-counted. Shared between dispatch queue and handlers. |
| `SharedState<T>` | `shared_ptr` held by both `AsyncTask<T>` and `Promise<T>` | Lives until both sides are destroyed. |
| `BackpressuredQueue<T>` | Non-owning reference to `IQueue<T>` | Caller must ensure the queue outlives the wrapper. |
| Metrics objects (`Counter`, `Gauge`, `LatencyTracker`) | `MetricsRegistry` via `unique_ptr` | Registry lifetime. References returned to callers are borrowed. |

### Key Design Decisions

- **No raw `new`/`delete` in user-facing API**: All heap allocations are managed by `unique_ptr`, `shared_ptr`, or the `Task` SBO mechanism.
- **Move-only semantics for tasks**: `Task` and `std::function<void()>` are moved through queues. No copying occurs on the hot path.
- **`EventPtr` uses `shared_ptr`** because a single event may be dispatched to multiple handlers. The reference count is incremented once per handler snapshot, not per dispatch.

---

## Sync vs Async Boundaries

### Synchronous Operations

- **Handler invocation within the EventLoop**: Handlers execute synchronously on the event loop thread. This provides deterministic ordering per event type but means a slow handler blocks subsequent events.
- **Task execution within a worker thread**: The task runs synchronously on the worker. Work stealing occurs only between tasks, not during.
- **Scheduler task execution**: The scheduled task runs on the scheduler thread (outside the mutex). A long-running scheduled task delays other scheduled tasks.

### Asynchronous Boundaries

- **`submit()` to worker execution**: The call to `submit()` returns immediately. The task is enqueued into the MPMC ring buffer and will be picked up asynchronously by a worker thread.
- **`dispatch()` to handler invocation**: The call to `dispatch()` pushes into the event queue and returns. The event loop thread processes it asynchronously.
- **`schedule_once()` / `schedule_periodic()`**: Returns immediately with a `ScheduleId`. The task fires asynchronously on the scheduler thread after the specified delay.
- **`AsyncTask::then()`**: The continuation may run immediately (if the result is already ready) or later on the producer's thread when `set_value()` is called.

### Cross-Thread Communication Mechanisms

| Boundary | Mechanism | Blocking? |
|---|---|---|
| Submitter to Worker | `MPMCQueue` (lock-free CAS) | No |
| Worker to Worker (steal) | `MPMCQueue` (lock-free CAS) | No |
| Dispatcher to EventLoop | `MPMCQueue` (lock-free CAS) | No |
| Scheduler add/cancel | `std::mutex` + `condition_variable` | Yes (briefly) |
| `Promise::set_value()` to `AsyncTask::get()` | `mutex` + `condition_variable` | Yes (`get()` blocks) |
| Handler registration | `shared_mutex` (write lock) | Yes (rare operation) |
| Handler lookup during dispatch | `shared_mutex` (read lock) | No (concurrent reads) |

---

## Performance Design Decisions

### MPMCQueue

| Decision | Rationale | Cost |
|---|---|---|
| Power-of-two capacity | Bitmask indexing (`pos & mask_`) costs ~1 cycle vs ~20-40 cycles for modulo | Up to 2x memory waste in worst case (e.g., request 513, get 1024) |
| Per-slot sequence numbers | Avoids ABA problem without a separate version counter or epoch-based reclamation | 8 extra bytes per slot |
| `head_` and `tail_` on separate cache lines | Eliminates false sharing between producers (CAS on `head_`) and consumers (CAS on `tail_`) | 64 bytes of padding |
| Contiguous slot allocation | CPU prefetcher can predict sequential access patterns; good spatial locality | Fixed at construction time |
| `acquire`/`release` memory ordering | Sufficient for happens-before without full sequential consistency barriers; cheaper on ARM | None vs `seq_cst` |

### ThreadPoolExecutor

| Decision | Rationale | Cost |
|---|---|---|
| Per-worker queues | Reduces contention vs a single shared queue; data stays in the submitting core's cache | N extra queues (N * `worker_queue_size` * element_size) |
| Round-robin submission | O(1) distribution via a single `fetch_add`. No lock contention. | Imperfect load balance if tasks have unequal cost |
| Work stealing | Idle workers pull from busy workers, rebalancing load without centralized coordination | Stealing traverses other workers' queues (cold cache) |
| Global overflow queue | Absorbs bursts when a target worker queue is full | Extra queue + fallback path |
| 100-microsecond timed wait | Prevents missed wakeups from notify/wait races without busy-spinning | Slight latency on idle-to-busy transition |
| Exception swallowing in workers | Prevents a single bad task from killing a worker thread | Exceptions are silently lost (should be logged via metrics) |

### Task (SBO)

| Decision | Rationale | Cost |
|---|---|---|
| 48-byte inline buffer | Fits most lambdas (up to ~6 captured pointers) without heap allocation | 48 bytes per Task even if the callable is smaller |
| 64-byte total size (one cache line) | When Tasks are stored contiguously in a ring buffer, each Task occupies exactly one cache line | Requires careful layout of `storage_` + `ops_` pointer |
| One-shot invocation | `ops_` is set to `nullptr` after `operator()()`, preventing double invocation | Cannot re-invoke |

### EventLoop

| Decision | Rationale | Cost |
|---|---|---|
| Lock-free event queue | `dispatch()` is the hot path; lock-free push avoids blocking dispatchers | Bounded capacity; events dropped if full |
| `shared_mutex` for handler registry | Reads (handler lookup during dispatch) are frequent; writes (registration) are rare | Slightly more complex than plain mutex |
| Handler snapshot before invocation | Handlers are invoked outside the lock, preventing deadlock if a handler calls `on()`/`off()` | One vector copy per event (contains only matched handlers) |

### Scheduler

| Decision | Rationale | Cost |
|---|---|---|
| `steady_clock` | Monotonic; unaffected by wall-clock adjustments (NTP, DST) | Cannot schedule at absolute wall-clock times |
| Min-heap priority queue | O(log n) insert, O(1) peek, O(log n) pop | `std::priority_queue` does not support efficient cancellation |
| Drift-free periodic re-enqueue | Next deadline = previous deadline + interval (not `now() + interval`) | If execution takes longer than interval, tasks pile up |
| Execution outside mutex | Prevents a long-running task from blocking scheduling of other tasks | Task execution is on the scheduler thread (single-threaded) |

### Metrics

| Decision | Rationale | Cost |
|---|---|---|
| Relaxed atomics for all counters | On x86, relaxed atomics compile to plain MOV (no fence). Monitoring data does not need ordering guarantees. | Values may be slightly stale across cores |
| `alignas(CACHE_LINE_SIZE)` on each atomic | Prevents false sharing between counters that may be updated by different threads | 64 bytes per counter/gauge/latency field |
| Lock-free CAS loop for min/max in `LatencyTracker` | Avoids mutex on the recording hot path | CAS retries under extreme contention |

---

## Concurrency Safety: How the Design Avoids Common Pitfalls

### Deadlocks

- **No nested locking**: No component acquires two mutexes simultaneously.
- **EventLoop handler snapshot**: Handlers are copied under a `shared_lock`, then invoked outside the lock. This prevents deadlock if a handler calls `on()`/`off()`.
- **Scheduler executes outside mutex**: `lock.unlock()` is called before `task.task()`, preventing a scheduled task from blocking the scheduler mutex.
- **Lock-free queues on the hot path**: `MPMCQueue` operations never acquire a mutex, so they cannot participate in a deadlock.

### Race Conditions

- **Per-slot sequence numbers** in `MPMCQueue` provide a correct happens-before relationship between producers and consumers without external locking.
- **`acquire`/`release` ordering** on sequence loads/stores ensures that a consumer sees all writes made by the producer before the sequence was updated.
- **Atomic stop flags** with `release`/`acquire` ordering ensure that workers see the stop signal and all preceding writes (e.g., queue state).

### False Sharing

- **`head_` and `tail_`** in `MPMCQueue` are each `alignas(CACHE_LINE_SIZE)` (64 bytes), placing them on separate cache lines. Producers mutate `head_`; consumers mutate `tail_`. Without this padding, both would share a cache line, causing it to bounce between cores on every operation.
- **`stop_` and `next_worker_`** in `ThreadPoolExecutor` are similarly padded.
- **`running_` and `stop_flag_`** in `EventLoop` are padded.
- All metric atomics (`Counter::value_`, `Gauge::value_`, each field in `LatencyTracker`) are individually padded.

### Cache Misses

- **Contiguous slot allocation** in `MPMCQueue`: Slots are allocated as a flat array, so sequential access benefits from hardware prefetching.
- **Per-worker queues** in `ThreadPoolExecutor`: A worker primarily accesses its own queue, which stays hot in its L1/L2 cache. Work stealing is the cold path and only happens when the worker is idle.
- **Task SBO (Small Buffer Optimization)**: Most tasks fit in 48 bytes inline, avoiding a pointer chase to heap memory.
- **Power-of-two capacity**: Bitmask indexing avoids division, which is not only slower but can also cause pipeline stalls.

---

## Edge Cases and Mitigations

### Thread Pool Exhaustion

**Scenario**: All worker queues and the global queue are full. New tasks are submitted faster than they can be consumed.

**Mitigation**:
- `submit()` attempts the target worker's queue first, then falls back to the global overflow queue. If both are full, the task is dropped.
- `BackpressuredQueue` provides configurable strategies: `Drop` (silently discard), `Block` (spin-wait with backoff and timeout), or `Reject` (return false to the caller).
- Metrics can be used to monitor `pending_tasks()` and alert before exhaustion.

### Task Starvation

**Scenario**: One worker's queue is consistently full while others are idle. Tasks assigned to the busy worker experience excessive latency.

**Mitigation**:
- **Work stealing**: Idle workers iterate over all other workers' queues (round-robin starting from `thief_id + 1`). Any task enqueued in a busy worker's queue will eventually be stolen by an idle worker.
- Round-robin submission distributes tasks evenly across workers in the common case.

### Deadlocks in User Tasks

**Scenario**: A user-submitted task blocks on a resource held by another task that is waiting for a worker thread.

**Mitigation**:
- This is a user-level issue (the framework cannot prevent arbitrary blocking in user code). However, the work-stealing design means that other workers can continue executing tasks even if one worker is blocked.
- Documentation recommends that users avoid blocking operations inside submitted tasks, or use a separate thread pool for blocking I/O.

### Priority Inversion

**Scenario**: A low-priority task holds a mutex needed by a high-priority task, while medium-priority tasks run on all available workers.

**Mitigation**:
- The lock-free `MPMCQueue` eliminates mutex-based priority inversion in the framework itself.
- The scheduler's mutex is held only briefly (to insert/pop from the heap), and task execution happens outside the lock.
- For user-level priority inversion, the `BackpressureStrategy::Block` spin phase uses `PAUSE` instructions (x86) or `std::this_thread::yield()` to reduce contention and give other threads a chance to run.

### Queue Overflow

**Scenario**: A producer overwhelms a bounded queue.

**Mitigation**:
- `MPMCQueue::try_push()` returns `false` immediately when full (non-blocking).
- `BackpressuredQueue` wraps any `IQueue<T>` with configurable overflow behavior:
  - `Drop`: Silently discard (best for telemetry/metrics where loss is acceptable).
  - `Block`: Spin phase (bounded iterations with `PAUSE`), then yield phase (up to `block_timeout`), then timeout. Prevents infinite spinning.
  - `Reject`: Return `false` to the caller for explicit error handling.
- The `ThreadPoolExecutor` has a two-tier overflow: worker queue full -> global queue; global queue full -> drop.

### Slow Consumers

**Scenario**: The event loop or a worker thread processes tasks slower than they arrive.

**Mitigation**:
- Bounded queues prevent unbounded memory growth. The queue acts as a natural backpressure signal.
- The `EventLoop` uses a bounded `MPMCQueue` for events. If handlers are slow, the queue fills and `dispatch()` returns without enqueuing (silent drop). Callers should check the return value or use `BackpressuredQueue` for explicit policy.
- For workers, work stealing redistributes load across idle workers.

### Burst Traffic

**Scenario**: A sudden spike of task submissions overwhelms the system.

**Mitigation**:
- The two-tier queue structure (per-worker + global overflow) provides burst absorption. The total buffering capacity is `N * worker_queue_size + global_queue_size`.
- `BackpressuredQueue` with `Block` strategy and a timeout allows producers to ride out short bursts by spin-waiting until space becomes available.
- The bounded queue design means the system degrades gracefully (drops or rejects excess work) rather than consuming unbounded memory.

### Missed Wakeups

**Scenario**: A `notify_one()` is issued between a worker checking the queue (finding it empty) and entering `wait()` on the condition variable.

**Mitigation**:
- Workers use `wait_for()` with a 100-microsecond timeout, not unbounded `wait()`. Even if a notification is missed, the worker will recheck within 100 microseconds.
- The EventLoop uses a 1-millisecond timed wait with a predicate that checks both the stop flag and queue emptiness.

### Exception Safety

**Scenario**: A user task or event handler throws an exception.

**Mitigation**:
- All execution points (`worker_loop`, `process_one` in EventLoop, `scheduler_loop`) wrap task invocation in `try/catch(...)` blocks that swallow exceptions.
- This prevents a single rogue task from killing a worker thread, crashing the event loop, or stopping the scheduler.
- In production, exceptions should be logged via the `MetricsSink` or a counter increment.
