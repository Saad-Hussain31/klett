#pragma once

/// @file BackpressurePolicy.h
/// @brief Configurable strategies for handling queue overflow / system overload.
///
/// DESIGN RATIONALE:
///   Backpressure is critical in any producer-consumer system. Without it,
///   a fast producer and slow consumer leads to unbounded memory growth or
///   data loss. We provide three strategies as a policy enum + a templated
///   wrapper that applies the policy to any IQueue<T>.
///
///   - Drop:  Discard the newest item. O(1), no blocking, data loss.
///            Best for: metrics, telemetry, non-critical updates.
///   - Block: Spin-wait until space is available. No data loss, but
///            increases latency and can cause priority inversion.
///            Best for: reliable message passing where loss is unacceptable.
///   - Reject: Return failure immediately. Caller decides what to do.
///             Best for: systems that need explicit error handling.
///
/// PERFORMANCE:
///   The policy check is a single branch on an enum, which the CPU branch
///   predictor will handle well since policies don't change at runtime.

#include "../queue/IQueue.h"
#include <chrono>
#include <thread>

namespace conc
{

    /// @brief Backpressure strategy when a queue is full.
    enum class BackpressureStrategy
    {
        Drop,  ///< Discard the item silently.
        Block, ///< Spin-wait (with backoff) until space is available.
        Reject ///< Return false immediately; caller handles the failure.
    };

    /// @brief Configuration for backpressure behavior.
    struct BackpressureConfig
    {
        BackpressureStrategy strategy = BackpressureStrategy::Reject;

        /// Maximum time to spin in Block mode before giving up.
        /// 0 = spin forever (use with caution).
        std::chrono::microseconds block_timeout{1000};

        /// Maximum number of spin iterations before yielding in Block mode.
        std::size_t spin_count = 64;
    };

    /// @brief Applies a backpressure policy to queue push operations.
    /// @tparam T Element type.
    ///
    /// This is a non-owning wrapper; the caller must ensure the underlying
    /// queue outlives this object.
    ///
    /// Thread-safety: Same guarantees as the underlying queue.
    template <typename T>
    class BackpressuredQueue
    {
    public:
        BackpressuredQueue(IQueue<T> &queue, BackpressureConfig config)
            : queue_(queue), config_(config) {}

        /// @brief Push with backpressure policy applied.
        /// @return true if enqueued, false if dropped/rejected/timed out.
        bool push(T item)
        {
            switch (config_.strategy)
            {
            case BackpressureStrategy::Drop:
                return try_push_or_drop(std::move(item));

            case BackpressureStrategy::Block:
                return try_push_blocking(std::move(item));

            case BackpressureStrategy::Reject:
                return queue_.try_push(std::move(item));
            }
            return false;
        }

        /// @brief Direct access to the underlying queue for pop operations.
        IQueue<T> &queue() { return queue_; }
        const IQueue<T> &queue() const { return queue_; }

        /// @brief Get current backpressure config.
        const BackpressureConfig &config() const { return config_; }

        /// @brief Update backpressure config at runtime.
        void set_config(BackpressureConfig config) { config_ = config; }

    private:
        bool try_push_or_drop(T item)
        {
            // Attempt once; if full, the item is silently dropped.
            return queue_.try_push(std::move(item));
        }

        bool try_push_blocking(T item)
        {
            // Try without waiting first (fast path).
            if (queue_.try_push(std::move(item)))
            {
                return true;
            }

            // Spin phase: busy-wait for a bounded number of iterations.
            // Spinning avoids the overhead of a context switch for short waits.
            for (std::size_t i = 0; i < config_.spin_count; ++i)
            {
                // PAUSE instruction hint (reduces power and contention on
                // hyper-threaded cores). GCC/Clang emit PAUSE for this.
#if defined(__x86_64__) || defined(_M_X64)
                __builtin_ia32_pause();
#else
                std::this_thread::yield();
#endif
                if (queue_.try_push(std::move(item)))
                {
                    return true;
                }
            }

            // Yield phase: sleep in short intervals up to the timeout.
            if (config_.block_timeout.count() > 0)
            {
                auto deadline = std::chrono::steady_clock::now() + config_.block_timeout;
                while (std::chrono::steady_clock::now() < deadline)
                {
                    std::this_thread::yield();
                    if (queue_.try_push(std::move(item)))
                    {
                        return true;
                    }
                }
                return false; // Timed out
            }

            // block_timeout == 0: spin forever (dangerous, but user requested it)
            for (;;)
            {
                std::this_thread::yield();
                if (queue_.try_push(std::move(item)))
                {
                    return true;
                }
            }
        }

        IQueue<T> &queue_;
        BackpressureConfig config_;
    };

} // namespace conc
