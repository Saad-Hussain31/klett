#include "nash/numerics/newton_solver.hpp"
#include <cmath>
#include <cassert>

namespace nash
{

    Mat numerical_jacobian(const std::function<Vec(const Vec &)> &F,
                           const Vec &x, double h)
    {
        std::size_t n = x.size();
        Vec Fx = F(x);
        std::size_t m = Fx.size();

        Mat J(m, n);
        Vec xp = x;
        Vec xm = x;

        for (std::size_t j = 0; j < n; ++j)
        {
            double hj = std::max(h, h * std::fabs(x[j]));
            xp[j] = x[j] + hj;
            xm[j] = x[j] - hj;

            Vec Fp = F(xp);
            Vec Fm = F(xm);

            for (std::size_t i = 0; i < m; ++i)
                J(i, j) = (Fp[i] - Fm[i]) / (2.0 * hj);

            xp[j] = x[j];
            xm[j] = x[j];
        }
        return J;
    }

    NewtonResult newton_solve(const NonlinearSystem &sys,
                              const Vec &x0,
                              double tol,
                              int max_iter)
    {
        assert(x0.size() == sys.dim);

        NewtonResult result;
        result.x = x0;
        result.converged = false;

        for (int iter = 0; iter < max_iter; ++iter)
        {
            Vec Fx = sys.F(result.x);
            result.residual = linalg::norm_inf(Fx);
            result.iterations = iter + 1;

            if (result.residual < tol)
            {
                result.converged = true;
                return result;
            }

            // Compute Jacobian
            Mat J = sys.J ? sys.J(result.x) : numerical_jacobian(sys.F, result.x);

            // Solve J * dx = -F(x)
            Vec neg_Fx(Fx.size());
            for (std::size_t i = 0; i < Fx.size(); ++i)
                neg_Fx[i] = -Fx[i];

            auto dx = linalg::solve(J, neg_Fx, 1e-14);
            if (!dx)
            {
                // Singular Jacobian: try damped step with pseudo-inverse-like approach
                // Add small regularization: (J^T J + lambda I) dx = -J^T F
                Mat Jt = linalg::transpose(J);
                Mat JtJ = linalg::matmul(Jt, J);
                double lambda = 1e-6 * (linalg::norm_inf(Fx) + 1.0);
                for (std::size_t i = 0; i < JtJ.rows; ++i)
                    JtJ(i, i) += lambda;
                Vec Jt_neg_F = linalg::matvec(Jt, neg_Fx);
                dx = linalg::solve(JtJ, Jt_neg_F);
                if (!dx)
                    return result; // Total failure
            }

            // Backtracking line search
            double alpha = 1.0;
            double Fx_norm = result.residual;
            for (int ls = 0; ls < 20; ++ls)
            {
                Vec x_new = linalg::axpy(alpha, *dx, result.x);
                Vec Fx_new = sys.F(x_new);
                double new_norm = linalg::norm_inf(Fx_new);
                if (new_norm < Fx_norm * (1.0 - 1e-4 * alpha) || new_norm < tol)
                {
                    result.x = std::move(x_new);
                    break;
                }
                alpha *= 0.5;
                if (ls == 19)
                {
                    // Accept the step anyway if progress is made
                    result.x = linalg::axpy(alpha, *dx, result.x);
                }
            }
        }

        // Final residual
        result.residual = linalg::norm_inf(sys.F(result.x));
        result.converged = result.residual < tol;
        return result;
    }

} // namespace nash
