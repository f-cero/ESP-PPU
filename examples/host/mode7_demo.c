/* mode7_demo — renders a rotating perspective floor over a checkerboard  */
/* world using the public API and writes PPM frames you can flip through: */
/*     for f in demo_*.ppm; do convert "$f" "${f%.ppm}.png"; done          */
#include "ppu16/ppu16.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define W 320
#define H 240
#define FRAMES 8

static uint8_t   map0[65536];                 /* LOD0 256x256             */
static uint8_t   mip1[16384], mip2[4096], mip3[1024];
static uint16_t  pal[256];
static uint16_t  fb[W * H];
static ppu16_mat6_t rowmat[H];

static void build_world(void)
{
    /* Checkerboard with a border ring, wrapping-friendly.               */
    for (int v = 0; v < 256; v++)
        for (int u = 0; u < 256; u++) {
            int c = ((u >> 4) ^ (v >> 4)) & 1;
            if (u < 8 || v < 8 || u > 247 || v > 247) c = 2;   /* border  */
            map0[v * 256 + u] = (uint8_t)(c ? 0x40 : 0x20);
        }
    /* Mip chain: 2x2 box filter.                                        */
    memcpy(mip1, map0, sizeof mip1);
    for (int v = 0; v < 128; v++)
        for (int u = 0; u < 128; u++)
            mip1[v * 128 + u] = (uint8_t)(((map0[(v*2)*256 + u*2]
                + map0[(v*2)*256 + u*2+1]) >> 1) & 0x7F);
    memcpy(mip2, mip1, sizeof mip2);
    memcpy(mip3, mip2, sizeof mip3);

    /* Palette: greys + accent.                                          */
    for (int i = 0; i < 256; i++)
        pal[i] = ppu16_rgb565((uint8_t)(i / 2), (uint8_t)(i / 3), (uint8_t)(255 - i));
    pal[0x20] = ppu16_rgb565(28, 30, 28);
    pal[0x40] = ppu16_rgb565(8, 12, 20);
}

static void write_ppm(const char *name, const uint16_t *img, int w, int h)
{
    FILE *f = fopen(name, "wb");
    if (!f) { perror(name); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint8_t px[3] = {
            (uint8_t)(PPU16_R565(img[i]) * 255 / 31),
            (uint8_t)(PPU16_G565(img[i]) * 255 / 63),
            (uint8_t)(PPU16_B565(img[i]) * 255 / 31)
        };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

int main(void)
{
    build_world();

    static uint8_t arena[PPU16_ARENA_MAX];
    ppu16_config_t cfg = { W, H, PPU16_MODE_AFFINE2, PPU16_CF_QFA_SCRATCH };
    ppu16_t ppu;
    if (ppu16_init(&ppu, &cfg, arena, sizeof arena) != PPU16_OK) return 1;

    ppu16_affine_t L = {
        .mip = { map0, mip1, mip2, mip3 },
        .palette = pal,
        .rowmat = rowmat, .rowmat_count = H,
        .dest_w = W, .dest_h = H,
        .mode = PPU16_AFF_QFA, .flags = PPU16_AF_WRAP,
        .priority = 3,                       /* behind everything        */
    };
    if (ppu16_affine_claim(&ppu, 0, &L) != PPU16_OK) return 1;

    ppu.fx.backdrop = ppu16_rgb565(4, 6, 12);

    char name[64];
    for (int fr = 0; fr < FRAMES; fr++) {
        /* Camera: rotate around the floor, pitch fixed, horizon at 96.  */
        double th  = fr * (6.28318 / FRAMES);
        int32_t cos_t = (int32_t)(cos(th) * 65536);
        int32_t sin_t = (int32_t)(sin(th) * 65536);
        ppu16_affine_perspective_fill(rowmat, H, 96,
                                      32         /* cam height, world units */,
                                      1 << 16  /* texels/world */,
                                      cos_t, sin_t);
        /* Camera position folded into the translation term.             */
        for (int y = 0; y < H; y++) {
            if (y <= 96) continue;
            rowmat[y].e += (128 << 16);
            rowmat[y].f += ((128 + fr * 2) << 16);
        }

        ppu16_render_frame(&ppu, fb);
        snprintf(name, sizeof name, "demo_%02d.ppm", fr);
        write_ppm(name, fb, W, H);
        printf("wrote %s\n", name);
    }
    printf("PPU-16 mode7 demo done: %d frames of %dx%d\n", FRAMES, W, H);
    return 0;
}
