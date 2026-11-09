QT += core gui network testlib
CONFIG += c++17 testcase
TARGET = warmup_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/metadata.h ../src/settings.h ../src/songstore.h ../src/theme.h ../src/translator.h
SOURCES += warmup_tests.cpp ../src/backend.cpp ../src/metadata.cpp ../src/settings.cpp ../src/songstore.cpp ../src/theme.cpp ../src/translator.cpp
