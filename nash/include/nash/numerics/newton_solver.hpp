#pragma once
/// @file newton_solver.hpp
/// Newton's method for nonlinear systems F(x) = 0.

#include "nash/core/types.hpp"
#include "nash/numerics/linear_algebra.hpp"
#include <functional>

namespace nash
{

    /// A nonlinear system F: R^n -> R^n with optional analytical Jacobian.
    struct NonlinearSystem
    {
        /// Evaluate F(x).
        std::function<Vec(const Vec &)> F;

        /// Evaluate the Jacobian J(x). If null, numerical differentiation is used.
        std::function<Mat(const Vec &)> J;

        /// Dimension.
        std::size_t dim = 0;
    };

    /// Result of a Newton solve.
    struct NewtonResult
    {
        Vec x;
        double residual = 0.0;
        int iterations = 0;
        bool converged = false;
    };

    /// Solve F(x) = 0 using Newton's method with line search.
    /// @param sys       The nonlinear system.
    /// @param x0        Initial guess.
    /// @param tol       Convergence tolerance on ||F(x)||_inf.
    /// @param max_iter  Maximum iterations.
    NewtonResult newton_solve(const NonlinearSystem &sys,
                              const Vec &x0,
                              double tol = 1e-10,
                              int max_iter = 100);

    /// Compute numerical Jacobian via central finite differences.
    Mat numerical_jacobian(const std::function<Vec(const Vec &)> &F,
                           const Vec &x,
                           double h = 1e-7);

} // namespace nash
