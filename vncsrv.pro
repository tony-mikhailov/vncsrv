TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
QT -= gui
CONFIG  -= QT
CONFIG -= core
CONFIG += RELEASE

INCLUDEPATH += include

SOURCES += main.c
SOURCES += keyboard.c
SOURCES += touch.c
SOURCES += ini.c


LIBS += -Llib -lvncserver  -lresolv -lz -lgnutls -lgnutlsxx -lgnutls-openssl -lgcrypt -ljpeg -lgpg-error -ltasn1 -lp11 -lp11-kit -lnettle -lhogweed -lgmp -lgmpxx -lffi -lrt

