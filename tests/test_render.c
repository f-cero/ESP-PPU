/* End-to-end scanline compositing: priorities, colour math, windows,     */
/* mosaic, QFA geometry (full-res vs quarter-res agreement).              */
#include "test_util.h"
#include "ppu16/ppu16.h"
#include <string.h>
#include <stdlib.h>

static void test_backdrop_and_fade(void)
{
    ppu16_t p;
    ppu16_config_t cfg = { 8, 8, PPU16_MODE_TEXT, 0 };
    CHECK(ppu16_init(&p, &cfg, NULL, 0) == PPU16_OK);
    p.fx.backdrop = 0x1234;
    uint16_t line[8];
    ppu16_render_line(&p, 0, line);
    for (int i = 0; i < 8; i++) CHECK(line[i] == 0x1234);
}

static void test_priority_order(void)
{
    /* Two BGs writing the same pixel: lower priority number wins.        */
    ppu16_t p;
    ppu16_config_t cfg = { 8, 8, PPU16_MODE_TEXT, 0 };
    static uint8_t arena[16];
    CHECK(ppu16_init(&p, &cfg, arena, sizeof arena) == PPU16_OK);

    static uint8_t tiles[32 * 2];
    static uint16_t map0[32 * 32], map1[32 * 32];
    tiles[0] = 0x01;
    map0[0] = 0x0400;               /* BG0: palette slot 1 (bit 10)       */
    map1[0] = 0x0800;               /* BG1: palette slot 2 (bits 10-12)   */
    p.bg_tiles = tiles;
    p.pal_bg[17] = 0x0F0F;      /* slot 1, texel 1                    */
    p.pal_bg[33] = 0xF0F0;      /* slot 2, texel 1                    */

    ppu16_bg_t b0 = { .priority = 0 };   /* front                         */
    ppu16_bg_t b1 = { .priority = 3 };   /* back                          */
    b0.tiles = tiles; b0.map = map0; b0.bpp = 4;
    b0.map_shift_x = 5; b0.map_shift_y = 5;
    b1 = b0; b1.map = map1; b1.priority = 3;
    CHECK(ppu16_bg_claim(&p, 0, &b0) == PPU16_OK);
    CHECK(ppu16_bg_claim(&p, 1, &b1) == PPU16_OK);

    uint16_t line[8];
    ppu16_render_line(&p, 0, line);
    CHECK(line[0] == 0x0F0F);           /* BG0 (front) wins              */
}

static void test_cmath(void)
{
    ppu16_t p;
    ppu16_config_t cfg = { 8, 8, PPU16_MODE_TEXT, 0 };
    static uint8_t arena[16];
    CHECK(ppu16_init(&p, &cfg, arena, sizeof arena) == PPU16_OK);
    p.fx.cmath_mode = PPU16_CM_ADD;
    p.fx.cmath_targets = PPU16_CT_BG(0);
    p.fx.backdrop = ppu16_rgb565(10, 10, 10);

    static uint8_t tiles[32];
    static uint16_t map[32 * 32];
    tiles[0] = 0x01;
    map[0] = 0x0400;
    p.bg_tiles = tiles;
    p.pal_bg[17] = ppu16_rgb565(5, 5, 5);   /* slot 1, texel 1        */

    ppu16_bg_t b0 = { .priority = 0 };
    b0.tiles = tiles; b0.map = map; b0.bpp = 4;
    b0.map_shift_x = 5; b0.map_shift_y = 5;
    CHECK(ppu16_bg_claim(&p, 0, &b0) == PPU16_OK);

    uint16_t line[8];
    ppu16_render_line(&p, 0, line);
    CHECK(line[0] == ppu16_rgb565(15, 15, 15));   /* per-channel 10+5      */
}

static void test_mosaic(void)
{
    ppu16_t p;
    ppu16_config_t cfg = { 16, 16, PPU16_MODE_TEXT, 0 };
    static uint8_t arena[16];
    CHECK(ppu16_init(&p, &cfg, arena, sizeof arena) == PPU16_OK);
    p.fx.mosaic_w = 4; p.fx.mosaic_h = 4;

    static uint8_t tiles[64];
    static uint16_t map[32 * 32];
    /* 4bpp: byte i low nibble = pixel 2i. Pixels 0,2,4,6 -> texels 1-4.  */
    for (int i = 0; i < 4; i++) tiles[i] = (uint8_t)(i + 1);
    map[0] = 0x0400;
    p.bg_tiles = tiles;
    p.pal_bg[17] = 0x0FFF; p.pal_bg[18] = 0x00FF; p.pal_bg[19] = 0x000F;

    ppu16_bg_t b0 = { .priority = 0 };
    b0.tiles = tiles; b0.map = map; b0.bpp = 4;
    b0.map_shift_x = 5; b0.map_shift_y = 5;
    CHECK(ppu16_bg_claim(&p, 0, &b0) == PPU16_OK);

    uint16_t line[16];
    ppu16_render_line(&p, 0, line);
    /* Within a 4-px block, every pixel shows the block's first colour.   */
    CHECK(line[0] == line[3]);
    CHECK(line[4] == line[7]);
    CHECK(line[0] != line[4]);
}

static void test_qfa_geometry(void)
{
    /* Handoff §12.1: the QFA render downscaled 2x must match full-res    */
    /* EXACTLY (same math, quarter pixel count).                          */
    enum { W = 128, H = 64 };
    static uint8_t map0[65536];
    static uint16_t pal[256];
    static ppu16_mat6_t rowmat[H];
    for (int i = 0; i < 65536; i++) map0[i] = (uint8_t)((i * 7 + (i >> 8)) & 0xFF);
    for (int i = 0; i < 256; i++) pal[i] = ppu16_rgb565((uint8_t)i, 0, (uint8_t)(255 - i));

    for (int y = 0; y < H; y++) {
        rowmat[y].a = (3 << 16) / 2;                    /* 1.5 step        */
        rowmat[y].c = (1 << 16) / 4;
        rowmat[y].e = (int32_t)(y << 14);               /* d*y folded      */
        rowmat[y].f = (int32_t)(y << 15);
    }

    ppu16_config_t cfg = { W, H, PPU16_MODE_AFFINE2,
                           PPU16_CF_QFA_SCRATCH };
    static uint8_t arena[PPU16_ARENA_MAX / 8];
    ppu16_t pf, pq;
    CHECK(ppu16_init(&pf, &cfg, arena, sizeof arena) == PPU16_OK);
    CHECK(ppu16_init(&pq, &cfg, arena, sizeof arena) == PPU16_OK);

    ppu16_affine_t Lf = {
        .mip = { map0 }, .palette = pal,
        .rowmat = rowmat, .rowmat_count = H,
        .dest_w = W, .dest_h = H,
        .mode = PPU16_AFF_FULL, .flags = PPU16_AF_WRAP, .priority = 2
    };
    ppu16_affine_t Lq = Lf;
    Lq.mode = PPU16_AFF_QFA;
    CHECK(ppu16_affine_claim(&pf, 0, &Lf) == PPU16_OK);
    CHECK(ppu16_affine_claim(&pq, 0, &Lq) == PPU16_OK);

    static uint16_t fb_full[W * H], fb_qfa[W * H];
    ppu16_render_frame(&pf, fb_full);
    ppu16_render_frame(&pq, fb_qfa);

    /* QFA output at (2x, 2y) must equal full-res output at (2x, 2y):     */
    /* both sample the same map position for even coordinates.            */
    int mismatches = 0;
    for (int y = 0; y < H / 2; y++)
        for (int x = 0; x < W / 2; x++)
            if (fb_full[(2 * y) * W + (2 * x)] != fb_qfa[(2 * y) * W + (2 * x)])
                mismatches++;
    CHECK(mismatches == 0);
    CHECK(pq.frame_count == 1);
}

static void test_window(void)
{
    ppu16_t p;
    ppu16_config_t cfg = { 16, 16, PPU16_MODE_TEXT, 0 };
    static uint8_t arena[16];
    CHECK(ppu16_init(&p, &cfg, arena, sizeof arena) == PPU16_OK);
    p.fx.backdrop = 0x0000;

    static uint8_t tiles[32];
    static uint16_t map[32 * 32];
    memset(tiles, 0x11, 4);     /* solid tile: all 8 texels = 1        */
    map[0] = 0x0400;
    p.bg_tiles = tiles;
    p.pal_bg[17] = 0xFFFF;      /* slot 1, texel 1                    */
    p.win[0].enable = 1;
    p.win[0].x0 = 4; p.win[0].x1 = 16; p.win[0].y0 = 0; p.win[0].y1 = 16;

    ppu16_bg_t b0 = { .priority = 0, .win_bits = 1 };
    b0.tiles = tiles; b0.map = map; b0.bpp = 4;
    b0.map_shift_x = 5; b0.map_shift_y = 5;
    CHECK(ppu16_bg_claim(&p, 0, &b0) == PPU16_OK);

    uint16_t line[16];
    ppu16_render_line(&p, 0, line);
    CHECK(line[0] == 0x0000);           /* outside window: backdrop      */
    CHECK(line[4] == 0xFFFF);           /* inside window: BG (tile 0)    */
}

void register_tests_render(void)
{
    ppu16_test_register("render/backdrop", test_backdrop_and_fade);
    ppu16_test_register("render/priority", test_priority_order);
    ppu16_test_register("render/cmath", test_cmath);
    ppu16_test_register("render/mosaic", test_mosaic);
    ppu16_test_register("render/qfa_geometry", test_qfa_geometry);
    ppu16_test_register("render/window", test_window);
}
