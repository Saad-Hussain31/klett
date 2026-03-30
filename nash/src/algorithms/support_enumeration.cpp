#include "nash/algorithms/support_enumeration.hpp"
#include "nash/numerics/linear_algebra.hpp"
#include <algorithm>
#include <cmath>

namespace nash
{

    // ── Utility: enumerate subsets of {0,...,n-1} of size k ──────────────

    namespace
    {

        /// Generate the next k-subset in lexicographic order. Returns false when done.
        bool next_subset(std::vector<std::size_t> &S, std::size_t n)
        {
            std::size_t k = S.size();
            if (k == 0)
                return false;

            // Find the rightmost element that can be incremented
            std::size_t i = k;
            while (i-- > 0)
            {
                if (S[i] < n - k + i)
                {
                    ++S[i];
                    for (std::size_t j = i + 1; j < k; ++j)
                        S[j] = S[j - 1] + 1;
                    return true;
                }
            }
            return false;
        }

        /// Initialize the first k-subset {0, 1, ..., k-1}.
        std::vector<std::size_t> first_subset(std::size_t k)
        {
            std::vector<std::size_t> S(k);
            for (std::size_t i = 0; i < k; ++i)
                S[i] = i;
            return S;
        }

    } // anonymous namespace

    // ── Support Enumeration implementation ───────────────────────────────

    std::optional<StrategyProfile> SupportEnumeration::try_support(
        const BimatrixGame &game,
        const std::vector<std::size_t> &supp1,
        const std::vector<std::size_t> &supp2) const
    {
        std::size_t k1 = supp1.size();
        std::size_t k2 = supp2.size();
        const auto &A = game.payoff_1();
        const auto &B = game.payoff_2();

        // ── Solve for Player 2's strategy (y) ───────────────────────────
        // Player 1's indifference: for all i in supp1, (Ay)_i = v1
        // Plus normalization: sum y_j = 1 for j in supp2
        //
        // System for y: (k1 + 1) equations in (k2 + 1) unknowns (y_{supp2}, v1)
        // We need k1 = k2 for a square system (k1 indif + 1 norm = k2 + 1 unknowns).

        if (k1 != k2)
        {
            // Non-square support: the system still works for k1 indifference eqs.
            // We build: (k1-1) difference equations + 1 normalization = k2 eqs in k2 unknowns.
            // But only if k1 >= 2.
            // For simplicity, we only handle the case where we solve for y and v1 simultaneously.
        }

        // Build system for y (Player 2's strategy on support):
        // Indifference: A[supp1[i], supp2] * y = v1 * 1  for i in supp1
        //   => A_sub * y - v1 * e = 0  (k1 equations)
        // Normalization: 1^T * y = 1   (1 equation)
        // Total: (k1 + 1) equations, (k2 + 1) unknowns [y_0,...,y_{k2-1}, v1]

        std::size_t sys_size = k1 + 1;
        if (sys_size != k2 + 1)
            return std::nullopt; // Need k1 = k2

        Mat M(sys_size, sys_size, 0.0);
        Vec rhs(sys_size, 0.0);

        // Indifference equations: A[supp1[i], supp2[j]] * y_j - v1 = 0
        for (std::size_t i = 0; i < k1; ++i)
        {
            for (std::size_t j = 0; j < k2; ++j)
                M(i, j) = A(supp1[i], supp2[j]);
            M(i, k2) = -1.0; // coefficient of v1
            rhs[i] = 0.0;
        }

        // Normalization: sum y_j = 1
        for (std::size_t j = 0; j < k2; ++j)
            M(k1, j) = 1.0;
        rhs[k1] = 1.0;

        auto sol_y = linalg::solve(M, rhs, tol_.pivot_tol);
        if (!sol_y)
            return std::nullopt;

        // Check y >= 0
        for (std::size_t j = 0; j < k2; ++j)
            if ((*sol_y)[j] < -tol_.eps)
                return std::nullopt;

        // ── Solve for Player 1's strategy (x) ───────────────────────────
        // Player 2's indifference: for all j in supp2, (B^T x)_j = v2
        // Normalization: sum x_i = 1

        Mat M2(sys_size, sys_size, 0.0);
        Vec rhs2(sys_size, 0.0);

        for (std::size_t j = 0; j < k2; ++j)
        {
            for (std::size_t i = 0; i < k1; ++i)
                M2(j, i) = B(supp1[i], supp2[j]);
            M2(j, k1) = -1.0; // coefficient of v2
            rhs2[j] = 0.0;
        }

        for (std::size_t i = 0; i < k1; ++i)
            M2(k2, i) = 1.0;
        rhs2[k2] = 1.0;

        auto sol_x = linalg::solve(M2, rhs2, tol_.pivot_tol);
        if (!sol_x)
            return std::nullopt;

        // Check x >= 0
        for (std::size_t i = 0; i < k1; ++i)
            if ((*sol_x)[i] < -tol_.eps)
                return std::nullopt;

        // ── Build full strategy vectors ──────────────────────────────────
        StrategyProfile profile;
        profile.strategy_1.assign(game.num_actions_1(), 0.0);
        profile.strategy_2.assign(game.num_actions_2(), 0.0);

        for (std::size_t i = 0; i < k1; ++i)
            profile.strategy_1[supp1[i]] = std::max(0.0, (*sol_x)[i]);
        for (std::size_t j = 0; j < k2; ++j)
            profile.strategy_2[supp2[j]] = std::max(0.0, (*sol_y)[j]);

        return profile;
    }

    bool SupportEnumeration::check_best_response(
        const BimatrixGame &game,
        const StrategyProfile &profile,
        const std::vector<std::size_t> &supp1,
        const std::vector<std::size_t> &supp2) const
    {
        const auto &A = game.payoff_1();
        const auto &B = game.payoff_2();
        std::size_t m = game.num_actions_1();
        std::size_t n = game.num_actions_2();

        // Compute expected payoff for each P1 action against y
        Vec Ay = linalg::matvec(A, profile.strategy_2);

        // Find the max payoff on support
        double v1 = -1e300;
        for (auto i : supp1)
            v1 = std::max(v1, Ay[i]);

        // Check: no off-support action gets more than v1
        for (std::size_t i = 0; i < m; ++i)
        {
            if (Ay[i] > v1 + tol_.eps)
                return false;
        }

        // Same for P2
        Mat Bt = linalg::transpose(B);
        Vec Btx = linalg::matvec(Bt, profile.strategy_1);

        double v2 = -1e300;
        for (auto j : supp2)
            v2 = std::max(v2, Btx[j]);

        for (std::size_t j = 0; j < n; ++j)
        {
            if (Btx[j] > v2 + tol_.eps)
                return false;
        }

        return true;
    }

    MultiEquilibriumResult SupportEnumeration::solve_all(const BimatrixGame &game)
    {
        MultiEquilibriumResult result;
        result.solver_name = name();
        result.total_iterations = 0;

        std::size_t m = game.num_actions_1();
        std::size_t n = game.num_actions_2();

        // Enumerate all support sizes k from 1 to min(m, n)
        std::size_t max_k = std::min(m, n);

        for (std::size_t k = 1; k <= max_k; ++k)
        {
            // Enumerate all k-subsets of {0,...,m-1} for P1
            auto s1 = first_subset(k);
            do
            {
                // Enumerate all k-subsets of {0,...,n-1} for P2
                auto s2 = first_subset(k);
                do
                {
                    ++result.total_iterations;

                    auto profile = try_support(game, s1, s2);
                    if (profile && check_best_response(game, *profile, s1, s2))
                    {
                        // Check for duplicates
                        bool duplicate = false;
                        for (const auto &eq : result.equilibria)
                        {
                            bool same = true;
                            for (std::size_t i = 0; i < m && same; ++i)
                                if (!approx_eq(eq.profile.strategy_1[i], profile->strategy_1[i], tol_.eps))
                                    same = false;
                            for (std::size_t j = 0; j < n && same; ++j)
                                if (!approx_eq(eq.profile.strategy_2[j], profile->strategy_2[j], tol_.eps))
                                    same = false;
                            if (same)
                            {
                                duplicate = true;
                                break;
                            }
                        }

                        if (!duplicate)
                        {
                            EquilibriumResult eq;
                            eq.profile = std::move(*profile);
                            eq.solver_name = name();
                            eq.status = SolverStatus::Converged;
                            eq.payoff_1 = game.expected_payoff_1(eq.profile.strategy_1, eq.profile.strategy_2);
                            eq.payoff_2 = game.expected_payoff_2(eq.profile.strategy_1, eq.profile.strategy_2);
                            eq.residual = nash_residual(game, eq.profile.strategy_1, eq.profile.strategy_2);
                            result.equilibria.push_back(std::move(eq));
                        }
                    }
                } while (next_subset(s2, n));
            } while (next_subset(s1, m));
        }

        result.status = result.equilibria.empty() ? SolverStatus::Infeasible : SolverStatus::Converged;
        return result;
    }

    EquilibriumResult SupportEnumeration::solve_one(const BimatrixGame &game)
    {
        auto multi = solve_all(game);
        if (multi.equilibria.empty())
        {
            EquilibriumResult r;
            r.solver_name = name();
            r.status = SolverStatus::Infeasible;
            return r;
        }
        return multi.equilibria[0];
    }

    // ── Shared utility: Nash residual and verification ──────────────────

    double nash_residual(const BimatrixGame &game, const Vec &x, const Vec &y)
    {
        const auto &A = game.payoff_1();
        const auto &B = game.payoff_2();

        // P1 expected payoffs per action
        Vec Ay = linalg::matvec(A, y);
        double v1 = linalg::dot(x, Ay); // expected payoff under mixed x

        double max_regret = 0.0;
        for (std::size_t i = 0; i < game.num_actions_1(); ++i)
            max_regret = std::max(max_regret, Ay[i] - v1);

        // P2 expected payoffs per action
        Mat Bt = linalg::transpose(B);
        Vec Btx = linalg::matvec(Bt, x);
        double v2 = linalg::dot(y, Btx);

        for (std::size_t j = 0; j < game.num_actions_2(); ++j)
            max_regret = std::max(max_regret, Btx[j] - v2);

        return max_regret;
    }

    bool verify_equilibrium(const BimatrixGame &game,
                            const StrategyProfile &profile,
                            double epsilon)
    {
        return profile.is_valid(epsilon) &&
               nash_residual(game, profile.strategy_1, profile.strategy_2) < epsilon;
    }

} // namespace nash
