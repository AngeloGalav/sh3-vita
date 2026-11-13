/* afs.h — CRI AFS archive reader (SH3 sound/sd.afs, sound/demo_afs/<name>.afs).
 *
 * J5 native reader (SH3 → Vita). Standalone portable C99, zero-allocation:
 * the caller owns the archive image in memory. All table reads are byte-wise
 * so the image may sit at any alignment.
 *
 * Layout (verified on the SH3 data, J3):
 *   'AFS\0' | u32 LE entry_count | entry_count x (u32 LE offset, u32 LE size)
 *   entries are 0x800-aligned in the shipped archives.
 */
#ifndef SH3_CODEC_AFS_H
#define SH3_CODEC_AFS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct afs_entry {
    uint32_t offset;
    uint32_t size;
} afs_entry_t;

typedef struct afs {
    const uint8_t *data;
    size_t size;
    uint32_t count;
} afs_t;

/* Open an AFS image already in memory. Returns 0, or -1 if the magic is bad
 * or the table is truncated or an entry lies outside the image. */
int afs_open(afs_t *afs, const uint8_t *data, size_t size);

/* Copy entry `idx` into *out. Returns 0, or -1 if idx is out of range. */
int afs_entry(const afs_t *afs, uint32_t idx, afs_entry_t *out);

/* Entry payload pointer and length (NULL if idx is out of range). */
const uint8_t *afs_entry_data(const afs_t *afs, uint32_t idx, size_t *len);

#ifdef __cplusplus
}
#endif
#endif /* SH3_CODEC_AFS_H */
