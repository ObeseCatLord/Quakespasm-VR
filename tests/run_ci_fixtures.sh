#!/usr/bin/env bash
# Build and run the self-contained C fixtures that need no GPU, display, VR
# runtime or game data. CI runs this on every push; it also works locally.
# VULKAN_INCLUDE optionally names a Vulkan-Headers include directory.
set -euo pipefail
cd "$(dirname "$0")/.."

fixtures=(
	cluster_lighting_geometry_fixture
	csqc_prediction_fixture
	datagram_rebind_fixture
	graphics_menu_layout_fixture
	loopback_sequence_fixture
	menu_ui_layout_fixture
	openxr_image_view_fault_fixture
	render_acquire_fixture
	savegame_dialect_fixture
	vr_aim_fixture
	vr_akimbo_crosshair_fixture
	vr_default_bindings_fixture
	vr_foveation_policy_fixture
	vr_foveation_rate_map_fixture
	vr_held_offset_scale_fixture
	vrik_interp_fixture
	vr_menu_anchor_fixture
	vr_openxr_math_fixture
)

out=$(mktemp -d)
trap 'rm -rf -- "$out"' EXIT
export HOME="$out" XDG_CONFIG_HOME="$out" SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
unset DISPLAY WAYLAND_DISPLAY XR_RUNTIME_JSON

failed=()
for name in "${fixtures[@]}"; do
	if ! gcc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -ffunction-sections -fdata-sections -Wl,--gc-sections \
		-IQuake -IWindows/SDL3/include ${VULKAN_INCLUDE:+-I"$VULKAN_INCLUDE"} -O1 -w -o "$out/$name" "tests/$name.c" -lm >"$out/$name.log" 2>&1; then
		echo "FAIL (compile) $name"; cat "$out/$name.log"; failed+=("$name"); continue
	fi
	if (cd "$out" && timeout 60 "./$name") >"$out/$name.log" 2>&1; then
		echo "ok   $name"
	else
		echo "FAIL $name"; tail -20 "$out/$name.log"; failed+=("$name")
	fi
done

if ((${#failed[@]})); then
	echo "${#failed[@]} of ${#fixtures[@]} fixtures failed: ${failed[*]}"
	exit 1
fi
echo "all ${#fixtures[@]} fixtures passed"
