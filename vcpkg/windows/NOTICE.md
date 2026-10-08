x86_64-win32-seh
# Windows vcpkg Build Notice

The prebuilt vcpkg libraries in this directory were compiled using the following MinGW-w64 toolchain:

**`mingw-x86_64-win32-seh-mvcrt`**

## Build Configuration

- **Compiler:** GCC (MinGW-w64)
- **C Runtime:** MSVCRT
- **Exception handling:** SEH
- **Thread model:** win32
- **Target:** Windows x86-64

## Important

When rebuilding or updating these libraries, use the same toolchain configuration to maintain build consistency and avoid potential ABI or runtime incompatibilities.

Do not assume that libraries built with UCRT, POSIX threading, or a different MinGW-w64 distribution are interchangeable without verification.

This notice documents the build environment for future maintenance and reproducibility.