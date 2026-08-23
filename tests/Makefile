CC ?= cc

CPPFLAGS += -D_DEFAULT_SOURCE -I../Quake $(shell sdl2-config --cflags 2>/dev/null)
# r_vrik.c uses Quake's GNU statement-expression math macros.  Keep the
# language mode compatible while treating the normal warning set as errors.
CFLAGS += -std=gnu99 -Wall -Wextra -Werror -Wformat -Wformat-security -g -O1
LDLIBS += -lm

# Both GCC and Clang support these on the supported Linux build hosts.  Set
# SANITIZE=0 when using a compiler or platform without the sanitizer runtimes.
SANITIZE ?= 1
ifeq ($(SANITIZE),1)
SANITIZE_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
endif

BUILD_DIR := .build
FIXTURES := \
	vr_fbt_state_fixture \
	vr_fbt_profile_fixture \
	vr_fbt_storage_fixture \
	vr_fbt_filter_fixture \
	vrik_codec_fixture \
	vrik_v2_fixture \
	vrik_protocol_fixture \
	vrik_relay_fixture \
	vrik_lowerbody_fixture \
	vrik_renderer_boundary_fixture \
	avatar_protocol_fixture \
	avatar_retarget_fixture
BINARIES := $(addprefix $(BUILD_DIR)/,$(FIXTURES))

.PHONY: all test clean

all: test

test: $(BINARIES)
	@set -e; for fixture in $(BINARIES); do \
		printf '==> %s\n' "$$fixture"; \
		case "$$fixture" in \
		.build/vrik_v2_fixture) "./$$fixture" | diff -u vrik_v2_fixture.expected - ;; \
		*) "./$$fixture" ;; \
		esac; \
	done

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/vr_fbt_state_fixture: vr_fbt_state_fixture.c ../Quake/vr_fbt.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SANITIZE_FLAGS) $^ $(LDFLAGS) $(SANITIZE_FLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/vr_fbt_profile_fixture: vr_fbt_profile_fixture.c ../Quake/vr_fbt_profile.c ../Quake/vr_fbt.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SANITIZE_FLAGS) $^ $(LDFLAGS) $(SANITIZE_FLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/vr_fbt_storage_fixture: vr_fbt_storage_fixture.c ../Quake/vr_fbt_storage.c ../Quake/vr_fbt_profile.c ../Quake/vr_fbt.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DVR_FBT_STORAGE_TEST $(SANITIZE_FLAGS) $^ $(LDFLAGS) $(SANITIZE_FLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/vr_fbt_filter_fixture: vr_fbt_filter_fixture.c ../Quake/vr_fbt_filter.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SANITIZE_FLAGS) $^ $(LDFLAGS) $(SANITIZE_FLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/vrik_codec_fixture: vrik_codec_fixture.c ../Quake/vrik_codec.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SANITIZE_FLAGS) $^ $(LDFLAGS) $(SANITIZE_FLAGS) $(LDLIBS) -o $@

# Keep this byte-oriented v2 compatibility baseline separate from the codec
# fixture, which exercises the production v2 decoder and encoder.
$(BUILD_DIR)/vrik_v2_fixture: vrik_v2_fixture.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SANITIZE_FLAGS) $< $(LDFLAGS) $(SANITIZE_FLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/vrik_protocol_fixture: vrik_protocol_fixture.c ../Quake/vrik_codec.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SANITIZE_FLAGS) $^ $(LDFLAGS) $(SANITIZE_FLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/vrik_relay_fixture: vrik_relay_fixture.c ../Quake/vrik_codec.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SANITIZE_FLAGS) $^ $(LDFLAGS) $(SANITIZE_FLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/vrik_lowerbody_fixture: vrik_lowerbody_fixture.c ../Quake/r_vrik.c ../Quake/mathlib.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SANITIZE_FLAGS) $^ $(LDFLAGS) $(SANITIZE_FLAGS) $(LDLIBS) -o $@

# r_alias is a complete renderer unit.  Section GC keeps this fixture linked
# only to its test seam and the receive-to-render translation it covers.
$(BUILD_DIR)/vrik_renderer_boundary_fixture: vrik_renderer_boundary_fixture.c ../Quake/r_alias.c ../Quake/r_avatar.c ../Quake/mathlib.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-error=missing-field-initializers -Wno-error=unused-parameter -DR_ALIAS_LOWER_TARGETS_TEST -ffunction-sections -fdata-sections $(SANITIZE_FLAGS) $^ $(LDFLAGS) -Wl,--gc-sections $(SANITIZE_FLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/avatar_retarget_fixture: avatar_retarget_fixture.c ../Quake/r_avatar.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SANITIZE_FLAGS) $^ $(LDFLAGS) $(SANITIZE_FLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/avatar_protocol_fixture: avatar_protocol_fixture.c ../Quake/player_avatar.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SANITIZE_FLAGS) $^ $(LDFLAGS) $(SANITIZE_FLAGS) $(LDLIBS) -o $@

# This directory contains only test executables.  Its name is fixed above so
# cleanup cannot be redirected outside tests/ by a command-line variable.
clean:
	rm -rf -- .build
