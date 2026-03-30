#include "nash/numerics/linear_algebra.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <numeric>

namespace nash
{
    namespace linalg
    {

        // ── Vector operations ───────────────────────────────────────────────

        double dot(const Vec &a, const Vec &b)
        {
            assert(a.size() == b.size());
            double s = 0.0;
            for (std::size_t i = 0; i < a.size(); ++i)
                s += a[i] * b[i];
            return s;
        }

        double norm2(const Vec &v)
        {
            return std::sqrt(dot(v, v));
        }

        double norm_inf(const Vec &v)
        {
            double mx = 0.0;
            for (auto x : v)
                mx = std::max(mx, std::fabs(x));
            return mx;
        }

        Vec add(const Vec &a, const Vec &b)
        {
            assert(a.size() == b.size());
            Vec r(a.size());
            for (std::size_t i = 0; i < a.size(); ++i)
                r[i] = a[i] + b[i];
            return r;
        }

        Vec sub(const Vec &a, const Vec &b)
        {
            assert(a.size() == b.size());
            Vec r(a.size());
            for (std::size_t i = 0; i < a.size(); ++i)
                r[i] = a[i] - b[i];
            return r;
        }

        Vec scale(double alpha, const Vec &a)
        {
            Vec r(a.size());
            for (std::size_t i = 0; i < a.size(); ++i)
                r[i] = alpha * a[i];
            return r;
        }

        Vec axpy(double alpha, const Vec &b, const Vec &a)
        {
            assert(a.size() == b.size());
            Vec r(a.size());
            for (std::size_t i = 0; i < a.size(); ++i)
                r[i] = a[i] + alpha * b[i];
            return r;
        }

        // ── Matrix-vector ───────────────────────────────────────────────────

        Vec matvec(const Mat &A, const Vec &x)
        {
            assert(A.cols == x.size());
            Vec y(A.rows, 0.0);
            for (std::size_t i = 0; i < A.rows; ++i)
                for (std::size_t j = 0; j < A.cols; ++j)
                    y[i] += A(i, j) * x[j];
            return y;
        }

        Vec matvec_transpose(const Mat &A, const Vec &x)
        {
            assert(A.rows == x.size());
            Vec y(A.cols, 0.0);
            for (std::size_t i = 0; i < A.rows; ++i)
                for (std::size_t j = 0; j < A.cols; ++j)
                    y[j] += A(i, j) * x[i];
            return y;
        }

        // ── Matrix operations ───────────────────────────────────────────────

        Mat matmul(const Mat &A, const Mat &B)
        {
            assert(A.cols == B.rows);
            Mat C(A.rows, B.cols, 0.0);
            for (std::size_t i = 0; i < A.rows; ++i)
                for (std::size_t k = 0; k < A.cols; ++k)
                    for (std::size_t j = 0; j < B.cols; ++j)
                        C(i, j) += A(i, k) * B(k, j);
            return C;
        }

        Mat transpose(const Mat &A)
        {
            Mat T(A.cols, A.rows);
            for (std::size_t i = 0; i < A.rows; ++i)
                for (std::size_t j = 0; j < A.cols; ++j)
                    T(j, i) = A(i, j);
            return T;
        }

        Mat eye(std::size_t n)
        {
            Mat I(n, n, 0.0);
            for (std::size_t i = 0; i < n; ++i)
                I(i, i) = 1.0;
            return I;
        }

        // ── LU decomposition with partial pivoting ──────────────────────────

        LU lu_decompose(const Mat &A, double pivot_tol)
        {
            assert(A.rows == A.cols);
            std::size_t n = A.rows;

            LU result;
            result.LU_combined = A; // copy
            result.perm.resize(n);
            std::iota(result.perm.begin(), result.perm.end(), 0);
            result.sign = 1;
            result.singular = false;

            auto &M = result.LU_combined;

            for (std::size_t k = 0; k < n; ++k)
            {
                // Find pivot
                std::size_t pivot_row = k;
                double pivot_val = std::fabs(M(k, k));
                for (std::size_t i = k + 1; i < n; ++i)
                {
                    double v = std::fabs(M(i, k));
                    if (v > pivot_val)
                    {
                        pivot_val = v;
                        pivot_row = i;
                    }
                }

                if (pivot_val < pivot_tol)
                {
                    result.singular = true;
                    return result;
                }

                // Swap rows
                if (pivot_row != k)
                {
                    std::swap(result.perm[k], result.perm[pivot_row]);
                    result.sign = -result.sign;
                    for (std::size_t j = 0; j < n; ++j)
                        std::swap(M(k, j), M(pivot_row, j));
                }

                // Eliminate below pivot
                for (std::size_t i = k + 1; i < n; ++i)
                {
                    double factor = M(i, k) / M(k, k);
                    M(i, k) = factor; // store L factor
                    for (std::size_t j = k + 1; j < n; ++j)
                        M(i, j) -= factor * M(k, j);
                }
            }
            return result;
        }

        Vec lu_solve(const LU &lu, const Vec &b)
        {
            std::size_t n = lu.LU_combined.rows;
            const auto &M = lu.LU_combined;
            const auto &perm = lu.perm;

            // Apply permutation: Pb
            Vec y(n);
            for (std::size_t i = 0; i < n; ++i)
                y[i] = b[perm[i]];

            // Forward substitution (L * z = Pb), L has unit diagonal
            for (std::size_t i = 1; i < n; ++i)
                for (std::size_t j = 0; j < i; ++j)
                    y[i] -= M(i, j) * y[j];

            // Back substitution (U * x = z)
            Vec x(n);
            for (std::size_t i = n; i-- > 0;)
            {
                double s = y[i];
                for (std::size_t j = i + 1; j < n; ++j)
                    s -= M(i, j) * x[j];
                x[i] = s / M(i, i);
            }
            return x;
        }

        std::optional<Vec> solve(const Mat &A, const Vec &b, double pivot_tol)
        {
            assert(A.rows == A.cols && A.rows == b.size());
            auto lu = lu_decompose(A, pivot_tol);
            if (lu.singular)
                return std::nullopt;
            return lu_solve(lu, b);
        }

        // ── Null space via QR on A^T ───────────────────────────────────────
        // For an m×n matrix A (m < n), ker(A) has dimension >= n - m.
        // We compute QR of A^T (n×m) with column pivoting to find
        // the null space of A.

        std::vector<Vec> null_space(const Mat &A, double tol)
        {
            // We compute the null space of A (m×n) by performing QR on A^T.
            // A^T is n×m. The null space of A is the set of columns of Q
            // corresponding to zero rows in R.

            // Alternative approach: SVD-like via Householder QR on A^T.
            // For small matrices, use a simpler approach: row-reduce A
            // and read off the null space.

            std::size_t m = A.rows;
            std::size_t n = A.cols;

            // Augmented matrix for row reduction
            Mat R = A; // copy

            // Column pivoting indices
            std::vector<std::size_t> col_perm(n);
            std::iota(col_perm.begin(), col_perm.end(), 0);

            std::size_t rank = 0;
            for (std::size_t k = 0; k < std::min(m, n); ++k)
            {
                // Find largest pivot in submatrix R[k:, k:]
                double best = 0.0;
                std::size_t best_r = k, best_c = k;
                for (std::size_t i = k; i < m; ++i)
                {
                    for (std::size_t j = k; j < n; ++j)
                    {
                        double v = std::fabs(R(i, j));
                        if (v > best)
                        {
                            best = v;
                            best_r = i;
                            best_c = j;
                        }
                    }
                }

                if (best < tol)
                    break; // remaining entries are zero

                // Swap rows k and best_r
                if (best_r != k)
                    for (std::size_t j = 0; j < n; ++j)
                        std::swap(R(k, j), R(best_r, j));

                // Swap columns k and best_c
                if (best_c != k)
                {
                    std::swap(col_perm[k], col_perm[best_c]);
                    for (std::size_t i = 0; i < m; ++i)
                        std::swap(R(i, k), R(i, best_c));
                }

                // Eliminate below
                for (std::size_t i = k + 1; i < m; ++i)
                {
                    double factor = R(i, k) / R(k, k);
                    R(i, k) = 0.0;
                    for (std::size_t j = k + 1; j < n; ++j)
                        R(i, j) -= factor * R(k, j);
                }

                // Eliminate above (full RREF)
                for (std::size_t i = 0; i < k; ++i)
                {
                    double factor = R(i, k) / R(k, k);
                    R(i, k) = 0.0;
                    for (std::size_t j = k + 1; j < n; ++j)
                        R(i, j) -= factor * R(k, j);
                }

                // Normalize pivot row
                double d = R(k, k);
                for (std::size_t j = k; j < n; ++j)
                    R(k, j) /= d;

                ++rank;
            }

            // Now R is in RREF with columns permuted by col_perm.
            // Free variables are columns rank, rank+1, ..., n-1 (in permuted order).
            // For each free variable j (>= rank), build a null space vector.

            std::size_t null_dim = n - rank;
            std::vector<Vec> basis(null_dim);

            for (std::size_t f = 0; f < null_dim; ++f)
            {
                Vec v(n, 0.0);
                std::size_t free_col = rank + f; // in permuted coordinates
                v[col_perm[free_col]] = 1.0;

                for (std::size_t k = 0; k < rank; ++k)
                {
                    v[col_perm[k]] = -R(k, free_col);
                }

                // Normalize
                double nrm = norm2(v);
                if (nrm > tol)
                    for (auto &x : v)
                        x /= nrm;

                basis[f] = std::move(v);
            }

            return basis;
        }

        double determinant(const Mat &A)
        {
            auto lu = lu_decompose(A);
            if (lu.singular)
                return 0.0;
            double det = static_cast<double>(lu.sign);
            for (std::size_t i = 0; i < A.rows; ++i)
                det *= lu.LU_combined(i, i);
            return det;
        }

        Mat submatrix(const Mat &A,
                      const std::vector<std::size_t> &row_idx,
                      const std::vector<std::size_t> &col_idx)
        {
            Mat S(row_idx.size(), col_idx.size());
            for (std::size_t i = 0; i < row_idx.size(); ++i)
                for (std::size_t j = 0; j < col_idx.size(); ++j)
                    S(i, j) = A(row_idx[i], col_idx[j]);
            return S;
        }

        Vec column(const Mat &A, std::size_t j)
        {
            Vec c(A.rows);
            for (std::size_t i = 0; i < A.rows; ++i)
                c[i] = A(i, j);
            return c;
        }

        Vec row(const Mat &A, std::size_t i)
        {
            Vec r(A.cols);
            for (std::size_t j = 0; j < A.cols; ++j)
                r[j] = A(i, j);
            return r;
        }

    }
} // namespace nash::linalg
