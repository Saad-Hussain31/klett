# Nash Equilibrium Solver

A self-contained C++17 library for computing Nash equilibria of 2-player finite normal-form (bimatrix) games. Implements three distinct algorithms with increasing mathematical sophistication: Support Enumeration, Lemke-Howson, and Homotopy Continuation via the Linear Tracing Procedure.

**Zero external dependencies** -- the library ships its own dense linear algebra, Newton solver, and predictor-corrector path follower. Only the C++ standard library is required.

---

## Key Features

- **Three complementary algorithms** for Nash equilibrium computation
- **Support Enumeration** -- exhaustive search that finds *all* equilibria
- **Lemke-Howson** -- complementary pivoting, guaranteed to find one equilibrium
- **Homotopy Continuation** -- Linear Tracing Procedure (Harsanyi 1975) with adaptive path following
- **Self-contained numerics** -- LU decomposition, null space computation, Newton's method with line search, predictor-corrector continuation
- **Zero external dependencies** -- no Eigen, no Boost, no LAPACK
- **Clean layered architecture** -- core types, numerics, and algorithms are fully decoupled
- **Comprehensive test suite** -- 26 tests covering linear algebra, game construction, and all three solvers
- **Python validation script** -- independent NumPy-based implementation for cross-verification

---

## Supported Algorithms

| Algorithm | Type | Guarantees | Complexity | Best For |
|---|---|---|---|---|
| **Support Enumeration** | Exhaustive | Finds **all** NE | O(2^(m+n) * k^3) | Small games (m,n <= 15) |
| **Lemke-Howson** | Complementary pivoting | Finds **one** NE | Polynomial per run | Medium games, pure/vertex NE |
| **Homotopy Continuation** | Path following (LTP) | Finds **one** NE | Adaptive | General games, robust convergence |

---

## Quick Start

### Prerequisites

- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- CMake 3.14+

### Build

```bash
cd nash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Run

```bash
# Run the demo driver (solves 4 classic games with all 3 algorithms)
./nash_solve

# Run the test suite
./nash_tests
# Or via CTest:
ctest --output-on-failure
```

### Debug Build (with AddressSanitizer + UBSan)

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

---

## Usage Examples

### Define and Solve a Game

```cpp
#include "nash/core/game.hpp"
#include "nash/algorithms/support_enumeration.hpp"
#include "nash/algorithms/lemke_howson.hpp"
#include "nash/algorithms/homotopy.hpp"

using namespace nash;

// Define payoff matrices
Mat A(2, 2), B(2, 2);
A(0,0) = 3; A(0,1) = 0;   // Player 1 payoffs
A(1,0) = 5; A(1,1) = 1;
B(0,0) = 3; B(0,1) = 5;   // Player 2 payoffs
B(1,0) = 0; B(1,1) = 1;

BimatrixGame game(A, B);
game.name = "Prisoner's Dilemma";
```

### Find All Equilibria (Support Enumeration)

```cpp
SupportEnumeration solver;
auto result = solver.solve_all(game);
result.print();  // Prints all found equilibria with payoffs
```

### Find One Equilibrium (Lemke-Howson)

```cpp
LemkeHowson lh(0);  // drop_label = 0
auto eq = lh.solve(game);
eq.print();

// Try all label drops to find multiple equilibria
auto multi = lh.solve_multiple(game);
```

### Find One Equilibrium (Homotopy Continuation)

```cpp
HomotopySolver hs;  // uses default (perturbed uniform) prior
auto eq = hs.solve(game);
eq.print();

// Custom prior beliefs
HomotopySolver hs2({0.7, 0.3}, {0.4, 0.6});
auto eq2 = hs2.solve(game);
```

### Special Game Constructors

```cpp
// Zero-sum game: B = -A
auto zs = BimatrixGame::zero_sum(A);

// Symmetric game: B = A^T
auto sym = BimatrixGame::symmetric(A);
```

### Verify an Equilibrium

```cpp
bool is_ne = verify_equilibrium(game, eq.profile, /*epsilon=*/1e-6);
double residual = nash_residual(game, eq.profile.strategy_1, eq.profile.strategy_2);
```

---

## Example Output

```
╔══════════════════════════════════════════════════╗
║  Game: Matching Pennies
║  Size: 2 x 2
╚══════════════════════════════════════════════════╝

── Support Enumeration (all equilibria) ──
=== Multi-Equilibrium Result ===
Solver: SupportEnumeration
Status: Converged
Found 1 equilibrium
--- Equilibrium 1 ---
Player 1 strategy: [0.500000, 0.500000]
Player 2 strategy: [0.500000, 0.500000]
Payoffs: (0.000000, 0.000000)

── Lemke-Howson (label 0) ──
=== Equilibrium Result ===
Solver: LemkeHowson
Status: Converged
Player 1 strategy: [0.500000, 0.500000]
Player 2 strategy: [0.500000, 0.500000]
Payoffs: (0.000000, 0.000000)

── Homotopy Continuation (uniform prior) ──
=== Equilibrium Result ===
Solver: HomotopyContinuation
Status: Converged
Player 1 strategy: [0.500000, 0.500000]
Player 2 strategy: [0.500000, 0.500000]
Payoffs: (0.000000, 0.000000)
```

---

## Project Structure

```
nash/
├── CMakeLists.txt                  # Build configuration
├── include/nash/
│   ├── core/
│   │   ├── types.hpp               # Vec, Mat, Tolerances, approx_* utilities
│   │   ├── game.hpp                # BimatrixGame class
│   │   └── strategy.hpp            # StrategyProfile, EquilibriumResult, SolverStatus
│   ├── algorithms/
│   │   ├── solver_interface.hpp    # IEquilibriumSolver, IMultiEquilibriumSolver
│   │   ├── support_enumeration.hpp # Exhaustive support enumeration
│   │   ├── lemke_howson.hpp        # Complementary pivoting (Lemke-Howson)
│   │   └── homotopy.hpp           # Homotopy continuation (LTP)
│   └── numerics/
│       ├── linear_algebra.hpp      # Dense LA: LU, null space, matrix ops
│       ├── newton_solver.hpp       # Newton's method with line search
│       └── path_follower.hpp       # Predictor-corrector continuation
├── src/
│   ├── main.cpp                    # Demo driver with classic games
│   ├── core/
│   │   ├── game.cpp
│   │   └── strategy.cpp
│   ├── algorithms/
│   │   ├── support_enumeration.cpp
│   │   ├── lemke_howson.cpp
│   │   └── homotopy.cpp
│   └── numerics/
│       ├── linear_algebra.cpp
│       ├── newton_solver.cpp
│       └── path_follower.cpp
├── tests/
│   ├── test_main.cpp               # Custom test framework
│   ├── test_linear_algebra.cpp     # 6 tests
│   ├── test_game.cpp               # 5 tests
│   ├── test_support_enumeration.cpp# 5 tests
│   ├── test_lemke_howson.cpp       # 5 tests
│   └── test_homotopy.cpp           # 5 tests
└── python/
    └── validate.py                 # Independent Python verification
```

---

## Dependencies

| Dependency | Required | Notes |
|---|---|---|
| C++ Standard Library | Yes | Only dependency for the C++ library |
| CMake 3.14+ | Build only | Build system |
| Python 3 + NumPy | Optional | For `python/validate.py` validation script |

---

## Classic Test Games

The library includes built-in tests against well-known games:

| Game | Size | Known Equilibria | All Solvers Pass |
|---|---|---|---|
| Prisoner's Dilemma | 2x2 | 1 pure: (D,D) | Yes |
| Matching Pennies | 2x2 | 1 mixed: (0.5, 0.5) | Yes |
| Battle of the Sexes | 2x2 | 2 pure + 1 mixed | Yes |
| Rock-Paper-Scissors | 3x3 | 1 mixed: (1/3, 1/3, 1/3) | Yes |
| Coordination Game | 2x2 | 2 pure + 1 mixed | Yes |

---

## Python Validation

An independent Python implementation of support enumeration is provided for cross-verification:

```bash
cd python
python3 validate.py
```

This runs the same classic game tests and benchmarks support enumeration on random games of increasing size.

---

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md) for the full developer guide including:
- Detailed algorithm walkthroughs
- UML class, component, sequence, and flow diagrams
- Mathematical foundations
- Numerical methods deep dive
- Extension guide

---

## Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/my-feature`)
3. Ensure all tests pass (`./nash_tests`)
4. Follow the existing code style (C++17, `nash::` namespace, Doxygen `@file`/`@param` comments)
5. Submit a pull request

---

## License

This project is provided for educational and research purposes.
