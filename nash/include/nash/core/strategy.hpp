#pragma once
/// @file strategy.hpp
/// Strategy profiles and equilibrium result types.

#include "nash/core/types.hpp"
#include <string>
#include <vector>
#include <iostream>

namespace nash
{

    /// Mixed strategy profile for a 2-player game.
    struct StrategyProfile
    {
        Vec strategy_1; ///< Player 1 mixed strategy (simplex)
        Vec strategy_2; ///< Player 2 mixed strategy (simplex)

        /// Check if strategies are valid probability distributions.
        bool is_valid(double tol = 1e-8) const;

        /// Support of player 1's strategy.
        std::vector<std::size_t> support_1(double tol = 1e-10) const;

        /// Support of player 2's strategy.
        std::vector<std::size_t> support_2(double tol = 1e-10) const;
    };

    /// Convergence/status info from a solver.
    enum class SolverStatus
    {
        Converged,
        MaxIterations,
        NumericalFailure,
        Infeasible,
        NotRun
    };

    const char *to_string(SolverStatus s);

    /// Complete result from an equilibrium solver.
    struct EquilibriumResult
    {
        StrategyProfile profile;
        double residual = 0.0; ///< How close to exact equilibrium
        int iterations = 0;
        SolverStatus status = SolverStatus::NotRun;
        std::string solver_name;

        /// Expected payoffs at the equilibrium (computed separately).
        double payoff_1 = 0.0;
        double payoff_2 = 0.0;

        /// Print a human-readable summary.
        void print(std::ostream &os = std::cout) const;
    };

    /// Result container for solvers that find multiple equilibria.
    struct MultiEquilibriumResult
    {
        std::vector<EquilibriumResult> equilibria;
        SolverStatus status = SolverStatus::NotRun;
        std::string solver_name;
        int total_iterations = 0;

        void print(std::ostream &os = std::cout) const;
    };

} // namespace nash
