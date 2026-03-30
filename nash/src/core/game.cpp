#include "nash/core/game.hpp"
#include "nash/numerics/linear_algebra.hpp"
#include <algorithm>
#include <cmath>

namespace nash
{

    BimatrixGame::BimatrixGame(Mat A, Mat B)
        : payoff_1_(std::move(A)), payoff_2_(std::move(B))
    {
        if (payoff_1_.rows != payoff_2_.rows || payoff_1_.cols != payoff_2_.cols)
            throw std::invalid_argument("Payoff matrices must have the same dimensions");
        if (payoff_1_.rows == 0 || payoff_1_.cols == 0)
            throw std::invalid_argument("Game must have at least one action per player");
    }

    BimatrixGame BimatrixGame::zero_sum(Mat A)
    {
        Mat B(A.rows, A.cols);
        for (std::size_t i = 0; i < A.rows * A.cols; ++i)
            B.data[i] = -A.data[i];
        return BimatrixGame(std::move(A), std::move(B));
    }

    BimatrixGame BimatrixGame::symmetric(Mat A)
    {
        if (A.rows != A.cols)
            throw std::invalid_argument("Symmetric game requires a square matrix");
        Mat B = linalg::transpose(A);
        return BimatrixGame(std::move(A), std::move(B));
    }

    double BimatrixGame::expected_payoff_1(const Vec &x, const Vec &y) const
    {
        // x^T A y
        double val = 0.0;
        for (std::size_t i = 0; i < payoff_1_.rows; ++i)
            for (std::size_t j = 0; j < payoff_1_.cols; ++j)
                val += x[i] * payoff_1_(i, j) * y[j];
        return val;
    }

    double BimatrixGame::expected_payoff_2(const Vec &x, const Vec &y) const
    {
        // x^T B y
        double val = 0.0;
        for (std::size_t i = 0; i < payoff_2_.rows; ++i)
            for (std::size_t j = 0; j < payoff_2_.cols; ++j)
                val += x[i] * payoff_2_(i, j) * y[j];
        return val;
    }

    double BimatrixGame::make_positive()
    {
        double min_val = *std::min_element(payoff_1_.data.begin(), payoff_1_.data.end());
        min_val = std::min(min_val, *std::min_element(payoff_2_.data.begin(), payoff_2_.data.end()));

        double shift = 0.0;
        if (min_val <= 0.0)
        {
            shift = -min_val + 1.0;
            for (auto &v : payoff_1_.data)
                v += shift;
            for (auto &v : payoff_2_.data)
                v += shift;
        }
        return shift;
    }

} // namespace nash
