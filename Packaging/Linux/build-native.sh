#!/usr/bin/env bash
set -Eeuo pipefail

fail() { printf 'build-native: %s\n' "$*" >&2; exit 1; }
[[ $# == 3 ]] || fail 'usage: build-native.sh PRODUCT.tar[.gz] REVISION-40-HEX EMPTY-OUTPUT-DIR'
PRODUCT=$(realpath -e -- "$1")
REVISION=${2,,}
[[ "$REVISION" =~ ^[0-9a-f]{40}$ ]] || fail 'revision must be a 40-digit git object id'
[[ -f "$PRODUCT" ]] || fail 'product archive is not a regular file'
mkdir -p -- "$3"
OUT=$(realpath -e -- "$3")
[[ -d "$OUT" ]] || fail 'output path is not a directory'
if find "$OUT" -mindepth 1 -maxdepth 1 -print -quit | grep -q .; then
    fail 'output directory must be empty'
fi
. /etc/os-release
[[ "${ID:-}" == ubuntu && "${VERSION_ID:-}" == 24.04 ]] || fail 'builder requires Ubuntu 24.04'
MACHINE=$(uname -m)
case "$MACHINE" in
    x86_64) ARCH=x86_64; BASE_FLAGS='-march=x86-64 -mtune=generic' ;;
    aarch64) ARCH=aarch64; BASE_FLAGS='-march=armv8-a -mtune=generic' ;;
    *) fail "unsupported native architecture: $MACHINE" ;;
esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/vkquake-native.XXXXXX")
trap 'rm -rf -- "$WORK"' EXIT
case "$PRODUCT" in
    *.tar.gz|*.tgz)
        PRODUCT_SUFFIX=.tar.gz
        gzip -dc -- "$PRODUCT" > "$WORK/product.tar"
        PRODUCT_COMMIT=$(git get-tar-commit-id < "$WORK/product.tar") ;;
    *.tar) PRODUCT_SUFFIX=.tar; PRODUCT_COMMIT=$(git get-tar-commit-id < "$PRODUCT") ;;
    *) fail 'product input must be a git archive in tar or tar.gz form' ;;
esac
[[ "$PRODUCT_COMMIT" == "$REVISION" ]] || fail 'archive commit does not match the supplied revision'

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
PINS="$SCRIPT_DIR/sources.json"
[[ -s "$PINS" ]] || fail 'sources.json is missing'
PRODUCT_ROOT="$WORK/product"
DEPS="$OUT/deps"
mkdir -p "$PRODUCT_ROOT" "$WORK/git" "$WORK/src" "$WORK/build" \
    "$DEPS" "$OUT/install" "$OUT/sources/dependencies" "$OUT/sources/recipes" \
    "$OUT/sources/ubuntu/packages" "$OUT/sources/ubuntu/source" "$OUT/receipts"
: > "$OUT/receipts/options.txt"
case "$PRODUCT_SUFFIX" in
    .tar.gz) tar -xf "$WORK/product.tar" -C "$PRODUCT_ROOT" ;;
    .tar) tar -xf "$PRODUCT" -C "$PRODUCT_ROOT" ;;
esac
PRODUCT_COPY="$OUT/sources/product$PRODUCT_SUFFIX"
cp -- "$PRODUCT" "$PRODUCT_COPY"
PRODUCT_HASH=$(sha256sum "$PRODUCT_COPY" | awk '{print $1}')
[[ "$PRODUCT_HASH" == "$(sha256sum "$PRODUCT" | awk '{print $1}')" ]] || fail 'product archive changed while copying'

unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS CC CXX AR RANLIB CMAKE_PREFIX_PATH CMAKE_GENERATOR \
    CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS \
    CMAKE_MODULE_LINKER_FLAGS CMAKE_TOOLCHAIN_FILE PKG_CONFIG_PATH PKG_CONFIG_LIBDIR \
    PKG_CONFIG_SYSROOT_DIR VULKAN_SDK LD_LIBRARY_PATH MAKEFLAGS CPATH C_INCLUDE_PATH \
    CPLUS_INCLUDE_PATH OBJC_INCLUDE_PATH LIBRARY_PATH COMPILER_PATH GCC_EXEC_PREFIX
export CC=gcc CXX=g++ CFLAGS="$BASE_FLAGS" CXXFLAGS="$BASE_FLAGS"
export CMAKE_PREFIX_PATH="$DEPS" PKG_CONFIG_PATH="$DEPS/lib/pkgconfig:$DEPS/share/pkgconfig"
export PATH="$DEPS/bin:$PATH"
JOBS=$(nproc)

mapfile -t COMPONENTS < <(python3 - "$PINS" <<'PY'
import json, sys
for item in json.load(open(sys.argv[1]))["git_sources"]:
    print("\t".join((item["name"], item["url"], item["commit"], "|".join(item["notices"]))))
PY
)
printf 'name\turl\tcommit\tarchive_sha256\tnotices\n' > "$OUT/receipts/sources.tsv"
for row in "${COMPONENTS[@]}"; do
    IFS=$'\t' read -r name url commit notices <<< "$row"
    [[ "$commit" =~ ^[0-9a-f]{40}$ ]] || fail "invalid pin for $name"
    repo="$WORK/git/$name"; source="$WORK/src/$name"
    archive="$OUT/sources/dependencies/$name-$commit.tar"
    mkdir -p "$repo" "$source"
    git -C "$repo" init --quiet
    git -C "$repo" fetch --quiet --depth=1 "$url" "$commit"
    git -C "$repo" cat-file -e "$commit^{commit}" || fail "pinned commit missing: $name"
    [[ "$(git -C "$repo" rev-parse FETCH_HEAD^{commit})" == "$commit" ]] || fail "fetched commit mismatch: $name"
    git -C "$repo" archive --format=tar --prefix="$name/" "$commit" > "$archive"
    [[ "$(git get-tar-commit-id < "$archive")" == "$commit" ]] || fail "archive commit mismatch: $name"
    tar -xf "$archive" -C "$source"
    for notice in ${notices//|/ }; do
        [[ -s "$source/$name/$notice" ]] || fail "pinned notice missing: $name/$notice"
    done
    hash=$(sha256sum "$archive" | awk '{print $1}')
    printf '%s\t%s\t%s\t%s\t%s\n' "$name" "$url" "$commit" "$hash" "$notices" >> "$OUT/receipts/sources.tsv"
done

cp -- "$PINS" "$OUT/sources/recipes/sources.json"
cp -- "$SCRIPT_DIR/build-native.sh" "$OUT/sources/recipes/build-native.sh"
cp -- "$SCRIPT_DIR/Dockerfile" "$OUT/sources/recipes/Dockerfile"
cp -- "$PRODUCT_ROOT/nix/steamaudio-unaligned-accumulate.patch" "$OUT/sources/recipes/"
cat > "$OUT/receipts/runtime-provider-ownership.txt" <<'EOF'
Slice 1 owns direct apt_packages receipts and records installed-packages.tsv
plus installed-file-owners.tsv for every installed package in this image.
package.py (slice 2) consumes those maps with install/, deps/, sources/, and
the pinned image's enabled deb-src indexes; run it in this same image so dpkg
ownership and apt source metadata are available.

package.py owns final staging and must resolve every staged ELF runtime
provider and installed *-dev header/static contributor. For each resolved
Ubuntu package it must capture the exact matching .deb and source archives,
binary/source identities, hashes, and installed copyright notice. Missing or
ambiguous ownership/source/notice is a hard failure. The direct apt_packages
list is not a complete contributor inventory.

Slice 1 currently receipts only sources.json, Dockerfile, build-native.sh, and
the reused Steam Audio patch under sources/recipes/. Final staging must add
package.py, host-policy.json, and all other production packaging inputs to its
source-access artifact.
EOF

mapfile -t PACKAGES < <(python3 - "$PINS" <<'PY'
import json, sys
print("\n".join(json.load(open(sys.argv[1]))["apt_packages"]))
PY
)
cp -- /etc/apt/sources.list.d/ubuntu.sources "$OUT/receipts/ubuntu.sources"
printf 'binary_package\tbinary_version\tsource_package\tsource_version\trepository\tdeb_sha256\n' > "$OUT/receipts/packages.tsv"
declare -A SOURCES_SAVED=()
for package in "${PACKAGES[@]}"; do
    binary_version=$(dpkg-query -W -f='${Version}' "$package")
    read -r source_package source_version < <(dpkg-query -W -f='${source:Package}\t${source:Version}\n' "$package")
    [[ -n "$source_package" ]] || fail "source package missing for $package"
    [[ -n "$source_version" ]] || source_version=$binary_version
    repository=$(apt-cache policy "$package" | awk '!found && $2 ~ /^https?:\/\// {print $2; found=1}')
    [[ -n "$repository" ]] || fail "repository receipt missing for $package"
    package_work="$WORK/package-$package"
    mkdir -p "$package_work"
    (cd "$package_work" && apt-get download "$package=$binary_version")
    deb=$(find "$package_work" -maxdepth 1 -type f -name '*.deb' -print -quit)
    [[ -n "$deb" ]] || fail "matching deb archive missing for $package"
    deb_hash=$(sha256sum "$deb" | awk '{print $1}')
    mv -- "$deb" "$OUT/sources/ubuntu/packages/$(basename "$deb")"
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$package" "$binary_version" "$source_package" "$source_version" "$repository" "$deb_hash" >> "$OUT/receipts/packages.tsv"
    source_key="$source_package:$source_version"
    if [[ -z "${SOURCES_SAVED[$source_key]+x}" ]]; then
        safe_key=${source_key//[^A-Za-z0-9.+~-]/_}
        source_dir="$OUT/sources/ubuntu/source/$safe_key"
        mkdir -p "$source_dir"
        (cd "$source_dir" && apt-get source --download-only "$source_package=$source_version")
        SOURCES_SAVED[$source_key]=1
    fi
done

cmake_native() {
    local source=$1 build=$2; shift 2
    {
        printf '[CMake %s]\n' "${source##*/}"
        printf '%s\n' 'CMAKE_BUILD_TYPE=Release' 'CMAKE_INSTALL_PREFIX=OUT_DEPS' \
            'CMAKE_INSTALL_LIBDIR=lib' "CMAKE_C_FLAGS=$BASE_FLAGS" \
            "CMAKE_CXX_FLAGS=$BASE_FLAGS" 'CMAKE_PREFIX_PATH=OUT_DEPS' \
            'CMAKE_INSTALL_RPATH=OUT_DEPS/lib'
        for option in "$@"; do
            option=${option//"$DEPS"/OUT_DEPS}
            printf '%s\n' "$option"
        done
    } >> "$OUT/receipts/options.txt"
    cmake -S "$source" -B "$build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$DEPS" -DCMAKE_INSTALL_LIBDIR=lib \
        -DCMAKE_C_FLAGS="$BASE_FLAGS" -DCMAKE_CXX_FLAGS="$BASE_FLAGS" \
        -DCMAKE_PREFIX_PATH="$DEPS" -DCMAKE_INSTALL_RPATH="$DEPS/lib" "$@"
    cmake --build "$build" --parallel "$JOBS"
    cmake --install "$build"
}

C="$WORK/src/vulkan-headers/vulkan-headers"
cmake_native "$C" "$WORK/build/vulkan-headers" -DVULKAN_HEADERS_ENABLE_TESTS=OFF -DVULKAN_HEADERS_ENABLE_INSTALL=ON -DVULKAN_HEADERS_ENABLE_MODULE=OFF
cmake_native "$WORK/src/vulkan-loader/vulkan-loader" "$WORK/build/vulkan-loader" \
    -DBUILD_TESTS=OFF -DBUILD_WSI_XCB_SUPPORT=ON -DBUILD_WSI_XLIB_SUPPORT=ON \
    -DBUILD_WSI_XLIB_XRANDR_SUPPORT=ON -DBUILD_WSI_WAYLAND_SUPPORT=ON
cmake_native "$WORK/src/spirv-headers/spirv-headers" "$WORK/build/spirv-headers" \
    -DSPIRV_HEADERS_ENABLE_TESTS=OFF
cmake_native "$WORK/src/spirv-tools/spirv-tools" "$WORK/build/spirv-tools" \
    -DSPIRV-Headers_SOURCE_DIR="$WORK/src/spirv-headers/spirv-headers" \
    -DSPIRV_SKIP_EXECUTABLES=OFF -DSPIRV_SKIP_TESTS=ON
cmake_native "$WORK/src/glslang/glslang" "$WORK/build/glslang" \
    -DBUILD_EXTERNAL=OFF -DALLOW_EXTERNAL_SPIRV_TOOLS=ON -DENABLE_OPT=ON \
    -DENABLE_SPIRV=ON -DENABLE_GLSLANG_BINARIES=ON -DGLSLANG_TESTS=OFF
cmake_native "$WORK/src/openxr-sdk/openxr-sdk" "$WORK/build/openxr-sdk" \
    -DBUILD_LOADER=ON -DDYNAMIC_LOADER=ON -DBUILD_API_LAYERS=OFF -DBUILD_TESTS=OFF \
    -DBUILD_TESTING=OFF -DBUILD_CONFORMANCE_TESTS=OFF -DBUILD_SDK_TESTS=OFF \
    -DBUILD_WITH_SYSTEM_JSONCPP=ON \
    -DBUILD_WITH_STD_FILESYSTEM=ON -DBUILD_LOADER_WITH_EXCEPTION_HANDLING=ON \
    -DCMAKE_INSTALL_SYSCONFDIR=/etc

cmake_native "$WORK/src/sdl/sdl" "$WORK/build/sdl" \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_INSTALL=ON -DSDL_RELOCATABLE=ON \
    -DSDL_INSTALL_CPACK=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
    -DSDL_VIDEO=ON -DSDL_AUDIO=ON -DSDL_VULKAN=ON -DSDL_X11=ON -DSDL_WAYLAND=ON \
    -DSDL_WAYLAND_LIBDECOR=ON -DSDL_KMSDRM=ON -DSDL_HIDAPI_LIBUSB=ON \
    -DSDL_DEPS_SHARED=ON -DSDL_X11_SHARED=ON -DSDL_WAYLAND_SHARED=ON \
    -DSDL_WAYLAND_LIBDECOR_SHARED=ON -DSDL_ALSA=ON -DSDL_ALSA_SHARED=ON \
    -DSDL_PULSEAUDIO=ON -DSDL_PULSEAUDIO_SHARED=ON -DSDL_PIPEWIRE=ON \
    -DSDL_PIPEWIRE_SHARED=ON -DSDL_JACK=ON -DSDL_JACK_SHARED=ON \
    -DSDL_SNDIO=ON -DSDL_SNDIO_SHARED=ON -DSDL_KMSDRM_SHARED=ON \
    -DSDL_HIDAPI_LIBUSB_SHARED=ON
cmake_native "$WORK/src/flatbuffers/flatbuffers" "$WORK/build/flatbuffers" \
    -DFLATBUFFERS_INSTALL=ON -DFLATBUFFERS_BUILD_FLATC=ON -DFLATBUFFERS_BUILD_FLATLIB=OFF \
    -DFLATBUFFERS_BUILD_SHAREDLIB=OFF -DFLATBUFFERS_BUILD_TESTS=OFF \
    -DFLATBUFFERS_BUILD_BENCHMARKS=OFF -DFLATBUFFERS_BUILD_GRPCTEST=OFF
cmake_native "$WORK/src/pffft/pffft" "$WORK/build/pffft" \
    -DBUILD_SHARED_LIBS=ON -DINSTALL_PFFFT=ON -DINSTALL_PFDSP=OFF -DINSTALL_PFFASTCONV=OFF \
    -DPFFFT_USE_TYPE_FLOAT=ON -DPFFFT_USE_TYPE_DOUBLE=ON -DPFFFT_USE_SIMD=ON \
    -DTARGET_C_ARCH=none -DTARGET_CXX_ARCH=none -DPFFFT_BUILD_TESTS=OFF \
    -DPFFFT_BUILD_BENCHMARKS=OFF -DPFFFT_BUILD_EXAMPLES=OFF

STEAM="$WORK/src/steam-audio/steam-audio"
patch -p2 -d "$STEAM/core" < "$PRODUCT_ROOT/nix/steamaudio-unaligned-accumulate.patch"
# GCC 13's libstdc++ cannot instantiate <future> with Valve's legacy ABI6.
# Build the complete private SDK implementation with the native compiler ABI;
# keep AVX and the public phonon C interface unchanged.
sed -i '/add_compile_options(-fabi-version=6)/d' "$STEAM/core/CMakeLists.txt"
sed -i '/^set(ZLIB_ROOT /d; /^set(ZLIB_INCLUDE_DIR /d' "$STEAM/core/build/FindMySOFA.cmake"
MY_HEADER=$(dpkg-query -L libmysofa-dev | awk '!found && /\/mysofa\.h$/ {print; found=1}')
MY_LIBRARY=$(dpkg-query -L libmysofa-dev | awk '!found && /\/libmysofa\.so$/ {print; found=1}')
[[ -n "$MY_HEADER" && -n "$MY_LIBRARY" ]] || fail 'libmysofa-dev headers/library missing'
STEAM_ARCH_OPTIONS=()
if [[ "$ARCH" == x86_64 ]]; then STEAM_ARCH_OPTIONS=(-DSTEAMAUDIO_ENABLE_AVX=ON); fi
cmake_native "$STEAM/core" "$WORK/build/steam-audio" \
    -DBUILD_SHARED_LIBS=ON -DSTEAMAUDIO_STATIC_RUNTIME=OFF \
    "${STEAM_ARCH_OPTIONS[@]}" \
    -DSTEAMAUDIO_ENABLE_IPP=OFF -DSTEAMAUDIO_ENABLE_MKL=OFF \
    -DSTEAMAUDIO_ENABLE_EMBREE=OFF -DSTEAMAUDIO_ENABLE_RADEONRAYS=OFF \
    -DSTEAMAUDIO_ENABLE_TRUEAUDIONEXT=OFF -DSTEAMAUDIO_BUILD_TESTS=OFF \
    -DSTEAMAUDIO_BUILD_ITESTS=OFF -DSTEAMAUDIO_BUILD_BENCHMARKS=OFF \
    -DSTEAMAUDIO_BUILD_SAMPLES=OFF -DSTEAMAUDIO_BUILD_DOCS=OFF \
    -DPFFFT_INCLUDE_DIR="$DEPS/include/pffft" -DPFFFT_LIBRARY="$DEPS/lib/libpffft.so" \
    -DFlatBuffers_INCLUDE_DIR="$DEPS/include" -DFlatBuffers_EXECUTABLE="$DEPS/bin/flatc" \
    -DMySOFA_INCLUDE_DIR="$(dirname -- "$MY_HEADER")" -DMySOFA_LIBRARY="$MY_LIBRARY"
install -Dm755 "$WORK/build/steam-audio/src/core/libphonon.so" "$DEPS/lib/libphonon.so"
install -Dm644 "$STEAM/core/src/core/phonon.h" "$DEPS/include/phonon.h"
install -Dm644 "$WORK/build/steam-audio/src/core/phonon_version.h" "$DEPS/include/phonon_version.h"

pkg-config --exists libcurl || fail 'libcurl pkg-config dependency is required'
curl_version=$(pkg-config --modversion libcurl)
ENGINE="$WORK/build/engine"
MESON_OPTIONS=(--prefix=/ --libdir=lib --buildtype=release --auto-features=enabled \
    --wrap-mode=nodownload -Ddebug=true -Dstrip=false -Duse_sdl3=enabled \
    -Duse_steam_audio=enabled -Dsteam_audio_include_dir="$DEPS/include" \
    -Dsteam_audio_library_dir="$DEPS/lib" -Duse_codec_wave=enabled \
    -Duse_codec_mp3=enabled -Dmp3_lib=mpg123 -Duse_codec_flac=enabled \
    -Duse_codec_vorbis=enabled -Dvorbis_lib=vorbis -Duse_codec_opus=enabled)
{
    printf '[Meson]\n'
    for option in "${MESON_OPTIONS[@]}"; do
        option=${option//"$DEPS"/OUT_DEPS}
        printf '%s\n' "$option"
    done
} >> "$OUT/receipts/options.txt"
meson setup "$ENGINE" "$PRODUCT_ROOT" "${MESON_OPTIONS[@]}"
meson compile -C "$ENGINE" --jobs "$JOBS"
DESTDIR="$OUT/install" meson install -C "$ENGINE"
[[ -x "$OUT/install/bin/vkquake" ]] || fail 'Meson install did not produce install/bin/vkquake'

cat >> "$OUT/receipts/options.txt" <<EOF

Ubuntu image index: $(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["ubuntu_image"])' "$PINS")
Native architecture: $MACHINE ($ARCH)
C/C++ baseline flags: $BASE_FLAGS; inherited compiler/linker flags cleared
Steam Audio patch: nix/steamaudio-unaligned-accumulate.patch; MySOFA finder adjusted for distro zlib
Steam Audio Linux private C++ implementation uses native compiler ABI; legacy -fabi-version=6 removed; phonon C API and AVX selection retained
Steam Audio x86-64 AVX option: ON; aarch64 uses the native SDK architecture path
libcurl pkg-config version: $curl_version (required precondition)
Meson DESTDIR: install/
Output dependencies prefix: deps/
EOF
{
    printf 'architecture\t%s\nproduct_revision\t%s\nproduct_archive_sha256\t%s\n' "$ARCH" "$REVISION" "$PRODUCT_HASH"
    printf 'libcurl_pkg_config_version\t%s\n' "$curl_version"
    printf '\n[os-release]\n'; cat /etc/os-release
    for tool in gcc g++ cmake ninja meson python3 pkg-config git patch patchelf; do
        printf '\n[%s]\n' "$tool"; "$tool" --version | awk 'NR == 1'
    done
} > "$OUT/receipts/environment.txt"
dpkg-query -W -f='${db:Status-Abbrev}\t${binary:Package}\t${Version}\t${source:Package}\t${source:Version}\n' \
    | awk -F '\t' '$1 ~ /^ii/ {print $2 "\t" $3 "\t" $4 "\t" $5}' \
    > "$OUT/receipts/installed-packages.tsv"
for package_files in /var/lib/dpkg/info/*.list; do
    [[ -f "$package_files" ]] || continue
    package=${package_files##*/}
    package=${package%.list}
    awk -v package="$package" 'NF {print $0 "\t" package}' "$package_files"
done | sort -u > "$OUT/receipts/installed-file-owners.tsv"
(
    cd "$OUT"
    find sources -type f -print0 | sort -z | xargs -0 sha256sum
) > "$OUT/receipts/sources.sha256"
(
    cd "$OUT"
    find install deps -type f -print0 | sort -z | xargs -0 sha256sum
) > "$OUT/receipts/native-artifacts.sha256"
(
    cd "$OUT"
    find install deps -type l -printf '%p\t%l\n' | sort > receipts/native-symlinks.tsv
    find receipts -type f ! -name receipts.sha256 -print0 | sort -z | xargs -0 sha256sum
) > "$OUT/receipts/receipts.sha256"
printf 'native source build complete: %s\n' "$OUT"
