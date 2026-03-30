#pragma once

// Mock FIX protocol adapter.
// Translates between the internal Order/ExecutionReport types and
// simplified FIX 4.4 messages.  Does not perform actual network I/O --
// messages are queued in-process for testing and simulation.

#include "connectors/venue_adapter.h"
#include <queue>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace sor::connectors
{

    // ---------------------------------------------------------------------------
    // FixMessage -- simplified FIX message representation.
    // ---------------------------------------------------------------------------

    struct FixMessage
    {
        enum class Type : uint8_t
        {
            NewOrderSingle,     // 35=D
            OrderCancelRequest, // 35=F
            ExecutionReport,    // 35=8
            Heartbeat,          // 35=0
            Logon,              // 35=A
            Logout,             // 35=5
        };

        Type type;
        std::unordered_map<int, std::string> fields; // tag -> value

        // -- Common FIX tags (subset) -----------------------------------------------

        static constexpr int TAG_MSG_TYPE = 35;
        static constexpr int TAG_CL_ORD_ID = 11;
        static constexpr int TAG_ORDER_ID = 37;
        static constexpr int TAG_EXEC_ID = 17;
        static constexpr int TAG_EXEC_TYPE = 150;
        static constexpr int TAG_ORD_STATUS = 39;
        static constexpr int TAG_SYMBOL = 55;
        static constexpr int TAG_SIDE = 54;
        static constexpr int TAG_ORDER_QTY = 38;
        static constexpr int TAG_PRICE = 44;
        static constexpr int TAG_LAST_PX = 31;
        static constexpr int TAG_LAST_QTY = 32;
        static constexpr int TAG_LEAVES_QTY = 151;
        static constexpr int TAG_CUM_QTY = 14;
        static constexpr int TAG_AVG_PX = 6;
        static constexpr int TAG_TEXT = 58;

        /// Serialize to a pipe-delimited FIX string: "tag=value|tag=value|..."
        [[nodiscard]] std::string serialize() const;

        /// Deserialize from a pipe-delimited FIX string.
        [[nodiscard]] static FixMessage deserialize(const std::string &raw);
    };

    // ---------------------------------------------------------------------------
    // FixAdapter -- mock FIX gateway adapter.
    // ---------------------------------------------------------------------------

    class FixAdapter : public VenueAdapter
    {
    public:
        struct Config
        {
            VenueId venue_id{2};
            std::string name{"FixVenue"};
            std::string sender_comp_id{"SOR_CLIENT"};
            std::string target_comp_id{"EXCHANGE"};
            std::chrono::microseconds simulated_latency{100};
        };

        explicit FixAdapter(Config config);

        // Non-copyable, non-movable.
        FixAdapter(const FixAdapter &) = delete;
        FixAdapter &operator=(const FixAdapter &) = delete;
        FixAdapter(FixAdapter &&) = delete;
        FixAdapter &operator=(FixAdapter &&) = delete;

        // -- VenueAdapter interface -------------------------------------------------

        bool connect() override;
        void disconnect() override;
        [[nodiscard]] bool is_connected() const override;

        bool send_order(const Order &order) override;
        bool cancel_order(const CancelRequest &request) override;

        [[nodiscard]] VenueId venue_id() const override;
        [[nodiscard]] const char *venue_name() const override;
        [[nodiscard]] VenueStatus status() const override;
        [[nodiscard]] std::chrono::microseconds avg_latency() const override;

        // -- FIX-specific operations ------------------------------------------------

        /// Process all queued incoming FIX messages, translating each
        /// ExecutionReport into the internal representation and dispatching
        /// via the registered callback.
        void process_incoming();

        /// Inject a FIX message into the incoming queue (for testing/simulation).
        void inject_message(FixMessage msg);

        /// Read-only access to the outbound message log (for debugging/assertions).
        [[nodiscard]] const std::vector<FixMessage> &sent_messages() const
        {
            return sent_log_;
        }

    private:
        // -- FIX <-> internal translation -------------------------------------------

        [[nodiscard]] FixMessage order_to_fix(const Order &order) const;
        [[nodiscard]] FixMessage cancel_to_fix(const CancelRequest &request) const;
        [[nodiscard]] ExecutionReport exec_from_fix(const FixMessage &msg) const;

        // -- Data members -----------------------------------------------------------

        Config config_;
        std::atomic<bool> connected_{false};

        std::queue<FixMessage> incoming_queue_;
        std::vector<FixMessage> sent_log_;
        std::unordered_map<OrderId, Order> active_orders_;

        mutable std::mutex mutex_;
        uint32_t msg_seq_num_{0};
    };

} // namespace sor::connectors
