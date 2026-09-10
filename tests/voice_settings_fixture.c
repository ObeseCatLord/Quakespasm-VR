#include "voice_settings.h"
#include "voice_capture.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void write_bytes(const char *path, const unsigned char *data, size_t bytes)
{
    FILE *f = fopen(path, "wb"); assert(f);
    assert(fwrite(data, 1, bytes, f) == bytes); assert(!fclose(f));
}
int main(void)
{
    char directory[] = "/tmp/qsvr-voice-settings-XXXXXX", path[256];
    unsigned char data[2048];
    voice_settings_t saved, loaded, previous;
    voice_capture_route_t route;
    size_t bytes; FILE *f;
    assert(mkdtemp(directory)); snprintf(path, sizeof(path), "%s/settings", directory);
    VoiceSettings_Defaults(&saved);
    assert(!saved.desktop.self_reverb && !saved.vr.self_reverb);
    strcpy(saved.desktop.device, "desktop mic"); strcpy(saved.vr.device, "headset mic");
    saved.desktop.transmit = 0; saved.vr.mode = 1; saved.vr.self_reverb = 1;
    saved.ptt_allowed[42] = 1;
    assert(VoiceSettings_Save(path, &saved));
    assert(VoiceSettings_Load(path, &loaded) == 1 && !memcmp(&saved, &loaded, sizeof(saved)));
    f = fopen(path, "rb"); assert(f); bytes = fread(data, 1, sizeof(data), f); fclose(f);
    assert(bytes > 2 && data[8] == 2);
    /* Actual v1 layout: no appended local-capture opt-ins. */
    data[8] = 1; write_bytes(path, data, bytes - 2);
    assert(VoiceSettings_Load(path, &loaded) == 1);
    saved.vr.self_reverb = 0; assert(!memcmp(&saved, &loaded, sizeof(saved)));
    previous = loaded;
    data[8] = 2; data[bytes-1] = 2; write_bytes(path, data, bytes);
    assert(VoiceSettings_Load(path, &loaded) == -1 && !memcmp(&previous, &loaded, sizeof(loaded)));
    data[bytes-1] = 1; data[bytes] = 0; write_bytes(path, data, bytes + 1);
    assert(VoiceSettings_Load(path, &loaded) == -1);
    write_bytes(path, data, bytes - 1); assert(VoiceSettings_Load(path, &loaded) == -1);
    unlink(path); rmdir(directory);
    /* Local capture is allowed in SP without arming network transmission. */
    route = Voice_CaptureRoute(1,1,1,0,0,1,1);
    assert(route.capture && route.monitor && !route.transmit);
    route = Voice_CaptureRoute(1,1,1,1,0,1,1);
    assert(route.capture && route.monitor && !route.transmit);
    route = Voice_CaptureRoute(1,1,1,1,1,0,0);
    assert(route.capture && route.transmit && !route.monitor);
    assert(!Voice_CaptureRoute(1,1,1,0,1,0,1).capture);
    assert(!Voice_CaptureRoute(1,1,1,0,0,1,0).capture);
    assert(!Voice_CaptureRoute(1,0,1,1,1,1,1).capture);
    assert(!Voice_CaptureRoute(1,1,0,1,1,1,1).capture);
    assert(!Voice_CaptureRoute(0,1,1,1,1,1,1).capture);
    puts("Voice settings: roundtrip, v1 migration, invalid files, capture purpose gates passed");
    return 0;
}
