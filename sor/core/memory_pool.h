#pragma once

// Lock-free object pool using a Treiber stack with tagged pointers
// to solve the ABA problem.  Zero heap allocations on the hot path --
// all storage is pre-allocated in a contiguous byte array.

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new> // placement new / std::launder

namespace sor
{

    // ---------------------------------------------------------------------------
    // MemoryPool<T, PoolSize>
    //
    // Template parameters:
    //   T        -- object type to pool (must be nothrow-destructible).
    //   PoolSize -- maximum number of objects (default 4096).
    //
    // Thread safety: allocate() and deallocate() are lock-free and safe to call
    // from any thread.  The ABA problem is mitigated by packing a monotonically-
    // increasing tag into the upper 32 bits of the free-list head.
    // ---------------------------------------------------------------------------

    template <typename T, size_t PoolSize = 4096>
    class MemoryPool
    {
        static_assert(PoolSize > 0, "PoolSize must be > 0");
        static_assert(PoolSize <= UINT32_MAX, "PoolSize must fit in uint32_t");

    public:
        MemoryPool() noexcept
        {
            // Build the free list: slot[i].next = i+1, last slot = SENTINEL.
            for (uint32_t i = 0; i < static_cast<uint32_t>(PoolSize); ++i)
            {
                node_at(i)->next.store(i + 1, std::memory_order_relaxed);
            }
            // Head points to slot 0 with initial tag 0.
            free_head_.store(pack(0, 0), std::memory_order_relaxed);
            available_.store(PoolSize, std::memory_order_relaxed);
        }

        ~MemoryPool() = default;

        // Non-copyable, non-movable (storage is embedded).
        MemoryPool(const MemoryPool &) = delete;
        MemoryPool &operator=(const MemoryPool &) = delete;
        MemoryPool(MemoryPool &&) = delete;
        MemoryPool &operator=(MemoryPool &&) = delete;

        // -- Public API ---------------------------------------------------------

        /// Allocate one object slot.  Returns nullptr if the pool is exhausted.
        /// The returned memory is *uninitialised*; the caller must placement-new
        /// into it (or use the typed allocate overload below).
        [[nodiscard]] T *allocate() noexcept
        {
            uint64_t old_head = free_head_.load(std::memory_order_acquire);

            while (true)
            {
                uint32_t idx = index_of(old_head);
                if (idx >= static_cast<uint32_t>(PoolSize))
                {
                    return nullptr; // pool exhausted (SENTINEL)
                }

                uint32_t tag = tag_of(old_head);
                uint32_t nxt = node_at(idx)->next.load(std::memory_order_relaxed);
                uint64_t new_head = pack(nxt, tag + 1);

                if (free_head_.compare_exchange_weak(
                        old_head, new_head,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire))
                {
                    available_.fetch_sub(1, std::memory_order_relaxed);
                    return slot_ptr(idx);
                }
                // CAS failed -- old_head was reloaded; retry.
            }
        }

        /// Return a previously-allocated slot back to the pool.
        /// Undefined behaviour if ptr was not obtained from this pool.
        void deallocate(T *ptr) noexcept
        {
            assert(ptr && "deallocate(nullptr) is not allowed");
            uint32_t idx = slot_index(ptr);
            assert(idx < static_cast<uint32_t>(PoolSize));

            uint64_t old_head = free_head_.load(std::memory_order_acquire);
            uint64_t new_head;

            do
            {
                node_at(idx)->next.store(index_of(old_head), std::memory_order_relaxed);
                new_head = pack(idx, tag_of(old_head) + 1);
            } while (!free_head_.compare_exchange_weak(
                old_head, new_head,
                std::memory_order_acq_rel,
                std::memory_order_acquire));

            available_.fetch_add(1, std::memory_order_relaxed);
        }

        /// Number of slots currently available.
        [[nodiscard]] size_t available() const noexcept
        {
            return available_.load(std::memory_order_relaxed);
        }

        /// Total pool capacity.
        [[nodiscard]] static constexpr size_t capacity() noexcept
        {
            return PoolSize;
        }

    private:
        // -- Tagged-pointer helpers (ABA prevention) ----------------------------
        // Layout of the 64-bit head word:  [ tag : 32 | index : 32 ]

        static constexpr uint32_t SENTINEL = static_cast<uint32_t>(PoolSize);

        static uint64_t pack(uint32_t index, uint32_t tag) noexcept
        {
            return (static_cast<uint64_t>(tag) << 32) | index;
        }
        static uint32_t index_of(uint64_t packed) noexcept
        {
            return static_cast<uint32_t>(packed & 0xFFFFFFFF);
        }
        static uint32_t tag_of(uint64_t packed) noexcept
        {
            return static_cast<uint32_t>(packed >> 32);
        }

        // -- Overlay node for the free list -------------------------------------
        // Each free slot is reused to store a next-index pointer.  The node
        // is placed at the beginning of each slot (sizeof(T) >= sizeof(Node)
        // is checked at compile time).

        struct Node
        {
            std::atomic<uint32_t> next;
        };

        static_assert(sizeof(T) >= sizeof(Node),
                      "Pooled type must be at least as large as the free-list node");

        // -- Storage access -----------------------------------------------------

        T *slot_ptr(uint32_t idx) noexcept
        {
            return std::launder(reinterpret_cast<T *>(
                storage_.data() + idx * sizeof(T)));
        }

        Node *node_at(uint32_t idx) noexcept
        {
            return std::launder(reinterpret_cast<Node *>(
                storage_.data() + idx * sizeof(T)));
        }

        uint32_t slot_index(const T *ptr) const noexcept
        {
            auto offset = reinterpret_cast<const std::byte *>(ptr) - storage_.data();
            return static_cast<uint32_t>(offset / sizeof(T));
        }

        // -- Data members -------------------------------------------------------

        // Raw byte storage for PoolSize objects.  Aligned to T's alignment so
        // that every slot is naturally aligned.
        alignas(alignof(T)) std::array<std::byte, sizeof(T) * PoolSize> storage_;

        // Lock-free Treiber-stack head (tagged pointer).
        alignas(64) std::atomic<uint64_t> free_head_{0};

        // Approximate count of free slots.
        alignas(64) std::atomic<size_t> available_{0};
    };

} // namespace sor
