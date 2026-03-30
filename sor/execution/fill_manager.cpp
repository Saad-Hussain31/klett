#include "execution/fill_manager.h"

namespace sor::execution
{

    // ---------------------------------------------------------------------------
    // Record
    // ---------------------------------------------------------------------------

    void FillManager::record_fill(const FillRecord &fill)
    {
        std::lock_guard lock(mutex_);
        const size_t idx = all_fills_.size();
        all_fills_.push_back(fill);
        order_fills_[fill.order_id].push_back(idx);
        symbol_fills_[fill.symbol].push_back(idx);
    }

    // ---------------------------------------------------------------------------
    // Queries
    // ---------------------------------------------------------------------------

    std::vector<FillRecord> FillManager::get_fills_for_order(OrderId order_id) const
    {
        std::lock_guard lock(mutex_);
        std::vector<FillRecord> result;
        auto it = order_fills_.find(order_id);
        if (it != order_fills_.end())
        {
            result.reserve(it->second.size());
            for (size_t idx : it->second)
            {
                result.push_back(all_fills_[idx]);
            }
        }
        return result;
    }

    std::vector<FillRecord> FillManager::get_fills_for_symbol(const Symbol &symbol) const
    {
        std::lock_guard lock(mutex_);
        std::vector<FillRecord> result;
        auto it = symbol_fills_.find(symbol);
        if (it != symbol_fills_.end())
        {
            result.reserve(it->second.size());
            for (size_t idx : it->second)
            {
                result.push_back(all_fills_[idx]);
            }
        }
        return result;
    }

    std::vector<FillRecord> FillManager::get_all_fills() const
    {
        std::lock_guard lock(mutex_);
        return all_fills_;
    }

    // ---------------------------------------------------------------------------
    // Aggregations
    // ---------------------------------------------------------------------------

    Quantity FillManager::total_filled_quantity(OrderId order_id) const
    {
        std::lock_guard lock(mutex_);
        Quantity total = 0;
        auto it = order_fills_.find(order_id);
        if (it != order_fills_.end())
        {
            for (size_t idx : it->second)
            {
                total += all_fills_[idx].quantity;
            }
        }
        return total;
    }

    Price FillManager::average_fill_price(OrderId order_id) const
    {
        std::lock_guard lock(mutex_);
        auto it = order_fills_.find(order_id);
        if (it == order_fills_.end() || it->second.empty())
        {
            return 0;
        }

        int64_t total_cost = 0;
        Quantity total_qty = 0;
        for (size_t idx : it->second)
        {
            const auto &f = all_fills_[idx];
            total_cost += f.price * f.quantity;
            total_qty += f.quantity;
        }

        return total_qty > 0 ? static_cast<Price>(total_cost / total_qty) : 0;
    }

    double FillManager::total_fees(OrderId order_id) const
    {
        std::lock_guard lock(mutex_);
        double total = 0.0;
        auto it = order_fills_.find(order_id);
        if (it != order_fills_.end())
        {
            for (size_t idx : it->second)
            {
                total += all_fills_[idx].fee;
            }
        }
        return total;
    }

    Price FillManager::vwap(const Symbol &symbol) const
    {
        std::lock_guard lock(mutex_);
        auto it = symbol_fills_.find(symbol);
        if (it == symbol_fills_.end() || it->second.empty())
        {
            return 0;
        }

        int64_t total_cost = 0;
        Quantity total_qty = 0;
        for (size_t idx : it->second)
        {
            const auto &f = all_fills_[idx];
            total_cost += f.price * f.quantity;
            total_qty += f.quantity;
        }

        return total_qty > 0 ? static_cast<Price>(total_cost / total_qty) : 0;
    }

    // ---------------------------------------------------------------------------
    // Reset
    // ---------------------------------------------------------------------------

    void FillManager::clear()
    {
        std::lock_guard lock(mutex_);
        all_fills_.clear();
        order_fills_.clear();
        symbol_fills_.clear();
    }

} // namespace sor::execution
