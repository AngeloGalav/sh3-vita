/* afs.c — CRI AFS archive reader. See afs.h. */
#include "afs.h"

#include <string.h>

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int afs_open(afs_t *afs, const uint8_t *data, size_t size)
{
    uint32_t count, i;
    if (afs == NULL || data == NULL) {
        return -1;
    }
    if (size < 8 || memcmp(data, "AFS", 3) != 0 || data[3] != 0) {
        return -1;
    }
    count = le32(data + 4);
    if (size < (size_t)8 + (size_t)count * 8u) {
        return -1;
    }
    /* Sanity-check the table once so callers can trust the entries. */
    for (i = 0; i < count; i++) {
        uint32_t off = le32(data + 8 + 8 * i);
        uint32_t sz = le32(data + 12 + 8 * i);
        if ((uint64_t)off + (uint64_t)sz > (uint64_t)size) {
            return -1;
        }
    }
    afs->data = data;
    afs->size = size;
    afs->count = count;
    return 0;
}

int afs_entry(const afs_t *afs, uint32_t idx, afs_entry_t *out)
{
    if (afs == NULL || out == NULL || idx >= afs->count) {
        return -1;
    }
    out->offset = le32(afs->data + 8 + 8 * (size_t)idx);
    out->size = le32(afs->data + 12 + 8 * (size_t)idx);
    return 0;
}

const uint8_t *afs_entry_data(const afs_t *afs, uint32_t idx, size_t *len)
{
    afs_entry_t e;
    if (afs_entry(afs, idx, &e) != 0) {
        return NULL;
    }
    if ((uint64_t)e.offset + (uint64_t)e.size > (uint64_t)afs->size) {
        return NULL;
    }
    if (len != NULL) {
        *len = e.size;
    }
    return afs->data + e.offset;
}
