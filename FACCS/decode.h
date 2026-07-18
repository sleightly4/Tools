/* decode.h — types and constants for amodem BITRATE=48 decoder */
#ifndef DECODE_H
#define DECODE_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* ================ amodem BITRATE=48 constants ================ */
#define FS              32000       /* sample rate */
#define NSYM            32          /* samples per baud (symbol) */
#define NFREQ           8           /* number of OFDM carriers */
#define BITS_PER_SYM    6           /* 64-QAM */
#define BITS_PER_BAUD   48          /* NFREQ * BITS_PER_SYM */
#define BAUD_RATE       1000        /* FS / NSYM */
#define BLOCK_SIZE      250         /* amodem frame payload size */
#define FRAME_MAX       255         /* amodem length-prefix max */

extern const double CARRIER_FREQS[NFREQ];  /* {3000, 4000, ..., 10000} */
#define PILOT_FREQ      3000.0
#define PILOT_OMEGA     (2.0 * 3.14159265358979323846 * PILOT_FREQ / FS)

/* preamble/training layout (from amodem/equalizer.py) */
#define EQ_LENGTH       200         /* # of training symbols (bauds) */
#define EQ_SILENCE_LEN  50          /* silence after pilot / after training */
#define CONSTANT_PREFIX 16          /* first N training bauds are all-ones */

/* pilot detection thresholds */
#define COHERENCE_THR   0.9
#define CARRIER_DUR     EQ_LENGTH   /* 200 bauds */
#define CARRIER_THR     ((int)(0.9 * CARRIER_DUR))   /* 180 */
#define SEARCH_WIN      ((int)(0.1 * CARRIER_DUR))   /* 20 */

/* ================ FEC v2 constants ================ */
#define FEC_BLOCK       250
#define FEC_HDR         8
#define FEC_PAYLOAD     242
#define FEC_K           16
#define FEC_M           4

/* ================ complex arithmetic ================ */
typedef struct { double re, im; } cplx;

static inline cplx cmul(cplx a, cplx b) {
    cplx r = { a.re*b.re - a.im*b.im, a.re*b.im + a.im*b.re };
    return r;
}
static inline cplx cadd(cplx a, cplx b) { cplx r = {a.re+b.re, a.im+b.im}; return r; }
static inline cplx cscale(cplx a, double s) { cplx r = {a.re*s, a.im*s}; return r; }
static inline cplx cdiv(cplx a, cplx b) {
    double d = b.re*b.re + b.im*b.im;
    cplx r = { (a.re*b.re + a.im*b.im)/d, (a.im*b.re - a.re*b.im)/d };
    return r;
}
static inline double cabs2(cplx a) { return a.re*a.re + a.im*a.im; }

/* ================ WAV loader ================ */
/* Loads WAV, optionally extracts one channel, returns 32 kHz mono float samples.
 * channel: 0=L, 1=R, -1=mono downmix.
 * Returns malloc'd samples; caller frees. Sets *n_out to sample count. */
double *wav_load_32khz(const char *path, int channel, size_t *n_out);

/* ================ resampler ================ */
/* Polyphase resample with Kaiser-windowed sinc. Returns malloc'd buffer. */
double *resample_to_32khz(const double *in, size_t n_in, int in_sr, size_t *n_out);

/* ================ FEC v2 decoder ================ */
/* Decodes amodem output stream via FEC v2. Returns malloc'd file bytes or NULL.
 * *out_len is set to decoded file length. */
uint8_t *fec_decode(const uint8_t *data, size_t data_len, size_t *out_len);

/* ================ amodem receiver ================ */
/* Runs amodem BITRATE=48 receiver on mono float samples.
 * Returns malloc'd buffer of raw frame payload bytes; *out_len is set.
 * Returns NULL on unrecoverable error (pilot not found, etc.). */
uint8_t *amodem_recv(const double *samples, size_t n_samples, size_t *out_len);

#endif
