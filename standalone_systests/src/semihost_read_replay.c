/*
 * Exercise semihosting SYS_READ into buffers whose pages are mapped with
 * various page sizes, and which may not be (fully) present in the TLB when
 * the trap is taken.
 *
 * The destination buffers live in a window that is only ever mapped on
 * demand by this test's own TLB-miss handler, using a selectable page size
 * and a selectable number of TLB entries.
 *
 * hexagon-sim has two different SYS_READ behaviours, so the test has two
 * modes.  Neither ever raises a TLB miss from the trap on hexagon-sim.
 *
 * Default mode, for hexagon-sim without an OS awareness model:
 *     hexagon-sim stores the data at PA == VA without consulting the TLB.
 *     The window is mapped VA == PA and is partly or wholly missing from
 *     the TLB when the trap is taken.  An implementation that resolves
 *     those misses by raising the exception and replaying the trap has to
 *     get through a handler whose tlbw can evict pages it already resolved,
 *     and must still consume the host stream exactly once and store all of
 *     it.
 *
 * "mapped" mode (first argument), for hexagon-sim --rtos <osam.cfg>:
 *     hexagon-sim translates the buffer through the TLB, and silently
 *     drops the bytes that belong on a page with no TLB entry.  Only fully
 *     mapped buffers have a useful reference result, so every page is put
 *     in the TLB before the trap, cases needing more pages than they have
 *     TLB entries are skipped, and the read must not cause a TLB miss.
 *     The window is mapped VA != PA to check the translation is honoured.
 *
 * The stream position is verified with a follow-up read into an ordinary
 * buffer.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define KB(x)           ((uint32_t)(x) << 10)
#define MB(x)           ((uint32_t)(x) << 20)

#define BACKING_SIZE    MB(8)
#define BACKING_ALIGN   MB(4)           /* largest page size used */
#define ALIAS_DELTA     0x40000000u     /* "mapped" mode: VA - PA */
#define FILE_SIZE       (MB(2) + KB(128))
#define GUARD           32u
#define FOLLOW_LEN      97u
#define POISON          0xa5
#define MISS_LIMIT      64u
#define USER_TLB_FIRST  1u              /* entries 1-5 belong to the user */
#define USER_TLB_COUNT  5u
#define NUM_VECTORS     48u
#define VEC_TLBMISSRW   6u

#define HEX_SYS_OPEN    0x01
#define HEX_SYS_CLOSE   0x02
#define HEX_SYS_READ    0x06
#define HEX_SYS_SEEK    0x0a

#define SHIFT_MIXED     0u

/*
 * The memory behind the window.  Aligned and sized so that it shares no page
 * of any size used here with the rest of the program, and so that offsets
 * into it have the same page alignment as their addresses.
 */
static unsigned char backing[BACKING_SIZE]
    __attribute__((aligned(BACKING_ALIGN)));
static unsigned char chunk[KB(64)];
static unsigned char follow[FOLLOW_LEN];
static const char filename[] = "semihost_read_replay.tmp";

static int mapped_mode;
static uint32_t delta;      /* window VA - PA */

/* Read by the assembly stub to decide whether a miss is ours. */
uint32_t srr_win_base;
uint32_t srr_win_size;

static struct {
    uint32_t shift;         /* page size, or SHIFT_MIXED */
    uint32_t nent;          /* how many TLB entries the handler may use */
    uint32_t next;
    uint32_t misses;
    uint32_t armed;         /* a SYS_READ is in flight: watch for livelock */
    uint32_t rescued;
} mmu;

static uint32_t my_vectors[1024] __attribute__((aligned(4096)));

static inline uint64_t tlbr(uint32_t idx)
{
    uint64_t entry;
    asm volatile("%0 = tlbr(%1)\n" : "=r"(entry) : "r"(idx));
    return entry;
}

static inline void tlbw(uint64_t entry, uint32_t idx)
{
    asm volatile("tlbw(%0, %1)\n" : : "r"(entry), "r"(idx));
}

static inline uint32_t tlbp(uint32_t va)
{
    uint32_t idx;
    asm volatile("%0 = tlbp(%1)\n" : "=r"(idx) : "r"(va >> 12));
    return idx;
}

static void tlb_invalidate(uint32_t idx)
{
    tlbw(tlbr(idx) & ~(1ull << 63), idx);
}

static void unmap_window(void)
{
    for (uint32_t i = 0; i < USER_TLB_COUNT; ++i) {
        tlb_invalidate(USER_TLB_FIRST + i);
    }
    mmu.next = 0;
}

/* Map the window the easy way, for the test's own fill and verify loops. */
static void relax_window(void)
{
    unmap_window();
    mmu.shift = 20;
    mmu.nent = USER_TLB_COUNT;
    mmu.armed = 0;
}

/*
 * Take over the window's addresses.  When that is the memory itself, drop
 * whatever 1M pages the runtime has already put over it.
 */
static void claim_window(void)
{
    for (uint32_t offset = 0; !delta && offset < BACKING_SIZE;
         offset += MB(1)) {
        uint32_t idx = tlbp((uint32_t)backing + offset);
        if (!(idx & 0x80000000u)) {
            tlb_invalidate(idx);
        }
    }
    srr_win_base = (uint32_t)backing + delta;
    srr_win_size = BACKING_SIZE;
    relax_window();
}

/*
 * Mixed layout, as offsets into the backing store.  Every region is aligned
 * to its own page size and the sizes grow towards the 1M boundary, so one
 * read can run through five different page sizes:
 *     [..688K) 4K, [688K..704K) 16K, [704K..768K) 64K,
 *     [768K..1M) 256K, [1M..) 1M
 */
static uint32_t mixed_shift(uint32_t offset)
{
    if (offset >= MB(1)) {
        return 20;
    } else if (offset >= KB(768)) {
        return 18;
    } else if (offset >= KB(704)) {
        return 16;
    } else if (offset >= KB(688)) {
        return 14;
    }
    return 12;
}

static uint32_t page_shift(uint32_t shift, uint32_t offset)
{
    return shift == SHIFT_MIXED ? mixed_shift(offset) : shift;
}

/* Called from srr_tlbmissrw with EX set: keep it small, no semihosting. */
__attribute__((used)) void srr_handle_miss(uint32_t badva)
{
    uint32_t shift, idx, va;

    if (++mmu.misses > MISS_LIMIT && mmu.armed) {
        /*
         * The read keeps missing.  Break the livelock by giving it all the
         * entries and the largest pages, so the test can report it and
         * carry on.
         */
        mmu.rescued = 1;
        mmu.armed = 0;
        unmap_window();
        mmu.shift = 22;
        mmu.nent = USER_TLB_COUNT;
    }
    shift = page_shift(mmu.shift, badva - srr_win_base);
    idx = USER_TLB_FIRST + (mmu.next++ % mmu.nent);
    va = badva & ~((1u << shift) - 1);

    tlbw((3ull << 62) |                         /* V, G */
         ((uint64_t)(va >> 12) << 32) |
         (0xfull << 28) |                       /* XWRU */
         (0x7ull << 24) |                       /* CCCC */
         (((va - delta) >> 12) << 1) |
         (1u << ((shift - 12) / 2)),            /* page size */
         idx);
}

/*
 * TLB miss RW entry point.  Misses outside the window are handed to
 * the standalone runtime untouched; misses inside it are resolved by
 * srr_handle_miss().
 */
asm(
"    .text\n"
"    .p2align 4\n"
"    .type srr_tlbmissrw, @function\n"
"srr_tlbmissrw:\n"
"    crswap(sp, sgp0)\n"
"    sp = add(sp, #-128)\n"
"    memd(sp + #0) = r1:0\n"
"    r0 = p3:0\n"
"    memw(sp + #8) = r0\n"
"    r0 = badva\n"
"    r1 = memw(##srr_win_base)\n"
"    r0 = sub(r0, r1)\n"
"    r1 = memw(##srr_win_size)\n"
"    p0 = cmp.gtu(r1, r0)\n"
"    if (p0) jump 1f\n"
"    r0 = memw(sp + #8)\n"
"    p3:0 = r0\n"
"    r1:0 = memd(sp + #0)\n"
"    sp = add(sp, #128)\n"
"    crswap(sp, sgp0)\n"
"    jump event_handle_tlbmissrw\n"
"1:\n"
"    memd(sp + #16) = r3:2\n"
"    memd(sp + #24) = r5:4\n"
"    memd(sp + #32) = r7:6\n"
"    memd(sp + #40) = r9:8\n"
"    memd(sp + #48) = r11:10\n"
"    memd(sp + #56) = r13:12\n"
"    memd(sp + #64) = r15:14\n"
"    memd(sp + #72) = r31:30\n"
"    memw(sp + #80) = r28\n"
"    r0 = lc0\n"
"    r1 = sa0\n"
"    memd(sp + #88) = r1:0\n"
"    r0 = lc1\n"
"    r1 = sa1\n"
"    memd(sp + #96) = r1:0\n"
"    r0 = usr\n"
"    memw(sp + #104) = r0\n"
"    r0 = badva\n"
"    call srr_handle_miss\n"
"    r0 = memw(sp + #104)\n"
"    usr = r0\n"
"    r1:0 = memd(sp + #96)\n"
"    lc1 = r0\n"
"    sa1 = r1\n"
"    r1:0 = memd(sp + #88)\n"
"    lc0 = r0\n"
"    sa0 = r1\n"
"    r28 = memw(sp + #80)\n"
"    r31:30 = memd(sp + #72)\n"
"    r15:14 = memd(sp + #64)\n"
"    r13:12 = memd(sp + #56)\n"
"    r11:10 = memd(sp + #48)\n"
"    r9:8 = memd(sp + #40)\n"
"    r7:6 = memd(sp + #32)\n"
"    r5:4 = memd(sp + #24)\n"
"    r3:2 = memd(sp + #16)\n"
"    r0 = memw(sp + #8)\n"
"    p3:0 = r0\n"
"    r1:0 = memd(sp + #0)\n"
"    sp = add(sp, #128)\n"
"    crswap(sp, sgp0)\n"
"    rte\n"
"    .size srr_tlbmissrw, . - srr_tlbmissrw\n"
);
extern void srr_tlbmissrw(void);

/*
 * Build a copy of the runtime's event vectors that differs only in the
 * TLB miss RW slot.  Each slot is a "jump #r22:2", so it is re-encoded
 * for its new location rather than bounced through a register.
 */
static int install_vectors(void)
{
    uint32_t *old;

    asm volatile("%0 = evb\n" : "=r"(old));
    for (uint32_t i = 0; i < NUM_VECTORS; ++i) {
        uint32_t insn = old[i];
        int32_t disp = (((insn >> 16) & 0x1ff) << 13) | ((insn >> 1) & 0x1fff);
        uint32_t target = (uint32_t)&old[i] + (((disp << 10) >> 10) << 2);

        if ((insn & 0xfe000000) != 0x58000000) {
            return -1;
        }
        if (i == VEC_TLBMISSRW) {
            target = (uint32_t)srr_tlbmissrw;
        }
        disp = (int32_t)(target - (uint32_t)&my_vectors[i]) >> 2;
        if (disp < -(1 << 21) || disp >= (1 << 21)) {
            return -1;
        }
        my_vectors[i] = 0x5800c000 | (((disp >> 13) & 0x1ff) << 16) |
                        ((disp & 0x1fff) << 1);
    }
    asm volatile("evb = %0\n" : : "r"(my_vectors));
    return 0;
}

static uint32_t semi_args[3];

/* The trap may be executed more than once; r0/r1 are still intact then. */
static __attribute__((noinline)) int semihost(uint32_t op, uint32_t a0,
                                              uint32_t a1, uint32_t a2)
{
    int ret;
    semi_args[0] = a0;
    semi_args[1] = a1;
    semi_args[2] = a2;
    asm volatile("r0 = %1\n"
                 "r1 = %2\n"
                 "trap0(#0)\n"
                 "%0 = r0\n"
                 : "=r"(ret)
                 : "r"(op), "r"(semi_args)
                 : "r0", "r1", "r2", "r3", "memory");
    return ret;
}

static unsigned char pattern(uint32_t offset)
{
    uint32_t x = offset ^ (offset >> 7) ^ (offset >> 15);
    return (unsigned char)((x * 37u + (x >> 3) + 0x5bu) & 0xffu);
}

static int create_file(void)
{
    FILE *f = fopen(filename, "wb");
    int ok = f != NULL;

    for (uint32_t offset = 0; ok && offset < FILE_SIZE;
         offset += sizeof(chunk)) {
        uint32_t count = FILE_SIZE - offset;
        if (count > sizeof(chunk)) {
            count = sizeof(chunk);
        }
        for (uint32_t i = 0; i < count; ++i) {
            chunk[i] = pattern(offset + i);
        }
        ok = fwrite(chunk, 1, count, f) == count;
    }
    if (f && fclose(f) != 0) {
        ok = 0;
    }
    return ok ? 0 : -1;
}

enum premap {
    PRE_NONE,       /* nothing mapped when the trap is taken */
    PRE_HEAD,       /* only the page holding the first byte is mapped */
    PRE_TAIL,       /* only the page holding the last byte is mapped */
    PRE_ALL,        /* every page of the buffer is mapped */
};

static const char *const premap_str[] = { "none", "head", "tail", "all" };

static const char *shift_str(uint32_t shift)
{
    switch (shift) {
    case 12: return "4K";
    case 14: return "16K";
    case 16: return "64K";
    case 18: return "256K";
    case 20: return "1M";
    case 22: return "4M";
    default: return "mixed";
    }
}

/* Count the pages under backing[start..], touching each one if asked to. */
static uint32_t walk_pages(uint32_t shift, uint32_t start, uint32_t length,
                           int touch)
{
    uint32_t pages = 0;
    uint32_t offset = start;

    while (offset < start + length) {
        uint32_t size = 1u << page_shift(shift, offset);
        if (touch) {
            (void)*(volatile unsigned char *)(backing + delta + offset);
        }
        offset = (offset & ~(size - 1)) + size;
        ++pages;
    }
    return pages;
}

static int fd;
static unsigned case_num;
static unsigned failures;

/*
 * Read 'length' bytes into the window at backing[start..], with the guest
 * TLB set up as described by shift/nent/premap.
 */
static void run_case(const char *shape, uint32_t shift, uint32_t nent,
                     enum premap premap, uint32_t start, uint32_t length)
{
    unsigned char *mem = backing + start;   /* always usable by the test */
    unsigned char *dst = mem + delta;       /* what SYS_READ is given */
    uint32_t pages = walk_pages(shift, start, length, 0);
    uint32_t bad = 0, first_bad = 0, file_offset, misses;
    int ret, follow_ret, rescued, ok = 1;

    if (mapped_mode) {
        /* One variant per case, and only if the whole buffer can be mapped. */
        if (premap != PRE_NONE || pages > nent) {
            return;
        }
        premap = PRE_ALL;
    }
    file_offset = (case_num * 4099u) % (FILE_SIZE - length - FOLLOW_LEN);
    ++case_num;
    memset(mem - GUARD, POISON, length + 2 * GUARD);
    memset(follow, POISON, sizeof(follow));

    unmap_window();
    mmu.shift = shift;
    mmu.nent = nent;
    mmu.rescued = 0;
    if (premap == PRE_HEAD) {
        (void)*(volatile unsigned char *)&dst[0];
    } else if (premap == PRE_TAIL) {
        (void)*(volatile unsigned char *)&dst[length - 1];
    } else if (premap == PRE_ALL) {
        walk_pages(shift, start, length, 1);
    }

    /* hexagon-sim returns the new position here rather than 0. */
    ret = semihost(HEX_SYS_SEEK, fd, file_offset, 0);
    if (ret < 0) {
        printf("FAIL: seek to %lu returned %d\n",
               (unsigned long)file_offset, ret);
        ++failures;
        relax_window();
        return;
    }
    mmu.misses = 0;
    mmu.armed = 1;
    ret = semihost(HEX_SYS_READ, fd, (uint32_t)dst, length);
    misses = mmu.misses;
    rescued = mmu.rescued;
    relax_window();
    /* The stream must now sit right behind what was just read. */
    follow_ret = semihost(HEX_SYS_READ, fd, (uint32_t)follow, FOLLOW_LEN);

    for (uint32_t i = 0; i < length; ++i) {
        if (mem[i] != pattern(file_offset + i)) {
            if (!bad++) {
                first_bad = i;
            }
        }
    }

    printf("%-5s %-9s tlb=%lu premap=%s start=0x%06lx len=0x%06lx "
           "pages=%lu misses=%lu: ",
           shift_str(shift), shape, (unsigned long)nent, premap_str[premap],
           (unsigned long)start, (unsigned long)length,
           (unsigned long)pages, (unsigned long)misses);
    if (ret != 0) {
        printf("[read returned %d] ", ret);
        ok = 0;
    }
    if (bad) {
        printf("[%lu bytes wrong, first at +0x%lx (va 0x%08lx): "
               "got %02x want %02x] ",
               (unsigned long)bad, (unsigned long)first_bad,
               (unsigned long)(uint32_t)&dst[first_bad],
               mem[first_bad], pattern(file_offset + first_bad));
        ok = 0;
    }
    for (uint32_t i = 0; i < GUARD; ++i) {
        if (mem[-(int)(i + 1)] != POISON || mem[length + i] != POISON) {
            printf("[guard overwritten] ");
            ok = 0;
            break;
        }
    }
    if (follow_ret != 0) {
        printf("[follow-up read returned %d] ", follow_ret);
        ok = 0;
    }
    for (uint32_t i = 0; i < FOLLOW_LEN; ++i) {
        if (follow[i] != pattern(file_offset + length + i)) {
            printf("[stream position wrong after read] ");
            ok = 0;
            break;
        }
    }
    if (mapped_mode && misses) {
        printf("[TLB miss on a fully mapped buffer] ");
        ok = 0;
    }
    if (rescued) {
        printf("[livelock: more than %lu TLB misses] ",
               (unsigned long)MISS_LIMIT);
        ok = 0;
    }
    puts(ok ? "ok" : "FAIL");
    failures += !ok;
}

static void run_premaps(const char *shape, uint32_t shift, uint32_t nent,
                        uint32_t start, uint32_t length)
{
    run_case(shape, shift, nent, PRE_NONE, start, length);
    if (length <= KB(512)) {    /* keeps the run time on hexagon-sim sane */
        run_case(shape, shift, nent, PRE_HEAD, start, length);
        run_case(shape, shift, nent, PRE_TAIL, start, length);
    }
}

int main(int argc, char **argv)
{
    /* A boundary between pages of every size used. */
    const uint32_t b = MB(4);

    if (argc > 1 && strcmp(argv[1], "mapped") == 0) {
        mapped_mode = 1;
        delta = ALIAS_DELTA;
    } else if (argc > 1) {
        printf("FAIL: unknown mode '%s'\n", argv[1]);
        return 1;
    }
    printf("mode: %s\n", mapped_mode ? "mapped" : "default");

    if (create_file() != 0) {
        puts("FAIL: cannot create test file");
        return 1;
    }
    if (install_vectors() != 0) {
        puts("FAIL: unexpected event vector encoding");
        return 1;
    }
    claim_window();
    fd = semihost(HEX_SYS_OPEN, (uint32_t)filename, 1 /* rb */,
                  sizeof(filename) - 1);
    if (fd < 0) {
        puts("FAIL: cannot open test file");
        return 1;
    }

    /* One page size at a time. */
    for (uint32_t shift = 12; shift <= 20; shift += 2) {
        uint32_t size = 1u << shift;

        run_case("within", shift, USER_TLB_COUNT, PRE_NONE, b + 0x321, 257);
        /* A few bytes either side of a page boundary. */
        run_premaps("cross", shift, USER_TLB_COUNT, b - 13, 29);
        /* Tail, a whole page, head. */
        run_premaps("span3", shift, USER_TLB_COUNT, b - 7, size + 19);
        /* Exactly two whole pages. */
        run_premaps("aligned2", shift, USER_TLB_COUNT, b, 2 * size);
        /* One whole page plus one byte. */
        run_premaps("aligned+1", shift, USER_TLB_COUNT, b, size + 1);
        if (shift > 12) {
            /* Crosses a 4K boundary inside a single larger page. */
            run_case("cross4K", shift, USER_TLB_COUNT, PRE_NONE,
                     b + KB(4) - 13, 29);
        }
    }

    /* 4M pages: the window only holds two of them. */
    run_case("within", 22, USER_TLB_COUNT, PRE_NONE, b + 0x321, 257);
    run_case("cross4K", 22, USER_TLB_COUNT, PRE_NONE, b + KB(4) - 13, 29);
    run_premaps("cross", 22, USER_TLB_COUNT, b - 13, 29);
    run_premaps("big", 22, USER_TLB_COUNT, b - KB(128) + 7, KB(256) + 19);

    /* Page sizes changing underneath one read. */
    run_premaps("4K|16K", SHIFT_MIXED, USER_TLB_COUNT, KB(688) - 13, 29);
    run_premaps("16K|64K", SHIFT_MIXED, USER_TLB_COUNT, KB(704) - 13, 29);
    run_premaps("64K|256K", SHIFT_MIXED, USER_TLB_COUNT, KB(768) - 13, 29);
    run_premaps("256K|1M", SHIFT_MIXED, USER_TLB_COUNT, MB(1) - 13, 29);
    run_premaps("4K..1M", SHIFT_MIXED, USER_TLB_COUNT,
                KB(688) - 9, MB(1) + 11 - (KB(688) - 9));
    run_premaps("4K..1M+", SHIFT_MIXED, USER_TLB_COUNT,
                KB(688) - KB(4) - 9, MB(1) + 11 - (KB(688) - KB(4) - 9));

    /*
     * Fewer TLB entries than pages in the buffer: resolving one page
     * evicts another one that the same read needs.
     */
    run_case("cross", 12, 1, PRE_NONE, b - 13, 29);
    run_case("cross", 16, 1, PRE_NONE, b - 13, 29);
    run_case("cross", 20, 1, PRE_NONE, b - 13, 29);
    run_case("span3", 12, 2, PRE_NONE, b - 7, KB(4) + 19);
    run_case("span8", 12, USER_TLB_COUNT, PRE_NONE, b - 7, 6 * KB(4) + 19);
    run_case("4K..1M", SHIFT_MIXED, 2, PRE_NONE,
             KB(688) - 9, MB(1) + 11 - (KB(688) - 9));

    if (semihost(HEX_SYS_CLOSE, fd, 0, 0) != 0 || remove(filename) != 0) {
        puts("FAIL: cleanup");
        ++failures;
    }
    printf("%u of %u cases failed\n", failures, case_num);
    puts(failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
