#pragma once
/// @file solver_interface.hpp
/// Abstract interfaces for equilibrium solvers.

#include "nash/core/game.hpp"
#include "nash/core/strategy.hpp"

namespace nash
{

    /// Interface for solvers that find a single Nash equilibrium.
    class IEquilibriumSolver
    {
    public:
        virtual ~IEquilibriumSolver() = default;

        /// Solve for one Nash equilibrium.
        virtual EquilibriumResult solve(const BimatrixGame &game) = 0;

        /// Solver name for diagnostics.
        virtual const char *name() const = 0;
    };

    /// Interface for solvers that enumerate multiple/all Nash equilibria.
    class IMultiEquilibriumSolver
    {
    public:
        virtual ~IMultiEquilibriumSolver() = default;

        /// Find all (or many) Nash equilibria.
        virtual MultiEquilibriumResult solve_all(const BimatrixGame &game) = 0;

        virtual const char *name() const = 0;
    };

    /// Compute the Nash equilibrium residual: max regret over all actions.
    /// Returns 0 if (x, y) is an exact Nash equilibrium.
    double nash_residual(const BimatrixGame &game,
                         const Vec &x, const Vec &y);

    /// Verify that a strategy profile is an epsilon-Nash equilibrium.
    bool verify_equilibrium(const BimatrixGame &game,
                            const StrategyProfile &profile,
                            double epsilon = 1e-6);

} // namespace nash
