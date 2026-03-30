#include "risk/kill_switch.h"

namespace sor::risk
{

    void KillSwitch::activate(const std::string &reason)
    {
        // Acquire the mutex to serialise callback invocation and reason update.
        std::lock_guard<std::mutex> lock(mutex_);

        reason_ = reason;
        active_.store(true, std::memory_order_release);

        // Invoke all registered callbacks synchronously.
        // Callbacks are expected to be fast (e.g., logging, sending alerts).
        for (const auto &cb : callbacks_)
        {
            if (cb)
            {
                cb(reason);
            }
        }
    }

    void KillSwitch::deactivate() noexcept
    {
        active_.store(false, std::memory_order_release);
        // reason_ is intentionally preserved for post-mortem inspection.
    }

    bool KillSwitch::is_active() const noexcept
    {
        return active_.load(std::memory_order_acquire);
    }

    const std::string &KillSwitch::reason() const
    {
        // Caller should only read reason after observing is_active() == true,
        // or under the knowledge that the switch was activated at some point.
        std::lock_guard<std::mutex> lock(mutex_);
        return reason_;
    }

    void KillSwitch::on_activate(Callback cb)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_.push_back(std::move(cb));
    }

} // namespace sor::risk
