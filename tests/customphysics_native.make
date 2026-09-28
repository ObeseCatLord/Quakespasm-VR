# Run from Quake: make -f ../tests/customphysics_native.make USE_SDL3=1
include Makefile

CUSTOMPHYSICS_FIXTURE ?= /tmp/qsvr-customphysics-native-fixture
CUSTOMPHYSICS_SOURCE ?= ../tests/customphysics_native_fixture.c
CUSTOMPHYSICS_ENGINE_OBJS = $(filter-out main_sdl.o sv_phys.o,$(OBJS))

.PHONY: customphysics-native-fixture
customphysics-native-fixture: $(CUSTOMPHYSICS_FIXTURE)

$(CUSTOMPHYSICS_FIXTURE).o: $(CUSTOMPHYSICS_SOURCE) ../tests/native_engine_fixture.h sv_phys.c
	$(CC) $(filter-out -DNDEBUG,$(DFLAGS) $(CPPFLAGS) $(CFLAGS)) $(SDL_CFLAGS) -I. -c $< -o $@

$(CUSTOMPHYSICS_FIXTURE): $(CUSTOMPHYSICS_FIXTURE).o $(SHADER_OBJS) $(CUSTOMPHYSICS_ENGINE_OBJS)
	$(LINKER) $^ $(LDFLAGS) $(LIBS) $(SDL_LIBS) -Wl,--wrap=Loop_Init -o $@
