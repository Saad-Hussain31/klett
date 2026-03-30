#pragma once
/// @file lemke_howson.hpp
/// Lemke-Howson algorithm for finding one Nash equilibrium of a bimatrix game.

#include "nash/algorithms/solver_interface.hpp"

namespace nash
{

    /// Lemke-Howson: complementary pivoting algorithm for 2-player games.
    ///
    /// Uses the best-response polytope formulation:
    ///   P1 = {x >= 0 : B^T x <= 1}
    ///   P2 = {y >= 0 : A  y <= 1}
    ///
    /// Requires strictly positive payoffs (auto-shifted if needed).
    /// Guaranteed to find one Nash equilibrium.
    /// Different initial label drops may find different equilibria.
    class LemkeHowson : public IEquilibriumSolver
    {
    public:
        /// @param drop_label  Which label to initially drop (0-indexed).
        ///                    If negative, uses label 0.
        /// @param tol         Numerical tolerances.
        explicit LemkeHowson(int drop_label = 0, Tolerances tol = kDefaultTol)
            : drop_label_(drop_label), tol_(tol) {}

        EquilibriumResult solve(const BimatrixGame &game) override;
        const char *name() const override { return "LemkeHowson"; }

        /// Find multiple equilibria by trying all m+n label drops.
        MultiEquilibriumResult solve_multiple(const BimatrixGame &game);

    private:
        int drop_label_;
        Tolerances tol_;

        /// Internal tableau for one polytope.
        struct Tableau
        {
            Mat tab; ///< Tableau matrix (rows x (m+n+1)), last col = RHS
            std::size_t num_rows;
            std::size_t num_vars;           ///< Total variable count (m + n)
            std::vector<std::size_t> basis; ///< basis[row] = variable index in basis

            /// Initialize from constraint matrix and RHS.
            void init(const Mat &constraint, const Vec &rhs,
                      const std::vector<std::size_t> &initial_basis);

            /// Perform a pivot: enter variable `enter_var`, return the leaving variable.
            /// Uses minimum ratio test. Returns SIZE_MAX on failure.
            std::size_t pivot(std::size_t enter_var, double tol);

            /// Get the value of a variable (0 if non-basic).
            double get_value(std::size_t var) const;

            /// Get the set of labels at this vertex.
            /// For P1: own vars (0..m-1), slack vars map to labels (m..m+n-1).
            /// For P2: own vars (m..m+n-1), slack vars map to labels (0..m-1).
            std::vector<bool> get_zero_labels(std::size_t m, std::size_t n,
                                              bool is_player1, double tol) const;
        };

        /// Run the complementary pivoting given a specific label to drop.
        EquilibriumResult run_pivoting(const BimatrixGame &game, int label) const;
    };

} // namespace nash
