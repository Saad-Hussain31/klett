#include "utils/string_utils.h"
#include "core/types.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <array>
#include <cmath>

namespace sor::utils
{

    // ---------------------------------------------------------------------------
    // Splitting / trimming
    // ---------------------------------------------------------------------------

    std::vector<std::string_view> split(std::string_view str, char delimiter)
    {
        std::vector<std::string_view> tokens;
        if (str.empty())
        {
            return tokens;
        }

        std::size_t start = 0;
        while (start <= str.size())
        {
            const auto pos = str.find(delimiter, start);
            if (pos == std::string_view::npos)
            {
                tokens.push_back(str.substr(start));
                break;
            }
            tokens.push_back(str.substr(start, pos - start));
            start = pos + 1;
        }
        return tokens;
    }

    std::string to_upper(std::string_view str)
    {
        std::string result(str);
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char c)
                       { return static_cast<char>(std::toupper(c)); });
        return result;
    }

    std::string to_lower(std::string_view str)
    {
        std::string result(str);
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return result;
    }

    std::string trim(std::string_view str)
    {
        const auto first = str.find_first_not_of(" \t\n\r\f\v");
        if (first == std::string_view::npos)
        {
            return {};
        }
        const auto last = str.find_last_not_of(" \t\n\r\f\v");
        return std::string(str.substr(first, last - first + 1));
    }

    bool starts_with(std::string_view str, std::string_view prefix)
    {
        if (prefix.size() > str.size())
        {
            return false;
        }
        return str.substr(0, prefix.size()) == prefix;
    }

    bool ends_with(std::string_view str, std::string_view suffix)
    {
        if (suffix.size() > str.size())
        {
            return false;
        }
        return str.substr(str.size() - suffix.size()) == suffix;
    }

    // ---------------------------------------------------------------------------
    // Number formatting
    // ---------------------------------------------------------------------------

    std::string format_price(int64_t price, int decimals)
    {
        // Convert from PRICE_SCALE fixed-point to double, then format.
        const double value = sor::to_double(price);

        // Build format string like "%.2f"
        std::array<char, 16> fmt_buf{};
        std::snprintf(fmt_buf.data(), fmt_buf.size(), "%%.%df", decimals);

        std::array<char, 64> buf{};
        const int n = std::snprintf(buf.data(), buf.size(), fmt_buf.data(), value);
        return std::string(buf.data(), static_cast<std::size_t>(n));
    }

    std::string format_quantity(int64_t qty)
    {
        std::array<char, 32> buf{};
        const int n = std::snprintf(buf.data(), buf.size(), "%ld", static_cast<long>(qty));
        return std::string(buf.data(), static_cast<std::size_t>(n));
    }

} // namespace sor::utils
