#!/usr/bin/env bash
# run_coverage.sh -- Build with coverage, run tests, generate HTML report.
#
# Prerequisites: gcov, lcov, genhtml
# Usage: ./scripts/run_coverage.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build_coverage"

echo "=== SOR Code Coverage ==="
echo ""

# ── 1. Check prerequisites ──────────────────────────────────────────────────
for tool in gcov lcov genhtml cmake; do
    if ! command -v "$tool" &>/dev/null; then
        echo "ERROR: $tool not found. Please install it first."
        exit 1
    fi
done

# ── 2. Clean old coverage data ──────────────────────────────────────────────
echo "[1/6] Cleaning old coverage data..."
rm -rf "$BUILD_DIR"

# ── 3. Configure with coverage enabled ──────────────────────────────────────
echo "[2/6] Configuring build with coverage enabled..."
cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_COVERAGE=ON \
    -DSOR_BUILD_TESTS=ON \
    -S "$PROJECT_DIR" \
    2>&1 | tail -5

# ── 4. Build ────────────────────────────────────────────────────────────────
echo "[3/6] Building..."
cmake --build "$BUILD_DIR" -j"$(nproc)" 2>&1 | tail -5

# ── 5. Run tests ────────────────────────────────────────────────────────────
echo "[4/6] Running tests..."
cd "$BUILD_DIR"

# Zero existing counters
lcov --directory . --zerocounters --quiet

# Run unit tests
echo "  Running unit tests..."
./tests/unit/sor_unit_tests --reporter compact 2>&1 | tail -3

# Run integration tests
echo "  Running integration tests..."
./tests/integration/sor_integration_tests --reporter compact 2>&1 | tail -3

# ── 6. Collect coverage ────────────────────────────────────────────────────
echo "[5/6] Collecting coverage data..."
lcov --directory . --capture --output-file coverage.info --quiet

# Filter out third-party, system headers, and test code
lcov --remove coverage.info \
    "*/third_party/*" \
    "*/build_coverage/_deps/*" \
    "/usr/*" \
    "*/tests/*" \
    "*/test_*" \
    --output-file coverage_filtered.info --quiet

# ── 7. Generate HTML report ────────────────────────────────────────────────
echo "[6/6] Generating HTML report..."
mkdir -p coverage
genhtml coverage_filtered.info \
    --output-directory coverage \
    --title "SOR Code Coverage" \
    --legend --branch-coverage \
    --quiet

# ── Summary ─────────────────────────────────────────────────────────────────
echo ""
echo "=== Coverage Summary ==="
lcov --summary coverage_filtered.info 2>&1 | grep -E "lines|functions|branches"
echo ""
echo "HTML report: $BUILD_DIR/coverage/index.html"
echo "Open with:   xdg-open $BUILD_DIR/coverage/index.html"
