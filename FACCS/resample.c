/* resample.c — windowed-sinc interpolation resampler.
 * For each output sample, evaluate a windowed sinc filter across nearby input samples.
 * O(N_out * W). Slower than polyphase but straightforward to verify.
 */
#include "decode.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double bessel_i0(double x) {
    double sum = 1.0, term = 1.0;
    double x2 = x * x / 4.0;
    for (int k = 1; k < 25; k++) {
        term *= x2 / (k * k);
        sum += term;
        if (term < 1e-14 * sum) break;
    }
    return sum;
}

double *resample_to_32khz(const double *in, size_t n_in, int in_sr, size_t *n_out) {
    if (in_sr == FS) {
        double *out = (double*)malloc(sizeof(double) * n_in);
        if (!out) return NULL;
        memcpy(out, in, sizeof(double) * n_in);
        *n_out = n_in;
        return out;
    }

    double cutoff = (FS < in_sr) ? (double)FS / (2.0 * in_sr) : 0.5;
    int W = 40;
    double beta = 8.6;
    double denom = bessel_i0(beta);

    size_t n_new = (size_t)((double)n_in * FS / in_sr + 0.5) + 1;
    double *out = (double*)calloc(n_new, sizeof(double));
    if (!out) return NULL;

    double step = (double)in_sr / (double)FS;

    for (size_t m = 0; m < n_new; m++) {
        double t_in = (double)m * step;
        long center = (long)floor(t_in);

        double acc = 0.0;
        for (int k = -W; k <= W; k++) {
            long n = center + k;
            if (n < 0 || (size_t)n >= n_in) continue;
            double u = t_in - (double)n;
            if (fabs(u) > W) continue;
            double arg = 2.0 * cutoff * u;
            double sinc = (fabs(arg) < 1e-12) ? 1.0 : sin(M_PI * arg) / (M_PI * arg);
            double w_arg = u / (double)W;
            double kaiser = bessel_i0(beta * sqrt(fmax(0.0, 1.0 - w_arg * w_arg))) / denom;
            acc += in[n] * 2.0 * cutoff * sinc * kaiser;
        }
        out[m] = acc;
    }
    *n_out = n_new;
    return out;
}
