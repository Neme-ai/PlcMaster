QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11
win32-msvc*: QMAKE_CXXFLAGS += /utf-8

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    plcsnap7client.cpp \
    plcworker.cpp

HEADERS += \
    mainwindow.h \
    plcsnap7client.h \
    plcworker.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target


INCLUDEPATH += $$PWD/thirdparty/snap7/include
LIBS += -L$$PWD/thirdparty/snap7/lib/x64 -lsnap7 -lWs2_32

# Copy runtime files beside the executable for both Debug and Release.

win32 {
    SNAP7_DLL = $$PWD/thirdparty/snap7/bin/x64/snap7.dll
    APP_STYLE = $$PWD/style/light.qss

    CONFIG(debug, debug|release) {
        APP_DIR = $$OUT_PWD/debug
    } else {
        APP_DIR = $$OUT_PWD/release
    }

    # qmake concatenates += values with spaces; explicit newlines keep commands separate.
    QMAKE_POST_LINK += $$QMAKE_COPY $$shell_quote($$shell_path($$SNAP7_DLL)) $$shell_quote($$shell_path($$APP_DIR/snap7.dll)) $$escape_expand(\\n\\t)
    QMAKE_POST_LINK += $$QMAKE_COPY $$shell_quote($$shell_path($$APP_STYLE)) $$shell_quote($$shell_path($$APP_DIR/light.qss))
}

DISTFILES += \
    style/light.qss




RESOURCES += points.qrc
