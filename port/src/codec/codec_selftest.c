/* codec_selftest.c — host test harness for the J5 native decoders.
 *
 * Subcommands:
 *   probe <file.afs>
 *       One line per AFS entry: kind, and for ADX the header + loop fields,
 *       for AIX the stream table. Format mirrors the Python reference script.
 *   hash <file.afs> <out.txt>
 *       Decode every ADX entry and every AIX substream to interleaved int16
 *       PCM and record "name sha256 nsamples" per stream.
 *   decrypt <in.000> <out.mpg>
 *       Demangle a .000 movie (movie000.h), write the MPEG-2 PS, print the
 *       SHA-256 of the decrypted bytes plus a pack-sync survey.
 *   movie <in.mpg>
 *       Decode every video frame with pl_mpeg; print frame count, fps,
 *       duration and dimensions.
 *   moviepng <in.mpg> <time> <out.png>
 *       Decode up to <time> seconds and write that frame as a PNG.
 *
 * Test-support only: uses stdio/malloc; the shipped codec files do not.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "afs.h"
#include "adx.h"
#include "aix.h"
#include "movie000.h"
#include "sha256.h"
#include "png_write.h"
#include "pl_mpeg.h"

static unsigned char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long sz;
    size_t got;
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = (unsigned char *)malloc((size_t)sz ? (size_t)sz : 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) {
        fprintf(stderr, "short read %s\n", path);
        free(buf);
        return NULL;
    }
    *len = (size_t)sz;
    return buf;
}

static int write_file(const char *path, const unsigned char *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return -1;
    }
    if (len && fwrite(data, 1, len, f) != len) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static const char *entry_kind(const unsigned char *b, size_t len)
{
    if (len >= 4 && memcmp(b, "RIFF", 4) == 0) {
        return "wav";
    }
    if (len >= 4 && memcmp(b, "AIXF", 4) == 0) {
        return "aix";
    }
    if (len >= 2 && b[0] == 0x80 && b[1] == 0x00) {
        return "adx";
    }
    if (len >= 4 && memcmp(b, "\x78\x56\x34\x12", 4) == 0) {
        return "x78";
    }
    return "bin";
}

static void sha256_hexdigest(const unsigned char *data, size_t len, char out[65])
{
    sha256_t s;
    uint8_t dig[32];
    sha256_init(&s);
    sha256_update(&s, data, len);
    sha256_final(&s, dig);
    sha256_hex(dig, out);
}

/* Decode one ADX frame region and append its hash line. */
static int hash_adx(FILE *out, const char *name, const unsigned char *hdr,
                    size_t hdr_len, const unsigned char *frames, size_t frames_len)
{
    adx_info_t info;
    long n;
    size_t need;
    int16_t *pcm;
    char hex[65];
    if (adx_parse(hdr, hdr_len, &info) != 0) {
        return -1;
    }
    need = adx_decoded_samples(frames_len, &info);
    if (need > (size_t)0x7FFFFFFF / (size_t)(info.channels ? info.channels : 1)) {
        return -1;
    }
    pcm = (int16_t *)malloc((need * (size_t)info.channels + 8) * sizeof(int16_t));
    if (!pcm) {
        return -1;
    }
    n = adx_decode_frames(frames, frames_len, &info, pcm, need);
    if (n < 0) {
        free(pcm);
        fprintf(stderr, "decode failed for %s (%ld)\n", name, n);
        return -1;
    }
    sha256_hexdigest((const unsigned char *)pcm, (size_t)n * info.channels * sizeof(int16_t), hex);
    fprintf(out, "%s %s %ld\n", name, hex, n);
    free(pcm);
    return 0;
}

static int cmd_probe(const char *path)
{
    size_t len;
    unsigned char *data = read_file(path, &len);
    afs_t afs;
    uint32_t i;
    if (!data) {
        return 1;
    }
    if (afs_open(&afs, data, len) != 0) {
        fprintf(stderr, "not AFS: %s\n", path);
        free(data);
        return 1;
    }
    for (i = 0; i < afs.count; i++) {
        size_t elen;
        const unsigned char *e = afs_entry_data(&afs, i, &elen);
        const char *kind = entry_kind(e, elen);
        if (strcmp(kind, "adx") == 0) {
            adx_info_t h;
            if (adx_parse(e, elen, &h) == 0) {
                printf("%u adx %u %u %u %d loop=%d %u %u %u %u\n", i,
                       h.channels, h.sample_rate, h.num_samples, h.version,
                       h.has_loop, h.loop_start_sample, h.loop_start_offset,
                       h.loop_end_sample, h.loop_end_offset);
            } else {
                printf("%u adx bad\n", i);
            }
        } else if (strcmp(kind, "aix") == 0) {
            aix_stream_t st[AIX_MAX_STREAMS];
            int n = aix_parse(e, elen, st, AIX_MAX_STREAMS);
            int s;
            printf("%u aix %d", i, n);
            for (s = 0; s < n; s++) {
                printf(" %u:%u", st[s].id, st[s].payload_size);
            }
            printf("\n");
        } else {
            printf("%u %s\n", i, kind);
        }
    }
    free(data);
    return 0;
}

static int cmd_hash(const char *path, const char *outpath)
{
    size_t len;
    unsigned char *data = read_file(path, &len);
    afs_t afs;
    uint32_t i;
    FILE *out;
    char base[128];
    const char *slash;
    char *dot;
    if (!data) {
        return 1;
    }
    if (afs_open(&afs, data, len) != 0) {
        free(data);
        return 1;
    }
    slash = strrchr(path, '/');
    strncpy(base, slash ? slash + 1 : path, sizeof(base) - 1);
    base[sizeof(base) - 1] = 0;
    dot = strrchr(base, '.');
    if (dot) {
        *dot = 0;
    }
    out = strcmp(outpath, "-") == 0 ? stdout : fopen(outpath, "wb");
    if (!out) {
        fprintf(stderr, "cannot write %s\n", outpath);
        free(data);
        return 1;
    }
    for (i = 0; i < afs.count; i++) {
        size_t elen;
        const unsigned char *e = afs_entry_data(&afs, i, &elen);
        const char *kind = entry_kind(e, elen);
        char name[192];
        if (strcmp(kind, "adx") == 0) {
            adx_info_t h;
            if (adx_parse(e, elen, &h) != 0) {
                continue;
            }
            snprintf(name, sizeof(name), "%s_%04u_adx", base, i);
            if (hash_adx(out, name, e, elen, e + h.data_off, elen - h.data_off) != 0) {
                fprintf(stderr, "FAILED %s\n", name);
                if (out != stdout) { fclose(out); }
                free(data);
                return 1;
            }
        } else if (strcmp(kind, "aix") == 0) {
            aix_stream_t st[AIX_MAX_STREAMS];
            int n = aix_parse(e, elen, st, AIX_MAX_STREAMS);
            int s;
            if (n < 0) {
                fprintf(stderr, "aix parse failed entry %u\n", i);
                if (out != stdout) { fclose(out); }
                free(data);
                return 1;
            }
            for (s = 0; s < n; s++) {
                unsigned char *frames = (unsigned char *)malloc(st[s].payload_size + 4);
                long got;
                if (!frames) {
                    if (out != stdout) { fclose(out); }
                    free(data);
                    return 1;
                }
                got = aix_collect(e, elen, &st[s], frames, st[s].payload_size + 4);
                if (got < 0) {
                    fprintf(stderr, "aix collect failed entry %u stream %u\n", i, st[s].id);
                    free(frames);
                    if (out != stdout) { fclose(out); }
                    free(data);
                    return 1;
                }
                snprintf(name, sizeof(name), "%s_%04u_s%u", base, i, st[s].id);
                if (hash_adx(out, name, st[s].adx_header, st[s].adx_header_len,
                             frames, (size_t)got) != 0) {
                    fprintf(stderr, "FAILED %s\n", name);
                    free(frames);
                    if (out != stdout) { fclose(out); }
                    free(data);
                    return 1;
                }
                free(frames);
            }
        }
    }
    if (out != stdout) { fclose(out); }
    free(data);
    return 0;
}

static int cmd_decrypt(const char *path, const char *outpath)
{
    size_t len;
    unsigned char *data = read_file(path, &len);
    uint32_t base;
    size_t dlen;
    char hex[65];
    size_t off, sectors = 0, bad = 0, b9 = 0;
    if (!data) {
        return 1;
    }
    if (len < MOVIE000_HEADER_SIZE) {
        fprintf(stderr, "%s smaller than padding block\n", path);
        free(data);
        return 1;
    }
    base = movie000_base_state(data);
    dlen = len - MOVIE000_HEADER_SIZE;
    movie000_decrypt(data + MOVIE000_HEADER_SIZE, dlen, base);
    sha256_hexdigest(data + MOVIE000_HEADER_SIZE, dlen, hex);
    /* Full 2048-byte sectors only, matching j3_movie.py --check semantics. */
    for (off = 0; off + 2048 <= dlen; off += 2048) {
        const unsigned char *p = data + MOVIE000_HEADER_SIZE + off;
        sectors++;
        if (p[0] == 0 && p[1] == 0 && p[2] == 1 && p[3] == 0xBA) {
            /* pack start */
        } else if (p[0] == 0 && p[1] == 0 && p[2] == 1 && p[3] == 0xB9) {
            b9++;
        } else {
            bad++;
        }
    }
    if (write_file(outpath, data + MOVIE000_HEADER_SIZE, dlen) != 0) {
        free(data);
        return 1;
    }
    {
        const char *slash = strrchr(path, '/');
        const char *nm = slash ? slash + 1 : path;
        int tail_b9 = (dlen >= 4 &&
                       memcmp(data + len - 4, "\x00\x00\x01\xb9", 4) == 0);
        printf("%s size=%zu sha256=%s sectors=%zu bad_sync=%zu pack_end_sync=%zu tail_packend=%d\n",
               nm, dlen, hex, sectors, bad, b9, tail_b9);
    }
    free(data);
    return 0;
}

static int cmd_movie(const char *path)
{
    plm_t *plm = plm_create_with_filename(path);
    long frames = 0;
    plm_frame_t *f;
    if (!plm) {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }
    while ((f = plm_decode_video(plm)) != NULL) {
        frames++;
    }
    printf("%s frames=%ld fps=%.6f duration=%.3f %dx%d\n",
           path, frames, plm_get_framerate(plm), plm_get_duration(plm),
           plm_get_width(plm), plm_get_height(plm));
    plm_destroy(plm);
    return frames > 0 ? 0 : 1;
}

static int cmd_moviepng(const char *path, double t, const char *outpath)
{
    plm_t *plm = plm_create_with_filename(path);
    plm_frame_t *f;
    unsigned char *rgb;
    int w, h, rc;
    if (!plm) {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }
    f = plm_seek_frame(plm, t, 1);
    if (!f) {
        fprintf(stderr, "no frame at t=%.3f in %s\n", t, path);
        plm_destroy(plm);
        return 1;
    }
    w = plm_get_width(plm);
    h = plm_get_height(plm);
    rgb = (unsigned char *)malloc((size_t)w * (size_t)h * 3);
    if (!rgb) {
        plm_destroy(plm);
        return 1;
    }
    plm_frame_to_rgb(f, rgb, w * 3);
    rc = png_write_rgb(outpath, w, h, rgb, w * 3);
    printf("%s t=%.3f frame_time=%.3f %dx%d -> %s\n", path, t, f->time, w, h, outpath);
    free(rgb);
    plm_destroy(plm);
    return rc;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[1], "probe") == 0) {
        return cmd_probe(argv[2]);
    }
    if (argc >= 4 && strcmp(argv[1], "hash") == 0) {
        return cmd_hash(argv[2], argv[3]);
    }
    if (argc >= 4 && strcmp(argv[1], "decrypt") == 0) {
        return cmd_decrypt(argv[2], argv[3]);
    }
    if (argc >= 3 && strcmp(argv[1], "movie") == 0) {
        return cmd_movie(argv[2]);
    }
    if (argc >= 5 && strcmp(argv[1], "moviepng") == 0) {
        return cmd_moviepng(argv[2], atof(argv[3]), argv[4]);
    }
    fprintf(stderr,
            "usage: %s probe <afs> | hash <afs> <out.txt> | decrypt <in.000> <out.mpg>"
            " | movie <mpg> | moviepng <mpg> <time> <out.png>\n", argv[0]);
    return 2;
}
