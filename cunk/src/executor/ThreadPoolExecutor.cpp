/// @file ThreadPoolExecutor.cpp
/// @brief Implementation of the work-stealing thread pool.

#include "concurrency/executor/ThreadPoolExecutor.h"
#include <cassert>

namespace conc
{

    ThreadPoolExecutor::ThreadPoolExecutor(ThreadPoolConfig config)
        : global_queue_(config.global_queue_size)
    {
        std::size_t n = config.num_threads;
        if (n == 0)
        {
            n = std::thread::hardware_concurrency();
            if (n == 0)
                n = 4; // fallback
        }

        workers_.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            workers_.push_back(std::make_unique<Worker>(config.worker_queue_size));
        }

        threads_.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            threads_.emplace_back(&ThreadPoolExecutor::worker_loop, this, i);
        }
    }

    ThreadPoolExecutor::~ThreadPoolExecutor()
    {
        if (!stop_.load(std::memory_order_relaxed))
        {
            shutdown();
        }
    }

    void ThreadPoolExecutor::submit(std::function<void()> task)
    {
        if (stop_.load(std::memory_order_relaxed))
        {
            return; // Reject after shutdown
        }

        // Round-robin to worker queues. The atomic increment is a single XADD
        // instruction on x86, costing ~1 cycle uncontended.
        std::size_t idx = next_worker_.fetch_add(1, std::memory_order_relaxed) % workers_.size();

        if (workers_[idx]->queue.try_push(std::move(task)))
        {
            workers_[idx]->task_count.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            // Worker queue full - try global overflow queue
            if (!global_queue_.try_push(std::move(task)))
            {
                // Both full - task is dropped. In production, this should
                // trigger a backpressure signal or metric increment.
                return;
            }
        }

        // Wake one sleeping worker. The lock is only for the condition_variable
        // protocol; the actual wake is the notify_one() call.
        wake_cv_.notify_one();
    }

    std::size_t ThreadPoolExecutor::worker_count() const
    {
        return threads_.size();
    }

    std::size_t ThreadPoolExecutor::pending_tasks() const
    {
        std::size_t total = global_queue_.size_approx();
        for (auto &w : workers_)
        {
            total += w->queue.size_approx();
        }
        return total;
    }

    void ThreadPoolExecutor::shutdown()
    {
        stop_.store(true, std::memory_order_release);
        wake_cv_.notify_all();

        for (auto &t : threads_)
        {
            if (t.joinable())
            {
                t.join();
            }
        }
    }

    bool ThreadPoolExecutor::is_shutdown() const
    {
        return stop_.load(std::memory_order_acquire);
    }

    void ThreadPoolExecutor::worker_loop(std::size_t worker_id)
    {
        while (!stop_.load(std::memory_order_acquire))
        {
            // 1. Try own queue (highest priority - cache-local data)
            if (try_execute_from(*workers_[worker_id]))
            {
                continue;
            }

            // 2. Try stealing from other workers
            if (try_steal(worker_id))
            {
                continue;
            }

            // 3. Try global queue
            if (auto task = global_queue_.try_pop())
            {
                try
                {
                    (*task)();
                }
                catch (...)
                {
                    // Swallow exceptions to prevent worker death.
                    // In production, log via metrics sink.
                }
                continue;
            }

            // 4. Nothing to do - sleep until notified.
            // We use a timed wait to periodically recheck (prevents missed wakeups
            // in edge cases where notify_one() races with the wait).
            std::unique_lock lock(wake_mutex_);
            wake_cv_.wait_for(lock, std::chrono::microseconds(100), [this]
                              { return stop_.load(std::memory_order_acquire); });
        }

        // Drain remaining tasks during shutdown for graceful completion.
        while (try_execute_from(*workers_[worker_id]))
        {
        }
        while (auto task = global_queue_.try_pop())
        {
            try
            {
                (*task)();
            }
            catch (...)
            {
            }
        }
    }

    bool ThreadPoolExecutor::try_execute_from(Worker &w)
    {
        if (auto task = w.queue.try_pop())
        {
            w.task_count.fetch_sub(1, std::memory_order_relaxed);
            try
            {
                (*task)();
            }
            catch (...)
            {
            }
            return true;
        }
        return false;
    }

    bool ThreadPoolExecutor::try_steal(std::size_t thief_id)
    {
        // Simple round-robin steal attempt. In a more sophisticated implementation,
        // we'd randomize the victim selection to reduce contention.
        std::size_t n = workers_.size();
        for (std::size_t i = 1; i < n; ++i)
        {
            std::size_t victim_id = (thief_id + i) % n;
            if (try_execute_from(*workers_[victim_id]))
            {
                return true;
            }
        }
        return false;
    }

} // namespace conc
