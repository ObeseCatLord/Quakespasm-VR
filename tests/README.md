# Migration boundary fixtures

`vr_openxr_vulkan_fixture.cpp` is reused from the product OpenXR branch at
`3080841333fa94000df7e1fb9e549c7158685dd6`, adapted to the Vulkan-only 2.0 backend.
It checks creation-result ownership, instance/device provenance, version
requirements, independent/array eye ownership, incomplete-frame suppression,
balanced queue callbacks on runtime failures, and unlocked renderer retirement
before runtime image destruction. Begun-frame healthy detach checks that the
queue callbacks remain usable for release/end after retirement and survive for
reattachment; full shutdown clears them. The dispatch is simulated;
passing does not prove a working Vulkan device, task-enabled multiview, headset
presentation or gameplay parity.

After the implementation slice is ready for consolidated checks, an SDL3 build is:

```sh
c++ -std=c++14 -DUSE_SDL3 -Wall -Wextra -Werror -Wno-missing-field-initializers \
  tests/vr_openxr_vulkan_fixture.cpp $(pkg-config --cflags --libs sdl3) \
  -o /tmp/quakespasm-2.0-openxr-vulkan-fixture
/tmp/quakespasm-2.0-openxr-vulkan-fixture
```

For SDL2, omit `-DUSE_SDL3` and use `pkg-config --cflags --libs sdl2`.
The fixture must retain assertions (do not define `NDEBUG`). No runtime or headset
is required; it does not open a session against an installed runtime.

## Presentation image acquisition

`render_acquire_fixture.c` executes the production frame description and recorder
with Vulkan command spies. Across ordinary transparency, WBOIT, MBOIT, and SSAO
on/off, it checks that a failed acquisition leaves scene/prepared commands intact
and executes neither the UI/presentation framebuffer nor screenshot readback.
The failed-acquisition cases deliberately have no UI framebuffer and an invalid
image index. Successful acquisition still executes both UI subpasses and readback.
This does not validate actual GPU execution or task scheduling.

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-missing-field-initializers -Wno-sign-compare \
  -ffunction-sections -fdata-sections tests/render_acquire_fixture.c \
  -Wl,--gc-sections $(pkg-config --cflags --libs sdl3) \
  -o /tmp/quakespasm-2.0-render-acquire-fixture
/tmp/quakespasm-2.0-render-acquire-fixture
```

Keep assertions enabled. Section garbage collection omits unrelated resource
creation code; the fixture includes the actual recorder rather than a duplicate
implementation. Windows and ARM verification are deferred until the end; these
commands cover this Linux development machine only.
