#pragma once

/// @file IQueue.h
/// @brief Abstract interface for concurrent queues.
///
/// Design rationale:
/// - try_push/try_pop return bool rather than throwing, keeping the hot path
///   branch-based (no exception overhead). Callers can implement their own
///   backpressure logic on failure.
/// - size() is approximate for lock-free implementations (the value may be
///   stale by the time the caller reads it). This is acceptable for monitoring
///   but must not be used for correctness decisions.

#include <cstddef>
#include <optional>

namespace conc
{

    /// @brief Interface for a concurrent queue.
    /// @tparam T Element type. Must be move-constructible at minimum.
    ///
    /// Thread-safety: All methods are safe to call from multiple threads
    /// concurrently unless documented otherwise.
    template <typename T>
    class IQueue
    {
    public:
        virtual ~IQueue() = default;

        /// @brief Attempt to enqueue an element.
        /// @param item The element to enqueue (moved in).
        /// @return true if the element was enqueued, false if the queue is full.
        /// @note Non-blocking for lock-free implementations.
        virtual bool try_push(T item) = 0;

        /// @brief Attempt to dequeue an element.
        /// @return The element if available, std::nullopt otherwise.
        /// @note Non-blocking for lock-free implementations.
        virtual std::optional<T> try_pop() = 0;

        /// @brief Approximate number of elements currently in the queue.
        /// @note For lock-free queues this is an estimate and may be stale.
        virtual std::size_t size_approx() const = 0;

        /// @brief Check if the queue is empty (approximate).
        virtual bool empty() const = 0;

        /// @brief Maximum capacity (0 = unbounded).
        virtual std::size_t capacity() const = 0;
    };

} // namespace conc
