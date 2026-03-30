#pragma once

// Lock-free Multi-Producer Single-Consumer (MPSC) bounded queue.
//
// Design:
//   - Array-based bounded ring buffer (power-of-two capacity).
//   - Multiple producers coordinate via CAS on the write cursor.
//   - Single consumer reads from the read cursor.
//   - Each slot carries a sequence number to detect availability.
//   - Inspired by Dmitry Vyukov's bounded MPMC queue, simplified for
//     single-consumer use.  The consumer never contends with other
//     consumers, so its read path is wait-free.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace sor
{

    template <typename T, size_t Capacity = 8192>
    class MPSCQueue
    {
        static_assert(Capacity > 0, "Capacity must be > 0");
        static_assert((Capacity & (Capacity - 1)) == 0,
                      "Capacity must be a power of 2");

    public:
        MPSCQueue() noexcept
        {
            // Initialise each cell's sequence to its index.
            for (size_t i = 0; i < Capacity; ++i)
            {
                cells_[i].sequence.store(i, std::memory_order_relaxed);
            }
            write_pos_.store(0, std::memory_order_relaxed);
            read_pos_.store(0, std::memory_order_relaxed);
        }

        ~MPSCQueue() = default;

        // Non-copyable, non-movable.
        MPSCQueue(const MPSCQueue &) = delete;
        MPSCQueue &operator=(const MPSCQueue &) = delete;
        MPSCQueue(MPSCQueue &&) = delete;
        MPSCQueue &operator=(MPSCQueue &&) = delete;

        // -- Producer API (thread-safe, lock-free) ------------------------------

        /// Try to enqueue an item (copy).  Returns false if the queue is full.
        bool try_push(const T &item) noexcept(std::is_nothrow_copy_constructible_v<T>)
        {
            return try_push_impl(item);
        }

        /// Try to enqueue an item (move).  Returns false if the queue is full.
        bool try_push(T &&item) noexcept(std::is_nothrow_move_constructible_v<T>)
        {
            return try_push_impl(std::move(item));
        }

        // -- Consumer API (single-consumer, wait-free) --------------------------

        /// Try to dequeue an item.  Returns false if the queue is empty.
        /// Must be called from exactly one consumer thread.
        bool try_pop(T &item) noexcept(std::is_nothrow_move_assignable_v<T>)
        {
            Cell &cell = cells_[read_pos_ & MASK];
            size_t seq = cell.sequence.load(std::memory_order_acquire);
            size_t pos = read_pos_.load(std::memory_order_relaxed);

            // The cell is ready for reading when its sequence equals pos + 1.
            // (Producers bump the sequence to pos + 1 after writing.)
            if (seq != pos + 1)
            {
                return false; // queue is empty (or producer hasn't committed yet)
            }

            item = std::move(cell.data);

            // Advance the sequence by Capacity so producers can reuse this slot.
            cell.sequence.store(pos + Capacity, std::memory_order_release);
            read_pos_.store(pos + 1, std::memory_order_relaxed);
            return true;
        }

        // -- Observers ----------------------------------------------------------

        /// True when the queue appears empty (snapshot, may race).
        [[nodiscard]] bool empty() const noexcept
        {
            size_t rp = read_pos_.load(std::memory_order_acquire);
            const Cell &cell = cells_[rp & MASK];
            size_t seq = cell.sequence.load(std::memory_order_acquire);
            return seq != rp + 1;
        }

        /// Approximate number of items in the queue (snapshot, may race).
        [[nodiscard]] size_t size() const noexcept
        {
            size_t wp = write_pos_.load(std::memory_order_acquire);
            size_t rp = read_pos_.load(std::memory_order_acquire);
            return (wp >= rp) ? (wp - rp) : 0;
        }

        [[nodiscard]] static constexpr size_t capacity() noexcept
        {
            return Capacity;
        }

    private:
        static constexpr size_t MASK = Capacity - 1;

        // Each cell contains the data plus a sequence counter that coordinates
        // producers and the single consumer.
        struct Cell
        {
            std::atomic<size_t> sequence;
            T data;
        };

        template <typename U>
        bool try_push_impl(U &&item) noexcept(
            std::is_nothrow_constructible_v<T, U &&>)
        {

            size_t pos = write_pos_.load(std::memory_order_relaxed);

            while (true)
            {
                Cell &cell = cells_[pos & MASK];
                size_t seq = cell.sequence.load(std::memory_order_acquire);

                intptr_t diff = static_cast<intptr_t>(seq) -
                                static_cast<intptr_t>(pos);

                if (diff == 0)
                {
                    // Slot is available.  Try to claim it.
                    if (write_pos_.compare_exchange_weak(
                            pos, pos + 1,
                            std::memory_order_relaxed,
                            std::memory_order_relaxed))
                    {
                        // We own this slot; write the data and publish.
                        cell.data = std::forward<U>(item);
                        cell.sequence.store(pos + 1, std::memory_order_release);
                        return true;
                    }
                    // CAS failed -- pos was reloaded; retry.
                }
                else if (diff < 0)
                {
                    // Queue is full.
                    return false;
                }
                else
                {
                    // Another producer is between claim and publish on this cell.
                    // Re-read write_pos_ and try the next slot.
                    pos = write_pos_.load(std::memory_order_relaxed);
                }
            }
        }

        // -- Data members -------------------------------------------------------

        // Shared write cursor (contended by multiple producers).
        alignas(64) std::atomic<size_t> write_pos_{0};
        char pad1_[64 - sizeof(std::atomic<size_t>)];

        // Consumer-private read cursor.
        alignas(64) std::atomic<size_t> read_pos_{0};
        char pad2_[64 - sizeof(std::atomic<size_t>)];

        // Cell array.
        alignas(64) std::array<Cell, Capacity> cells_;
    };

} // namespace sor
