# Windows x64 MSVC dependency SDK

Raw export created on 2026-10-01 from D:/lib/vcpkg at revision
0cb95c860ea83aafc1b24350510b30dec535989a.

## Contents

- libzip 1.11.4, x64-windows-static-md
- minizip-ng 4.0.10, x64-windows-static-md, with zlib feature
- zlib 1.3.1, x64-windows-static-md
- vcpkg-cmake and vcpkg-cmake-config integration files
- Release and Debug static libraries, CMake metadata, and license notices
- SHA-256 checksums of binary artifacts in binary-checksums.csv

Boost 1.88 Regex and Nowide conversion headers are shared with the MinGW
SDK at ../windows/boost_1_88_0, including its Boost license.
No Boost compiled API is needed for the current app. CMake defines BOOST_NOWIDE_NO_LIB to suppress its automatic MSVC library linkage.

## Local Release build

Run PowerShell from the repository root:

    cmake -S . -B out/msvc-bundled-release -G "Visual Studio 17 2022" -A x64 -DZHO_USE_BUNDLED_MINGW=OFF
    cmake --build out/msvc-bundled-release --config Release --parallel 4
    & C:/Qt/6.8.3/msvc2022_64/bin/windeployqt.exe --release --compiler-runtime out/msvc-bundled-release/Release/ZhoConverterQt.exe

Qt remains hard-coded to C:/Qt/6.8.3/msvc2022_64. MSVC uses /MD for
Release and /MDd for Debug, matching this static-library/dynamic-CRT
triplet. The local compiler is MSVC 19.44 (VS 2022 toolset v143).
CMake uses C++20 because the existing source contains designated initializers. Use a fresh build directory when switching compiler, triplet, or SDK.

OpenCC and PDFium use their repository DLL/import-library pairs and are
copied beside the executable after linking. Qt and the compiler runtime
are still DLL dependencies; this is not a fully static application.

## Refresh

    ./scripts/export-msvc-deps.ps1 -OutputName windows-msvc-next

Exports already installed packages without rebuilding or installing.
Inspect the new SDK before replacing windows-msvc. Record updated
versions here and verify a fresh build after replacement.

The existing MinGW SDK remains at ../windows for compatibility with
the previous local build. No external D:/lib dependency path is needed
to consume either Windows SDK.
