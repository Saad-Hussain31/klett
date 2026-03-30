#pragma once

// Common string manipulation utilities for the Smart Order Router.

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

namespace sor::utils
{

    // ---------------------------------------------------------------------------
    // Splitting / trimming
    // ---------------------------------------------------------------------------

    /// Split @p str by @p delimiter.  Returns lightweight string_views into the
    /// original string -- caller must ensure the source outlives the result.
    std::vector<std::string_view> split(std::string_view str, char delimiter);

    /// Convert all characters to upper-case (ASCII only).
    std::string to_upper(std::string_view str);

    /// Convert all characters to lower-case (ASCII only).
    std::string to_lower(std::string_view str);

    /// Remove leading and trailing whitespace.
    std::string trim(std::string_view str);

    /// True if @p str begins with @p prefix.
    bool starts_with(std::string_view str, std::string_view prefix);

    /// True if @p str ends with @p suffix.
    bool ends_with(std::string_view str, std::string_view suffix);

    // ---------------------------------------------------------------------------
    // Number formatting
    // ---------------------------------------------------------------------------

    /// Format a fixed-point price (PRICE_SCALE encoding) with @p decimals digits.
    std::string format_price(int64_t price, int decimals = 2);

    /// Format a raw quantity as a plain decimal string.
    std::string format_quantity(int64_t qty);

} // namespace sor::utils
