cmake_minimum_required(VERSION 3.12)

project(shipedit)

set(NATIVEFILEDIALOG_SOURCES
    src/nativefiledialog/src/nfd_common.c
)

set(NATIVE_LIBRARIES "")
set(NFD_LIBRARIES "")
set(LIBRARY_TYPE "SHARED")

if(WIN32)
    set(LIBRARY_TYPE "STATIC")

    if(MSVC)
      include_directories(${CMAKE_CURRENT_SOURCE_DIR}/msvc/include)
      include_directories(${CMAKE_CURRENT_SOURCE_DIR}/msvc/include/SDL2)
      link_directories(${CMAKE_CURRENT_SOURCE_DIR}/msvc/lib)

      set(PNG_LIBRARY libpng16)
      set(ZLIB_LIBRARY zlib)
      set(SDL2_LIBRARIES SDL2main SDL2)
    else()
      include_directories(${CMAKE_CURRENT_BINARY_DIR}/../include)
      include_directories(${CMAKE_CURRENT_BINARY_DIR}/../include/SDL2)
      link_directories(${CMAKE_CURRENT_BINARY_DIR}/../lib)

      set(PNG_LIBRARY png)
      set(ZLIB_LIBRARY z)
      set(SDL2_LIBRARIES mingw32 SDL2main SDL2 dxguid user32 gdi32 winmm imm32 ole32 oleaut32 shell32 setupapi version uuid)
      list(APPEND NATIVE_LIBRARIES -static -static-libgcc -static-libstdc++ z)
    endif()

    list(APPEND NATIVEFILEDIALOG_SOURCES
        src/nativeui_win32.c
    )
else()
    find_package(SDL2 REQUIRED)
    find_package(PNG REQUIRED)
    find_package(ZLIB REQUIRED)

    if(APPLE)
        list(APPEND NATIVEFILEDIALOG_SOURCES
            src/nativefiledialog/src/nfd_cocoa.m
        )
        find_library(COCOA_LIBRARY Cocoa)
        list(APPEND NFD_LIBRARIES ${COCOA_LIBRARY})
    else()
        list(APPEND NATIVEFILEDIALOG_SOURCES
            src/nativefiledialog/src/nfd_gtk.c
        )
    endif()

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

    link_directories(
        ${GTK3_LIBRARY_DIRS}
    )

    list(APPEND NATIVEFILEDIALOG_SOURCES
        src/nativeui_linux.c
    )
    list(APPEND NFD_LIBRARIES ${GTK3_LIBRARIES})

    list(APPEND NATIVE_LIBRARIES m)
endif()

find_package(OpenGL)

include_directories(
    ${PNG_INCLUDE_DIR}
    ${SDL2_INCLUDE_DIRS}
    ${GTK3_INCLUDE_DIRS}
    src/psp-save
    src/libkirk
    src/scolorq
    src/fontaine
    src/nativefiledialog/src/include
)

add_library(saveskin ${LIBRARY_TYPE}
    src/saveskin.c
    src/psp-save/psp-save.c
    src/psp-save/chnnlsv.c
    src/psp-save/hash.c
    src/psp-save/psf.c
    src/libkirk/AES.c
    src/libkirk/bn.c
    src/libkirk/ec.c
    src/libkirk/kirk_engine.c
    src/libkirk/SHA1.c
)

add_library(scolorq ${LIBRARY_TYPE}
    src/scolorq/spatial_color_quant.cpp
)

add_executable(shipedit
    src/shipedit.c
    src/fileio.c
    src/util.c
    src/fontaine/fontaine2.c
    ${NATIVEFILEDIALOG_SOURCES}
)

target_link_libraries(shipedit
    saveskin
    scolorq
    ${PNG_LIBRARY}
    ${ZLIB_LIBRARY}
    ${SDL2_LIBRARIES}
    ${NATIVE_LIBRARIES}
    ${OPENGL_LIBRARIES}
    ${NFD_LIBRARIES}
)
