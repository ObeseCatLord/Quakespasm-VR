/* Pure routing policy shared by capture lifecycle and its regression fixture. */
#ifndef VOICE_CAPTURE_H
#define VOICE_CAPTURE_H
typedef struct { int capture, monitor, transmit; } voice_capture_route_t;
static inline voice_capture_route_t Voice_CaptureRoute(int device, int runtime,
    int in_game, int multiplayer, int transmit, int self_reverb, int spatial)
{
    voice_capture_route_t r;
    r.transmit = device && runtime && in_game && multiplayer && transmit;
    r.monitor = device && runtime && in_game && self_reverb && spatial;
    r.capture = r.transmit || r.monitor;
    return r;
}
#endif
