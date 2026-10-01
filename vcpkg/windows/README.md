# Windows MinGW dependency SDK

This directory contains a raw vcpkg export made on 2026-10-01 from
D:/lib/vcpkg (revision 0cb95c860ea83aafc1b24350510b30dec535989a), plus the local Boost 1.88.0 header subset from
D:/lib/boost_1_88_0/boost. No Boost MSVC binaries are included.

## Contents

- libzip 1.11.4, x64-mingw-static
- minizip-ng 4.0.10, x64-mingw-static, with zlib feature
- zlib 1.3.1, x64-mingw-static
- vcpkg-cmake and vcpkg-cmake-config host integration files
- Boost 1.88.0 headers used by Regex and Nowide conversions
- Release and Debug static libraries and CMake discovery metadata
- Package notices under installed/*/share/*/copyright
- Boost license under boost_1_88_0/LICENSE_1_0.txt
- SHA-256 checksums of binary artifacts in binary-checksums.csv

The bundle is approximately 14 MB. It does not include Qt, a compiler, or
prebuilt Boost.Nowide compiled APIs. The current app only uses its header
conversion functions.

## Local Release build

Run PowerShell from the repository root:

    $env:PATH = "C:/Qt/Tools/mingw1310_64/bin;C:/Qt/Tools/Ninja;" + $env:PATH
    cmake -S . -B out/mingw-bundled-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe -DZHO_USE_BUNDLED_MINGW=ON
    cmake --build out/mingw-bundled-release --parallel 4

Qt remains hard-coded to C:/Qt/6.8.3/mingw_64. Verified with GCC 13.1.0.
Use a fresh build directory when switching compiler, triplet, or SDK.
MSVC uses the repository SDK at ../windows-msvc with -DZHO_USE_BUNDLED_MINGW=OFF.

OpenCC and PDFium DLLs are copied beside the executable after linking.
Qt and compiler runtime deployment can be performed with:

    & C:/Qt/6.8.3/mingw_64/bin/windeployqt.exe --release --compiler-runtime out/mingw-bundled-release/ZhoConverterQt.exe

The ZIP libraries are static; Qt, OpenCC, PDFium, and possibly compiler
runtime libraries remain DLL dependencies. Successful compilation/linking
does not validate document conversion behavior.

## Refresh the SDK

From the repository root, export to a new directory for inspection:

    ./scripts/export-mingw-deps.ps1 -OutputName windows-next

The script exports already installed packages; it does not rebuild or
install packages. After inspecting a refreshed SDK, replace windows,
refresh this version record and binary-checksums.csv, and build in a new
directory. The local Boost source is a reduced header tree; future uses
of additional Boost APIs may require additional headers or libraries.
