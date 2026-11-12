QT += core gui widgets multimedia network websockets

CONFIG += c++17 utf8_source

TARGET = gui
TEMPLATE = app

# 源码文件
SOURCES += \
    main.cpp \
    MainWindow.cpp \
    board.cpp \
    NetworkManager.cpp \
    StartScreen.cpp \
    api.cpp \
    LoginDialog.cpp \
    InternetClient.cpp \
    FriendsPanel.cpp \
    ProfileDialog.cpp \
    ShopDialog.cpp \
    MailDialog.cpp \
    StickerPicker.cpp \
    ChatDialog.cpp \
    AchievementsDialog.cpp \
    BagDialog.cpp \
    ProfileViewDialog.cpp \
    Inventory.cpp \
    Achievements.cpp

# 头文件
HEADERS += \
    MainWindow.h \
    board.h \
    NetworkManager.h \
    StartScreen.h \
    api.h \
    LoginDialog.h \
    ServerConfig.h \
    InternetClient.h \
    FriendsPanel.h \
    ProfileDialog.h \
    ShopDialog.h \
    MailDialog.h \
    StickerPicker.h \
    ChatDialog.h \
    AchievementsDialog.h \
    BagDialog.h \
    ProfileViewDialog.h \
    Inventory.h \
    Achievements.h

# UI文件
FORMS += \
    MainWindow.ui

# 音效资源（编译进 exe，用 qrc:/sounds/xxx.wav 访问）
# 形象资源（编译进 exe，用 qrc:/avatar/xxx.jpg 访问）
RESOURCES += \
    sounds.qrc \
    avatar.qrc \
    media.qrc \
    videos.qrc \
    stickers.qrc \
    startmusic.qrc

# Windows exe 图标（两只猫戴耳机）
win32:RC_FILE = app.rc

# MSVC编译选项
msvc {
    QMAKE_CXXFLAGS += /utf-8 /Zc:__cplusplus
    QMAKE_LFLAGS += /SUBSYSTEM:CONSOLE
}

# Android：自定义 Manifest（应用名等），目录结构与 Qt 模板一致
android {
    ANDROID_PACKAGE_SOURCE_DIR = $$PWD/android
    ANDROID_VERSION_NAME = 3.3
    ANDROID_VERSION_CODE = 33
}
