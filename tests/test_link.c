/* Link protocol: encode on the "host", decode on the "gpu", verify the   */
/* PPU state arrived intact. Runs over an in-memory sink (no hardware).   */
#include "test_util.h"
#include "ppu16/ppu16.h"
#include <string.h>

static uint8_t wire[4096];
static uint32_t wire_len;

static int sink_write(void *user, const uint8_t *data, uint32_t len)
{
    (void)user;
    if (wire_len + len > sizeof wire) return -1;
    memcpy(wire + wire_len, data, len);
    wire_len += len;
    return 0;
}

static void test_crc(void)
{
    /* CCITT-FALSE("123456789") == 0x29B1 — the canonical check value.    */
    uint16_t c = ppu16_link_crc16((const uint8_t *)"123456789", 9);
    CHECK(c == 0x29B1u);
}

static void test_roundtrip(void)
{
    static ppu16_t gpu_ppu;
    static uint8_t bg_tiles[4096], spr_tiles[4096];
    static uint16_t map[2048];
    static ppu16_mat6_t rowmats[PPU16_MAX_AFFINE][16];

    ppu16_config_t cfg = { 320, 240, PPU16_MODE_AFFINE, 0 };
    CHECK(ppu16_init(&gpu_ppu, &cfg, NULL, 0) == PPU16_OK);

    ppu16_link_gpu_t gpu;
    CHECK(ppu16_link_gpu_attach(&gpu, &gpu_ppu) == PPU16_OK);
    gpu.bg_tiles = bg_tiles;   gpu.bg_tiles_size = sizeof bg_tiles;
    gpu.spr_tiles = spr_tiles; gpu.spr_tiles_size = sizeof spr_tiles;
    gpu.map = map;             gpu.map_size = 2048;
    gpu.rowmat_pool[0] = rowmats[0]; gpu.rowmat_pool_rows = 16;

    ppu16_link_decoder_t dec;
    ppu16_link_decoder_init(&dec, ppu16_link_gpu_dispatch, &gpu);

    /* --- host side: build a real frame's worth of traffic.            */
    ppu16_link_host_t host;
    ppu16_link_host_init(&host, (ppu16_link_sink_t){ sink_write, NULL });
    wire_len = 0;

    CHECK(ppu16_link_host_hello(&host) == PPU16_OK);
    CHECK(ppu16_link_host_frame(&host) == PPU16_OK);
    CHECK(ppu16_link_host_reg(&host, PPU16_REG_MODE, PPU16_MODE_AFFINE) == PPU16_OK);
    CHECK(ppu16_link_host_reg(&host, PPU16_REG_BACKDROP, 0x7C10) == PPU16_OK);
    CHECK(ppu16_link_host_reg(&host, PPU16_REG_CMATH_MODE, PPU16_CM_ADD) == PPU16_OK);
    CHECK(ppu16_link_host_reg(&host, PPU16_REG_MOSAIC, 0x0404u) == PPU16_OK);

    uint8_t blob[300];
    for (int i = 0; i < 300; i++) blob[i] = (uint8_t)i;
    CHECK(ppu16_link_host_mem(&host, PPU16_AS_BG_TILES + 16, blob, 300) == PPU16_OK);
    CHECK(ppu16_link_host_mem(&host, PPU16_AS_PAL_BG, blob, 512 / 2) == PPU16_OK);

    uint16_t entries[4] = { 0x0400, 0x0401, 0x8402, 0x4403 };
    CHECK(ppu16_link_host_mem(&host, PPU16_AS_MAP, (const uint8_t *)entries, 8) == PPU16_OK);

    ppu16_oam_entry_t oam[3] = {
        { 10, 20, 16, 16, 4, 1, 4, 1, 0, -1, 0 },
        { 40, 60, 8, 8, 0, 0, 4, 2, PPU16_SF_HFLIP, -1, 0 },
        { 100, 100, 32, 32, 8, 2, 8, 0, 0, 1, 0 }
    };
    CHECK(ppu16_link_host_oam(&host, oam, 3) == PPU16_OK);

    ppu16_mat_t mats[2] = { { 1 << 16, 0, 0, 1 << 16 }, { 0, 1 << 15, -(1 << 15), 0 } };
    CHECK(ppu16_link_host_mat(&host, mats, 2) == PPU16_OK);

    CHECK(ppu16_link_host_present(&host) == PPU16_OK);

    /* --- gpu side: feed the wire.                                      */
    ppu16_link_decode(&dec, wire, wire_len);

    /* Verify applied state.                                              */
    CHECK(gpu_ppu.cfg.mode == PPU16_MODE_AFFINE);
    CHECK(gpu_ppu.fx.backdrop == 0x7C10);
    CHECK(gpu_ppu.fx.cmath_mode == PPU16_CM_ADD);
    CHECK(gpu_ppu.fx.mosaic_w == 4 && gpu_ppu.fx.mosaic_h == 4);
    CHECK(gpu_ppu.oam_count == 3);
    CHECK(gpu_ppu.oam[0].x == 10 && gpu_ppu.oam[0].w == 16);
    CHECK(gpu_ppu.oam[2].affine_id == 1);
    CHECK(gpu_ppu.oam[1].flags == PPU16_SF_HFLIP);
    CHECK(gpu_ppu.spr_mat[1].b == (1 << 15));
    CHECK(gpu_ppu.spr_mat[1].c == -(1 << 15));
    CHECK(memcmp(bg_tiles + 16, blob, 300) == 0);
    CHECK(map[0] == 0x0400 && map[2] == 0x8402 && map[3] == 0x4403);
    CHECK(gpu.frame_active == 0);       /* PRESENT closed the frame       */
}

static void test_garbage_resync(void)
{
    static ppu16_t p;
    ppu16_config_t cfg = { 8, 8, PPU16_MODE_TEXT, 0 };
    CHECK(ppu16_init(&p, &cfg, NULL, 0) == PPU16_OK);
    ppu16_link_gpu_t gpu;
    CHECK(ppu16_link_gpu_attach(&gpu, &p) == PPU16_OK);
    ppu16_link_decoder_t dec;
    ppu16_link_decoder_init(&dec, ppu16_link_gpu_dispatch, &gpu);

    /* Noise between packets must not corrupt framing.                    */
    uint8_t buf[64];
    memset(buf, 0x00, sizeof buf);
    /* Valid packet: REG_WRITE backdrop 0xABCD.                           */
    uint8_t pkt[16];
    uint32_t pl = 0;
    pkt[pl++] = 0xA5; pkt[pl++] = PPU16_CMD_REG_WRITE;
    pkt[pl++] = 6; pkt[pl++] = 0;
    pkt[pl++] = (uint8_t)(PPU16_REG_BACKDROP & 0xFF); pkt[pl++] = (uint8_t)(PPU16_REG_BACKDROP >> 8);
    pkt[pl++] = 0xCD; pkt[pl++] = 0xAB; pkt[pl++] = 0; pkt[pl++] = 0;
    uint16_t crc = ppu16_link_crc16(pkt + 1, pl - 1);
    pkt[pl++] = (uint8_t)crc; pkt[pl++] = (uint8_t)(crc >> 8);

    memcpy(buf + 5, pkt, pl);
    ppu16_link_decode(&dec, buf, 5 + pl);   /* leading noise + packet    */
    CHECK(p.fx.backdrop == 0xABCD);
}

void register_tests_link(void)
{
    ppu16_test_register("link/crc16", test_crc);
    ppu16_test_register("link/roundtrip", test_roundtrip);
    ppu16_test_register("link/garbage_resync", test_garbage_resync);
}
