#pragma once
/// @file homotopy.hpp
/// Homotopy continuation via the Linear Tracing Procedure for Nash equilibria.

#include "nash/algorithms/solver_interface.hpp"
#include "nash/numerics/path_follower.hpp"

namespace nash
{

    /// Homotopy Continuation solver using the Linear Tracing Procedure (Harsanyi 1975).
    ///
    /// Starting from a prior belief p = (p1, p2), defines a homotopy parameter t ∈ [0,1]:
    ///   u_i^t(s, σ_{-i}) = (1-t) * u_i(s, p_{-i}) + t * u_i(s, σ_{-i})
    ///
    /// At t=0: trivial (best response to prior).
    /// At t=1: Nash equilibrium.
    ///
    /// The path of equilibria is followed using predictor-corrector continuation.
    class HomotopySolver : public IEquilibriumSolver
    {
    public:
        /// @param prior_1  Prior belief about player 1's strategy.
        ///                 If empty, uses uniform distribution.
        /// @param prior_2  Prior belief about player 2's strategy.
        ///                 If empty, uses uniform distribution.
        HomotopySolver(Vec prior_1 = {}, Vec prior_2 = {},
                       PathFollowerConfig cfg = {},
                       Tolerances tol = kDefaultTol);

        EquilibriumResult solve(const BimatrixGame &game) override;
        const char *name() const override { return "HomotopyContinuation"; }

        /// Set the prior beliefs.
        void set_prior(Vec p1, Vec p2)
        {
            prior_1_ = std::move(p1);
            prior_2_ = std::move(p2);
        }

        /// Get/set path follower configuration.
        PathFollowerConfig &config() { return cfg_; }
        const PathFollowerConfig &config() const { return cfg_; }

    private:
        Vec prior_1_;
        Vec prior_2_;
        PathFollowerConfig cfg_;
        Tolerances tol_;

        /// Build the homotopy system for the given game and supports.
        HomotopySystem build_homotopy(
            const BimatrixGame &game,
            const std::vector<std::size_t> &support_1,
            const std::vector<std::size_t> &support_2) const;

        /// Compute the starting point at t=0.
        Vec compute_start(
            const BimatrixGame &game,
            std::vector<std::size_t> &support_1,
            std::vector<std::size_t> &support_2) const;

        /// Extract strategy profile from the augmented variable w.
        StrategyProfile extract_profile(
            const Vec &w,
            std::size_t m, std::size_t n,
            const std::vector<std::size_t> &support_1,
            const std::vector<std::size_t> &support_2) const;
    };

} // namespace nash
