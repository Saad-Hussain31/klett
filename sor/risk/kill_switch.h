#pragma once

// Kill switch -- a global circuit breaker that halts all order flow.
// Activation is atomic and can be observed lock-free by the order path.
// Callbacks are invoked on the activating thread (they must be fast).

#include <atomic>
#include <functional>
#include <vector>
#include <string>
#include <mutex>

namespace sor::risk
{

    class KillSwitch
    {
    public:
        using Callback = std::function<void(const std::string &reason)>;

        // Activate the kill switch.  All registered callbacks are invoked
        // synchronously with the provided reason string.
        void activate(const std::string &reason);

        // Deactivate the kill switch (re-enable trading).
        void deactivate() noexcept;

        // Fast, lock-free check suitable for the hot path.
        [[nodiscard]] bool is_active() const noexcept;

        // Return the activation reason (empty when inactive).
        [[nodiscard]] const std::string &reason() const;

        // Register a callback to be invoked when the switch is activated.
        // NOT thread-safe with respect to activate() -- register callbacks
        // during initialisation only.
        void on_activate(Callback cb);

    private:
        std::atomic<bool> active_{false};
        std::string reason_;
        std::vector<Callback> callbacks_;
        mutable std::mutex mutex_; // protects reason_ and callbacks_ list
    };

} // namespace sor::risk
