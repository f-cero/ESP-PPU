/* SPDX-License-Identifier: Apache-2.0
 * demo_mvid.c — MVID v1 reference decode, standalone (no engine, no SDL).
 *
 * Build:  cc -O2 -std=c99 -I../codec demo_mvid.c ../codec/mvid_util.c \
 *            ../codec/mvid_decode.c -o demo_mvid
 * Run:    ./demo_mvid ../clips/splash.mvid [out.ppm]
 *
 * Prints a fingerprint over every decoded frame (FNV-1a 64 across the RGB565
 * buffers) so a port can prove byte parity with the reference in one line.
 */
#include <ppu16/mvid.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t  g_sheet[MVID_MAX_TILES * MVID_TILE_MAX];   /* 64 KiB worst case */
static uint16_t g_map[MVID_MAX_MAP * MVID_MAX_MAP];        /* 32 KiB worst case */
static uint16_t g_fb[320 * 240];

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--) { h ^= (uint64_t)*b++; h *= 1099511628211ULL; }
    return h;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s clip.mvid [out.ppm]\n", argv[0]); return 2; }
    size_t bytes = 0;
    uint8_t *data = mvid_read_file(argv[1], &bytes);
    if (!data) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }

    int e = mvid_check(data, bytes);
    if (e) { fprintf(stderr, "%s: %s\n", argv[1], mvid_err_str(e)); free(data); return 1; }

    mvid_t v;
    memset(&v, 0, sizeof v);
    e = mvid_open(&v, data, bytes, g_sheet, g_map);
    if (e) { fprintf(stderr, "%s: open: %s\n", argv[1], mvid_err_str(e)); free(data); return 1; }

    const mvid_info_t *i = &v.info;
    unsigned n = 0;
    uint64_t fp = 14695981039346656037ULL;

    do {
        mvid_draw(&v, g_fb, i->width, i->height);
        fp = fnv(fp, g_fb, (size_t)i->width * i->height * 2u);
        n++;
        if (argc > 2 && n == 1) {
            FILE *f = fopen(argv[2], "wb");
            if (f) {
                fprintf(f, "P6\n%u %u\n255\n", i->width, i->height);
                for (unsigned p = 0; p < (unsigned)i->width * i->height; p++) {
                    uint16_t c = g_fb[p];
                    unsigned r = (c >> 11) & 31u, g = (c >> 5) & 63u, b = c & 31u;
                    fputc((int)((r * 255u + 15u) / 31u), f);
                    fputc((int)((g * 255u + 31u) / 63u), f);
                    fputc((int)((b * 255u + 15u) / 31u), f);
                }
                fclose(f);
            }
        }
    } while (mvid_next(&v));

    printf("%u frames, %ux%u %ubpp %ufps, map %ux%u, %u pal, sheet %u B, %zu B/frame, fnv=%016llx\n",
           n, i->width, i->height, i->bpp, i->fps, i->map_w, i->map_h,
           i->pal_count, i->sheet_bytes, bytes / (n ? n : 1),
           (unsigned long long)fp);
    mvid_close(&v);
    free(data);
    return 0;
}
