#include <opus/opus.h>
#include <opus/opusfile.h>
#include <stdio.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        goto cleanup; \
    } \
} while (0)

int main(int argc, char **argv)
{
    opus_int16 input[960], decoded_pcm[960], stereo[1920];
    unsigned char packet[4000];
    OpusEncoder *encoder = NULL;
    OpusDecoder *decoder = NULL;
    OggOpusFile *file = NULL;
    int error = OPUS_OK, bytes, decoded, i, nonzero = 0, status = 1;

    CHECK(argc == 2);
    for (i = 0; i < 960; ++i)
        input[i] = (i % 96 < 48) ? 12000 : -12000;
    encoder = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &error);
    CHECK(encoder != NULL && error == OPUS_OK);
    CHECK(opus_encoder_ctl(encoder, OPUS_SET_BITRATE(24000)) == OPUS_OK);
    bytes = opus_encode(encoder, input, 960, packet, (opus_int32)sizeof(packet));
    CHECK(bytes > 0);
    decoder = opus_decoder_create(48000, 1, &error);
    CHECK(decoder != NULL && error == OPUS_OK);
    decoded = opus_decode(decoder, packet, bytes, decoded_pcm, 960, 0);
    CHECK(decoded == 960);
    for (i = 0; i < decoded; ++i)
        if (decoded_pcm[i] != 0) { nonzero = 1; break; }
    CHECK(nonzero);
    CHECK(opus_decoder_ctl(decoder, OPUS_RESET_STATE) == OPUS_OK);
    CHECK(opus_encoder_ctl(encoder, OPUS_RESET_STATE) == OPUS_OK);
    opus_decoder_destroy(decoder); decoder = NULL;
    opus_encoder_destroy(encoder); encoder = NULL;

    error = 0;
    file = op_open_file(argv[1], &error);
    CHECK(file != NULL && error == OPUS_OK);
    nonzero = 0;
    for (;;) {
        int frames = op_read_stereo(file, stereo, 960);
        CHECK(frames >= 0);
        if (frames == 0) break;
        for (i = 0; i < frames * 2; ++i)
            if (stereo[i] != 0) { nonzero = 1; break; }
    }
    CHECK(nonzero);
    op_free(file); file = NULL;
    puts("WINDOWS_CODEC_SMOKE_PASSED");
    status = 0;
cleanup:
    if (file != NULL) op_free(file);
    if (decoder != NULL) opus_decoder_destroy(decoder);
    if (encoder != NULL) opus_encoder_destroy(encoder);
    return status;
}
