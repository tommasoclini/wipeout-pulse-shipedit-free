#!/bin/bash

cd "$(dirname "$0")"

set -e -x

VERSION="1.0.3"

build_to() {
    OUTDIR=$1
    TOOLCHAIN=$2
    HOSTARCH=$3
    LIBDEP=$4

    ZLIB_VERSION="1.2.13"
    LIBPNG_VERSION="1.6.39"
    SDL2_VERSION="2.26.1"

    test -f zlib-$ZLIB_VERSION.tar.gz || wget https://zlib.net/fossils/zlib-$ZLIB_VERSION.tar.gz
    test -f libpng-$LIBPNG_VERSION.tar.xz || wget http://prdownloads.sourceforge.net/libpng/libpng-$LIBPNG_VERSION.tar.xz
    test -f SDL2-$SDL2_VERSION.tar.gz || wget https://www.libsdl.org/release/SDL2-$SDL2_VERSION.tar.gz

    mkdir -p $OUTDIR
    cd $OUTDIR

    test -d zlib-$ZLIB_VERSION || tar xvf ../zlib-$ZLIB_VERSION.tar.gz
    test -f include/zlib.h || (
        cd zlib-$ZLIB_VERSION
        PREFIXDIR=..
        make -j8 -f win32/Makefile.gcc \
            BINARY_PATH=$PREFIXDIR/bin \
            INCLUDE_PATH=$PREFIXDIR/include \
            LIBRARY_PATH=$PREFIXDIR/lib \
            SHARED_MODE=1 PREFIX=$HOSTARCH- install
        cd ..
    )

    test -d libpng-$LIBPNG_VERSION || tar xvf ../libpng-$LIBPNG_VERSION.tar.xz
    test -f include/png.h || (
        cd libpng-$LIBPNG_VERSION
        ./configure --prefix=$(pwd)/.. --host=$HOSTARCH LDFLAGS=-L$(pwd)/../lib CPPFLAGS=-I$(pwd)/../include
        make -j8 install
        cd ..
    )

    test -d SDL2-$SDL2_VERSION || tar xvf ../SDL2-$SDL2_VERSION.tar.gz
    test -f include/SDL2/SDL.h || (
        cd SDL2-$SDL2_VERSION
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

    pushd ../../..
    zip shipedit-$VERSION-$OUTDIR-mingw.zip README LICENSE CHANGELOG.md editor.wad shipedit-$VERSION-$OUTDIR-mingw.exe
    popd

    cd ../..
}

build_to win32 mingw32.toolchain i686-w64-mingw32
build_to win64 mingw64.toolchain x86_64-w64-mingw32
