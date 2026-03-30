# Developer Guide — API Reference & Integration

**A practical guide for developers integrating the Nash equilibrium solver into applications or extending the library with new algorithms.**

---

## Table of Contents

1. [API Reference](#api-reference)
2. [Integration Patterns](#integration-patterns)
3. [Common Workflows](#common-workflows)
4. [Adding Custom Solvers](#adding-custom-solvers)
5. [Error Handling](#error-handling)
6. [Performance Tips](#performance-tips)
7. [FAQ](#faq)

---

## API Reference

### Core Types

#### `Vec` — Dense Vector

```cpp
using Vec = std::vector<double>;
```

**Operations** (in `nash::linalg` namespace):

```cpp
double dot(const Vec& a, const Vec& b);              // Dot product
double norm2(const Vec& v);                          // Euclidean norm
double norm_inf(const Vec& v);                       // Max absolute value
Vec add(const Vec& a, const Vec& b);                 // Vector addition
Vec sub(const Vec& a, const Vec& b);                 // Vector subtraction
Vec scale(double alpha, const Vec& a);               // Scalar multiply
Vec axpy(double alpha, const Vec& b, const Vec& a);  // a + alpha*b
```

#### `Mat` — Dense Matrix (Row-Major)

```cpp
struct Mat {
    std::size_t rows, cols;
    std::vector<double> data;

    void resize(std::size_t r, std::size_t c, double val = 0.0);
    double& operator()(std::size_t i, std::size_t j);       // Element access [i,j]
    double operator()(std::size_t i, std::size_t j) const;  // Read-only
};
```

**Operations** (in `nash::linalg` namespace):

```cpp
Vec matvec(const Mat& A, const Vec& x);              // A * x
Vec matvec_transpose(const Mat& A, const Vec& x);    // A^T * x
Mat matmul(const Mat& A, const Mat& B);              // A * B
Mat transpose(const Mat& A);                         // A^T
Mat eye(std::size_t n);                              // Identity matrix
```

#### `BimatrixGame` — 2-Player Game

```cpp
class BimatrixGame {
public:
    BimatrixGame();
    BimatrixGame(Mat A, Mat B);

    // Factory methods
    static BimatrixGame zero_sum(Mat A);
    static BimatrixGame symmetric(Mat A);

    // Accessors
    std::size_t num_actions_1() const;   // m
    std::size_t num_actions_2() const;   // n
    const Mat& payoff_1() const;
    const Mat& payoff_2() const;
    Mat& payoff_1();
    Mat& payoff_2();

    // Direct payoff access
    double u1(std::size_t i, std::size_t j) const;
    double u2(std::size_t i, std::size_t j) const;

    // Expected payoff (for mixed strategy)
    double expected_payoff_1(const Vec& x, const Vec& y) const;  // x^T·A·y
    double expected_payoff_2(const Vec& x, const Vec& y) const;  // x^T·B·y

    // Utilities
    double make_positive();    // Shift payoffs so all > 1 (for Lemke-Howson)

    std::string name;          // Optional game identifier
};
```

#### `StrategyProfile` — Strategy Pair

```cpp
struct StrategyProfile {
    Vec strategy_1;    // Probability distribution over player 1's actions (size m)
    Vec strategy_2;    // Probability distribution over player 2's actions (size n)

    bool is_valid(double tol = 1e-8) const;              // All elements ∈ [0,1], sum ≈ 1
    std::vector<std::size_t> support_1(double tol = 1e-10) const;  // Indices with xᵢ > tol
    std::vector<std::size_t> support_2(double tol = 1e-10) const;  // Indices with yⱼ > tol
};
```

#### `EquilibriumResult` — Single Equilibrium

```cpp
struct EquilibriumResult {
    StrategyProfile profile;          // The equilibrium strategy pair
    double residual = 0.0;            // ||F|| - how close to exact equilibrium
    int iterations = 0;               // Iterations taken
    SolverStatus status;              // Converged, MaxIterations, etc.
    std::string solver_name;          // Name of solver
    double payoff_1 = 0.0;            // Expected payoff for player 1
    double payoff_2 = 0.0;            // Expected payoff for player 2

    void print(std::ostream& os = std::cout) const;  // Pretty-print result
};

enum class SolverStatus {
    Converged,           // Found equilibrium to tolerance
    MaxIterations,       // Failed - iteration limit exceeded
    NumericalFailure,    // Failed - singular matrix, etc.
    Infeasible,          // Failed - no solution for given support
    NotRun               // Never executed
};
```

#### `MultiEquilibriumResult` — Multiple Equilibria

```cpp
struct MultiEquilibriumResult {
    std::vector<EquilibriumResult> equilibria;  // All equilibria found
    SolverStatus status;                        // Overall status
    std::string solver_name;                    // Solver identifier
    int total_iterations = 0;                   // Total iterations across all

    void print(std::ostream& os = std::cout) const;  // Pretty-print all results
};
```

#### `Tolerances` — Numerical Configuration

```cpp
struct Tolerances {
    double eps = 1e-10;           // "Near zero" threshold
    double pivot_tol = 1e-12;     // Minimum LU pivot magnitude
    double conv_tol = 1e-9;       // Convergence tolerance (residual threshold)
    int max_iter = 10000;         // Maximum iterations
};

inline const Tolerances kDefaultTol{};  // Global default tolerances
```

#### Approximation Utilities

```cpp
bool approx_zero(double v, double tol = 1e-10);       // |v| < tol
bool approx_eq(double a, double b, double tol = 1e-10);  // |a - b| < tol
bool approx_ge(double a, double b, double tol = 1e-10);  // a > b - tol (≥ with tolerance)
```

---

### Solver Interfaces

#### `IEquilibriumSolver` — Find One Equilibrium

```cpp
class IEquilibriumSolver {
public:
    virtual ~IEquilibriumSolver() = default;
    virtual EquilibriumResult solve(const BimatrixGame& game) = 0;
    virtual const char* name() const = 0;
};
```

**Implementations**:
- `LemkeHowson`
- `HomotopySolver`

#### `IMultiEquilibriumSolver` — Find All Equilibria

```cpp
class IMultiEquilibriumSolver {
public:
    virtual ~IMultiEquilibriumSolver() = default;
    virtual MultiEquilibriumResult solve_all(const BimatrixGame& game) = 0;
    virtual const char* name() const = 0;
};
```

**Implementations**:
- `SupportEnumeration`

---

### Concrete Solvers

#### `SupportEnumeration`

```cpp
class SupportEnumeration : public IMultiEquilibriumSolver {
public:
    explicit SupportEnumeration(Tolerances tol = kDefaultTol);

    // Find all Nash equilibria
    MultiEquilibriumResult solve_all(const BimatrixGame& game) override;

    // Also find just one (first found)
    EquilibriumResult solve_one(const BimatrixGame& game);

    const char* name() const override { return "SupportEnumeration"; }
};
```

**Complexity**: O(2^(m+n) · k³) — exhaustive over all support pairs
**Best for**: m, n ≤ 15 (complete enumeration)
**Returns**: All equilibria (may find multiple)

#### `LemkeHowson`

```cpp
class LemkeHowson : public IEquilibriumSolver {
public:
    // @param drop_label: Initial label to drop (0...m+n-1)
    // @param tol: Numerical tolerances
    explicit LemkeHowson(int drop_label = 0, Tolerances tol = kDefaultTol);

    EquilibriumResult solve(const BimatrixGame& game) override;
    const char* name() const override { return "LemkeHowson"; }

    // Find multiple equilibria by trying all label drops
    MultiEquilibriumResult solve_multiple(const BimatrixGame& game);
};
```

**Complexity**: Polynomial per label drop (usually O(k³) or better)
**Best for**: m, n ≤ 50; quick general solver
**Returns**: One NE per `solve()` call; up to m+n via `solve_multiple()`

**Key property**: Guaranteed to find at least one NE for any bimatrix game (requires positive payoffs).

#### `HomotopySolver`

```cpp
class HomotopySolver : public IEquilibriumSolver {
public:
    // @param prior_1: Prior belief about player 1's strategy ([] = uniform)
    // @param prior_2: Prior belief about player 2's strategy ([] = uniform)
    // @param cfg: Path follower configuration
    // @param tol: Numerical tolerances
    HomotopySolver(Vec prior_1 = {}, Vec prior_2 = {},
                   PathFollowerConfig cfg = {},
                   Tolerances tol = kDefaultTol);

    EquilibriumResult solve(const BimatrixGame& game) override;
    const char* name() const override { return "HomotopyContinuation"; }

    void set_prior(Vec p1, Vec p2);
    PathFollowerConfig& config();
    const PathFollowerConfig& config() const;
};
```

**Complexity**: O(steps · k³); adaptive step sizing adjusts steps automatically
**Best for**: Medium to large games; robust convergence; custom priors
**Returns**: One NE found via path following

**Key idea**: Homotopy deformation from t=0 (best response to prior) to t=1 (true Nash equilibrium).

---

### Verification Utilities

```cpp
// Compute maximum payoff loss from unilateral deviation (residual)
double nash_residual(const BimatrixGame& game, const Vec& x, const Vec& y);

// Check if (x, y) is an ε-Nash equilibrium
bool verify_equilibrium(const BimatrixGame& game,
                       const StrategyProfile& profile,
                       double epsilon = 1e-6);
```

---

### Linear Algebra API

#### LU Decomposition

```cpp
namespace linalg {

struct LU {
    Mat LU_combined;               // Combined L\U factor
    std::vector<std::size_t> perm; // Row permutation
    int sign;                      // Sign of permutation (for det)
    bool singular;                 // True if zero pivot detected
};

// Compute LU decomposition with partial pivoting
LU lu_decompose(const Mat& A, double pivot_tol = 1e-12);

// Solve using pre-computed LU
Vec lu_solve(const LU& lu, const Vec& b);

// Solve directly (computes LU internally)
std::optional<Vec> solve(const Mat& A, const Vec& b, double pivot_tol = 1e-12);

// Null space of matrix (for rectangular A with m < n)
std::vector<Vec> null_space(const Mat& A, double tol = 1e-10);

// Matrix determinant
double determinant(const Mat& A);

}
```

#### Submatrix Operations

```cpp
namespace linalg {

// Extract rows/columns
Mat submatrix(const Mat& A,
              const std::vector<std::size_t>& row_idx,
              const std::vector<std::size_t>& col_idx);

Vec column(const Mat& A, std::size_t j);
Vec row(const Mat& A, std::size_t i);

}
```

---

### Newton Solver

```cpp
struct NonlinearSystem {
    std::function<Vec(const Vec&)> F;   // Evaluate F(x)
    std::function<Mat(const Vec&)> J;   // Evaluate Jacobian (optional; use numerical if null)
    std::size_t dim;                    // System dimension
};

struct NewtonResult {
    Vec x;                // Solution
    double residual;      // ||F(x)||_∞
    int iterations;       // Iterations taken
    bool converged;       // Whether converged to tolerance
};

NewtonResult newton_solve(const NonlinearSystem& sys,
                         const Vec& x0,
                         double tol = 1e-10,
                         int max_iter = 100);

// Compute numerical Jacobian via finite differences
Mat numerical_jacobian(const std::function<Vec(const Vec&)>& F,
                       const Vec& x,
                       double h = 1e-7);
```

---

### Path Following (Homotopy)

```cpp
struct PathFollowerConfig {
    double initial_step = 0.01;      // Initial step along curve
    double min_step = 1e-8;          // Failure threshold
    double max_step = 0.1;           // Upper bound
    double step_increase = 1.5;      // Growth factor
    double step_decrease = 0.5;      // Shrink factor
    int corrector_iter = 10;         // Newton corrector iterations
    double corrector_tol = 1e-10;    // Newton convergence tolerance
    int max_steps = 50000;           // Fail if path too long
    double target_t = 1.0;           // Target parameter value
};

struct HomotopySystem {
    std::function<Vec(const Vec& w)> H;    // Homotopy H(w) where w = (z, t)
    std::function<Mat(const Vec& w)> DH;   // Jacobian DH/Dw
    std::size_t n;                         // Dimension of z (n equations, n+1 unknowns)
};

struct PathResult {
    Vec w_final;              // Final point on path
    double t_final;           // Final parameter value
    bool reached_target;      // Whether reached target_t
    int total_steps;          // Path steps taken
    int total_corrections;    // Total Newton corrector iterations
    std::string failure_reason;
};

// Follow 1D solution curve from w0
PathResult follow_path(const HomotopySystem& sys,
                       const Vec& w0,
                       const PathFollowerConfig& cfg = {});
```

---

## Integration Patterns

### Pattern 1: Single Call to Solver

**Goal**: Solve game once, get result.

```cpp
#include "nash/core/game.hpp"
#include "nash/algorithms/lemke_howson.hpp"

BimatrixGame game(A, B);
LemkeHowson solver;
auto result = solver.solve(game);

if (result.status == nash::SolverStatus::Converged) {
    std::cout << "Success! Strategy: ["
              << result.profile.strategy_1[0] << ", "
              << result.profile.strategy_1[1] << "]" << std::endl;
} else {
    std::cout << "Failed: " << nash::to_string(result.status) << std::endl;
}
```

### Pattern 2: Try Multiple Algorithms

**Goal**: Use fallback chain (try fast, then robust, then exhaustive).

```cpp
EquilibriumResult best_result;
SolverStatus best_status = SolverStatus::NotRun;

// Try 1: Quick Lemke-Howson
{
    LemkeHowson lh;
    best_result = lh.solve(game);
    best_status = best_result.status;
}

// If failed, try robust Homotopy
if (best_status != SolverStatus::Converged) {
    HomotopySolver hs;
    best_result = hs.solve(game);
    best_status = best_result.status;
}

// If still failed, exhaustive search
if (best_status != SolverStatus::Converged && game.num_actions_1() <= 12) {
    SupportEnumeration se;
    auto multi = se.solve_all(game);
    if (!multi.equilibria.empty()) {
        best_result = multi.equilibria[0];
        best_status = multi.status;
    }
}
```

### Pattern 3: Find All Equilibria

**Goal**: Enumerate all or many equilibria.

```cpp
// Strategy 1: Support enumeration (complete, small games only)
if (game.num_actions_1() <= 15 && game.num_actions_2() <= 15) {
    SupportEnumeration se(tol);
    auto result = se.solve_all(game);
    std::cout << "Found " << result.equilibria.size() << " equilibria" << std::endl;
}

// Strategy 2: Lemke-Howson with all label drops (partial enumeration)
else {
    LemkeHowson lh_base;
    auto result = lh_base.solve_multiple(game);
    std::cout << "Found " << result.equilibria.size() << " equilibria" << std::endl;
}
```

### Pattern 4: Custom Numerical Configuration

**Goal**: Tune tolerances for specific problem.

```cpp
// Loose tolerances (faster, less accurate)
nash::Tolerances loose;
loose.eps = 1e-6;
loose.pivot_tol = 1e-10;
loose.conv_tol = 1e-6;
loose.max_iter = 1000;

// Tight tolerances (slower, high accuracy)
nash::Tolerances tight;
tight.eps = 1e-12;
tight.pivot_tol = 1e-14;
tight.conv_tol = 1e-11;
tight.max_iter = 50000;

// Use with solver
SupportEnumeration solver(tight);
auto result = solver.solve_all(game);
```

### Pattern 5: Homotopy with Custom Prior

**Goal**: Provide market/domain knowledge about opponent's strategy.

```cpp
// We believe player 1 plays first action 80% of the time
Vec prior_1 = {0.8, 0.2};

// We have no prior about player 2 (uniform)
Vec prior_2 = {};  // Empty = uniform

HomotopySolver solver(prior_1, prior_2);
auto result = solver.solve(game);

// Solution represents the path deforming from this prior to true NE
```

---

## Common Workflows

### Workflow 1: Solve & Verify

```cpp
BimatrixGame game(A, B);
LemkeHowson solver;
auto result = solver.solve(game);

// Compute Nash residual (regret from deviating)
double residual = nash_residual(game,
                                 result.profile.strategy_1,
                                 result.profile.strategy_2);

// Verify is valid equilibrium
bool is_ne = verify_equilibrium(game, result.profile, 1e-9);

std::cout << "Residual: " << residual << std::endl
          << "Is NE: " << (is_ne ? "yes" : "no") << std::endl;
```

### Workflow 2: Compare Algorithms

```cpp
BimatrixGame game(A, B);

auto results = std::map<std::string, EquilibriumResult>();

// Same game, three solvers
{
    SupportEnumeration se;
    auto multi = se.solve_all(game);
    if (!multi.equilibria.empty()) {
        results["Support Enum"] = multi.equilibria[0];
    }
}

{
    LemkeHowson lh;
    results["Lemke-Howson"] = lh.solve(game);
}

{
    HomotopySolver hs;
    results["Homotopy"] = hs.solve(game);
}

// Compare strategies
for (const auto& [solver, eq] : results) {
    std::cout << solver << ": "
              << eq.profile.strategy_1[0] << ", "
              << eq.profile.strategy_1[1] << std::endl;
}
```

### Workflow 3: Parametric Study

**Goal**: Solve games for varying parameter, plot equilibrium path.

```cpp
std::vector<double> param_values = {0.1, 0.2, ..., 1.0};
std::vector<EquilibriumResult> equilibria;

for (double p : param_values) {
    // Modify game by parameter
    Mat A_p = scale_payoff(A, p);
    Mat B_p = scale_payoff(B, p);
    BimatrixGame game_p(A_p, B_p);

    // Solve
    HomotopySolver solver;
    auto eq = solver.solve(game_p);
    equilibria.push_back(eq);
}

// Plot x1(p), x2(p) to see "equilibrium path" with parameter
for (const auto& eq : equilibria) {
    std::cout << eq.profile.strategy_1[0] << ", "
              << eq.profile.strategy_2[0] << std::endl;
}
```

### Workflow 4: Batch Processing

```cpp
std::vector<BimatrixGame> games = {game1, game2, ..., gameN};
std::vector<EquilibriumResult> results;
results.reserve(games.size());

for (const auto& game : games) {
    LemkeHowson solver;
    auto eq = solver.solve(game);
    results.push_back(eq);
}

// Summary statistics
int converged = 0;
for (const auto& eq : results) {
    if (eq.status == SolverStatus::Converged) converged++;
}
std::cout << "Solved " << converged << " / " << games.size() << std::endl;
```

---

## Adding Custom Solvers

### Step-by-Step: Implement "My New Solver"

#### 1. Create Header

`include/nash/algorithms/my_algorithm.hpp`:

```cpp
#pragma once
#include "nash/algorithms/solver_interface.hpp"

namespace nash {

class MyAlgorithm : public IEquilibriumSolver {
public:
    explicit MyAlgorithm(Tolerances tol = kDefaultTol) : tol_(tol) {}

    EquilibriumResult solve(const BimatrixGame& game) override;
    const char* name() const override { return "MyAlgorithm"; }

private:
    Tolerances tol_;
    // ... private helper methods
};

}
```

#### 2. Implement Core Logic

`src/algorithms/my_algorithm.cpp`:

```cpp
#include "nash/algorithms/my_algorithm.hpp"
#include "nash/numerics/linear_algebra.hpp"

namespace nash {

EquilibriumResult MyAlgorithm::solve(const BimatrixGame& game) {
    // 1. Validate input
    if (game.num_actions_1() == 0 || game.num_actions_2() == 0) {
        return {StrategyProfile(), 0, 0, SolverStatus::Infeasible, name()};
    }

    // 2. Run algorithm
    StrategyProfile profile;
    int iterations = 0;

    try {
        // Your algorithm core here
        profile = solve_internal(game, iterations);
    } catch (const std::exception& e) {
        return {StrategyProfile(), 0, 0, SolverStatus::NumericalFailure, name()};
    }

    // 3. Compute residual
    double residual = nash_residual(game, profile.strategy_1, profile.strategy_2);

    // 4. Verify equilibrium
    bool is_valid = verify_equilibrium(game, profile, tol_.conv_tol);
    if (!is_valid) {
        return {profile, residual, iterations, SolverStatus::NumericalFailure, name()};
    }

    // 5. Compute payoffs
    double payoff_1 = game.expected_payoff_1(profile.strategy_1, profile.strategy_2);
    double payoff_2 = game.expected_payoff_2(profile.strategy_1, profile.strategy_2);

    return {profile, residual, iterations, SolverStatus::Converged, name(),
            payoff_1, payoff_2};
}

StrategyProfile MyAlgorithm::solve_internal(const BimatrixGame& game, int& iterations) {
    // Your core algorithm implementation
    std::size_t m = game.num_actions_1();
    std::size_t n = game.num_actions_2();

    // Example: uniform mixture (replace with real algorithm!)
    StrategyProfile result;
    result.strategy_1 = Vec(m, 1.0 / m);
    result.strategy_2 = Vec(n, 1.0 / n);
    iterations = 0;

    return result;
}

}
```

#### 3. Update Build Configuration

In `CMakeLists.txt`:

```cmake
target_sources(nash_lib PRIVATE
    # ... existing sources ...
    src/algorithms/my_algorithm.cpp
)
```

#### 4. Test

Create `tests/test_my_algorithm.cpp`:

```cpp
#include "nash/algorithms/my_algorithm.hpp"
#include "nash/core/game.hpp"

void test_my_algorithm_prisoners_dilemma() {
    Mat A(2, 2), B(2, 2);
    A(0, 0) = 3; A(0, 1) = 0;
    A(1, 0) = 5; A(1, 1) = 1;
    B(0, 0) = 3; B(0, 1) = 5;
    B(1, 0) = 0; B(1, 1) = 1;

    BimatrixGame game(A, B);
    game.name = "Prisoner's Dilemma";

    MyAlgorithm solver;
    auto result = solver.solve(game);

    // Verify convergence
    assert(result.status == nash::SolverStatus::Converged);
    assert(result.residual < 1e-9);

    // Verify correct equilibrium (should find pure (D, D) = (1, 0), (1, 0))
    assert(apex_eq(result.profile.strategy_1[1], 1.0, 1e-7));    // P1 plays Defect
    assert(approx_eq(result.profile.strategy_2[1], 1.0, 1e-7));  // P2 plays Defect
}
```

#### 5. Register in Demo

In `src/main.cpp`:

```cpp
#include "nash/algorithms/my_algorithm.hpp"

// In solve_and_print():
std::cout << "── MyAlgorithm ──" << std::endl;
timed("time", [&]() {
    MyAlgorithm ma;
    auto result = ma.solve(game);
    result.print();
});
```

#### 6. Compile & Test

```bash
cd build
cmake ..
make
./nash_tests
```

---

## Error Handling

### Status Codes

Always check `result.status` after solving:

```cpp
auto result = solver.solve(game);

switch (result.status) {
    case SolverStatus::Converged:
        // Success! Use result.profile
        break;

    case SolverStatus::MaxIterations:
        // Iteration limit reached; may be close to solution
        // Try increasing tol.max_iter
        break;

    case SolverStatus::NumericalFailure:
        // Singular matrix or ill-conditioning
        // Try different algorithm or adjust tolerances
        break;

    case SolverStatus::Infeasible:
        // No solution for given support (Support Enum only)
        // Continue to next support
        break;

    case SolverStatus::NotRun:
        // Never executed
        break;
}
```

### Exception Safety

The Nash library does **not** throw exceptions in normal operation. Errors are reported via `SolverStatus` enum.

In rare cases, `std::bad_alloc` may be thrown on memory exhaustion (caught from `std::vector`).

### Validation Patterns

Always verify equilibrium quality after solving:

```cpp
// 1. Check convergence
if (result.status != SolverStatus::Converged) {
    std::cerr << "Solver failed: " << to_string(result.status) << std::endl;
    return;
}

// 2. Check residual
if (result.residual > 1e-6) {
    std::cerr << "Large residual: " << result.residual << std::endl;
    // May accept anyway, depending on application
}

// 3. Verify valid probabilities
if (!result.profile.is_valid(1e-8)) {
    std::cerr << "Invalid probability distribution" << std::endl;
    return;
}

// 4. Double-check via verify_equilibrium()
if (!verify_equilibrium(game, result.profile, 1e-9)) {
    std::cerr << "Failed equilibrium verification" << std::endl;
    return;
}

// All good!
use_equilibrium(result);
```

---

## Performance Tips

### 1. Choose Algorithm by Game Size

```cpp
EquilibriumResult solve_automatically(const BimatrixGame& game) {
    auto m = game.num_actions_1();
    auto n = game.num_actions_2();

    if (m <= 12 && n <= 12) {
        // Small: use exhaustive search
        return SupportEnumeration().solve_one(game);
    } else if (m <= 50 && n <= 50) {
        // Medium: use LH
        return LemkeHowson().solve(game);
    } else {
        // Large: use robust homotopy
        return HomotopySolver().solve(game);
    }
}
```

### 2. Precompute Game Payoffs Once

```cpp
// BAD: Recomputes expected payoff repeatedly
for (const auto& x : strategies) {
    double u = game.expected_payoff_1(x, y_fixed);  // Recomputes each time
}

// GOOD: Cache payoff once
Vec payoffs = linalg::matvec(game.payoff_1(), y_fixed);  // Compute once
for (const auto& x : strategies) {
    double u = linalg::dot(x, payoffs);  // Just dot product
}
```

### 3. Avoid Repeated LU Factorizations

```cpp
// BAD: LU done inside loop
for (...) {
    auto sol = linalg::solve(M, b);  // New LU decomposition each time
}

// GOOD: Factor once, solve multiple times
auto lu = linalg::lu_decompose(M);
for (...) {
    auto sol = linalg::lu_solve(lu, b);  // Reuse factorization
}
```

### 4. Tune Tolerances for Tradeoff

```cpp
// Prioritize speed
nash::Tolerances fast_tol;
fast_tol.eps = 1e-6;
fast_tol.conv_tol = 1e-5;
fast_tol.max_iter = 100;

SupportEnumeration solver(fast_tol);
```

### 5. Skip Expensive Verification in Loops

```cpp
// In large loop, skip verification (trust convergence flag)
for (const auto& game : games) {
    auto result = solver.solve(game);
    if (result.status != SolverStatus::Converged) {
        // Handle error
        continue;
    }
    // Skip verify_equilibrium() here; trust residual < threshold
    use_equilibrium(result);
}

// After loop, spot-check a few
for (int i = 0; i < 3; ++i) {
    verify_equilibrium(games[i], results[i].profile, 1e-9);
}
```

---

## FAQ

**Q: Why is my result showing very small but negative probabilities?**

A: Small numerical errors can produce values like -1e-16. These are clamped internally. If you see larger negatives, something is wrong.

**Q: Can I provide an analytical Jacobian to Newton solver?**

A: Yes! In `NonlinearSystem`, set the `J` lambda to your analytical Jacobian. Much faster than numerical differentiation.

**Q: How do I get the payoffs for a strategy profile?**

A:
```cpp
double u1 = game.expected_payoff_1(result.profile.strategy_1, result.profile.strategy_2);
double u2 = game.expected_payoff_2(result.profile.strategy_1, result.profile.strategy_2);
std::cout << "Payoffs: (" << u1 << ", " << u2 << ")" << std::endl;
```

**Q: What if I need to find equilibria for multiple games efficiently?**

A:
```cpp
// Batch process without reallocating
for (const auto& game : games) {
    auto result = solver.solve(game);
    process(result);
}
```

**Q: Can I use this library in a production system?**

A: Yes, but note:
- No external dependencies (good!)
- Double precision only (suitable for most games)
- No certification or formal proof (use for approximate solutions)
- Full test coverage on classic games (reliable)

**Q: What's the maximum game size I can solve?**

A:
- **Support Enumeration**: m, n ≤ 15 practical
- **Lemke-Howson**: m, n ≤ 50-100 depending on structure
- **Homotopy**: m, n ≤ 100+ depending on path length

For larger games, consider approximation algorithms or dimension reduction.

---

**End of Developer Guide**
