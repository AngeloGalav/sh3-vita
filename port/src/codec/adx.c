/* adx.c — CRI ADX (type 3, 4-bit ADPCM) decoder. See adx.h.
 *
 * Bit-exact with ../tools/j3_audio.py (adx_header/adx_coefs/adx_decode_frames).
 * All arithmetic is plain int32/double; the only libm calls are sqrt, cos and
 * floor, matching the reference's math module calls one for one.
 */
#include "adx.h"

#include <math.h>
#include <string.h>

/* Closest double to pi; identical to Python's math.pi. */
#define ADX_PI 3.14159265358979323846

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

int adx_parse(const uint8_t *buf, size_t len, adx_info_t *info)
{
    size_t base;

    if (info == NULL) {
        return -1;
    }
    memset(info, 0, sizeof(*info));
    if (buf == NULL || len < 0x14 || buf[0] != 0x80 || buf[1] != 0x00) {
        return -1;
    }
    info->data_off = (uint32_t)be16(buf + 2) + 4u;
    info->encoding = buf[4];
    info->block_size = buf[5];
    info->bits_per_sample = buf[6];
    info->channels = buf[7];
    info->sample_rate = be32(buf + 8);
    info->num_samples = be32(buf + 12);
    info->cutoff = be16(buf + 0x10);
    info->version = buf[0x12];
    info->flags = buf[0x13];

    /* Loop info: version 3 at +0x18, version 4 at +0x24 (after three u32
     * history words). Present when the header length (copyright offset + 4)
     * covers the 20-byte block, the buffer holds it, and the flag is set. */
    if (info->version == 3) {
        base = 0x18;
    } else if (info->version == 4) {
        base = 0x24;
    } else {
        base = 0;
    }
    if (base != 0 &&
        (size_t)info->data_off >= base + 20u &&
        len >= base + 20u) {
        if (be32(buf + base) != 0) {
            info->has_loop = 1;
            info->loop_start_sample = be32(buf + base + 4);
            info->loop_start_offset = be32(buf + base + 8);
            info->loop_end_sample = be32(buf + base + 12);
            info->loop_end_offset = be32(buf + base + 16);
        }
    }
    return 0;
}

void adx_coefficients(uint16_t cutoff, uint32_t rate, int32_t *c1, int32_t *c2)
{
    double a = sqrt(2.0) - cos(((2.0 * ADX_PI * (double)cutoff) / (double)rate));
    double bb = sqrt(2.0) - 1.0;
    double c = (a - sqrt((a + bb) * (a - bb))) / bb;
    *c1 = (int32_t)floor(c * 8192.0);
    *c2 = (int32_t)floor(c * c * -4096.0);
}

/* Arithmetic shift right by 12 (floor division by 4096), portable form;
 * matches Python's >> on negative ints. Inputs are far from INT32_MIN:
 * |c1*h1 + c2*h2| < 2^30 for int16 histories. */
static int32_t asr12(int32_t x)
{
    return x >= 0 ? (x >> 12) : -(((-x) + 4095) >> 12);
}

size_t adx_decoded_samples(size_t len, const adx_info_t *info)
{
    size_t nblocks;
    size_t total;
    if (info == NULL || info->channels == 0) {
        return 0;
    }
    nblocks = len / ((size_t)info->block_size * (size_t)info->channels);
    total = nblocks * 32u;
    if ((uint64_t)info->num_samples < (uint64_t)total) {
        total = (size_t)info->num_samples;
    }
    return total;
}

long adx_decode_frames(const uint8_t *frames, size_t len,
                       const adx_info_t *info, int16_t *pcm, size_t cap)
{
    int ch, c, i;
    size_t nblocks, total, blk;
    int32_t c1, c2;
    int32_t h1[ADX_MAX_CHANNELS], h2[ADX_MAX_CHANNELS];

    if (frames == NULL || info == NULL ||
        info->encoding != 3 || info->bits_per_sample != 4 ||
        info->block_size != 18 || info->channels == 0 ||
        info->channels > ADX_MAX_CHANNELS) {
        return -1;
    }
    ch = info->channels;
    nblocks = len / ((size_t)18 * (size_t)ch);
    total = adx_decoded_samples(len, info);
    if (total > cap) {
        return -2;
    }
    if (total == 0) {
        return 0;
    }
    if (pcm == NULL) {
        return -1;
    }
    adx_coefficients(info->cutoff, info->sample_rate, &c1, &c2);
    for (c = 0; c < ch; c++) {
        h1[c] = 0;
        h2[c] = 0;
    }

    for (blk = 0; blk < nblocks; blk++) {
        size_t base = blk * 32u;
        for (c = 0; c < ch; c++) {
            size_t o = ((blk * (size_t)ch) + (size_t)c) * 18u;
            int32_t scale = (int32_t)((((uint32_t)frames[o] << 8) |
                                      (uint32_t)frames[o + 1]) & 0x1FFFu) + 1;
            int32_t a1 = h1[c], a2 = h2[c];
            for (i = 0; i < 32; i++) {
                uint8_t byte = frames[o + 2 + (i >> 1)];
                int32_t n = ((i & 1) == 0) ? (int32_t)(byte >> 4)
                                           : (int32_t)(byte & 15);
                int32_t s;
                if (n >= 8) {
                    n -= 16;
                }
                s = n * scale + asr12(c1 * a1 + c2 * a2);
                if (s > 32767) {
                    s = 32767;
                } else if (s < -32768) {
                    s = -32768;
                }
                a2 = a1;
                a1 = s;
                if (base + (size_t)i < total) {
                    pcm[(base + (size_t)i) * (size_t)ch + (size_t)c] = (int16_t)s;
                }
            }
            h1[c] = a1;
            h2[c] = a2;
        }
    }
    return (long)total;
}

long adx_decode(const uint8_t *buf, size_t len, int16_t *pcm, size_t cap)
{
    adx_info_t info;
    int r = adx_parse(buf, len, &info);
    if (r != 0) {
        return (long)r;
    }
    if ((size_t)info.data_off > len) {
        return -1;
    }
    return adx_decode_frames(buf + info.data_off, len - info.data_off,
                             &info, pcm, cap);
}
