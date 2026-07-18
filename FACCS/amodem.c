/* amodem.c — BITRATE=48 receiver with SFO tracking + Levinson-Durbin FIR equalizer.
 * Faithful port of amodem/{detect,dsp,sampling,recv,equalizer,levinson}.py for BITRATE=48.
 */
#include "decode.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ==== Constants and precomputed carriers ==== */
#define EQ_ORDER        10
#define EQ_LOOKAHEAD    10
#define EQ_FILTER_LEN   (EQ_ORDER + EQ_LOOKAHEAD)   /* 20 */
#define ITERS_PER_UPDATE 100         /* SFO update cadence, in bauds */
#define FREQ_ERR_GAIN   (0.01 * NSYM / (double)FS)  /* 0.01 * Tsym */

static cplx CARRIER_FILT[NFREQ][NSYM];       /* exp(-jωk)/(0.5*NSYM) */

static void init_carriers(void) {
    static int done = 0;
    if (done) return;
    done = 1;
    for (int c = 0; c < NFREQ; c++) {
        double w = 2.0 * M_PI * CARRIER_FREQS[c] / FS;
        for (int k = 0; k < NSYM; k++) {
            double s = 1.0 / (0.5 * NSYM);
            CARRIER_FILT[c][k].re =  cos(w * k) * s;
            CARRIER_FILT[c][k].im = -sin(w * k) * s;
        }
    }
}

/* ==== Windowed-sinc sub-sample interpolation ==== */
/* We use direct sinc evaluation at each output sample. WIDTH samples on each side,
 * Kaiser windowed. */
#define INTERP_WIDTH 16
static double interp_bessel_i0(double x) {
    double sum = 1.0, term = 1.0;
    double x2 = x * x / 4.0;
    for (int k = 1; k < 25; k++) {
        term *= x2 / (k * k);
        sum += term;
        if (term < 1e-14 * sum) break;
    }
    return sum;
}

/* Sampler with interpolation and optional post-interpolation FIR equalizer. */
typedef struct {
    const double *src;
    size_t n_src;
    double offset;              /* fractional sample position in src */
    double freq;                /* sample-rate ratio, ~1.0 */
    /* FIR equalizer (applied to interpolated stream) */
    double fir_taps[EQ_FILTER_LEN];
    double fir_hist[EQ_FILTER_LEN];
    int    fir_pos;             /* head in fir_hist (circular) */
    int    fir_active;
    /* Interpolator */
    double kaiser_denom;        /* bessel_i0(beta) */
    double kaiser_beta;
} sampler_t;

static void sampler_init(sampler_t *s, const double *src, size_t n, double start_offset) {
    s->src = src;
    s->n_src = n;
    s->offset = start_offset;
    s->freq = 1.0;
    for (int i = 0; i < EQ_FILTER_LEN; i++) { s->fir_taps[i] = 0.0; s->fir_hist[i] = 0.0; }
    s->fir_pos = 0;
    s->fir_active = 0;
    s->kaiser_beta = 8.0;
    s->kaiser_denom = interp_bessel_i0(s->kaiser_beta);
}

/* Interpolate one sample at s->offset, then advance by s->freq. */
static double sampler_interp(sampler_t *s) {
    double t = s->offset;
    long k = (long)floor(t);
    double acc = 0.0;
    int W = INTERP_WIDTH;
    /* Windowed-sinc; sinc has cutoff = 0.5 (Nyquist) since we're interpolating a
     * signal already at its own sample rate, and we just want fractional lookup. */
    for (int i = -W + 1; i <= W; i++) {
        long n = k + i;
        if (n < 0 || (size_t)n >= s->n_src) continue;
        double u = t - (double)n;                       /* signed distance */
        if (fabs(u) >= (double)W) continue;
        double au = fabs(u);
        double sinc = (au < 1e-12) ? 1.0 : sin(M_PI * u) / (M_PI * u);
        double w_arg = u / (double)W;
        double kaiser = interp_bessel_i0(s->kaiser_beta * sqrt(fmax(0.0, 1.0 - w_arg*w_arg)))
                        / s->kaiser_denom;
        acc += s->src[n] * sinc * kaiser;
    }
    s->offset += s->freq;
    return acc;
}

/* Apply FIR to one sample (or pass through if not active). */
static double sampler_fir(sampler_t *s, double x) {
    if (!s->fir_active) return x;
    s->fir_hist[s->fir_pos] = x;
    double y = 0.0;
    /* Convolve: y[n] = sum h[i] * x[n-i] with i=0..fir_len-1 */
    int pos = s->fir_pos;
    for (int i = 0; i < EQ_FILTER_LEN; i++) {
        int idx = pos - i;
        if (idx < 0) idx += EQ_FILTER_LEN;
        y += s->fir_taps[i] * s->fir_hist[idx];
    }
    s->fir_pos = (s->fir_pos + 1) % EQ_FILTER_LEN;
    return y;
}

/* Take one baud (NSYM samples) into out[]. */
static void sampler_take_baud(sampler_t *s, double *out) {
    for (int k = 0; k < NSYM; k++) {
        double raw = sampler_interp(s);
        out[k] = sampler_fir(s, raw);
    }
}

/* Take N raw samples (interpolated, but no FIR). Used during training data collection. */
static void sampler_take_raw(sampler_t *s, double *out, int n) {
    for (int k = 0; k < n; k++) out[k] = sampler_interp(s);
}

/* ==== Demux ==== */
static void demux_baud(const double *frame, cplx *out) {
    for (int c = 0; c < NFREQ; c++) {
        double re = 0.0, im = 0.0;
        const cplx *f = CARRIER_FILT[c];
        for (int k = 0; k < NSYM; k++) {
            re += frame[k] * f[k].re;
            im += frame[k] * f[k].im;
        }
        out[c].re = re;
        out[c].im = im;
    }
}

/* ==== Pilot detection (unchanged from prior version) ==== */
static long find_pilot(const double *samples, size_t n_samples) {
    cplx pf[NSYM];
    double s = 1.0 / sqrt(0.5 * NSYM);
    for (int k = 0; k < NSYM; k++) {
        pf[k].re =  cos(PILOT_OMEGA * k) * s;
        pf[k].im = -sin(PILOT_OMEGA * k) * s;
    }

    int counter = 0;
    long max_offset = (long)n_samples - NSYM;
    if (max_offset > 10L * FS) max_offset = 10L * FS;

    for (long offset = 0; offset <= max_offset; offset += NSYM) {
        const double *frame = samples + offset;
        double norm2 = 0.0;
        for (int k = 0; k < NSYM; k++) norm2 += frame[k] * frame[k];
        double norm = sqrt(norm2);
        if (norm == 0.0) { counter = 0; continue; }

        double dre = 0.0, dim = 0.0;
        for (int k = 0; k < NSYM; k++) {
            dre += pf[k].re * frame[k];
            dim += pf[k].im * frame[k];
        }
        double coh = sqrt(dre*dre + dim*dim) / norm;

        if (coh > COHERENCE_THR) counter++;
        else counter = 0;

        if (counter >= CARRIER_THR) {
            long begin = offset - (long)(CARRIER_THR - 1) * NSYM;
            if (begin < 0) begin = 0;
            return begin;
        }
    }
    return -1;
}

/* ==== Prefix verify: sample carrier 0 through the pilot region ==== */
static int prefix_verify(sampler_t *s, double gain) {
    int errors = 0;
    double buf[NSYM];
    for (int b = 0; b < EQ_LENGTH + EQ_SILENCE_LEN; b++) {
        sampler_take_baud(s, buf);
        cplx sym[NFREQ];
        demux_baud(buf, sym);
        double amp = sqrt(cabs2(sym[0])) * gain;
        int expected = (b < EQ_LENGTH) ? 1 : 0;
        int actual = (int)(amp + 0.5);
        if (actual != expected) errors++;
    }
    if (errors > 0) {
        fprintf(stderr, "Incorrect prefix: %d errors\n", errors);
        return -1;
    }
    return 0;
}

/* ==== Training symbol generation (matches JS TX) ==== */
static void gen_training(cplx train[EQ_LENGTH][NFREQ]) {
    unsigned reg = 1;
    const unsigned poly = 0x1100b;
    const unsigned mask = (1 << 2) - 1;
    /* Compute effective register size: highest bit index of poly. */
    unsigned prbs_size = 0;
    { unsigned p = poly; while ((p >> prbs_size) > 1) prbs_size++; }
    const cplx qpsk[4] = {{1,0}, {0,1}, {-1,0}, {0,-1}};
    for (int i = 0; i < EQ_LENGTH; i++) {
        for (int c = 0; c < NFREQ; c++) {
            unsigned v = reg & mask;
            reg <<= 1;
            if ((reg >> prbs_size) > 0) reg ^= poly;
            if (i < CONSTANT_PREFIX) {
                train[i][c].re = 1.0;
                train[i][c].im = 0.0;
            } else {
                train[i][c] = qpsk[v];
            }
        }
    }
}

/* Modulate NFREQ symbols into NSYM real samples (matches TX modulator). */
static void modulate_baud(const cplx sym[NFREQ], double *out) {
    for (int k = 0; k < NSYM; k++) {
        double acc = 0.0;
        for (int c = 0; c < NFREQ; c++) {
            double wk = 2.0 * M_PI * CARRIER_FREQS[c] * k / FS;
            acc += sym[c].re * cos(wk) - sym[c].im * sin(wk);
        }
        out[k] = acc;  /* NOT divided by NFREQ (per equalizer.py: modulator*NFREQ) */
    }
}

/* ==== Levinson-Durbin solver ====
 * Solve M x = y where M is symmetric Toeplitz with first column t.
 * O(N^2). N is small (20).
 */
void levinson_solve(const double *t, const double *y, double *x, int N) {
    /* B[n*N .. n*N+n] = backward vector at iteration n, of length (n+1).
     * We must keep every B[n] because the reconstruction step below uses
     * the b vector matching each n, not just the final one. */
    double *B = (double*)calloc((size_t)N * N, sizeof(double));
    double *f = (double*)calloc(N, sizeof(double));
    double *f_new = (double*)calloc(N, sizeof(double));
    double *b_new = (double*)calloc(N, sizeof(double));
    if (!B || !f || !f_new || !b_new) { free(B); free(f); free(f_new); free(b_new); return; }

    f[0] = 1.0 / t[0];
    B[0] = 1.0 / t[0];        /* B[0] = [1/t0] */

    for (int n = 1; n < N; n++) {
        double *b_prev = B + (n - 1) * N;   /* length n */
        double ef = 0.0, eb = 0.0;
        for (int i = 0; i < n; i++) {
            ef += t[n - i] * f[i];
            eb += t[i + 1] * b_prev[i];
        }
        double det = 1.0 - ef * eb;
        for (int i = 0; i <= n; i++) {
            double fi = (i < n) ? f[i] : 0.0;
            double bi = (i > 0) ? b_prev[i - 1] : 0.0;
            f_new[i] = (fi - ef * bi) / det;
            b_new[i] = (bi - eb * fi) / det;
        }
        memcpy(f, f_new, (size_t)(n + 1) * sizeof(double));
        memcpy(B + n * N, b_new, (size_t)(n + 1) * sizeof(double));
    }

    memset(x, 0, (size_t)N * sizeof(double));
    for (int n = 0; n < N; n++) {
        double ef = 0.0;
        for (int i = 0; i < n; i++) ef += t[n - i] * x[i];
        double gain = y[n] - ef;
        double *b_n = B + n * N;    /* length n+1 */
        for (int i = 0; i <= n; i++) x[i] += gain * b_n[i];
    }
    free(B); free(f); free(f_new); free(b_new);
}

/* ==== Equalizer training ====
 * Given received signal (length L) and expected signal (length L), find FIR taps
 * that map received → expected. Uses lookahead delay: expected is shifted right
 * by `lookahead`, x is padded right with `lookahead` zeros.
 */
void train_equalizer(const double *signal, const double *expected,
                            int L, double *taps) {
    int N = EQ_FILTER_LEN;
    int total = L + EQ_LOOKAHEAD;
    double *x = (double*)calloc(total, sizeof(double));
    double *y = (double*)calloc(total, sizeof(double));
    memcpy(x, signal, L * sizeof(double));                   /* [signal, 0*lookahead] */
    memcpy(y + EQ_LOOKAHEAD, expected, L * sizeof(double));  /* [0*lookahead, expected] */

    double Rxx[EQ_FILTER_LEN], Rxy[EQ_FILTER_LEN];
    for (int i = 0; i < N; i++) {
        double axx = 0.0, axy = 0.0;
        int len = total - i;
        for (int k = 0; k < len; k++) {
            axx += x[k + i] * x[k];
            axy += y[k + i] * x[k];
        }
        Rxx[i] = axx;
        Rxy[i] = axy;
    }
    levinson_solve(Rxx, Rxy, taps, N);
    free(x); free(y);
}

/* ==== 64-QAM slicing ==== */
static const double NORM_FACTOR = 4.9497474683058327;

static int slice_qam64(cplx s) {
    double xf = s.re * NORM_FACTOR + 3.5;
    double yf = s.im * NORM_FACTOR + 3.5;
    int x = (int)floor(xf + 0.5);
    int y = (int)floor(yf + 0.5);
    if (x < 0) x = 0; else if (x > 7) x = 7;
    if (y < 0) y = 0; else if (y > 7) y = 7;
    return x * 8 + y;
}

/* Convert constellation index to complex point (for error tracking) */
static cplx qam64_point(int idx) {
    double xv = (double)(idx / 8) - 3.5;
    double yv = (double)(idx % 8) - 3.5;
    cplx r = { xv / NORM_FACTOR, yv / NORM_FACTOR };
    return r;
}

/* ==== Frame extraction (CRC32 length-prefixed) ==== */
static uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return crc ^ 0xFFFFFFFF;
}

/* Hole marker byte written in place of dropped amodem frames.
 * The FEC decoder detects this pattern at chunk byte 125..128 to identify
 * blocks whose second half was corrupted by an L/R-straddle frame drop. */
#define HOLE_MARK 0xFE

static size_t extract_frames(const uint8_t *bits_bytes, size_t n_bytes,
                             uint8_t *out, size_t out_cap) {
    (void)out_cap;
    size_t out_pos = 0, in_pos = 0;
    int bad = 0, good = 0, holes_inserted = 0;
    long frame_num = 0;
    int synced = 0;
    int consec_bad = 0;

    while (in_pos < n_bytes) {
        uint8_t length = bits_bytes[in_pos];
        /* amodem emits length=254 (data block), 129 (odd-tail partial block),
         * or 4 (EOF). Any other value is either bit-slip noise or the tail of
         * a previous frame's data being read as if it were a length byte. */
        int length_valid = (length == 254 || length == 129 || length == 4) &&
                           in_pos + 1 + length <= n_bytes;
        if (!length_valid) {
            /* Invalid length byte. */
            if (synced && consec_bad < 3) {
                /* Presume this position IS a real frame boundary but the length
                 * byte is corrupted → drop it as a lost frame, emit 250 bytes of
                 * hole marker so the FEC scanner can mark straddling blocks
                 * missing. Advance by 255 (standard frame size). */
                memset(out + out_pos, HOLE_MARK, 250);
                out_pos += 250;
                holes_inserted++;
                in_pos += 255;
                bad++; frame_num++; consec_bad++;
            } else {
                /* Not synced, or too many consecutive failures — byte-search. */
                if (synced) synced = 0;
                in_pos++; bad++; frame_num++;
            }
            continue;
        }

        const uint8_t *frame   = bits_bytes + in_pos + 1;
        uint32_t recv_crc = ((uint32_t)frame[0] << 24) | ((uint32_t)frame[1] << 16)
                          | ((uint32_t)frame[2] << 8)  |  (uint32_t)frame[3];
        const uint8_t *payload = frame + 4;
        size_t payload_len = (size_t)length - 4;
        uint32_t calc_crc = crc32(payload, payload_len);

        if (recv_crc != calc_crc) {
            if (bad < 20) {
                fprintf(stderr,
                    "Invalid checksum @ frame #%ld (~%ld output bytes): %08x != %08x\n",
                    frame_num, frame_num * 250, recv_crc, calc_crc);
            }
            if (synced && consec_bad < 3) {
                /* Real amodem frame position, CRC failure → lost frame. Emit
                 * a hole of the payload's size so alignment is preserved. */
                memset(out + out_pos, HOLE_MARK, payload_len ? payload_len : 250);
                out_pos += (payload_len ? payload_len : 250);
                holes_inserted++;
                in_pos += 1 + length;
                bad++; frame_num++; consec_bad++;
            } else {
                if (synced) synced = 0;
                in_pos++; bad++; frame_num++;
            }
            if (bad > 2000000) { fprintf(stderr, "Too many bad frames, giving up\n"); break; }
            continue;
        }

        /* Good frame */
        in_pos += 1 + length;
        frame_num++;
        synced = 1;
        consec_bad = 0;
        if (payload_len == 0) {
            fprintf(stderr, "EOF frame detected (%d bad frames skipped, %d holes inserted)\n",
                    bad, holes_inserted);
            break;
        }
        memcpy(out + out_pos, payload, payload_len);
        out_pos += payload_len;
        good++;
    }
    fprintf(stderr, "Received %.3f kB (%d good, %d bad frames, %d holes)\n",
            out_pos / 1000.0, good, bad, holes_inserted);
    return out_pos;
}

/* ==== Main receiver ==== */
uint8_t *amodem_recv(const double *samples, size_t n_samples, size_t *out_len) {
    init_carriers();

    /* 1. Find pilot */
    long pilot_start = find_pilot(samples, n_samples);
    if (pilot_start < 0) {
        fprintf(stderr, "amodem: pilot not detected\n");
        return NULL;
    }
    fprintf(stderr, "amodem: pilot @ sample %ld (%.2f s)\n",
            pilot_start, pilot_start / (double)FS);

    /* 2. Estimate pilot amplitude AND per-baud freq_error via linear regression
     *    on unwrapped phase across the 200-baud pilot region. Skip 5 at each end. */
    #define PILOT_SKIP 5
    int n_pilot = EQ_LENGTH - 2 * PILOT_SKIP;    /* 190 */
    double *pilot_re = (double*)calloc(n_pilot, sizeof(double));
    double *pilot_im = (double*)calloc(n_pilot, sizeof(double));
    double amp_sum = 0.0;
    for (int b = 0; b < n_pilot; b++) {
        cplx sym[NFREQ];
        demux_baud(samples + pilot_start + (b + PILOT_SKIP) * NSYM, sym);
        pilot_re[b] = sym[0].re;
        pilot_im[b] = sym[0].im;
        amp_sum += sqrt(sym[0].re*sym[0].re + sym[0].im*sym[0].im);
    }
    double pilot_amp = amp_sum / n_pilot;
    double gain = 1.0 / pilot_amp;

    /* Compute phase in fractional cycles, unwrapped */
    double *phase_cyc = (double*)calloc(n_pilot, sizeof(double));
    double prev = atan2(pilot_im[0], pilot_re[0]) / (2.0 * M_PI);
    phase_cyc[0] = prev;
    for (int b = 1; b < n_pilot; b++) {
        double p = atan2(pilot_im[b], pilot_re[b]) / (2.0 * M_PI);
        /* Unwrap: keep p within 0.5 cycle of previous */
        while (p - prev > 0.5) p -= 1.0;
        while (p - prev < -0.5) p += 1.0;
        phase_cyc[b] = p;
        prev = p;
    }
    /* Linear regression: phase = a*index + b */
    double sx=0, sy=0, sxx=0, sxy=0;
    for (int i = 0; i < n_pilot; i++) {
        sx += i; sy += phase_cyc[i]; sxx += (double)i*i; sxy += (double)i*phase_cyc[i];
    }
    double slope = (n_pilot * sxy - sx * sy) / (n_pilot * sxx - sx * sx);
    /* freq_err = slope / (Tsym * Fc), where Tsym = NSYM/FS */
    double Tsym = (double)NSYM / (double)FS;
    double freq_err = slope / (Tsym * (double)CARRIER_FREQS[0]);
    fprintf(stderr, "amodem: pilot amp=%.4f, gain=%.4f, freq_err=%.3f ppm\n",
            pilot_amp, gain, freq_err * 1e6);
    free(pilot_re); free(pilot_im); free(phase_cyc);

    /* 3. Set up sampler at pilot_start with SFO-corrected freq */
    sampler_t sampler;
    sampler_init(&sampler, samples, n_samples, (double)pilot_start);
    sampler.freq = 1.0 / (1.0 + freq_err);

    /* 4. Verify prefix (consumes EQ_LENGTH + EQ_SILENCE_LEN bauds) */
    if (prefix_verify(&sampler, gain) < 0) return NULL;

    /* 5. Take training + lookahead samples in ONE take (matches amodem's
     *    `sampler.take(signal_length + lookahead)`). This advances the sampler
     *    exactly signal_len+lookahead*freq positions — leaving it aligned with
     *    the first data sample plus lookahead ahead-of-data, which combines
     *    with the FIR's lookahead-sample group delay to zero net alignment. */
    int signal_len = (EQ_LENGTH + 2 * EQ_SILENCE_LEN) * NSYM;
    int take_len = signal_len + EQ_LOOKAHEAD;
    double *train_signal_rx     = (double*)calloc(take_len, sizeof(double));  /* raw */
    double *train_signal_scaled = (double*)calloc(take_len, sizeof(double));  /* gain-applied for Levinson */
    if (!train_signal_rx || !train_signal_scaled) return NULL;
    sampler_take_raw(&sampler, train_signal_rx, take_len);
    for (int i = 0; i < take_len; i++) train_signal_scaled[i] = train_signal_rx[i] * gain;

    /* 6. Regenerate expected training signal. Length matches amodem's:
     *    [zeros(silence_len), train_signal, zeros(silence_len + lookahead)] over
     *    the region signal[prefix:-postfix] which INCLUDES lookahead samples
     *    past the training region. amodem's expected = [train_signal, zeros(lookahead)]
     *    is length equalizer_length*Nsym + lookahead = 6410. */
    double *train_signal_tx = (double*)calloc(take_len, sizeof(double));
    if (!train_signal_tx) { free(train_signal_rx); free(train_signal_scaled); return NULL; }
    cplx train_syms[EQ_LENGTH][NFREQ];
    gen_training(train_syms);
    for (int b = 0; b < EQ_LENGTH; b++) {
        double baud[NSYM];
        modulate_baud(train_syms[b], baud);
        int base = (EQ_SILENCE_LEN + b) * NSYM;
        memcpy(train_signal_tx + base, baud, NSYM * sizeof(double));
    }

    /* 7. Train FIR equalizer on the region signal[prefix:-postfix] which spans
     *    from index EQ_SILENCE_LEN*NSYM to take_len - EQ_SILENCE_LEN*NSYM.
     *    That's mid_len = EQ_LENGTH*NSYM + EQ_LOOKAHEAD samples. Expected is
     *    [train_signal, zeros(lookahead)] over the same span. */
    int mid_start = EQ_SILENCE_LEN * NSYM;
    int mid_len = EQ_LENGTH * NSYM + EQ_LOOKAHEAD;
    train_equalizer(train_signal_scaled + mid_start, train_signal_tx + mid_start,
                    mid_len, sampler.fir_taps);
    fprintf(stderr, "amodem: FIR taps [");
    for (int i = 0; i < EQ_FILTER_LEN; i++) fprintf(stderr, "%s%.3f", i?",":"", sampler.fir_taps[i]);
    fprintf(stderr, "]\n");
    free(train_signal_tx);

    /* 8. Warm up FIR history by feeding the taken samples through the FIR.
     *    sampler.offset is already positioned correctly — do NOT touch it.
     *    Feeding all take_len samples means fir_hist holds the last EQ_FILTER_LEN
     *    of them, and the next FIR call will output the first "data" sample
     *    (delayed by lookahead, which was baked into the take). */
    sampler.fir_active = 1;
    for (int i = 0; i < take_len; i++) (void)sampler_fir(&sampler, train_signal_rx[i]);
    free(train_signal_rx);
    free(train_signal_scaled);

    /* 9. Data phase */
    long data_bauds_avail = ((long)n_samples - (long)sampler.offset) / NSYM;
    if (data_bauds_avail <= 0) {
        fprintf(stderr, "amodem: not enough data samples\n");
        return NULL;
    }

    size_t bits_bytes_cap = (size_t)data_bauds_avail * (BITS_PER_BAUD / 8) + 16;
    uint8_t *bits_bytes = (uint8_t*)calloc(bits_bytes_cap, 1);
    if (!bits_bytes) return NULL;
    size_t bit_pos = 0;

    /* Per-carrier gain still needed since equalizer only aligns time-domain waveform,
     * not per-carrier amplitude. Estimate on first CONSTANT_PREFIX data-like bauds
     * from the training we already processed?  No — those are gone. Instead we use
     * training expected: since expected training was designed such that demux gives
     * exactly the training symbol, and FIR equalizes signal→expected, demuxing the
     * equalized data should give the true symbols. So no additional gain needed.
     * BUT we do need to scale by the same `gain` as we did for training. */
    /* Actually — since we scaled train_signal_rx by gain BEFORE training, the FIR taps
     * incorporate that gain. So the FIR output on unscaled samples would be too small.
     * Fix: scale post-FIR samples by gain. */

    /* SFO tracking accumulators */
    double err_sum_re = 0.0, err_sum_im = 0.0;
    int err_count = 0;
    long baud_count = 0;
    long last_offset_report = 0;

    while (data_bauds_avail-- > 0 && (size_t)sampler.offset + NSYM < n_samples) {
        double buf[NSYM];
        sampler_take_baud(&sampler, buf);
        for (int k = 0; k < NSYM; k++) buf[k] *= gain;

        cplx sym[NFREQ];
        demux_baud(buf, sym);
        for (int c = 0; c < NFREQ; c++) {
            int idx = slice_qam64(sym[c]);
            /* SFO error: received / decoded (complex ratio, phase = timing drift) */
            cplx decoded = qam64_point(idx);
            double dnorm2 = cabs2(decoded);
            if (dnorm2 > 1e-6) {
                cplx ratio = cdiv(sym[c], decoded);
                err_sum_re += ratio.re;
                err_sum_im += ratio.im;
                err_count++;
            }
            for (int k = 0; k < BITS_PER_SYM; k++) {
                int bit = (idx >> k) & 1;
                size_t byte_idx = bit_pos >> 3;
                size_t bit_off = bit_pos & 7;
                if (bit) bits_bytes[byte_idx] |= (uint8_t)(1u << bit_off);
                bit_pos++;
            }
        }
        baud_count++;

        if (baud_count % ITERS_PER_UPDATE == 0 && err_count > 0) {
            /* Update sampler based on mean phase of received/decoded ratios */
            double mean_re = err_sum_re / err_count;
            double mean_im = err_sum_im / err_count;
            double err_phase = atan2(mean_im, mean_re) / (2.0 * M_PI);   /* fraction of cycle */
            sampler.freq -= FREQ_ERR_GAIN * err_phase;
            sampler.offset -= err_phase;
            err_sum_re = err_sum_im = 0.0;
            err_count = 0;
            if (baud_count - last_offset_report >= 20000) {
                fprintf(stderr, "amodem: baud %ld, sampler.freq=%.7f (drift %+.1f ppm)\n",
                        baud_count, sampler.freq, (1.0 - sampler.freq) * 1e6);
                last_offset_report = baud_count;
            }
        }
    }
    size_t bits_bytes_len = (bit_pos + 7) / 8;

    size_t out_cap = bits_bytes_len + 1024;
    uint8_t *out = (uint8_t*)malloc(out_cap);
    if (!out) { free(bits_bytes); return NULL; }
    size_t got = extract_frames(bits_bytes, bits_bytes_len, out, out_cap);
    free(bits_bytes);
    *out_len = got;
    return out;
}
