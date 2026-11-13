/* sha256.h — minimal SHA-256 (FIPS 180-4) for the codec self-test harness.
 * Test-support only; not part of the shipped decoder API. */
#ifndef SH3_CODEC_SHA256_H
#define SH3_CODEC_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct sha256 {
    uint32_t h[8];
    uint64_t bytes;
    uint8_t buf[64];
    size_t buflen;
} sha256_t;

void sha256_init(sha256_t *s);
void sha256_update(sha256_t *s, const void *data, size_t len);
void sha256_final(sha256_t *s, uint8_t out[32]);
/* Hex string of the digest into out (65 bytes). */
void sha256_hex(const uint8_t digest[32], char out[65]);

#endif /* SH3_CODEC_SHA256_H */
