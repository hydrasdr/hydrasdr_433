# - Try to find HydraSDR
# Once done this will define
#
#  HydraSDR_FOUND - System has libhydrasdr
#  HydraSDR_INCLUDE_DIRS - The libhydrasdr include directories
#  HydraSDR_LIBRARIES - The libraries needed to use libhydrasdr
#  HydraSDR_DEFINITIONS - Compiler switches required for using libhydrasdr
#  HydraSDR_VERSION - The libhydrasdr version
#
# User-configurable variables:
#  HYDRASDR_DIR - Path to HydraSDR installation or source directory

find_package(PkgConfig)
pkg_check_modules(PC_HydraSDR QUIET hydrasdr)
set(HydraSDR_DEFINITIONS ${PC_HydraSDR_CFLAGS_OTHER})

# Build search paths from HYDRASDR_DIR if specified
set(_hydrasdr_search_include)
set(_hydrasdr_search_lib)
if(HYDRASDR_DIR)
    # Support both installed and source tree layouts
    list(APPEND _hydrasdr_search_include
        ${HYDRASDR_DIR}/include
        ${HYDRASDR_DIR}/libhydrasdr/src
        ${HYDRASDR_DIR}/src)
    list(APPEND _hydrasdr_search_lib
        ${HYDRASDR_DIR}/lib)
    # Prefer matching platform-specific build directories first
    if(MSVC)
        list(APPEND _hydrasdr_search_lib
            ${HYDRASDR_DIR}/build_VS2022/libhydrasdr/src/Release
            ${HYDRASDR_DIR}/build_VS2022/hydrasdr-tools/src/Release
            ${HYDRASDR_DIR}/build_VS2022/libhydrasdr/src
            ${HYDRASDR_DIR}/build_VS2022/hydrasdr-tools/src)
    else()
        list(APPEND _hydrasdr_search_lib
            ${HYDRASDR_DIR}/build_MINGW64/libhydrasdr/src
            ${HYDRASDR_DIR}/build_MINGW64/hydrasdr-tools/src)
    endif()
    list(APPEND _hydrasdr_search_lib
        # Generic build directory last
        ${HYDRASDR_DIR}/build
        ${HYDRASDR_DIR}/build/src
        ${HYDRASDR_DIR}/build/Release
        ${HYDRASDR_DIR}/build/Debug
        ${HYDRASDR_DIR}/build/libhydrasdr/src
        ${HYDRASDR_DIR}/build/hydrasdr-tools/src
        # Parent directory variants
        ${HYDRASDR_DIR}/../build_MINGW64/libhydrasdr/src
        ${HYDRASDR_DIR}/../build_MINGW64/hydrasdr-tools/src
        ${HYDRASDR_DIR}/../build_VS2022/libhydrasdr/src
        ${HYDRASDR_DIR}/../build_VS2022/hydrasdr-tools/src
        ${HYDRASDR_DIR}/../build/libhydrasdr/src
        ${HYDRASDR_DIR}/../build/hydrasdr-tools/src)
endif()

find_path(HydraSDR_INCLUDE_DIR NAMES hydrasdr.h
          HINTS ${PC_HydraSDR_INCLUDE_DIRS}
                ${_hydrasdr_search_include}
          PATHS
          /usr/include
          /usr/local/include)

# Search for dynamic library (import library on Windows)
find_library(HydraSDR_LIBRARY NAMES hydrasdr libhydrasdr
             HINTS ${PC_HydraSDR_LIBRARY_DIRS}
                   ${_hydrasdr_search_lib}
             PATHS
             /usr/lib
             /usr/local/lib)

# Try to get version from header file
if(HydraSDR_INCLUDE_DIR AND EXISTS "${HydraSDR_INCLUDE_DIR}/hydrasdr.h")
    file(STRINGS "${HydraSDR_INCLUDE_DIR}/hydrasdr.h" _hydrasdr_version_line
         REGEX "^#define[ \t]+HYDRASDR_VERSION[ \t]+\"[^\"]*\"")
    if(_hydrasdr_version_line)
        string(REGEX REPLACE "^#define[ \t]+HYDRASDR_VERSION[ \t]+\"([^\"]*)\".*" "\\1"
               HydraSDR_VERSION "${_hydrasdr_version_line}")
    endif()
endif()

# Fallback to pkg-config version
if(NOT HydraSDR_VERSION AND PC_HydraSDR_VERSION)
    set(HydraSDR_VERSION ${PC_HydraSDR_VERSION})
endif()

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set HydraSDR_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(HydraSDR
                                  FOUND_VAR HydraSDR_FOUND
                                  REQUIRED_VARS HydraSDR_LIBRARY HydraSDR_INCLUDE_DIR
                                  VERSION_VAR HydraSDR_VERSION)

mark_as_advanced(HydraSDR_LIBRARY HydraSDR_INCLUDE_DIR HydraSDR_VERSION)

set(HydraSDR_LIBRARIES ${HydraSDR_LIBRARY})
set(HydraSDR_INCLUDE_DIRS ${HydraSDR_INCLUDE_DIR})

# Get directory containing the library and DLLs for copying on Windows
if(HydraSDR_LIBRARY)
    get_filename_component(HydraSDR_LIBRARY_DIR ${HydraSDR_LIBRARY} DIRECTORY)
    # On MinGW/MSVC, DLLs may be in a different directory than import libs
    # Look for DLLs in hydrasdr-tools/src if not found in library dir
    set(_hydrasdr_dll_search
        ${HydraSDR_LIBRARY_DIR}
        ${HydraSDR_LIBRARY_DIR}/../hydrasdr-tools/src
        ${HydraSDR_LIBRARY_DIR}/../hydrasdr-tools/src/Release
        ${HydraSDR_LIBRARY_DIR}/../../hydrasdr-tools/src
        ${HydraSDR_LIBRARY_DIR}/../../hydrasdr-tools/src/Release
        ${HydraSDR_LIBRARY_DIR}/../../../hydrasdr-tools/src
        ${HydraSDR_LIBRARY_DIR}/../../../hydrasdr-tools/src/Release)
    find_file(HydraSDR_DLL NAMES libhydrasdr.dll hydrasdr.dll
              HINTS ${_hydrasdr_dll_search}
              NO_DEFAULT_PATH)
    if(HydraSDR_DLL)
        get_filename_component(HydraSDR_DLL_DIR ${HydraSDR_DLL} DIRECTORY)
    else()
        set(HydraSDR_DLL_DIR ${HydraSDR_LIBRARY_DIR})
    endif()
    set(HydraSDR_DLL_DIR ${HydraSDR_DLL_DIR} CACHE PATH "Directory containing HydraSDR DLLs")
endif()
