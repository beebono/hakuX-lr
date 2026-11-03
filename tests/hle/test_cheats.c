/*
 * hakuX native core — the cheat engine (native/core/cheats.c) over a fake
 * guest memory: parsing, writes, conditionals, pointers, once, toggles, a
 * cheat compiled while the game runs, the frontend's cheats.
 */
#include "hle_test.h"

#include "native/core/cheats.h"

#include <pthread.h>

/* 64 KiB of "guest memory" at VA 0x10000; anything else is unmapped */
#define BASE 0x10000u
static uint8_t g_mem[0x10000];
static unsigned g_writes;

static bool rd(void *o, uint32_t va, void *buf, unsigned n)
{
    if (va < BASE || va + n > BASE + sizeof(g_mem)) {
        return false;
    }
    memcpy(buf, g_mem + (va - BASE), n);
    return true;
}

static bool wr(void *o, uint32_t va, const void *buf, unsigned n)
{
    if (va < BASE || va + n > BASE + sizeof(g_mem)) {
        return false;
    }
    memcpy(g_mem + (va - BASE), buf, n);
    g_writes++;
    return true;
}

static const NcCheatMem k_mem = {NULL, rd, wr};

static uint32_t at32(uint32_t va)
{
    uint32_t v;
    memcpy(&v, g_mem + (va - BASE), 4);
    return v;
}

static void put32(uint32_t va, uint32_t v)
{
    memcpy(g_mem + (va - BASE), &v, 4);
}

static bool line(const char *l)
{
    char err[64];
    const bool ok = nc_cheat_line(l, err, sizeof(err));
    if (!ok) {
        fprintf(stderr, "  \"%s\": %s\n", l, err);
    }
    return ok;
}

static void test_parse(void)
{
    nc_cheats_reset();
    HT_CHECK(nc_cheat_add("p", true, false) == 0);
    HT_CHECK(line("w8 0x10000 0xFF"));
    HT_CHECK(line("  w16 0x10002 -1   # a comment"));
    HT_CHECK(line("W32 0x10004 4294967295"));
    HT_CHECK(line("wf 0x10008 1.5"));
    HT_CHECK(line("if32 [0x10010]+0x10 >= 3"));
    HT_CHECK(line("else"));
    HT_CHECK(line("end"));
    HT_CHECK(line("iff [[0x10010] - 4]+8 < -2.25"));
    HT_CHECK(line("end"));
    HT_CHECK(line(""));
    HT_CHECK(line("# only a comment"));
    HT_CHECK(!line("w8 0x10000 256"));         /* doesn't fit */
    HT_CHECK(!line("w8 0x10000 -129"));
    HT_CHECK(!line("w64 0x10000 1"));
    HT_CHECK(!line("x8 0x10000 1"));
    HT_CHECK(!line("w8 [0x10000 1"));          /* no ] */
    HT_CHECK(!line("if8 0x10000 =~ 1"));
    HT_CHECK(!line("iff 0x10000 & 1.0"));
    HT_CHECK(!line("end"));                    /* nothing open */
    HT_CHECK(!line("w8 0x10000 1 2"));
    HT_CHECK(!line("w8 [[[[[0x10000]]]]] 1"));  /* five pointers */
}

static void test_writes(void)
{
    nc_cheats_reset();
    memset(g_mem, 0x11, sizeof(g_mem));
    nc_cheat_add("w", true, false);
    line("w8 0x10000 0xAB");
    line("w16 0x10002 0x1234");
    line("w32 0x10004 0xDEADBEEF");
    line("wf 0x10008 2.0");
    line("w32 0x90000 1");                     /* unmapped: skipped */
    nc_cheats_run(&k_mem);
    HT_EQ_U32(g_mem[0], 0xAB);
    HT_EQ_U32(g_mem[1], 0x11);                 /* only the 8 bits */
    HT_EQ_U32(g_mem[2] | g_mem[3] << 8, 0x1234);
    HT_EQ_U32(at32(0x10004), 0xDEADBEEF);
    HT_EQ_U32(at32(0x10008), 0x40000000);
    HT_EQ_U32(nc_cheat_misses(0), 1);
    /* the same values again: nothing written */
    g_writes = 0;
    HT_EQ_U32(nc_cheats_run(&k_mem), 0);
    HT_EQ_U32(g_writes, 0);
    /* the game changes one: written back */
    put32(0x10004, 5);
    HT_EQ_U32(nc_cheats_run(&k_mem), 4);
    HT_EQ_U32(at32(0x10004), 0xDEADBEEF);
}

static void test_ifs(void)
{
    nc_cheats_reset();
    memset(g_mem, 0, sizeof(g_mem));
    nc_cheat_add("if", true, false);
    line("if16 0x10000 == 7");
    line("  w8 0x10010 1");
    line("  if8 0x10002 & 0x0C");
    line("    w8 0x10011 2");
    line("  else");
    line("    w8 0x10011 3");
    line("  end");
    line("else");
    line("  w8 0x10012 4");
    line("end");
    line("w8 0x10013 5");
    nc_cheats_run(&k_mem);                     /* 0 != 7: the else */
    HT_EQ_U32(g_mem[0x10], 0);
    HT_EQ_U32(g_mem[0x11], 0);
    HT_EQ_U32(g_mem[0x12], 4);
    HT_EQ_U32(g_mem[0x13], 5);
    memset(g_mem, 0, 0x20);
    g_mem[0] = 7;
    g_mem[2] = 0x0D;                           /* bits 2 and 3 set */
    nc_cheats_run(&k_mem);
    HT_EQ_U32(g_mem[0x10], 1);
    HT_EQ_U32(g_mem[0x11], 2);
    HT_EQ_U32(g_mem[0x12], 0);
    HT_EQ_U32(g_mem[0x13], 5);
    g_mem[2] = 0x04;                           /* only bit 2: the inner else */
    nc_cheats_run(&k_mem);
    HT_EQ_U32(g_mem[0x11], 3);
    /* an unmapped if is false, its else runs */
    nc_cheats_reset();
    nc_cheat_add("gone", true, false);
    line("if32 0x90000 == 0");
    line("w8 0x10020 1");
    line("else");
    line("w8 0x10021 1");
    line("end");
    nc_cheats_run(&k_mem);
    HT_EQ_U32(g_mem[0x20], 0);
    HT_EQ_U32(g_mem[0x21], 1);
    /* floats compare as floats */
    nc_cheats_reset();
    float f = -3.0f;
    memcpy(g_mem + 0x30, &f, 4);
    nc_cheat_add("f", true, false);
    line("iff 0x10030 < -2.5");
    line("wf 0x10030 100");
    line("end");
    nc_cheats_run(&k_mem);
    memcpy(&f, g_mem + 0x30, 4);
    HT_CHECK(f == 100.0f);
}

static void test_pointers(void)
{
    nc_cheats_reset();
    memset(g_mem, 0, sizeof(g_mem));
    put32(0x10100, 0x10200);                   /* a pointer to a struct */
    put32(0x10204, 0x10300);                   /* ... holding another */
    nc_cheat_add("ptr", true, false);
    line("w32 [0x10100]+0x10 0x1111");
    line("w32 [[0x10100]+4]+8 0x2222");
    line("w32 [0x10400]+4 0x3333");            /* a NULL pointer: skipped */
    nc_cheats_run(&k_mem);
    HT_EQ_U32(at32(0x10210), 0x1111);
    HT_EQ_U32(at32(0x10308), 0x2222);
    HT_EQ_U32(at32(0x10004), 0);
    HT_EQ_U32(nc_cheat_misses(0), 1);
    /* the struct moved: the cheat follows it */
    put32(0x10100, 0x10500);
    nc_cheats_run(&k_mem);
    HT_EQ_U32(at32(0x10510), 0x1111);
}

static void test_toggles_and_once(void)
{
    nc_cheats_reset();
    memset(g_mem, 0, sizeof(g_mem));
    HT_EQ_INT(nc_cheat_add("off", false, false), 0);
    line("w8 0x10000 1");
    HT_EQ_INT(nc_cheat_add("once", true, true), 1);
    line("w8 0x10001 1");
    HT_EQ_U32(nc_cheat_count(), 2);
    HT_EQ_STR(nc_cheat_name(1), "once");
    nc_cheats_run(&k_mem);
    HT_EQ_U32(g_mem[0], 0);                    /* off */
    HT_EQ_U32(g_mem[1], 1);
    g_mem[1] = 0;
    nc_cheats_run(&k_mem);
    HT_EQ_U32(g_mem[1], 0);                    /* once: not again */
    nc_cheat_set(1, false);
    nc_cheat_set(1, true);
    nc_cheats_run(&k_mem);
    HT_EQ_U32(g_mem[1], 1);                    /* turned on again: once more */
    nc_cheat_set(0, true);
    HT_CHECK(nc_cheat_on(0));
    nc_cheats_run(&k_mem);
    HT_EQ_U32(g_mem[0], 1);
    nc_cheat_set(0, false);
    g_mem[0] = 9;
    nc_cheats_run(&k_mem);
    HT_EQ_U32(g_mem[0], 9);
    /* a once cheat whose memory isn't there yet tries again */
    nc_cheats_reset();
    nc_cheat_add("later", true, true);
    line("w8 [0x10040] 7");
    nc_cheats_run(&k_mem);
    put32(0x10040, 0x10050);
    nc_cheats_run(&k_mem);
    HT_EQ_U32(g_mem[0x50], 7);
}

static void test_compile(void)
{
    char err[96];
    nc_cheats_reset();
    memset(g_mem, 0, sizeof(g_mem));
    nc_cheat_add("boot", true, false);
    line("w8 0x10000 1");
    HT_EQ_INT(nc_cheat_compile("later", true, false, "w8 0x10001 2\r\nif8 0x10000 == 1\n  w8 0x10002 3\nend\n", false,
                               err, sizeof(err)), 1);
    HT_EQ_U32(nc_cheat_count(), 2);
    HT_EQ_STR(nc_cheat_name(1), "later");
    nc_cheats_run(&k_mem);
    HT_EQ_U32(g_mem[0], 1);
    HT_EQ_U32(g_mem[1], 2);
    HT_EQ_U32(g_mem[2], 3);
    /* only checked, or refused: nothing added */
    HT_EQ_INT(nc_cheat_compile("check", true, false, "w8 0x10003 4", true, err, sizeof(err)), 0);
    HT_EQ_INT(nc_cheat_compile("bad", true, false, "w8 0x10003 4\nw8 [0x10000 1", false, err, sizeof(err)), -1);
    HT_EQ_STR(err, "line 2: bad address");
    HT_EQ_INT(nc_cheat_compile("open", true, false, "if8 0x10000 == 1", false, err, sizeof(err)), -1);
    HT_EQ_STR(err, "an if without its end");
    HT_EQ_INT(nc_cheat_compile("none", true, false, "# a comment\n", false, err, sizeof(err)), -1);
    HT_EQ_STR(err, "no code lines");
    HT_EQ_U32(nc_cheat_count(), 2);
    nc_cheats_run(&k_mem);
    HT_EQ_U32(g_mem[3], 0);
    /* the next one's ops where the refused ones were */
    HT_EQ_INT(nc_cheat_compile("off", false, false, "w8 0x10004 5", false, err, sizeof(err)), 2);
    nc_cheats_run(&k_mem);
    HT_EQ_U32(g_mem[4], 0);
    nc_cheat_set(2, true);
    nc_cheats_run(&k_mem);
    HT_EQ_U32(g_mem[4], 5);
    HT_EQ_U32(g_mem[2], 3);
}

static bool front(unsigned index, const char *code)
{
    char err[96];
    const bool ok = nc_cheat_front_set(index, true, code, err, sizeof(err));
    if (!ok) {
        fprintf(stderr, "  frontend \"%s\": %s\n", code, err);
    }
    return ok;
}

/* why the frontend's code is refused ("" when it isn't) */
static const char *front_err(unsigned index, const char *code)
{
    static char err[96];
    err[0] = 0;
    return nc_cheat_front_set(index, true, code, err, sizeof(err)) ? "" : err;
}

static void test_front(void)
{
    nc_cheats_reset();
    nc_cheat_front_reset();
    memset(g_mem, 0, sizeof(g_mem));
    put32(0x10100, 0x10200);
    HT_CHECK(front(0, "w8 0x10000 0xAB+w16 0x10002 0x1234"));
    HT_CHECK(front(1, "00010004 DEADBEEF 00010008 0000002a"));
    HT_CHECK(front(2, "if8 0x10010 == 0\n  w8 0x10011 5 # five\r\nelse+w8 0x10011 6\nend"));
    HT_CHECK(front(3, "w32 [0x10100]+0x10 7 + 00010020 00000002+"));
    HT_EQ_U32(nc_cheat_count(), 0);            /* the manifest's set has none */
    nc_cheats_run(&k_mem);
    HT_EQ_U32(g_mem[0], 0xAB);
    HT_EQ_U32(g_mem[2] | g_mem[3] << 8, 0x1234);
    HT_EQ_U32(at32(0x10004), 0xDEADBEEF);
    HT_EQ_U32(at32(0x10008), 0x2A);
    HT_EQ_U32(g_mem[0x11], 5);
    HT_EQ_U32(at32(0x10210), 7);
    HT_EQ_U32(at32(0x10020), 2);
    /* refused */
    HT_EQ_STR(front_err(4, "w8 0x10000"), "code 1: bad value");
    HT_EQ_STR(front_err(4, "w8 0x10000 1 + x8 0x10000 1"), "code 2: unknown code");
    HT_EQ_STR(front_err(4, "0001000 00000001"), "code 1: unknown code");     /* 7 digits */
    HT_EQ_STR(front_err(4, "00010000 000000001"), "code 1: unknown code");   /* 9 */
    HT_EQ_STR(front_err(4, "w8 0x10000 1x"), "code 1: extra text");
    HT_EQ_STR(front_err(4, "if8 0x10000 == 1 + w8 0x10001 1"), "an if without its end");
    HT_EQ_STR(front_err(4, "w8 0x10000 1 end"), "code 2: no if to close");
    HT_EQ_STR(front_err(4, ""), "no codes");
    HT_EQ_STR(front_err(4, " + \n# nothing"), "no codes");
    HT_EQ_STR(front_err(256, "w8 0x10000 1"), "too many cheats");
    /* a refused code, or off, leaves its index off; another code replaces it */
    memset(g_mem, 0, 0x30);
    HT_CHECK(*front_err(0, "w8"));
    HT_CHECK(nc_cheat_front_set(1, false, "not a code", NULL, 0));
    HT_CHECK(front(3, "w8 0x10030 9"));
    put32(0x10210, 0);
    nc_cheats_run(&k_mem);
    HT_EQ_U32(g_mem[0], 0);
    HT_EQ_U32(at32(0x10004), 0);
    HT_EQ_U32(g_mem[0x11], 5);
    HT_EQ_U32(g_mem[0x30], 9);
    HT_EQ_U32(at32(0x10210), 0);
    /* a stop keeps them; their own reset doesn't */
    nc_cheats_reset();
    g_mem[0x30] = 0;
    nc_cheats_run(&k_mem);
    HT_EQ_U32(g_mem[0x30], 9);
    nc_cheat_front_reset();
    g_mem[0x30] = 0;
    nc_cheats_run(&k_mem);
    HT_EQ_U32(g_mem[0x30], 0);
    /* an unmapped address: said once, and again after a stop */
    HT_CHECK(front(5, "w8 0x90000 1"));
    HT_EQ_INT(nc_cheat_front_missed(), -1);
    nc_cheats_run(&k_mem);
    HT_EQ_INT(nc_cheat_front_missed(), 5);
    nc_cheats_run(&k_mem);
    HT_EQ_INT(nc_cheat_front_missed(), -1);
    nc_cheats_reset();
    nc_cheats_run(&k_mem);
    HT_EQ_INT(nc_cheat_front_missed(), 5);
    /* beside the manifest's */
    nc_cheat_front_reset();
    nc_cheat_add("m", true, false);
    line("w8 0x10040 1");
    HT_CHECK(front(0, "w8 0x10041 2"));
    nc_cheats_run(&k_mem);
    HT_EQ_U32(g_mem[0x40], 1);
    HT_EQ_U32(g_mem[0x41], 2);
    nc_cheats_reset();
    nc_cheat_front_reset();
}

/* The guest's thread runs the cheats while another changes the frontend's. */
static int g_stop;

static void *runner(void *arg)
{
    while (!__atomic_load_n(&g_stop, __ATOMIC_ACQUIRE)) {
        nc_cheats_run(&k_mem);
    }
    return arg;
}

static void test_front_threads(void)
{
    nc_cheats_reset();
    nc_cheat_front_reset();
    memset(g_mem, 0, sizeof(g_mem));
    nc_cheat_add("m", true, false);
    line("w8 0x10050 1");
    pthread_t th;
    HT_CHECK(pthread_create(&th, NULL, runner, NULL) == 0);
    char err[96];
    for (unsigned i = 0; i < 20000; i++) {
        nc_cheat_front_reset();
        nc_cheat_front_set(0, true, "w32 0x10060 1 + w32 0x10064 2", err, sizeof(err));
        nc_cheat_front_set(i % 7, true, "00010068 00000003", err, sizeof(err));
        nc_cheat_front_set(3, i & 1, "if8 0x10050 == 1 + w8 0x1006C 4 + end", err, sizeof(err));
        nc_cheat_set(0, i & 1);
    }
    __atomic_store_n(&g_stop, 1, __ATOMIC_RELEASE);
    pthread_join(th, NULL);
    nc_cheats_run(&k_mem);
    HT_EQ_U32(at32(0x10060), 1);
    HT_EQ_U32(at32(0x10064), 2);
    HT_EQ_U32(at32(0x10068), 3);
    nc_cheats_reset();
    nc_cheat_front_reset();
}

int main(void)
{
    test_parse();
    test_writes();
    test_ifs();
    test_pointers();
    test_toggles_and_once();
    test_compile();
    test_front();
    test_front_threads();
    return ht_failures ? 1 : 0;
}
