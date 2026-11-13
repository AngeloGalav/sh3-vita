/* adx.h — CRI ADX (type 3, 4-bit ADPCM) decoder with loop points.
 *
 * J5 native decoder (SH3 → Vita). Standalone portable C99: no platform
 * headers beyond stdint/stddef/string, no x86 intrinsics, no host-only
 * libraries. Bit-exact with the J3 reference tool ../tools/j3_audio.py
 * (verified on the SH3 data set; see README.md).
 */
#ifndef SH3_CODEC_ADX_H
#define SH3_CODEC_ADX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ADX_MAX_CHANNELS 8

typedef struct adx_info {
    uint32_t data_off;        /* frame-data start inside the ADX buffer */
    uint8_t  encoding;        /* codec id; only 3 (ADX 4-bit) is decodable */
    uint8_t  block_size;      /* bytes per block per channel; must be 18 */
    uint8_t  bits_per_sample; /* must be 4 */
    uint8_t  channels;
    uint32_t sample_rate;
    uint32_t num_samples;     /* total samples per channel */
    uint16_t cutoff;          /* highpass cutoff used for the coefficients */
    uint8_t  version;         /* 3 or 4 */
    uint8_t  flags;
    int      has_loop;        /* 1 when loop info is present and enabled */
    uint32_t loop_start_sample;
    uint32_t loop_start_offset; /* byte offset of the loop-start frame */
    uint32_t loop_end_sample;
    uint32_t loop_end_offset;
} adx_info_t;

/* Parse an ADX header. Returns 0 on success, -1 if the buffer is not an ADX
 * (bad magic or too short). Variant validation happens in adx_decode_frames,
 * mirroring the reference tool. Loop info: version 3 at +0x18, version 4 at
 * +0x24, five u32 BE (flag, start sample, start offset, end sample, end
 * offset); present only when the copyright offset covers the block and the
 * flag is non-zero. */
int adx_parse(const uint8_t *buf, size_t len, adx_info_t *info);

/* Fixed-point predictor coefficients. Reproduces the reference tool's
 * double-precision computation bit for bit:
 *   a  = sqrt(2) - cos(2*pi*cutoff/rate)
 *   bb = sqrt(2) - 1
 *   c  = (a - sqrt((a+bb)*(a-bb))) / bb
 *   c1 = floor(c*8192), c2 = floor(c*c*-4096)
 * Run both sides on the same libm for identical results. */
void adx_coefficients(uint16_t cutoff, uint32_t rate, int32_t *c1, int32_t *c2);

/* Number of samples per channel that adx_decode_frames would produce for a
 * frame region of `len` bytes: min(num_samples, len/(18*channels)*32). */
size_t adx_decoded_samples(size_t len, const adx_info_t *info);

/* Decode interleaved ADX frames (18 bytes per channel per 32-sample block,
 * channels interleaved block-wise). Writes interleaved int16 PCM, truncated
 * to info->num_samples. Returns samples per channel written, -1 on
 * unsupported variant/bad params, -2 if pcm cap is too small. */
long adx_decode_frames(const uint8_t *frames, size_t len,
                       const adx_info_t *info, int16_t *pcm, size_t cap);

/* Decode a complete ADX file buffer (header + frames at info.data_off).
 * Returns samples per channel written, or a negative error. */
long adx_decode(const uint8_t *buf, size_t len, int16_t *pcm, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SH3_CODEC_ADX_H */
