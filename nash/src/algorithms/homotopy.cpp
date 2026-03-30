#include "nash/algorithms/homotopy.hpp"
#include "nash/numerics/linear_algebra.hpp"
#include "nash/numerics/newton_solver.hpp"
#include <algorithm>
#include <cmath>

namespace nash
{

    HomotopySolver::HomotopySolver(Vec prior_1, Vec prior_2,
                                   PathFollowerConfig cfg, Tolerances tol)
        : prior_1_(std::move(prior_1)), prior_2_(std::move(prior_2)),
          cfg_(cfg), tol_(tol) {}

    Vec HomotopySolver::compute_start(
        const BimatrixGame &game,
        std::vector<std::size_t> &support_1,
        std::vector<std::size_t> &support_2) const
    {
        std::size_t m = game.num_actions_1();
        std::size_t n = game.num_actions_2();
        const auto &A = game.payoff_1();
        const auto &B = game.payoff_2();

        // At t=0, each player best-responds to the prior about their opponent.
        // P1 best responds to prior_2 (prior about P2).
        // P2 best responds to prior_1 (prior about P1).

        // P1's payoff from action i when P2 plays prior_2:
        Vec Ap2 = linalg::matvec(A, prior_2_);

        // Find the best-response set for P1
        double max_v1 = *std::max_element(Ap2.begin(), Ap2.end());
        support_1.clear();
        for (std::size_t i = 0; i < m; ++i)
        {
            if (Ap2[i] > max_v1 - tol_.eps)
                support_1.push_back(i);
        }

        // P2's payoff from action j when P1 plays prior_1:
        Mat Bt = linalg::transpose(B);
        Vec Btp1 = linalg::matvec(Bt, prior_1_);

        double max_v2 = *std::max_element(Btp1.begin(), Btp1.end());
        support_2.clear();
        for (std::size_t j = 0; j < n; ++j)
        {
            if (Btp1[j] > max_v2 - tol_.eps)
                support_2.push_back(j);
        }

        // Starting strategy: uniform over best-response set.
        std::size_t k1 = support_1.size();
        std::size_t k2 = support_2.size();

        // Build augmented variable: w = (x_{S1}, y_{S2}, v1, v2, t)
        // At t=0: x uniform on S1, y uniform on S2, v1 = max_v1, v2 = max_v2
        std::size_t dim = k1 + k2 + 3; // +v1 +v2 +t
        Vec w0(dim);

        for (std::size_t i = 0; i < k1; ++i)
            w0[i] = 1.0 / static_cast<double>(k1);
        for (std::size_t j = 0; j < k2; ++j)
            w0[k1 + j] = 1.0 / static_cast<double>(k2);
        w0[k1 + k2] = max_v1;     // v1
        w0[k1 + k2 + 1] = max_v2; // v2
        w0[k1 + k2 + 2] = 0.0;    // t = 0

        return w0;
    }

    HomotopySystem HomotopySolver::build_homotopy(
        const BimatrixGame &game,
        const std::vector<std::size_t> &support_1,
        const std::vector<std::size_t> &support_2) const
    {
        std::size_t k1 = support_1.size();
        std::size_t k2 = support_2.size();

        const auto &A = game.payoff_1();
        const auto &B = game.payoff_2();

        // Pre-compute prior payoff vectors
        Vec Ap2 = linalg::matvec(A, prior_2_);
        Mat Bt = linalg::transpose(B);
        Vec Btp1 = linalg::matvec(Bt, prior_1_);

        // Variable layout: w = (x_{S1[0]}, ..., x_{S1[k1-1]},
        //                       y_{S2[0]}, ..., y_{S2[k2-1]},
        //                       v1, v2, t)
        // Indices:
        //   x_i: index i       (0 <= i < k1)
        //   y_j: index k1 + j  (0 <= j < k2)
        //   v1:  index k1 + k2
        //   v2:  index k1 + k2 + 1
        //   t:   index k1 + k2 + 2

        // Equations (total k1 + k2 + 2):
        //   H_i = t * sum_j A(S1[i], S2[j]) * y_j + (1-t) * Ap2[S1[i]] - v1 = 0
        //         for i = 0, ..., k1-1
        //   H_{k1+j} = t * sum_i B(S1[i], S2[j]) * x_i + (1-t) * Btp1[S2[j]] - v2 = 0
        //         for j = 0, ..., k2-1
        //   H_{k1+k2} = sum_i x_i - 1 = 0
        //   H_{k1+k2+1} = sum_j y_j - 1 = 0

        // Total: k1 + k2 + 2 equations in k1 + k2 + 3 variables → 1D curve.

        HomotopySystem sys;
        sys.n = k1 + k2 + 2;

        // Capture by value for safety
        auto s1 = support_1;
        auto s2 = support_2;
        auto ap2 = Ap2;
        auto btp1 = Btp1;

        sys.H = [=](const Vec &w) -> Vec
        {
            Vec h(k1 + k2 + 2);

            double t = w[k1 + k2 + 2];
            double v1 = w[k1 + k2];
            double v2 = w[k1 + k2 + 1];

            // P1 indifference equations
            for (std::size_t i = 0; i < k1; ++i)
            {
                double payoff_t = 0.0;
                for (std::size_t j = 0; j < k2; ++j)
                    payoff_t += A(s1[i], s2[j]) * w[k1 + j];
                h[i] = t * payoff_t + (1.0 - t) * ap2[s1[i]] - v1;
            }

            // P2 indifference equations
            for (std::size_t j = 0; j < k2; ++j)
            {
                double payoff_t = 0.0;
                for (std::size_t i = 0; i < k1; ++i)
                    payoff_t += B(s1[i], s2[j]) * w[i];
                h[k1 + j] = t * payoff_t + (1.0 - t) * btp1[s2[j]] - v2;
            }

            // Normalization
            double sum_x = 0.0;
            for (std::size_t i = 0; i < k1; ++i)
                sum_x += w[i];
            h[k1 + k2] = sum_x - 1.0;

            double sum_y = 0.0;
            for (std::size_t j = 0; j < k2; ++j)
                sum_y += w[k1 + j];
            h[k1 + k2 + 1] = sum_y - 1.0;

            return h;
        };

        sys.DH = [=](const Vec &w) -> Mat
        {
            std::size_t neq = k1 + k2 + 2;
            std::size_t nvar = k1 + k2 + 3;
            Mat J(neq, nvar, 0.0);

            double t = w[k1 + k2 + 2];

            // ∂H_i/∂x_l = 0 (P1's payoff doesn't depend on own strategy)
            // ∂H_i/∂y_j = t * A(S1[i], S2[j])
            for (std::size_t i = 0; i < k1; ++i)
            {
                for (std::size_t j = 0; j < k2; ++j)
                    J(i, k1 + j) = t * A(s1[i], s2[j]);
                J(i, k1 + k2) = -1.0; // ∂H_i/∂v1
                // ∂H_i/∂v2 = 0
                // ∂H_i/∂t
                double A_y = 0.0;
                for (std::size_t j = 0; j < k2; ++j)
                    A_y += A(s1[i], s2[j]) * w[k1 + j];
                J(i, k1 + k2 + 2) = A_y - ap2[s1[i]];
            }

            // ∂H_{k1+j}/∂x_i = t * B(S1[i], S2[j])
            // ∂H_{k1+j}/∂y_l = 0
            for (std::size_t j = 0; j < k2; ++j)
            {
                for (std::size_t i = 0; i < k1; ++i)
                    J(k1 + j, i) = t * B(s1[i], s2[j]);
                // ∂H_{k1+j}/∂v1 = 0
                J(k1 + j, k1 + k2 + 1) = -1.0; // ∂/∂v2
                // ∂H_{k1+j}/∂t
                double B_x = 0.0;
                for (std::size_t i = 0; i < k1; ++i)
                    B_x += B(s1[i], s2[j]) * w[i];
                J(k1 + j, k1 + k2 + 2) = B_x - btp1[s2[j]];
            }

            // Normalization rows
            for (std::size_t i = 0; i < k1; ++i)
                J(k1 + k2, i) = 1.0;
            for (std::size_t j = 0; j < k2; ++j)
                J(k1 + k2 + 1, k1 + j) = 1.0;

            return J;
        };

        return sys;
    }

    StrategyProfile HomotopySolver::extract_profile(
        const Vec &w,
        std::size_t m, std::size_t n,
        const std::vector<std::size_t> &support_1,
        const std::vector<std::size_t> &support_2) const
    {
        std::size_t k1 = support_1.size();
        std::size_t k2 = support_2.size();

        StrategyProfile profile;
        profile.strategy_1.assign(m, 0.0);
        profile.strategy_2.assign(n, 0.0);

        for (std::size_t i = 0; i < k1; ++i)
            profile.strategy_1[support_1[i]] = std::max(0.0, w[i]);
        for (std::size_t j = 0; j < k2; ++j)
            profile.strategy_2[support_2[j]] = std::max(0.0, w[k1 + j]);

        // Renormalize for numerical safety
        double sum1 = 0.0, sum2 = 0.0;
        for (auto v : profile.strategy_1)
            sum1 += v;
        for (auto v : profile.strategy_2)
            sum2 += v;
        if (sum1 > 1e-15)
            for (auto &v : profile.strategy_1)
                v /= sum1;
        if (sum2 > 1e-15)
            for (auto &v : profile.strategy_2)
                v /= sum2;

        return profile;
    }

    EquilibriumResult HomotopySolver::solve(const BimatrixGame &game)
    {
        std::size_t m = game.num_actions_1();
        std::size_t n = game.num_actions_2();

        // Initialize priors to slightly perturbed uniform if not set.
        // Pure uniform can cause degeneracy (rank-deficient Jacobians at t=0).
        if (prior_1_.empty())
        {
            prior_1_.resize(m);
            double base = 1.0 / static_cast<double>(m);
            double sum = 0.0;
            for (std::size_t i = 0; i < m; ++i) {
                prior_1_[i] = base + 0.01 * static_cast<double>(i + 1) / static_cast<double>(m);
                sum += prior_1_[i];
            }
            for (auto& v : prior_1_) v /= sum;
        }
        if (prior_2_.empty())
        {
            prior_2_.resize(n);
            double base = 1.0 / static_cast<double>(n);
            double sum = 0.0;
            for (std::size_t j = 0; j < n; ++j) {
                prior_2_[j] = base + 0.01 * static_cast<double>(j + 1) / static_cast<double>(n);
                sum += prior_2_[j];
            }
            for (auto& v : prior_2_) v /= sum;
        }

        EquilibriumResult result;
        result.solver_name = name();

        // Compute starting point and supports
        std::vector<std::size_t> supp1, supp2;
        Vec w0 = compute_start(game, supp1, supp2);

        // Build homotopy system
        auto sys = build_homotopy(game, supp1, supp2);

        // Verify starting point
        Vec h0 = sys.H(w0);
        double res0 = linalg::norm_inf(h0);
        if (res0 > 1e-6)
        {
            // Try Newton correction at t=0
            NonlinearSystem ns;
            ns.dim = sys.n + 1;
            ns.F = sys.H;
            // Use numerical Jacobian from the homotopy system's DH
            ns.J = [&sys](const Vec &w) -> Mat
            {
                Mat DH = sys.DH(w);
                // Remove last column (t) to make it square for correction
                // Actually, for a correction at fixed t, we need to work differently.
                // Instead, use the full augmented system with t pinned.
                // For now, accept the starting residual.
                return DH; // This won't work as-is for Newton (non-square)
            };
            // Skip Newton correction - the path follower will handle small residuals
        }

        // Follow the path from t=0 to t=1
        auto path_result = follow_path(sys, w0, cfg_);

        result.iterations = path_result.total_steps;

        if (path_result.reached_target || path_result.t_final > 1.0 - 1e-4)
        {
            // Extract the equilibrium at t=1
            // If we didn't quite reach t=1, do a final Newton correction
            Vec w_final = path_result.w_final;

            if (std::fabs(w_final.back() - 1.0) > 1e-8)
            {
                // Pin t=1 and do Newton correction
                w_final.back() = 1.0;
                // Newton on H with t fixed at 1
                std::size_t k1 = supp1.size();
                std::size_t k2 = supp2.size();

                NonlinearSystem pin_sys;
                pin_sys.dim = k1 + k2 + 2; // x, y, v1, v2 (t is fixed)
                pin_sys.F = [&sys, &w_final, k1, k2](const Vec &z) -> Vec
                {
                    Vec w(z.size() + 1);
                    for (std::size_t i = 0; i < z.size(); ++i)
                        w[i] = z[i];
                    w.back() = 1.0; // t = 1
                    return sys.H(w);
                };

                Vec z0(pin_sys.dim);
                for (std::size_t i = 0; i < pin_sys.dim; ++i)
                    z0[i] = w_final[i];

                auto nr = newton_solve(pin_sys, z0, tol_.conv_tol, 50);
                if (nr.converged)
                {
                    for (std::size_t i = 0; i < pin_sys.dim; ++i)
                        w_final[i] = nr.x[i];
                    w_final.back() = 1.0;
                }
            }

            result.profile = extract_profile(w_final, m, n, supp1, supp2);
            result.status = SolverStatus::Converged;
            result.payoff_1 = game.expected_payoff_1(result.profile.strategy_1,
                                                     result.profile.strategy_2);
            result.payoff_2 = game.expected_payoff_2(result.profile.strategy_1,
                                                     result.profile.strategy_2);
            result.residual = nash_residual(game, result.profile.strategy_1,
                                            result.profile.strategy_2);

            // If residual is too large, the homotopy may have failed
            if (result.residual > tol_.conv_tol)
            {
                // Try a final Newton polish on the full equilibrium conditions
                NonlinearSystem eq_sys;
                eq_sys.dim = m + n;
                eq_sys.F = [&game, m, n](const Vec &z) -> Vec
                {
                    Vec x(z.begin(), z.begin() + m);
                    Vec y(z.begin() + m, z.end());

                    const auto &A = game.payoff_1();
                    const auto &B = game.payoff_2();

                    Vec Ay = linalg::matvec(A, y);
                    Mat Bt = linalg::transpose(B);
                    Vec Btx = linalg::matvec(Bt, x);

                    // Find best response values
                    double v1 = *std::max_element(Ay.begin(), Ay.end());
                    double v2 = *std::max_element(Btx.begin(), Btx.end());

                    // Residual: for each action,
                    //   x_i * (v1 - Ay_i) = 0 and x_i >= 0 and v1 - Ay_i >= 0
                    Vec F(m + n);
                    for (std::size_t i = 0; i < m; ++i)
                        F[i] = x[i] * (v1 - Ay[i]); // complementarity
                    for (std::size_t j = 0; j < n; ++j)
                        F[m + j] = y[j] * (v2 - Btx[j]);
                    return F;
                };

                Vec z0(m + n);
                for (std::size_t i = 0; i < m; ++i)
                    z0[i] = result.profile.strategy_1[i];
                for (std::size_t j = 0; j < n; ++j)
                    z0[m + j] = result.profile.strategy_2[j];

                auto nr = newton_solve(eq_sys, z0, tol_.conv_tol, 50);
                if (nr.converged)
                {
                    for (std::size_t i = 0; i < m; ++i)
                        result.profile.strategy_1[i] = std::max(0.0, nr.x[i]);
                    for (std::size_t j = 0; j < n; ++j)
                        result.profile.strategy_2[j] = std::max(0.0, nr.x[m + j]);

                    // Renormalize
                    double s1 = 0, s2 = 0;
                    for (auto v : result.profile.strategy_1)
                        s1 += v;
                    for (auto v : result.profile.strategy_2)
                        s2 += v;
                    if (s1 > 1e-15)
                        for (auto &v : result.profile.strategy_1)
                            v /= s1;
                    if (s2 > 1e-15)
                        for (auto &v : result.profile.strategy_2)
                            v /= s2;

                    result.payoff_1 = game.expected_payoff_1(result.profile.strategy_1,
                                                             result.profile.strategy_2);
                    result.payoff_2 = game.expected_payoff_2(result.profile.strategy_1,
                                                             result.profile.strategy_2);
                    result.residual = nash_residual(game, result.profile.strategy_1,
                                                    result.profile.strategy_2);
                }
            }
        }
        else
        {
            result.status = SolverStatus::NumericalFailure;
            // Return best available result
            result.profile = extract_profile(path_result.w_final, m, n, supp1, supp2);
            result.residual = nash_residual(game, result.profile.strategy_1,
                                            result.profile.strategy_2);
        }

        // If the result has large residual, retry with different priors.
        // The initial prior may select a support that doesn't contain any NE.
        if (result.residual > tol_.conv_tol) {
            // Try a few different priors
            double perturbations[][2] = {{0.8, 0.2}, {0.2, 0.8}, {0.6, 0.4}};
            for (auto& pert : perturbations) {
                Vec p1(m), p2(n);
                double s1 = 0.0, s2 = 0.0;
                for (std::size_t i = 0; i < m; ++i) {
                    p1[i] = pert[0] + pert[1] * static_cast<double>(i) / std::max(1.0, static_cast<double>(m - 1));
                    s1 += p1[i];
                }
                for (std::size_t j = 0; j < n; ++j) {
                    p2[j] = pert[0] + pert[1] * static_cast<double>(j) / std::max(1.0, static_cast<double>(n - 1));
                    s2 += p2[j];
                }
                for (auto& v : p1) v /= s1;
                for (auto& v : p2) v /= s2;

                auto old_p1 = prior_1_;
                auto old_p2 = prior_2_;
                prior_1_ = p1;
                prior_2_ = p2;

                std::vector<std::size_t> retry_s1, retry_s2;
                Vec retry_w0 = compute_start(game, retry_s1, retry_s2);
                auto retry_sys = build_homotopy(game, retry_s1, retry_s2);
                auto retry_path = follow_path(retry_sys, retry_w0, cfg_);

                prior_1_ = old_p1;
                prior_2_ = old_p2;

                if (retry_path.reached_target || retry_path.t_final > 1.0 - 1e-4) {
                    Vec w_f = retry_path.w_final;
                    auto prof = extract_profile(w_f, m, n, retry_s1, retry_s2);
                    double res = nash_residual(game, prof.strategy_1, prof.strategy_2);
                    if (res < result.residual) {
                        result.profile = prof;
                        result.residual = res;
                        result.iterations += retry_path.total_steps;
                        result.payoff_1 = game.expected_payoff_1(prof.strategy_1, prof.strategy_2);
                        result.payoff_2 = game.expected_payoff_2(prof.strategy_1, prof.strategy_2);
                        if (res < tol_.conv_tol) {
                            result.status = SolverStatus::Converged;
                            break;
                        }
                    }
                }
            }
        }

        // If still not converged, retry with uniform prior (full support).
        // For zero-sum games, any non-uniform prior gives a pure best response,
        // but the NE may be fully mixed. Uniform prior makes all actions tied.
        if (result.residual > tol_.conv_tol) {
            auto old_p1 = prior_1_;
            auto old_p2 = prior_2_;

            // Exact uniform prior: ties all actions, giving full support
            prior_1_.assign(m, 1.0 / static_cast<double>(m));
            prior_2_.assign(n, 1.0 / static_cast<double>(n));

            std::vector<std::size_t> retry_s1, retry_s2;
            Vec retry_w0 = compute_start(game, retry_s1, retry_s2);
            auto retry_sys = build_homotopy(game, retry_s1, retry_s2);

            // The Jacobian at t=0 with uniform prior may be rank-deficient.
            // Start path from a small positive t to avoid the singularity.
            double t_start = 0.01;
            retry_w0.back() = t_start;

            // Newton-correct onto the homotopy curve at t=t_start (pin t)
            for (int nc = 0; nc < 30; ++nc) {
                Vec h = retry_sys.H(retry_w0);
                double res = linalg::norm_inf(h);
                if (res < 1e-12) break;

                Mat J = retry_sys.DH(retry_w0);
                std::size_t neq = retry_sys.n;
                // Pin t: solve the neq x neq system (excluding t column)
                Mat Jsq(neq, neq);
                for (std::size_t i = 0; i < neq; ++i)
                    for (std::size_t j = 0; j < neq; ++j)
                        Jsq(i, j) = J(i, j);
                Vec rhs(neq);
                for (std::size_t i = 0; i < neq; ++i)
                    rhs[i] = -h[i];
                auto dw = linalg::solve(Jsq, rhs, 1e-14);
                if (!dw) break;
                for (std::size_t i = 0; i < neq; ++i)
                    retry_w0[i] += (*dw)[i];
                retry_w0.back() = t_start; // keep t pinned
            }

            auto retry_path = follow_path(retry_sys, retry_w0, cfg_);

            prior_1_ = old_p1;
            prior_2_ = old_p2;

            if (retry_path.reached_target || retry_path.t_final > 1.0 - 1e-4) {
                auto prof = extract_profile(retry_path.w_final, m, n, retry_s1, retry_s2);
                double res = nash_residual(game, prof.strategy_1, prof.strategy_2);
                if (res < result.residual) {
                    result.profile = prof;
                    result.residual = res;
                    result.iterations += retry_path.total_steps;
                    result.payoff_1 = game.expected_payoff_1(prof.strategy_1, prof.strategy_2);
                    result.payoff_2 = game.expected_payoff_2(prof.strategy_1, prof.strategy_2);
                }
            }
        }

        // Final convergence check: if the residual is small, declare convergence
        // regardless of what the path follower reported.
        if (result.status != SolverStatus::Converged && result.residual < tol_.conv_tol) {
            result.status = SolverStatus::Converged;
        }

        return result;
    }

} // namespace nash
