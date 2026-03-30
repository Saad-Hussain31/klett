#!/usr/bin/env python3
"""
Nash equilibrium validation and benchmarking script.
Uses numpy and scipy to independently verify equilibria found by the C++ library.
"""

import numpy as np
from itertools import combinations
import time
import subprocess
import json


def support_enumeration(A, B, tol=1e-8):
    """Find all Nash equilibria of bimatrix game (A, B) via support enumeration."""
    m, n = A.shape
    equilibria = []

    for k in range(1, min(m, n) + 1):
        for s1 in combinations(range(m), k):
            for s2 in combinations(range(n), k):
                s1, s2 = list(s1), list(s2)
                eq = try_support(A, B, s1, s2, tol)
                if eq is not None:
                    x, y = eq
                    if is_best_response(A, B, x, y, s1, s2, tol):
                        if not any(np.allclose(x, ex, atol=tol) and np.allclose(y, ey, atol=tol)
                                   for ex, ey in equilibria):
                            equilibria.append((x, y))
    return equilibria


def try_support(A, B, s1, s2, tol=1e-8):
    """Try to find equilibrium strategies with given support."""
    k = len(s1)
    if len(s2) != k:
        return None

    # Solve for y (player 2 strategy on support s2)
    # Indifference: A[s1, :][:, s2] y = v1 * ones
    # Normalization: sum y = 1
    A_sub = A[np.ix_(s1, s2)]
    M_y = np.zeros((k + 1, k + 1))
    M_y[:k, :k] = A_sub
    M_y[:k, k] = -1.0
    M_y[k, :k] = 1.0
    rhs_y = np.zeros(k + 1)
    rhs_y[k] = 1.0

    try:
        sol_y = np.linalg.solve(M_y, rhs_y)
    except np.linalg.LinAlgError:
        return None

    y_sub = sol_y[:k]
    if np.any(y_sub < -tol):
        return None

    # Solve for x (player 1 strategy on support s1)
    B_sub = B[np.ix_(s1, s2)]
    M_x = np.zeros((k + 1, k + 1))
    M_x[:k, :k] = B_sub.T
    M_x[:k, k] = -1.0
    M_x[k, :k] = 1.0
    rhs_x = np.zeros(k + 1)
    rhs_x[k] = 1.0

    try:
        sol_x = np.linalg.solve(M_x, rhs_x)
    except np.linalg.LinAlgError:
        return None

    x_sub = sol_x[:k]
    if np.any(x_sub < -tol):
        return None

    x = np.zeros(A.shape[0])
    y = np.zeros(A.shape[1])
    x[s1] = np.maximum(0, x_sub)
    y[s2] = np.maximum(0, y_sub)

    return x, y


def is_best_response(A, B, x, y, s1, s2, tol=1e-8):
    """Check if x is best response to y and vice versa."""
    Ay = A @ y
    v1 = max(Ay[i] for i in s1)
    if any(Ay[i] > v1 + tol for i in range(len(x))):
        return False

    Btx = B.T @ x
    v2 = max(Btx[j] for j in s2)
    if any(Btx[j] > v2 + tol for j in range(len(y))):
        return False

    return True


def nash_residual(A, B, x, y):
    """Compute max regret (Nash residual)."""
    Ay = A @ y
    v1 = x @ Ay
    Btx = B.T @ x
    v2 = y @ Btx
    return max(max(Ay - v1), max(Btx - v2))


def verify_equilibrium(A, B, x, y, tol=1e-6):
    """Verify that (x, y) is an epsilon-Nash equilibrium."""
    if abs(sum(x) - 1) > tol or abs(sum(y) - 1) > tol:
        return False
    if any(v < -tol for v in x) or any(v < -tol for v in y):
        return False
    return nash_residual(A, B, x, y) < tol


# ── Test games ───────────────────────────────────────────────────────

def test_prisoners_dilemma():
    A = np.array([[3, 0], [5, 1]], dtype=float)
    B = np.array([[3, 5], [0, 1]], dtype=float)
    eqs = support_enumeration(A, B)
    assert len(eqs) == 1, f"Expected 1 NE, got {len(eqs)}"
    x, y = eqs[0]
    assert np.allclose(x, [0, 1], atol=1e-6), f"Expected (D, D), got {x}"
    print(f"  Prisoner's Dilemma: {len(eqs)} NE, x={x}, y={y}")


def test_matching_pennies():
    A = np.array([[1, -1], [-1, 1]], dtype=float)
    B = -A
    eqs = support_enumeration(A, B)
    assert len(eqs) >= 1
    x, y = eqs[0]
    assert np.allclose(x, [0.5, 0.5], atol=1e-6)
    print(f"  Matching Pennies: {len(eqs)} NE, x={x}, y={y}")


def test_battle_of_sexes():
    A = np.array([[3, 0], [0, 2]], dtype=float)
    B = np.array([[2, 0], [0, 3]], dtype=float)
    eqs = support_enumeration(A, B)
    assert len(eqs) == 3, f"Expected 3 NE, got {len(eqs)}"
    print(f"  Battle of Sexes: {len(eqs)} NE")
    for i, (x, y) in enumerate(eqs):
        res = nash_residual(A, B, x, y)
        print(f"    NE {i+1}: x={x}, y={y}, residual={res:.2e}")


def test_rps():
    A = np.array([[0, -1, 1], [1, 0, -1], [-1, 1, 0]], dtype=float)
    B = -A
    eqs = support_enumeration(A, B)
    assert len(eqs) >= 1
    x, y = eqs[0]
    assert np.allclose(x, [1/3, 1/3, 1/3], atol=1e-6)
    print(f"  Rock-Paper-Scissors: {len(eqs)} NE, x={np.round(x, 4)}")


def benchmark_random_games(sizes=[(2,2), (3,3), (4,4), (5,5), (8,8), (10,10)],
                           n_games=10):
    """Benchmark support enumeration on random games."""
    print("\n  Benchmark: Support Enumeration on random games")
    print(f"  {'Size':>8} {'Avg NE':>8} {'Avg Time (ms)':>14}")
    print("  " + "-" * 34)

    for m, n in sizes:
        times = []
        n_eq = []
        for _ in range(n_games):
            A = np.random.randn(m, n)
            B = np.random.randn(m, n)
            t0 = time.time()
            eqs = support_enumeration(A, B)
            t1 = time.time()
            times.append((t1 - t0) * 1000)
            n_eq.append(len(eqs))
        print(f"  {f'{m}x{n}':>8} {np.mean(n_eq):>8.1f} {np.mean(times):>14.2f}")


if __name__ == "__main__":
    print("Nash Equilibrium Validation (Python)")
    print("=" * 50)

    print("\nClassic game tests:")
    test_prisoners_dilemma()
    test_matching_pennies()
    test_battle_of_sexes()
    test_rps()
    print("\nAll tests passed!")

    benchmark_random_games()
