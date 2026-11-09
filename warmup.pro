QT += core gui qml quick quickcontrols2 network

CONFIG += c++17 release
TARGET = warmup
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/metadata.h \
    src/settings.h \
    src/songstore.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/metadata.cpp \
    src/settings.cpp \
    src/songstore.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
