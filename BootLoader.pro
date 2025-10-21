# Qt模块配置
QT += core gui widgets serialport

# C++标准
CONFIG += c++17

# 目标名称和模板
TARGET = BootLoader
TEMPLATE = app
RC_ICONS = ui/com.ico

# 源文件
SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp

# 头文件
HEADERS += \
    inc/mainwindow.h

# UI文件
FORMS += \
    ui/mainwindow.ui



