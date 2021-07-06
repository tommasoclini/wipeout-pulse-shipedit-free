#!/bin/bash

cd "$(dirname "$0")"

set -e -x

VERSION="1.0.1"

build_to() {
    OUTDIR=$1
    TOOLCHAIN=$2
    HOSTARCH=$3
    LIBDEP=$4

    test -f zlib-1.2.11.tar.gz || wget https://zlib.net/zlib-1.2.11.tar.gz
    test -f libpng-1.6.37.tar.xz || wget http://prdownloads.sourceforge.net/libpng/libpng-1.6.37.tar.xz
    test -f SDL2-2.0.14.tar.gz || wget https://www.libsdl.org/release/SDL2-2.0.14.tar.gz

    mkdir -p $OUTDIR
    cd $OUTDIR

    test -d zlib-1.2.11 || tar xvf ../zlib-1.2.11.tar.gz
    test -f include/zlib.h || (
        cd zlib-1.2.11
        PREFIXDIR=..
        make -j8 -f win32/Makefile.gcc \
            BINARY_PATH=$PREFIXDIR/bin \
            INCLUDE_PATH=$PREFIXDIR/include \
            LIBRARY_PATH=$PREFIXDIR/lib \
            SHARED_MODE=1 PREFIX=$HOSTARCH- install
        cd ..
    )

    test -d libpng-1.6.37 || tar xvf ../libpng-1.6.37.tar.xz
    test -f include/png.h || (
        cd libpng-1.6.37
        ./configure --prefix=$(pwd)/.. --host=$HOSTARCH LDFLAGS=-L$(pwd)/../lib CPPFLAGS=-I$(pwd)/../include
        make -j8 install
        cd ..
    )

    test -d SDL2-2.0.14 || tar xvf ../SDL2-2.0.14.tar.gz
    test -f include/SDL2/SDL.h || (
        cd SDL2-2.0.14
        ./configure --prefix=$(pwd)/.. --host=$HOSTARCH LDFLAGS=-L$(pwd)/../lib CPPFLAGS=-I$(pwd)/../include
        make -j8 install
        cd ..
    )

    mkdir -p build
    cd build
    cmake -DCMAKE_TOOLCHAIN_FILE=../../$TOOLCHAIN -DDCMAKE_BUILD_TYPE=Release ../../..
    make -j8

    $HOSTARCH-strip -s *.exe

    cp shipedit.exe ../../../shipedit-$VERSION-$OUTDIR-mingw.exe

    cd ../..
}

build_to win32 mingw32.toolchain i686-w64-mingw32
build_to win64 mingw64.toolchain x86_64-w64-mingw32
