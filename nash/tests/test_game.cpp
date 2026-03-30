/// @file test_game.cpp

#include "nash/core/game.hpp"
#include "nash/core/strategy.hpp"
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

static bool test_game_construction()
{
    Mat A(2, 2);
    A(0, 0) = 3;
    A(0, 1) = 0;
    A(1, 0) = 5;
    A(1, 1) = 1;
    Mat B(2, 2);
    B(0, 0) = 3;
    B(0, 1) = 5;
    B(1, 0) = 0;
    B(1, 1) = 1;
    BimatrixGame game(A, B);
    ASSERT_TRUE(game.num_actions_1() == 2);
    ASSERT_TRUE(game.num_actions_2() == 2);
    ASSERT_NEAR(game.u1(0, 0), 3.0, 1e-12);
    ASSERT_NEAR(game.u2(0, 1), 5.0, 1e-12);
    return true;
}

static bool test_expected_payoff()
{
    // Matching Pennies
    Mat A(2, 2);
    A(0, 0) = 1;
    A(0, 1) = -1;
    A(1, 0) = -1;
    A(1, 1) = 1;
    auto game = BimatrixGame::zero_sum(A);
    Vec x = {0.5, 0.5};
    Vec y = {0.5, 0.5};
    ASSERT_NEAR(game.expected_payoff_1(x, y), 0.0, 1e-12);
    ASSERT_NEAR(game.expected_payoff_2(x, y), 0.0, 1e-12);
    return true;
}

static bool test_make_positive()
{
    Mat A(2, 2);
    A(0, 0) = -1;
    A(0, 1) = 2;
    A(1, 0) = 3;
    A(1, 1) = -4;
    Mat B = A;
    BimatrixGame game(A, B);
    double shift = game.make_positive();
    ASSERT_TRUE(shift > 0);
    // All payoffs should now be > 0
    for (std::size_t i = 0; i < 2; ++i)
        for (std::size_t j = 0; j < 2; ++j)
            ASSERT_TRUE(game.u1(i, j) > 0);
    return true;
}

static bool test_zero_sum()
{
    Mat A(2, 2);
    A(0, 0) = 3;
    A(0, 1) = -1;
    A(1, 0) = -2;
    A(1, 1) = 4;
    auto game = BimatrixGame::zero_sum(A);
    ASSERT_NEAR(game.u2(0, 0), -3.0, 1e-12);
    ASSERT_NEAR(game.u2(1, 1), -4.0, 1e-12);
    return true;
}

static bool test_strategy_profile()
{
    StrategyProfile profile;
    profile.strategy_1 = {0.3, 0.7};
    profile.strategy_2 = {0.5, 0.5};
    ASSERT_TRUE(profile.is_valid());
    auto s1 = profile.support_1();
    ASSERT_TRUE(s1.size() == 2);
    return true;
}

void register_game_tests()
{
    test_registry().push_back({"game::construction", test_game_construction});
    test_registry().push_back({"game::expected_payoff", test_expected_payoff});
    test_registry().push_back({"game::make_positive", test_make_positive});
    test_registry().push_back({"game::zero_sum", test_zero_sum});
    test_registry().push_back({"game::strategy_profile", test_strategy_profile});
}
