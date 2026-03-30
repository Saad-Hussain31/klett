/// @file test_lemke_howson.cpp

#include "nash/algorithms/lemke_howson.hpp"
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

static bool test_lh_prisoners_dilemma()
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

    LemkeHowson solver(0);
    auto result = solver.solve(game);

    ASSERT_TRUE(result.status == SolverStatus::Converged);
    ASSERT_TRUE(verify_equilibrium(game, result.profile, 1e-4));
    return true;
}

static bool test_lh_matching_pennies()
{
    Mat A(2, 2);
    A(0, 0) = 1;
    A(0, 1) = -1;
    A(1, 0) = -1;
    A(1, 1) = 1;
    auto game = BimatrixGame::zero_sum(A);

    LemkeHowson solver(0);
    auto result = solver.solve(game);

    ASSERT_TRUE(result.status == SolverStatus::Converged);
    ASSERT_TRUE(verify_equilibrium(game, result.profile, 1e-4));
    ASSERT_NEAR(result.profile.strategy_1[0], 0.5, 1e-4);
    ASSERT_NEAR(result.profile.strategy_2[0], 0.5, 1e-4);
    return true;
}

static bool test_lh_battle_of_sexes()
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

    LemkeHowson solver(0);
    auto result = solver.solve(game);

    ASSERT_TRUE(result.status == SolverStatus::Converged);
    ASSERT_TRUE(verify_equilibrium(game, result.profile, 1e-4));
    return true;
}

static bool test_lh_multiple_labels()
{
    // Try multiple label drops on Battle of Sexes to find different equilibria
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

    LemkeHowson solver;
    auto result = solver.solve_multiple(game);

    ASSERT_TRUE(result.status == SolverStatus::Converged);
    ASSERT_TRUE(result.equilibria.size() >= 1);

    // All found equilibria should be valid
    for (const auto &eq : result.equilibria)
        ASSERT_TRUE(verify_equilibrium(game, eq.profile, 1e-4));

    return true;
}

static bool test_lh_3x3_rps()
{
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

    LemkeHowson solver(0);
    auto result = solver.solve(game);

    ASSERT_TRUE(result.status == SolverStatus::Converged);
    ASSERT_TRUE(verify_equilibrium(game, result.profile, 1e-4));
    return true;
}

void register_lemke_howson_tests()
{
    test_registry().push_back({"lemke_howson::prisoners_dilemma", test_lh_prisoners_dilemma});
    test_registry().push_back({"lemke_howson::matching_pennies", test_lh_matching_pennies});
    test_registry().push_back({"lemke_howson::battle_of_sexes", test_lh_battle_of_sexes});
    test_registry().push_back({"lemke_howson::multiple_labels", test_lh_multiple_labels});
    test_registry().push_back({"lemke_howson::3x3_rps", test_lh_3x3_rps});
}
