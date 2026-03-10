#!/bin/sh

# Quakespasm-OpenVR Cross-compilation script for Windows x64
# This script ensures that Clang is used for C++ components to avoid ABI issues with OpenVR.

TARGET=x86_64-w64-mingw32
# Set PREFIX only if you have a custom MinGW installation directory
# PREFIX=/opt/cross_win64

if [ -n "$PREFIX" ]; then
    PATH="$PREFIX/bin:$PATH"
    export PATH
fi

MAKE_CMD=make

# Use standard MinGW GCC for C code
CC="$TARGET-gcc"

# USE CLANG++ FOR C++ CODE TO AVOID OPENVR ABI CRASHES
# Check if a specific wrapper exists, otherwise use clang++ with the target triple
if command -v "$TARGET-clang++" >/dev/null 2>&1; then
    CXX="$TARGET-clang++"
else
    CXX="clang++ --target=$TARGET"
fi

AS="$TARGET-as"
RANLIB="$TARGET-ranlib"
AR="$TARGET-ar"
WINDRES="$TARGET-windres"
STRIP="$TARGET-strip"

# Verify compilers
if ! command -v "$CC" >/dev/null 2>&1; then
    echo "ERROR: $CC not found. Please install mingw-w64."
    exit 1
fi

# We don't check for CXX here because clang++ might be a generic binary
# but we check if clang++ itself exists if we are using the fallback
case "$CXX" in
    "clang++"*)
        if ! command -v clang++ >/dev/null 2>&1; then
            echo "ERROR: clang++ not found. Please install clang."
            exit 1
        fi
        ;;
esac

export PATH CC CXX AS AR RANLIB WINDRES STRIP

echo "Building for $TARGET using CC=$CC and CXX=$CXX..."
exec $MAKE_CMD USE_SDL2=1 CC="$CC" CXX="$CXX" AS="$AS" RANLIB="$RANLIB" AR="$AR" WINDRES="$WINDRES" STRIP="$STRIP" -f Makefile.w64 $*

