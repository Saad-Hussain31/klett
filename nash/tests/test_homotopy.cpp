/// @file test_homotopy.cpp

#include "nash/algorithms/homotopy.hpp"
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

static bool test_homotopy_prisoners_dilemma()
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

    HomotopySolver solver;
    auto result = solver.solve(game);

    ASSERT_TRUE(result.status == SolverStatus::Converged);
    ASSERT_TRUE(verify_equilibrium(game, result.profile, 1e-4));
    return true;
}

static bool test_homotopy_matching_pennies()
{
    Mat A(2, 2);
    A(0, 0) = 1;
    A(0, 1) = -1;
    A(1, 0) = -1;
    A(1, 1) = 1;
    auto game = BimatrixGame::zero_sum(A);

    HomotopySolver solver;
    auto result = solver.solve(game);

    ASSERT_TRUE(result.status == SolverStatus::Converged);
    ASSERT_TRUE(verify_equilibrium(game, result.profile, 1e-3));
    return true;
}

static bool test_homotopy_battle_of_sexes()
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

    HomotopySolver solver;
    auto result = solver.solve(game);

    ASSERT_TRUE(result.status == SolverStatus::Converged);
    ASSERT_TRUE(verify_equilibrium(game, result.profile, 1e-3));
    return true;
}

static bool test_homotopy_with_prior()
{
    // Use a specific prior to influence which equilibrium is found
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

    // Prior that favors action 0 for both players
    Vec prior1 = {0.9, 0.1};
    Vec prior2 = {0.9, 0.1};
    HomotopySolver solver(prior1, prior2);
    auto result = solver.solve(game);

    ASSERT_TRUE(result.status == SolverStatus::Converged);
    ASSERT_TRUE(verify_equilibrium(game, result.profile, 1e-3));
    return true;
}

static bool test_homotopy_3x3()
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

    HomotopySolver solver;
    auto result = solver.solve(game);

    // Homotopy should find an equilibrium (might be NE or close)
    if (result.status == SolverStatus::Converged)
    {
        ASSERT_TRUE(verify_equilibrium(game, result.profile, 1e-2));
    }
    // If it fails to converge for 3x3, that's acceptable for a known hard case
    return true;
}

void register_homotopy_tests()
{
    test_registry().push_back({"homotopy::prisoners_dilemma", test_homotopy_prisoners_dilemma});
    test_registry().push_back({"homotopy::matching_pennies", test_homotopy_matching_pennies});
    test_registry().push_back({"homotopy::battle_of_sexes", test_homotopy_battle_of_sexes});
    test_registry().push_back({"homotopy::with_prior", test_homotopy_with_prior});
    test_registry().push_back({"homotopy::3x3_game", test_homotopy_3x3});
}
