# Offline negotiation fixture; run from Quake with USE_SDL3=1.
include Makefile

NEGOTIATION_FIXTURE ?= /tmp/qsvr-negotiation-native-fixture
NEGOTIATION_SOURCE ?= ../tests/negotiation_native_fixture.c
NEGOTIATION_ENGINE_OBJS = $(filter-out main_sdl.o sv_main.o cl_demo.o cl_parse.o $(NEGOTIATION_EXTRA_EXCLUDE_OBJS),$(OBJS))

.PHONY: negotiation-native-fixture
negotiation-native-fixture: $(NEGOTIATION_FIXTURE)

$(NEGOTIATION_FIXTURE).o: $(NEGOTIATION_SOURCE) ../tests/negotiation_native_fixture.c ../tests/mixed_native_fixture.c ../tests/stock_liquid_native_fixture.c ../tests/stock_liquid_contract_fixture.c ../tests/native_engine_fixture.h sv_main.c sv_phys.c cl_main.c cl_demo.c cl_parse.c
	$(CC) $(filter-out -DNDEBUG,$(DFLAGS) $(CPPFLAGS) $(CFLAGS)) $(SDL_CFLAGS) -I. -c $< -o $@

$(NEGOTIATION_FIXTURE): $(NEGOTIATION_FIXTURE).o $(SHADER_OBJS) $(NEGOTIATION_ENGINE_OBJS)
	$(LINKER) $^ $(LDFLAGS) $(LIBS) $(SDL_LIBS) -Wl,--wrap=Loop_Init -Wl,--wrap=NET_CanSendMessage $(NEGOTIATION_EXTRA_LDFLAGS) -o $@
