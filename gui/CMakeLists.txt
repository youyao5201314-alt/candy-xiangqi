cmake_minimum_required(VERSION 3.16)
project(candyboard_gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 自动处理 moc/uic/rcc
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 找Qt
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# 源码
set(SOURCES
    main.cpp
    MainWindow.cpp
    board.cpp
)

set(HEADERS
    MainWindow.h
    board.h
)

set(FORMS
    MainWindow.ui
)

add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS} ${FORMS})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "gui")

target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

# MSVC UTF-8
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8 /Zc:__cplusplus)
endif()
