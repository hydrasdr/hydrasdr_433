########################################################################
# Decoder regression tests (optional, fetches ~300MB test data)
########################################################################
option(ENABLE_DECODER_TESTS "Enable decoder regression tests (fetches rtl_433_tests)" OFF)

if(ENABLE_DECODER_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        rtl_433_tests
        GIT_REPOSITORY https://github.com/merbanan/rtl_433_tests.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(rtl_433_tests)
    set(RTL_433_TESTS_DIR ${rtl_433_tests_SOURCE_DIR})
    message(STATUS "rtl_433_tests fetched to: ${RTL_433_TESTS_DIR}")
endif()

########################################################################
# CU8-to-CF32 converter tool
########################################################################
add_executable(cu8_to_cf32 cu8_to_cf32.c)

########################################################################
# Compile test cases
########################################################################
add_executable(data-test data-test.c ../src/output_file.c ../src/term_ctl.c)

target_link_libraries(data-test data)

add_test(data-test data-test)

add_executable(baseband-test baseband-test.c ../src/baseband.c ../src/logger.c)

if(UNIX)
target_link_libraries(baseband-test m)
endif()

#add_test(baseband-test baseband-test)

add_executable(resampler-test resampler-test.c ../src/cf32_resampler.c)
target_include_directories(resampler-test PRIVATE ${PROJECT_SOURCE_DIR}/include)

if(UNIX)
target_link_libraries(resampler-test m)
endif()

add_test(resampler-test resampler-test)

add_executable(channelizer-test channelizer-test.c ../src/channelizer.c
    ../src/channelizer_sse2.c ../src/channelizer_avx2.c ../src/channelizer_avx512.c
    ../src/channelizer_neon.c ../src/channelizer_sve.c)
target_include_directories(channelizer-test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/external/hydrasdr-lfft)
target_link_libraries(channelizer-test hydrasdr_lfft)

if(UNIX)
target_link_libraries(channelizer-test m)
endif()

add_test(channelizer-test channelizer-test)

add_executable(channelizer-functional-test channelizer-functional-test.c ../src/channelizer.c
    ../src/channelizer_sse2.c ../src/channelizer_avx2.c ../src/channelizer_avx512.c
    ../src/channelizer_neon.c ../src/channelizer_sve.c)
target_include_directories(channelizer-functional-test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/external/hydrasdr-lfft)
target_link_libraries(channelizer-functional-test hydrasdr_lfft)

if(UNIX)
target_link_libraries(channelizer-functional-test m)
endif()

add_test(channelizer-functional-test channelizer-functional-test)

add_executable(channelizer-bench channelizer-bench.c ../src/channelizer.c
    ../src/channelizer_sse2.c ../src/channelizer_avx2.c ../src/channelizer_avx512.c
    ../src/channelizer_neon.c ../src/channelizer_sve.c)
target_include_directories(channelizer-bench PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/external/hydrasdr-lfft)
target_link_libraries(channelizer-bench hydrasdr_lfft)

if(UNIX)
target_link_libraries(channelizer-bench m)
endif()

add_test(channelizer-bench channelizer-bench)

add_executable(channelizer-filter-char channelizer-filter-char.c ../src/channelizer.c
    ../src/channelizer_sse2.c ../src/channelizer_avx2.c ../src/channelizer_avx512.c
    ../src/channelizer_neon.c ../src/channelizer_sve.c)
target_include_directories(channelizer-filter-char PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/external/hydrasdr-lfft)
target_link_libraries(channelizer-filter-char hydrasdr_lfft)

if(UNIX)
target_link_libraries(channelizer-filter-char m)
endif()

add_executable(pipeline-test pipeline-test.c ../src/channelizer.c ../src/cf32_resampler.c
    ../src/channelizer_sse2.c ../src/channelizer_avx2.c ../src/channelizer_avx512.c
    ../src/channelizer_neon.c ../src/channelizer_sve.c)
target_include_directories(pipeline-test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/external/hydrasdr-lfft)
target_link_libraries(pipeline-test hydrasdr_lfft)

if(UNIX)
target_link_libraries(pipeline-test m)
endif()

add_test(pipeline-test pipeline-test)

add_executable(channelizer-os-test channelizer-os-test.c ../src/channelizer.c
    ../src/channelizer_sse2.c ../src/channelizer_avx2.c ../src/channelizer_avx512.c
    ../src/channelizer_neon.c ../src/channelizer_sve.c)
target_include_directories(channelizer-os-test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/external/hydrasdr-lfft)
target_link_libraries(channelizer-os-test hydrasdr_lfft)

if(UNIX)
target_link_libraries(channelizer-os-test m)
endif()

add_test(channelizer-os-test channelizer-os-test)

add_executable(channelizer-profile channelizer-profile.c ../src/channelizer.c
    ../src/channelizer_sse2.c ../src/channelizer_avx2.c ../src/channelizer_avx512.c
    ../src/channelizer_neon.c ../src/channelizer_sve.c)
target_include_directories(channelizer-profile PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/external/hydrasdr-lfft)
target_link_libraries(channelizer-profile hydrasdr_lfft)

if(UNIX)
target_link_libraries(channelizer-profile m)
endif()

# ISA-specific flags for channelizer hot-path variants (test scope)
if(ENABLE_NATIVE_OPTIMIZATIONS)
    set_source_files_properties(../src/channelizer_sse2.c ../src/channelizer_avx2.c ../src/channelizer_avx512.c
        ../src/channelizer_neon.c ../src/channelizer_sve.c
        channelizer-profile.c
        PROPERTIES COMPILE_FLAGS "${DSP_OPTIMIZE_FLAGS}")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i[3-6]86")
    if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
        set_source_files_properties(../src/channelizer_sse2.c
            PROPERTIES COMPILE_FLAGS "-ffast-math")
        set_source_files_properties(../src/channelizer_avx2.c
            PROPERTIES COMPILE_FLAGS "-mavx2 -mfma -ffast-math")
        set_source_files_properties(../src/channelizer_avx512.c
            PROPERTIES COMPILE_FLAGS "-mavx512f -mavx512vl -mfma -ffast-math")
        # ARM variant files: compile as baseline (linked but never called on x86)
        set_source_files_properties(../src/channelizer_neon.c ../src/channelizer_sve.c
            PROPERTIES COMPILE_FLAGS "-ffast-math")
    elseif(MSVC)
        set_source_files_properties(../src/channelizer_sse2.c
            PROPERTIES COMPILE_FLAGS "/fp:fast")
        set_source_files_properties(../src/channelizer_avx2.c
            PROPERTIES COMPILE_FLAGS "/arch:AVX2 /fp:fast")
        set_source_files_properties(../src/channelizer_avx512.c
            PROPERTIES COMPILE_FLAGS "/arch:AVX512 /fp:fast")
        # ARM variant files: compile as baseline (linked but never called on x86)
        set_source_files_properties(../src/channelizer_neon.c ../src/channelizer_sve.c
            PROPERTIES COMPILE_FLAGS "/fp:fast")
    endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    # ARM AArch64: NEON is mandatory, SVE is optional
    if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
        set_source_files_properties(../src/channelizer_neon.c
            PROPERTIES COMPILE_FLAGS "-ffast-math")
        # Apple Clang does not support -march=armv8-a+sve (no SVE on Apple Silicon)
        if(NOT APPLE)
            set_source_files_properties(../src/channelizer_sve.c
                PROPERTIES COMPILE_FLAGS "-march=armv8-a+sve -ffast-math")
        else()
            set_source_files_properties(../src/channelizer_sve.c
                PROPERTIES COMPILE_FLAGS "-ffast-math")
        endif()
        # x86 variant files: compile as baseline (linked but never called on ARM)
        set_source_files_properties(../src/channelizer_sse2.c ../src/channelizer_avx2.c ../src/channelizer_avx512.c
            PROPERTIES COMPILE_FLAGS "-ffast-math")
    endif()
else()
    # Non-x86, non-ARM (RISC-V, etc.): all variants get baseline fast-math only
    if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
        set_source_files_properties(../src/channelizer_sse2.c ../src/channelizer_avx2.c ../src/channelizer_avx512.c
            ../src/channelizer_neon.c ../src/channelizer_sve.c
            PROPERTIES COMPILE_FLAGS "-ffast-math")
    endif()
endif()
# channelizer.c itself: NO -ffast-math (fixes frequency mapping test failures)
# cf32_resampler.c: keep DSP optimization flags when native optimizations enabled
if(DSP_OPTIMIZE_FLAGS)
    set_source_files_properties(../src/cf32_resampler.c
        channelizer-profile.c
        PROPERTIES COMPILE_FLAGS "${DSP_OPTIMIZE_FLAGS}")
endif()

########################################################################
# Define and build all unit tests
########################################################################
# target_compile_definitions was only added in CMake 2.8.11
add_definitions(-D_TEST)
foreach(testSrc bitbuffer.c fileformat.c optparse.c bit_util.c)
    get_filename_component(testName ${testSrc} NAME_WE)

    add_executable(test_${testName} ../src/${testSrc})

    add_test(${testName}_test test_${testName})
endforeach(testSrc)

########################################################################
# Define integration tests
########################################################################
add_test(hydrasdr_433_help ../src/hydrasdr_433 -h)

########################################################################
# Define style checks
########################################################################
add_executable(style-check style-check.c)
file(GLOB STYLE_CHECK_FILES  ../include/*.h ../src/*.c ../src/devices/*.c ../CMakeLists.txt ../*/CMakeLists.txt)
list(REMOVE_ITEM STYLE_CHECK_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/jsmn.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/jsmn.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/mongoose.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/mongoose.c")
add_test(style-check style-check ${STYLE_CHECK_FILES})

########################################################################
# Decoder regression tests
########################################################################
if(ENABLE_DECODER_TESTS)
    add_test(NAME decoder-regression
        COMMAND ${CMAKE_COMMAND}
            -DHYDRASDR_433=$<TARGET_FILE:hydrasdr_433>
            -DCU8_TO_CF32=$<TARGET_FILE:cu8_to_cf32>
            -DTEST_DIR=${RTL_433_TESTS_DIR}/tests
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_decoder_tests.cmake
    )
endif()

########################################################################
# Define clang static analyzer checks
########################################################################
if(BUILD_TESTING_ANALYZER)
file(GLOB ANALYZER_CHECK_FILES  ../include/*.h ../src/*.c ../src/devices/*.c)
list(REMOVE_ITEM ANALYZER_CHECK_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/jsmn.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/jsmn.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/mongoose.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/mongoose.c")
add_test(clang-analyzer
    ${CMAKE_CURRENT_SOURCE_DIR}/exitcode-for-output.sh
    clang
    -I${CMAKE_CURRENT_SOURCE_DIR}/../include
    --analyze
    -Xanalyzer
    -analyzer-output=text
    -Xanalyzer
    ${ANALYZER_CHECK_FILES})
endif()
