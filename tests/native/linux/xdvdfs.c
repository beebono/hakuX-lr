/* Copyright (C) 2026-present PortareOS */
/*
 * Host test of the disc's directory search (native/core/xdvdfs.c) on a
 * synthetic XISO: a root whose tree is one 80-entry chain (deeper than the
 * 64 levels the search once stopped at; Conker's dvddata\Audio is 77), and
 * a tree that loops back on itself.
 */
#include "native/core/xdvdfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SECTOR NC_XDVDFS_SECTOR
#define CHAIN  80

static int g_fail;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    g_fail += !ok;
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    wr16(p, (uint16_t)v);
    wr16(p + 2, (uint16_t)(v >> 16));
}

/* An XISO of 35 sectors: the volume descriptor at 32, the root at 33 (one
 * chain of CHAIN files and LOOP, each the right child of the one before),
 * LOOP's looping tree at 34. */
static int image(void)
{
    char path[] = "/tmp/xdvdfs-test-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) {
        return -1;
    }
    unlink(path);
    static uint8_t img[35 * SECTOR];
    memset(img, 0, sizeof(img));
    uint8_t *vd = img + 32 * SECTOR;
    memcpy(vd, "MICROSOFT*XBOX*MEDIA", 20);
    memcpy(vd + 0x7EC, "MICROSOFT*XBOX*MEDIA", 20);
    wr32(vd + 20, 33);
    wr32(vd + 24, SECTOR);
    uint8_t *root = img + 33 * SECTOR;
    memset(root, 0xFF, SECTOR);         /* padding after the entries */
    for (unsigned i = 0; i < CHAIN; i++) {
        uint8_t *e = root + i * 20;     /* 14 + a 3-byte name, 4-aligned */
        wr16(e, 0);
        wr16(e + 2, i + 1 < CHAIN ? (uint16_t)((i + 1) * 20 / 4) : 0);
        wr32(e + 4, 1000 + i);
        wr32(e + 8, i);
        e[12] = 0x20;
        e[13] = 3;
        char name[4];
        snprintf(name, sizeof(name), "F%02u", i);
        memcpy(e + 14, name, 3);
    }
    /* the chain's end: LOOP, a directory whose tree loops (AAA's right
     * child BBB is its own left child) */
    wr16(root + (CHAIN - 1) * 20 + 2, (uint16_t)(CHAIN * 20 / 4));
    uint8_t *e = root + CHAIN * 20;
    wr32(e + 4, 34);
    wr32(e + 8, SECTOR);
    e[12] = 0x10;
    e[13] = 4;
    memcpy(e + 14, "LOOP", 4);
    uint8_t *loop = img + 34 * SECTOR;
    memset(loop, 0xFF, SECTOR);
    for (unsigned k = 0; k < 2; k++) {
        uint8_t *x = loop + k * 20;
        wr16(x, k ? 20 / 4 : 0);
        wr16(x + 2, k ? 0 : 20 / 4);
        wr32(x + 4, 2000 + k);
        wr32(x + 8, 1);
        x[12] = 0x20;
        x[13] = 3;
        memcpy(x + 14, k ? "BBB" : "AAA", 3);
    }
    if (write(fd, img, sizeof(img)) != (ssize_t)sizeof(img)) {
        close(fd);
        return -1;
    }
    return fd;
}

int main(void)
{
    int fd = image();
    NcDisc d;
    if (fd < 0 || !nc_disc_open(&d, fd)) {
        printf("FAIL the image\n");
        return 1;
    }
    NcDiscFile f;
    check(nc_disc_find(&d, "F00", &f) && f.sector == 1000, "the chain's first entry");
    check(nc_disc_find(&d, "\\f40", &f) && f.sector == 1040, "its 41st, any case");
    check(nc_disc_find(&d, "F79", &f) && f.sector == 1079 && f.size == 79, "its 80th, 79 deep");
    check(nc_disc_find(&d, "LOOP\\BBB", &f) && f.sector == 2001, "a file in the looping tree");
    check(!nc_disc_find(&d, "LOOP\\CCC", &f), "a missing one there: not found, and the search ends");
    check(!nc_disc_find(&d, "F80", &f), "a missing one in the root");
    nc_disc_close(&d);
    printf("%s\n", g_fail ? "FAILED" : "all passed");
    return g_fail != 0;
}
