#include "nash/algorithms/lemke_howson.hpp"
#include "nash/numerics/linear_algebra.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace nash
{

    // ── Tableau methods ─────────────────────────────────────────────────

    void LemkeHowson::Tableau::init(const Mat &constraint, const Vec &rhs,
                                    const std::vector<std::size_t> &initial_basis)
    {
        num_rows = constraint.rows;
        num_vars = constraint.cols; // includes slack variables
        basis = initial_basis;

        // Tableau: [constraint | rhs]
        tab = Mat(num_rows, num_vars + 1);
        for (std::size_t i = 0; i < num_rows; ++i)
        {
            for (std::size_t j = 0; j < num_vars; ++j)
                tab(i, j) = constraint(i, j);
            tab(i, num_vars) = rhs[i];
        }
    }

    std::size_t LemkeHowson::Tableau::pivot(std::size_t enter_var, double tol)
    {
        // Minimum ratio test to find leaving variable
        std::size_t pivot_row = std::numeric_limits<std::size_t>::max();
        double min_ratio = std::numeric_limits<double>::max();

        for (std::size_t i = 0; i < num_rows; ++i)
        {
            double coeff = tab(i, enter_var);
            if (coeff > tol)
            {
                double ratio = tab(i, num_vars) / coeff;
                if (ratio < min_ratio - tol)
                {
                    min_ratio = ratio;
                    pivot_row = i;
                }
            }
        }

        if (pivot_row == std::numeric_limits<std::size_t>::max())
            return std::numeric_limits<std::size_t>::max(); // unbounded

        std::size_t leave_var = basis[pivot_row];

        // Perform the pivot operation
        double pivot_elem = tab(pivot_row, enter_var);

        // Normalize pivot row
        for (std::size_t j = 0; j <= num_vars; ++j)
            tab(pivot_row, j) /= pivot_elem;

        // Eliminate enter_var from other rows
        for (std::size_t i = 0; i < num_rows; ++i)
        {
            if (i == pivot_row)
                continue;
            double factor = tab(i, enter_var);
            if (std::fabs(factor) > tol)
            {
                for (std::size_t j = 0; j <= num_vars; ++j)
                    tab(i, j) -= factor * tab(pivot_row, j);
            }
        }

        basis[pivot_row] = enter_var;
        return leave_var;
    }

    double LemkeHowson::Tableau::get_value(std::size_t var) const
    {
        for (std::size_t i = 0; i < num_rows; ++i)
        {
            if (basis[i] == var)
                return tab(i, num_vars);
        }
        return 0.0; // non-basic → zero
    }

    std::vector<bool> LemkeHowson::Tableau::get_zero_labels(
        std::size_t m, std::size_t n, bool is_player1, double tol) const
    {
        // Total labels: 0..m+n-1
        // For Player 1 tableau:
        //   Variables 0..m-1 are x_0..x_{m-1} (own strategies)
        //   Variables m..m+n-1 are s_0..s_{n-1} (slack for B^T x <= 1)
        //   Label i (i < m): contributed when x_i = 0 (non-basic or basic with value 0)
        //   Label m+j (j < n): contributed when s_j = 0

        // For Player 2 tableau:
        //   Variables 0..n-1 are y_0..y_{n-1} (own strategies)
        //   Variables n..n+m-1 are r_0..r_{m-1} (slack for A y <= 1)
        //   Label m+j (j < n): contributed when y_j = 0
        //   Label i (i < m): contributed when r_i = 0

        std::vector<bool> labels(m + n, false);

        if (is_player1)
        {
            // Own vars: x_i, i = 0..m-1 → label i if x_i ≈ 0
            for (std::size_t i = 0; i < m; ++i)
            {
                if (get_value(i) < tol)
                    labels[i] = true;
            }
            // Slack vars: s_j, j = 0..n-1 → label m+j if s_j ≈ 0
            for (std::size_t j = 0; j < n; ++j)
            {
                if (get_value(m + j) < tol)
                    labels[m + j] = true;
            }
        }
        else
        {
            // Own vars: y_j, j = 0..n-1 → label m+j if y_j ≈ 0
            for (std::size_t j = 0; j < n; ++j)
            {
                if (get_value(j) < tol)
                    labels[m + j] = true;
            }
            // Slack vars: r_i, i = 0..m-1 → label i if r_i ≈ 0
            for (std::size_t i = 0; i < m; ++i)
            {
                if (get_value(n + i) < tol)
                    labels[i] = true;
            }
        }

        return labels;
    }

    // ── Lemke-Howson main logic ─────────────────────────────────────────

    EquilibriumResult LemkeHowson::run_pivoting(const BimatrixGame &game_orig, int label) const
    {
        // Make a copy and ensure positive payoffs
        BimatrixGame game = game_orig;
        game.make_positive();

        std::size_t m = game.num_actions_1();
        std::size_t n = game.num_actions_2();

        const auto &A = game.payoff_1();
        const auto &B = game.payoff_2();

        // ── Build Tableau for Player 1 ──────────────────────────────────
        // Polytope P1: B^T x + s = 1, x >= 0, s >= 0
        // B^T is n×m. Constraint matrix has n rows, m+n columns:
        //   [B^T | I_n]  (columns: x_0..x_{m-1}, s_0..s_{n-1})
        Mat C1(n, m + n, 0.0);
        Vec rhs1(n, 1.0);
        for (std::size_t j = 0; j < n; ++j)
        {
            for (std::size_t i = 0; i < m; ++i)
                C1(j, i) = B(i, j); // B^T
            C1(j, m + j) = 1.0;     // slack
        }
        std::vector<std::size_t> basis1(n);
        for (std::size_t j = 0; j < n; ++j)
            basis1[j] = m + j; // slacks in basis

        Tableau tab1;
        tab1.init(C1, rhs1, basis1);

        // ── Build Tableau for Player 2 ──────────────────────────────────
        // Polytope P2: A y + r = 1, y >= 0, r >= 0
        // A is m×n. Constraint matrix has m rows, n+m columns:
        //   [A | I_m]  (columns: y_0..y_{n-1}, r_0..r_{m-1})
        Mat C2(m, n + m, 0.0);
        Vec rhs2(m, 1.0);
        for (std::size_t i = 0; i < m; ++i)
        {
            for (std::size_t j = 0; j < n; ++j)
                C2(i, j) = A(i, j);
            C2(i, n + i) = 1.0; // slack
        }
        std::vector<std::size_t> basis2(m);
        for (std::size_t i = 0; i < m; ++i)
            basis2[i] = n + i; // slacks in basis

        Tableau tab2;
        tab2.init(C2, rhs2, basis2);

        // ── Complementary pivoting ──────────────────────────────────────
        // Initial vertex: x = 0, y = 0
        // P1 labels from x=0: {0, ..., m-1}
        // P2 labels from y=0: {m, ..., m+n-1}
        // Completely labeled.

        // Drop label `label` and start pivoting.
        std::size_t dropped_label = static_cast<std::size_t>(label);

        // Determine which tableau gets the first pivot
        bool pivot_in_1; // true if we pivot in P1's tableau
        std::size_t enter_var;

        if (dropped_label < m)
        {
            // Label is from P1's own variables (x_i = 0 gives label i)
            // Drop from P1: enter x_{label} into P1's basis
            pivot_in_1 = true;
            enter_var = dropped_label; // variable x_i in P1's tableau
        }
        else
        {
            // Label is from P2's own variables (y_j = 0 gives label m+j)
            // Drop from P2: enter y_{label-m} into P2's basis
            pivot_in_1 = false;
            enter_var = dropped_label - m; // variable y_j in P2's tableau
        }

        EquilibriumResult result;
        result.solver_name = name();

        for (int iter = 0; iter < tol_.max_iter; ++iter)
        {
            result.iterations = iter + 1;

            // Perform pivot in the designated tableau
            std::size_t leave_var;
            std::size_t new_label;

            if (pivot_in_1)
            {
                leave_var = tab1.pivot(enter_var, tol_.eps);
                if (leave_var == std::numeric_limits<std::size_t>::max())
                {
                    result.status = SolverStatus::NumericalFailure;
                    return result;
                }
                // What label does the leaving variable contribute?
                if (leave_var < m)
                {
                    new_label = leave_var; // x_i = 0 → label i
                }
                else
                {
                    new_label = leave_var; // s_j has index m+j, label m+j ... wait
                    // In P1's tableau, slack s_j has variable index m+j, and label m+j
                    new_label = leave_var; // already m+j since var index = m + j
                }
            }
            else
            {
                leave_var = tab2.pivot(enter_var, tol_.eps);
                if (leave_var == std::numeric_limits<std::size_t>::max())
                {
                    result.status = SolverStatus::NumericalFailure;
                    return result;
                }
                // In P2's tableau:
                if (leave_var < n)
                {
                    new_label = m + leave_var; // y_j = 0 → label m+j
                }
                else
                {
                    new_label = leave_var - n; // r_i has var index n+i, label i
                }
            }

            // Check if completely labeled again
            if (new_label == dropped_label)
            {
                // Found a Nash equilibrium!
                // Extract strategies

                // P1 strategy from tab1: x_i values
                Vec x_raw(m);
                double x_sum = 0.0;
                for (std::size_t i = 0; i < m; ++i)
                {
                    x_raw[i] = std::max(0.0, tab1.get_value(i));
                    x_sum += x_raw[i];
                }

                // P2 strategy from tab2: y_j values
                Vec y_raw(n);
                double y_sum = 0.0;
                for (std::size_t j = 0; j < n; ++j)
                {
                    y_raw[j] = std::max(0.0, tab2.get_value(j));
                    y_sum += y_raw[j];
                }

                // Normalize to probability distributions
                result.profile.strategy_1.resize(m);
                result.profile.strategy_2.resize(n);

                if (x_sum > tol_.eps)
                {
                    for (std::size_t i = 0; i < m; ++i)
                        result.profile.strategy_1[i] = x_raw[i] / x_sum;
                }
                else
                {
                    // Degenerate: P1 at origin, use uniform
                    for (std::size_t i = 0; i < m; ++i)
                        result.profile.strategy_1[i] = 1.0 / static_cast<double>(m);
                }

                if (y_sum > tol_.eps)
                {
                    for (std::size_t j = 0; j < n; ++j)
                        result.profile.strategy_2[j] = y_raw[j] / y_sum;
                }
                else
                {
                    for (std::size_t j = 0; j < n; ++j)
                        result.profile.strategy_2[j] = 1.0 / static_cast<double>(n);
                }

                result.status = SolverStatus::Converged;
                result.payoff_1 = game_orig.expected_payoff_1(result.profile.strategy_1,
                                                              result.profile.strategy_2);
                result.payoff_2 = game_orig.expected_payoff_2(result.profile.strategy_1,
                                                              result.profile.strategy_2);
                result.residual = nash_residual(game_orig, result.profile.strategy_1,
                                                result.profile.strategy_2);
                return result;
            }

            // The new label is duplicate. Drop it from the OTHER tableau.

            if (pivot_in_1)
            {
                // We just pivoted in P1. Now pivot in P2 to drop the duplicate label.
                pivot_in_1 = false;
                if (new_label < m)
                {
                    // Label i (P1 action) → in P2, it comes from slack r_i (var index n+i)
                    enter_var = n + new_label;
                }
                else
                {
                    // Label m+j (P2 action) → in P2, it comes from y_j (var index j)
                    enter_var = new_label - m;
                }
            }
            else
            {
                // Just pivoted in P2. Now pivot in P1.
                pivot_in_1 = true;
                if (new_label < m)
                {
                    // Label i → in P1, it comes from x_i (var index i)
                    enter_var = new_label;
                }
                else
                {
                    // Label m+j → in P1, it comes from slack s_j (var index m+j)
                    enter_var = new_label;
                }
            }
        }

        result.status = SolverStatus::MaxIterations;
        return result;
    }

    EquilibriumResult LemkeHowson::solve(const BimatrixGame &game)
    {
        int lbl = drop_label_;
        if (lbl < 0)
            lbl = 0;
        auto total = static_cast<int>(game.num_actions_1() + game.num_actions_2());
        if (lbl >= total)
            lbl = 0;
        return run_pivoting(game, lbl);
    }

    MultiEquilibriumResult LemkeHowson::solve_multiple(const BimatrixGame &game)
    {
        MultiEquilibriumResult result;
        result.solver_name = name();

        std::size_t total = game.num_actions_1() + game.num_actions_2();

        for (std::size_t lbl = 0; lbl < total; ++lbl)
        {
            auto eq = run_pivoting(game, static_cast<int>(lbl));
            result.total_iterations += eq.iterations;

            if (eq.status == SolverStatus::Converged)
            {
                // Check for duplicates
                bool dup = false;
                for (const auto &existing : result.equilibria)
                {
                    bool same = true;
                    for (std::size_t i = 0; i < eq.profile.strategy_1.size() && same; ++i)
                        if (!approx_eq(eq.profile.strategy_1[i], existing.profile.strategy_1[i], tol_.eps))
                            same = false;
                    for (std::size_t j = 0; j < eq.profile.strategy_2.size() && same; ++j)
                        if (!approx_eq(eq.profile.strategy_2[j], existing.profile.strategy_2[j], tol_.eps))
                            same = false;
                    if (same)
                    {
                        dup = true;
                        break;
                    }
                }
                if (!dup)
                    result.equilibria.push_back(std::move(eq));
            }
        }

        result.status = result.equilibria.empty() ? SolverStatus::NumericalFailure : SolverStatus::Converged;
        return result;
    }

} // namespace nash
