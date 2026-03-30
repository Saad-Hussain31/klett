#pragma once

/// @file AsyncTask.h
/// @brief Lightweight async task with continuation (then) support.
///
/// DESIGN RATIONALE:
///   std::future lacks continuation support (no .then()). This AsyncTask
///   provides:
///   - submit_async(): returns AsyncTask<T> that wraps the result.
///   - then(): chains a continuation that runs when the result is ready.
///   - get(): blocks for the result (like std::future::get).
///
///   Internally uses shared state (SharedState<T>) with a condition variable
///   for blocking get() and an optional continuation stored atomically.
///
/// TRADE-OFFS:
///   - SharedState is heap-allocated (std::shared_ptr) because the lifetime
///     must span both the producer and consumer, which may live on different
///     threads with different lifetimes.
///   - We use a simple mutex+cv rather than lock-free state machine because:
///     (a) get() is typically called once per task (not hot path),
///     (b) the continuation fast path (result already ready) avoids the mutex.
///
///   vs std::future:
///     + Supports then() continuations.
///     + Avoids std::packaged_task overhead for simple cases.
///     - Does not support shared_future semantics (single consumer).

#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>

namespace conc
{

    namespace detail
    {

        /// @brief Shared state between a promise and its corresponding async task.
        template <typename T>
        struct SharedState
        {
            std::mutex mutex;
            std::condition_variable cv;
            std::optional<T> value;
            std::exception_ptr exception;
            std::atomic<bool> ready{false};
            std::function<void(T)> continuation;
        };

        /// Specialization for void.
        template <>
        struct SharedState<void>
        {
            std::mutex mutex;
            std::condition_variable cv;
            std::exception_ptr exception;
            std::atomic<bool> ready{false};
            std::function<void()> continuation;
        };

    } // namespace detail

    // Forward declaration.
    template <typename T>
    class Promise;

    /// @brief Async task handle representing a future result.
    /// @tparam T Result type.
    ///
    /// Thread-safety: get() and then() should be called from a single consumer.
    /// The result is set from the producer thread via the associated Promise.
    template <typename T>
    class AsyncTask
    {
    public:
        AsyncTask() = default;

        /// @brief Block until the result is available and return it.
        /// @throws Rethrows any exception set by the producer.
        T get()
        {
            auto &s = *state_;
            std::unique_lock lock(s.mutex);
            s.cv.wait(lock, [&]
                      { return s.ready.load(std::memory_order_acquire); });
            if (s.exception)
            {
                std::rethrow_exception(s.exception);
            }
            return std::move(*s.value);
        }

        /// @brief Check if the result is ready (non-blocking).
        bool is_ready() const
        {
            return state_ && state_->ready.load(std::memory_order_acquire);
        }

        /// @brief Chain a continuation that runs when the result is ready.
        /// @tparam F Callable taking T and returning U.
        /// @return AsyncTask<U> for the continuation result.
        ///
        /// If the result is already available, the continuation runs immediately
        /// on the calling thread. Otherwise, it runs on the producer's thread
        /// when the result is set.
        template <typename F, typename U = std::invoke_result_t<F, T>>
        AsyncTask<U> then(F &&func)
        {
            auto next = Promise<U>();
            auto next_task = next.get_task();
            auto &s = *state_;

            std::unique_lock lock(s.mutex);
            if (s.ready.load(std::memory_order_acquire))
            {
                // Already ready - run continuation immediately.
                lock.unlock();
                try
                {
                    if constexpr (std::is_void_v<U>)
                    {
                        func(std::move(*s.value));
                        next.set_value();
                    }
                    else
                    {
                        next.set_value(func(std::move(*s.value)));
                    }
                }
                catch (...)
                {
                    next.set_exception(std::current_exception());
                }
            }
            else
            {
                // Not ready - store continuation for later execution.
                s.continuation = [n = std::move(next), f = std::forward<F>(func)](T value) mutable
                {
                    try
                    {
                        if constexpr (std::is_void_v<U>)
                        {
                            f(std::move(value));
                            n.set_value();
                        }
                        else
                        {
                            n.set_value(f(std::move(value)));
                        }
                    }
                    catch (...)
                    {
                        n.set_exception(std::current_exception());
                    }
                };
            }

            return next_task;
        }

    private:
        friend class Promise<T>;
        explicit AsyncTask(std::shared_ptr<detail::SharedState<T>> state)
            : state_(std::move(state)) {}

        std::shared_ptr<detail::SharedState<T>> state_;
    };

    /// Specialization for void.
    template <>
    class AsyncTask<void>
    {
    public:
        AsyncTask() = default;

        void get()
        {
            auto &s = *state_;
            std::unique_lock lock(s.mutex);
            s.cv.wait(lock, [&]
                      { return s.ready.load(std::memory_order_acquire); });
            if (s.exception)
            {
                std::rethrow_exception(s.exception);
            }
        }

        bool is_ready() const
        {
            return state_ && state_->ready.load(std::memory_order_acquire);
        }

        template <typename F, typename U = std::invoke_result_t<F>>
        AsyncTask<U> then(F &&func)
        {
            auto next = Promise<U>();
            auto next_task = next.get_task();
            auto &s = *state_;

            std::unique_lock lock(s.mutex);
            if (s.ready.load(std::memory_order_acquire))
            {
                lock.unlock();
                try
                {
                    if constexpr (std::is_void_v<U>)
                    {
                        func();
                        next.set_value();
                    }
                    else
                    {
                        next.set_value(func());
                    }
                }
                catch (...)
                {
                    next.set_exception(std::current_exception());
                }
            }
            else
            {
                s.continuation = [n = std::move(next), f = std::forward<F>(func)]() mutable
                {
                    try
                    {
                        if constexpr (std::is_void_v<U>)
                        {
                            f();
                            n.set_value();
                        }
                        else
                        {
                            n.set_value(f());
                        }
                    }
                    catch (...)
                    {
                        n.set_exception(std::current_exception());
                    }
                };
            }
            return next_task;
        }

    private:
        friend class Promise<void>;
        explicit AsyncTask(std::shared_ptr<detail::SharedState<void>> state)
            : state_(std::move(state)) {}

        std::shared_ptr<detail::SharedState<void>> state_;
    };

    /// @brief Producer-side handle to set the result of an AsyncTask.
    /// @tparam T Result type.
    template <typename T>
    class Promise
    {
    public:
        Promise() : state_(std::make_shared<detail::SharedState<T>>()) {}

        /// @brief Get the consumer-side handle.
        AsyncTask<T> get_task()
        {
            return AsyncTask<T>(state_);
        }

        /// @brief Set the result value. Wakes any blocked get() calls and
        /// runs the continuation (if any) on this thread.
        void set_value(T value)
        {
            auto &s = *state_;
            std::function<void(T)> cont;
            {
                std::lock_guard lock(s.mutex);
                s.value.emplace(std::move(value));
                s.ready.store(true, std::memory_order_release);
                cont = std::move(s.continuation);
            }
            s.cv.notify_all();
            if (cont)
            {
                cont(std::move(*s.value));
            }
        }

        /// @brief Set an exception.
        void set_exception(std::exception_ptr ex)
        {
            auto &s = *state_;
            {
                std::lock_guard lock(s.mutex);
                s.exception = ex;
                s.ready.store(true, std::memory_order_release);
            }
            s.cv.notify_all();
        }

    private:
        std::shared_ptr<detail::SharedState<T>> state_;
    };

    /// Specialization for void.
    template <>
    class Promise<void>
    {
    public:
        Promise() : state_(std::make_shared<detail::SharedState<void>>()) {}

        AsyncTask<void> get_task()
        {
            return AsyncTask<void>(state_);
        }

        void set_value()
        {
            auto &s = *state_;
            std::function<void()> cont;
            {
                std::lock_guard lock(s.mutex);
                s.ready.store(true, std::memory_order_release);
                cont = std::move(s.continuation);
            }
            s.cv.notify_all();
            if (cont)
            {
                cont();
            }
        }

        void set_exception(std::exception_ptr ex)
        {
            auto &s = *state_;
            {
                std::lock_guard lock(s.mutex);
                s.exception = ex;
                s.ready.store(true, std::memory_order_release);
            }
            s.cv.notify_all();
        }

    private:
        std::shared_ptr<detail::SharedState<void>> state_;
    };

} // namespace conc
