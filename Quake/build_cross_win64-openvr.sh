#!/bin/sh
# Cross-compile quakespasm-openvr.exe for Windows x64 on Linux.
#
# Two bugs historically caused the cross-compiled exe to crash on VR init:
#
#  1. Missing libgcc_s_dw2-1.dll at runtime.
#     Fixed in Makefile.w64 by -static-libgcc -static-libstdc++.
#
#  2. DWARF-2 / SEH exception-handling mismatch.
#     openvr_api.dll is an MSVC/SEH DLL. When the MinGW DWARF-2 unwinder
#     encounters SEH frames inside OpenVR during VR_Init it crashes.
#     Fixed in Makefile.w64 by -fno-exceptions -fno-rtti on the VR C++ files.
#     OpenVR returns error codes, not C++ exceptions, so this is safe.
#
# Both fixes are already in Makefile.w64, so a standard MinGW toolchain works.
# If clang++ is available it is used for C++ files as an extra layer of safety
# (clang++ targets Windows with SEH by default).
#
# Prerequisites (Debian/Ubuntu):
#   sudo apt install mingw-w64
#   sudo apt install clang          # optional but recommended
#
# Usage:
#   cd Quake
#   ./build_cross_win64-openvr.sh          # release
#   ./build_cross_win64-openvr.sh DEBUG=1  # debug

set -e

TARGET=x86_64-w64-mingw32

CC="$TARGET-gcc"
WINDRES="$TARGET-windres"
STRIP="$TARGET-strip"
AR="$TARGET-ar"
RANLIB="$TARGET-ranlib"

# Verify required tools
for tool in "$CC" "$WINDRES"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: $tool not found. Install with: sudo apt install mingw-w64" >&2
        exit 1
    fi
done

# Prefer clang++ targeting the MinGW triple (uses SEH natively).
# Fall back to mingw g++ — safe because Makefile.w64 compiles VR files
# with -fno-exceptions -fno-rtti, which eliminates the DWARF-2/SEH mismatch.
if command -v "$TARGET-clang++" >/dev/null 2>&1; then
    CXX="$TARGET-clang++"
elif command -v clang++ >/dev/null 2>&1; then
    CXX="clang++ --target=$TARGET"
else
    CXX="$TARGET-g++"
    # Warn if this g++ uses DWARF-2 exception handling. The Makefile fixes
    # prevent a crash, but a SEH-capable compiler is still preferred.
    if "$CXX" -v 2>&1 | grep -qi "dw2\|dwarf"; then
        echo "WARNING: $CXX uses DWARF-2 exceptions. Install clang++ for a safer build." >&2
        echo "         The Makefile -fno-exceptions fix should prevent VR crashes, but" >&2
        echo "         clang++ (sudo apt install clang) is the recommended toolchain." >&2
    fi
fi

echo "CC:  $CC"
echo "CXX: $CXX"
echo ""

exec make USE_SDL2=1 \
    CC="$CC" CXX="$CXX" \
    AR="$AR" RANLIB="$RANLIB" \
    WINDRES="$WINDRES" STRIP="$STRIP" \
    -f Makefile.w64 "$@"
