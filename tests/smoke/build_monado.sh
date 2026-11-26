#!/usr/bin/env bash
# Build a minimal pinned Monado (simulated headset, null compositor, service)
# for the VR smoke test. Usage: build_monado.sh PREFIX
# Ubuntu 22.04 packages: see tests/smoke/README.md.
set -euo pipefail
MONADO_COMMIT=735e29e4e7552b254528dbb20e0e96ec8f32368c
prefix=$(realpath -m "$1")
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
git -C "$work" init -q monado
git -C "$work/monado" fetch -q --depth 1 https://gitlab.freedesktop.org/monado/monado.git "$MONADO_COMMIT"
git -C "$work/monado" checkout -q FETCH_HEAD
cmake -S "$work/monado" -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX="$prefix" -DXRT_FEATURE_SERVICE=ON -DXRT_FEATURE_SERVICE_SYSTEMD=OFF -DXRT_HAVE_SDL=OFF \
	-DXRT_FEATURE_OPENVR=OFF -DXRT_MODULE_OPENVR_STATE_TRACKER=OFF -DXRT_FEATURE_STEAMVR_PLUGIN=OFF \
	-DXRT_HAVE_OPENGL=OFF -DXRT_HAVE_OPENGL_GLX=OFF -DXRT_HAVE_OPENGLES=OFF -DXRT_FEATURE_SLAM=OFF \
	-DXRT_MODULE_MERCURY_HANDTRACKING=OFF -DXRT_BUILD_DRIVER_HANDTRACKING=OFF -DXRT_FEATURE_DEBUG_GUI=OFF \
	-DBUILD_TESTING=OFF -DBUILD_DOC=OFF
cmake --build "$work/build"
cmake --install "$work/build"
echo "$MONADO_COMMIT" > "$prefix/monado-commit.txt"
