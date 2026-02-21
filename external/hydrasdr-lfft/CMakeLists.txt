# hydrasdr-lfft library for hydrasdr_433
# Light FFT - simplified scalar-only build for small FFT sizes (2-32 points)
#
# License: MIT
# Copyright (c) 2025-2026, Benjamin Vernoux <bvernoux@hydrasdr.com>

cmake_minimum_required(VERSION 3.16)

# Source files
set(HYDRASDR_LFFT_SOURCES
	hydrasdr_lfft.c
	stockham_scalar.c
)

# Create static library
add_library(hydrasdr_lfft STATIC ${HYDRASDR_LFFT_SOURCES})

target_include_directories(hydrasdr_lfft PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(hydrasdr_lfft PRIVATE ${PROJECT_SOURCE_DIR}/include)

# Link math library on Unix
if(UNIX)
	target_link_libraries(hydrasdr_lfft m)
endif()

# Fast math for better auto-vectorization of scalar code
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
	target_compile_options(hydrasdr_lfft PRIVATE -ffast-math)
elseif(MSVC)
	target_compile_options(hydrasdr_lfft PRIVATE /fp:fast)
endif()

# Apply native CPU targeting if enabled in parent project
if(DSP_OPTIMIZE_FLAGS)
	if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
		target_compile_options(hydrasdr_lfft PRIVATE -march=native)
	elseif(MSVC)
		target_compile_options(hydrasdr_lfft PRIVATE /arch:AVX2)
	endif()
endif()

# Test executable (optional)
option(HYDRASDR_LFFT_BUILD_TESTS "Build hydrasdr-lfft tests" ON)
if(HYDRASDR_LFFT_BUILD_TESTS)
	add_executable(lfft_test lfft_test.c)
	target_link_libraries(lfft_test hydrasdr_lfft)
	if(UNIX)
		target_link_libraries(lfft_test m)
	endif()
endif()
