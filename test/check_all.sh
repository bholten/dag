#!/bin/bash
#
# Build Dagwood in both regular and AddressSanitizer flavors and run the
# full test suite (unit + integration) against each. The ASan run catches
# memory errors that the regular run never sees (see ISSUES.md #34, #48).
#
# Usage: ./test/check_all.sh
#
# Build directories: build/ and build-asan/ at the repo root. Both are
# re-configured idempotently each run.
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$REPO_ROOT"

run_flavor() {
    local label="$1"
    local build_dir="$2"
    shift 2

    echo
    echo "============================================================"
    echo "  $label"
    echo "============================================================"

    cmake -B "$build_dir" "$@" >/dev/null
    cmake --build "$build_dir" -j"$(nproc 2>/dev/null || echo 4)" >/dev/null
    (cd "$build_dir" && ctest --output-on-failure)
}

run_flavor "Regular build" build
run_flavor "AddressSanitizer build" build-asan -DDAGWOOD_ENABLE_ASAN=ON

echo
echo "All flavors passed."
