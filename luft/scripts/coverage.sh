#!/usr/bin/env bash
# ──────────────────────────────────────────────
# luft coverage report generator
# Requires: gcov, lcov, genhtml (from lcov package)
# Usage: ./scripts/coverage.sh
# ──────────────────────────────────────────────
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build-coverage"

echo "=== luft coverage report ==="
echo "Project: ${PROJECT_DIR}"
echo ""

# Clean build with coverage
echo "[1/5] Configuring with coverage..."
cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DLUFT_COVERAGE=ON \
    -DLUFT_BUILD_UI=OFF \
    -DLUFT_BUILD_TESTS=ON

echo "[2/5] Building..."
cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "[3/5] Running tests..."
cd "${BUILD_DIR}"
ctest --output-on-failure || true

echo "[4/5] Collecting coverage data..."
# Reset counters
# --ignore-errors mismatch is only available in lcov >= 2.0
LCOV_IGNORE_MISMATCH=""
if lcov --help 2>&1 | grep -q "mismatch"; then
    LCOV_IGNORE_MISMATCH="--ignore-errors mismatch"
fi

lcov --capture --directory "${BUILD_DIR}" \
     --output-file "${BUILD_DIR}/coverage_raw.info" \
     --rc lcov_branch_coverage=1 \
     ${LCOV_IGNORE_MISMATCH} \
     --quiet

# Filter out test files, googletest, and system headers
lcov --remove "${BUILD_DIR}/coverage_raw.info" \
     '*/tests/*' \
     '*/googletest/*' \
     '*/_deps/*' \
     '/usr/*' \
     '*/gtest/*' \
     --output-file "${BUILD_DIR}/coverage.info" \
     --rc lcov_branch_coverage=1 \
     --quiet

echo "[5/5] Generating HTML report..."
genhtml "${BUILD_DIR}/coverage.info" \
    --output-directory "${BUILD_DIR}/coverage_html" \
    --branch-coverage \
    --title "luft coverage" \
    --legend \
    --quiet

echo ""
echo "=== Coverage summary ==="
lcov --summary "${BUILD_DIR}/coverage.info" --rc lcov_branch_coverage=1 2>&1 | tail -4
echo ""
echo "HTML report: ${BUILD_DIR}/coverage_html/index.html"
echo "Open with: xdg-open ${BUILD_DIR}/coverage_html/index.html"
