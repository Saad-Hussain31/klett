/// @file test_main.cpp
/// Minimal test framework and entry point.

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <cmath>

// Simple test framework
struct TestCase
{
    std::string name;
    std::function<bool()> func;
};

std::vector<TestCase> &test_registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

#define REGISTER_TEST(name, func) \
    static bool _reg_##func = (test_registry().push_back({name, func}), true)

#define ASSERT_TRUE(cond)                                                                           \
    do                                                                                              \
    {                                                                                               \
        if (!(cond))                                                                                \
        {                                                                                           \
            std::cerr << "  FAIL: " << #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false;                                                                           \
        }                                                                                           \
    } while (0)

#define ASSERT_NEAR(a, b, tol)                                                                                     \
    do                                                                                                             \
    {                                                                                                              \
        if (std::fabs((a) - (b)) > (tol))                                                                          \
        {                                                                                                          \
            std::cerr << "  FAIL: " << #a << " ≈ " << #b << " (" << (a) << " vs " << (b)                           \
                      << ", diff=" << std::fabs((a) - (b)) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false;                                                                                          \
        }                                                                                                          \
    } while (0)

// External test registration functions
void register_linear_algebra_tests();
void register_game_tests();
void register_support_enumeration_tests();
void register_lemke_howson_tests();
void register_homotopy_tests();

int main()
{
    register_linear_algebra_tests();
    register_game_tests();
    register_support_enumeration_tests();
    register_lemke_howson_tests();
    register_homotopy_tests();

    int passed = 0, failed = 0;
    for (const auto &test : test_registry())
    {
        std::cout << "[ RUN  ] " << test.name << std::endl;
        bool ok = false;
        try
        {
            ok = test.func();
        }
        catch (const std::exception &e)
        {
            std::cerr << "  EXCEPTION: " << e.what() << std::endl;
        }
        if (ok)
        {
            std::cout << "[ PASS ] " << test.name << std::endl;
            ++passed;
        }
        else
        {
            std::cout << "[ FAIL ] " << test.name << std::endl;
            ++failed;
        }
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << passed << " passed, " << failed << " failed, "
              << (passed + failed) << " total" << std::endl;

    return failed > 0 ? 1 : 0;
}
