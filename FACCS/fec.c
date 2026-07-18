/* fec.c — Reed-Solomon erasure decoding over GF(2^8), plus wrapper for FEC v1/v2.
 * See decode.h for constants.
 */
#include "decode.h"
#include <stdlib.h>
#include <string.h>

/* GF(2^8) tables, polynomial 0x11D */
static uint8_t GF_EXP[512];
static uint8_t GF_LOG[256];

static void gf_init(void) {
    static int done = 0;
    if (done) return;
    done = 1;
    int x = 1;
    for (int i = 0; i < 255; i++) {
        GF_EXP[i] = (uint8_t)x;
        GF_LOG[x] = (uint8_t)i;
        x <<= 1;
        if (x & 0x100) x ^= 0x11D;
    }
    for (int i = 255; i < 512; i++) GF_EXP[i] = GF_EXP[i - 255];
}

static uint8_t gf_mul(uint8_t a, uint8_t b) {
    if (a == 0 || b == 0) return 0;
    return GF_EXP[(int)GF_LOG[a] + (int)GF_LOG[b]];
}
static uint8_t gf_div(uint8_t a, uint8_t b) {
    if (a == 0) return 0;
    int d = (int)GF_LOG[a] - (int)GF_LOG[b];
    if (d < 0) d += 255;
    return GF_EXP[d];
}

/* Compute Lagrange coefficients for interpolating from received positions to missing position.
 * xs[i] = i+1 in GF(2^8). Returns coefficients c[k] such that
 *   value at x_missing = sum_k received[k] * c[k].
 */
static void lagrange_coeffs(const int *received, int nreceived, int missing,
                            int total, uint8_t *out) {
    (void)total;
    uint8_t x_m = (uint8_t)(missing + 1);
    for (int r = 0; r < nreceived; r++) {
        uint8_t x_r = (uint8_t)(received[r] + 1);
        uint8_t num = 1, den = 1;
        for (int k = 0; k < nreceived; k++) {
            if (k == r) continue;
            uint8_t x_k = (uint8_t)(received[k] + 1);
            num = gf_mul(num, x_m ^ x_k);
            den = gf_mul(den, x_r ^ x_k);
        }
        out[r] = gf_div(num, den);
    }
}

/* FEC decoder — handles v1 (XOR, 1 parity) and v2 (RS, M parity). */
#define FEC_MAGIC_HDR    "\xFE\xC0\x00\x00"
#define FEC_MAGIC_V1     "\xFE\xC0\x00\x01"
#define FEC_MAGIC_V2     "\xFE\xC0\x00\x02"

/* Small hash: gid -> array of (bid, ptr) pairs. Since groups are small, linear scan is fine. */
typedef struct {
    int bid;
    const uint8_t *payload;   /* points into original data buffer */
} block_ref;

typedef struct {
    int gid;
    int nblocks;
    block_ref blocks[FEC_K + FEC_M + 4];   /* small slack */
} group_state;

uint8_t *fec_decode(const uint8_t *data, size_t data_len, size_t *out_len) {
    gf_init();

    uint32_t file_len = 0, num_groups = 0, num_data = 0;
    uint16_t K = 0, M = 0, version = 1;
    int have_header = 0;

    /* First pass: find header, count groups */
    int max_gid = -1;
    for (size_t i = 0; i + FEC_BLOCK <= data_len; i += FEC_BLOCK) {
        const uint8_t *chunk = data + i;
        if (!memcmp(chunk, FEC_MAGIC_HDR, 4)) {
            if (!have_header) {
                const uint8_t *p = chunk + FEC_HDR;
                file_len = ((uint32_t)p[0]<<24) | ((uint32_t)p[1]<<16) | ((uint32_t)p[2]<<8) | p[3];
                K = ((uint16_t)p[4]<<8) | p[5];
                num_groups = ((uint32_t)p[6]<<24) | ((uint32_t)p[7]<<16) | ((uint32_t)p[8]<<8) | p[9];
                num_data = ((uint32_t)p[10]<<24) | ((uint32_t)p[11]<<16) | ((uint32_t)p[12]<<8) | p[13];
                M = ((uint16_t)p[14]<<8) | p[15];
                version = ((uint16_t)p[16]<<8) | p[17];
                if (version == 0) version = 1;   /* legacy header */
                have_header = 1;
                fprintf(stderr, "[FEC v%u] header: %uB file, K=%u, M=%u, %u groups, %u data blocks\n",
                        version, file_len, K, M, num_groups, num_data);
            }
        } else if (!memcmp(chunk, FEC_MAGIC_V1, 4) || !memcmp(chunk, FEC_MAGIC_V2, 4)) {
            int gid = ((int)chunk[4]<<8) | chunk[5];
            if (gid > max_gid) max_gid = gid;
        }
    }
    if (!have_header) {
        fprintf(stderr, "[FEC] ERROR: no global header found\n");
        return NULL;
    }
    if (K > FEC_K + FEC_M || M > FEC_M) {
        fprintf(stderr, "[FEC] ERROR: config K=%u M=%u exceeds compiled limits\n", K, M);
        return NULL;
    }

    /* Second pass: collect blocks by group. Allocate groups array. */
    group_state *groups = (group_state*)calloc(num_groups, sizeof(group_state));
    if (!groups) return NULL;
    for (uint32_t g = 0; g < num_groups; g++) groups[g].gid = (int)g;

    /* Hole marker inserted by extract_frames for dropped amodem frames.
     * If a chunk's byte 125..128 are 4 consecutive HOLE_MARK, the FEC block's
     * second half was corrupted by a straddling frame drop — skip it. */
    #define HOLE_MARK 0xFE
    int holes_detected = 0;

    for (size_t i = 0; i + FEC_BLOCK <= data_len; i += FEC_BLOCK) {
        const uint8_t *chunk = data + i;
        if (memcmp(chunk, FEC_MAGIC_V1, 4) && memcmp(chunk, FEC_MAGIC_V2, 4)) continue;
        /* Hole detection: chunks whose payload tail is HOLE_MARK are corrupt. */
        if (chunk[125] == HOLE_MARK && chunk[126] == HOLE_MARK &&
            chunk[127] == HOLE_MARK && chunk[128] == HOLE_MARK) {
            holes_detected++;
            continue;
        }
        int gid = ((int)chunk[4]<<8) | chunk[5];
        int bid = ((int)chunk[6]<<8) | chunk[7];
        if (gid < 0 || (uint32_t)gid >= num_groups) continue;
        if (bid < 0 || bid >= K + M) continue;
        /* De-dup: overwrite if we already have this bid */
        group_state *gs = &groups[gid];
        int found = -1;
        for (int j = 0; j < gs->nblocks; j++) {
            if (gs->blocks[j].bid == bid) { found = j; break; }
        }
        if (found >= 0) {
            gs->blocks[found].payload = chunk + FEC_HDR;
        } else if (gs->nblocks < (int)(sizeof(gs->blocks)/sizeof(gs->blocks[0]))) {
            gs->blocks[gs->nblocks].bid = bid;
            gs->blocks[gs->nblocks].payload = chunk + FEC_HDR;
            gs->nblocks++;
        }
    }
    if (holes_detected)
        fprintf(stderr, "[FEC] %d chunks skipped due to hole marker\n", holes_detected);

    /* Third pass: for each group, recover missing data blocks. */
    uint8_t *out = (uint8_t*)calloc((size_t)num_data * FEC_PAYLOAD, 1);
    if (!out) { free(groups); return NULL; }
    int recovered = 0, lost_groups = 0, lost_blocks = 0;

    for (uint32_t g = 0; g < num_groups; g++) {
        group_state *gs = &groups[g];

        /* Find which data blocks (bid 0..K-1) are present */
        const uint8_t *data_blocks[FEC_K + FEC_M];
        int have_bid[FEC_K + FEC_M];
        for (int j = 0; j < K + M; j++) { data_blocks[j] = NULL; have_bid[j] = 0; }
        for (int j = 0; j < gs->nblocks; j++) {
            int bid = gs->blocks[j].bid;
            if (bid < K + M) {
                data_blocks[bid] = gs->blocks[j].payload;
                have_bid[bid] = 1;
            }
        }

        /* How many data-blocks (bid < K) are missing and needed? */
        int needed_count = 0;
        int missing_data[FEC_K];
        int n_missing = 0;
        for (int b = 0; b < K; b++) {
            if ((uint32_t)(g * K + b) < num_data) {
                needed_count++;
                if (!have_bid[b]) {
                    missing_data[n_missing++] = b;
                }
            }
        }

        if (n_missing == 0) {
            /* All data present */
            for (int b = 0; b < K; b++) {
                uint32_t idx = g * K + b;
                if (idx >= num_data) break;
                memcpy(out + (size_t)idx * FEC_PAYLOAD, data_blocks[b], FEC_PAYLOAD);
            }
            continue;
        }

        if (version <= 1) {
            /* v1: single XOR parity at bid=K. Recover 1 erasure. */
            if (n_missing == 1 && have_bid[K]) {
                int miss = missing_data[0];
                uint8_t recovered_block[FEC_PAYLOAD];
                memcpy(recovered_block, data_blocks[K], FEC_PAYLOAD);
                for (int other = 0; other < K; other++) {
                    if (other != miss && have_bid[other]) {
                        for (int j = 0; j < FEC_PAYLOAD; j++)
                            recovered_block[j] ^= data_blocks[other][j];
                    }
                }
                data_blocks[miss] = NULL;   /* mark as we use recovered_block below */
                for (int b = 0; b < K; b++) {
                    uint32_t idx = g * K + b;
                    if (idx >= num_data) break;
                    if (b == miss) memcpy(out + (size_t)idx * FEC_PAYLOAD, recovered_block, FEC_PAYLOAD);
                    else if (data_blocks[b]) memcpy(out + (size_t)idx * FEC_PAYLOAD, data_blocks[b], FEC_PAYLOAD);
                }
                recovered++;
            } else {
                lost_groups++;
                lost_blocks += n_missing;
                for (int b = 0; b < K; b++) {
                    uint32_t idx = g * K + b;
                    if (idx >= num_data) break;
                    if (data_blocks[b]) memcpy(out + (size_t)idx * FEC_PAYLOAD, data_blocks[b], FEC_PAYLOAD);
                }
            }
            continue;
        }

        /* v2: Reed-Solomon. Need K received blocks total. */
        int received_positions[FEC_K + FEC_M];
        int n_received = 0;
        for (int b = 0; b < K + M && n_received < K; b++) {
            if (have_bid[b]) received_positions[n_received++] = b;
        }
        if (n_received < K) {
            lost_groups++;
            lost_blocks += n_missing;
            for (int b = 0; b < K; b++) {
                uint32_t idx = g * K + b;
                if (idx >= num_data) break;
                if (data_blocks[b]) memcpy(out + (size_t)idx * FEC_PAYLOAD, data_blocks[b], FEC_PAYLOAD);
            }
            continue;
        }

        /* Recover each missing data block via Lagrange */
        uint8_t recovered_data[FEC_K][FEC_PAYLOAD];
        for (int m_i = 0; m_i < n_missing; m_i++) {
            int miss = missing_data[m_i];
            uint8_t coeffs[FEC_K];
            lagrange_coeffs(received_positions, K, miss, K + M, coeffs);
            for (int bp = 0; bp < FEC_PAYLOAD; bp++) {
                uint8_t acc = 0;
                for (int r = 0; r < K; r++) {
                    acc ^= gf_mul(coeffs[r], data_blocks[received_positions[r]][bp]);
                }
                recovered_data[m_i][bp] = acc;
            }
            recovered++;
        }

        /* Emit */
        for (int b = 0; b < K; b++) {
            uint32_t idx = g * K + b;
            if (idx >= num_data) break;
            if (have_bid[b]) {
                memcpy(out + (size_t)idx * FEC_PAYLOAD, data_blocks[b], FEC_PAYLOAD);
            } else {
                /* look up in missing_data → recovered_data */
                for (int m_i = 0; m_i < n_missing; m_i++) {
                    if (missing_data[m_i] == b) {
                        memcpy(out + (size_t)idx * FEC_PAYLOAD, recovered_data[m_i], FEC_PAYLOAD);
                        break;
                    }
                }
            }
        }
    }

    fprintf(stderr, "[FEC v%u] %d recovered; %d groups lost (%d blocks)\n",
            version, recovered, lost_groups, lost_blocks);
    free(groups);
    *out_len = file_len;
    return out;
}
