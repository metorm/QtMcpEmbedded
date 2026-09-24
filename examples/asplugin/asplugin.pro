QT += widgets

CONFIG += c++17

win32-msvc*: QMAKE_CXXFLAGS += /utf-8

SOURCES += \
    main.cpp \
    MainWindow.cpp

HEADERS += \
    MainWindow.h

FORMS += \
    MainWindow.ui
