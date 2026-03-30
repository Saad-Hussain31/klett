#pragma once
/// @file linear_algebra.hpp
/// Self-contained dense linear algebra for small-to-medium matrices.

#include "nash/core/types.hpp"
#include <optional>
#include <stdexcept>
#include <string>

namespace nash
{
    namespace linalg
    {

        // ── Vector operations ───────────────────────────────────────────────

        /// Dot product.
        double dot(const Vec &a, const Vec &b);

        /// Euclidean norm.
        double norm2(const Vec &v);

        /// Infinity norm.
        double norm_inf(const Vec &v);

        /// v = a + b
        Vec add(const Vec &a, const Vec &b);

        /// v = a - b
        Vec sub(const Vec &a, const Vec &b);

        /// v = alpha * a
        Vec scale(double alpha, const Vec &a);

        /// v = a + alpha * b
        Vec axpy(double alpha, const Vec &b, const Vec &a);

        // ── Matrix-vector ───────────────────────────────────────────────────

        /// y = A * x
        Vec matvec(const Mat &A, const Vec &x);

        /// y = A^T * x
        Vec matvec_transpose(const Mat &A, const Vec &x);

        // ── Matrix operations ───────────────────────────────────────────────

        /// C = A * B
        Mat matmul(const Mat &A, const Mat &B);

        /// Transpose.
        Mat transpose(const Mat &A);

        /// Identity matrix.
        Mat eye(std::size_t n);

        // ── Linear solvers ──────────────────────────────────────────────────

        /// Result of LU decomposition with partial pivoting.
        struct LU
        {
            Mat LU_combined;               ///< Combined L\U factor (L has unit diagonal)
            std::vector<std::size_t> perm; ///< Permutation vector
            int sign = 1;                  ///< Sign of permutation (for determinant)
            bool singular = false;         ///< Whether a zero pivot was encountered
        };

        /// Compute PA = LU decomposition with partial pivoting.
        LU lu_decompose(const Mat &A, double pivot_tol = 1e-12);

        /// Solve Ax = b using pre-computed LU.
        Vec lu_solve(const LU &lu, const Vec &b);

        /// Solve Ax = b directly (computes LU internally).
        /// Returns nullopt if the matrix is singular.
        std::optional<Vec> solve(const Mat &A, const Vec &b, double pivot_tol = 1e-12);

        /// Compute the null space basis of an (m x n) matrix with m < n.
        /// Returns a vector of basis vectors spanning ker(A).
        /// Uses QR with column pivoting on A^T.
        std::vector<Vec> null_space(const Mat &A, double tol = 1e-10);

        /// Determinant via LU.
        double determinant(const Mat &A);

        /// Extract submatrix A[row_idx, col_idx].
        Mat submatrix(const Mat &A,
                      const std::vector<std::size_t> &row_idx,
                      const std::vector<std::size_t> &col_idx);

        /// Extract a column from a matrix.
        Vec column(const Mat &A, std::size_t j);

        /// Extract a row from a matrix.
        Vec row(const Mat &A, std::size_t i);

    }
} // namespace nash::linalg
