/// @file main.cpp
/// Nash equilibrium solver - command-line driver.

#include "nash/core/game.hpp"
#include "nash/core/strategy.hpp"
#include "nash/algorithms/support_enumeration.hpp"
#include "nash/algorithms/lemke_howson.hpp"
#include "nash/algorithms/homotopy.hpp"
#include <iostream>
#include <chrono>

using namespace nash;

namespace
{

    BimatrixGame make_prisoners_dilemma()
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
        BimatrixGame g(A, B);
        g.name = "Prisoner's Dilemma";
        return g;
    }

    BimatrixGame make_matching_pennies()
    {
        Mat A(2, 2);
        A(0, 0) = 1;
        A(0, 1) = -1;
        A(1, 0) = -1;
        A(1, 1) = 1;
        auto g = BimatrixGame::zero_sum(A);
        g.name = "Matching Pennies";
        return g;
    }

    BimatrixGame make_battle_of_sexes()
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
        BimatrixGame g(A, B);
        g.name = "Battle of the Sexes";
        return g;
    }

    BimatrixGame make_rps()
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
        auto g = BimatrixGame::zero_sum(A);
        g.name = "Rock-Paper-Scissors";
        return g;
    }

    template <typename F>
    void timed(const char *label, F &&func)
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        func();
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "  [" << label << ": " << ms << " ms]" << std::endl;
    }

    void solve_and_print(const BimatrixGame &game)
    {
        std::cout << "\n╔══════════════════════════════════════════════════╗" << std::endl;
        std::cout << "║  Game: " << game.name << std::endl;
        std::cout << "║  Size: " << game.num_actions_1() << " x " << game.num_actions_2() << std::endl;
        std::cout << "╚══════════════════════════════════════════════════╝\n"
                  << std::endl;

        // 1. Support Enumeration
        std::cout << "── Support Enumeration (all equilibria) ──" << std::endl;
        timed("time", [&]()
              {
        SupportEnumeration se;
        auto result = se.solve_all(game);
        result.print(); });

        // 2. Lemke-Howson
        std::cout << "── Lemke-Howson (label 0) ──" << std::endl;
        timed("time", [&]()
              {
        LemkeHowson lh(0);
        auto result = lh.solve(game);
        result.print(); });

        // 3. Homotopy Continuation
        std::cout << "── Homotopy Continuation (uniform prior) ──" << std::endl;
        timed("time", [&]()
              {
        HomotopySolver hs;
        auto result = hs.solve(game);
        result.print(); });
    }

} // anonymous namespace

int main()
{
    std::cout << "════════════════════════════════════════════════════" << std::endl;
    std::cout << "  Nash Equilibrium Solver Library" << std::endl;
    std::cout << "  Algorithms: Support Enumeration, Lemke-Howson," << std::endl;
    std::cout << "              Homotopy Continuation (LTP)" << std::endl;
    std::cout << "════════════════════════════════════════════════════" << std::endl;

    solve_and_print(make_prisoners_dilemma());
    solve_and_print(make_matching_pennies());
    solve_and_print(make_battle_of_sexes());
    solve_and_print(make_rps());

    return 0;
}
