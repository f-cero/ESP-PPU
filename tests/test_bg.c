/* Tile BG: map fetch, flips, wrap, scroll and span rendering.           */
#include "test_util.h"
#include "ppu16/ppu16.h"
#include <string.h>

/* 1x1-tileBG world: one 8x8 4bpp tile, palette slot 1.                  */
static uint8_t tiles[32];
static uint16_t map[32 * 32];

static void setup_bg(ppu16_t *p, ppu16_bg_t *bg, uint16_t entry)
{
    ppu16_config_t cfg = { 32, 8, PPU16_MODE_TEXT, 0 };
    static uint8_t arena[16];
    CHECK(ppu16_init(p, &cfg, arena, sizeof arena) == PPU16_OK);
    memset(tiles, 0, sizeof tiles);
    tiles[0] = 0x21;                    /* pixel(0,0)=1, pixel(1,0)=2     */
    memset(map, 0, sizeof map);
    map[0] = entry;
    memset(bg, 0, sizeof *bg);
    bg->tiles = tiles; bg->map = map; bg->bpp = 4;
    bg->map_shift_x = 5; bg->map_shift_y = 5;
    CHECK(ppu16_bg_claim(p, 0, bg) == PPU16_OK);
}

static void test_tile_fetch(void)
{
    ppu16_t p; ppu16_bg_t bg;
    setup_bg(&p, &bg, 0x0400u /* pal slot 1 */);
    uint16_t line[32];
    p.pal_bg[16 + 1] = 0x1111;   /* slot 1 colour 1                    */
    p.pal_bg[16 + 2] = 0x2222;

    ppu16_render_line(&p, 0, line);
    CHECK(line[0] == 0x1111);           /* texel 1                      */
    CHECK(line[1] == 0x2222);           /* texel 2                      */
    CHECK(line[2] == 0x0000);           /* texel 0 = transparent        */
}

static void test_hflip(void)
{
    ppu16_t p; ppu16_bg_t bg;
    setup_bg(&p, &bg, 0x4400u /* hflip, pal slot 1 */);
    tiles[3] = 0x03;               /* flipped: fx'=6 reads texel 3       */
    p.pal_bg[16 + 3] = 0x2222;
    uint16_t line[32];
    ppu16_render_line(&p, 0, line);
    /* x=0 flips to texel 7 (0); x=1 flips to texel 6 (3).               */
    CHECK(line[0] == 0x0000);
    CHECK(line[1] == 0x2222);
}

static void test_wrap_masks(void)
{
    /* Scrolling left by 1 wraps through the map with no seams.          */
    ppu16_t p; ppu16_bg_t bg;
    setup_bg(&p, &bg, 0x0400u);
    bg.scroll_x = -1;
    CHECK(ppu16_bg_claim(&p, 0, &bg) == PPU16_OK);
    p.pal_bg[17] = 0x3333;
    uint16_t line[32];
    ppu16_render_line(&p, 0, line);
    /* x=0 samples map tile x=-1 -> wraps to tile 31 (transparent),      */
    /* x=1 samples tile 0 texel 0.                                       */
    CHECK(line[0] == 0x0000);
    CHECK(line[1] == 0x3333);
}

static void test_hdma_scroll_table(void)
{
    ppu16_t p; ppu16_bg_t bg;
    setup_bg(&p, &bg, 0x0400u);
    static int16_t tbl[8];
    for (int i = 0; i < 8; i++) tbl[i] = (int16_t)(i * 8);
    bg.scroll_x_table = tbl;
    CHECK(ppu16_bg_claim(&p, 0, &bg) == PPU16_OK);
    p.pal_bg[17] = 0x1111;
    /* Row 4 has scroll_x 32 -> map entry tile (4,0) — zeroed here.      */
    uint16_t line[32];
    ppu16_render_line(&p, 4, line);
    CHECK(line[0] == 0x0000);
    /* Row 0: scroll 0 -> texel 0 visible.                               */
    ppu16_render_line(&p, 0, line);
    CHECK(line[0] == 0x1111);
}

void register_tests_bg(void)
{
    ppu16_test_register("bg/tile_fetch", test_tile_fetch);
    ppu16_test_register("bg/hflip", test_hflip);
    ppu16_test_register("bg/wrap_masks", test_wrap_masks);
    ppu16_test_register("bg/hdma_scroll", test_hdma_scroll_table);
}
