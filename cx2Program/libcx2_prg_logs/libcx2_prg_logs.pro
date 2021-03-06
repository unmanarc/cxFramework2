QT       -= core gui
CONFIG   += c++11

SOURCES += \
    src/applog.cpp \
    src/logbase.cpp \
    src/rpclog.cpp \
    src/weblog.cpp

HEADERS += \
    src/logbase.h \
    src/loglevels.h \
    src/logcolors.h \
    src/logmodes.h \
    src/applog.h \
    src/rpclog.h \
    src/weblog.h

isEmpty(PREFIX) {
    PREFIX = /usr/local
}

# includes dir
QMAKE_INCDIR += $$PREFIX/include
QMAKE_INCDIR += src

INCLUDEPATH += $$PREFIX/include
INCLUDEPATH += src

# C++ standard.
include(../../cflags.pri)

TARGET = cx2_prg_logs
TEMPLATE = lib

win32:LIBS+= -L$$PREFIX/lib -lcx2_hlp_functions2

# INSTALLATION:
target.path = $$PREFIX/lib
header_files.files = $$HEADERS
header_files.path = $$PREFIX/include/$$TARGET
INSTALLS += target
INSTALLS += header_files
# PKGCONFIG
CONFIG += create_pc create_prl no_install_prl
QMAKE_PKGCONFIG_LIBDIR = $$PREFIX/lib/
QMAKE_PKGCONFIG_INCDIR = $$PREFIX/include/$$TARGET
QMAKE_PKGCONFIG_CFLAGS = -I$$PREFIX/include/
QMAKE_PKGCONFIG_DESTDIR = pkgconfig

include(../../version.pri)
