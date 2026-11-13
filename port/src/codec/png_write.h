/* png_write.h — minimal 8-bit RGB PNG writer (uncompressed deflate) for the
 * codec self-test harness. Test support only; no zlib dependency. */
#ifndef SH3_CODEC_PNG_WRITE_H
#define SH3_CODEC_PNG_WRITE_H

#include <stddef.h>

/* Write a w x h RGB image (3 bytes per pixel, row `stride` bytes) as a PNG.
 * Returns 0 on success, -1 on I/O or allocation failure. */
int png_write_rgb(const char *path, int w, int h,
                  const unsigned char *rgb, int stride);

#endif /* SH3_CODEC_PNG_WRITE_H */
