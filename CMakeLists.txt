cmake_minimum_required(VERSION 3.21)

project(OpenCVPractice
    VERSION 1.0
    LANGUAGES CXX
)


# =========================
# C++标准
# =========================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# =========================
# Qt
# =========================

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core
    Widgets
)


# =========================
# OpenCV
# =========================

find_package(OpenCV REQUIRED)


qt_standard_project_setup()


# =========================
# 源文件
# =========================

qt_add_executable(OpenCVPractice

    WIN32

    main.cpp

    mainwindow.cpp
    mainwindow.h

    mainwindow.ui
    opencvhelper.h opencvhelper.cpp
    fluentstyle.h
)


# =========================
# 头文件路径
# =========================

target_include_directories(OpenCVPractice
    PRIVATE
        src
)


# =========================
# 链接库
# =========================

target_link_libraries(OpenCVPractice

    PRIVATE

        Qt::Core
        Qt::Widgets

        ${OpenCV_LIBS}

)


# =========================
# 输出目录
# =========================

set_target_properties(OpenCVPractice
    PROPERTIES

    RUNTIME_OUTPUT_DIRECTORY
    ${CMAKE_BINARY_DIR}/bin

)
