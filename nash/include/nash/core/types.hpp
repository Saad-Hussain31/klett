#pragma once
/// @file types.hpp
/// Fundamental types for the Nash equilibrium library.

#include <cstddef>
#include <vector>
#include <cmath>
#include <limits>

namespace nash
{

    /// Dense vector backed by std::vector<double>.
    using Vec = std::vector<double>;

    /// Dense row-major matrix.
    struct Mat
    {
        std::size_t rows = 0;
        std::size_t cols = 0;
        std::vector<double> data; // row-major storage

        Mat() = default;
        Mat(std::size_t r, std::size_t c, double val = 0.0)
            : rows(r), cols(c), data(r * c, val) {}

        double &operator()(std::size_t i, std::size_t j) { return data[i * cols + j]; }
        double operator()(std::size_t i, std::size_t j) const { return data[i * cols + j]; }

        /// Resize and zero-fill.
        void resize(std::size_t r, std::size_t c, double val = 0.0)
        {
            rows = r;
            cols = c;
            data.assign(r * c, val);
        }
    };

    /// Numerical tolerances used throughout the library.
    struct Tolerances
    {
        double eps = 1e-10;       ///< Near-zero threshold
        double pivot_tol = 1e-12; ///< Minimum pivot element
        double conv_tol = 1e-9;   ///< Convergence criterion
        int max_iter = 10000;     ///< Maximum iterations
    };

    /// Global default tolerances (can be overridden per-solver).
    inline const Tolerances kDefaultTol{};

    /// Convenience: check approximate equality.
    inline bool approx_zero(double v, double tol = 1e-10) { return std::fabs(v) < tol; }
    inline bool approx_eq(double a, double b, double tol = 1e-10) { return std::fabs(a - b) < tol; }
    inline bool approx_ge(double a, double b, double tol = 1e-10) { return a > b - tol; }

} // namespace nash
