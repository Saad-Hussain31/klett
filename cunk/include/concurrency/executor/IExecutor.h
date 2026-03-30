#pragma once

/// @file IExecutor.h
/// @brief Abstract interface for task executors.
///
/// Design rationale:
///   A single executor interface allows swapping implementations (thread pool,
///   inline executor, strand) without changing calling code. The submit()
///   method takes a type-erased callable to avoid template pollution in
///   interfaces that depend on the executor.

#include <functional>
#include <future>
#include <type_traits>

namespace conc
{

    /// @brief Interface for submitting tasks for asynchronous execution.
    ///
    /// Thread-safety: submit() is safe to call from any thread concurrently.
    class IExecutor
    {
    public:
        virtual ~IExecutor() = default;

        /// @brief Submit a fire-and-forget task.
        /// @param task Callable to execute. Must be move-constructible.
        virtual void submit(std::function<void()> task) = 0;

        /// @brief Submit a task and get a future for its result.
        /// @tparam F Callable type.
        /// @tparam R Return type of F.
        /// @return std::future<R> that will hold the result.
        ///
        /// Default implementation wraps F in a packaged_task. Subclasses may
        /// override for more efficient implementations.
        template <typename F, typename R = std::invoke_result_t<std::decay_t<F>>>
        std::future<R> submit_async(F &&func)
        {
            auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(func));
            auto future = task->get_future();
            submit([task = std::move(task)]()
                   { (*task)(); });
            return future;
        }

        /// @brief Number of worker threads (0 if not applicable).
        virtual std::size_t worker_count() const = 0;

        /// @brief Number of pending tasks (approximate).
        virtual std::size_t pending_tasks() const = 0;

        /// @brief Initiate graceful shutdown. No new tasks accepted.
        /// Blocks until all pending tasks complete.
        virtual void shutdown() = 0;

        /// @brief Whether shutdown has been initiated.
        virtual bool is_shutdown() const = 0;
    };

} // namespace conc
