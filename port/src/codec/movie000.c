/* movie000.c — SH3 `.000` movie demangler. See movie000.h.
 * Byte-wise little-endian word access: no alignment or endianness
 * assumptions, matching j3_movie.py's struct "<I" semantics. */
#include "movie000.h"

uint32_t movie000_lcg_step(uint32_t state)
{
    return state * 0x5D588B65u + 1u;
}

uint32_t movie000_seed(const uint8_t *hdr)
{
    return (uint32_t)hdr[0x2DDE] |
           ((uint32_t)hdr[0x3D40] << 8) |
           ((uint32_t)hdr[0x2A5C] << 16) |
           ((uint32_t)hdr[0x55E2] << 24);
}

uint32_t movie000_base_state(const uint8_t *hdr)
{
    uint32_t st = movie000_seed(hdr);
    int i;
    for (i = 0; i < MOVIE000_HEADER_SIZE / 4; i++) {
        st = movie000_lcg_step(st);
    }
    return st;
}

void movie000_decrypt(uint8_t *buf, size_t len, uint32_t base)
{
    size_t c;
    for (c = 0; c < len; c += MOVIE000_PERIOD) {
        size_t clen = len - c;
        size_t words, i;
        uint32_t st = base;
        if (clen > MOVIE000_PERIOD) {
            clen = MOVIE000_PERIOD;
        }
        words = clen / 4;
        for (i = 0; i < words; i++) {
            uint8_t *p = buf + c + i * 4;
            uint32_t v;
            st = movie000_lcg_step(st);
            v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            v ^= st;
            p[0] = (uint8_t)(v & 0xFFu);
            p[1] = (uint8_t)((v >> 8) & 0xFFu);
            p[2] = (uint8_t)((v >> 16) & 0xFFu);
            p[3] = (uint8_t)((v >> 24) & 0xFFu);
        }
    }
}
