#pragma once

/// @file Scheduler.h
/// @brief Timer-based task scheduling: delayed and periodic execution.
///
/// ARCHITECTURE:
///   A single dedicated thread runs a min-heap priority queue keyed by
///   deadline (std::chrono::steady_clock::time_point). The thread sleeps
///   until the earliest deadline, then executes the task.
///
///   Periodic tasks re-enqueue themselves after execution with the next
///   deadline computed from the previous one (not from "now") to prevent
///   drift accumulation.
///
/// PERFORMANCE RATIONALE:
///   - steady_clock: monotonic, unaffected by wall-clock adjustments.
///   - Priority queue: O(log n) insert, O(1) peek, O(log n) pop.
///   - Single scheduler thread: avoids contention on the heap. External
///     submissions are serialized through a mutex, but this is acceptable
///     because scheduling is a control-plane operation, not data-plane.
///   - For high-frequency timers (sub-millisecond), users should use the
///     executor directly and manage timing in the task itself.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace conc
{

    /// @brief Handle for a scheduled task. Can be used to cancel.
    using ScheduleId = std::uint64_t;

    /// @brief Scheduler for delayed and periodic task execution.
    ///
    /// Thread-safety:
    ///   - schedule_once(), schedule_periodic(), cancel() are safe from any thread.
    ///   - start()/shutdown() should be called from a single managing thread.
    class Scheduler
    {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;
        using Duration = Clock::duration;

        Scheduler();
        ~Scheduler();

        Scheduler(const Scheduler &) = delete;
        Scheduler &operator=(const Scheduler &) = delete;

        /// @brief Schedule a task to run once after a delay.
        /// @return ID that can be passed to cancel().
        ScheduleId schedule_once(Duration delay, std::function<void()> task);

        /// @brief Schedule a task to run repeatedly at a fixed interval.
        /// @param initial_delay Delay before first execution.
        /// @param interval Time between subsequent executions.
        /// @return ID that can be passed to cancel().
        ScheduleId schedule_periodic(Duration initial_delay,
                                     Duration interval,
                                     std::function<void()> task);

        /// @brief Cancel a scheduled task.
        /// @return true if the task was found and cancelled.
        bool cancel(ScheduleId id);

        /// @brief Start the scheduler thread.
        void start();

        /// @brief Stop the scheduler. Pending tasks are discarded.
        void shutdown();

        /// @brief Number of pending scheduled tasks.
        std::size_t pending_count() const;

    private:
        struct ScheduledTask
        {
            ScheduleId id;
            TimePoint deadline;
            Duration interval; // 0 = one-shot
            std::function<void()> task;
            bool cancelled = false;

            // Min-heap: earliest deadline first.
            bool operator>(const ScheduledTask &other) const
            {
                return deadline > other.deadline;
            }
        };

        void scheduler_loop();

        using TaskQueue = std::priority_queue<
            ScheduledTask,
            std::vector<ScheduledTask>,
            std::greater<ScheduledTask>>;

        mutable std::mutex mutex_;
        std::condition_variable cv_;
        TaskQueue queue_;
        std::thread thread_;

        std::atomic<bool> running_{false};
        std::atomic<ScheduleId> next_id_{1};
    };

} // namespace conc
