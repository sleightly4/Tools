/* wav.c — minimal WAV reader with resampling */
#include "decode.h"
#include <stdlib.h>
#include <string.h>

const double CARRIER_FREQS[NFREQ] = {
    3000.0, 4000.0, 5000.0, 6000.0, 7000.0, 8000.0, 9000.0, 10000.0
};

static uint32_t rd_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static uint16_t rd_u16le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1]<<8);
}

double *wav_load_32khz(const char *path, int channel, size_t *n_out) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return NULL; }
    uint8_t hdr[44];
    if (fread(hdr, 1, 12, f) != 12) { fclose(f); return NULL; }
    if (memcmp(hdr, "RIFF", 4) || memcmp(hdr+8, "WAVE", 4)) {
        fprintf(stderr, "not a WAV file\n"); fclose(f); return NULL;
    }
    /* scan chunks for 'fmt ' and 'data' */
    int channels = 0, sr = 0, sampwidth = 0;
    uint8_t *pcm = NULL;
    size_t pcm_len = 0;
    while (fread(hdr, 1, 8, f) == 8) {
        uint32_t sz = rd_u32le(hdr+4);
        if (!memcmp(hdr, "fmt ", 4)) {
            uint8_t fmtbuf[40];
            size_t rd = sz > sizeof(fmtbuf) ? sizeof(fmtbuf) : sz;
            if (fread(fmtbuf, 1, rd, f) != rd) { fclose(f); return NULL; }
            if (sz > rd) fseek(f, sz - rd, SEEK_CUR);
            channels = rd_u16le(fmtbuf+2);
            sr = (int)rd_u32le(fmtbuf+4);
            sampwidth = rd_u16le(fmtbuf+14) / 8;
        } else if (!memcmp(hdr, "data", 4)) {
            pcm_len = sz;
            pcm = (uint8_t*)malloc(sz);
            if (!pcm) { fclose(f); return NULL; }
            if (fread(pcm, 1, sz, f) != sz) { free(pcm); fclose(f); return NULL; }
            break;
        } else {
            fseek(f, sz, SEEK_CUR);
        }
    }
    fclose(f);
    if (!pcm) { fprintf(stderr, "no data chunk found\n"); return NULL; }
    if (sampwidth != 2) {
        fprintf(stderr, "only 16-bit WAV supported, got %d-bit\n", sampwidth*8);
        free(pcm); return NULL;
    }
    if (channel >= channels) {
        fprintf(stderr, "requested channel %d but WAV has %d\n", channel, channels);
        free(pcm); return NULL;
    }

    size_t n_frames = pcm_len / (sampwidth * channels);
    double *raw = (double*)malloc(sizeof(double) * n_frames);
    if (!raw) { free(pcm); return NULL; }
    const int16_t *s16 = (const int16_t*)pcm;
    if (channels == 1) {
        for (size_t i = 0; i < n_frames; i++) raw[i] = s16[i] / 32768.0;
    } else if (channel < 0) {
        /* mono downmix */
        for (size_t i = 0; i < n_frames; i++) {
            double sum = 0.0;
            for (int c = 0; c < channels; c++) sum += s16[i*channels + c] / 32768.0;
            raw[i] = sum / channels;
        }
    } else {
        for (size_t i = 0; i < n_frames; i++) raw[i] = s16[i*channels + channel] / 32768.0;
    }
    free(pcm);

    if (sr != FS) {
        size_t n_new;
        double *r = resample_to_32khz(raw, n_frames, sr, &n_new);
        free(raw);
        if (!r) return NULL;
        *n_out = n_new;
        return r;
    }
    *n_out = n_frames;
    return raw;
}
