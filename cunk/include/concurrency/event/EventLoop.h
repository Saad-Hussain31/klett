#pragma once

/// @file EventLoop.h
/// @brief Concrete event loop implementation.
///
/// ARCHITECTURE:
///   - Events are enqueued into an MPMCQueue (lock-free, bounded).
///   - A dedicated processing thread (in run()) dequeues events and
///     dispatches them to registered handlers.
///   - Handler registry is a map from EventTypeId to vector of handlers.
///   - A mutex protects the handler registry (handler registration is
///     rare; event dispatch is frequent - this amortizes well).
///
/// PERFORMANCE:
///   - Event dispatch (hot path): lock-free push into MPMCQueue.
///   - Event processing: lock-free pop, then handler lookup (shared_mutex
///     reader lock - allows concurrent reads).
///   - Handler invocation is synchronous within the loop thread, giving
///     deterministic ordering per-event-type.

#include "IEventLoop.h"
#include "../queue/MPMCQueue.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace conc
{

    /// @brief Configuration for the EventLoop.
    struct EventLoopConfig
    {
        /// Event queue capacity.
        std::size_t queue_size = 4096;
    };

    /// @brief Concrete event loop with lock-free event queue and handler dispatch.
    class EventLoop final : public IEventLoop
    {
    public:
        explicit EventLoop(EventLoopConfig config = {});
        ~EventLoop() override;

        EventLoop(const EventLoop &) = delete;
        EventLoop &operator=(const EventLoop &) = delete;

        void off(HandlerId id) override;
        void dispatch(EventPtr event) override;
        void run() override;
        bool run_one() override;
        void run_for(std::chrono::milliseconds duration) override;
        void stop() override;
        bool running() const override;
        std::size_t pending_events() const override;

    protected:
        HandlerId on_impl(EventTypeId type_id, EventHandler handler) override;

    private:
        struct HandlerEntry
        {
            HandlerId id;
            EventTypeId type_id;
            EventHandler handler;
        };

        bool process_one();
        void notify();

        MPMCQueue<EventPtr> queue_;

        mutable std::shared_mutex handlers_mutex_;
        std::vector<HandlerEntry> handlers_;
        std::atomic<HandlerId> next_handler_id_{1};

        alignas(64) std::atomic<bool> running_{false};
        alignas(64) std::atomic<bool> stop_flag_{false};

        std::mutex wake_mutex_;
        std::condition_variable wake_cv_;
    };

} // namespace conc
