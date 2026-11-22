/* ==========================================================================
 * PPU-16 — link.c: packet framing, CRC, host encoder, GPU decoder.
 * --------------------------------------------------------------------------
 * Wire format: [0xA5][cmd][len u16 LE][payload][crc16 LE] where the CRC
 * covers cmd+len+payload. Transport-agnostic: the host hands complete
 * packets to a write callback; the decoder is fed a raw byte stream and
 * resynchronises on the sync byte, so it tolerates garbage between
 * packets (e.g. other traffic on the bus).
 * ========================================================================== */
#include "ppu16/link.h"

#include <string.h>

uint16_t ppu16_link_crc16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFF;                     /* CCITT-FALSE             */
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (int b = 0; b < 8; b++) {
            uint16_t next = (uint16_t)(crc << 1);
            if (crc & 0x8000u) next ^= 0x1021u;
            crc = next;
        }
    }
    return crc;
}

/* ------------------------------------------------------------------ */
/* Host encoder                                                         */
/* ------------------------------------------------------------------ */

static void put_u16(uint8_t *p, uint16_t v)
{ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static void put_u32(uint8_t *p, uint32_t v)
{ put_u16(p, (uint16_t)v); put_u16(p + 2, (uint16_t)(v >> 16)); }

/* Finalise and flush the packet under construction in h->buf.          */
static ppu16_err_t host_flush(ppu16_link_host_t *h)
{
    uint16_t n = (uint16_t)(h->len - 4);       /* payload length          */
    if (!h->out.write) return PPU16_E_PARAM;
    h->buf[2] = (uint8_t)n; h->buf[3] = (uint8_t)(n >> 8);
    uint16_t crc = ppu16_link_crc16(h->buf + 1, (uint32_t)n + 3);
    put_u16(h->buf + h->len, crc);
    h->len += 2;
    if (h->out.write(h->out.user, h->buf, h->len) != 0) return PPU16_E_STATE;
    h->len = 0;
    return PPU16_OK;
}

static void host_start(ppu16_link_host_t *h, uint8_t cmd)
{
    h->len = 0;
    h->buf[h->len++] = PPU16_LINK_SYNC;
    h->buf[h->len++] = cmd;
    h->len += 2;                                /* reserve length        */
}

void ppu16_link_host_init(ppu16_link_host_t *h, ppu16_link_sink_t sink)
{
    memset(h, 0, sizeof *h);
    h->out = sink;
}

ppu16_err_t ppu16_link_host_hello(ppu16_link_host_t *h)
{
    if (!h) return PPU16_E_PARAM;
    host_start(h, PPU16_CMD_HELLO);
    memcpy(h->buf + h->len, "PPU16", 5); h->len += 5;
    h->buf[h->len++] = PPU16_LINK_VERSION;
    return host_flush(h);
}

ppu16_err_t ppu16_link_host_frame(ppu16_link_host_t *h)
{
    if (!h) return PPU16_E_PARAM;
    host_start(h, PPU16_CMD_FRAME);
    return host_flush(h);
}

ppu16_err_t ppu16_link_host_present(ppu16_link_host_t *h)
{
    if (!h) return PPU16_E_PARAM;
    host_start(h, PPU16_CMD_PRESENT);
    return host_flush(h);
}

ppu16_err_t ppu16_link_host_reset(ppu16_link_host_t *h)
{
    if (!h) return PPU16_E_PARAM;
    host_start(h, PPU16_CMD_RESET);
    return host_flush(h);
}

ppu16_err_t ppu16_link_host_mem(ppu16_link_host_t *h, uint32_t addr,
                                const uint8_t *data, uint32_t n)
{
    if (!h || (!data && n)) return PPU16_E_PARAM;
    while (n) {
        uint32_t chunk = n;
        const uint32_t max = PPU16_LINK_PAYLOAD_MAX - 8u;
        if (chunk > max) chunk = max;
        host_start(h, PPU16_CMD_MEM_WRITE);
        put_u32(h->buf + h->len, addr);          h->len += 4;
        put_u32(h->buf + h->len, chunk);         h->len += 4;
        memcpy(h->buf + h->len, data, chunk);    h->len += (uint16_t)chunk;
        ppu16_err_t e = host_flush(h);
        if (e != PPU16_OK) return e;
        data += chunk; addr += chunk; n -= chunk;
    }
    return PPU16_OK;
}

ppu16_err_t ppu16_link_host_reg(ppu16_link_host_t *h, uint16_t reg, uint32_t val)
{
    if (!h) return PPU16_E_PARAM;
    host_start(h, PPU16_CMD_REG_WRITE);
    put_u16(h->buf + h->len, reg);              h->len += 2;
    put_u32(h->buf + h->len, val);              h->len += 4;
    return host_flush(h);
}

ppu16_err_t ppu16_link_host_oam(ppu16_link_host_t *h,
                                const ppu16_oam_entry_t *oam, uint16_t count)
{
    if (!h || (!oam && count)) return PPU16_E_PARAM;
    if (count > PPU16_MAX_SPRITES) return PPU16_E_RANGE;
    host_start(h, PPU16_CMD_OAM_WRITE);
    put_u16(h->buf + h->len, count);            h->len += 2;
    for (uint16_t i = 0; i < count; i++) {
        const ppu16_oam_entry_t *s = &oam[i];
        uint8_t *w = h->buf + h->len;
        put_u16(w, (uint16_t)s->x);      put_u16(w + 2, (uint16_t)s->y);
        w[4] = s->w; w[5] = s->h; w[6] = s->tile; w[7] = s->pal;
        w[8] = s->bpp; w[9] = s->priority; w[10] = s->flags;
        w[11] = (uint8_t)s->affine_id;
        h->len += PPU16_OAM_WIRE_SIZE;
    }
    return host_flush(h);
}

ppu16_err_t ppu16_link_host_mat(ppu16_link_host_t *h,
                                const ppu16_mat_t *mat, uint16_t count)
{
    if (!h || (!mat && count)) return PPU16_E_PARAM;
    if (count > PPU16_MAX_MAT) return PPU16_E_RANGE;
    host_start(h, PPU16_CMD_MAT_WRITE);
    put_u16(h->buf + h->len, count);            h->len += 2;
    for (uint16_t i = 0; i < count; i++) {
        uint8_t *w = h->buf + h->len;
        put_u32(w,      (uint32_t)mat[i].a);
        put_u32(w + 4,  (uint32_t)mat[i].b);
        put_u32(w + 8,  (uint32_t)mat[i].c);
        put_u32(w + 12, (uint32_t)mat[i].d);
        h->len += PPU16_MAT_WIRE_SIZE;
    }
    return host_flush(h);
}

/* ------------------------------------------------------------------ */
/* GPU decoder — byte-stream state machine                              */
/* ------------------------------------------------------------------ */

void ppu16_link_decoder_init(ppu16_link_decoder_t *d,
                             ppu16_link_dispatch_fn fn, void *user)
{
    memset(d, 0, sizeof *d);
    d->on_packet = fn;
    d->user = user;
    d->state = 1;                               /* header after sync     */
    d->need = 3;                                /* cmd + len             */
}

void ppu16_link_decode(ppu16_link_decoder_t *d, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        uint8_t b = data[i];
        switch (d->state) {
        case 0:                                  /* hunting for sync     */
            if (b == PPU16_LINK_SYNC) {
                d->state = 1; d->have = 0; d->need = 3;
            }
            break;

        case 1:                                  /* header               */
            d->buf[d->have++] = b;
            if (d->have >= d->need) {
                d->cmd = d->buf[0];
                uint16_t plen = (uint16_t)(d->buf[1] | ((uint16_t)d->buf[2] << 8));
                if (plen > PPU16_LINK_PAYLOAD_MAX) { d->state = 0; break; }
                /* Shift: keep [cmd len payload...] layout for CRC.      */
                d->need = (uint16_t)(3 + plen + 2);
                d->state = 2;
            }
            break;

        case 2:                                  /* payload + crc        */
            d->buf[d->have++] = b;
            if (d->have >= d->need) {
                uint16_t plen = (uint16_t)(d->buf[1] | ((uint16_t)d->buf[2] << 8));
                uint16_t crc_calc = ppu16_link_crc16(d->buf, (uint32_t)plen + 3);
                uint16_t crc_wire = (uint16_t)(d->buf[3 + plen]
                                   | ((uint16_t)d->buf[3 + plen + 1] << 8));
                if (crc_calc == crc_wire && d->on_packet) {
                    d->on_packet(d->user, d->cmd, d->buf + 3, plen);
                }
                d->state = 0;
            }
            break;

        default:
            d->state = 0;
            break;
        }
    }
}
