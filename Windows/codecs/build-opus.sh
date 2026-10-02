#!/bin/sh
set -eu

usage() { echo "usage: $0 archive_path new_output_dir" >&2; }
[ "$#" -eq 2 ] || { usage; exit 2; }
archive_arg=$1
output_arg=$2
case "$archive_arg" in /*) ;; *) archive_arg=./$archive_arg ;; esac
case "$output_arg" in /*) ;; *) output_arg=./$output_arg ;; esac
archive_parent=$(CDPATH= cd -P "$(dirname "$archive_arg")" && pwd)
archive=$archive_parent/$(basename "$archive_arg")
[ -f "$archive" ] && [ -r "$archive" ] || { echo "archive is not readable: $archive" >&2; exit 1; }
output_parent=$(CDPATH= cd -P "$(dirname "$output_arg")" && pwd)
output_base=$(basename "$output_arg")
case "$output_base" in ''|.|..|/) echo "invalid output directory" >&2; exit 1 ;; esac
out=$output_parent/$output_base
if ls -d "$out" >/dev/null 2>&1; then
    echo "output already exists: $out" >&2
    exit 1
fi
if command -v sha256sum >/dev/null 2>&1; then
    hash_line=$(sha256sum < "$archive")
elif command -v shasum >/dev/null 2>&1; then
    hash_line=$(shasum -a 256 < "$archive")
else
    echo "sha256sum or shasum is required" >&2
    exit 1
fi
set -- $hash_line
[ "${1:-}" = 65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1 ] || {
    echo "official Opus 1.5.2 archive SHA-256 mismatch" >&2
    exit 1
}
mkdir "$out"
source_root=$out/source/opus-1.5.2
build_dir=$out/build
products=$out/products
tmp_dir=$out/tmp
mkdir "$out/source" "$build_dir" "$products" "$tmp_dir"
tar -xzf "$archive" -C "$out/source"
[ -d "$source_root" ] || { echo "archive lacks opus-1.5.2/" >&2; exit 1; }
if ! (
    cd "$build_dir"
    set -- --host=x86_64-w64-mingw32 --enable-shared --disable-static --disable-doc --disable-extra-programs --disable-dred --disable-osce
    CFLAGS='-O2 -g0 -mcrtdll=msvcrt-os' LDFLAGS='-static-libgcc -mcrtdll=msvcrt-os' TMPDIR="$tmp_dir" CCACHE_DISABLE=1 "$source_root/configure" "$@" > "$out/configure.log" 2>&1
); then
    echo "configure failed; see $out/configure.log" >&2
    exit 1
fi
if ! (cd "$build_dir" && make -j2 libopus.la > "$out/make.log" 2>&1); then
    echo "make failed; see $out/make.log" >&2
    exit 1
fi
cp "$build_dir/.libs/libopus-0.dll" "$products/libopus-0.dll"
cp "$build_dir/.libs/libopus.dll.a" "$products/libopus.dll.a"
cp "$source_root/COPYING" "$products/COPYING"
