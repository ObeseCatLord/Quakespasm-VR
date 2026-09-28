# Offline negotiation fixture; run from Quake with USE_SDL3=1.
include Makefile

NEGOTIATION_FIXTURE ?= /tmp/qsvr-negotiation-native-fixture
NEGOTIATION_ENGINE_OBJS = $(filter-out main_sdl.o sv_main.o,$(OBJS))

.PHONY: negotiation-native-fixture
negotiation-native-fixture: $(NEGOTIATION_FIXTURE)

$(NEGOTIATION_FIXTURE).o: ../tests/negotiation_native_fixture.c ../tests/native_engine_fixture.h sv_main.c
	$(CC) $(filter-out -DNDEBUG,$(DFLAGS) $(CPPFLAGS) $(CFLAGS)) $(SDL_CFLAGS) -I. -c $< -o $@

$(NEGOTIATION_FIXTURE): $(NEGOTIATION_FIXTURE).o $(SHADER_OBJS) $(NEGOTIATION_ENGINE_OBJS)
	$(LINKER) $^ $(LDFLAGS) $(LIBS) $(SDL_LIBS) -Wl,--wrap=Loop_Init -Wl,--wrap=NET_CanSendMessage -o $@
