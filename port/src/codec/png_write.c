/* png_write.c — minimal PNG writer using stored (uncompressed) deflate
 * blocks, so no zlib is required. Test support only. */
#include "png_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long crc_table[256];
static int crc_table_ready = 0;

static void crc_init(void)
{
    unsigned long n, k, c;
    for (n = 0; n < 256; n++) {
        c = n;
        for (k = 0; k < 8; k++) {
            c = (c & 1) ? (0xEDB88320uL ^ (c >> 1)) : (c >> 1);
        }
        crc_table[n] = c;
    }
    crc_table_ready = 1;
}

static unsigned long crc_upd(unsigned long crc, const unsigned char *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        crc = crc_table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

static void put_be32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static int write_chunk(FILE *f, const char *type, const unsigned char *data, size_t len)
{
    unsigned char hdr[8];
    unsigned long crc;
    put_be32(hdr, (unsigned long)len);
    memcpy(hdr + 4, type, 4);
    if (fwrite(hdr, 1, 8, f) != 8) {
        return -1;
    }
    if (len && fwrite(data, 1, len, f) != len) {
        return -1;
    }
    crc = crc_upd(0xFFFFFFFFuL, (const unsigned char *)type, 4);
    if (data && len) {
        crc = crc_upd(crc, data, len);
    }
    put_be32(hdr, crc ^ 0xFFFFFFFFuL);
    return fwrite(hdr, 1, 4, f) == 4 ? 0 : -1;
}

int png_write_rgb(const char *path, int w, int h,
                  const unsigned char *rgb, int stride)
{
    static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    FILE *f;
    unsigned char ihdr[13];
    unsigned char *raw = NULL, *z = NULL;
    size_t rowlen, rawlen, zcap, zpos;
    int y;
    unsigned long adler_s1 = 1, adler_s2 = 0;

    if (!crc_table_ready) {
        crc_init();
    }
    if (w <= 0 || h <= 0 || rgb == NULL) {
        return -1;
    }
    rowlen = (size_t)w * 3u + 1u;
    rawlen = rowlen * (size_t)h;
    raw = (unsigned char *)malloc(rawlen);
    if (!raw) {
        return -1;
    }
    for (y = 0; y < h; y++) {
        raw[(size_t)y * rowlen] = 0; /* filter: None */
        memcpy(raw + (size_t)y * rowlen + 1, rgb + (size_t)y * (size_t)stride,
               (size_t)w * 3u);
    }
    /* zlib stream with stored deflate blocks (max 65535 bytes each). */
    {
        size_t nblocks = rawlen / 65535u + 1u;
        zcap = 2u + nblocks * 5u + rawlen + 4u;
    }
    z = (unsigned char *)malloc(zcap);
    if (!z) {
        free(raw);
        return -1;
    }
    zpos = 0;
    z[zpos++] = 0x78; /* CMF: deflate, 32K window */
    z[zpos++] = 0x01; /* FLG: no dict, CINFO small; stored blocks */
    {
        size_t off = 0;
        while (off < rawlen) {
            size_t len = rawlen - off;
            int last;
            if (len > 65535u) {
                len = 65535u;
            }
            last = (off + len >= rawlen);
            z[zpos++] = (unsigned char)(last ? 1 : 0);
            z[zpos++] = (unsigned char)(len & 0xFFu);
            z[zpos++] = (unsigned char)((len >> 8) & 0xFFu);
            z[zpos++] = (unsigned char)(~len & 0xFFu);
            z[zpos++] = (unsigned char)((~len >> 8) & 0xFFu);
            memcpy(z + zpos, raw + off, len);
            zpos += len;
            off += len;
        }
    }
    {
        size_t i;
        for (i = 0; i < rawlen; i++) {
            adler_s1 = (adler_s1 + raw[i]) % 65521uL;
            adler_s2 = (adler_s2 + adler_s1) % 65521uL;
        }
    }
    put_be32(z + zpos, (adler_s2 << 16) | adler_s1);
    zpos += 4;

    f = fopen(path, "wb");
    if (!f) {
        free(raw);
        free(z);
        return -1;
    }
    if (fwrite(sig, 1, 8, f) != 8) {
        fclose(f); free(raw); free(z); return -1;
    }
    put_be32(ihdr, (unsigned long)w);
    put_be32(ihdr + 4, (unsigned long)h);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 2;  /* color type: truecolor RGB */
    ihdr[10] = 0; /* deflate */
    ihdr[11] = 0;
    ihdr[12] = 0;
    if (write_chunk(f, "IHDR", ihdr, 13) != 0 ||
        write_chunk(f, "IDAT", z, zpos) != 0 ||
        write_chunk(f, "IEND", NULL, 0) != 0) {
        fclose(f); free(raw); free(z); return -1;
    }
    fclose(f);
    free(raw);
    free(z);
    return 0;
}
