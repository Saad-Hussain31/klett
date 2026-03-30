# Nash Equilibrium Solver

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![License](https://img.shields.io/badge/license-Research-green.svg)](LICENSE)
[![Tests](https://img.shields.io/badge/tests-26%20passing-brightgreen.svg)](#testing)

A **self-contained C++17 library** for computing Nash equilibria of 2-player finite normal-form (bimatrix) games. Implements three distinct algorithms with increasing mathematical sophistication: **Support Enumeration**, **Lemke-Howson**, and **Homotopy Continuation**.

**Zero external dependencies** — Ships with dense linear algebra, Newton solver, and predictor-corrector path follower. Only the C++ standard library is required.

---

## 🎯 Core Features

- **Three complementary algorithms** for finding Nash equilibria
  - **Support Enumeration** — exhaustive search, finds *all* equilibria
  - **Lemke-Howson** — complementary pivoting, guaranteed to find one NE
  - **Homotopy Continuation** — Linear Tracing Procedure (Harsanyi 1975), robust path following
- **Self-contained numerics** — LU decomposition, null space computation, Newton's method with line search, predictor-corrector continuation
- **Zero external dependencies** — no Eigen, no Boost, no LAPACK
- **Clean layered architecture** — core types, numerics, and algorithms fully decoupled
- **Comprehensive test coverage** — 26 tests, classic games verified against all solvers
- **Python validation script** — independent NumPy implementation for cross-verification
- **Extensible design** — interface-based solvers, easy to add new algorithms

---

## 📊 Algorithm Comparison

| Feature | Support Enumeration | Lemke-Howson | Homotopy Continuation |
|---------|---|---|---|
| **Finds** | All equilibria | One equilibrium | One equilibrium |
| **Type** | Exhaustive search | Complementary pivoting | Path following |
| **Complexity** | O(2^(m+n) · k³) | Polynomial per run | Adaptive |
| **Scalability** | m,n ≤ 15 | m,n ≤ 50 | m,n ≤ 100 |
| **Pure/Mixed** | Both | Vertices preferred | Any support |
| **Robustness** | High (exact) | High (labeled pivoting) | Very high (adaptive stepping) |
| **Handles zero-sum** | Yes | Yes (with shift) | Yes |
| **Best for** | Small games, complete enumeration | General purpose | Ill-conditioned games |

---

## 🚀 Quick Start

### Prerequisites

- **C++17 compatible compiler** (GCC 7+, Clang 5+, MSVC 2017+)
- **CMake 3.14+**
- **Python 3 + NumPy** (optional, for validation script)

### Build

```bash
cd nash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Run Demo

```bash
# Solves 4 classic games with all 3 algorithms
./nash_solve

# Run full test suite
./nash_tests

# Or via CTest
ctest --output-on-failure
```

### Debug Build (with AddressSanitizer + UBSan)

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug
make
```

---

## 💻 Usage Examples

### Define a Game

```cpp
#include "nash/core/game.hpp"
#include "nash/algorithms/support_enumeration.hpp"

using namespace nash;

// Create payoff matrices (2x2 game)
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
result.print();  // Prints all equilibria found
```

**Output:**
```
=== Multi-Equilibrium Result ===
Solver: SupportEnumeration
Status: Converged
Found 1 equilibrium
--- Equilibrium 1 ---
Player 1 strategy: [0.000000, 1.000000]
Player 2 strategy: [0.000000, 1.000000]
Payoffs: (1.000000, 1.000000)
```

### Find One Equilibrium (Lemke-Howson)

```cpp
#include "nash/algorithms/lemke_howson.hpp"

LemkeHowson lh(0);  // drop_label = 0
auto eq = lh.solve(game);
eq.print();

// Try multiple label drops to find different equilibria
auto multi = lh.solve_multiple(game);
std::cout << "Found " << multi.equilibria.size() << " equilibria" << std::endl;
```

### Find One Equilibrium (Homotopy Continuation)

```cpp
#include "nash/algorithms/homotopy.hpp"

HomotopySolver hs;  // Uses uniform prior by default
auto eq = hs.solve(game);
eq.print();

// Specify custom prior beliefs
Vec prior_1 = {0.7, 0.3};  // P1 likely plays first action
Vec prior_2 = {0.4, 0.6};  // P2 likely plays second action
HomotopySolver hs_custom(prior_1, prior_2);
auto eq2 = hs_custom.solve(game);
```

### Special Game Constructors

```cpp
// Zero-sum game (B = -A)
auto matching_pennies = BimatrixGame::zero_sum(payoff_matrix);

// Symmetric game (B = A^T)
auto sym_game = BimatrixGame::symmetric(payoff_matrix);
```

### Verify Equilibrium

```cpp
// Compute Nash residual (max regret from unilateral deviation)
double residual = nash_residual(game, eq.profile.strategy_1, eq.profile.strategy_2);
std::cout << "Residual: " << residual << std::endl;

// Check if profile is ε-Nash equilibrium
bool is_eq = verify_equilibrium(game, eq.profile, /*epsilon=*/1e-6);
if (is_eq) {
    std::cout << "Valid Nash equilibrium!" << std::endl;
}
```

---

## 📈 Complete Example Output

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
  [time: 0.234 ms]

── Lemke-Howson (label 0) ──
=== Equilibrium Result ===
Solver: LemkeHowson
Status: Converged
Player 1 strategy: [0.500000, 0.500000]
Player 2 strategy: [0.500000, 0.500000]
Payoffs: (0.000000, 0.000000)
Residual: 2.22045e-16
Iterations: 3
  [time: 0.187 ms]

── Homotopy Continuation (uniform prior) ──
=== Equilibrium Result ===
Solver: HomotopyContinuation
Status: Converged
Player 1 strategy: [0.500000, 0.500000]
Player 2 strategy: [0.500000, 0.500000]
Payoffs: (0.000000, 0.000000)
Residual: 1.11022e-16
Iterations: 42
  [time: 0.412 ms]
```

---

## 📚 Project Structure

```
nash/
├── README.md                           # This file
├── ARCHITECTURE.md                     # Deep technical documentation
├── CMakeLists.txt                      # Build configuration
├── include/nash/
│   ├── core/
│   │   ├── types.hpp                   # Vec, Mat, Tolerances
│   │   ├── game.hpp                    # BimatrixGame class
│   │   └── strategy.hpp                # StrategyProfile, EquilibriumResult
│   ├── algorithms/
│   │   ├── solver_interface.hpp        # IEquilibriumSolver interface
│   │   ├── support_enumeration.hpp     # Support enumeration solver
│   │   ├── lemke_howson.hpp            # Lemke-Howson complementary pivoting
│   │   └── homotopy.hpp                # Homotopy continuation (LTP)
│   └── numerics/
│       ├── linear_algebra.hpp          # Dense LA: LU, null space, matrix ops
│       ├── newton_solver.hpp           # Newton's method + line search
│       └── path_follower.hpp           # Predictor-corrector path following
├── src/
│   ├── main.cpp                        # Demo driver
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
│   ├── test_main.cpp                   # Custom test framework
│   ├── test_linear_algebra.cpp         # 6 tests
│   ├── test_game.cpp                   # 5 tests
│   ├── test_support_enumeration.cpp    # 5 tests
│   ├── test_lemke_howson.cpp           # 5 tests
│   └── test_homotopy.cpp               # 5 tests
└── python/
    └── validate.py                     # Independent NumPy verification
```

---

## 🧮 Dependencies

| Dependency | Required | Purpose |
|---|---|---|
| **C++ Standard Library** | ✓ Yes | Only library dependency |
| **CMake 3.14+** | ✓ Yes (build) | Build system |
| **Python 3 + NumPy** | Optional | Validation script only |

---

## 🎮 Classic Test Games

All algorithms are verified on well-known games with known equilibria:

| Game | Type | Equilibria | All Solvers ✓ |
|------|------|---|---|
| **Prisoner's Dilemma** | 2×2 | 1 pure: (Defect, Defect) | ✓ |
| **Matching Pennies** | 2×2 | 1 mixed: (0.5, 0.5) each | ✓ |
| **Battle of the Sexes** | 2×2 | 2 pure + 1 mixed | ✓ |
| **Rock-Paper-Scissors** | 3×3 | 1 mixed: (1/3, 1/3, 1/3) | ✓ |
| **Coordination Game** | 2×2 | 2 pure + 1 mixed | ✓ |

---

## 🧪 Testing & Validation

### Run Unit Tests

```bash
cd build
./nash_tests
# or
ctest --output-on-failure
```

**Test Coverage**:
- **6 tests** — Linear algebra (LU, null space, determinant, submatrix)
- **5 tests** — Game construction and payoff computation
- **5 tests** — Support enumeration (all equilibria, edge cases)
- **5 tests** — Lemke-Howson (multi-label drops, convergence)
- **5 tests** — Homotopy continuation (path following, convergence)

**Total: 26 passing tests** across all components.

### Python Validation

Independent NumPy-based implementation for cross-verification:

```bash
cd python
python3 validate.py
```

Tests:
- Same classic games against C++ solvers
- Random games of increasing size
- Verifies residual ≤ 1e-9 for all equilibria found
- Benchmarks support enumeration scaling

---

## 📖 Documentation

This repository includes comprehensive documentation:

1. **README.md** (this file) — Quick start, usage examples, overview
2. **ARCHITECTURE.md** — In-depth technical guide including:
   - Mathematical foundations (Nash equilibrium theory)
   - System architecture and module design
   - UML diagrams (component, class, sequence, flow)
   - Algorithm deep dives with pseudocode
   - Numerical methods explanations
   - Execution flow diagrams
   - Edge cases and error handling
   - Performance analysis
   - Extension guide for new solvers
   - Troubleshooting guide

**To read the full technical documentation**, see [ARCHITECTURE.md](ARCHITECTURE.md).

---

## 🛠 Building for Different Configurations

### Release Build (Optimized)

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-O3"
make -j$(nproc)
```

### Debug Build (with sanitizers)

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug
make
./nash_tests  # Run with AddressSanitizer + UBSan
```

### With Verbose Output

```bash
cmake .. -DCMAKE_VERBOSE_MAKEFILE=ON
make
```

---

## 🎓 Understanding the Mathematics

### Nash Equilibrium

A strategy profile **(x*, y*)** is a **Nash Equilibrium** if neither player can improve their payoff by unilateral deviation:

```
u₁(x*, y*) ≥ u₁(x, y*)  for all strategies x
u₂(x*, y*) ≥ u₂(x*, y)  for all strategies y
```

### Key Concepts

- **Mixed strategy**: Probability distribution over actions
- **Support**: Set of actions played with positive probability
- **Best response**: Strategy maximizing payoff against opponent's strategy
- **Indifference**: In equilibrium, all actions in support yield equal payoff

For detailed mathematical treatment, see [ARCHITECTURE.md → Mathematical Foundations](ARCHITECTURE.md#mathematical-foundations).

---

## 🔍 When to Use Each Algorithm

### Support Enumeration
**Best for**: Small games (m, n ≤ 12), need all equilibria, complete enumeration
```cpp
SupportEnumeration solver;
auto result = solver.solve_all(game);
```

### Lemke-Howson
**Best for**: Medium-sized games, guaranteed convergence, preference for vertex solutions
```cpp
LemkeHowson solver(0);  // Try different label drops for multiple NE
auto result = solver.solve(game);
```

### Homotopy Continuation
**Best for**: Large games, ill-conditioned games, robust convergence, custom priors
```cpp
Vec prior_1 = {0.6, 0.4};
HomotopySolver solver(prior_1, {});
auto result = solver.solve(game);
```

---

## ⚙️ Configuration & Tuning

### Numerical Tolerances

```cpp
Tolerances tol;
tol.eps = 1e-10;           // Near-zero threshold
tol.pivot_tol = 1e-12;     // LU pivot tolerance
tol.conv_tol = 1e-9;       // Convergence criterion
tol.max_iter = 10000;      // Max iterations

SupportEnumeration solver(tol);
```

### Path Follower Configuration

```cpp
PathFollowerConfig cfg;
cfg.initial_step = 0.01;
cfg.max_step = 0.1;
cfg.step_increase = 1.5;
cfg.corrector_iter = 10;
cfg.max_steps = 50000;

HomotopySolver solver({}, {}, cfg);
```

---

## 🤝 Contributing

We welcome contributions! Here's how to get involved:

1. **Read the architecture guide** — [ARCHITECTURE.md](ARCHITECTURE.md)
2. **Fork the repository** and create a feature branch
3. **Implement your changes** following the code style guide below
4. **Add tests** for your changes (see `tests/` for examples)
5. **Run the full test suite** — `./nash_tests`
6. **Submit a pull request** with a clear description

### Code Style Guide

- **Language**: C++17 (standard library only)
- **Namespace**: All code in `nash::` namespace
- **Naming**: `snake_case` for functions/variables, `PascalCase` for types
- **Documentation**: Doxygen-style comments with `@file`, `@param`, `@return`
- **Formatting**: 4-space indentation, no tabs
- **Comments**: Explain "why", not "what"; code should be self-documenting

### Example Contribution: Adding a New Solver

See [ARCHITECTURE.md → Extension Guide](ARCHITECTURE.md#extension-guide) for step-by-step instructions on adding a new algorithm.

---

## 🐛 Troubleshooting

### Matrix singular error
The system of indifference equations is singular (support infeasible). Try a different algorithm.

### Lemke-Howson: Unbounded
Payoffs must be strictly positive. Use `game.make_positive()` (done automatically by solver).

### Homotopy: Step size too small
Path following got stuck. Try different prior beliefs or different initial support.

### Support Enumeration: Too slow
Game is too large for exhaustive search. Switch to Lemke-Howson or Homotopy for m, n > 15.

**For detailed troubleshooting**, see [ARCHITECTURE.md → Troubleshooting Guide](ARCHITECTURE.md#troubleshooting-guide).

---

## 📊 Performance

Typical wall-clock times on modern CPU (single core):

| Game Size | Support Enum. | Lemke-Howson | Homotopy |
|---|---|---|---|
| 2×2 | 0.1 ms | 0.05 ms | 0.2 ms |
| 5×5 | 5 ms | 0.2 ms | 0.5 ms |
| 10×10 | 5 sec | 1 ms | 2 ms |
| 20×20 | N/A | 5 ms | 10 ms |

**Note**: Support Enumeration exponential in game size (O(2^(m+n))); unsuitable for m, n > 15.

For detailed complexity analysis, see [ARCHITECTURE.md → Performance & Complexity](ARCHITECTURE.md#performance--complexity).

---

## 📄 License

This project is provided for **educational and research purposes**. See LICENSE file for details.

---

## 🔗 References

### Algorithm Papers

- **Lemke-Howson** (1964) — "Equilibrium points of bimatrix games"
- **Support Enumeration** — Classic Nash equilibrium theory (von Neumann, Nash)
- **Homotopy Continuation / LTP** — Harsanyi (1975), Govindan & Wilson (2003), "A Global Newton Method"

### Key Books

- **Game Theory, Alive** — Karlin & Peres
- **Game Theory and Strategy** — Binmore
- **The Theory of Games and Economic Behavior** — von Neumann & Morgenstern

### Implementation Reference

- **Computational Aspects of Game Theory** — Nisan et al.
- **Lectures on Game Theory** — Osborne & Rubinstein

---

## ❓ FAQ

**Q: Does this library support 3+ player games?**
A: Currently 2-player games only. N-player is fundamentally different (different solution concepts). Future versions may extend.

**Q: Can I use this for commercial purposes?**
A: Check the LICENSE file. For research and educational use, unrestricted.

**Q: How large can games be?**
A: Tested up to ~100×100 with Lemke-Howson and Homotopy. Support Enumeration limited to ~15 actions per player.

**Q: What about floating-point precision?**
A: Double precision throughout. Tolerances tunable. Not suitable for certification/exact computation (would require arbitrary precision).

**Q: Can I parallelize the solvers?**
A: Support enumeration naturally parallelizable (each support pair independent). Lemke-Howson and Homotopy are inherently sequential.

---

## 📬 Feedback & Issues

- **Questions?** Open an issue with tag `[question]`
- **Found a bug?** File an issue with reproducible example
- **Feature request?** Check existing issues; suggest via discussion

---

**Happy computing! 🎮**
