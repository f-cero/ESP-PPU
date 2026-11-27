/* SPDX-License-Identifier: Apache-2.0
 * MVID v1 decoder conformance (docs/mvid.md, docs/mvid-handoff.md §4).
 *
 * The two reference clips decode to published FNV-1a-64 fingerprints; any
 * change to the decoder that shifts them is a spec violation. Also checks
 * that mvid_check() rejects truncated/garbage bytes — the only place
 * untrusted input is parsed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ppu16/mvid.h"
#include "test_util.h"

#ifndef PPU16_SRC_DIR
#define PPU16_SRC_DIR "."
#endif

/* Caller-owned decode storage — the decoder itself never allocates. */
static uint8_t  g_sheet[MVID_MAX_TILES * MVID_TILE_MAX];
static uint16_t g_map[MVID_MAX_MAP * MVID_MAX_MAP];
static uint16_t g_fb[320 * 240];

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--) { h ^= (uint64_t)*b++; h *= 1099511628211ULL; }
    return h;
}

/* Load clip, structurally validate, decode every frame, return fingerprint. */
static int decode_clip(const char *rel, uint32_t *frames_out, uint64_t *fp_out)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", PPU16_SRC_DIR, rel);
    size_t bytes = 0;
    uint8_t *data = mvid_read_file(path, &bytes);
    if (!data) { fprintf(stderr, "  cannot read %s\n", path); return -1; }

    int e = mvid_check(data, bytes);
    if (e) { fprintf(stderr, "  %s: %s\n", rel, mvid_err_str(e)); free(data); return -1; }

    mvid_t v;
    memset(&v, 0, sizeof v);
    e = mvid_open(&v, data, bytes, g_sheet, g_map);
    if (e) { fprintf(stderr, "  %s: open: %s\n", rel, mvid_err_str(e)); free(data); return -1; }

    uint32_t n = 0;
    uint64_t fp = 14695981039346656037ULL;
    do {
        mvid_draw(&v, g_fb, v.info.width, v.info.height);
        fp = fnv(fp, g_fb, (size_t)v.info.width * v.info.height * 2u);
        n++;
    } while (mvid_next(&v));
    mvid_close(&v);
    free(data);
    *frames_out = n;
    *fp_out = fp;
    return 0;
}

static void test_title_fingerprint(void)
{
    uint32_t n = 0; uint64_t fp = 0;
    CHECK(decode_clip("assets/clips/title.mvid", &n, &fp) == 0);
    CHECK(n == 1u);
    CHECK(fp == 0x5ff0b32bda700f9dULL);
}

static void test_splash_fingerprint(void)
{
    uint32_t n = 0; uint64_t fp = 0;
    CHECK(decode_clip("assets/clips/splash.mvid", &n, &fp) == 0);
    CHECK(n == 70u);
    CHECK(fp == 0xe9f487cd54b107b9ULL);
}

static void test_check_rejects_garbage(void)
{
    char path[512];
    snprintf(path, sizeof path, "%s/assets/clips/title.mvid", PPU16_SRC_DIR);
    size_t bytes = 0;
    uint8_t *data = mvid_read_file(path, &bytes);
    CHECK(data != NULL);
    if (!data) return;

    /* Truncated header, truncated tail, corrupted CRC region. */
    CHECK(mvid_check(data, 8) != MVID_E_OK);
    CHECK(mvid_check(data, bytes - 1) != MVID_E_OK);
    data[bytes - 8] ^= 0xFFu; data[bytes - 9] ^= 0xFFu;
    CHECK(mvid_check(data, bytes) != MVID_E_OK);
    free(data);
}

void register_tests_mvid(void)
{
    ppu16_test_register("mvid/title fingerprint", test_title_fingerprint);
    ppu16_test_register("mvid/splash fingerprint", test_splash_fingerprint);
    ppu16_test_register("mvid/check rejects garbage", test_check_rejects_garbage);
}
