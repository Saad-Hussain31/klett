/// @file test_support_enumeration.cpp

#include "nash/algorithms/support_enumeration.hpp"
#include <iostream>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

struct TestCase
{
    std::string name;
    std::function<bool()> func;
};
extern std::vector<TestCase> &test_registry();
#define ASSERT_TRUE(cond)                                                                           \
    do                                                                                              \
    {                                                                                               \
        if (!(cond))                                                                                \
        {                                                                                           \
            std::cerr << "  FAIL: " << #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false;                                                                           \
        }                                                                                           \
    } while (0)
#define ASSERT_NEAR(a, b, tol)                                                                                                                   \
    do                                                                                                                                           \
    {                                                                                                                                            \
        if (std::fabs((a) - (b)) > (tol))                                                                                                        \
        {                                                                                                                                        \
            std::cerr << "  FAIL: " << #a << " ≈ " << #b << " (" << (a) << " vs " << (b) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false;                                                                                                                        \
        }                                                                                                                                        \
    } while (0)

using namespace nash;

// ── Prisoner's Dilemma ──────────────────────────────────────────────
// (C,C)=(3,3) (C,D)=(0,5) (D,C)=(5,0) (D,D)=(1,1)
// Unique NE: (D, D) = pure strategy
static bool test_prisoners_dilemma()
{
    Mat A(2, 2);
    Mat B(2, 2);
    A(0, 0) = 3;
    A(0, 1) = 0;
    A(1, 0) = 5;
    A(1, 1) = 1;
    B(0, 0) = 3;
    B(0, 1) = 5;
    B(1, 0) = 0;
    B(1, 1) = 1;
    BimatrixGame game(A, B);

    SupportEnumeration solver;
    auto result = solver.solve_all(game);

    ASSERT_TRUE(result.status == SolverStatus::Converged);
    ASSERT_TRUE(result.equilibria.size() == 1);
    ASSERT_NEAR(result.equilibria[0].profile.strategy_1[1], 1.0, 1e-6); // D
    ASSERT_NEAR(result.equilibria[0].profile.strategy_2[1], 1.0, 1e-6); // D
    return true;
}

// ── Matching Pennies ────────────────────────────────────────────────
// Unique NE: (0.5, 0.5), (0.5, 0.5)
static bool test_matching_pennies()
{
    Mat A(2, 2);
    A(0, 0) = 1;
    A(0, 1) = -1;
    A(1, 0) = -1;
    A(1, 1) = 1;
    auto game = BimatrixGame::zero_sum(A);

    SupportEnumeration solver;
    auto result = solver.solve_all(game);

    ASSERT_TRUE(result.status == SolverStatus::Converged);
    ASSERT_TRUE(result.equilibria.size() >= 1);
    auto &eq = result.equilibria[0];
    ASSERT_NEAR(eq.profile.strategy_1[0], 0.5, 1e-6);
    ASSERT_NEAR(eq.profile.strategy_1[1], 0.5, 1e-6);
    ASSERT_NEAR(eq.profile.strategy_2[0], 0.5, 1e-6);
    ASSERT_NEAR(eq.profile.strategy_2[1], 0.5, 1e-6);
    return true;
}

// ── Battle of the Sexes ─────────────────────────────────────────────
// (O,O)=(3,2) (O,F)=(0,0) (F,O)=(0,0) (F,F)=(2,3)
// Three NE: (O,O), (F,F), and a mixed NE (3/5, 2/5), (2/5, 3/5)
static bool test_battle_of_sexes()
{
    Mat A(2, 2);
    Mat B(2, 2);
    A(0, 0) = 3;
    A(0, 1) = 0;
    A(1, 0) = 0;
    A(1, 1) = 2;
    B(0, 0) = 2;
    B(0, 1) = 0;
    B(1, 0) = 0;
    B(1, 1) = 3;
    BimatrixGame game(A, B);

    SupportEnumeration solver;
    auto result = solver.solve_all(game);

    ASSERT_TRUE(result.status == SolverStatus::Converged);
    ASSERT_TRUE(result.equilibria.size() == 3);

    // Verify all are valid NE
    for (const auto &eq : result.equilibria)
    {
        ASSERT_TRUE(verify_equilibrium(game, eq.profile, 1e-6));
    }
    return true;
}

// ── 3x3 game ────────────────────────────────────────────────────────
static bool test_3x3_game()
{
    // Rock-Paper-Scissors: unique NE is (1/3, 1/3, 1/3) for both
    Mat A(3, 3);
    A(0, 0) = 0;
    A(0, 1) = -1;
    A(0, 2) = 1;
    A(1, 0) = 1;
    A(1, 1) = 0;
    A(1, 2) = -1;
    A(2, 0) = -1;
    A(2, 1) = 1;
    A(2, 2) = 0;
    auto game = BimatrixGame::zero_sum(A);

    SupportEnumeration solver;
    auto result = solver.solve_all(game);

    ASSERT_TRUE(result.status == SolverStatus::Converged);
    ASSERT_TRUE(result.equilibria.size() >= 1);

    auto &eq = result.equilibria[0];
    ASSERT_NEAR(eq.profile.strategy_1[0], 1.0 / 3.0, 1e-6);
    ASSERT_NEAR(eq.profile.strategy_1[1], 1.0 / 3.0, 1e-6);
    ASSERT_NEAR(eq.profile.strategy_1[2], 1.0 / 3.0, 1e-6);
    return true;
}

// ── Pure coordination game ──────────────────────────────────────────
static bool test_coordination_game()
{
    // Both want to match: (A,A)=(2,2) (A,B)=(0,0) (B,A)=(0,0) (B,B)=(1,1)
    Mat A(2, 2);
    Mat B(2, 2);
    A(0, 0) = 2;
    A(0, 1) = 0;
    A(1, 0) = 0;
    A(1, 1) = 1;
    B(0, 0) = 2;
    B(0, 1) = 0;
    B(1, 0) = 0;
    B(1, 1) = 1;
    BimatrixGame game(A, B);

    SupportEnumeration solver;
    auto result = solver.solve_all(game);

    ASSERT_TRUE(result.status == SolverStatus::Converged);
    ASSERT_TRUE(result.equilibria.size() == 3); // 2 pure + 1 mixed

    for (const auto &eq : result.equilibria)
        ASSERT_TRUE(verify_equilibrium(game, eq.profile, 1e-6));

    return true;
}

void register_support_enumeration_tests()
{
    test_registry().push_back({"support_enum::prisoners_dilemma", test_prisoners_dilemma});
    test_registry().push_back({"support_enum::matching_pennies", test_matching_pennies});
    test_registry().push_back({"support_enum::battle_of_sexes", test_battle_of_sexes});
    test_registry().push_back({"support_enum::3x3_rps", test_3x3_game});
    test_registry().push_back({"support_enum::coordination", test_coordination_game});
}
