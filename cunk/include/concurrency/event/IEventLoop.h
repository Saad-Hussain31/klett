#pragma once

/// @file IEventLoop.h
/// @brief Interface for event-driven dispatch systems.
///
/// DESIGN:
///   The event loop decouples event producers from consumers. Producers
///   call dispatch() which enqueues events for processing. Consumers
///   register handlers for specific event types. The loop processes events
///   on its own thread (or the caller's thread via run_one/run_for).
///
///   This enables reactive, push-based architectures where components
///   respond to events without polling or tight coupling.

#include "Event.h"
#include <functional>
#include <chrono>

namespace conc
{

    /// @brief Callback type for event handlers.
    using EventHandler = std::function<void(const EventBase &)>;

    /// @brief Handle returned when registering an event handler.
    /// Can be used to unregister.
    using HandlerId = std::uint64_t;

    /// @brief Interface for an event dispatch loop.
    ///
    /// Thread-safety:
    ///   - dispatch() is safe to call from any thread.
    ///   - on() / off() should be called before run() or from within handlers.
    ///   - run() / stop() should be called from a single managing thread.
    class IEventLoop
    {
    public:
        virtual ~IEventLoop() = default;

        /// @brief Register a handler for events of type E.
        /// @tparam E Concrete event type (must derive from Event<E>).
        /// @param handler Callback invoked when an event of type E is dispatched.
        /// @return Handle for deregistration via off().
        template <typename E>
        HandlerId on(std::function<void(const E &)> handler)
        {
            return on_impl(E::type_id(), [h = std::move(handler)](const EventBase &e)
                           { h(static_cast<const E &>(e)); });
        }

        /// @brief Remove a previously registered handler.
        virtual void off(HandlerId id) = 0;

        /// @brief Dispatch an event for asynchronous processing.
        /// The event is enqueued and processed by the event loop thread.
        virtual void dispatch(EventPtr event) = 0;

        /// @brief Helper: construct and dispatch an event.
        template <typename E, typename... Args>
        void emit(Args &&...args)
        {
            dispatch(make_event<E>(std::forward<Args>(args)...));
        }

        /// @brief Start the event loop (blocks the calling thread).
        virtual void run() = 0;

        /// @brief Process a single event (non-blocking if no event ready).
        /// @return true if an event was processed.
        virtual bool run_one() = 0;

        /// @brief Process events for the given duration.
        virtual void run_for(std::chrono::milliseconds duration) = 0;

        /// @brief Signal the event loop to stop.
        /// Safe to call from any thread.
        virtual void stop() = 0;

        /// @brief Whether the loop is running.
        virtual bool running() const = 0;

        /// @brief Number of pending events.
        virtual std::size_t pending_events() const = 0;

    protected:
        /// Implementation hook for handler registration.
        virtual HandlerId on_impl(EventTypeId type_id, EventHandler handler) = 0;
    };

} // namespace conc
