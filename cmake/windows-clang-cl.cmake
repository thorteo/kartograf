# Cross-compile to Windows (MSVC ABI) from Linux, using clang-cl against the
# genuine Microsoft CRT and Windows SDK that xwin fetches.
#
# This route exists because MinGW is not an option here: its dcomp.h and d3d11.h
# have historically been incomplete, and DirectComposition is the whole basis of
# the overlay. See docs/build.md and docs/adr/0003-overlay-window.md.
#
# Set up once:
#   xwin --accept-license --arch x86_64 --variant desktop splat --output ~/.xwin-sdk

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

if(NOT DEFINED XWIN_ROOT)
    if(DEFINED ENV{XWIN_ROOT})
        set(XWIN_ROOT "$ENV{XWIN_ROOT}")
    else()
        set(XWIN_ROOT "$ENV{HOME}/.xwin-sdk")
    endif()
endif()
set(XWIN_ROOT "${XWIN_ROOT}" CACHE PATH "Directory produced by 'xwin splat'")

if(NOT EXISTS "${XWIN_ROOT}/sdk/include/um/dcomp.h")
    message(FATAL_ERROR
        "Windows SDK not found under XWIN_ROOT='${XWIN_ROOT}'.\n"
        "Run: xwin --accept-license --arch x86_64 --variant desktop splat "
        "--output ${XWIN_ROOT}")
endif()

set(CMAKE_C_COMPILER   clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER       lld-link)
set(CMAKE_RC_COMPILER  llvm-rc)
set(CMAKE_AR           llvm-lib)
set(CMAKE_MT           llvm-mt)

set(CMAKE_C_COMPILER_TARGET   x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)

# /imsvc marks these as system includes, which keeps -Wall from reporting the
# SDK's own warnings as ours.
set(_xwin_includes
    "/imsvc${XWIN_ROOT}/crt/include"
    "/imsvc${XWIN_ROOT}/sdk/include/ucrt"
    "/imsvc${XWIN_ROOT}/sdk/include/um"
    "/imsvc${XWIN_ROOT}/sdk/include/shared"
    "/imsvc${XWIN_ROOT}/sdk/include/winrt"
)
string(JOIN " " _xwin_include_flags ${_xwin_includes})

set(_xwin_common "${_xwin_include_flags} -fuse-ld=lld-link /EHsc /DUNICODE /D_UNICODE /DWIN32 /D_WINDOWS")
set(CMAKE_C_FLAGS_INIT   "${_xwin_common}")
set(CMAKE_CXX_FLAGS_INIT "${_xwin_common}")

set(_xwin_libs
    "/libpath:${XWIN_ROOT}/crt/lib/x86_64"
    "/libpath:${XWIN_ROOT}/sdk/lib/um/x86_64"
    "/libpath:${XWIN_ROOT}/sdk/lib/ucrt/x86_64"
)
string(JOIN " " _xwin_lib_flags ${_xwin_libs})

set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_xwin_lib_flags}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_xwin_lib_flags}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_xwin_lib_flags}")

# Look for programs on the host, everything else in the cross SDK.
set(CMAKE_FIND_ROOT_PATH "${XWIN_ROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM BEFORE)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Static release CRT, in every configuration.
#
# Two reasons. First, xwin's splat carries only the release CRT -- there is no
# msvcrtd.lib to link a debug build against. Second, and the reason we would
# choose this anyway: linking the CRT statically means the overlay ships as one
# .exe that runs without the Visual C++ redistributable installed, which is what
# ADR 0001 promised. Debug information is still produced; only the CRT flavour
# is pinned.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "" FORCE)
