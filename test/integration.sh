#!/bin/bash
#
# Dagwood Integration Test Suite
#
# Run from repository root:
#   ./test/integration.sh
#
# Or specify a custom binary:
#   DAGWOOD=./build/dagwood ./test/integration.sh
#

set -euo pipefail

# ============================================================================
# Configuration
# ============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DAGWOOD="${DAGWOOD:-$REPO_ROOT/build/dag}"
TEST_PROJECTS="$SCRIPT_DIR/test-projects"

# Test counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

# Colors (disabled if not a terminal)
if [[ -t 1 ]]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[0;33m'
    BOLD='\033[1m'
    RESET='\033[0m'
else
    RED='' GREEN='' YELLOW='' BOLD='' RESET=''
fi

# ============================================================================
# Test Framework
# ============================================================================

log_pass() {
    echo -e "  ${GREEN}PASS${RESET} $1"
}

log_fail() {
    echo -e "  ${RED}FAIL${RESET} $1"
}

log_section() {
    echo -e "\n${BOLD}$1${RESET}"
}

# Run a test case
# Usage: run_test "description" command [args...]
run_test() {
    local desc="$1"
    shift

    TESTS_RUN=$((TESTS_RUN + 1))

    if "$@" >/dev/null 2>&1; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "$desc"
        return 0
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "$desc"
        return 1
    fi
}

# Run a test that should fail (exit non-zero)
# Usage: run_test_fails "description" command [args...]
run_test_fails() {
    local desc="$1"
    shift

    TESTS_RUN=$((TESTS_RUN + 1))

    if "$@" >/dev/null 2>&1; then
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "$desc (expected failure, got success)"
        return 1
    else
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "$desc"
        return 0
    fi
}

# Check that command output contains a string
# Usage: run_test_output_contains "description" "expected" command [args...]
run_test_output_contains() {
    local desc="$1"
    local expected="$2"
    shift 2

    TESTS_RUN=$((TESTS_RUN + 1))

    local output

    if output=$("$@" 2>&1) && echo "$output" | grep -qF -- "$expected"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "$desc"
        return 0
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "$desc (expected '$expected' in output)"
        return 1
    fi
}

# Check that command output contains a string (regardless of exit code)
# Usage: run_test_output_contains_any_exit "description" "expected" command [args...]
run_test_output_contains_any_exit() {
    local desc="$1"
    local expected="$2"
    shift 2

    TESTS_RUN=$((TESTS_RUN + 1))

    local output
    output=$("$@" 2>&1) || true

    if echo "$output" | grep -qF -- "$expected"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "$desc"
        return 0
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "$desc (expected '$expected' in output)"
        return 1
    fi
}

# Check that command output matches exactly
# Usage: run_test_output_exact "description" "expected" command [args...]
run_test_output_exact() {
    local desc="$1"
    local expected="$2"
    shift 2

    TESTS_RUN=$((TESTS_RUN + 1))

    local output
    output=$("$@" 2>&1) || true

    if [[ "$output" == "$expected" ]]; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "$desc"
        return 0
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "$desc"
        echo "    expected: $expected"
        echo "    got:      $output"
        return 1
    fi
}

# ============================================================================
# Pre-flight Checks
# ============================================================================

preflight() {
    log_section "Pre-flight checks"

    if [[ ! -x "$DAGWOOD" ]]; then
        echo -e "${RED}ERROR${RESET}: dagwood binary not found at $DAGWOOD"
        echo "Build with: cmake --build build"
        exit 1
    fi

    if [[ ! -d "$TEST_PROJECTS" ]]; then
        echo -e "${RED}ERROR${RESET}: test-projects directory not found at $TEST_PROJECTS"
        exit 1
    fi

    echo "  Binary: $DAGWOOD"
    echo "  Test projects: $TEST_PROJECTS"
}

# ============================================================================
# CLI Tests
# ============================================================================

test_cli_options() {
    log_section "CLI Options"

    # Help
    run_test_output_contains "-h shows usage" "Usage:" "$DAGWOOD" -h
    run_test_output_contains "--help shows usage" "Usage:" "$DAGWOOD" --help

    # Version
    run_test_output_contains "-v shows version" "0.1.0" "$DAGWOOD" -v
    run_test_output_contains "--version shows version" "0.1.0" "$DAGWOOD" --version

    # Unknown option should fail
    run_test_fails "--invalid-option fails" "$DAGWOOD" --invalid-option
}

test_cli_list() {
    log_section "CLI List Tasks"

    local proj="$TEST_PROJECTS/simple-project"

    run_test_output_contains "-l lists projects" "Projects:" "$DAGWOOD" -f "$proj/Dag" -l
    run_test_output_contains "-l lists tasks" "Tasks:" "$DAGWOOD" -f "$proj/Dag" -l
    run_test_output_contains "-l shows step_1" "step_1" "$DAGWOOD" -f "$proj/Dag" -l
    run_test_output_contains "-l shows step_2" "step_2" "$DAGWOOD" -f "$proj/Dag" -l
    run_test_output_contains "-l shows commands" "Commands:" "$DAGWOOD" -f "$proj/Dag" -l
    run_test_output_contains "-l shows clean command" "clean" "$DAGWOOD" -f "$proj/Dag" -l
}

test_cli_dot() {
    log_section "CLI Dot Output"

    local proj="$TEST_PROJECTS/simple-project"

    run_test_output_contains "-d outputs digraph" "digraph" "$DAGWOOD" -f "$proj/Dag" -d
    run_test_output_contains "-d shows task nodes" "step_1" "$DAGWOOD" -f "$proj/Dag" -d
    run_test_output_contains "-d shows edges" "->" "$DAGWOOD" -f "$proj/Dag" -d
}

test_cli_dry_run() {
    log_section "CLI Dry Run"

    local proj="$TEST_PROJECTS/simple-project"

    run_test_output_contains "-y shows plan" "Layer" "$DAGWOOD" -f "$proj/Dag" -y
    run_test_output_contains "-y shows tasks" "Task Name" "$DAGWOOD" -f "$proj/Dag" -y
}

# ============================================================================
# Task Execution Tests
# ============================================================================

test_task_execution() {
    log_section "Task Execution"

    local proj="$TEST_PROJECTS/simple-project"

    # Clean up any previous test artifacts
    rm -rf "$proj/s1" "$proj/s2"

    # Run the full project (use -C to change to project directory)
    run_test "run full project" "$DAGWOOD" -C "$proj"

    # Check outputs were created
    run_test "step_1 created s1/build/1.out" test -f "$proj/s1/build/1.out"
    run_test "step_1 created s1/build/2.out" test -f "$proj/s1/build/2.out"
    run_test "step_1 created s1/build/3.out" test -f "$proj/s1/build/3.out"
    run_test "step_2 created s2/build/1.out" test -f "$proj/s2/build/1.out"
}

test_output_capture() {
    log_section "Output Capture & Prefix (#45 Phase 2)"

    local proj="$TEST_PROJECTS/output-capture-test"

    # Plain stdout: every line gets a [task] prefix, in order.
    local out
    out=$("$DAGWOOD" -C "$proj" "capture::plain_stdout" 2>/dev/null)
    run_test_output_exact "plain stdout lines are prefixed in order" \
        "[capture::plain_stdout] first
[capture::plain_stdout] second
[capture::plain_stdout] third" \
        bash -c "'$DAGWOOD' -C '$proj' capture::plain_stdout 2>/dev/null"

    # Plain stderr: lines go to parent stderr with prefix, NOT stdout.
    TESTS_RUN=$((TESTS_RUN + 1))
    local stderr_only
    stderr_only=$("$DAGWOOD" -C "$proj" capture::plain_stderr 2>&1 1>/dev/null)
    if echo "$stderr_only" | grep -qF "[capture::plain_stderr] err 1" && \
       echo "$stderr_only" | grep -qF "[capture::plain_stderr] err 2"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "stderr lines reach parent stderr with task prefix"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "stderr lines missing or wrong channel"
    fi

    TESTS_RUN=$((TESTS_RUN + 1))
    local stdout_only
    stdout_only=$("$DAGWOOD" -C "$proj" capture::plain_stderr 2>/dev/null)
    if [[ -z "$stdout_only" ]]; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "stderr-only task produces no stdout"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "stderr-only task leaked to stdout: $stdout_only"
    fi

    # Partial trailing line is flushed at EOF (no data dropped).
    run_test_output_exact "trailing partial line flushed at EOF" \
        "[capture::no_trailing_newline] no_newline_here" \
        bash -c "'$DAGWOOD' -C '$proj' capture::no_trailing_newline 2>/dev/null"

    # Long line (>8 KB without newline): split silently into multiple
    # prefixed lines; concatenated payload preserves every byte.
    TESTS_RUN=$((TESTS_RUN + 1))
    local long_out
    long_out=$("$DAGWOOD" -C "$proj" capture::long_line 2>/dev/null)
    # Strip prefixes, concatenate, count x's.
    local x_count
    x_count=$(echo "$long_out" | sed 's/^\[[^]]*\] //' | tr -d '\n' | wc -c)
    if [[ "$x_count" == "10000" ]]; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "long line split silently, payload preserved (10000 bytes)"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "long line payload corrupted (got $x_count bytes, expected 10000)"
    fi

    # Long line should produce at least 2 prefixed lines (split).
    TESTS_RUN=$((TESTS_RUN + 1))
    local prefix_count
    prefix_count=$(echo "$long_out" | grep -c '^\[capture::long_line\]')
    if [[ "$prefix_count" -ge 2 ]]; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "long line produces multiple prefixed segments ($prefix_count)"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "long line should have split into >= 2 segments (got $prefix_count)"
    fi

    # Crasher: partial line flushed, then signal annotation on stderr.
    run_test_output_contains_any_exit "crash: pre-crash output reaches stdout" \
        "[capture::crasher] before crash" \
        bash -c "'$DAGWOOD' -C '$proj' capture::crasher 2>/dev/null"
    run_test_output_contains_any_exit "crash: signal annotation reported" \
        "terminated by signal" "$DAGWOOD" -C "$proj" capture::crasher
    run_test_fails "crash: task failure propagates" \
        "$DAGWOOD" -C "$proj" capture::crasher

    # Parallel layer: each sibling's lines remain attributable.
    # The project also contains capture::crasher which will fail, so
    # `|| true` lets us inspect the output regardless of exit code.
    TESTS_RUN=$((TESTS_RUN + 1))
    local par_out
    par_out=$("$DAGWOOD" -j 0 -C "$proj" 2>/dev/null || true)
    local a_lines b_lines c_lines
    a_lines=$(echo "$par_out" | grep -c '^\[capture::parallel_a\]')
    b_lines=$(echo "$par_out" | grep -c '^\[capture::parallel_b\]')
    c_lines=$(echo "$par_out" | grep -c '^\[capture::parallel_c\]')
    # Each task emits 3 lines; should all be present (interleaving allowed).
    if [[ "$a_lines" == "3" && "$b_lines" == "3" && "$c_lines" == "3" ]]; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "parallel layer: all sibling output attributed correctly"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "parallel layer counts wrong (a=$a_lines b=$b_lines c=$c_lines)"
    fi

    # -q suppresses child output but task still runs (exit 0, side effect).
    TESTS_RUN=$((TESTS_RUN + 1))
    local quiet_out
    quiet_out=$("$DAGWOOD" -q -C "$proj" capture::plain_stdout 2>/dev/null)
    if [[ -z "$quiet_out" ]]; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "-q suppresses child stdout"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "-q did not suppress stdout (got: $quiet_out)"
    fi

    TESTS_RUN=$((TESTS_RUN + 1))
    local quiet_err
    quiet_err=$("$DAGWOOD" -q -C "$proj" capture::plain_stderr 2>&1 1>/dev/null)
    # Look for lines that START with the prefix (i.e. captured child output),
    # not [dagwood] chatter which also mentions the task id mid-line.
    if echo "$quiet_err" | grep -qE '^\[capture::plain_stderr\]'; then
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "-q did not suppress child stderr"
    else
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "-q suppresses child stderr"
    fi

    # -q does NOT suppress [dagwood] chatter (orthogonal axis).
    TESTS_RUN=$((TESTS_RUN + 1))
    local quiet_chatter
    quiet_chatter=$("$DAGWOOD" -q -C "$proj" capture::plain_stdout 2>&1 1>/dev/null)
    if echo "$quiet_chatter" | grep -qF "[dagwood]"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "-q preserves [dagwood] chatter on stderr"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "-q wrongly suppressed [dagwood] chatter"
    fi
}

test_output_channels() {
    log_section "Output Channels (stdout/stderr split, #45)"

    local proj="$TEST_PROJECTS/simple-project"
    rm -rf "$proj/s1" "$proj/s2"

    # Run a task and capture stdout and stderr separately.
    local stdout_capture stderr_capture
    stdout_capture=$("$DAGWOOD" -C "$proj" simple_project::step_1 2>/dev/null)
    stderr_capture=$("$DAGWOOD" -C "$proj" simple_project::step_1 2>&1 1>/dev/null)

    # Dagwood's own chatter must not appear on stdout.
    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "$stdout_capture" | grep -qF "[dagwood]"; then
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "stdout should not contain [dagwood] chatter"
        echo "    got: $stdout_capture"
    else
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "stdout free of [dagwood] chatter during run"
    fi

    # The chatter must be on stderr.
    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "$stderr_capture" | grep -qF "[dagwood]"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "stderr carries [dagwood] chatter during run"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "stderr missing [dagwood] chatter"
    fi

    # Structured outputs must still go to stdout.
    run_test_output_contains "--dry-run stays on stdout" "Dagwood DAG Plan" \
        bash -c "'$DAGWOOD' -C '$proj' -y 2>/dev/null"
    run_test_output_contains "--dot stays on stdout" "digraph" \
        bash -c "'$DAGWOOD' -C '$proj' -d 2>/dev/null"
    run_test_output_contains "--list stays on stdout" "Tasks:" \
        bash -c "'$DAGWOOD' -C '$proj' -l 2>/dev/null"
    run_test_output_contains "--inspect stays on stdout" "step_1" \
        bash -c "'$DAGWOOD' -C '$proj' -i simple_project::step_1 2>/dev/null"
}

test_individual_task() {
    log_section "Individual Task Execution"

    local proj="$TEST_PROJECTS/simple-project"

    # Clean up
    rm -rf "$proj/s1" "$proj/s2"

    # Run just step_1 - should only run step_1, not dependents
    run_test "run individual task step_1" "$DAGWOOD" -C "$proj" "simple_project::step_1"

    # step_1 outputs should exist
    run_test "step_1 outputs exist" test -f "$proj/s1/build/1.out"

    # step_2 should NOT have run (not a dependency of step_1)
    run_test "step_2 did not run (not a dependency)" test ! -f "$proj/s2/build/1.out"
}

test_dependency_chain() {
    log_section "Dependency Chain"

    local proj="$TEST_PROJECTS/simple-project"

    # Clean up
    rm -rf "$proj/s1" "$proj/s2"

    # Run step_2 which depends on step_1
    run_test "run step_2 (depends on step_1)" "$DAGWOOD" -C "$proj" "simple_project::step_2"

    # Both should have run
    run_test "step_1 ran (dependency)" test -f "$proj/s1/build/1.out"
    run_test "step_2 ran" test -f "$proj/s2/build/1.out"
}

# ============================================================================
# Staleness Tests
# ============================================================================

test_staleness() {
    log_section "Staleness Detection"

    local proj="$TEST_PROJECTS/simple-project"

    # Clean and run step_1
    rm -rf "$proj/s1" "$proj/s2"
    "$DAGWOOD" -C "$proj" "simple_project::step_1" >/dev/null 2>&1

    # Get mtime of step_1's output
    local mtime_before
    mtime_before=$(stat -c %Y "$proj/s1/build/1.out" 2>/dev/null || stat -f %m "$proj/s1/build/1.out")

    # Run step_1 again - should NOT rebuild (outputs exist, no inputs to check)
    sleep 1
    "$DAGWOOD" -C "$proj" "simple_project::step_1" >/dev/null 2>&1

    local mtime_after
    mtime_after=$(stat -c %Y "$proj/s1/build/1.out" 2>/dev/null || stat -f %m "$proj/s1/build/1.out")

    TESTS_RUN=$((TESTS_RUN + 1))
    if [[ "$mtime_before" == "$mtime_after" ]]; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "task with outputs (no inputs) not rebuilt when outputs exist"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "task was unnecessarily rebuilt"
    fi

    # Now run step_2 to create its outputs
    "$DAGWOOD" -C "$proj" "simple_project::step_2" >/dev/null 2>&1

    mtime_before=$(stat -c %Y "$proj/s2/build/1.out" 2>/dev/null || stat -f %m "$proj/s2/build/1.out")

    # Touch an input to make step_2 stale
    sleep 1
    touch "$proj/s1/build/1.out"

    "$DAGWOOD" -C "$proj" "simple_project::step_2" >/dev/null 2>&1

    mtime_after=$(stat -c %Y "$proj/s2/build/1.out" 2>/dev/null || stat -f %m "$proj/s2/build/1.out")

    TESTS_RUN=$((TESTS_RUN + 1))
    if [[ "$mtime_before" != "$mtime_after" ]]; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "stale task rebuilt after input touched"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "stale task not rebuilt (mtime unchanged)"
    fi
}

# ============================================================================
# Error Handling Tests
# ============================================================================

test_error_handling() {
    log_section "Error Handling"

    # Non-existent Dagwood file
    run_test_fails "missing Dagwood file fails" "$DAGWOOD" -f "/nonexistent/Dagwood"

    # Non-existent task
    run_test_fails "non-existent task fails" "$DAGWOOD" -f "$TEST_PROJECTS/simple-project/Dagwood" "nonexistent::task"

    # Invalid directory
    run_test_fails "-C to invalid directory fails" "$DAGWOOD" -C "/nonexistent/dir"
}

# ============================================================================
# Command Tests
# ============================================================================

test_commands() {
    log_section "Commands"

    local proj="$TEST_PROJECTS/simple-project"

    # First, make sure outputs exist
    "$DAGWOOD" -C "$proj" >/dev/null 2>&1 || true

    # Run clean command
    run_test "clean command runs" "$DAGWOOD" -C "$proj" "simple_project::clean"

    # Verify outputs were cleaned (clean removes s1/build and s2/build, not s1 and s2)
    run_test "clean removed s1/build" test ! -d "$proj/s1/build"
    run_test "clean removed s2/build" test ! -d "$proj/s2/build"
}

# ============================================================================
# Namespace Feature Tests
# ============================================================================

test_namespaces() {
    log_section "Namespace Features"

    local proj="$TEST_PROJECTS/namespace-test"
    local output

    # Clean up from any previous runs
    rm -rf "$proj/build" "$proj/file1.txt" "$proj/file2.txt" "$proj/file3.txt"

    # --- Test 1: Let variable substitution ---
    run_test "let variables substitute in run script" \
        "$DAGWOOD" -C "$proj" "ns_test::test_let_vars"

    # Verify the output was created at the correct path
    TESTS_RUN=$((TESTS_RUN + 1))
    if [[ -f "$proj/build/output.txt" ]] && grep -q "let_vars_work" "$proj/build/output.txt"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "let variable path resolved correctly"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "let variable path not resolved (expected build/output.txt)"
    fi

    # --- Test 2: Let with list value ---
    run_test "let with list value works" \
        "$DAGWOOD" -C "$proj" "ns_test::test_let_list"

    # Check that all three files were created (list was properly expanded)
    TESTS_RUN=$((TESTS_RUN + 1))
    if [[ -f "$proj/file1.txt" ]] && [[ -f "$proj/file2.txt" ]] && [[ -f "$proj/file3.txt" ]]; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "list outputs expanded correctly"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "list outputs not expanded (expected file1.txt file2.txt file3.txt)"
    fi

    # --- Test 3: Task output reference in dry-run ---
    output=$("$DAGWOOD" -C "$proj" --dry-run 2>&1)

    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "$output" | grep -q "consumer" && echo "$output" | grep -A5 "consumer" | grep -q "build/produced.txt"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "task output reference works in inputs"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "task output reference not resolved in consumer inputs"
    fi

    # --- Test 4: Explicit depends-on ---
    run_test "explicit depends-on works" \
        "$DAGWOOD" -C "$proj" "ns_test::dependent_task"

    # --- Test 5: Multiple depends-on ---
    run_test "multiple depends-on works" \
        "$DAGWOOD" -C "$proj" "ns_test::multi_dep"

    # --- Test 6: Braced variable syntax ---
    output=$("$DAGWOOD" -C "$proj" "ns_test::braced_var" 2>&1)

    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "$output" | grep -q "Braced: build/subdir"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "braced variable syntax works"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "braced variable syntax failed"
    fi

    # --- Test 7: Mixed shell and LCL variables ---
    output=$("$DAGWOOD" -C "$proj" "ns_test::mixed_content" 2>&1)

    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "$output" | grep -q "LCL var: build"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "LCL variables substituted in mixed content"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "LCL variables not substituted in mixed content"
    fi

    # Shell special vars should be preserved for the shell to handle
    TESTS_RUN=$((TESTS_RUN + 1))
    # $$ will be the shell's PID (a number), so just check it runs
    if echo "$output" | grep -qE "Shell special: [0-9]+"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "shell special variables preserved for shell"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "shell special variables not preserved"
    fi

    # Guest ${...} without :: passes through and the shell expands it
    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "$output" | grep -q "Guest braced: /"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "guest \${HOME} passes through to the shell"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "guest \${HOME} was not left for the shell"
    fi

    # Bare qualified refs pass through verbatim (foreign namespaces)
    run_test_output_contains "bare foreign \$pkg::var passes through verbatim" \
        'Foreign: $some_pkg::value' \
        "$DAGWOOD" -C "$proj" "ns_test::mixed_content"

    # Bare refs matching a registered project pass through too...
    run_test_output_contains "bare project-prefixed ref passes through verbatim" \
        'Warned: $ns_test::BUILD_DIR' \
        "$DAGWOOD" -C "$proj" "ns_test::mixed_content"

    # ...but trigger the forgot-the-braces lint on stderr
    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "$output" | grep -q 'warning.*bare \$ns_test::BUILD_DIR'; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "bare project-prefixed ref triggers braces warning"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "no warning for bare project-prefixed ref"
    fi

    # \${ escape yields a literal ${...} in the delivered script
    run_test_output_contains "escaped \\\${...} yields literal text" \
        'Escaped: ${ns_test::BUILD_DIR}' \
        "$DAGWOOD" -C "$proj" "ns_test::mixed_content"

    # Test 8: Undefined variable handling - REMOVED
    # Undefined namespace vars are now a hard failure (Issue #26).
    # See test_undefined_var() for the hard-failure test.

    # --- Test 9: Task attribute reference - another task's run script ---
    output=$("$DAGWOOD" -C "$proj" "ns_test::reference_other_run" 2>&1)

    TESTS_RUN=$((TESTS_RUN + 1))
    # Check that both the label and the run script content appear (may be on different lines)
    if echo "$output" | grep -q "Other task run script:" && echo "$output" | grep -q "base_script_output"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "task attribute reference works (other task run)"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "task attribute reference failed (expected base_task::run content)"
    fi

    # --- Test 10: Task attribute reference - self-referencing outputs ---
    output=$("$DAGWOOD" -C "$proj" "ns_test::self_ref_outputs" 2>&1)

    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "$output" | grep -q "My outputs: build/self_ref.txt"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "self-referencing task outputs works"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "self-referencing task outputs failed"
    fi

    # --- Test 11: Task attribute reference - description ---
    output=$("$DAGWOOD" -C "$proj" "ns_test::reference_description" 2>&1)

    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "$output" | grep -q "Producer description: Producer task for output reference test"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "task description attribute reference works"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "task description attribute reference failed"
    fi

    # --- Test 12: Command attribute reference ---
    output=$("$DAGWOOD" -C "$proj" "ns_test::reference_command_run" 2>&1)

    TESTS_RUN=$((TESTS_RUN + 1))
    # Check that both the label and the rm -rf appear (may be on different lines)
    if echo "$output" | grep -q "Clean command run:" && echo "$output" | grep -q "rm -rf"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "command attribute reference works"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "command attribute reference failed"
    fi

    # --- Cleanup ---
    "$DAGWOOD" -C "$proj" "ns_test::clean" >/dev/null 2>&1 || true
}

# ============================================================================
# Working Directory Tests
# ============================================================================

test_chdir() {
    log_section "Working Directory (chdir)"

    local proj="$TEST_PROJECTS/chdir-test"

    # Clean up
    rm -f "$proj/cwd_output.txt"

    # Run the task that writes pwd to a file
    run_test "chdir task runs" "$DAGWOOD" -C "$proj" "chdir_test::check_cwd"

    # Verify the output file was created
    run_test "cwd_output.txt created" test -f "$proj/cwd_output.txt"

    # Verify the working directory in the output matches the project directory
    TESTS_RUN=$((TESTS_RUN + 1))
    local expected_dir
    expected_dir=$(cd "$proj" && pwd)
    local actual_dir
    actual_dir=$(cat "$proj/cwd_output.txt" | tr -d '[:space:]')

    if [[ "$actual_dir" == "$expected_dir" ]]; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "task ran in correct working directory"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "wrong working directory (expected '$expected_dir', got '$actual_dir')"
    fi

    # Clean up
    "$DAGWOOD" -C "$proj" "chdir_test::clean" >/dev/null 2>&1 || true
}

# ============================================================================
# Spawn Error Tests
# ============================================================================

test_spawn_error() {
    log_section "Task Failure Handling"

    local proj="$TEST_PROJECTS/error-test"

    # Task that exits non-zero should cause dagwood to fail
    run_test_fails "task with non-zero exit fails" "$DAGWOOD" -C "$proj" "error_test::will_fail"

    # Check that the error message mentions task failure
    run_test_output_contains_any_exit "task failure reported" "failed" "$DAGWOOD" -C "$proj" "error_test::will_fail"
}

# ============================================================================
# Cycle Detection Tests
# ============================================================================

test_cycle_detection() {
    log_section "Cycle Detection"

    local proj="$TEST_PROJECTS/cycle-test"

    # Running a project with circular dependencies should fail
    run_test_fails "cyclic project fails" "$DAGWOOD" -C "$proj"

    # Should mention cycle in error output
    run_test_output_contains_any_exit "cycle error reported" "cycle" "$DAGWOOD" -C "$proj"

    # Cycles should propagate through dry-run and dot output too (Issue #35)
    run_test_fails "cyclic project fails under --dry-run" "$DAGWOOD" -C "$proj" -y
    run_test_fails "cyclic project fails under --dot" "$DAGWOOD" -C "$proj" -d
}

# ============================================================================
# Undefined Namespace Variable Tests (Issue #26)
# ============================================================================

test_undefined_var() {
    log_section "Undefined Namespace Variable (hard failure)"

    local proj="$TEST_PROJECTS/undefined-var-test"

    # A misspelled variable in a splice should cause a hard failure
    run_test_fails "undefined splice var fails" "$DAGWOOD" -C "$proj"

    # Error message should mention the undefined splice
    run_test_output_contains_any_exit "error mentions undefined variable" \
        "undefined variable in splice" "$DAGWOOD" -C "$proj"

    # Error message should include the variable name
    run_test_output_contains_any_exit "error shows variable name" \
        "BUILD_DIER" "$DAGWOOD" -C "$proj"

    # A splice naming an unknown project also hard-fails, loudly, even
    # for --list (it's a load-time error), and the message points at
    # the escape.
    local splice_proj="$TEST_PROJECTS/bad-splice-test"

    run_test_fails "unknown-project splice fails at load" \
        "$DAGWOOD" -C "$splice_proj" -l

    run_test_output_contains_any_exit "error names the unknown project" \
        "unknown project 'HOME'" "$DAGWOOD" -C "$splice_proj" -l

    run_test_output_contains_any_exit "error suggests the escape" \
        'use \${...} for literal text' "$DAGWOOD" -C "$splice_proj" -l

    local bare_proj="$TEST_PROJECTS/bare-qualified-test"

    run_test_fails "bare \$proj::VAR in expression position fails at load" \
        "$DAGWOOD" -C "$bare_proj" -l

    run_test_output_contains_any_exit "bare-qualified error names the braced spelling" \
        'qualified substitutions require braces' "$DAGWOOD" -C "$bare_proj" -l
}

# ============================================================================
# Bad depends-on Tests (Issue #27)
# ============================================================================

test_bad_depends_on() {
    log_section "Unresolvable depends-on (hard failure)"

    local proj="$TEST_PROJECTS/bad-depends-test"

    # A depends-on referencing a non-existent task should cause a hard failure
    run_test_fails "unresolvable depends-on fails" "$DAGWOOD" -C "$proj"

    # Error message should mention the missing dependency
    run_test_output_contains_any_exit "error mentions missing dependency" \
        "does not exist" "$DAGWOOD" -C "$proj"

    # Error message should include the bad task name
    run_test_output_contains_any_exit "error shows missing task name" \
        "step_1_typo" "$DAGWOOD" -C "$proj"
}

# ============================================================================
# Run Subcommand Tests (Issue #24)
# ============================================================================

test_run_subcommand() {
    log_section "Run Subcommand"

    local proj="$TEST_PROJECTS/simple-project"

    # Clean up
    rm -rf "$proj/s1" "$proj/s2"

    # "dag run <task>" should work the same as "dag <task>"
    run_test "run subcommand executes task" \
        "$DAGWOOD" -C "$proj" run "simple_project::step_1"
    run_test "run subcommand created outputs" test -f "$proj/s1/build/1.out"

    # Clean up and test with --force flag
    rm -rf "$proj/s1" "$proj/s2"

    # Run step_1 to create outputs, then run again with --force to rebuild
    "$DAGWOOD" -C "$proj" run "simple_project::step_1" >/dev/null 2>&1
    local mtime_before
    mtime_before=$(stat -c %Y "$proj/s1/build/1.out" 2>/dev/null || stat -f %m "$proj/s1/build/1.out")

    sleep 1
    "$DAGWOOD" -C "$proj" --force run "simple_project::step_1" >/dev/null 2>&1
    local mtime_after
    mtime_after=$(stat -c %Y "$proj/s1/build/1.out" 2>/dev/null || stat -f %m "$proj/s1/build/1.out")

    TESTS_RUN=$((TESTS_RUN + 1))
    if [[ "$mtime_before" != "$mtime_after" ]]; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "--force rebuilds even when outputs exist"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "--force did not rebuild"
    fi

    # Also test --force with direct task (not via run subcommand)
    mtime_before=$(stat -c %Y "$proj/s1/build/1.out" 2>/dev/null || stat -f %m "$proj/s1/build/1.out")
    sleep 1
    "$DAGWOOD" -C "$proj" --force "simple_project::step_1" >/dev/null 2>&1
    mtime_after=$(stat -c %Y "$proj/s1/build/1.out" 2>/dev/null || stat -f %m "$proj/s1/build/1.out")

    TESTS_RUN=$((TESTS_RUN + 1))
    if [[ "$mtime_before" != "$mtime_after" ]]; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "--force works with direct task invocation"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "--force did not work with direct task invocation"
    fi

    # Clean up
    rm -rf "$proj/s1" "$proj/s2"
}

# ============================================================================
# Custom Shell Tests (Issue #21)
# ============================================================================

test_custom_shell() {
    log_section "Custom Shell Configuration"

    local proj="$TEST_PROJECTS/custom-shell-test"

    # Run a task that uses bash-specific syntax (BASH_VERSION variable)
    # If posix_spawn correctly uses the configured shell (/bin/bash),
    # BASH_VERSION will be non-empty. If it falls back to /bin/sh, it will be empty.
    local output
    output=$("$DAGWOOD" -C "$proj" "custom_shell_test::check_shell" 2>&1)

    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "$output" | grep -qE "SHELL_CHECK:.+"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "custom shell (/bin/bash) is actually used by posix_spawn"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "custom shell not used (BASH_VERSION empty, likely running /bin/sh)"
    fi

    # Task-level shell override: project shell is bash, task overrides
    # to /bin/echo, which prints the marker (its shell-arg) verbatim.
    # If the project shell ran the body instead, the marker never prints.
    run_test_output_contains "task-level shell override beats project shell" \
        "TASK_SHELL_MARKER" \
        "$DAGWOOD" -C "$proj" "custom_shell_test::task_shell_override"

    # Command-level shell override goes through the same path.
    run_test_output_contains "command-level shell override beats project shell" \
        "CMD_SHELL_MARKER" \
        "$DAGWOOD" -C "$proj" "custom_shell_test::command_shell_override"

    # Bare shell name (no path) must be resolved via PATH (posix_spawnp),
    # and it must actually be bash (BASH_VERSION non-empty).
    output=$("$DAGWOOD" -C "$proj" "custom_shell_test::bare_shell_name" 2>&1)

    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "$output" | grep -qE "BARE_SHELL_CHECK:.+"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "bare shell name resolved via PATH (posix_spawnp)"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "bare shell name not resolved via PATH"
    fi
}

# ============================================================================
# Lcl-as-Shell Tests (per-task `shell lcl`)
# ============================================================================

test_lcl_shell() {
    log_section "Lcl as Task Shell"

    local proj="$TEST_PROJECTS/lcl-shell-test"

    # This needs only the embedded interpreter (the Dag file itself uses
    # posix::glob), so it runs regardless of whether the lcl CLI exists.
    run_test "posix::glob usable in Dag file" \
        "$DAGWOOD" -C "$proj" -l

    if ! command -v lcl >/dev/null 2>&1; then
        echo -e "  ${YELLOW}SKIP${RESET} lcl CLI not on PATH; skipping shell-lcl execution tests"
        return 0
    fi

    rm -f "$proj/lcl_task.out"

    run_test_output_contains "task run body evaluated by lcl CLI" \
        "LCL_SHELL_OK" \
        "$DAGWOOD" -C "$proj" "lcl_shell_test::lcl_task"

    run_test "lcl run body wrote its output file" \
        test -f "$proj/lcl_task.out"

    run_test_output_contains "command run body uses posix::glob via lcl" \
        "LCL_GLOB_COUNT:2" \
        "$DAGWOOD" -C "$proj" "lcl_shell_test::lcl_command"

    # ${a::b} is both Dagwood's splice and Lcl's own qualified-ref
    # syntax; Dagwood claims it at load, \${ hands it to the guest lcl.
    run_test_output_contains "lcl run body: Dagwood splice resolved before lcl" \
        "SPLICED:SPLICE_OK" \
        "$DAGWOOD" -C "$proj" "lcl_shell_test::lcl_splice"

    run_test_output_contains "lcl run body: escaped \\\${a::b} resolved by guest lcl" \
        "GUEST:GUEST_OK" \
        "$DAGWOOD" -C "$proj" "lcl_shell_test::lcl_splice"

    rm -f "$proj/lcl_task.out"
}

# ============================================================================
# Multi-Project Tests (Issue #25)
# ============================================================================

test_multi_project() {
    log_section "Multi-Project Import"

    local proj="$TEST_PROJECTS/multi-project"

    # Clean up from any previous runs
    rm -rf "$proj/build" "$proj/project_1/task_1" "$proj/project_1/task_2" \
           "$proj/project_1/task_3" "$proj/project_2/build" "$proj/project_3/build"

    # List should show all imported projects
    run_test_output_contains "list shows imported project_1" "project_1" \
        "$DAGWOOD" -f "$proj/Dag" -C "$proj" -l
    run_test_output_contains "list shows imported project_2" "project_2" \
        "$DAGWOOD" -f "$proj/Dag" -C "$proj" -l
    run_test_output_contains "list shows imported project_3" "project_3" \
        "$DAGWOOD" -f "$proj/Dag" -C "$proj" -l
    run_test_output_contains "list shows main multiproject" "multiproject" \
        "$DAGWOOD" -f "$proj/Dag" -C "$proj" -l

    # Dry run should show cross-project dependencies
    run_test_output_contains "dry run shows cross-project tasks" "project_1::task_1" \
        "$DAGWOOD" -f "$proj/Dag" -C "$proj" -y

    # Run the full multi-project build
    run_test "full multi-project build succeeds" \
        "$DAGWOOD" -C "$proj"

    # Check that outputs from all projects were created
    run_test "project_1 task_1 output created" test -f "$proj/task_1/build/task_1.out"
    run_test "project_1 task_2 outputs created" test -f "$proj/task_2/build/task_2.out1"
    run_test "project_1 task_3 output created" test -f "$proj/task_3/build/task_3.out"
    run_test "project_2 output created" test -f "$proj/build/output.txt"
    run_test "project_3 combined output created" test -f "$proj/build/combined.txt"
    run_test "multiproject final output created" test -f "$proj/build/complete.txt"

    # Run an individual cross-project task
    rm -rf "$proj/build" "$proj/task_1" "$proj/task_2" "$proj/task_3"
    run_test "individual cross-project task runs" \
        "$DAGWOOD" -C "$proj" "project_1::task_2"
    run_test "cross-project dependency ran (task_1)" test -f "$proj/task_1/build/task_1.out"
    run_test "cross-project target ran (task_2)" test -f "$proj/task_2/build/task_2.out1"

    # Clean up
    rm -rf "$proj/build" "$proj/task_1" "$proj/task_2" "$proj/task_3"
}

test_import_tracking() {
    log_section "Import Tracking (\${dagwood::imports})"

    local proj="$TEST_PROJECTS/multi-project"
    local out

    # Each imported project should appear in ${dagwood::imports} as a
    # project_name -> path mapping (Phase A+B import macro fix).
    out=$(printf 'puts ${dagwood::imports}\nexit\n' \
          | "$DAGWOOD" -C "$proj" -r 2>/dev/null)

    for p in project_1 project_2 project_3; do
        TESTS_RUN=$((TESTS_RUN + 1))
        if echo "$out" | grep -qF "$p"; then
            TESTS_PASSED=$((TESTS_PASSED + 1))
            log_pass "import tracked in \${dagwood::imports}: $p"
        else
            TESTS_FAILED=$((TESTS_FAILED + 1))
            log_fail "$p missing from \${dagwood::imports}"
        fi
    done
}

# ============================================================================
# Args Tests (Issue #25)
# ============================================================================

test_args() {
    log_section "CLI Arguments (arg command)"

    local proj="$TEST_PROJECTS/args-test"

    # Default values should be used when no CLI args given
    run_test_output_contains "default arg values used" "BUILD_TYPE=debug" \
        "$DAGWOOD" -C "$proj" "args_test::show_args"
    run_test_output_contains "default CC value used" "CC=gcc" \
        "$DAGWOOD" -C "$proj" "args_test::show_args"

    # CLI args should override defaults
    run_test_output_contains "CLI arg overrides default" "BUILD_TYPE=release" \
        "$DAGWOOD" -C "$proj" BUILD_TYPE=release "args_test::show_args"
    run_test_output_contains "CLI arg overrides CC" "CC=clang" \
        "$DAGWOOD" -C "$proj" CC=clang "args_test::show_args"

    # Multiple CLI args at once
    local output
    output=$("$DAGWOOD" -C "$proj" BUILD_TYPE=release CC=clang "args_test::show_args" 2>&1)

    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "$output" | grep -q "BUILD_TYPE=release" && echo "$output" | grep -q "CC=clang"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "multiple CLI args override correctly"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "multiple CLI args override failed"
    fi
}

# ============================================================================
# Staleness Memo Collision Tests (Issue #22)
# ============================================================================

test_memo_collision() {
    log_section "Staleness Memo Key Collision"

    local proj="$TEST_PROJECTS/memo-collision-test"

    # Clean up
    rm -f "$proj/main_output.txt" "$proj/sub_output.txt"

    # Run the full project - both "build" tasks should execute
    run_test "memo collision: full build succeeds" \
        "$DAGWOOD" -C "$proj"

    # Both outputs should exist (both "build" tasks ran)
    run_test "memo collision: sub_project::build output exists" \
        test -f "$proj/sub_output.txt"
    run_test "memo collision: main_project::build output exists" \
        test -f "$proj/main_output.txt"

    # Now delete only main_output.txt (making main_project::build stale
    # but sub_project::build should remain clean)
    rm -f "$proj/main_output.txt"

    # Run again - main_project::build should be stale and re-run
    # If memo uses local name "build", sub_project::build's CLEAN state
    # will be cached and main_project::build will incorrectly be CLEAN too
    run_test "memo collision: rebuild after partial clean" \
        "$DAGWOOD" -C "$proj"

    run_test "memo collision: main_project::build re-created" \
        test -f "$proj/main_output.txt"

    # Clean up
    rm -f "$proj/main_output.txt" "$proj/sub_output.txt"
}

# ============================================================================
# REPL (Issue #46)
# ============================================================================

test_repl() {
    log_section "REPL (Issue #46)"

    local proj="$TEST_PROJECTS/simple-project"
    local args_proj="$TEST_PROJECTS/args-test"

    # REPL exits cleanly on "exit" input.
    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "exit" | "$DAGWOOD" -C "$proj" -r >/dev/null 2>&1; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "REPL exits cleanly on 'exit'"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "REPL did not exit cleanly"
    fi

    # REPL with a loaded Dag file can read project-scoped variables.
    run_test_output_contains "REPL evaluates loaded project variable" \
        "s1/build" \
        bash -c "printf 'puts \${simple_project::S1_BUILD_DIR}\nexit\n' | '$DAGWOOD' -C '$proj' -r 2>/dev/null"

    # REPL can introspect loaded tasks via task-get.
    run_test_output_contains "REPL can task-get on loaded project" \
        "description {Step 1 Test" \
        bash -c "printf 'puts [task-get simple_project::step_1]\nexit\n' | '$DAGWOOD' -C '$proj' -r 2>/dev/null"

    # CLI args reach the REPL.
    run_test_output_contains "REPL receives CLI arg override" \
        "release" \
        bash -c "printf 'puts \${args_test::BUILD_TYPE}\nexit\n' | '$DAGWOOD' -C '$args_proj' BUILD_TYPE=release -r 2>/dev/null"

    # Missing Dag file: REPL warns but still starts.
    local nodag_tmp
    nodag_tmp=$(mktemp -d)
    run_test_output_contains_any_exit "REPL warns when Dag file missing" \
        "starting without a loaded project" \
        bash -c "echo exit | '$DAGWOOD' -C '$nodag_tmp' -r"
    rm -rf "$nodag_tmp"
}

# ============================================================================
# Task Mutation
# ============================================================================

test_task_mutation() {
    log_section "Task Mutation"

    local proj="$TEST_PROJECTS/mutation-test"
    rm -rf "$proj/build"

    # task-override: mylib::test description should be replaced
    run_test_output_contains "task-override changes description" \
        "Overridden test" \
        "$DAGWOOD" -C "$proj" -i mylib::test

    # task-override: deploy should be gone (task-disable)
    local list_output
    list_output=$("$DAGWOOD" -C "$proj" -l 2>&1)

    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "$list_output" | grep -qF "mylib::deploy"; then
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "task-disable removes task from list"
    else
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "task-disable removes task from list"
    fi

    # task-extend: always-run should be set
    run_test_output_contains "task-extend sets always-run" \
        "always-run: true" \
        "$DAGWOOD" -C "$proj" -i mylib::build

    # task-extend on a list attribute: should concatenate, not overwrite.
    # mylib::package starts with depends-on (mylib::build mylib::test).
    # Main Dag does: task-extend mylib::package { depends-on mylib::build }
    # Expected: all three entries appear (Issue #37).
    local package_output
    package_output=$("$DAGWOOD" -C "$proj" -i mylib::package 2>&1)
    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "$package_output" | grep -qE "depends-on:.*mylib::build.*mylib::test.*mylib::build"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "task-extend concatenates list attributes (#37)"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "task-extend overwrote list instead of concatenating"
        echo "    got: $(echo "$package_output" | grep depends-on)"
    fi

    # project-override: description should be set
    run_test_output_contains "project-override changes project description" \
        "Mutated library" \
        "$DAGWOOD" -C "$proj" -l

    # Inspect all tasks
    run_test_output_contains "inspect shows all tasks" \
        "mutator::integrate" \
        "$DAGWOOD" -C "$proj" -i

    # Inspect specific task
    run_test_output_contains "inspect shows specific task details" \
        "description: Integration task" \
        "$DAGWOOD" -C "$proj" -i mutator::integrate

    # Inspect non-existent task fails
    run_test_fails "inspect non-existent task fails" \
        "$DAGWOOD" -C "$proj" -i nonexistent::task

    # End-to-end execution of mutated graph
    run_test_output_contains "mutated graph executes overridden task" \
        "mylib tested (overridden)" \
        "$DAGWOOD" -C "$proj"

    run_test_output_contains "mutated graph executes own task" \
        "integration done" \
        "$DAGWOOD" -C "$proj"

    # deploy should NOT have run (it was disabled)
    TESTS_RUN=$((TESTS_RUN + 1))
    if [[ -f "$proj/build/deploy.out" ]]; then
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "disabled task did not execute"
    else
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "disabled task did not execute"
    fi

    rm -rf "$proj/build"

    # task-extend must not leak its setter procs into the caller's
    # top-level scope: a stray bare `shell ...` after a task-extend
    # block must be a hard "unknown command" error, not a silent no-op.
    run_test_fails "task-extend does not leak setter procs" \
        "$DAGWOOD" -C "$TEST_PROJECTS/extend-leak-test" -l

    # task-extend refreshes the ${proj::task::*} introspection namespace
    # (mutation-test extends mylib::build with always-run true).
    run_test_output_contains "task-extend refreshes introspection namespace" \
        "true" \
        bash -c "printf 'puts \${mylib::build::always-run}\nexit\n' | '$DAGWOOD' -C '$proj' -r 2>/dev/null"
}

# ============================================================================
# Main
# ============================================================================

# ============================================================================
# Generated Tasks (programmatic task creation + ${self::...} splices)
# ============================================================================

# ============================================================================
# Job Scheduler Tests (-j)
# ============================================================================

test_jobs_scheduler() {
    log_section "Job Scheduler (-j)"

    local proj="$TEST_PROJECTS/jobs-test"
    rm -f "$proj/order.log" "$proj/left.flag" "$proj/right.flag"

    # -j 1: strictly serial — every start line is immediately followed
    # by its own end line.
    run_test "-j 1 executes the graph" "$DAGWOOD" -C "$proj" -j 1 jobs::all
    TESTS_RUN=$((TESTS_RUN + 1))
    local serial_ok=1
    local expect=""
    while IFS= read -r line; do
        case "$line" in
            start-*)
                [[ -n "$expect" ]] && serial_ok=0
                expect="end-${line#start-}"
                ;;
            end-*)
                [[ "$line" == "$expect" ]] || serial_ok=0
                expect=""
                ;;
            *) serial_ok=0 ;;
        esac
    done < "$proj/order.log"
    [[ -z "$expect" ]] || serial_ok=0
    if [[ "$serial_ok" == "1" && $(wc -l < "$proj/order.log") == "6" ]]; then
        log_pass "-j 1 runs tasks strictly serially"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_fail "-j 1 interleaved task execution ($(tr '\n' ' ' < "$proj/order.log"))"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi

    # Deterministic scheduling: two -j 1 runs produce identical order.
    local first_order
    first_order=$(cat "$proj/order.log")
    rm -f "$proj/order.log"
    "$DAGWOOD" -C "$proj" -j 1 jobs::all >/dev/null 2>&1 || true
    TESTS_RUN=$((TESTS_RUN + 1))
    if [[ "$(cat "$proj/order.log")" == "$first_order" ]]; then
        log_pass "-j 1 scheduling order is deterministic"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_fail "-j 1 order differed between runs"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    rm -f "$proj/order.log"

    # -j 2: rendezvous tasks require true concurrency to finish.
    run_test "-j 2 runs independent tasks concurrently" \
        "$DAGWOOD" -C "$proj" -j 2 jobs::both
    rm -f "$proj/left.flag" "$proj/right.flag"

    # -j 0: unbounded fan-out (the old default) still works.
    run_test "-j 0 (unbounded) executes the graph" \
        "$DAGWOOD" -C "$proj" -j 0 jobs::both
    rm -f "$proj/left.flag" "$proj/right.flag" "$proj/order.log"

    # Default is bounded to the CPU count, reported in the chatter.
    run_test_output_contains_any_exit "default -j is bounded (CPU count)" \
        "[dagwood] jobs: " \
        bash -c "'$DAGWOOD' -C '$proj' jobs::all 2>&1 | grep -v unbounded"
    rm -f "$proj/order.log"

    # Invalid -j values are rejected up front.
    run_test_fails "-j rejects negative values" "$DAGWOOD" -C "$proj" -j -1 jobs::all
    run_test_fails "-j rejects non-numeric values" "$DAGWOOD" -C "$proj" -j abc jobs::all
}

test_generated_tasks() {
    log_section "Generated Tasks"

    local proj="$TEST_PROJECTS/generated-tasks-test"
    rm -rf "$proj/site"

    # Tasks generated from an imported .dag/ helper module appear in the graph
    run_test_output_contains "module-generated tasks are listed" \
        "gen::alpha" \
        "$DAGWOOD" -C "$proj" --list
    run_test_output_contains "in-file generator tasks are listed" \
        "gen::copy-beta" \
        "$DAGWOOD" -C "$proj" --list

    # Task bodies close over the generator's loop variable
    run_test_output_contains "generated task has per-iteration inputs" \
        "inputs: docs/beta.txt" \
        "$DAGWOOD" -C "$proj" -i gen::beta

    # ${self::inputs} / ${self::outputs} resolve to the task's own attributes
    run_test_output_contains "self splice resolves to task attributes" \
        "cp docs/alpha.txt" \
        "$DAGWOOD" -C "$proj" -i gen::alpha

    # Generated tasks actually execute, and a task can depend on them
    run_test "generated tasks execute via dependent" \
        "$DAGWOOD" -C "$proj" gen::all
    if [ -f "$proj/site/alpha.html" ] && [ -f "$proj/site/beta.html" ]; then
        log_pass "generated tasks produced their outputs"
    else
        log_fail "generated tasks did not produce outputs"
    fi
    run_test "in-file generated task executes" \
        "$DAGWOOD" -C "$proj" gen::copy-alpha
    rm -rf "$proj/site"

    # Explicit ${proj::kebab-task::attr} splices and kebab introspection names
    run_test_output_contains "explicit kebab-segment splice resolves" \
        "copy-alpha reads docs/alpha.txt" \
        "$DAGWOOD" -C "$proj" gen::attrs
    run_test_output_contains "kebab introspection variable resolves" \
        "all always-run: true" \
        "$DAGWOOD" -C "$proj" gen::attrs

    # Undefined self attribute is a load-time error with a task-specific message
    run_test_fails "project named self is rejected" \
        "$DAGWOOD" -C "$TEST_PROJECTS/self-reserved-test" --list
    run_test_output_contains_any_exit "self reserved error names the reason" \
        "'self' is reserved" \
        "$DAGWOOD" -C "$TEST_PROJECTS/self-reserved-test" --list

    # `task` outside a project block errors clearly
    run_test_fails "task outside project fails" \
        "$DAGWOOD" -C "$TEST_PROJECTS/task-outside-project-test" --list
    run_test_output_contains_any_exit "task outside project error is specific" \
        "must be used inside a project block" \
        "$DAGWOOD" -C "$TEST_PROJECTS/task-outside-project-test" --list
}

main() {
    echo -e "${BOLD}Dagwood Integration Test Suite${RESET}"
    echo "================================"

    preflight

    test_cli_options
    test_cli_list
    test_cli_dot
    test_cli_dry_run
    test_task_execution
    test_output_channels
    test_output_capture
    test_individual_task
    test_dependency_chain
    test_staleness
    test_error_handling
    test_commands
    test_namespaces
    test_chdir
    test_spawn_error
    test_cycle_detection
    test_undefined_var
    test_bad_depends_on
    test_run_subcommand
    test_custom_shell
    test_lcl_shell
    test_multi_project
    test_import_tracking
    test_args
    test_memo_collision
    test_repl
    test_task_mutation
    test_generated_tasks
    test_jobs_scheduler

    # Summary
    log_section "Summary"
    echo -e "  Total:  $TESTS_RUN"
    echo -e "  Passed: ${GREEN}$TESTS_PASSED${RESET}"
    echo -e "  Failed: ${RED}$TESTS_FAILED${RESET}"

    if [[ $TESTS_FAILED -eq 0 ]]; then
        echo -e "\n${GREEN}${BOLD}All tests passed!${RESET}"
        exit 0
    else
        echo -e "\n${RED}${BOLD}Some tests failed.${RESET}"
        exit 1
    fi
}

main "$@"
