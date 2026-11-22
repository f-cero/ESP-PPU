/* Colour math must be exact — the console look depends on it.           */
#include "test_util.h"
#include "ppu16/color.h"

static void test_rgb_roundtrip(void)
{
    uint16_t c = ppu16_rgb565(31, 63, 31);
    CHECK(PPU16_R565(c) == 31 && PPU16_G565(c) == 63 && PPU16_B565(c) == 31);
}

static void test_blend_add_saturation(void)
{
    uint16_t w = ppu16_rgb565(31, 63, 31);
    /* Saturates at white, never wraps.                                  */
    CHECK(ppu16_blend_add(w, w) == w);
    CHECK(ppu16_blend_add(ppu16_rgb565(10, 20, 30), ppu16_rgb565(5, 5, 5))
          == ppu16_rgb565(15, 25, 31));
}

static void test_blend_sub_clamp(void)
{
    uint16_t z = ppu16_rgb565(0, 0, 0);
    CHECK(ppu16_blend_sub(z, z) == z);
    CHECK(ppu16_blend_sub(ppu16_rgb565(20, 20, 20), ppu16_rgb565(30, 30, 30)) == z);
}

static void test_half(void)
{
    /* Non-saturating pair: (a+a)/2 == a.                                 */
    uint16_t a = ppu16_rgb565(10, 20, 5);
    uint16_t h = ppu16_blend_add_half(a, a);
    CHECK(PPU16_R565(h) == 10 && PPU16_G565(h) == 20 && PPU16_B565(h) == 5);
}

static void test_fade(void)
{
    uint16_t a = ppu16_rgb565(0, 0, 0);
    uint16_t w = ppu16_rgb565(31, 63, 31);
    CHECK(ppu16_fade(a, w, 0) == a);
    CHECK(ppu16_fade(a, w, 32) == w);
    uint16_t mid = ppu16_fade(a, w, 16);
    CHECK(PPU16_R565(mid) >= 15 && PPU16_R565(mid) <= 16);
}

void register_tests_color(void)
{
    ppu16_test_register("color/roundtrip", test_rgb_roundtrip);
    ppu16_test_register("color/add_saturate", test_blend_add_saturation);
    ppu16_test_register("color/sub_clamp", test_blend_sub_clamp);
    ppu16_test_register("color/half", test_half);
    ppu16_test_register("color/fade", test_fade);
}
