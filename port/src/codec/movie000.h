/* movie000.h — SH3 `.000` movie demangler (MSVC-CRT LCG keystream).
 *
 * J5 native decoder (SH3 → Vita). Standalone portable C99. Bit-exact with
 * the J3 reference tool ../tools/j3_movie.py (verified on all 7 SH3 movies;
 * see README.md).
 *
 * Scheme (SH3 exe FUN_00405680, documented in docs/DATA-FORMATS-SH3.md):
 *   * The file starts with a 0x8000-byte block whose plaintext is all zeros;
 *     its ciphertext carries the seed at four fixed offsets:
 *       seed = f[0x2dde] | f[0x3d40]<<8 | f[0x2a5c]<<16 | f[0x55e2]<<24
 *   * LCG (MSVC rand()): state = state * 0x5d588b65 + 1  (mod 2^32).
 *   * base = LCG^8192(seed) — the state after running over the padding.
 *   * Data region (from 0x8000): the keystream is periodic with period 0x1000
 *     bytes: at every 4096-byte chunk the state resets to `base`, then per
 *     32-bit little-endian word: state = step(state); plain = cipher ^ state.
 *   * Trailing bytes not forming a full word are left as-is (reference
 *     behaviour).
 */
#ifndef SH3_CODEC_MOVIE000_H
#define SH3_CODEC_MOVIE000_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOVIE000_HEADER_SIZE 0x8000
#define MOVIE000_PERIOD 0x1000

/* Gather the seed from a 0x8000-byte header block. */
uint32_t movie000_seed(const uint8_t *hdr);

/* base = the LCG state after advancing 8192 words from the seed. */
uint32_t movie000_base_state(const uint8_t *hdr);

/* One LCG step. */
uint32_t movie000_lcg_step(uint32_t state);

/* Decrypt (in place, it is an involution) a data region of `len` bytes that
 * starts at a 0x1000-aligned chunk boundary in the file, using `base`. */
void movie000_decrypt(uint8_t *buf, size_t len, uint32_t base);

#ifdef __cplusplus
}
#endif
#endif /* SH3_CODEC_MOVIE000_H */
