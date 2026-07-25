# libmpv is not currently an official vcpkg port. This module locates the
# development package normalized by scripts/fetch_mpv.ps1 and exposes it as
# the imported target Supernova::mpv.

set(MPV_ROOT "${CMAKE_SOURCE_DIR}/deps/mpv" CACHE PATH
    "Path to the normalized libmpv development package")

find_path(MPV_INCLUDE_DIR
    NAMES mpv/client.h
    PATHS "${MPV_ROOT}/include"
    NO_DEFAULT_PATH
)

find_library(MPV_IMPLIB
    NAMES mpv libmpv
    PATHS "${MPV_ROOT}/lib"
    NO_DEFAULT_PATH
)

file(GLOB MPV_RUNTIME_DLL_CANDIDATES
    "${MPV_ROOT}/bin/libmpv-2.dll"
    "${MPV_ROOT}/bin/mpv-2.dll"
)

list(LENGTH MPV_RUNTIME_DLL_CANDIDATES _mpv_dll_count)
if(_mpv_dll_count EQUAL 0)
    message(FATAL_ERROR
        "No mpv runtime DLL found under ${MPV_ROOT}/bin. "
        "Run scripts/fetch_mpv.ps1 first.")
elseif(_mpv_dll_count GREATER 1)
    message(FATAL_ERROR
        "Multiple mpv runtime DLLs found under ${MPV_ROOT}/bin. "
        "Remove the stale DLL and run scripts/fetch_mpv.ps1 again.")
endif()
list(GET MPV_RUNTIME_DLL_CANDIDATES 0 MPV_RUNTIME_DLL)

if(NOT MPV_INCLUDE_DIR OR NOT MPV_IMPLIB)
    message(FATAL_ERROR
        "The libmpv development package is incomplete under ${MPV_ROOT}. "
        "Run scripts/fetch_mpv.ps1 first, or set -DMPV_ROOT=<path>.")
endif()

if(NOT TARGET Supernova::mpv)
    add_library(Supernova::mpv SHARED IMPORTED GLOBAL)
    set_target_properties(Supernova::mpv PROPERTIES
        IMPORTED_IMPLIB "${MPV_IMPLIB}"
        IMPORTED_LOCATION "${MPV_RUNTIME_DLL}"
        INTERFACE_INCLUDE_DIRECTORIES "${MPV_INCLUDE_DIR}"
    )
endif()
