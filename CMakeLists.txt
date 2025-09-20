cmake_minimum_required(VERSION 3.16)
if (WIN32)
    set(CMAKE_TOOLCHAIN_FILE "D:/lib/vcpkg/scripts/buildsystems/vcpkg.cmake")
    if (MSVC)
        set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "" FORCE)
        set(VCPKG_HOST_TRIPLET "x64-windows-static-md" CACHE STRING "" FORCE)
    else ()
        set(VCPKG_TARGET_TRIPLET "x64-mingw-static" CACHE STRING "" FORCE)
        set(VCPKG_HOST_TRIPLET "x64-mingw-static" CACHE STRING "" FORCE)
    endif ()
elseif (UNIX)
    set(CMAKE_TOOLCHAIN_FILE "/home/bryan/lib/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(VCPKG_TARGET_TRIPLET "x64-linux" CACHE STRING "" FORCE)
    set(VCPKG_HOST_TRIPLET "x64-linux" CACHE STRING "" FORCE)
endif ()

project(ZhoConverterQt LANGUAGES CXX)

include(qt.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Install path" FORCE)

if (WIN32)
    if (MSVC)
        set(CMAKE_PREFIX_PATH "C:/QT/6.8.3/MSVC2022_64")
        list(APPEND CMAKE_PREFIX_PATH "D:/lib/vcpkg/installed/x64-windows-static-md")
        link_directories("D:/lib/boost_1_88_0/stage/lib")
        include_directories("D:/lib/vcpkg/installed/x64-windows-static-md/include")
        # add_compile_options(/Zc:__cplusplus)  # Ensure __cplusplus is correctly set
    else ()
        set(CMAKE_PREFIX_PATH "C:/Qt/6.9.2/mingw_64")
        list(APPEND CMAKE_PREFIX_PATH "D:/lib/vcpkg/installed/x64-mingw-static")
        include_directories("D:/lib/vcpkg/installed/x64-mingw-static/include")
    endif ()
    # Include for Boost C Library
    include_directories("D:/lib/boost_1_88_0")

elseif (APPLE)
    set(CMAKE_PREFIX_PATH "/Users/bryan/Qt/6.8.3/macos")

elseif (UNIX)
    set(CMAKE_PREFIX_PATH "/opt/Qt/6.8.3/gcc_64")
    list(APPEND CMAKE_PREFIX_PATH "/home/bryan/lib/vcpkg/installed/x64-linux")
    include_directories("/home/bryan/lib/vcpkg/installed/x64-linux/include")
    include_directories("/home/bryan/lib")
    set(CMAKE_BUILD_RPATH "$ORIGIN")
endif ()

set(QT_DIR "${CMAKE_PREFIX_PATH}/lib/cmake/Qt6")

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR}
        COMPONENTS
        Core
        Gui
        Widgets
)
qt_standard_project_setup()
#set(libzip_DIR "D:/lib/vcpkg/packages/libzip_x64-windows-static/share/libzip")

find_package(libzip CONFIG REQUIRED)
find_package(minizip-ng CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/opencc-fmmseg)

# Tell CMake where to find minizip headers
# include_directories(${CMAKE_SOURCE_DIR}/minizip-reduced/include)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.ui
        mainwindow.h
        mainwindow.cpp
        src/draglistwidget.cpp
        src/draglistwidget.h
        src/texteditwidget.cpp
        src/texteditwidget.h
        src/zhoutilities.h
        src/zhoutilities.cpp
)

set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/zhoconverterqt.rc")
set(HEADERS
        src/OfficeConverter.hpp
        src/OfficeConverterMinizip.hpp
        src/ZipPathUtils.hpp
)
qt_add_resources(SOURCES Resource.qrc)
qt_add_executable(ZhoConverterQt
        ${PROJECT_SOURCES}
        ${APP_ICON_RESOURCE_WINDOWS}
        ${SOURCES}
        ${HEADERS}
)

set_target_properties(ZhoConverterQt
        PROPERTIES
        WIN32_EXECUTABLE TRUE
)

target_link_libraries(ZhoConverterQt
        PUBLIC
        Qt::Core
        Qt::Gui
        Qt::Widgets
)
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
    target_link_libraries(ZhoConverterQt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/opencc-fmmseg/opencc_fmmseg_capi.dll.lib")
    #    target_link_libraries(ZhoConverterQt PUBLIC "D:/codes/lib/vcpkg/vcpkg/packages/libzip_x64-windows/lib/zip.lib")
elseif (APPLE)
    target_link_libraries(ZhoConverterQt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/opencc-fmmseg/libopencc_fmmseg_capi.dylib")
elseif (UNIX)
    target_link_libraries(ZhoConverterQt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/opencc-fmmseg/libopencc_fmmseg_capi.so")
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
            DESTINATION bin
    )
elseif (APPLE)
    install(FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/opencc-fmmseg/libopencc_fmmseg_capi.dylib"
            DESTINATION "."
    )
elseif (UNIX)
    install(FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/opencc-fmmseg/libopencc_fmmseg_capi.so"
            DESTINATION bin
    )
endif ()

# Option to deploy Qt DLLs with windeployqt
option(DEPLOY_WITH_WINDEPLOYQT "Run windeployqt after install" OFF)

if (WIN32 AND NOT CMAKE_CROSSCOMPILING AND DEPLOY_WITH_WINDEPLOYQT)
    install(CODE "
        execute_process(
            COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
                --no-translations
                --no-quick-import
                --compiler-runtime
                --dir \"${CMAKE_INSTALL_PREFIX}/bin\"
                \"${CMAKE_INSTALL_PREFIX}/bin/$<TARGET_FILE_NAME:ZhoConverterQt>\"
            RESULT_VARIABLE result
        )
        if (NOT result EQUAL 0)
            message(FATAL_ERROR \"windeployqt failed with exit code \${result}\")
        endif()
    ")
endif ()
