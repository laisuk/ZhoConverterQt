cmake_minimum_required(VERSION 3.16)
project(ZhoConverterQt LANGUAGES CXX)

include(qt.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (WIN32)
    if (MSVC)
        set(CMAKE_PREFIX_PATH "C:/QT/6.8.0/MSVC2022_64")
        # add_compile_options(/Zc:__cplusplus)  # Ensure __cplusplus is correctly set
    else ()
        set(CMAKE_PREFIX_PATH "C:/Qt/6.8.0/mingw_64")
    endif ()

elseif (APPLE)
    set(CMAKE_PREFIX_PATH "/Users/bryan/Qt/6.8.0/macos")

elseif (UNIX)
    set(CMAKE_PREFIX_PATH "/home/bryan/Qt/6.8.0/gcc_64")
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

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/opencc-fmmseg)

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
# Conditionally link the appropriate DLL file based on the operating system
if (WIN32)
    target_link_libraries(ZhoConverterQt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/opencc-fmmseg/opencc_fmmseg_capi.dll.lib")
elseif (APPLE)
    target_link_libraries(ZhoConverterQt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/opencc-fmmseg/libopencc_fmmseg_capi.dylib")
elseif (UNIX)
    target_link_libraries(ZhoConverterQt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/opencc-fmmseg/libopencc_fmmseg_capi.so")
endif ()
