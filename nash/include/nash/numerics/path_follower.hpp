#pragma once
/// @file path_follower.hpp
/// Predictor-corrector path following for homotopy continuation.

#include "nash/core/types.hpp"
#include "nash/numerics/linear_algebra.hpp"
#include <functional>

namespace nash
{

    /// Configuration for the path follower.
    struct PathFollowerConfig
    {
        double initial_step = 0.01;   ///< Initial step size along the path
        double min_step = 1e-8;       ///< Minimum step size before failure
        double max_step = 0.1;        ///< Maximum step size
        double step_increase = 1.5;   ///< Factor to increase step on success
        double step_decrease = 0.5;   ///< Factor to decrease step on failure
        int corrector_iter = 10;      ///< Max Newton corrector iterations
        double corrector_tol = 1e-10; ///< Newton convergence tolerance
        int max_steps = 50000;        ///< Max path steps
        double target_t = 1.0;        ///< Target parameter value
    };

    /// A homotopy system H(z, t) = 0 where z ∈ R^n and t ∈ R.
    /// The augmented variable is w = (z, t) ∈ R^(n+1).
    /// H maps R^(n+1) -> R^n (one fewer equation than unknowns → 1D curve).
    struct HomotopySystem
    {
        /// Evaluate H(w) where w = (z_1, ..., z_n, t).
        std::function<Vec(const Vec &w)> H;

        /// Evaluate the Jacobian DH/Dw (n × (n+1) matrix).
        std::function<Mat(const Vec &w)> DH;

        /// Dimension of z (number of equations = n, augmented dim = n+1).
        std::size_t n = 0;
    };

    /// State of the path follower at each step.
    struct PathPoint
    {
        Vec w;           ///< Current point (z, t)
        double t;        ///< Current parameter value (last component of w)
        double step;     ///< Current step size
        int corrections; ///< Number of corrector iterations at this step
    };

    /// Result of path following.
    struct PathResult
    {
        Vec w_final; ///< Final point
        double t_final = 0.0;
        bool reached_target = false;
        int total_steps = 0;
        int total_corrections = 0;
        std::string failure_reason;
    };

    /// Follow a 1D solution curve of H(w) = 0 from w0 toward target_t.
    /// Uses Euler predictor + Newton corrector with adaptive step sizing.
    PathResult follow_path(const HomotopySystem &sys,
                           const Vec &w0,
                           const PathFollowerConfig &cfg = {});

} // namespace nash
