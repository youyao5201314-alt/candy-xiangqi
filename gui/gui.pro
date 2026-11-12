QT += core gui widgets multimedia

CONFIG += c++17 utf8_source

TARGET = gui
TEMPLATE = app

# 源码文件
SOURCES += \
    main.cpp \
    MainWindow.cpp \
    board.cpp

# 头文件
HEADERS += \
    MainWindow.h \
    board.h

# UI文件
FORMS += \
    MainWindow.ui

# 音效资源（编译进 exe，用 qrc:/sounds/xxx.wav 访问）
# 形象资源（编译进 exe，用 qrc:/avatar/xxx.jpg 访问）
RESOURCES += \
    sounds.qrc \
    avatar.qrc \
    media.qrc

# MSVC编译选项
msvc {
    QMAKE_CXXFLAGS += /utf-8 /Zc:__cplusplus
    QMAKE_LFLAGS += /SUBSYSTEM:CONSOLE
}
