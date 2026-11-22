/* Sprites: tile fetch, flips, affine transform sanity.                  */
#include "test_util.h"
#include "ppu16/ppu16.h"
#include <string.h>

static uint8_t tiles[64 * 4];      /* 4 tiles of 8x8 4bpp                */
static ppu16_t p;

static void setup(void)
{
    ppu16_config_t cfg = { 64, 64, PPU16_MODE_TEXT, 0 };
    static uint8_t arena[16];
    CHECK(ppu16_init(&p, &cfg, arena, sizeof arena) == PPU16_OK);
    memset(tiles, 0, sizeof tiles);
    p.spr_tiles = tiles;
}

static void test_basic_sprite(void)
{
    setup();
    tiles[0] = 0x01;               /* texel(0,0)=1                       */
    p.pal_spr[1] = 0xAAAA;

    ppu16_oam_entry_t s = {
        .x = 10, .y = 5, .w = 8, .h = 8, .tile = 0,
        .pal = 0, .bpp = 4, .priority = 1, .flags = 0, .affine_id = -1
    };
    p.oam[0] = s; p.oam_count = 1;

    uint16_t line[64];
    ppu16_render_line(&p, 5, line);
    CHECK(line[10] == 0xAAAA);
    CHECK(line[11] == 0x0000);
    /* One row above/below: sprite absent.                               */
    ppu16_render_line(&p, 4, line);
    CHECK(line[10] == 0x0000);
}

static void test_vflip(void)
{
    setup();
    tiles[0] = 0x01;
    p.pal_spr[1] = 0xAAAA;
    ppu16_oam_entry_t s = {
        .x = 0, .y = 0, .w = 8, .h = 8, .tile = 0,
        .pal = 0, .bpp = 4, .priority = 1,
        .flags = PPU16_SF_VFLIP, .affine_id = -1
    };
    p.oam[0] = s; p.oam_count = 1;
    uint16_t line[64];
    ppu16_render_line(&p, 7, line);      /* flipped: row 7 shows texel row 0 */
    CHECK(line[0] == 0xAAAA);
    ppu16_render_line(&p, 0, line);
    CHECK(line[0] == 0x0000);
}

static void test_affine_sprite(void)
{
    setup();
    tiles[0] = 0x01;
    p.pal_spr[1] = 0xAAAA;
    ppu16_oam_entry_t s = {
        .x = 16, .y = 16, .w = 16, .h = 16, .tile = 0,
        .pal = 0, .bpp = 4, .priority = 1, .flags = 0, .affine_id = 0
    };
    p.oam[0] = s; p.oam_count = 1;
    /* Identity matrix: affine sprite must render like a plain one.      */
    p.spr_mat[0].a = 1 << 16; p.spr_mat[0].b = 0;
    p.spr_mat[0].c = 0;       p.spr_mat[0].d = 1 << 16;
    uint16_t line[64];
    ppu16_render_line(&p, 16, line);
    CHECK(line[16] == 0xAAAA);
}

static void test_2d_tile_layout(void)
{
    setup();
    /* 16x8 sprite: tile 1 is the right half.                            */
    tiles[32] = 0x01;
    p.pal_spr[1] = 0xBBBB;
    ppu16_oam_entry_t s = {
        .x = 0, .y = 0, .w = 16, .h = 8, .tile = 0,
        .pal = 0, .bpp = 4, .priority = 1, .flags = 0, .affine_id = -1
    };
    p.oam[0] = s; p.oam_count = 1;
    uint16_t line[64];
    ppu16_render_line(&p, 0, line);
    CHECK(line[8] == 0xBBBB);
}

void register_tests_sprite(void)
{
    ppu16_test_register("sprite/basic", test_basic_sprite);
    ppu16_test_register("sprite/vflip", test_vflip);
    ppu16_test_register("sprite/affine_identity", test_affine_sprite);
    ppu16_test_register("sprite/2d_tiles", test_2d_tile_layout);
}
