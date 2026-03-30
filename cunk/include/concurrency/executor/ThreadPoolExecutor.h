#pragma once

/// @file ThreadPoolExecutor.h
/// @brief Fixed-size thread pool with per-worker queues and work stealing.
///
/// ARCHITECTURE:
///   - N worker threads, each with a private MPMC task queue.
///   - A global overflow queue for when a worker's local queue is full.
///   - submit() round-robins tasks to worker queues (cheap atomic increment).
///   - Workers: (1) pop from own queue, (2) steal from other workers,
///     (3) pop from global queue, (4) wait on condition variable.
///
/// PERFORMANCE RATIONALE:
///   Per-worker queues:
///     - Reduces contention: most pushes/pops are on thread-local queues.
///     - Better cache locality: the worker that pushes a task likely has
///       related data in its L1/L2 cache.
///   Work stealing:
///     - Rebalances load without centralized scheduling.
///     - Stealing from the opposite end of the deque reduces contention
///       with the owning worker.
///   Round-robin submission:
///     - O(1) distribution, no lock on the hot path.
///     - Atomic counter increment is ~1 cycle on x86 (XADD).
///
/// THREAD SAFETY:
///   submit() is safe from any thread. shutdown() blocks until workers exit.
///
/// EDGE CASES HANDLED:
///   - Pool exhaustion: tasks queue up; backpressure via bounded queues.
///   - Task starvation: work stealing ensures idle workers pull from busy ones.
///   - Graceful shutdown: stop flag + notify all; workers drain queues.
///   - Exception safety: exceptions in tasks are caught and swallowed to
///     prevent worker thread death.

#include "IExecutor.h"
#include "../queue/MPMCQueue.h"
#include "../Common.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace conc
{

    /// @brief Configuration for ThreadPoolExecutor.
    struct ThreadPoolConfig
    {
        /// Number of worker threads. 0 = hardware_concurrency.
        std::size_t num_threads = 0;

        /// Per-worker queue capacity (power-of-two).
        std::size_t worker_queue_size = 1024;

        /// Global overflow queue capacity.
        std::size_t global_queue_size = 4096;
    };

    /// @brief Fixed-size thread pool with work stealing.
    class ThreadPoolExecutor final : public IExecutor
    {
    public:
        explicit ThreadPoolExecutor(ThreadPoolConfig config = {});
        ~ThreadPoolExecutor() override;

        ThreadPoolExecutor(const ThreadPoolExecutor &) = delete;
        ThreadPoolExecutor &operator=(const ThreadPoolExecutor &) = delete;

        void submit(std::function<void()> task) override;
        std::size_t worker_count() const override;
        std::size_t pending_tasks() const override;
        void shutdown() override;
        bool is_shutdown() const override;

    private:
        struct Worker
        {
            MPMCQueue<std::function<void()>> queue;
            alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> task_count{0};

            explicit Worker(std::size_t queue_size) : queue(queue_size) {}
        };

        void worker_loop(std::size_t worker_id);
        bool try_execute_from(Worker &w);
        bool try_steal(std::size_t thief_id);

        std::vector<std::unique_ptr<Worker>> workers_;
        std::vector<std::thread> threads_;
        MPMCQueue<std::function<void()>> global_queue_;

        alignas(CACHE_LINE_SIZE) std::atomic<bool> stop_{false};
        alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> next_worker_{0};

        std::mutex wake_mutex_;
        std::condition_variable wake_cv_;
    };

} // namespace conc
