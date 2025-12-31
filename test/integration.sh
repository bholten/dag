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
DAGWOOD="${DAGWOOD:-$REPO_ROOT/build/dagwood}"
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

    run_test_output_contains "-l lists projects" "Projects:" "$DAGWOOD" -f "$proj/Dagwood" -l
    run_test_output_contains "-l lists tasks" "Tasks:" "$DAGWOOD" -f "$proj/Dagwood" -l
    run_test_output_contains "-l shows step_1" "step_1" "$DAGWOOD" -f "$proj/Dagwood" -l
    run_test_output_contains "-l shows step_2" "step_2" "$DAGWOOD" -f "$proj/Dagwood" -l
    run_test_output_contains "-l shows commands" "Commands:" "$DAGWOOD" -f "$proj/Dagwood" -l
    run_test_output_contains "-l shows clean command" "clean" "$DAGWOOD" -f "$proj/Dagwood" -l
}

test_cli_dot() {
    log_section "CLI Dot Output"

    local proj="$TEST_PROJECTS/simple-project"

    run_test_output_contains "-d outputs digraph" "digraph" "$DAGWOOD" -f "$proj/Dagwood" -d
    run_test_output_contains "-d shows task nodes" "step_1" "$DAGWOOD" -f "$proj/Dagwood" -d
    run_test_output_contains "-d shows edges" "->" "$DAGWOOD" -f "$proj/Dagwood" -d
}

test_cli_dry_run() {
    log_section "CLI Dry Run"

    local proj="$TEST_PROJECTS/simple-project"

    run_test_output_contains "-y shows plan" "Layer" "$DAGWOOD" -f "$proj/Dagwood" -y
    run_test_output_contains "-y shows tasks" "Task Name" "$DAGWOOD" -f "$proj/Dagwood" -y
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

    # --- Test 4: Task run reference in run script ---
    output=$("$DAGWOOD" -C "$proj" "ns_test::reference_run" 2>&1)

    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "$output" | grep -q "base_script_output"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "task run script reference substituted"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "task run script reference not substituted"
    fi

    # --- Test 5: Dynamic task definition ---
    output=$("$DAGWOOD" -C "$proj" --dry-run 2>&1)

    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "$output" | grep -q "compile_main" && echo "$output" | grep -q "compile_util"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "dynamic task definitions created"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "dynamic task definitions not found"
    fi

    # Check dynamic task inputs/outputs
    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "$output" | grep -A3 "compile_main" | grep -q "src/main.c"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "dynamic task has correct inputs"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "dynamic task inputs not set correctly"
    fi

    # --- Test 6: Dynamic task output reference in link task ---
    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "$output" | grep -A5 "link_all" | grep -q "build/main.o"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "dynamic task outputs referenced in another task"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "dynamic task outputs not referenced correctly"
    fi

    # --- Test 7: Self-referencing task attributes ---
    output=$("$DAGWOOD" -C "$proj" "ns_test::self_ref" 2>&1)

    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "$output" | grep -q "My outputs are: build/self.txt"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "self-referencing outputs attribute works"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "self-referencing outputs attribute not working"
    fi

    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "$output" | grep -q "My description is: Self-referencing test"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "self-referencing description attribute works"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "self-referencing description attribute not working"
    fi

    # --- Test 8: Command attribute reference ---
    output=$("$DAGWOOD" -C "$proj" "ns_test::verify_command_attrs" 2>&1)

    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "$output" | grep -q "Command description: Shows project information"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "command description attribute accessible"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "command description attribute not accessible"
    fi

    # --- Test 9: Deep nested reference ---
    output=$("$DAGWOOD" -C "$proj" "ns_test::deep_reference" 2>&1)

    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "$output" | grep -q "Producer outputs: build/produced.txt"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "deep nested attribute reference works"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "deep nested attribute reference failed"
    fi

    # --- Test 10: Undefined variable preserved ---
    output=$("$DAGWOOD" -C "$proj" "ns_test::undefined_var" 2>&1)

    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "$output" | grep -q 'Undefined: $undefined::variable'; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "undefined variable preserved"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "undefined variable not preserved"
    fi

    # --- Test 11: Braced variable syntax ---
    output=$("$DAGWOOD" -C "$proj" "ns_test::braced_var" 2>&1)

    TESTS_RUN=$((TESTS_RUN + 1))
    if echo "$output" | grep -q "Braced: build/subdir"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "braced variable syntax works"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "braced variable syntax failed"
    fi

    # --- Test 12: Mixed shell and LCL variables ---
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

    # --- Cleanup ---
    "$DAGWOOD" -C "$proj" "ns_test::clean" >/dev/null 2>&1 || true
}

# ============================================================================
# Main
# ============================================================================

main() {
    echo -e "${BOLD}Dagwood Integration Test Suite${RESET}"
    echo "================================"

    preflight

    test_cli_options
    test_cli_list
    test_cli_dot
    test_cli_dry_run
    test_task_execution
    test_individual_task
    test_dependency_chain
    test_staleness
    test_error_handling
    test_commands
    test_namespaces

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
