/* ==========================================================================
 * PPU-16 — link.h: the game-CPU <-> GPU link protocol.
 * --------------------------------------------------------------------------
 * When the PPU runs on a different chip than the game code (e.g. a CYD as
 * display+GPU), game code targets the SAME register model through packets:
 *
 *      [ u8 sync=0xA5 | u8 cmd | u16 len (LE) | payload | u16 crc16 (LE) ]
 *
 * CRC is CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF). All multi-byte
 * fields are little-endian. Payload is capped at PPU16_LINK_PAYLOAD_MAX.
 *
 * The transport is injectable: the host encoder hands bytes to a write
 * callback (SPI, UART, loopback for tests); the GPU side decoder is fed
 * bytes and emits packets to a dispatcher. Nothing in here knows about
 * SPI hardware — that binding lives in ports/.
 * ========================================================================== */
#ifndef PPU16_LINK_H
#define PPU16_LINK_H

#include "types.h"
#include "ppu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PPU16_LINK_VERSION       1
#define PPU16_LINK_SYNC          0xA5u
#define PPU16_LINK_PAYLOAD_MAX   512u
#define PPU16_LINK_MAX_PKT       (PPU16_LINK_PAYLOAD_MAX + 8u)

/* Commands.                                                           */
#define PPU16_CMD_HELLO      0x01u   /* payload: magic+version handshake   */
#define PPU16_CMD_CAPS       0x02u   /* gpu -> host: capability report     */
#define PPU16_CMD_RESET      0x03u
#define PPU16_CMD_ECHO       0x04u
#define PPU16_CMD_MEM_WRITE  0x10u   /* [u32 addr][u32 n][bytes]           */
#define PPU16_CMD_REG_WRITE  0x11u   /* ([u16 reg][u32 val])*              */
#define PPU16_CMD_OAM_WRITE  0x12u   /* packed OAM entries (wire format)   */
#define PPU16_CMD_MAT_WRITE  0x13u   /* packed affine sprite matrices      */
#define PPU16_CMD_FRAME      0x14u   /* begin frame: following regs apply  */
#define PPU16_CMD_PRESENT    0x15u   /* end frame: scanout                 */

/* Register-file addresses (REG_WRITE). Mirrors the ppu16_t fields.     */
#define PPU16_REG_MODE          0x00u
#define PPU16_REG_CMATH_MODE    0x10u
#define PPU16_REG_CMATH_TARGETS 0x11u
#define PPU16_REG_BACKDROP      0x12u
#define PPU16_REG_BRIGHTNESS    0x13u
#define PPU16_REG_FADE_COLOR    0x14u
#define PPU16_REG_MOSAIC        0x15u   /* [u8 w][u8 h] in low 16 bits       */
#define PPU16_REG_WIN_LOGIC     0x16u
#define PPU16_REG_WIN0          0x20u   /* [u16 x0 y0 x1 y1][u8 en inv]      */
#define PPU16_REG_WIN1          0x21u
#define PPU16_REG_BG_SCX(i)     (0x30u + (i) * 8u)
#define PPU16_REG_BG_SCY(i)     (0x31u + (i) * 8u)
#define PPU16_REG_BG_PRIORITY(i)(0x32u + (i) * 8u)
#define PPU16_REG_BG_WINBITS(i) (0x33u + (i) * 8u)
#define PPU16_REG_AFF_MODE(i)   (0x40u + (i) * 8u)
#define PPU16_REG_AFF_FLAGS(i)  (0x41u + (i) * 8u)
#define PPU16_REG_AFF_DEST(i)   (0x42u + (i) * 8u)  /* x,y,w,h                 */
#define PPU16_REG_AFF_PRI(i)    (0x43u + (i) * 8u)

/* GPU-side address space for MEM_WRITE. The GPU port places its        */
/* palette/OAM/tile pools at these offsets (ppu16_link_gpu_attach).     */
#define PPU16_AS_BG_TILES   0x00000000u
#define PPU16_AS_SPR_TILES  0x00010000u
#define PPU16_AS_MAP        0x00020000u
#define PPU16_AS_PAL_BG     0x00030000u   /* 512 bytes                        */
#define PPU16_AS_PAL_SPR    0x00030200u   /* 512 bytes                        */
#define PPU16_AS_ROWMAT0(i) (0x00031000u + (i) * 0x00001000u) /* mat6 tables */

/* ------------------------------------------------------------------ */
/* Wire formats (packed, little-endian, converted field by field —      */
/* never struct dumps; see AGENTS.md).                                  */
/* ------------------------------------------------------------------ */
#define PPU16_OAM_WIRE_SIZE   12u  /* x s16 y s16 w u8 h u8 tile u8 pal u8
                                       bpp u8 pri u8 flags u8 affine s8
                                       win u8                            */
#define PPU16_MAT_WIRE_SIZE   16u  /* a b c d, each s32 LE                */

/* ------------------------------------------------------------------ */
/* Host side (game CPU): encode commands into a transport.              */
/* ------------------------------------------------------------------ */
typedef int (*ppu16_link_write_fn)(void *user, const uint8_t *data, uint32_t len);

typedef struct {
    ppu16_link_write_fn write;     /* returns 0 on success                */
    void *user;
} ppu16_link_sink_t;

typedef struct {
    ppu16_link_sink_t out;
    uint8_t  buf[PPU16_LINK_MAX_PKT];
    uint16_t len;
} ppu16_link_host_t;

void        ppu16_link_host_init(ppu16_link_host_t *h, ppu16_link_sink_t sink);
ppu16_err_t ppu16_link_host_hello(ppu16_link_host_t *h);
ppu16_err_t ppu16_link_host_frame(ppu16_link_host_t *h);      /* FRAME        */
ppu16_err_t ppu16_link_host_present(ppu16_link_host_t *h);    /* PRESENT      */
ppu16_err_t ppu16_link_host_reset(ppu16_link_host_t *h);
ppu16_err_t ppu16_link_host_mem(ppu16_link_host_t *h, uint32_t addr,
                                const uint8_t *data, uint32_t n);
ppu16_err_t ppu16_link_host_reg(ppu16_link_host_t *h, uint16_t reg, uint32_t val);
ppu16_err_t ppu16_link_host_oam(ppu16_link_host_t *h,
                                const ppu16_oam_entry_t *oam, uint16_t count);
ppu16_err_t ppu16_link_host_mat(ppu16_link_host_t *h,
                                const ppu16_mat_t *mat, uint16_t count);

/* ------------------------------------------------------------------ */
/* GPU side: byte-stream decoder + dispatcher.                          */
/* ------------------------------------------------------------------ */
typedef void (*ppu16_link_dispatch_fn)(void *user, uint8_t cmd,
                                       const uint8_t *payload, uint16_t len);

typedef struct {
    ppu16_link_dispatch_fn on_packet;
    void *user;
    /* parser state */
    uint8_t  buf[PPU16_LINK_MAX_PKT];
    uint16_t have, need;
    uint8_t  state;                /* 0 sync, 1 header, 2 payload, 3 crc  */
    uint8_t  cmd;
} ppu16_link_decoder_t;

void ppu16_link_decoder_init(ppu16_link_decoder_t *d,
                             ppu16_link_dispatch_fn fn, void *user);
/* Feed raw transport bytes; fires on_packet for every complete packet. */
void ppu16_link_decode(ppu16_link_decoder_t *d, const uint8_t *data, uint32_t len);

/* ------------------------------------------------------------------ */
/* GPU-side default dispatcher: applies packets to a local ppu16_t.     */
/* ------------------------------------------------------------------ */
typedef struct {
    ppu16_t      *ppu;
    /* GPU memory pools, wired up by ppu16_link_gpu_attach.               */
    uint8_t       *bg_tiles;  uint32_t bg_tiles_size;
    uint8_t       *spr_tiles; uint32_t spr_tiles_size;
    uint16_t      *map;       uint32_t map_size;      /* in entries          */
    ppu16_mat6_t  *rowmat_pool[PPU16_MAX_AFFINE];
    uint32_t       rowmat_pool_rows;
    uint8_t        frame_active;
} ppu16_link_gpu_t;

ppu16_err_t ppu16_link_gpu_attach(ppu16_link_gpu_t *g, ppu16_t *ppu);
void ppu16_link_gpu_dispatch(void *user, uint8_t cmd,
                             const uint8_t *payload, uint16_t len);

/* CRC-16/CCITT-FALSE over len bytes.                                   */
uint16_t ppu16_link_crc16(const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif
#endif /* PPU16_LINK_H */
