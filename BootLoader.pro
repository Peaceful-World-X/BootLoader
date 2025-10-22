# Qt模块配置
QT += core gui widgets serialport network

# C++标准
CONFIG += c++17

# 目标名称和模板
TARGET = BootLoader
TEMPLATE = app
RC_ICONS = gui/com.ico

# 源文件
SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/protocol.cpp \
    src/communication.cpp \
    src/upgrade.cpp

# 头文件
HEADERS += \
    inc/mainwindow.h \
    inc/protocol.h \
    inc/communication.h \
    inc/upgrade.h

# UI文件
FORMS += \
    gui/mainwindow.ui



