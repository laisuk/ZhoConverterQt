cmake_minimum_required(VERSION 3.16)
# Select the dependency SDK before compiler detection.
if (WIN32)
    set(_zho_mingw_default ON)
    if (CMAKE_GENERATOR MATCHES "Visual Studio" OR
            CMAKE_CXX_COMPILER MATCHES "cl([.]exe)?$" OR
            VCPKG_TARGET_TRIPLET MATCHES "^x64-windows")
        set(_zho_mingw_default OFF)
    endif ()

    option(ZHO_USE_BUNDLED_MINGW
            "Use the repository MinGW dependency SDK"
            ${_zho_mingw_default})

    if (ZHO_USE_BUNDLED_MINGW)
        set(CMAKE_TOOLCHAIN_FILE
                "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/windows/scripts/buildsystems/vcpkg.cmake"
                CACHE FILEPATH "Dependency toolchain")
        set(VCPKG_TARGET_TRIPLET
                "x64-mingw-static"
                CACHE STRING "Dependency triplet")
        set(VCPKG_HOST_TRIPLET
                "x64-mingw-static"
                CACHE STRING "Host triplet")
    else ()
        set(CMAKE_TOOLCHAIN_FILE
                "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/windows-msvc/scripts/buildsystems/vcpkg.cmake"
                CACHE FILEPATH "Dependency toolchain")
        set(VCPKG_TARGET_TRIPLET
                "x64-windows-static-md"
                CACHE STRING "Dependency triplet")
        set(VCPKG_HOST_TRIPLET
                "x64-windows-static-md"
                CACHE STRING "Host triplet")
    endif ()

elseif (APPLE)
    set(CMAKE_TOOLCHAIN_FILE
            "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/macos-arm64/scripts/buildsystems/vcpkg.cmake"
            CACHE FILEPATH "Dependency toolchain")
    set(VCPKG_TARGET_TRIPLET
            "arm64-osx-static"
            CACHE STRING "Dependency triplet")
    set(VCPKG_HOST_TRIPLET
            "arm64-osx-static"
            CACHE STRING "Host triplet")

elseif (UNIX)
    set(CMAKE_TOOLCHAIN_FILE
            "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/linux/scripts/buildsystems/vcpkg.cmake"
            CACHE FILEPATH "Dependency toolchain")
    set(VCPKG_TARGET_TRIPLET
            "x64-linux"
            CACHE STRING "Dependency triplet")
    set(VCPKG_HOST_TRIPLET
            "x64-linux"
            CACHE STRING "Host triplet")
endif ()


project(ZhoConverterQt LANGUAGES C CXX)

if (WIN32 AND ZHO_USE_BUNDLED_MINGW AND NOT MINGW)
    message(FATAL_ERROR "The bundled SDK requires MinGW. Configure MSVC with -DZHO_USE_BUNDLED_MINGW=OFF in a fresh build directory.")
endif ()
if (WIN32 AND NOT ZHO_USE_BUNDLED_MINGW AND NOT MSVC)
    message(FATAL_ERROR "The MSVC SDK requires MSVC. Configure MinGW with -DZHO_USE_BUNDLED_MINGW=ON in a fresh build directory.")
endif ()
if (WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The bundled Windows SDKs require an x64 build.")
endif ()
if (MSVC)
    # Match x64-windows-static-md and the standard Qt MSVC runtime.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    add_compile_options(/utf-8)
endif ()

include(qt.cmake)

# Encoding tables and detector results use C++20 designated initializers.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Install path" FORCE)

if (WIN32)
    if (MSVC)
        #        set(CMAKE_PREFIX_PATH "C:/QT/6.11.2/MSVC2022_64")
        list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/windows-msvc/installed/x64-windows-static-md")
        include_directories("${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/windows-msvc/installed/x64-windows-static-md/include")
        # add_compile_options(/Zc:__cplusplus)  # Ensure __cplusplus is correctly set
    else ()
        #        set(CMAKE_PREFIX_PATH "C:/Qt/6.11.2/mingw_64")
        list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/windows/installed/x64-mingw-static")
        include_directories("${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/windows/installed/x64-mingw-static/include")
    endif ()


elseif (APPLE)
    # Local Qt fallback; CI supplies CMAKE_PREFIX_PATH.
    if (NOT CMAKE_PREFIX_PATH)
        set(CMAKE_PREFIX_PATH "/Users/bryan/Qt/6.8.3/macos")
    endif ()

    list(APPEND CMAKE_PREFIX_PATH
            "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/macos-arm64/installed/arm64-osx-static")

    include_directories(
            "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/macos-arm64/installed/arm64-osx-static/include")

elseif (UNIX)
    # Local Qt fallback; CI supplies CMAKE_PREFIX_PATH.
    if (NOT CMAKE_PREFIX_PATH)
        set(CMAKE_PREFIX_PATH "/opt/Qt/6.8.3/gcc_64")
    endif ()

    list(APPEND CMAKE_PREFIX_PATH
            "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/linux/installed/x64-linux")

    include_directories(
            "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/linux/installed/x64-linux/include")

    set(CMAKE_BUILD_RPATH "$ORIGIN")
endif ()


include_directories(
        "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/boost_1_88_0")

#set(QT_DIR "${CMAKE_PREFIX_PATH}/lib/cmake/Qt6")

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR}
        COMPONENTS
        Core
        Gui
        Widgets
        Concurrent
)
qt_standard_project_setup()
#set(libzip_DIR "D:/lib/vcpkg/packages/libzip_x64-windows-static/share/libzip")

find_package(libzip CONFIG REQUIRED)
find_package(minizip-ng CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

if (NOT WIN32)
    find_package(Iconv REQUIRED)
endif ()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/opencc-fmmseg)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/pdfium)

# Tell CMake where to find minizip headers
# include_directories(${CMAKE_SOURCE_DIR}/minizip-reduced/include)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.ui
        mainwindow.h
        mainwindow.cpp
        src/DictionaryFile.cpp
        src/DictionaryFile.h
        src/DictionaryWidget.cpp
        src/DictionaryWidget.h
        src/draglistwidget.cpp
        src/draglistwidget.h
        src/texteditwidget.cpp
        src/texteditwidget.h
        src/zhoutilities.h
        src/zhoutilities.cpp
        pdfium/PdfExtractWorker.cpp
)

# Windows application icon
set(APP_ICON_RESOURCE_WINDOWS
        "${CMAKE_CURRENT_SOURCE_DIR}/zhoconverterqt.rc"
)

# Application sources
set(APP_SOURCES
        # Main application
        ${PROJECT_SOURCES}

        # Office document conversion
        src/OfficeConverter.hpp
        src/OfficeConverterMinizip.hpp
        src/ZipPathUtils.hpp

        # Document extraction
        src/EpubHelper.h
        src/EpubHelper.cpp
        src/OpenXmlHelper.h
        src/OpenXmlHelper.cpp

        # PDF extraction and reflow
        pdfium/PdfiumHelper.hpp
        pdfium/ReflowHelper.hpp
        pdfium/PdfExtractWorker.h
        pdfium/batchworker.h
        pdfium/batchworker.cpp
        pdfium/PunctSets.hpp
        pdfium/CjkText.hpp

        # Text encoding and detection
        src/EncodingDetector.h
        src/EncodingDetector.cpp
        src/TextEncoding.h
        src/TextEncoding.cpp
        uchardet_trimmed/uchardet/ChineseLegacyDetector.h
        uchardet_trimmed/uchardet/ChineseLegacyDetector.cpp

        # File utilities
        src/filetype_utils.h
        src/filetype_utils.cpp

        # Dialogs
        src/AboutDialog.cpp
)

# Qt resources
qt_add_resources(APP_RESOURCES Resource.qrc)

# Application target
qt_add_executable(ZhoConverterQt
        ${APP_SOURCES}
        ${APP_RESOURCES}
        ${APP_ICON_RESOURCE_WINDOWS}
)

if (APPLE)
    set_target_properties(ZhoConverterQt PROPERTIES
            BUILD_RPATH "@executable_path/../Frameworks"
            INSTALL_RPATH "@executable_path/../Frameworks"
    )
endif ()

set_target_properties(ZhoConverterQt
        PROPERTIES
        WIN32_EXECUTABLE TRUE
        MACOSX_BUNDLE TRUE
)

target_link_libraries(ZhoConverterQt
        PUBLIC
        Qt::Core
        Qt::Gui
        Qt::Widgets
        Qt::Concurrent
)

if (NOT WIN32)
    target_link_libraries(ZhoConverterQt PRIVATE Iconv::Iconv)
endif ()
if (MSVC)
    #	target_link_libraries(ZhoConverterQt PRIVATE zip zlib)
    target_link_libraries(ZhoConverterQt PRIVATE libzip::zip)
    target_link_libraries(ZhoConverterQt PRIVATE MINIZIP::minizip-ng)
else ()
    target_link_libraries(ZhoConverterQt PRIVATE libzip::zip)
    target_link_libraries(ZhoConverterQt PRIVATE MINIZIP::minizip-ng)
    #target_link_libraries(ZhoConverterQt
    # PRIVATE
    # ${CMAKE_SOURCE_DIR}/minizip-reduced/lib/libminizip.dll.a
    # ${CMAKE_SOURCE_DIR}/minizip-reduced/lib/libzlib.dll.a
    #)
endif ()
# Link static libraries manually

# (Optional) Copy DLL to build dir post-build
#add_custom_command(TARGET ZhoConverterQt POST_BUILD
#        COMMAND ${CMAKE_COMMAND} -E copy_if_different
#        ${CMAKE_SOURCE_DIR}/minizip-reduced/bin/libzlib1.dll
#        $<TARGET_FILE_DIR:ZhoConverterQt>
#        COMMAND ${CMAKE_COMMAND} -E copy_if_different
#        ${CMAKE_SOURCE_DIR}/minizip-reduced/bin/libminizip.dll
#        $<TARGET_FILE_DIR:ZhoConverterQt>
#)

# Conditionally link the appropriate DLL file based on the operating system
if (WIN32)
    if (MSVC)
        target_link_libraries(ZhoConverterQt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/opencc-fmmseg/opencc_fmmseg_capi.dll.lib")
    else ()
        target_link_libraries(ZhoConverterQt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/opencc-fmmseg/opencc_fmmseg_capi.dll")
    endif ()
    # ---- PDFium (MinGW + MSVC both link to pdfium.lib) ----
    target_link_libraries(ZhoConverterQt PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/pdfium/pdfium.dll.lib"
    )
elseif (APPLE)
    find_program(ZHO_INSTALL_NAME_TOOL NAMES install_name_tool)
    find_program(ZHO_CODESIGN NAMES codesign)
    if (NOT ZHO_INSTALL_NAME_TOOL OR NOT ZHO_CODESIGN)
        message(FATAL_ERROR "macOS builds require install_name_tool and codesign")
    endif ()

    # Link against build-local copies with portable IDs; never modify SDK files.
    function(zho_stage_macos_library target source filename)
        set(staged "${CMAKE_CURRENT_BINARY_DIR}/native-runtime/${filename}")
        add_custom_command(OUTPUT "${staged}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/native-runtime"
                COMMAND ${CMAKE_COMMAND} -E copy "${source}" "${staged}"
                COMMAND "${ZHO_INSTALL_NAME_TOOL}" -id "@rpath/${filename}" "${staged}"
                COMMAND "${ZHO_CODESIGN}" --force --sign - "${staged}"
                DEPENDS "${source}"
                VERBATIM
        )
        add_custom_target(${target}_runtime DEPENDS "${staged}")
        add_library(${target} SHARED IMPORTED)
        set_target_properties(${target} PROPERTIES
                IMPORTED_LOCATION "${staged}"
                IMPORTED_SONAME "@rpath/${filename}")
        add_dependencies(${target} ${target}_runtime)
        target_link_libraries(ZhoConverterQt PRIVATE ${target})
    endfunction()

    zho_stage_macos_library(zho_opencc
            "${CMAKE_CURRENT_SOURCE_DIR}/opencc-fmmseg/libopencc_fmmseg_capi.dylib"
            libopencc_fmmseg_capi.dylib)
    zho_stage_macos_library(zho_pdfium
            "${CMAKE_CURRENT_SOURCE_DIR}/pdfium/libpdfium.dylib"
            libpdfium.dylib)
    add_custom_command(TARGET ZhoConverterQt POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ZhoConverterQt>/../Frameworks"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:zho_opencc>" "$<TARGET_FILE:zho_pdfium>"
            "$<TARGET_FILE_DIR:ZhoConverterQt>/../Frameworks"
            COMMAND "${ZHO_CODESIGN}" --force --deep --sign - "$<TARGET_BUNDLE_DIR:ZhoConverterQt>"
            VERBATIM
    )
elseif (UNIX)
    target_link_libraries(ZhoConverterQt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/opencc-fmmseg/libopencc_fmmseg_capi.so")
    # ---- PDFium ----
    target_link_libraries(ZhoConverterQt PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/pdfium/libpdfium.so"
    )
endif ()

# UcharDet trimmed
add_library(uchardet_trimmed STATIC
        uchardet_trimmed/uchardet/ChineseLegacyDetector.cpp
        uchardet_trimmed/uchardet/CharDistribution.cpp
        uchardet_trimmed/uchardet/JpCntx.cpp
        uchardet_trimmed/uchardet/nsBig5Prober.cpp
        uchardet_trimmed/uchardet/nsGB2312Prober.cpp
        uchardet_trimmed/uchardet/nsSJISProber.cpp
        uchardet_trimmed/uchardet/nsMBCSSM.cpp
)

target_include_directories(uchardet_trimmed PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/uchardet_trimmed/uchardet
)

target_compile_features(uchardet_trimmed PUBLIC cxx_std_17)

target_link_libraries(ZhoConverterQt PRIVATE
        uchardet_trimmed
)

# Vendored libxml2 2.15.4 — static XML + tolerant HTML parser.
set(BUILD_SHARED_LIBS OFF)

set(LIBXML2_WITH_HTML ON)
set(LIBXML2_WITH_ICONV OFF)
set(LIBXML2_WITH_MODULES OFF)
set(LIBXML2_WITH_CATALOG OFF)
set(LIBXML2_WITH_DEBUG OFF)

set(LIBXML2_WITH_PROGRAMS OFF)
set(LIBXML2_WITH_TESTS OFF)
set(LIBXML2_WITH_PYTHON OFF)
set(LIBXML2_WITH_DOCS OFF)

add_subdirectory(vcpkg/libxml2_minimal)

target_link_libraries(ZhoConverterQt PRIVATE
        LibXml2::LibXml2
)

if (WIN32)
    # The repository DLLs are needed both for local runs and installed builds.
    add_custom_command(TARGET ZhoConverterQt POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/opencc-fmmseg/opencc_fmmseg_capi.dll"
            "${CMAKE_CURRENT_SOURCE_DIR}/pdfium/pdfium.dll"
            "$<TARGET_FILE_DIR:ZhoConverterQt>"
            VERBATIM
    )
    # Nowide conversions are inline; suppress its MSVC compiled-library auto-link.
    target_compile_definitions(ZhoConverterQt PRIVATE BOOST_NOWIDE_NO_LIB)
    # Only the Regex and Nowide conversion headers are used by this app.
    #    target_include_directories(ZhoConverterQt PRIVATE
    #            "vcpkg/boost_1_88_0")
endif ()

# Install the application binary
install(TARGETS ZhoConverterQt
        RUNTIME DESTINATION bin
        BUNDLE DESTINATION "."  # Only meaningful on macOS
)

# Install external dependency DLLs or shared libraries
if (WIN32)
    install(FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/opencc-fmmseg/opencc_fmmseg_capi.dll"
            "${CMAKE_CURRENT_SOURCE_DIR}/pdfium/pdfium.dll"
            DESTINATION bin
    )
elseif (APPLE)
    install(FILES
            "$<TARGET_FILE:zho_opencc>"
            "$<TARGET_FILE:zho_pdfium>"
            DESTINATION "ZhoConverterQt.app/Contents/Frameworks"
    )
    # Installation may edit the executable's RPATH, so refresh its signature.
    install(CODE "
        execute_process(
            COMMAND \"${ZHO_CODESIGN}\" --force --deep --sign -
                \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/ZhoConverterQt.app\"
            RESULT_VARIABLE _zho_sign_result)
        if(NOT _zho_sign_result EQUAL 0)
            message(FATAL_ERROR \"Signing installed macOS bundle failed\")
        endif()
    ")
elseif (UNIX)
    install(FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/opencc-fmmseg/libopencc_fmmseg_capi.so"
            "${CMAKE_CURRENT_SOURCE_DIR}/pdfium/libpdfium.so"
            DESTINATION bin
    )
endif ()

# Option to deploy Qt DLLs with windeployqt
option(DEPLOY_WITH_WINDEPLOYQT "Run windeployqt after install" OFF)

if (WIN32 AND NOT CMAKE_CROSSCOMPILING AND DEPLOY_WITH_WINDEPLOYQT)
    find_program(WINDEPLOYQT_EXECUTABLE
            NAMES windeployqt
            REQUIRED
    )

    message(STATUS "windeployqt: ${WINDEPLOYQT_EXECUTABLE}")

    install(CODE "
        message(STATUS \"Running windeployqt...\")

        execute_process(
            COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
                --release
                --no-translations
                --no-quick-import
                --compiler-runtime
                --dir \"${CMAKE_INSTALL_PREFIX}/bin\"
                \"${CMAKE_INSTALL_PREFIX}/bin/ZhoConverterQt.exe\"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE output
            ERROR_VARIABLE error
        )

        message(STATUS \"windeployqt output: \${output}\")

        if(NOT result EQUAL 0)
            message(FATAL_ERROR
                \"windeployqt failed with exit code \${result}: \${error}\")
        endif()
    ")
endif ()

# Install MinGW runtime DLLs from the selected compiler toolchain.
if (MINGW)
    get_filename_component(_zho_mingw_bin
            "${CMAKE_CXX_COMPILER}" DIRECTORY)

    install(FILES
            "${_zho_mingw_bin}/libwinpthread-1.dll"
            "${_zho_mingw_bin}/libgcc_s_seh-1.dll"
            "${_zho_mingw_bin}/libstdc++-6.dll"
            DESTINATION bin
    )
endif ()

option(ZHO_BUILD_TESTS "Build batch text encoding integration tests" OFF)
if (ZHO_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    set(_zho_encoding_test_sources ${PROJECT_SOURCES})
    list(REMOVE_ITEM _zho_encoding_test_sources main.cpp)
    qt_add_executable(zho_batch_encoding_tests
            tests/batch_encoding_test.cpp ${_zho_encoding_test_sources} ${SOURCES} ${HEADERS})
    set_target_properties(zho_batch_encoding_tests PROPERTIES WIN32_EXECUTABLE FALSE)
    target_include_directories(zho_batch_encoding_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(zho_batch_encoding_tests PRIVATE
            $<TARGET_PROPERTY:ZhoConverterQt,LINK_LIBRARIES> Qt6::Test)
    add_dependencies(zho_batch_encoding_tests ZhoConverterQt)
    if (WIN32)
        target_compile_definitions(zho_batch_encoding_tests PRIVATE BOOST_NOWIDE_NO_LIB)
        add_custom_command(TARGET zho_batch_encoding_tests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/opencc-fmmseg/opencc_fmmseg_capi.dll"
                "${CMAKE_CURRENT_SOURCE_DIR}/pdfium/pdfium.dll"
                "$<TARGET_FILE_DIR:zho_batch_encoding_tests>"
                VERBATIM)
    elseif (APPLE)
        set_target_properties(zho_batch_encoding_tests PROPERTIES
                BUILD_RPATH "@executable_path;@executable_path/../Frameworks")
        add_custom_command(TARGET zho_batch_encoding_tests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:zho_opencc>" "$<TARGET_FILE:zho_pdfium>"
                "$<TARGET_FILE_DIR:zho_batch_encoding_tests>"
                VERBATIM)
    endif ()
    add_test(NAME batch_encoding COMMAND zho_batch_encoding_tests -platform offscreen)
endif ()
