#pragma once
/// @file game.hpp
/// Bimatrix game representation for 2-player finite normal-form games.

#include "nash/core/types.hpp"
#include <string>
#include <stdexcept>

namespace nash
{

    /// A 2-player normal-form (bimatrix) game.
    /// Player 1 has m actions, Player 2 has n actions.
    /// Payoffs are stored as two m×n matrices A (player 1) and B (player 2).
    class BimatrixGame
    {
    public:
        BimatrixGame() = default;

        /// Construct from payoff matrices.
        /// @param A  Player 1 payoff matrix (m × n)
        /// @param B  Player 2 payoff matrix (m × n)
        BimatrixGame(Mat A, Mat B);

        /// Construct a zero-sum game from a single matrix (B = -A).
        static BimatrixGame zero_sum(Mat A);

        /// Construct a symmetric game (B = A^T).
        static BimatrixGame symmetric(Mat A);

        /// Number of actions for player 1.
        std::size_t num_actions_1() const { return payoff_1_.rows; }

        /// Number of actions for player 2.
        std::size_t num_actions_2() const { return payoff_1_.cols; }

        /// Player 1 payoff matrix.
        const Mat &payoff_1() const { return payoff_1_; }
        Mat &payoff_1() { return payoff_1_; }

        /// Player 2 payoff matrix.
        const Mat &payoff_2() const { return payoff_2_; }
        Mat &payoff_2() { return payoff_2_; }

        /// Payoff for player 1 at action profile (i, j).
        double u1(std::size_t i, std::size_t j) const { return payoff_1_(i, j); }

        /// Payoff for player 2 at action profile (i, j).
        double u2(std::size_t i, std::size_t j) const { return payoff_2_(i, j); }

        /// Expected payoff for player 1 given mixed strategies.
        double expected_payoff_1(const Vec &x, const Vec &y) const;

        /// Expected payoff for player 2 given mixed strategies.
        double expected_payoff_2(const Vec &x, const Vec &y) const;

        /// Shift all payoffs so they are strictly positive.
        /// Some algorithms (e.g. Lemke-Howson) require positive payoffs.
        /// Returns the shift applied.
        double make_positive();

        /// Name/description (optional).
        std::string name;

    private:
        Mat payoff_1_;
        Mat payoff_2_;
    };

} // namespace nash
