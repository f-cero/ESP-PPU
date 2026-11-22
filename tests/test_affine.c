/* Affine layers: the handoff §12.1 correctness matrix on the host.       */
#include "test_util.h"
#include "ppu16/ppu16.h"
#include <string.h>
#include <stdlib.h>

#define MAPSZ 65536
static uint8_t map0[MAPSZ];               /* LOD 0: 256x256               */
static uint16_t pal[256];
static ppu16_mat6_t rowmat[256];

static void build_world(void)
{
    for (int i = 0; i < MAPSZ; i++) map0[i] = (uint8_t)(i & 0xFF);
    for (int i = 0; i < 256; i++) pal[i] = ppu16_rgb565((uint8_t)i, (uint8_t)(i >> 1), (uint8_t)(255 - i));
}

static ppu16_affine_t make_layer(ppu16_aff_mode_t mode)
{
    ppu16_affine_t L;
    memset(&L, 0, sizeof L);
    L.mip[0] = map0;
    L.palette = pal;
    L.rowmat = rowmat; L.rowmat_count = 64;
    L.dest_x = 0; L.dest_y = 0; L.dest_w = 64; L.dest_h = 64;
    L.mode = mode;
    L.flags = PPU16_AF_WRAP;
    L.priority = 2;
    return L;
}

static void test_identity(void)
{
    build_world();
    ppu16_t p;
    ppu16_config_t cfg = { 64, 64, PPU16_MODE_AFFINE2, 0 };
    CHECK(ppu16_init(&p, &cfg, NULL, 0) == PPU16_OK);

    memset(rowmat, 0, sizeof rowmat);
    for (int y = 0; y < 64; y++) {
        rowmat[y].a = 1 << 16; rowmat[y].d = 1 << 16;
        rowmat[y].e = 0; rowmat[y].f = (int32_t)y << 16;
    }
    ppu16_affine_t L = make_layer(PPU16_AFF_FULL);
    /* rowmat[y].f should be y — fold handles it, but set explicitly:    */
    ppu16_mat6_t base = { .a = 1 << 16, .d = 1 << 16 };
    ppu16_affine_fold_rowmat(rowmat, 64, &base);
    CHECK(ppu16_affine_claim(&p, 0, &L) == PPU16_OK);

    uint16_t fb[64 * 64];
    ppu16_render_frame(&p, fb);
    /* Identity: output(x, y) must equal palette[map[y*256 + x]].         */
    for (int y = 0; y < 64; y += 7)
        for (int x = 0; x < 64; x += 7) {
            unsigned ti = map0[(unsigned)y << 8 | (unsigned)x];
            CHECK(fb[y * 64 + x] == pal[ti]);
        }
}

static void test_wrap(void)
{
    build_world();
    /* Step 1.0 across the full map width: exactly one repeat, no seam.  */
    for (int y = 0; y < 64; y++) {
        rowmat[y].a = 1 << 16;
        rowmat[y].e = 0; rowmat[y].f = (int32_t)y << 16;
    }
    ppu16_t p;
    ppu16_config_t cfg = { 512, 64, PPU16_MODE_AFFINE2, 0 };
    CHECK(ppu16_init(&p, &cfg, NULL, 0) == PPU16_OK);
    ppu16_affine_t L = make_layer(PPU16_AFF_FULL);
    L.dest_w = 512; L.rowmat_count = 64;
    CHECK(ppu16_affine_claim(&p, 0, &L) == PPU16_OK);
    uint16_t line[512];
    ppu16_render_line(&p, 0, line);
    /* Pixel 0 and pixel 256 must be the same map texel (one repeat).     */
    CHECK(line[0] == pal[0]);
    CHECK(line[256] == pal[0]);
    CHECK(line[511] == pal[255]);
    /* Wrap is also correct for negative coordinates.                     */
    memset(rowmat, 0, sizeof rowmat);
    rowmat[0].a = 1 << 16; rowmat[0].e = -(2 << 16);
    ppu16_render_line(&p, 0, line);
    CHECK(line[0] == pal[254]);
}

static void test_lod_selection(void)
{
    /* L1 approximation: monotone, saturates at the mip cap.              */
    CHECK(ppu16_lod_for_step(1 << 16, 0) == 0);
    CHECK(ppu16_lod_for_step(2 << 16, 0) == 1);
    CHECK(ppu16_lod_for_step(4 << 16, 0) == 2);
    CHECK(ppu16_lod_for_step(16 << 16, 0) == PPU16_MAX_MIP - 1);
    CHECK(ppu16_lod_for_step(0, 0) == 0);
}

static void test_perspective_fill(void)
{
    ppu16_mat6_t tbl[240];
    ppu16_affine_perspective_fill(tbl, 240, 80, 16, 1 << 16,
                                  1 << 16, 0);
    /* Above the horizon: zero matrix (sky rows).                         */
    CHECK(tbl[40].a == 0 && tbl[40].f == 0);
    /* Below: step magnitude falls with distance — 1/(y - yh).            */
    CHECK(tbl[240 - 1].a != 0);
    int32_t near_step = tbl[81].a, far_step = tbl[239].a;
    CHECK(near_step > far_step);
    /* Near-field clamp: never past 8.0.                                  */
    CHECK(near_step <= 8 * PPU16_FP16_ONE);
}

static void test_dup_hv(void)
{
    static uint16_t src[3 * 3], dst[6 * 6];
    for (int i = 0; i < 9; i++) src[i] = (uint16_t)(0x1000 + i);
    ppu16_affine_dup_hv(dst, 6, src, 3, 3, 3);
    /* Each source pixel becomes a 2x2 block; the vertical is a row copy. */
    for (int y = 0; y < 6; y += 2)
        for (int x = 0; x < 6; x += 2) {
            CHECK(dst[y * 6 + x] == dst[y * 6 + x + 1]);
            CHECK(dst[y * 6 + x] == dst[(y + 1) * 6 + x]);
        }
}

static void test_fold_rowmat(void)
{
    ppu16_mat6_t out[8];
    ppu16_mat6_t base = { .a = 1 << 16, .b = 1 << 15, .c = 0, .d = 1 << 14,
                          .e = 5 << 16, .f = 7 << 16 };
    ppu16_affine_fold_rowmat(out, 8, &base);
    /* out[3].e must be b*3 + e = 0.5*3 + 5 = 6.5 in 16.16.               */
    CHECK(out[3].e == (int32_t)(6.5 * 65536));
    CHECK(out[3].f == (int32_t)(7.75 * 65536));
    CHECK(out[3].a == base.a && out[3].c == base.c);
}

void register_tests_affine(void)
{
    ppu16_test_register("affine/identity", test_identity);
    ppu16_test_register("affine/wrap", test_wrap);
    ppu16_test_register("affine/lod_selection", test_lod_selection);
    ppu16_test_register("affine/perspective_fill", test_perspective_fill);
    ppu16_test_register("affine/dup_hv", test_dup_hv);
    ppu16_test_register("affine/fold_rowmat", test_fold_rowmat);
}
