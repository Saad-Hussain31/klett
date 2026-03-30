#pragma once
/// @file support_enumeration.hpp
/// Support enumeration method for finding ALL Nash equilibria of bimatrix games.

#include "nash/algorithms/solver_interface.hpp"
#include <optional>

namespace nash
{

    /// Support Enumeration: enumerates all possible support pairs and solves
    /// the indifference equations for each. Finds all Nash equilibria.
    ///
    /// Complexity: O(2^(m+n)) support pairs, each requiring O(k^3) linear solve.
    /// Only practical for small games (m, n <= ~15).
    class SupportEnumeration : public IMultiEquilibriumSolver
    {
    public:
        explicit SupportEnumeration(Tolerances tol = kDefaultTol) : tol_(tol) {}

        MultiEquilibriumResult solve_all(const BimatrixGame &game) override;
        const char *name() const override { return "SupportEnumeration"; }

        /// Also implement single-equilibrium interface (returns first found).
        EquilibriumResult solve_one(const BimatrixGame &game);

    private:
        Tolerances tol_;

        /// Try to find an equilibrium with given support pair.
        /// Returns nullopt if infeasible.
        std::optional<StrategyProfile> try_support(
            const BimatrixGame &game,
            const std::vector<std::size_t> &support_1,
            const std::vector<std::size_t> &support_2) const;

        /// Check best-response condition: no off-support action is profitable.
        bool check_best_response(
            const BimatrixGame &game,
            const StrategyProfile &profile,
            const std::vector<std::size_t> &support_1,
            const std::vector<std::size_t> &support_2) const;
    };

} // namespace nash
