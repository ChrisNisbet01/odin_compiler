#!/bin/bash

set -e

# Usage: run_tests.sh [compiler_path] [test_file]
# If test_file is specified, only that test is run.

COMPILER="${1:-./build/src/odinc}"
SPECIFIC_TEST="$2"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_DIR="$SCRIPT_DIR"
OUTPUT_DIR="$TEST_DIR/output"

mkdir -p "$OUTPUT_DIR"

TEST_FAILED=false
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
FAILED_TESTS_FILE="$OUTPUT_DIR/failed_tests.list"
> "$FAILED_TESTS_FILE"

run_test() {
    local odin_file="$1"
    local base_name=$(basename "$odin_file" .odin)
    local exe_file="$OUTPUT_DIR/${base_name}"
    local err_file="$OUTPUT_DIR/${base_name}.err"
    local current_test_failed=false

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    echo ""
    echo "--- Testing: $odin_file ---"

    local base_dir_name=$(dirname "$odin_file")
    rm -f "$exe_file" "$err_file" "$OUTPUT_DIR/${base_name}.out"
    rm -f "$base_dir_name/${base_name}.ll" "$base_dir_name/${base_name}.o"

    # 1. Compile and link with odinc
    # Use --keep-temps so the .ll file is available for any debugging
    echo "  [ODINC] Building $odin_file"
    if ! "$COMPILER" build --keep-temps "$odin_file" > "$OUTPUT_DIR/${base_name}.out" 2> "$err_file"; then
        echo "  ERROR: odinc build failed for $odin_file. Check $err_file"
        current_test_failed=true
    fi
    if [ -s "$err_file" ]; then
        echo "  WARNING: odinc produced warnings for $odin_file. Check $err_file"
    fi

    # Check executable was produced (odinc emits it alongside the .ll and .o)
    local base_dir_name=$(dirname "$odin_file")
    local expected_exe="$base_dir_name/${base_name}"
    if [ ! -f "$expected_exe" ] && [ ! -f "$exe_file" ]; then
        echo "  ERROR: No executable produced for $odin_file"
        current_test_failed=true
    fi
    # Move executable to output dir if it was left in test/
    if [ -f "$expected_exe" ] && [ ! -f "$exe_file" ]; then
        mv "$expected_exe" "$exe_file"
    fi

    if [ "$current_test_failed" = "true" ]; then
        TEST_FAILED=true
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo "$odin_file" >> "$FAILED_TESTS_FILE"
        return
    fi

    # 2. Run the executable
    echo "  [RUN] Executing $exe_file"
    local exec_output="$OUTPUT_DIR/${base_name}_exec.log"
    set +e
    timeout 2 "$exe_file" > "$exec_output" 2>&1
    local exit_code=$?
    set -e

    if [ "$exit_code" -ne 0 ]; then
        echo "  FAILURE: $odin_file exit code $exit_code (expected 0)"
        TEST_FAILED=true
        current_test_failed=true
    else
        echo "  SUCCESS: $odin_file returned $exit_code"
        if [ -s "$exec_output" ]; then
            echo "  Output:"
            cat "$exec_output"
        fi
    fi

    if [ "$current_test_failed" = "true" ]; then
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo "$odin_file" >> "$FAILED_TESTS_FILE"
    else
        PASSED_TESTS=$((PASSED_TESTS + 1))
    fi
}

run_expected_fail_test() {
    local odin_file="$1"
    local base_name=$(basename "$odin_file" .odin)
    local err_file="$OUTPUT_DIR/${base_name}.err"
    local current_test_failed=false

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    echo ""
    echo "--- Testing (expected to fail): $odin_file ---"

    rm -f "$err_file" "$OUTPUT_DIR/${base_name}.out"

    set +e
    "$COMPILER" build "$odin_file" > "$OUTPUT_DIR/${base_name}.out" 2> "$err_file"
    local exit_code=$?
    set -e

    if [ "$exit_code" -eq 0 ]; then
        echo "  UNEXPECTED SUCCESS: compiled successfully but was expected to fail"
        TEST_FAILED=true
        current_test_failed=true
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo "$odin_file" >> "$FAILED_TESTS_FILE"
    else
        echo "  EXPECTED FAILURE: odinc failed as expected. Check $err_file"
    fi

    if [ "$current_test_failed" = "true" ]; then
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo "$odin_file" >> "$FAILED_TESTS_FILE"
    else
        PASSED_TESTS=$((PASSED_TESTS + 1))
    fi
}

# Set ODIN_ROOT so stubs in <project>/stubs/ are resolvable via <odin_root>/src/<pkg>/<pkg>.odin
export ODIN_ROOT="$(cd "$SCRIPT_DIR/../stubs" && pwd)"
echo "ODIN_ROOT=$ODIN_ROOT"

echo "--- Starting Odin Compiler Test Suite ---"
echo "Compiler: $COMPILER"
echo "Test directory: $TEST_DIR"
echo "Output directory: $OUTPUT_DIR"

if [ -n "$SPECIFIC_TEST" ]; then
    if [ ! -f "$SPECIFIC_TEST" ] && [ -f "$TEST_DIR/$SPECIFIC_TEST" ]; then
        SPECIFIC_TEST="$TEST_DIR/$SPECIFIC_TEST"
    fi
    if [ ! -f "$SPECIFIC_TEST" ]; then
        echo "Error: Test file not found: $SPECIFIC_TEST"
        exit 1
    fi
    run_test "$SPECIFIC_TEST"
else
    while IFS= read -r -d $'\0' odin_file; do
        run_test "$odin_file"
    done < <(find "$TEST_DIR" -maxdepth 1 -name "*.odin" -print0)

    if [ -d "$TEST_DIR/expected_to_fail" ]; then
        echo ""
        echo "=== Running Expected-to-Fail Tests ==="
        while IFS= read -r -d $'\0' odin_file; do
            run_expected_fail_test "$odin_file"
        done < <(find "$TEST_DIR/expected_to_fail" -maxdepth 1 -name "*.odin" -print0)
    fi
fi

echo ""
echo "--- Test Suite Summary ---"
echo "Total Tests: $TOTAL_TESTS"
echo "Passed:      $PASSED_TESTS"
echo "Failed:      $FAILED_TESTS"

if [ "$TEST_FAILED" = "true" ]; then
    echo "Result: SOME TESTS FAILED"
    echo "Failed tests:"
    sort "$FAILED_TESTS_FILE"
    exit 1
else
    echo "Result: ALL TESTS PASSED"
    exit 0
fi
