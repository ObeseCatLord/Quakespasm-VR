/* Fail-closed capture routing shared by the voice lifecycle. */
#ifndef VOICE_CAPTURE_H
#define VOICE_CAPTURE_H

typedef struct
{
	int capture;
	int transmit;
} voice_capture_route_t;

static inline voice_capture_route_t Voice_CaptureRoute(int device_is_unique,
	int local_consent, int multiplayer_transport)
{
	voice_capture_route_t route;
	route.capture = device_is_unique && local_consent && multiplayer_transport;
	route.transmit = route.capture;
	return route;
}

#endif /* VOICE_CAPTURE_H */
