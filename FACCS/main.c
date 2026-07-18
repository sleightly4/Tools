/* main.c — CLI for amodem+FEC decoder */
#include "decode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [--stereo] input.wav output.file\n"
        "  --stereo    decode L+R independently, concatenate\n",
        prog);
}

static int process(const char *wav_path, int channel, uint8_t **frames_out, size_t *frames_len) {
    size_t n_samples;
    double *samples = wav_load_32khz(wav_path, channel, &n_samples);
    if (!samples) return -1;
    fprintf(stderr, "loaded %zu samples (%.2f s) [channel=%d]\n",
            n_samples, n_samples / (double)FS, channel);
    /* prepend 1 s of silence so pilot detection has room */
    size_t pad = FS;
    double *padded = (double*)calloc(n_samples + pad, sizeof(double));
    if (!padded) { free(samples); return -1; }
    memcpy(padded + pad, samples, n_samples * sizeof(double));
    free(samples);

    *frames_out = amodem_recv(padded, n_samples + pad, frames_len);
    free(padded);
    if (!*frames_out) return -1;
    return 0;
}

int main(int argc, char **argv) {
    int stereo = 0;
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-') {
        if (!strcmp(argv[argi], "--stereo")) { stereo = 1; argi++; }
        else if (!strcmp(argv[argi], "-h") || !strcmp(argv[argi], "--help")) {
            usage(argv[0]); return 0;
        } else { fprintf(stderr, "unknown flag: %s\n", argv[argi]); return 2; }
    }
    if (argi + 2 != argc) { usage(argv[0]); return 2; }
    const char *in_path = argv[argi], *out_path = argv[argi + 1];

    uint8_t *frames_all = NULL;
    size_t frames_all_len = 0;

    if (stereo) {
        uint8_t *fl = NULL, *fr = NULL;
        size_t nl = 0, nr = 0;
        fprintf(stderr, "== L channel ==\n");
        if (process(in_path, 0, &fl, &nl) < 0) return 1;
        fprintf(stderr, "== R channel ==\n");
        if (process(in_path, 1, &fr, &nr) < 0) { free(fl); return 1; }
        frames_all_len = nl + nr;
        frames_all = (uint8_t*)malloc(frames_all_len);
        if (!frames_all) { free(fl); free(fr); return 1; }
        memcpy(frames_all, fl, nl);
        memcpy(frames_all + nl, fr, nr);
        free(fl); free(fr);
    } else {
        if (process(in_path, -1, &frames_all, &frames_all_len) < 0) return 1;
    }

    /* FEC decode */
    size_t decoded_len;
    uint8_t *decoded = fec_decode(frames_all, frames_all_len, &decoded_len);
    if (!decoded) {
        fprintf(stderr, "no FEC header — writing raw frame bytes\n");
        FILE *fo = fopen(out_path, "wb");
        if (!fo) { free(frames_all); return 1; }
        fwrite(frames_all, 1, frames_all_len, fo);
        fclose(fo);
        free(frames_all);
        fprintf(stderr, "wrote %zu bytes to %s\n", frames_all_len, out_path);
        return 0;
    }
    free(frames_all);
    FILE *fo = fopen(out_path, "wb");
    if (!fo) { free(decoded); return 1; }
    fwrite(decoded, 1, decoded_len, fo);
    fclose(fo);
    free(decoded);
    fprintf(stderr, "wrote %zu bytes to %s\n", decoded_len, out_path);
    return 0;
}
