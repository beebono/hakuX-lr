/*
 * Host test of the native USB stack (native/dev/ohci.c, usb.c, usb_hub.c,
 * xid.c): a small host driver enumerates the front panel hub, a controller's
 * hub and its gamepad through OHCI EDs and TDs in fake guest RAM, then reads
 * a report and sends rumble. Then the controllers on the root ports, as
 * nc_dev_ohci_init makes them: plugged in before the boot and while the
 * console runs (nc_pad_plug), pulled out, and enumerated again. Then an Xbox
 * Live Communicator in player 1's controller (xblc.c): its isochronous
 * endpoints through chained isochronous TDs, a TD whose frames went by, one
 * for a later frame, isochronous processing switched off (HcControl.IE),
 * and the microphone's noise gate.
 *
 *   cc -O1 -I. tests/native/usb/usb_test.c native/dev/usb.c native/dev/usb_hub.c native/dev/xid.c \
 *      native/dev/xblc.c -lpthread -lm
 *
 * The build's usb-smoke test (native/cmake/linux-tests.cmake) is this.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* The MMIO and IRQ glue, stubbed (their headers need a POSIX host). */
#define NATIVE_CORE_MMIO_H
#define NATIVE_CORE_IRQ_H
#include "native/core/vm.h"
typedef struct NcMmioDev {
    const char *name;
    uint32_t base, size;
    uint64_t (*read)(void *opaque, uint32_t off, unsigned size);
    void (*write)(void *opaque, uint32_t off, uint64_t value, unsigned size);
    void *opaque;
} NcMmioDev;
static void nc_mmio_register(const NcMmioDev *dev)
{
}
static int g_irq_level;
static void nc_irq_set(unsigned line, bool level)
{
    g_irq_level = level;
}
void nc_log(const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("  [%s] ", tag);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}
/* The emulation never pauses here; the frame thread only starts and stops
 * (the test runs the frames). */
bool nc_paused(void)
{
    return false;
}
static void nc_pause_register(const char *name)
{
}
static void nc_pause_idle(bool idle)
{
}
static void nc_pause_wait(void)
{
}
/* Frame-timed input scripts have no renderer in this USB-only test. */
uint64_t hle_back_swap_count(void)
{
    return 0;
}

#include "native/dev/ohci.c"

/* Savestates aren't tested here. */
void nc_buf_put(NcBuf *b, const void *p, size_t n)
{
}
bool nc_rd_get(NcRd *r, void *p, size_t n)
{
    return false;
}
void nc_rec_put(NcBuf *b, const NcRecord *rec, const void *obj)
{
}
void nc_rec_get(NcRd *r, const NcRecord *rec, void *obj, unsigned v)
{
}

static int g_fail;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                                       \
            g_fail++;                                                                                                  \
        }                                                                                                              \
    } while (0)

#define RAM_SIZE (1u << 20)
static uint8_t *g_ram;
static NcVm g_vm;
static Ohci *o = &g_ohci;

#define HCCA   0x10000u
#define ED_CTL 0x11000u
#define ED_INT 0x11100u
#define ED_ISO 0x11200u
#define TDS    0x12000u
#define ITDS   0x13000u
#define BUFS   0x20000u

static uint32_t rd(uint32_t pa)
{
    return nc_rd32(g_ram + pa);
}
static void wr(uint32_t pa, uint32_t v)
{
    nc_wr32(g_ram + pa, v);
}
static uint32_t reg(uint32_t off)
{
    return (uint32_t)ohci_read(o, off, 4);
}
static void set_reg(uint32_t off, uint32_t v)
{
    ohci_write(o, off, v, 4);
}

static void td(uint32_t pa, unsigned dp, unsigned toggle, bool round, unsigned di, uint32_t buf, uint32_t len,
               uint32_t next)
{
    wr(pa, (0xfu << 28) | (toggle << 24) | (di << 21) | (dp << 19) | (round ? TD_R : 0));
    wr(pa + 4, len ? buf : 0);
    wr(pa + 8, next);
    wr(pa + 12, len ? buf + len - 1 : 0);
}

/* Runs frames until the done queue arrives; returns it (WDH acknowledged). */
static uint32_t wait_done(void)
{
    for (int i = 0; i < 16; i++) {
        frame(o);
        if (reg(0x0C) & INT_WDH) {
            const uint32_t d = rd(HCCA + 0x84);
            wr(HCCA + 0x84, 0);
            set_reg(0x0C, INT_WDH);
            return d & ~1u;
        }
    }
    return 0;
}

static unsigned td_cc(uint32_t pa)
{
    return rd(pa) >> 28;
}

/* A control transfer to (addr, ep 0). Returns the data stage's byte count, or -cc. */
static int control(uint8_t addr, uint8_t type, uint8_t req, uint16_t value, uint16_t index, uint8_t *data,
                   uint16_t len)
{
    const bool in = type & 0x80;
    uint8_t *setup = g_ram + BUFS;
    setup[0] = type;
    setup[1] = req;
    nc_wr16(setup + 2, value);
    nc_wr16(setup + 4, index);
    nc_wr16(setup + 6, len);
    if (!in && len) {
        memcpy(g_ram + BUFS + 0x100, data, len);
    }
    const uint32_t t_setup = TDS, t_data = TDS + 0x10, t_status = TDS + 0x20, t_tail = TDS + 0x30;
    td(t_setup, PID_SETUP, 2, false, 7, BUFS, 8, len ? t_data : t_status);
    if (len) {
        td(t_data, in ? PID_IN : PID_OUT, 3, true, 7, BUFS + 0x100, len, t_status);
    }
    td(t_status, in ? PID_OUT : PID_IN, 3, false, 0, 0, 0, t_tail);
    td(t_tail, 0, 0, false, 7, 0, 0, 0);
    wr(ED_CTL, addr | (0u << 7) | (64u << 16));
    wr(ED_CTL + 4, t_tail);
    wr(ED_CTL + 8, t_setup);
    wr(ED_CTL + 12, 0);
    set_reg(0x20, ED_CTL);
    set_reg(0x08, CS_CLF);
    const uint32_t done = wait_done();
    if (!done) {
        return -100;
    }
    /* The done queue runs newest first: status, data, setup. */
    for (uint32_t t = done; t; t = rd(t + 8)) {
        if (td_cc(t) != CC_NOERROR) {
            return -(int)td_cc(t);
        }
    }
    if (!len) {
        return 0;
    }
    const uint32_t cbp = rd(t_data + 4);
    const int n = cbp ? (int)(cbp - (BUFS + 0x100)) : len;
    if (in) {
        memcpy(data, g_ram + BUFS + 0x100, n);
    }
    return n;
}

static bool desc_ok(const uint8_t *d, int n)
{
    return n >= 9 && d[1] == 2 && nc_rd16(d + 2) == n;
}

/* GET_DESCRIPTOR(configuration) with the data stage as one 8-byte TD per
 * packet and no buffer rounding, as XAPI sends it: the short last packet
 * halts the ED with DataUnderrun, and its CBP must count what arrived.
 * Returns the bytes the host driver would count. */
static int config_in_packets(uint8_t addr, uint16_t wlen)
{
    uint8_t *setup = g_ram + BUFS;
    const uint8_t s[8] = {0x80, 6, 0x00, 0x02, 0, 0, (uint8_t)wlen, (uint8_t)(wlen >> 8)};
    memcpy(setup, s, 8);
    const unsigned npk = (wlen + 7) / 8;
    const uint32_t t0 = TDS + 0x200;
    td(t0, PID_SETUP, 2, false, 7, BUFS, 8, t0 + 0x10);
    for (unsigned i = 0; i < npk; i++) {
        td(t0 + 0x10 * (i + 1), PID_IN, i & 1 ? 2 : 3, false, 7, BUFS + 0x100 + 8 * i, 8, t0 + 0x10 * (i + 2));
    }
    const uint32_t tail = t0 + 0x10 * (npk + 1);
    td(tail, 0, 0, false, 7, 0, 0, 0);
    wr(ED_CTL, addr | (8u << 16));
    wr(ED_CTL + 4, tail);
    wr(ED_CTL + 8, t0);
    wr(ED_CTL + 12, 0);
    set_reg(0x20, ED_CTL);
    set_reg(0x08, CS_CLF);
    const uint32_t done = wait_done();
    CHECK(rd(ED_CTL + 8) & ED_H); /* halted on the short packet */
    int total = 0;
    for (uint32_t t = done; t; t = rd(t + 8)) {
        if (t == t0) {
            continue;
        }
        const uint32_t start = BUFS + 0x100 + 8 * ((t - t0) / 0x10 - 1);
        const uint32_t cbp = rd(t + 4);
        total += cbp ? (int)(cbp - start) : 8;
        if (td_cc(t) != CC_NOERROR) {
            CHECK(td_cc(t) == CC_DATAUNDERRUN);
        }
    }
    CHECK(desc_ok(g_ram + BUFS + 0x100, total));
    /* The driver drops the rest of the data TDs and runs the status stage. */
    wr(ED_CTL + 8, 0);
    return total;
}

/* One interrupt IN transfer through the periodic list; NAK returns -1. */
static int interrupt_in(uint8_t addr, uint8_t ep, uint8_t *data, uint16_t len)
{
    const uint32_t t = TDS + 0x100, tail = TDS + 0x110;
    td(t, PID_IN, 0, true, 0, BUFS + 0x200, len, tail);
    td(tail, 0, 0, false, 7, 0, 0, 0);
    wr(ED_INT, addr | ((uint32_t)ep << 7) | (2u << 11) | (64u << 16));
    wr(ED_INT + 4, tail);
    wr(ED_INT + 8, t);
    wr(ED_INT + 12, 0);
    for (int i = 0; i < 32; i++) {
        wr(HCCA + 4 * i, ED_INT);
    }
    const uint32_t done = wait_done();
    for (int i = 0; i < 32; i++) {
        wr(HCCA + 4 * i, 0);
    }
    if (!done) {
        return -1;
    }
    CHECK(done == t);
    if (td_cc(t) != CC_NOERROR) {
        return -(int)td_cc(t);
    }
    const uint32_t cbp = rd(t + 4);
    const int n = cbp ? (int)(cbp - (BUFS + 0x200)) : len;
    memcpy(data, g_ram + BUFS + 0x200, n);
    return n;
}

/* An isochronous TD for frames sf.., a packet a frame of the sizes given, in
 * the page of buf from buf on. */
static void itd(uint32_t pa, uint16_t sf, unsigned di, uint32_t buf, const unsigned *sizes, unsigned n,
                uint32_t next)
{
    uint16_t off[8] = {0};
    uint32_t at = buf & 0xfffu;
    for (unsigned i = 0; i < n; i++) {
        off[i] = (uint16_t)(0xe000u | at);  /* CC: not accessed */
        at += sizes[i];
    }
    wr(pa, (0xfu << 28) | ((n - 1) << 24) | (di << 21) | sf);
    wr(pa + 4, buf & ~0xfffu);
    wr(pa + 8, next);
    wr(pa + 12, (buf & ~0xfffu) + at - 1);
    for (unsigned i = 0; i < 4; i++) {
        wr(pa + 16 + 4 * i, off[2 * i] | (uint32_t)off[2 * i + 1] << 16);
    }
}

static uint16_t psw(uint32_t pa, unsigned n)
{
    return nc_rd16(g_ram + pa + 16 + 2 * n);
}

/* An isochronous ED (in: IN, else OUT) from head to tail, on every frame's
 * interrupt list. */
static void iso_ed(uint8_t addr, uint8_t ep, bool in, uint32_t head, uint32_t tail)
{
    wr(ED_ISO, addr | ((uint32_t)ep << 7) | ((in ? 2u : 1u) << 11) | ED_F | (48u << 16));
    wr(ED_ISO + 4, tail);
    wr(ED_ISO + 8, head);
    wr(ED_ISO + 12, 0);
    for (int i = 0; i < 32; i++) {
        wr(HCCA + 4 * i, ED_ISO);
    }
}

/* Frames until the done queue arrives (at most max); returns it, or 0. */
static uint32_t frames_until_done(int max)
{
    for (int i = 0; i < max; i++) {
        frame(o);
        if (reg(0x0C) & INT_WDH) {
            const uint32_t d = rd(HCCA + 0x84);
            wr(HCCA + 0x84, 0);
            set_reg(0x0C, INT_WDH);
            return d & ~1u;
        }
    }
    return 0;
}

/* The microphone's noise gate on Communicator c (player 2, 16 kHz): n
 * samples of gen through it, 10 ms at a time; the loudest output sample
 * from sample `from` on. */
static int gated(NcUsbDev *c, int16_t (*gen)(unsigned), unsigned n, unsigned from)
{
    static unsigned t;          /* the signal's time, in samples */
    int loudest = 0;
    for (unsigned done = 0; done < n; done += 160) {
        int16_t s[160];
        for (unsigned i = 0; i < 160; i++) {
            s[i] = gen(t++);
        }
        nc_xblc_mic_put(1, s, 160, 16000);
        uint8_t pkt[48];
        unsigned at = done;
        for (int r; (r = c->data(c, true, 5, pkt, 48)) > 0;) {
            for (int i = 0; i + 1 < r; i += 2, at++) {
                const int v = abs((int16_t)nc_rd16(pkt + i));
                loudest = at >= from && v > loudest ? v : loudest;
            }
        }
    }
    return loudest;
}

static int16_t silence(unsigned t)
{
    return 0;
}

/* Broadband noise at -20 dBFS RMS: as loud as a voice, no pitch (a stick, a fan). */
static int16_t noise(unsigned t)
{
    static uint32_t seed = 12345;
    seed = seed * 1103515245u + 12345u;
    return (int16_t)((int)(seed >> 16 & 0x7fff) * 2 * 5676 / 0x7fff - 5676);
}

/* A voiced sound: 150 Hz and two harmonics, about -20 dBFS. */
static int16_t voice(unsigned t)
{
    const double w = 2 * M_PI * 150 * t / 16000;
    return (int16_t)(4000 * sin(w) + 2400 * sin(2 * w) + 1600 * sin(3 * w));
}

static void hub_port(uint8_t addr, uint16_t port, uint16_t feature, bool set)
{
    CHECK(control(addr, 0x23, set ? 3 : 1, feature, port, NULL, 0) == 0);
}

static uint32_t hub_port_status(uint8_t addr, uint16_t port)
{
    uint8_t st[4] = {0};
    CHECK(control(addr, 0xA3, 0, 0, port, st, 4) == 4);
    return nc_rd32(st);
}

/* Reset a hub port and give what appears at address 0 the address new_addr. */
static void enumerate_on_hub_port(uint8_t hub, uint16_t port, uint8_t new_addr, uint8_t dev_desc[18])
{
    hub_port(hub, port, 4, true); /* PORT_RESET */
    const uint32_t st = hub_port_status(hub, port);
    CHECK(st & 0x0002);           /* enabled */
    CHECK(st & (0x0010u << 16));  /* C_PORT_RESET */
    hub_port(hub, port, 20, false);
    CHECK(control(0, 0x80, 6, 0x0100, 0, dev_desc, 18) == 18);
    CHECK(control(0, 0x00, 5, new_addr, 0, NULL, 0) == 0);
    CHECK(control(new_addr, 0x00, 9, 1, 0, NULL, 0) == 0);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    g_ram = calloc(1, RAM_SIZE);
    g_vm.ram_size = RAM_SIZE;
    g_vm.phys = g_ram;
    o->vm = &g_vm;
    pthread_mutex_init(&o->lock, NULL);
    pthread_cond_init(&o->run, NULL);

    /* nc_dev_ohci_init's topology: player 1's controller on front hub port 3. */
    NcUsbDev *front = nc_usb_hub_new(4);
    NcUsbDev *pad_hub = nc_usb_hub_new(3);
    nc_usb_hub_attach(pad_hub, 1, nc_xid_gamepad_new(0));
    nc_usb_hub_attach(front, 3, pad_hub);
    o->dev[0] = front;
    soft_reset(o);
    o->ctl = 0;
    roothub_reset(o);

    /* Controller bring-up, as XAPI does it. */
    CHECK(reg(0x00) == 0x10);
    set_reg(0x08, CS_HCR);
    set_reg(0x18, HCCA);
    set_reg(0x10, INT_MIE | INT_WDH | INT_RHSC);
    set_reg(0x04, HCFS_OPERATIONAL | CTL_PLE | CTL_CLE | CTL_BLE);
    CHECK((reg(0x48) & 0xff) == ROOT_PORTS);

    /* Root port 1: the front panel hub is connected; reset enables it. */
    CHECK((reg(0x54) & (PORT_CCS | PORT_CSC)) == (PORT_CCS | PORT_CSC));
    CHECK(!(reg(0x58) & PORT_CCS));
    set_reg(0x54, PORT_CSC);
    set_reg(0x54, PORT_PRS);
    CHECK((reg(0x54) & (PORT_PES | PORT_PRSC)) == (PORT_PES | PORT_PRSC));
    set_reg(0x54, PORT_PRSC);

    uint8_t desc[64];
    CHECK(control(0, 0x80, 6, 0x0100, 0, desc, 18) == 18);
    CHECK(desc[4] == 9); /* hub class */
    CHECK(control(0, 0x00, 5, 1, 0, NULL, 0) == 0);
    CHECK(control(0, 0x80, 6, 0x0100, 0, desc, 18) == -CC_DEVICENOTRESPONDING); /* moved to address 1 */
    CHECK(control(1, 0x80, 6, 0x0200, 0, desc, 64) == 25);
    CHECK(config_in_packets(1, 0x30) == 25);
    CHECK(control(1, 0x00, 9, 1, 0, NULL, 0) == 0);
    CHECK(control(1, 0xA0, 6, 0x2900, 0, desc, 9) == 9);
    CHECK(desc[2] == 4);

    /* Ports are powered from reset: the connection on port 3 is a change. */
    for (uint16_t p = 1; p <= 4; p++) {
        hub_port(1, p, 8, true); /* PORT_POWER */
    }
    CHECK(interrupt_in(1, 1, desc, 1) == 1);
    CHECK(desc[0] == (1u << 3));
    CHECK(hub_port_status(1, 3) == (0x0101u | (1u << 16)));
    CHECK(hub_port_status(1, 1) == 0x0100u);
    hub_port(1, 3, 16, false); /* C_PORT_CONNECTION */
    CHECK(interrupt_in(1, 1, desc, 1) == -1); /* nothing changed since */

    /* The controller's own hub, then the gamepad on its port 1. */
    enumerate_on_hub_port(1, 3, 2, desc);
    CHECK(desc[4] == 9);
    for (uint16_t p = 1; p <= 3; p++) {
        hub_port(2, p, 8, true);
    }
    CHECK(hub_port_status(2, 1) & 1);
    enumerate_on_hub_port(2, 1, 3, desc);
    CHECK(nc_rd16(desc + 8) == 0x045e && nc_rd16(desc + 10) == 0x0202);

    CHECK(control(3, 0x80, 6, 0x0200, 0, desc, 64) == 32);
    CHECK(desc[14] == 0x58); /* XID interface */
    CHECK(control(3, 0xC1, 6, 0x4200, 0, desc, 16) == 16);
    CHECK(desc[1] == 0x42 && desc[4] == 1 && desc[6] == 20 && desc[7] == 6);
    CHECK(control(3, 0xC1, 1, 0x0100, 0, desc, 20) == 20);
    CHECK(desc[1] == 20 && desc[2] == 0xff);

    /* Input: a report on interrupt IN 2. */
    NcPadState pad = {.buttons = NC_PAD_START | NC_PAD_DPAD_UP, .lx = -12345};
    pad.analog[0] = 0xff;
    nc_pad_set(0, &pad);
    CHECK(interrupt_in(3, 2, desc, 20) == 20);
    CHECK(desc[1] == 20 && nc_rd16(desc + 2) == (NC_PAD_START | NC_PAD_DPAD_UP));
    CHECK(desc[4] == 0xff && (int16_t)nc_rd16(desc + 12) == -12345);
    CHECK(control(3, 0xA1, 1, 0x0100, 0, desc, 20) == 20);
    CHECK(nc_rd16(desc + 2) == (NC_PAD_START | NC_PAD_DPAD_UP));

    /* Rumble: HID SET_REPORT. */
    const uint8_t rumble[6] = {0, 6, 0x34, 0x12, 0x78, 0x56};
    CHECK(control(3, 0x21, 9, 0x0200, 0, (uint8_t *)rumble, 6) == 6);
    uint16_t l = 0, r = 0;
    nc_pad_rumble(0, &l, &r);
    CHECK(l == 0x1234 && r == 0x5678);

    /* An unknown address stalls nothing; it doesn't answer. Unknown requests stall. */
    CHECK(control(9, 0x80, 6, 0x0100, 0, desc, 18) == -CC_DEVICENOTRESPONDING);
    CHECK(control(3, 0xC1, 6, 0x4300, 0, desc, 16) == -CC_STALL);

    /* The interrupt line follows status & enable & MIE. */
    set_reg(0x0C, 0xffffffffu);
    CHECK(!g_irq_level);
    set_reg(0x10, INT_SF);
    frame(o);
    CHECK(g_irq_level);
    set_reg(0x14, INT_SF);
    CHECK(!g_irq_level);

    /* The root ports, as nc_dev_ohci_init makes them, players 1 and 2 plugged
     * in before the boot: on ports 3 and 4. */
    nc_dev_ohci_free();
    nc_dev_ohci_set_pads(3);     /* the Android front end: players 1 and 2 */
    nc_dev_ohci_init(&g_vm);
    nc_dev_ohci_stop();          /* the frames run here */
    set_reg(0x08, CS_HCR);
    set_reg(0x18, HCCA);
    set_reg(0x10, INT_MIE | INT_WDH | INT_RHSC);
    set_reg(0x04, HCFS_OPERATIONAL | CTL_PLE | CTL_CLE | CTL_BLE);
    CHECK(!(reg(0x54) & PORT_CCS) && !(reg(0x58) & PORT_CCS));
    CHECK((reg(0x5C) & (PORT_CCS | PORT_CSC)) == (PORT_CCS | PORT_CSC));
    CHECK((reg(0x60) & (PORT_CCS | PORT_CSC)) == (PORT_CCS | PORT_CSC));

    /* Player 1's controller: its hub at 1, the gamepad at 2. */
    set_reg(0x5C, PORT_CSC);
    set_reg(0x5C, PORT_PRS);
    set_reg(0x5C, PORT_PRSC);
    CHECK(control(0, 0x80, 6, 0x0100, 0, desc, 18) == 18 && desc[4] == 9);
    CHECK(control(0, 0x00, 5, 1, 0, NULL, 0) == 0);
    CHECK(control(1, 0x00, 9, 1, 0, NULL, 0) == 0);
    for (uint16_t p = 1; p <= 3; p++) {
        hub_port(1, p, 8, true);
    }
    enumerate_on_hub_port(1, 1, 2, desc);
    CHECK(nc_rd16(desc + 8) == 0x045e && nc_rd16(desc + 10) == 0x0202);
    pad = (NcPadState){.buttons = NC_PAD_BACK};
    nc_pad_set(0, &pad);
    CHECK(interrupt_in(2, 2, desc, 20) == 20 && nc_rd16(desc + 2) == NC_PAD_BACK);

    /* Pulled out: the port's connection goes, it is disabled, the guest is
     * told; what was at 1 and 2 no longer answers. */
    set_reg(0x0C, INT_RHSC);
    CHECK(!g_irq_level);
    nc_pad_plug(0, false);
    CHECK((reg(0x5C) & (PORT_CCS | PORT_PES | PORT_CSC | PORT_PESC)) == (PORT_CSC | PORT_PESC));
    CHECK((reg(0x0C) & INT_RHSC) && g_irq_level);
    CHECK(control(2, 0x80, 6, 0x0100, 0, desc, 18) == -CC_DEVICENOTRESPONDING);
    CHECK(control(1, 0x80, 6, 0x0100, 0, desc, 18) == -CC_DEVICENOTRESPONDING);
    set_reg(0x5C, PORT_CSC | PORT_PESC);
    set_reg(0x0C, INT_RHSC);
    nc_pad_plug(0, false);       /* already out: nothing changes */
    CHECK(!(reg(0x0C) & INT_RHSC) && !(reg(0x5C) & PORT_WTC));

    /* Plugged in again: a fresh connection, enumerated from address 0. */
    nc_pad_plug(0, true);
    CHECK((reg(0x5C) & (PORT_CCS | PORT_PES | PORT_CSC)) == (PORT_CCS | PORT_CSC));
    CHECK(reg(0x0C) & INT_RHSC);
    set_reg(0x5C, PORT_CSC);
    set_reg(0x5C, PORT_PRS);
    CHECK(control(0, 0x80, 6, 0x0100, 0, desc, 18) == 18 && desc[4] == 9);
    CHECK(control(0, 0x00, 5, 4, 0, NULL, 0) == 0);
    CHECK(control(4, 0x00, 9, 1, 0, NULL, 0) == 0);
    for (uint16_t p = 1; p <= 3; p++) {
        hub_port(4, p, 8, true);
    }
    CHECK(hub_port_status(4, 1) & (1u << 16));      /* the gamepad: a connection to see */
    CHECK(control(2, 0x80, 6, 0x0100, 0, desc, 18) == -CC_DEVICENOTRESPONDING);  /* not enabled yet */
    enumerate_on_hub_port(4, 1, 5, desc);
    CHECK(nc_rd16(desc + 8) == 0x045e);
    CHECK(interrupt_in(5, 2, desc, 20) == 20 && nc_rd16(desc + 2) == NC_PAD_BACK);

    /* Player 3 plugged in while the console runs: root port 1. */
    nc_dev_ohci_set_pads(7);     /* the mask API sees nc_pad_plug's changes too */
    CHECK((reg(0x54) & (PORT_CCS | PORT_CSC)) == (PORT_CCS | PORT_CSC));

    /* The players plugged in stay so for the next boot: 1, 2 and 3. */
    nc_dev_ohci_free();
    nc_dev_ohci_init(&g_vm);
    nc_dev_ohci_stop();
    CHECK(o->dev[2] && o->dev[3] && o->dev[0] && !o->dev[1]);
    nc_dev_ohci_free();

    /* Player 1's controller with a Communicator in expansion slot A (hub port
     * 2), its noise gate off: the samples as they are. */
    uint64_t packets = 1;
    CHECK(nc_xblc_state(0, &packets) == 0 && packets == 0);
    nc_dev_ohci_set_pads(1);
    nc_dev_ohci_set_voice(1);
    nc_xblc_set_gate("off");
    nc_dev_ohci_init(&g_vm);
    nc_dev_ohci_stop();
    set_reg(0x08, CS_HCR);
    set_reg(0x18, HCCA);
    set_reg(0x10, INT_MIE | INT_WDH | INT_RHSC);
    set_reg(0x04, HCFS_OPERATIONAL | CTL_PLE | CTL_IE | CTL_CLE | CTL_BLE);
    set_reg(0x5C, PORT_CSC);
    set_reg(0x5C, PORT_PRS);
    set_reg(0x5C, PORT_PRSC);
    CHECK(control(0, 0x00, 5, 1, 0, NULL, 0) == 0);
    CHECK(control(1, 0x00, 9, 1, 0, NULL, 0) == 0);
    for (uint16_t p = 1; p <= 3; p++) {
        hub_port(1, p, 8, true);
    }
    CHECK((hub_port_status(1, 2) & 1) && !(hub_port_status(1, 3) & 1));    /* slot B empty */
    enumerate_on_hub_port(1, 1, 2, desc);
    CHECK(nc_rd16(desc + 10) == 0x0202);
    enumerate_on_hub_port(1, 2, 3, desc);
    CHECK(nc_rd16(desc + 8) == 0x045e && nc_rd16(desc + 10) == 0x0283);
    CHECK(control(3, 0x80, 6, 0x0200, 0, desc, 64) == 45);
    CHECK(desc[14] == 0x78 && desc[20] == 0x04 && desc[21] == 0x01);       /* headphone: isochronous OUT 4 */
    CHECK(desc[32] == 0x78 && desc[38] == 0x85 && desc[39] == 0x01);       /* microphone: isochronous IN 5 */
    CHECK(control(3, 0x41, 3, 2, 0, NULL, 0) == 0);         /* SET_FEATURE: 16 kHz */
    CHECK(control(3, 0x41, 3, 1, 1, NULL, 0) == 0);         /* AGC */
    CHECK(control(3, 0x41, 3, 9, 0, NULL, 0) == -CC_STALL); /* no such rate */
    CHECK(nc_xblc_state(0, &packets) == 16000 && packets == 0);

    /* The headphone: six TDs of eight 1 ms packets (16 samples each, 48 ms),
     * chained, only the last asking for the interrupt. They retire as their
     * last frame goes, newest first on the done queue. */
    const unsigned out_pkts[8] = {32, 32, 32, 32, 32, 32, 32, 32};
    uint16_t sf = (uint16_t)(o->frame_number + 1);
    for (unsigned k = 0; k < 6; k++) {
        itd(ITDS + 0x20 * k, (uint16_t)(sf + 8 * k), k == 5 ? 0 : 7, BUFS + 0x1000 + 0x100 * k, out_pkts, 8,
            ITDS + 0x20 * (k + 1));
    }
    for (unsigned i = 0; i < 6 * 8 * 16; i++) {
        nc_wr16(g_ram + BUFS + 0x1000 + 2 * i, (uint16_t)(1000 + i));
    }
    iso_ed(3, 4, false, ITDS, ITDS + 0xC0);
    uint32_t done = frames_until_done(60);
    CHECK(done == ITDS + 0xA0);
    for (unsigned k = 6; k-- > 0 && done;) {
        CHECK(done == ITDS + 0x20 * k && td_cc(done) == CC_NOERROR);
        for (unsigned i = 0; i < 8; i++) {
            CHECK(psw(done, i) == 0);                       /* NoError; an OUT's size is 0 */
        }
        done = rd(done + 8);
    }
    CHECK((rd(ED_ISO + 8) & PTR_MASK) == ITDS + 0xC0 && !(rd(ED_ISO + 8) & ED_H));
    CHECK(nc_xblc_state(0, &packets) == 16000 && packets == 0);    /* the headphone's don't count */
    static int16_t heard[768];
    CHECK(nc_xblc_phone_read(0, heard, 768, 16000) == 768);
    int wrong = 0;
    for (unsigned i = 0; i < 768; i++) {
        wrong += heard[i] != (int16_t)(1000 + i);
    }
    CHECK(wrong == 0);
    CHECK(nc_xblc_phone_read(0, heard, 16, 16000) == 16 && heard[0] == 0 && heard[15] == 0);   /* run dry */
    CHECK(nc_xblc_phone_read(3, heard, 16, 16000) == 0);   /* player 4 has none */

    /* The microphone: 40 samples queued; three 48-byte packets take 24, 16
     * and none. */
    int16_t said[40];
    for (unsigned i = 0; i < 40; i++) {
        said[i] = (int16_t)(-2000 - (int)i);
    }
    nc_xblc_mic_put(0, said, 40, 16000);
    const unsigned in_pkts[3] = {48, 48, 48};
    itd(ITDS, (uint16_t)(o->frame_number + 1), 0, BUFS + 0x2000, in_pkts, 3, ITDS + 0x20);
    memset(g_ram + BUFS + 0x2000, 0x55, 144);
    iso_ed(3, 5, true, ITDS, ITDS + 0x20);
    CHECK(frames_until_done(8) == ITDS && td_cc(ITDS) == CC_NOERROR);
    CHECK(psw(ITDS, 0) == 48 && psw(ITDS, 1) == 32 && psw(ITDS, 2) == 0);
    CHECK(nc_xblc_state(0, &packets) == 16000 && packets == 3);     /* the microphone's do, empty ones too */
    wrong = 0;
    for (unsigned i = 0; i < 40; i++) {
        wrong += (int16_t)nc_rd16(g_ram + BUFS + 0x2000 + (i < 24 ? 2 * i : 48 + 2 * (i - 24))) != said[i];
    }
    CHECK(wrong == 0);

    /* A TD whose frames went by retires with DataOverrun, its packet not
     * accessed; one for a later frame waits for it. */
    const unsigned one[1] = {32};
    itd(ITDS, (uint16_t)(o->frame_number - 10), 0, BUFS + 0x2000, one, 1, ITDS + 0x20);
    iso_ed(3, 4, false, ITDS, ITDS + 0x20);
    CHECK(frames_until_done(2) == ITDS && td_cc(ITDS) == CC_DATAOVERRUN && (psw(ITDS, 0) >> 12) == 0xe);
    itd(ITDS, (uint16_t)(o->frame_number + 4), 0, BUFS + 0x2000, one, 1, ITDS + 0x20);
    iso_ed(3, 4, false, ITDS, ITDS + 0x20);
    CHECK(!frames_until_done(3) && (rd(ED_ISO + 8) & PTR_MASK) == ITDS && (psw(ITDS, 0) >> 12) == 0xe);
    CHECK(frames_until_done(1) == ITDS && psw(ITDS, 0) == 0);

    /* Nobody at the address: the packet's status says so. */
    itd(ITDS, (uint16_t)(o->frame_number + 1), 0, BUFS + 0x2000, one, 1, ITDS + 0x20);
    iso_ed(9, 4, false, ITDS, ITDS + 0x20);
    CHECK(frames_until_done(2) == ITDS && psw(ITDS, 0) >> 12 == CC_DEVICENOTRESPONDING);

    /* With HcControl.IE clear the periodic list stops at the isochronous ED.
     * The gamepad's interrupt ED before it still runs. The microphone's TD
     * waits and its buffer isn't written. Once IE is set again, the TD's
     * frame has passed and it retires with DataOverrun. */
    set_reg(0x04, HCFS_OPERATIONAL | CTL_PLE | CTL_CLE | CTL_BLE);
    itd(ITDS, (uint16_t)(o->frame_number + 1), 0, BUFS + 0x2000, in_pkts, 1, ITDS + 0x20);
    memset(g_ram + BUFS + 0x2000, 0x55, 48);
    iso_ed(3, 5, true, ITDS, ITDS + 0x20);
    td(TDS + 0x100, PID_IN, 0, true, 0, BUFS + 0x200, 20, TDS + 0x110);
    td(TDS + 0x110, 0, 0, false, 7, 0, 0, 0);
    wr(ED_INT, 2 | (2u << 7) | (2u << 11) | (64u << 16));
    wr(ED_INT + 4, TDS + 0x110);
    wr(ED_INT + 8, TDS + 0x100);
    wr(ED_INT + 12, ED_ISO);
    for (int i = 0; i < 32; i++) {
        wr(HCCA + 4 * i, ED_INT);
    }
    CHECK(frames_until_done(3) == TDS + 0x100 && td_cc(TDS + 0x100) == CC_NOERROR);
    CHECK(!frames_until_done(3));
    CHECK((rd(ED_ISO + 8) & PTR_MASK) == ITDS && (psw(ITDS, 0) >> 12) == 0xe && g_ram[BUFS + 0x2000] == 0x55);
    set_reg(0x04, HCFS_OPERATIONAL | CTL_PLE | CTL_IE | CTL_CLE | CTL_BLE);
    CHECK(frames_until_done(1) == ITDS && td_cc(ITDS) == CC_DATAOVERRUN && g_ram[BUFS + 0x2000] == 0x55);
    for (int i = 0; i < 32; i++) {
        wr(HCCA + 4 * i, 0);
    }

    /* The gamepad beside it still reports. */
    pad = (NcPadState){.buttons = NC_PAD_START};
    nc_pad_set(0, &pad);
    CHECK(interrupt_in(2, 2, desc, 20) == 20 && nc_rd16(desc + 2) == NC_PAD_START);

    /* Freed with the console: the front end finds none. */
    nc_dev_ohci_free();
    CHECK(nc_xblc_state(0, &packets) == 0);
    nc_dev_ohci_set_voice(0);

    /* The noise gate (-38 dBFS): silence, and a loud sound without a pitch,
     * stay silent; a voice opens it, and it closes 400 ms after the voice
     * ends (and a 30 ms fade). Under a -10 dBFS threshold the voice is too
     * quiet. */
    nc_xblc_set_gate(NULL);
    NcUsbDev *c = nc_xblc_new(1);
    int loud[6];
    loud[0] = gated(c, silence, 16000, 0);
    loud[1] = gated(c, noise, 16000, 0);
    loud[2] = gated(c, silence, 8000, 0);
    loud[3] = gated(c, voice, 8000, 4000);
    loud[4] = gated(c, noise, 8000, 0);         /* the hold (and the voice's last 30 ms): the noise passes */
    loud[5] = gated(c, noise, 16000, 0);        /* closed again */
    nc_usb_dev_free(c);
    printf("  gate's loudest out: silence %d, noise %d, voice %d, noise after it %d then %d\n", loud[0], loud[1],
           loud[3], loud[4], loud[5]);
    CHECK(!loud[0] && !loud[1] && !loud[2] && loud[3] > 2000 && loud[4] > 2000 && !loud[5]);
    nc_xblc_set_gate("-10");
    c = nc_xblc_new(1);
    CHECK(gated(c, silence, 16000, 0) == 0);
    CHECK(gated(c, voice, 8000, 0) == 0);
    nc_usb_dev_free(c);
    nc_xblc_set_gate(NULL);

    printf(g_fail ? "usb_test: %d FAILED\n" : "usb_test: all passed\n", g_fail);
    return g_fail != 0;
}
