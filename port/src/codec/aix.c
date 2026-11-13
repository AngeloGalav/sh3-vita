/* aix.c — CRI AIXF demuxer. See aix.h. Mirrors ../tools/j3_audio.py
 * (aix_streams) chunk walk exactly, including the short-payload clamp that
 * Python's slicing performs implicitly. */
#include "aix.h"

#include <string.h>

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int find_stream(const aix_stream_t *out, int n, uint8_t id)
{
    int i;
    for (i = 0; i < n; i++) {
        if (out[i].id == id) {
            return i;
        }
    }
    return -1;
}

int aix_parse(const uint8_t *buf, size_t len, aix_stream_t *out, int max_streams)
{
    uint32_t hs;
    size_t p;
    int n = 0;

    if (buf == NULL || len < 8 || memcmp(buf, "AIXF", 4) != 0) {
        return -1;
    }
    hs = be32(buf + 4);
    p = 8 + (size_t)hs;
    while (p + 16 <= len && memcmp(buf + p, "AIXP", 4) == 0) {
        uint32_t size = be32(buf + p + 4);
        uint8_t sid = buf[p + 8];
        uint32_t w = be16(buf + p + 10);
        uint32_t ctr = be32(buf + p + 12);
        size_t payload = p + 16;
        size_t pend = payload + w;
        int idx;
        if (pend > len) {
            pend = len; /* Python's b[p+16:p+16+w] clamps the same way */
        }
        idx = find_stream(out, n, sid);
        if (idx < 0) {
            if (n >= max_streams) {
                return -2;
            }
            idx = n;
            out[n].id = sid;
            out[n].adx_header = NULL;
            out[n].adx_header_len = 0;
            out[n].payload_size = 0;
            n++;
        }
        if (ctr == 0xFFFFFFFFu) {
            out[idx].adx_header = buf + payload;
            out[idx].adx_header_len = (uint32_t)(pend - payload);
        } else {
            out[idx].payload_size += (uint32_t)(pend - payload);
        }
        p += 8 + (size_t)size;
    }
    return n;
}

long aix_collect(const uint8_t *buf, size_t len, const aix_stream_t *stream,
                 uint8_t *dst, size_t dst_cap)
{
    uint32_t hs;
    size_t p;
    size_t total = 0;

    if (buf == NULL || stream == NULL || len < 8 ||
        memcmp(buf, "AIXF", 4) != 0) {
        return -1;
    }
    if ((uint64_t)stream->payload_size > (uint64_t)dst_cap) {
        return -2;
    }
    hs = be32(buf + 4);
    p = 8 + (size_t)hs;
    while (p + 16 <= len && memcmp(buf + p, "AIXP", 4) == 0) {
        uint32_t size = be32(buf + p + 4);
        uint8_t sid = buf[p + 8];
        uint32_t w = be16(buf + p + 10);
        uint32_t ctr = be32(buf + p + 12);
        size_t payload = p + 16;
        size_t pend = payload + w;
        if (pend > len) {
            pend = len;
        }
        if (sid == stream->id && ctr != 0xFFFFFFFFu) {
            memcpy(dst + total, buf + payload, pend - payload);
            total += pend - payload;
        }
        p += 8 + (size_t)size;
    }
    return (long)total;
}
