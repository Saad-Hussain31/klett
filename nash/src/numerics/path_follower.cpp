#include "nash/numerics/path_follower.hpp"
#include <cmath>
#include <cassert>

namespace nash
{

    PathResult follow_path(const HomotopySystem &sys,
                           const Vec &w0,
                           const PathFollowerConfig &cfg)
    {
        assert(w0.size() == sys.n + 1);

        PathResult result;
        Vec w = w0;
        double step = cfg.initial_step;
        int direction_sign = 1; // track orientation along curve

        // Determine initial tangent direction (should increase t)
        auto choose_direction = [&](const Vec &tangent) -> int
        {
            // We want t to increase, so check sign of last component
            double dt = tangent.back();
            return (dt >= 0.0) ? 1 : -1;
        };

        result.total_steps = 0;
        result.total_corrections = 0;

        for (int s = 0; s < cfg.max_steps; ++s)
        {
            double t_current = w.back();

            // Check if we've reached the target
            if (t_current >= cfg.target_t - 1e-12)
            {
                result.w_final = w;
                result.t_final = t_current;
                result.reached_target = true;
                return result;
            }

            result.total_steps = s + 1;

            // ── Predictor step ──────────────────────────────────────────
            // Compute Jacobian DH(w): n × (n+1) matrix
            Mat J = sys.DH(w);

            // Find tangent: the 1D null space of J
            auto ns = linalg::null_space(J, 1e-12);
            if (ns.empty())
            {
                result.failure_reason = "Jacobian has full rank (no tangent)";
                result.w_final = w;
                result.t_final = w.back();
                return result;
            }

            Vec tangent = ns[0];

            // If null space has dimension > 1, choose the direction that
            // maximizes movement in t (the last component).
            if (ns.size() > 1) {
                double best_t = std::fabs(tangent.back());
                for (std::size_t k = 1; k < ns.size(); ++k) {
                    double t_comp = std::fabs(ns[k].back());
                    if (t_comp > best_t) {
                        best_t = t_comp;
                        tangent = ns[k];
                    }
                }
                // If still no tangent with t-component, try linear combinations
                if (best_t < 1e-14 && ns.size() >= 2) {
                    // Add all basis vectors weighted by their norms to break symmetry
                    tangent = ns[0];
                    for (std::size_t k = 1; k < ns.size(); ++k)
                        for (std::size_t i = 0; i < tangent.size(); ++i)
                            tangent[i] += ns[k][i] * (1.0 + 0.1 * static_cast<double>(k));
                }
            }

            // Orient tangent: on first step, choose direction to increase t.
            // On subsequent steps, maintain consistent orientation.
            if (s == 0)
            {
                direction_sign = choose_direction(tangent);
            }
            else
            {
                // Maintain orientation: dot with previous tangent should be positive.
                // We use the sign convention from the first step.
                if (tangent.back() * direction_sign < 0)
                {
                    for (auto &v : tangent)
                        v = -v;
                }
            }

            // Scale tangent to have unit norm
            double tn = linalg::norm2(tangent);
            if (tn < 1e-15)
            {
                result.failure_reason = "Zero tangent vector";
                result.w_final = w;
                result.t_final = w.back();
                return result;
            }
            for (auto &v : tangent)
                v /= tn;

            // Limit step so we don't overshoot t = target
            double effective_step = step;
            double t_pred = w.back() + effective_step * tangent.back();
            if (t_pred > cfg.target_t + 0.01)
            {
                // Reduce step to land near target
                if (std::fabs(tangent.back()) > 1e-15)
                {
                    effective_step = (cfg.target_t - w.back()) / tangent.back();
                    effective_step = std::max(effective_step, cfg.min_step);
                }
            }

            // Euler predictor: w_pred = w + step * tangent
            Vec w_pred(w.size());
            for (std::size_t i = 0; i < w.size(); ++i)
                w_pred[i] = w[i] + effective_step * tangent[i];

            // ── Corrector step (Newton on augmented system) ─────────────
            // We fix arc-length parameterization: add the constraint
            //   tangent^T * (w - w_pred) = 0
            // to make the system square (n+1 equations in n+1 unknowns).
            bool corrector_ok = false;
            Vec w_corr = w_pred;

            for (int c = 0; c < cfg.corrector_iter; ++c)
            {
                result.total_corrections++;

                Vec Hw = sys.H(w_corr);
                double res = linalg::norm_inf(Hw);

                // Also check the hyperplane constraint
                double plane = 0.0;
                for (std::size_t i = 0; i < w.size(); ++i)
                    plane += tangent[i] * (w_corr[i] - w_pred[i]);

                if (res < cfg.corrector_tol && std::fabs(plane) < cfg.corrector_tol)
                {
                    corrector_ok = true;
                    break;
                }
                // Accept if H residual is very small even if plane constraint isn't perfect
                if (res < cfg.corrector_tol * 0.01)
                {
                    corrector_ok = true;
                    break;
                }

                // Build augmented Jacobian: (n+1) × (n+1)
                Mat Jc = sys.DH(w_corr); // n × (n+1)
                Mat Jaug(sys.n + 1, sys.n + 1);
                for (std::size_t i = 0; i < sys.n; ++i)
                    for (std::size_t j = 0; j < sys.n + 1; ++j)
                        Jaug(i, j) = Jc(i, j);
                // Last row: tangent^T
                for (std::size_t j = 0; j < sys.n + 1; ++j)
                    Jaug(sys.n, j) = tangent[j];

                // RHS: [-H(w); -plane]
                Vec rhs(sys.n + 1);
                for (std::size_t i = 0; i < sys.n; ++i)
                    rhs[i] = -Hw[i];
                rhs[sys.n] = -plane;

                auto dw = linalg::solve(Jaug, rhs, 1e-14);
                if (!dw)
                    break; // singular

                for (std::size_t i = 0; i < w_corr.size(); ++i)
                    w_corr[i] += (*dw)[i];
            }

            if (corrector_ok)
            {
                w = w_corr;
                // Increase step size on success
                step = std::min(step * cfg.step_increase, cfg.max_step);
            }
            else
            {
                // Decrease step and retry
                step *= cfg.step_decrease;
                if (step < cfg.min_step)
                {
                    result.failure_reason = "Step size below minimum";
                    result.w_final = w;
                    result.t_final = w.back();
                    return result;
                }
                --s; // retry this step
            }
        }

        result.failure_reason = "Maximum steps exceeded";
        result.w_final = w;
        result.t_final = w.back();
        return result;
    }

} // namespace nash
