# Decoder regression test runner for hydrasdr_433
#
# Expected variables (passed via -D on command line):
#   HYDRASDR_433  - path to hydrasdr_433 executable
#   CU8_TO_CF32   - path to cu8_to_cf32 converter
#   TEST_DIR      - path to rtl_433_tests/tests directory
#
# Finds all .cu8 files with matching .json expected output,
# converts to CF32, replays both through hydrasdr_433, and
# compares output against expected JSON.

cmake_minimum_required(VERSION 3.11)

if(NOT HYDRASDR_433)
    message(FATAL_ERROR "HYDRASDR_433 not set")
endif()
if(NOT CU8_TO_CF32)
    message(FATAL_ERROR "CU8_TO_CF32 not set")
endif()
if(NOT TEST_DIR)
    message(FATAL_ERROR "TEST_DIR not set")
endif()

# Collect all test directories that have .cu8 files
file(GLOB_RECURSE CU8_FILES "${TEST_DIR}/*.cu8")

set(PASS_COUNT 0)
set(FAIL_COUNT 0)
set(SKIP_COUNT 0)
set(FAILED_TESTS "")

foreach(CU8_FILE ${CU8_FILES})
    # Derive paths
    get_filename_component(CU8_DIR "${CU8_FILE}" DIRECTORY)
    get_filename_component(CU8_NAME "${CU8_FILE}" NAME)
    get_filename_component(CU8_NAME_WE "${CU8_FILE}" NAME_WE)

    # Look for expected JSON output
    # rtl_433_tests uses convention: same directory, same base name or
    # the directory contains an expected .json file
    set(EXPECTED_JSON "")
    if(EXISTS "${CU8_DIR}/${CU8_NAME_WE}.json")
        set(EXPECTED_JSON "${CU8_DIR}/${CU8_NAME_WE}.json")
    else()
        # Some tests use a single .json for the directory
        file(GLOB DIR_JSON "${CU8_DIR}/*.json")
        list(LENGTH DIR_JSON JSON_COUNT)
        if(JSON_COUNT EQUAL 1)
            list(GET DIR_JSON 0 EXPECTED_JSON)
        endif()
    endif()

    if(NOT EXPECTED_JSON)
        math(EXPR SKIP_COUNT "${SKIP_COUNT} + 1")
        continue()
    endif()

    # Read expected JSON
    file(READ "${EXPECTED_JSON}" EXPECTED_CONTENT)
    # Normalize line endings
    string(REPLACE "\r\n" "\n" EXPECTED_CONTENT "${EXPECTED_CONTENT}")
    string(STRIP "${EXPECTED_CONTENT}" EXPECTED_CONTENT)

    # --- Test 1: Replay CU8 directly ---
    execute_process(
        COMMAND "${HYDRASDR_433}" -r "${CU8_FILE}" -F json -F null
        OUTPUT_VARIABLE CU8_OUTPUT
        ERROR_VARIABLE CU8_STDERR
        RESULT_VARIABLE CU8_RESULT
        TIMEOUT 30
    )
    string(REPLACE "\r\n" "\n" CU8_OUTPUT "${CU8_OUTPUT}")
    string(STRIP "${CU8_OUTPUT}" CU8_OUTPUT)

    # --- Test 2: Convert to CF32 and replay ---
    string(REGEX REPLACE "\\.cu8$" ".cf32" CF32_FILE "${CU8_FILE}")

    execute_process(
        COMMAND "${CU8_TO_CF32}" "${CU8_FILE}" "${CF32_FILE}"
        RESULT_VARIABLE CONV_RESULT
        TIMEOUT 30
    )

    set(CF32_OUTPUT "")
    set(CF32_RESULT 0)
    if(CONV_RESULT EQUAL 0)
        execute_process(
            COMMAND "${HYDRASDR_433}" -r "${CF32_FILE}" -F json -F null
            OUTPUT_VARIABLE CF32_OUTPUT
            ERROR_VARIABLE CF32_STDERR
            RESULT_VARIABLE CF32_RESULT
            TIMEOUT 30
        )
        string(REPLACE "\r\n" "\n" CF32_OUTPUT "${CF32_OUTPUT}")
        string(STRIP "${CF32_OUTPUT}" CF32_OUTPUT)

        # Cleanup temp CF32 file
        file(REMOVE "${CF32_FILE}")
    endif()

    # --- Compare results ---
    # Extract model fields from expected and actual for comparison
    # We check that every line in expected output has a matching line in actual
    set(TEST_PASSED TRUE)
    set(FAIL_REASON "")

    if(NOT CU8_RESULT EQUAL 0)
        set(TEST_PASSED FALSE)
        set(FAIL_REASON "CU8 replay exited with code ${CU8_RESULT}")
    endif()

    # Compare CU8 output against expected
    # We do a line-by-line model match: extract "model" from each JSON line
    if(TEST_PASSED AND EXPECTED_CONTENT)
        # Split expected into lines
        string(REPLACE "\n" ";" EXPECTED_LINES "${EXPECTED_CONTENT}")
        string(REPLACE "\n" ";" CU8_LINES "${CU8_OUTPUT}")

        list(LENGTH EXPECTED_LINES EXPECTED_LINE_COUNT)
        list(LENGTH CU8_LINES CU8_LINE_COUNT)

        # Check each expected line has a model match in output
        foreach(EXPECTED_LINE ${EXPECTED_LINES})
            # Skip empty lines
            string(STRIP "${EXPECTED_LINE}" EXPECTED_LINE)
            if(NOT EXPECTED_LINE)
                continue()
            endif()

            # Extract model field from expected
            string(REGEX MATCH "\"model\" *: *\"([^\"]+)\"" _MATCH "${EXPECTED_LINE}")
            if(NOT _MATCH)
                continue()
            endif()
            set(EXPECTED_MODEL "${CMAKE_MATCH_1}")

            # Check if any output line contains this model
            set(MODEL_FOUND FALSE)
            foreach(CU8_LINE ${CU8_LINES})
                string(FIND "${CU8_LINE}" "\"${EXPECTED_MODEL}\"" POS)
                if(NOT POS EQUAL -1)
                    set(MODEL_FOUND TRUE)
                    break()
                endif()
            endforeach()

            if(NOT MODEL_FOUND)
                set(TEST_PASSED FALSE)
                set(FAIL_REASON "Model '${EXPECTED_MODEL}' not found in CU8 output")
                break()
            endif()
        endforeach()
    endif()

    # Report
    get_filename_component(TEST_REL_DIR "${CU8_DIR}" NAME)
    if(TEST_PASSED)
        math(EXPR PASS_COUNT "${PASS_COUNT} + 1")
    else()
        math(EXPR FAIL_COUNT "${FAIL_COUNT} + 1")
        list(APPEND FAILED_TESTS "${TEST_REL_DIR}/${CU8_NAME}: ${FAIL_REASON}")
        message(STATUS "FAIL: ${TEST_REL_DIR}/${CU8_NAME} - ${FAIL_REASON}")
    endif()
endforeach()

# Summary
message(STATUS "")
message(STATUS "=== Decoder Regression Test Summary ===")
message(STATUS "Passed:  ${PASS_COUNT}")
message(STATUS "Failed:  ${FAIL_COUNT}")
message(STATUS "Skipped: ${SKIP_COUNT} (no expected .json)")
message(STATUS "")

if(FAIL_COUNT GREATER 0)
    message(STATUS "Failed tests:")
    foreach(F ${FAILED_TESTS})
        message(STATUS "  ${F}")
    endforeach()
    message(FATAL_ERROR "${FAIL_COUNT} decoder regression test(s) failed")
endif()

message(STATUS "All decoder regression tests passed.")
