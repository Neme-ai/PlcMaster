QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

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

win32 {
    SNAP7_DLL = $$shell_path($$PWD/thirdparty/snap7/bin/x64/snap7.dll)
    QSS_FILE  = $$shell_path($$PWD/style/light.qss)

    # 输出目录：优先使用 DESTDIR（如果你设置了），否则沿用 OUT_PWD/debug|release
    isEmpty(DESTDIR) {
        CONFIG(debug, debug|release) {
            APP_DIR = $$shell_path($$OUT_PWD/debug)
        } else {
            APP_DIR = $$shell_path($$OUT_PWD/release)
        }
    } else {
        APP_DIR = $$shell_path($$DESTDIR)
    }

    message(SNAP7_DLL=$$SNAP7_DLL)
    message(QSS_FILE=$$QSS_FILE)
    message(APP_DIR=$$APP_DIR)

    QMAKE_POST_LINK += $$quote(if not exist \"$$APP_DIR\" mkdir \"$$APP_DIR\") $$escape_expand(\\n\\t)
    QMAKE_POST_LINK += $$quote(copy /y \"$$SNAP7_DLL\" \"$$APP_DIR\\snap7.dll\") $$escape_expand(\\n\\t)
    QMAKE_POST_LINK += $$quote(copy /y \"$$QSS_FILE\"  \"$$APP_DIR\\light.qss\") $$escape_expand(\\n\\t)
    QMAKE_POST_LINK += $$quote(dir \"$$APP_DIR\\snap7.dll\") $$escape_expand(\\n\\t)
    QMAKE_POST_LINK += $$quote(dir \"$$APP_DIR\\light.qss\")
}

DISTFILES += \
    style/light.qss


