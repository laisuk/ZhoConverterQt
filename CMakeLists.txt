cmake_minimum_required(VERSION 3.16)
#set(CMAKE_TOOLCHAIN_FILE D:/lib/vcpkg/scripts/buildsystems/vcpkg.cmake)
project(ZhoConverterQt LANGUAGES CXX)

include(qt.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Install path" FORCE)

if (WIN32)
    if (MSVC)
        set(CMAKE_PREFIX_PATH "C:/QT/6.9.1/MSVC2022_64")
        # add_compile_options(/Zc:__cplusplus)  # Ensure __cplusplus is correctly set
    else ()
        set(CMAKE_PREFIX_PATH "C:/Qt/6.9.1/mingw_64")
    endif ()

elseif (APPLE)
    set(CMAKE_PREFIX_PATH "/Users/bryan/Qt/6.8.3/macos")

elseif (UNIX)
    set(CMAKE_PREFIX_PATH "/home/bryan/Qt/6.8.3/gcc_64")
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
#find_package(ZLIB REQUIRED)

set(libzip_DIR "D:/lib/vcpkg/packages/libzip_x64-windows/share/libzip")
find_package(libzip CONFIG REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/opencc-fmmseg)
# Include for Boost C Library
include_directories("D:/lib/boost_1_88_0")
# Tell CMake where to find minizip headers
include_directories(${CMAKE_SOURCE_DIR}/minizip-reduced/include)

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

set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/zhoconverterqt.rc"
        src/OfficeConverter.hpp
        src/OfficeConverterMinizip.hpp)
qt_add_resources(SOURCES Resource.qrc)
qt_add_executable(ZhoConverterQt
        ${PROJECT_SOURCES}
        ${APP_ICON_RESOURCE_WINDOWS}
        ${SOURCES}
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
target_link_libraries(ZhoConverterQt PRIVATE libzip::zip)
# Link static libraries manually
target_link_libraries(ZhoConverterQt
        PRIVATE
        ${CMAKE_SOURCE_DIR}/minizip-reduced/lib/libminizip.dll.a
        #        ${CMAKE_SOURCE_DIR}/minizip-reduced/lib/libzlib.dll.a
)

# (Optional) Copy DLL to build dir post-build
add_custom_command(TARGET ZhoConverterQt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/minizip-reduced/bin/libzlib1.dll
        $<TARGET_FILE_DIR:ZhoConverterQt>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/minizip-reduced/bin/libminizip.dll
        $<TARGET_FILE_DIR:ZhoConverterQt>
)

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
