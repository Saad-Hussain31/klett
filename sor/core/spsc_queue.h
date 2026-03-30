#pragma once

// Lock-free Single-Producer Single-Consumer (SPSC) bounded queue.
//
// Design:
//   - Power-of-two capacity for fast modular indexing (bitwise AND).
//   - Head and tail on separate cache lines to eliminate false sharing.
//   - Producer owns tail; consumer owns head.
//   - Relaxed loads for the "own" index, acquire/release for cross-thread.

#include <array>
#include <atomic>
#include <cstddef>
#include <new> // std::hardware_destructive_interference_size
#include <type_traits>
#include <utility> // std::move

namespace sor
{

    template <typename T, size_t Capacity = 8192>
    class SPSCQueue
    {
        static_assert(Capacity > 0, "Capacity must be > 0");
        static_assert((Capacity & (Capacity - 1)) == 0,
                      "Capacity must be a power of 2");

    public:
        SPSCQueue() = default;
        ~SPSCQueue() = default;

        // Non-copyable, non-movable.
        SPSCQueue(const SPSCQueue &) = delete;
        SPSCQueue &operator=(const SPSCQueue &) = delete;
        SPSCQueue(SPSCQueue &&) = delete;
        SPSCQueue &operator=(SPSCQueue &&) = delete;

        // -- Producer API -------------------------------------------------------

        /// Try to enqueue an item (copy).  Returns false if the queue is full.
        bool try_push(const T &item) noexcept(std::is_nothrow_copy_constructible_v<T>)
        {
            const size_t t = tail_.load(std::memory_order_relaxed);
            const size_t next_t = (t + 1) & MASK;

            // If next_tail == head, the queue is full.
            if (next_t == head_.load(std::memory_order_acquire))
            {
                return false;
            }

            buffer_[t] = item;
            tail_.store(next_t, std::memory_order_release);
            return true;
        }

        /// Try to enqueue an item (move).  Returns false if the queue is full.
        bool try_push(T &&item) noexcept(std::is_nothrow_move_constructible_v<T>)
        {
            const size_t t = tail_.load(std::memory_order_relaxed);
            const size_t next_t = (t + 1) & MASK;

            if (next_t == head_.load(std::memory_order_acquire))
            {
                return false;
            }

            buffer_[t] = std::move(item);
            tail_.store(next_t, std::memory_order_release);
            return true;
        }

        // -- Consumer API -------------------------------------------------------

        /// Try to dequeue an item.  Returns false if the queue is empty.
        bool try_pop(T &item) noexcept(std::is_nothrow_move_assignable_v<T>)
        {
            const size_t h = head_.load(std::memory_order_relaxed);

            // If head == tail, the queue is empty.
            if (h == tail_.load(std::memory_order_acquire))
            {
                return false;
            }

            item = std::move(buffer_[h]);
            head_.store((h + 1) & MASK, std::memory_order_release);
            return true;
        }

        // -- Observers ----------------------------------------------------------

        /// True when the queue appears empty (snapshot, may race).
        [[nodiscard]] bool empty() const noexcept
        {
            return head_.load(std::memory_order_acquire) ==
                   tail_.load(std::memory_order_acquire);
        }

        /// Approximate number of items in the queue (snapshot, may race).
        [[nodiscard]] size_t size() const noexcept
        {
            const size_t h = head_.load(std::memory_order_acquire);
            const size_t t = tail_.load(std::memory_order_acquire);
            return (t - h + Capacity) & MASK;
        }

        [[nodiscard]] static constexpr size_t capacity() noexcept
        {
            // Usable capacity is Capacity - 1 (one slot is sentinel).
            return Capacity - 1;
        }

    private:
        static constexpr size_t MASK = Capacity - 1;

        // Producer index -- only written by the producer thread.
        // Placed on its own cache line.
        alignas(64) std::atomic<size_t> head_{0};
        char pad1_[64 - sizeof(std::atomic<size_t>)];

        // Consumer index -- only written by the consumer thread.
        alignas(64) std::atomic<size_t> tail_{0};
        char pad2_[64 - sizeof(std::atomic<size_t>)];

        // Data buffer.
        alignas(64) std::array<T, Capacity> buffer_{};
    };

} // namespace sor
