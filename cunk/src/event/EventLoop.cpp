/// @file EventLoop.cpp
/// @brief Implementation of the event loop.

#include "concurrency/event/EventLoop.h"
#include <algorithm>

namespace conc
{

    EventLoop::EventLoop(EventLoopConfig config)
        : queue_(config.queue_size)
    {
    }

    EventLoop::~EventLoop()
    {
        if (running_.load(std::memory_order_relaxed))
        {
            stop();
        }
    }

    HandlerId EventLoop::on_impl(EventTypeId type_id, EventHandler handler)
    {
        HandlerId id = next_handler_id_.fetch_add(1, std::memory_order_relaxed);
        std::unique_lock lock(handlers_mutex_);
        handlers_.push_back({id, type_id, std::move(handler)});
        return id;
    }

    void EventLoop::off(HandlerId id)
    {
        std::unique_lock lock(handlers_mutex_);
        handlers_.erase(
            std::remove_if(handlers_.begin(), handlers_.end(),
                           [id](const HandlerEntry &e)
                           { return e.id == id; }),
            handlers_.end());
    }

    void EventLoop::dispatch(EventPtr event)
    {
        if (event)
        {
            queue_.try_push(std::move(event));
            notify();
        }
    }

    void EventLoop::run()
    {
        running_.store(true, std::memory_order_release);
        stop_flag_.store(false, std::memory_order_release);

        while (!stop_flag_.load(std::memory_order_acquire))
        {
            if (!process_one())
            {
                // No event available - wait for notification.
                std::unique_lock lock(wake_mutex_);
                wake_cv_.wait_for(lock, std::chrono::milliseconds(1), [this]
                                  { return stop_flag_.load(std::memory_order_acquire) ||
                                           !queue_.empty(); });
            }
        }

        running_.store(false, std::memory_order_release);
    }

    bool EventLoop::run_one()
    {
        return process_one();
    }

    void EventLoop::run_for(std::chrono::milliseconds duration)
    {
        auto deadline = std::chrono::steady_clock::now() + duration;
        running_.store(true, std::memory_order_release);
        stop_flag_.store(false, std::memory_order_release);

        while (!stop_flag_.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline)
        {
            if (!process_one())
            {
                auto remaining = deadline - std::chrono::steady_clock::now();
                if (remaining.count() <= 0)
                    break;
                auto wait_time = std::min(
                    std::chrono::duration_cast<std::chrono::milliseconds>(remaining),
                    std::chrono::milliseconds(1));
                std::unique_lock lock(wake_mutex_);
                wake_cv_.wait_for(lock, wait_time);
            }
        }

        running_.store(false, std::memory_order_release);
    }

    void EventLoop::stop()
    {
        stop_flag_.store(true, std::memory_order_release);
        notify();
    }

    bool EventLoop::running() const
    {
        return running_.load(std::memory_order_acquire);
    }

    std::size_t EventLoop::pending_events() const
    {
        return queue_.size_approx();
    }

    bool EventLoop::process_one()
    {
        auto event = queue_.try_pop();
        if (!event.has_value())
            return false;

        auto &evt = *event;
        EventTypeId tid = evt->type_id();

        // Snapshot handlers under reader lock, then invoke outside the lock
        // to prevent deadlock if a handler calls on()/off().
        std::vector<EventHandler> matched;
        {
            std::shared_lock lock(handlers_mutex_);
            for (auto &entry : handlers_)
            {
                if (entry.type_id == tid)
                {
                    matched.push_back(entry.handler);
                }
            }
        }

        for (auto &handler : matched)
        {
            try
            {
                handler(*evt);
            }
            catch (...)
            {
                // Swallow handler exceptions to keep the loop alive.
            }
        }

        return true;
    }

    void EventLoop::notify()
    {
        wake_cv_.notify_one();
    }

} // namespace conc
