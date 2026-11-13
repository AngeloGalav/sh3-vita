/* aix.h — CRI AIXF demuxer: splits an AIXF entry into its ADX substreams.
 *
 * J5 native demuxer (SH3 → Vita). Standalone portable C99, zero-allocation.
 *
 * Layout (verified on the SH3 sd.afs, J3):
 *   'AIXF' | u32 BE hdr_size | header | chunks at 8+hdr_size:
 *     'AIXP' | u32 BE size | u8 stream | u8 nstreams-1 | u16 BE payload |
 *     u32 BE counter | payload[size-8 bytes...]
 *   counter 0xffffffff = that stream's ADX header chunk (no frames);
 *   otherwise the payload is ADX frame data appended to that stream.
 * Each stream is an ordinary ADX (stereo or mono) once its frames are
 * concatenated; decode with adx.h using the stream's own header.
 */
#ifndef SH3_CODEC_AIX_H
#define SH3_CODEC_AIX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIX_MAX_STREAMS 8

typedef struct aix_stream {
    uint8_t id;                   /* stream id byte from the AIXP header */
    const uint8_t *adx_header;    /* payload of the counter==0xffffffff chunk */
    uint32_t adx_header_len;
    uint32_t payload_size;        /* total frame-data bytes across chunks */
} aix_stream_t;

/* Parse an AIXF image. Fills out[] with up to max_streams streams in
 * first-seen order. Returns the stream count, -1 if not AIXF, -2 if more
 * than max_streams streams are present. */
int aix_parse(const uint8_t *buf, size_t len, aix_stream_t *out, int max_streams);

/* Concatenate the frame-data chunks of `stream` (an element of a previous
 * aix_parse result, matched by id) into dst. Returns bytes written, or -2 if
 * dst_cap is too small. */
long aix_collect(const uint8_t *buf, size_t len, const aix_stream_t *stream,
                 uint8_t *dst, size_t dst_cap);

#ifdef __cplusplus
}
#endif
#endif /* SH3_CODEC_AIX_H */
