/* ==========================================================================
 * PPU-16 — link_gpu.c: the default GPU-side dispatcher.
 * --------------------------------------------------------------------------
 * Applies decoded packets to a local ppu16_t plus the GPU's memory pools.
 * All wire formats are decoded field-by-field (never struct dumps), so
 * the host and GPU can differ in struct packing safely.
 * Layer wiring (which map entry array is BG0, which mip chain is affine
 * 0, …) is the port's job — the dispatcher only moves bytes and applies
 * registers. See docs/protocol.md and ports/esp32/.
 * ========================================================================== */
#include "ppu16/link.h"

#include <string.h>

ppu16_err_t ppu16_link_gpu_attach(ppu16_link_gpu_t *g, ppu16_t *ppu)
{
    if (!g || !ppu) return PPU16_E_PARAM;
    memset(g, 0, sizeof *g);
    g->ppu = ppu;
    return PPU16_OK;
}

static uint16_t get_u16(const uint8_t *p)
{ return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }

static uint32_t get_u32(const uint8_t *p)
{ return (uint32_t)get_u16(p) | ((uint32_t)get_u16(p + 2) << 16); }

static void apply_mode(ppu16_t *p, uint32_t mode)
{
    if (mode > PPU16_MODE_AFFINE2) return;
    p->cfg.mode = (uint8_t)mode;
    switch (mode) {
    case PPU16_MODE_TEXT:    p->n_bg = 2; p->n_affine = 0; break;
    case PPU16_MODE_RICH:    p->n_bg = 2; p->n_affine = 0; break;
    case PPU16_MODE_AFFINE:  p->n_bg = 1; p->n_affine = 1; break;
    case PPU16_MODE_AFFINE2: p->n_bg = 0; p->n_affine = 2; break;
    default: break;
    }
}

static void apply_reg(ppu16_t *p, uint16_t reg, const uint8_t *val)
{
    /* Most registers are a plain u32; window registers carry 10 extra   */
    /* bytes in the payload, handled by the caller.                      */
    uint32_t v = get_u32(val);
    switch (reg) {
    case PPU16_REG_MODE:           apply_mode(p, v); break;
    case PPU16_REG_CMATH_MODE:     p->fx.cmath_mode = (uint8_t)(v & 7u); break;
    case PPU16_REG_CMATH_TARGETS:  p->fx.cmath_targets = (uint8_t)(v & 0x3Fu); break;
    case PPU16_REG_BACKDROP:       p->fx.backdrop = (uint16_t)(v & 0xFFFFu); break;
    case PPU16_REG_BRIGHTNESS:     p->fx.brightness = (uint8_t)(v > 32u ? 32u : v); break;
    case PPU16_REG_FADE_COLOR:     p->fx.fade_color = (uint16_t)(v & 0xFFFFu); break;
    case PPU16_REG_MOSAIC:
        p->fx.mosaic_w = (uint8_t)(v & 0xFFu);
        p->fx.mosaic_h = (uint8_t)((v >> 8) & 0xFFu);
        break;
    case PPU16_REG_WIN_LOGIC:      p->fx.win_logic = (uint8_t)(v & 3u); break;
    case PPU16_REG_BG_SCX(0): case PPU16_REG_BG_SCX(1):
        p->bg[(reg - PPU16_REG_BG_SCX(0)) / 8].scroll_x = (int16_t)(v & 0xFFFFu); break;
    case PPU16_REG_BG_SCY(0): case PPU16_REG_BG_SCY(1):
        p->bg[(reg - PPU16_REG_BG_SCY(0)) / 8].scroll_y = (int16_t)(v & 0xFFFFu); break;
    case PPU16_REG_BG_PRIORITY(0): case PPU16_REG_BG_PRIORITY(1):
        p->bg[(reg - PPU16_REG_BG_PRIORITY(0)) / 8].priority = (uint8_t)(v & 3u); break;
    case PPU16_REG_BG_WINBITS(0): case PPU16_REG_BG_WINBITS(1):
        p->bg[(reg - PPU16_REG_BG_WINBITS(0)) / 8].win_bits = (uint8_t)(v & 3u); break;
    case PPU16_REG_AFF_MODE(0): case PPU16_REG_AFF_MODE(1):
        p->affine[(reg - PPU16_REG_AFF_MODE(0)) / 8].mode = (uint8_t)(v & 3u); break;
    case PPU16_REG_AFF_FLAGS(0): case PPU16_REG_AFF_FLAGS(1):
        p->affine[(reg - PPU16_REG_AFF_FLAGS(0)) / 8].flags = (uint8_t)(v & 0xFFu); break;
    case PPU16_REG_AFF_PRI(0): case PPU16_REG_AFF_PRI(1):
        p->affine[(reg - PPU16_REG_AFF_PRI(0)) / 8].priority = (uint8_t)(v & 3u); break;
    default: break;   /* unknown register: ignored, forward-compatible */
    }
}

/* Window register payload: [u16 reg][u16 x0 y0 x1 y1][u8 en][u8 inv].   */
static void apply_win(ppu16_t *p, uint16_t reg, const uint8_t *pl)
{
    ppu16_window_t *w = &p->win[reg == PPU16_REG_WIN0 ? 0 : 1];
    w->x0 = get_u16(pl);       w->y0 = get_u16(pl + 2);
    w->x1 = get_u16(pl + 4);   w->y1 = get_u16(pl + 6);
    w->enable = pl[8]; w->invert = pl[9];
}

void ppu16_link_gpu_dispatch(void *user, uint8_t cmd,
                             const uint8_t *payload, uint16_t len)
{
    ppu16_link_gpu_t *g = (ppu16_link_gpu_t *)user;
    ppu16_t *p = g ? g->ppu : NULL;
    if (!g || !p) return;

    switch (cmd) {
    case PPU16_CMD_RESET:
        ppu16_init(p, &p->cfg, p->arena, p->arena_size);
        g->frame_active = 0;
        break;

    case PPU16_CMD_FRAME:
        g->frame_active = 1;
        break;

    case PPU16_CMD_PRESENT:
        g->frame_active = 0;
        break;

    case PPU16_CMD_MEM_WRITE: {
        if (len < 8) return;
        uint32_t addr = get_u32(payload);
        uint32_t n    = get_u32(payload + 4);
        const uint8_t *data = payload + 8;
        if ((uint32_t)len < 8u + n) return;

        if (addr - PPU16_AS_BG_TILES < g->bg_tiles_size)
            memcpy(g->bg_tiles + (addr - PPU16_AS_BG_TILES), data,
                   n > g->bg_tiles_size - (addr - PPU16_AS_BG_TILES)
                   ? g->bg_tiles_size - (addr - PPU16_AS_BG_TILES) : n);
        else if (addr >= PPU16_AS_SPR_TILES && addr - PPU16_AS_SPR_TILES < g->spr_tiles_size)
            memcpy(g->spr_tiles + (addr - PPU16_AS_SPR_TILES), data,
                   n > g->spr_tiles_size - (addr - PPU16_AS_SPR_TILES)
                   ? g->spr_tiles_size - (addr - PPU16_AS_SPR_TILES) : n);
        else if (addr >= PPU16_AS_MAP && (addr - PPU16_AS_MAP) / 2 < g->map_size) {
            uint32_t off = (addr - PPU16_AS_MAP) / 2;
            for (uint32_t i = 0; i < n / 2 && off + i < g->map_size; i++)
                g->map[off + i] = get_u16(data + i * 2);
        }
        else if (addr >= PPU16_AS_PAL_BG && addr - PPU16_AS_PAL_BG < PPU16_MAX_PAL * 2) {
            uint32_t off = (addr - PPU16_AS_PAL_BG) / 2;
            for (uint32_t i = 0; i < n / 2 && off + i < PPU16_MAX_PAL; i++)
                p->pal_bg[off + i] = get_u16(data + i * 2);
        }
        else if (addr >= PPU16_AS_PAL_SPR && addr - PPU16_AS_PAL_SPR < PPU16_MAX_PAL * 2) {
            uint32_t off = (addr - PPU16_AS_PAL_SPR) / 2;
            for (uint32_t i = 0; i < n / 2 && off + i < PPU16_MAX_PAL; i++)
                p->pal_spr[off + i] = get_u16(data + i * 2);
        }
        else {
            for (int li = 0; li < PPU16_MAX_AFFINE; li++) {
                uint32_t base = PPU16_AS_ROWMAT0(li);
                if (addr >= base && (addr - base) / 24 < g->rowmat_pool_rows) {
                    uint32_t off = (addr - base) / 24;
                    for (uint32_t i = 0; i < n / 24 && off + i < g->rowmat_pool_rows; i++) {
                        const uint8_t *w = data + i * 24;
                        ppu16_mat6_t *m = &g->rowmat_pool[li][off + i];
                        m->a = (int32_t)get_u32(w);      m->b = (int32_t)get_u32(w + 4);
                        m->c = (int32_t)get_u32(w + 8);  m->d = (int32_t)get_u32(w + 12);
                        m->e = (int32_t)get_u32(w + 16); m->f = (int32_t)get_u32(w + 20);
                    }
                }
            }
        }
        break;
    }

    case PPU16_CMD_REG_WRITE: {
        uint16_t off = 0;
        while (off + 6 <= len) {
            uint16_t reg = get_u16(payload + off);
            if (reg == PPU16_REG_WIN0 || reg == PPU16_REG_WIN1) {
                if (off + 16 > len) break;          /* needs 6 + 10 bytes  */
                apply_win(p, reg, payload + off + 2);
                off += 16;
            } else {
                apply_reg(p, reg, payload + off + 2);
                off += 6;
            }
        }
        break;
    }

    case PPU16_CMD_OAM_WRITE: {
        if (len < 2) return;
        uint16_t count = get_u16(payload);
        if (count > PPU16_MAX_SPRITES) count = PPU16_MAX_SPRITES;
        if (len < (uint16_t)(2u + count * PPU16_OAM_WIRE_SIZE)) return;
        for (uint16_t i = 0; i < count; i++) {
            const uint8_t *w = payload + 2 + (uint16_t)i * PPU16_OAM_WIRE_SIZE;
            ppu16_oam_entry_t *s = &p->oam[i];
            s->x = (int16_t)get_u16(w);        s->y = (int16_t)get_u16(w + 2);
            s->w = w[4]; s->h = w[5]; s->tile = w[6]; s->pal = w[7];
            s->bpp = w[8]; s->priority = w[9]; s->flags = w[10];
            s->affine_id = (int8_t)w[11]; s->win_bits = 0;
        }
        p->oam_count = count;
        break;
    }

    case PPU16_CMD_MAT_WRITE: {
        if (len < 2) return;
        uint16_t count = get_u16(payload);
        if (count > PPU16_MAX_MAT) count = PPU16_MAX_MAT;
        if (len < (uint16_t)(2u + count * PPU16_MAT_WIRE_SIZE)) return;
        for (uint16_t i = 0; i < count; i++) {
            const uint8_t *w = payload + 2 + (uint16_t)i * PPU16_MAT_WIRE_SIZE;
            ppu16_mat_t *m = &p->spr_mat[i];
            m->a = (int32_t)get_u32(w);     m->b = (int32_t)get_u32(w + 4);
            m->c = (int32_t)get_u32(w + 8); m->d = (int32_t)get_u32(w + 12);
        }
        break;
    }

    default:
        break;   /* unknown command: ignored, forward-compatible */
    }
}
