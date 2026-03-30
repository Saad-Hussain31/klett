#pragma once

/// @file Task.h
/// @brief Type-erased callable with small buffer optimization (SBO).
///
/// PERFORMANCE RATIONALE:
///   std::function typically provides SBO for small callables, but the
///   buffer size and behavior are implementation-defined. This Task class
///   provides a guaranteed 48-byte inline buffer, large enough for most
///   lambdas (captures up to ~6 pointers). This avoids heap allocation
///   on the hot path for the common case.
///
///   When the callable exceeds the buffer, we fall back to heap allocation.
///   The total Task size is 64 bytes (one cache line) including the vtable
///   pointer and metadata, maximizing cache utilization when tasks are
///   stored contiguously (e.g., in a ring buffer).

#include "../Common.h"
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace conc
{

    /// @brief Type-erased callable with guaranteed small buffer optimization.
    ///
    /// Fits in exactly one cache line (64 bytes). Callables up to 48 bytes
    /// are stored inline; larger ones are heap-allocated.
    ///
    /// Thread-safety: NOT thread-safe. A single Task should be owned by one
    /// thread at a time. The queue/executor handles the thread-safe handoff.
    class Task
    {
    public:
        static constexpr std::size_t SMALL_BUFFER_SIZE = 48;

        Task() noexcept : ops_(nullptr) {}

        /// @brief Construct from any callable.
        template <typename F,
                  typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, Task>>>
        Task(F &&f)
        {
            using Fn = std::decay_t<F>;
            if constexpr (sizeof(Fn) <= SMALL_BUFFER_SIZE &&
                          alignof(Fn) <= alignof(std::max_align_t) &&
                          std::is_nothrow_move_constructible_v<Fn>)
            {
                // Store inline (no heap allocation)
                new (&storage_) Fn(std::forward<F>(f));
                ops_ = &inline_ops<Fn>;
            }
            else
            {
                // Heap-allocate
                auto *ptr = new Fn(std::forward<F>(f));
                std::memcpy(&storage_, &ptr, sizeof(ptr));
                ops_ = &heap_ops<Fn>;
            }
        }

        Task(Task &&other) noexcept : ops_(nullptr)
        {
            if (other.ops_)
            {
                other.ops_->move(&other.storage_, &storage_);
                ops_ = other.ops_;
                other.ops_ = nullptr;
            }
        }

        Task &operator=(Task &&other) noexcept
        {
            if (this != &other)
            {
                destroy();
                if (other.ops_)
                {
                    other.ops_->move(&other.storage_, &storage_);
                    ops_ = other.ops_;
                    other.ops_ = nullptr;
                }
            }
            return *this;
        }

        Task(const Task &) = delete;
        Task &operator=(const Task &) = delete;

        ~Task() { destroy(); }

        /// @brief Invoke the held callable.
        void operator()()
        {
            if (ops_)
            {
                ops_->invoke(&storage_);
                ops_ = nullptr; // one-shot
            }
        }

        /// @brief Check whether a callable is held.
        explicit operator bool() const noexcept { return ops_ != nullptr; }

    private:
        struct Ops
        {
            void (*invoke)(void *storage);
            void (*destroy)(void *storage);
            void (*move)(void *src, void *dst);
        };

        template <typename Fn>
        static const Ops inline_ops;

        template <typename Fn>
        static const Ops heap_ops;

        void destroy()
        {
            if (ops_)
            {
                ops_->destroy(&storage_);
                ops_ = nullptr;
            }
        }

        alignas(std::max_align_t) char storage_[SMALL_BUFFER_SIZE];
        const Ops *ops_;
    };

    // --- Inline (SBO) operations ---
    template <typename Fn>
    const Task::Ops Task::inline_ops = {
        // invoke
        [](void *storage)
        {
            auto &fn = *reinterpret_cast<Fn *>(storage);
            fn();
            fn.~Fn();
        },
        // destroy
        [](void *storage)
        {
            reinterpret_cast<Fn *>(storage)->~Fn();
        },
        // move
        [](void *src, void *dst)
        {
            new (dst) Fn(std::move(*reinterpret_cast<Fn *>(src)));
            reinterpret_cast<Fn *>(src)->~Fn();
        }};

    // --- Heap operations ---
    template <typename Fn>
    const Task::Ops Task::heap_ops = {
        // invoke
        [](void *storage)
        {
            Fn *ptr;
            std::memcpy(&ptr, storage, sizeof(ptr));
            (*ptr)();
            delete ptr;
        },
        // destroy
        [](void *storage)
        {
            Fn *ptr;
            std::memcpy(&ptr, storage, sizeof(ptr));
            delete ptr;
        },
        // move
        [](void *src, void *dst)
        {
            std::memcpy(dst, src, sizeof(Fn *));
        }};

} // namespace conc
