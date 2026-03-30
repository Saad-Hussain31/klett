#pragma once

/// @file MPMCQueue.h
/// @brief Lock-free bounded MPMC queue based on Dmitry Vyukov's design.
///
/// ARCHITECTURE:
///   A ring buffer of Slots, each containing an atomic sequence number and
///   a data element. Producers and consumers coordinate via CAS on head_/tail_
///   indices, using the per-slot sequence number to detect whether a slot is
///   ready for writing or reading.
///
/// PERFORMANCE RATIONALE:
///   - Power-of-two capacity allows bitmask indexing (AND vs modulo).
///   - Per-slot sequence numbers avoid the ABA problem without a separate
///     version counter.
///   - head_ and tail_ are on separate cache lines to prevent false sharing
///     between producers and consumers.
///   - Slots are allocated contiguously for spatial locality (prefetcher
///     friendly when accessed sequentially).
///   - No dynamic memory allocation after construction.
///   - Memory ordering: acquire/release on sequence loads/stores provides
///     happens-before without full barriers (cheaper on ARM, same on x86).
///
/// TRADE-OFFS:
///   Lock-free vs mutex-based:
///     + No blocking: threads never sleep, predictable latency.
///     + No priority inversion: no mutex to hold.
///     - CAS retry loops under extreme contention can waste CPU.
///     - More complex to reason about correctness.
///   Bounded vs unbounded:
///     + Bounded prevents unbounded memory growth under burst.
///     + Fixed allocation is cache-friendly and allocation-free.
///     - Requires choosing capacity upfront; too small = drops, too large = waste.

#include "IQueue.h"
#include "../Common.h"

#include <atomic>
#include <cassert>
#include <memory>
#include <new>
#include <vector>

namespace conc
{

    /// @brief Lock-free bounded MPMC queue (ring buffer).
    /// @tparam T Element type. Must be nothrow-move-constructible.
    ///
    /// Thread-safety: All methods are safe to call from any number of
    /// concurrent producers and consumers.
    template <typename T>
    class MPMCQueue final : public IQueue<T>
    {
        static_assert(std::is_nothrow_move_constructible_v<T>,
                      "MPMCQueue<T> requires T to be nothrow-move-constructible");

    public:
        /// @brief Construct a queue with the given minimum capacity.
        /// Actual capacity is rounded up to the next power of two.
        explicit MPMCQueue(std::size_t min_capacity)
            : capacity_(next_power_of_two(min_capacity < 2 ? 2 : min_capacity)), mask_(capacity_ - 1), slots_(static_cast<Slot *>(
                                                                                                           ::operator new(sizeof(Slot) * capacity_, std::align_val_t{alignof(Slot)})))
        {
            for (std::size_t i = 0; i < capacity_; ++i)
            {
                new (&slots_[i].sequence) std::atomic<std::size_t>(i);
                new (&slots_[i].data) T();
            }
            head_.store(0, std::memory_order_relaxed);
            tail_.store(0, std::memory_order_relaxed);
        }

        ~MPMCQueue() override
        {
            // Drain remaining elements to call destructors
            while (try_pop().has_value())
            {
            }
            for (std::size_t i = 0; i < capacity_; ++i)
            {
                slots_[i].data.~T();
                slots_[i].sequence.~atomic();
            }
            ::operator delete(slots_, std::align_val_t{alignof(Slot)});
        }

        MPMCQueue(const MPMCQueue &) = delete;
        MPMCQueue &operator=(const MPMCQueue &) = delete;

        /// @brief Try to enqueue an element.
        /// @return true if successful, false if the queue is full.
        /// @note Wait-free in practice (bounded CAS retries under normal load).
        bool try_push(T item) override
        {
            std::size_t pos = head_.load(std::memory_order_relaxed);
            for (;;)
            {
                Slot &slot = slots_[pos & mask_];
                std::size_t seq = slot.sequence.load(std::memory_order_acquire);
                auto diff = static_cast<std::ptrdiff_t>(seq) - static_cast<std::ptrdiff_t>(pos);

                if (diff == 0)
                {
                    // Slot is free for writing at this position
                    if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                    {
                        slot.data = std::move(item);
                        slot.sequence.store(pos + 1, std::memory_order_release);
                        return true;
                    }
                    // CAS failed, pos updated by compare_exchange_weak, retry
                }
                else if (diff < 0)
                {
                    // Queue is full
                    return false;
                }
                else
                {
                    // Another producer advanced head, reload
                    pos = head_.load(std::memory_order_relaxed);
                }
            }
        }

        /// @brief Try to dequeue an element.
        /// @return The element if available, std::nullopt if queue is empty.
        std::optional<T> try_pop() override
        {
            std::size_t pos = tail_.load(std::memory_order_relaxed);
            for (;;)
            {
                Slot &slot = slots_[pos & mask_];
                std::size_t seq = slot.sequence.load(std::memory_order_acquire);
                auto diff = static_cast<std::ptrdiff_t>(seq) - static_cast<std::ptrdiff_t>(pos + 1);

                if (diff == 0)
                {
                    // Slot has data ready for this consumer position
                    if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                    {
                        T result = std::move(slot.data);
                        slot.sequence.store(pos + mask_ + 1, std::memory_order_release);
                        return result;
                    }
                }
                else if (diff < 0)
                {
                    // Queue is empty
                    return std::nullopt;
                }
                else
                {
                    pos = tail_.load(std::memory_order_relaxed);
                }
            }
        }

        std::size_t size_approx() const override
        {
            auto h = head_.load(std::memory_order_relaxed);
            auto t = tail_.load(std::memory_order_relaxed);
            return h >= t ? h - t : 0;
        }

        bool empty() const override
        {
            return size_approx() == 0;
        }

        std::size_t capacity() const override
        {
            return capacity_;
        }

    private:
        struct Slot
        {
            std::atomic<std::size_t> sequence;
            T data;
        };

        const std::size_t capacity_;
        const std::size_t mask_;
        Slot *slots_;

        // head_ and tail_ on separate cache lines to avoid false sharing
        // between producers (who CAS head_) and consumers (who CAS tail_).
        alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> head_;
        alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> tail_;
    };

} // namespace conc
