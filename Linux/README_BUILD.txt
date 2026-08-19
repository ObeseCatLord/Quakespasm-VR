Quakespasm VR Linux Build Instructions (Cross-compiling for Windows)
======================================================================

To build the Windows version of Quakespasm VR on Linux, you must use a
combination of MinGW-w64 (for C code) and Clang (for C++ code).

IMPORTANT: Using MinGW's g++ for the C++ VR components (vr.c) causes a severe
ABI mismatch with the official OpenVR DLL on Windows x64, leading to crashes
when entering VR. Using Clang++ for the C++ parts resolves this as it handles
the Microsoft x64 calling convention for struct returns correctly.

Prerequisites
-------------
- MinGW-w64 cross-compiler (x86_64-w64-mingw32-gcc)
- Clang compiler (clang++)
- SDL2 development libraries for Windows (MinGW version)

Build Steps
-----------
1. Navigate to the 'Quake' directory.
2. Use the provided cross-compilation scripts:

   For 64-bit Windows:
   ./build_cross_win64-sdl2.sh

   For 32-bit Windows:
   ./build_cross_win32-sdl2.sh

The resulting 'quakespasm.exe' will be generated in the 'Quake' directory.

Manual Build Command (Example)
------------------------------
If you prefer to run make directly:

make USE_SDL2=1 \
     CC=x86_64-w64-mingw32-gcc \
     CXX=x86_64-w64-mingw32-clang++ \
     -f Makefile.w64
