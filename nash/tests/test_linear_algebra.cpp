/// @file test_linear_algebra.cpp

#include "nash/numerics/linear_algebra.hpp"
#include <iostream>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

// Reuse test framework from test_main.cpp
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
using namespace nash::linalg;

static bool test_dot_product()
{
    Vec a = {1.0, 2.0, 3.0};
    Vec b = {4.0, 5.0, 6.0};
    ASSERT_NEAR(dot(a, b), 32.0, 1e-12);
    return true;
}

static bool test_matvec()
{
    Mat A(2, 3);
    A(0, 0) = 1;
    A(0, 1) = 2;
    A(0, 2) = 3;
    A(1, 0) = 4;
    A(1, 1) = 5;
    A(1, 2) = 6;
    Vec x = {1.0, 1.0, 1.0};
    Vec y = matvec(A, x);
    ASSERT_NEAR(y[0], 6.0, 1e-12);
    ASSERT_NEAR(y[1], 15.0, 1e-12);
    return true;
}

static bool test_solve_2x2()
{
    // Solve: [2, 1; 1, 3] x = [5, 10]
    // Solution: x = [5/5, 15/5] = [1, 3]
    Mat A(2, 2);
    A(0, 0) = 2;
    A(0, 1) = 1;
    A(1, 0) = 1;
    A(1, 1) = 3;
    Vec b = {5.0, 10.0};
    auto x = solve(A, b);
    ASSERT_TRUE(x.has_value());
    ASSERT_NEAR((*x)[0], 1.0, 1e-10);
    ASSERT_NEAR((*x)[1], 3.0, 1e-10);
    return true;
}

static bool test_solve_singular()
{
    Mat A(2, 2);
    A(0, 0) = 1;
    A(0, 1) = 2;
    A(1, 0) = 2;
    A(1, 1) = 4;
    Vec b = {3.0, 6.0};
    auto x = solve(A, b);
    ASSERT_TRUE(!x.has_value());
    return true;
}

static bool test_null_space()
{
    // [1, 2, 3; 4, 5, 6] has rank 2, null space dim 1
    Mat A(2, 3);
    A(0, 0) = 1;
    A(0, 1) = 2;
    A(0, 2) = 3;
    A(1, 0) = 4;
    A(1, 1) = 5;
    A(1, 2) = 6;
    auto ns = null_space(A);
    ASSERT_TRUE(ns.size() == 1);
    // Verify A * v ≈ 0
    Vec Av = matvec(A, ns[0]);
    ASSERT_NEAR(Av[0], 0.0, 1e-8);
    ASSERT_NEAR(Av[1], 0.0, 1e-8);
    return true;
}

static bool test_determinant()
{
    Mat A(3, 3);
    A(0, 0) = 1;
    A(0, 1) = 2;
    A(0, 2) = 3;
    A(1, 0) = 4;
    A(1, 1) = 5;
    A(1, 2) = 6;
    A(2, 0) = 7;
    A(2, 1) = 8;
    A(2, 2) = 10;
    // det = 1(50-48) - 2(40-42) + 3(32-35) = 2 + 4 - 9 = -3
    ASSERT_NEAR(determinant(A), -3.0, 1e-10);
    return true;
}

void register_linear_algebra_tests()
{
    test_registry().push_back({"linalg::dot_product", test_dot_product});
    test_registry().push_back({"linalg::matvec", test_matvec});
    test_registry().push_back({"linalg::solve_2x2", test_solve_2x2});
    test_registry().push_back({"linalg::solve_singular", test_solve_singular});
    test_registry().push_back({"linalg::null_space", test_null_space});
    test_registry().push_back({"linalg::determinant", test_determinant});
}
