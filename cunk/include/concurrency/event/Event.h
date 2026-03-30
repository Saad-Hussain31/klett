#pragma once

/// @file Event.h
/// @brief Type-safe event system with runtime type identification.
///
/// DESIGN:
///   Events are identified by a unique TypeId derived from the concrete
///   event type. This avoids virtual dispatch for type checking (just an
///   integer comparison) while keeping the system extensible - any struct
///   can be an event.
///
/// PERFORMANCE:
///   - TypeId generation uses a static local variable pattern (one per type).
///     This is O(1) and thread-safe since C++11 (magic statics).
///   - EventBase is 16 bytes (vtable + type_id). Concrete events add their
///     payload directly, no extra indirection.

#include <cstdint>
#include <atomic>
#include <memory>

namespace conc
{

    /// @brief Unique type identifier for events.
    using EventTypeId = std::uint64_t;

    namespace detail
    {

        /// @brief Generates a unique ID for each type T.
        /// Thread-safe (C++11 magic statics guarantee).
        inline EventTypeId next_event_id()
        {
            static std::atomic<EventTypeId> counter{0};
            return counter.fetch_add(1, std::memory_order_relaxed);
        }

        template <typename T>
        EventTypeId event_type_id()
        {
            static const EventTypeId id = next_event_id();
            return id;
        }

    } // namespace detail

    /// @brief Base class for all events.
    /// Provides runtime type identification without RTTI.
    class EventBase
    {
    public:
        virtual ~EventBase() = default;

        /// @brief Get the runtime type ID of this event.
        EventTypeId type_id() const { return type_id_; }

    protected:
        explicit EventBase(EventTypeId id) : type_id_(id) {}

    private:
        EventTypeId type_id_;
    };

    /// @brief CRTP helper to automatically assign type IDs to event types.
    ///
    /// Usage:
    ///   struct MyEvent : Event<MyEvent> {
    ///       int data;
    ///       MyEvent(int d) : Event<MyEvent>(), data(d) {}
    ///   };
    template <typename Derived>
    class Event : public EventBase
    {
    public:
        Event() : EventBase(type_id()) {}

        static EventTypeId type_id()
        {
            return detail::event_type_id<Derived>();
        }
    };

    /// @brief Type-erased event wrapper for queue storage.
    /// Uses shared_ptr for safe multi-handler dispatch.
    using EventPtr = std::shared_ptr<EventBase>;

    /// @brief Helper to create an event.
    template <typename E, typename... Args>
    EventPtr make_event(Args &&...args)
    {
        return std::make_shared<E>(std::forward<Args>(args)...);
    }

} // namespace conc
