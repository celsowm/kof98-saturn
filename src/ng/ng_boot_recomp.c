/* ng_boot_recomp.c - native 68000 dispatch backend.
 * Extracted from ng_boot.c (SRP): CPU state, routine lookup (resident cache,
 * BIOS table, overlay pages), exception entry and the step/init drivers are
 * the native-backend responsibility. The bus/device side stays in ng_boot.c.
 */
#include "ng_boot_internal.h"
#include <neogeorecomp/cpu.h>
#include <neogeorecomp/bus.h>
#include <neogeorecomp/neogeo.h>
#ifndef NG_BOOT_RECOMP_ONLY
#include "m68k.h"
#endif

#ifdef NG_BOOT_RECOMP
/* ---- native recomp dispatch (X2) ----
 * Executes recompiled 68000 routines (generated from the user's own ROM) as
 * native C, falling back to Musashi for addresses with no native entry. IRQ is
 * taken at routine boundaries (like exec.c), not per instruction, so the guest
 * sees real 68000 exceptions. CPU state lives in ng_cpu; Musashi is synced in and
 * out only around fallback instructions. The entry table is sorted, so lookup is
 * a binary search (no 512 KB pointer table). */
extern const ng_func_entry_t bios_ng_recomp_table[];
extern const size_t bios_ng_recomp_count;

ng_cpu_t ng_cpu;
#ifdef KOF_32BIT_CYCLES
int32_t  ng_next_event;
#else
int64_t  ng_next_event;
#endif
#ifdef KOF_32BIT_CYCLES
uint32_t ng_icount;
#else
uint64_t ng_icount;
#endif
uint8_t *ng_rmap[256];
uint8_t *ng_wmap[256];
static int s_recomp_disabled;
volatile uint32_t ng_boot_recomp_bad;
volatile uint32_t ng_boot_recomp_blocks;   /* native routines entered */
volatile uint32_t ng_boot_recomp_interp;   /* instructions run in Musashi */

uint8_t  ng_r8_slow(uint32_t a) { return ng_boot_read8(a); }
uint16_t ng_r16_slow(uint32_t a) { return ng_boot_read16(a); }
void ng_set_sr(uint16_t sr) {
    uint8_t s = (sr >> 13) & 1;
    if (s != ng_cpu.s) { uint32_t t = ng_cpu.a[7]; ng_cpu.a[7] = ng_cpu.osp; ng_cpu.osp = t; ng_cpu.s = s; }
    ng_cpu.t = (sr >> 15) & 1;
    ng_cpu.ipl = (sr >> 8) & 7;
    ng_set_ccr(sr);
}
void ng_exception(int vector, uint32_t ret_pc) {
    uint16_t old = ng_get_sr();
    ng_set_sr((uint16_t)((old | 0x2000) & ~0x8000));
    /* The recompiler passes the address of the faulting instruction.  A 68000
     * exception frame holds the PC after it; ILLEGAL/LINEA/LINEF are all
     * 2-byte opcodes.  The host oracle hides this via Musashi fallback, but a
     * native-only build would re-dispatch the same illegal opcode forever. */
    if (vector == 4 || vector == 10 || vector == 11) ret_pc = (ret_pc + 2u) & 0xFFFFFFu;
    ng_push32(ret_pc);
    ng_push16(old);
    ng_cpu.pc = ng_r32((uint32_t)vector * 4);
    ng_cycles += 34;
}
void ng_bad_dispatch(uint32_t pc) {
    /* Table and generated code disagree: disable native dispatch (fall back to
     * Musashi everywhere) rather than spin or corrupt state. */
    (void)pc;
    ng_boot_recomp_bad++;
    s_recomp_disabled = 1;
}

#ifdef NG_BOOT_OVERLAY
static ng_func_t overlay_lookup(uint32_t a);   /* defined with the overlay runtime */
#endif

/* ---- game overlay (X2) ----------------------------------------------------------------------
 * The game P-ROM recomp is split into NG_RECOMP_PAGES load units on the disc;
 * a dispatch into a non-resident page faults it in through the platform
 * loader (ng_overlay_loader), evicting the LRU page when the resident budget
 * is full. The game page map (game_recomp_pages.h, local-only) carries the
 * addr -> {fn, page} entries; the flat table is not linked. */
#ifdef NG_BOOT_GAME_OVERLAY
#include "game_recomp_pages.h"   /* ng_gamemap_page_entry_t, NG_RECOMP_PAGES */
extern int ng_overlay_loader(unsigned page);
extern uint8_t* ng_overlay_page_base(unsigned page);
__attribute__((weak)) const ng_gamemap_page_entry_t* ng_game_pages(void) { return NULL; }
__attribute__((weak)) size_t ng_game_pages_count(void) { return 0; }
static uint8_t s_game_page_resident[NG_RECOMP_PAGES];
static uint16_t s_game_page_clock[NG_RECOMP_PAGES];
static uint16_t s_game_page_stamp;
static unsigned s_game_budget;        /* 0 = every page resident */
volatile uint32_t ng_boot_game_loads;
volatile uint32_t ng_boot_game_faults;
volatile uint32_t ng_boot_game_evictions;
volatile uint32_t ng_boot_game_errors;
volatile uint32_t ng_boot_game_resident;

void ng_game_overlay_set_budget(unsigned pages) {
    s_game_budget = pages > NG_RECOMP_PAGES ? NG_RECOMP_PAGES : pages;
}

static int game_resident_count(void) {
    unsigned n = 0;
    for (unsigned i = 0; i < NG_RECOMP_PAGES; i++) n += s_game_page_resident[i] != 0;
    return (int)n;
}
static int game_victim(void) {
    int best = -1;
    uint16_t best_age = 0;
    for (unsigned i = 0; i < NG_RECOMP_PAGES; i++) {
        if (!s_game_page_resident[i]) continue;
        if (best < 0 || s_game_page_clock[i] < best_age) { best = (int)i; best_age = s_game_page_clock[i]; }
    }
    return best;
}
static int game_fault(unsigned page) {
    if (!s_game_page_resident[page]) {
        ng_boot_game_faults++;
        /* The Saturn arena is direct-mapped: any resident page whose slot
         * equals this page's slot will be overwritten by the load.  Drop all
         * of those from the residency set first. */
        uint8_t* slot = ng_overlay_page_base(page);
        for (unsigned i = 0; i < NG_RECOMP_PAGES; i++) {
            if (s_game_page_resident[i] && ng_overlay_page_base(i) == slot) {
                s_game_page_resident[i] = 0;
                ng_boot_game_resident--;
                ng_boot_game_evictions++;
            }
        }
        if (ng_overlay_loader(page) != 0) { ng_boot_game_errors++; return 0; }
        s_game_page_resident[page] = 1;
        ng_boot_game_resident++;
        ng_boot_game_loads++;
    }
    s_game_page_clock[page] = ++s_game_page_stamp;
    return 1;
}

static ng_func_t game_overlay_lookup(uint32_t a) {
    const ng_gamemap_page_entry_t* pages = ng_game_pages();
    const size_t count = ng_game_pages_count();
    if (pages == NULL || count == 0) return NULL;
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t m = lo + ((hi - lo) >> 1);
        uint32_t e = pages[m].addr;
        if (e < a) lo = m + 1; else hi = m;
    }
    if (lo >= count || pages[lo].addr != a) return NULL;
    if (!game_fault(pages[lo].page)) { ng_boot_recomp_bad++; return NULL; }
    /* rawcode: the map carries an offset into the resident page blob, not a
     * linked function pointer. Resolve it against the page's arena base. */
    return (ng_func_t)(void*)(ng_overlay_page_base(pages[lo].page) + pages[lo].off);
}
#endif /* NG_BOOT_GAME_OVERLAY */

/* bus_peek16: plain-memory read used by kof_local_recomp-recompiled code. */
uint16_t bus_peek16(uint32_t a) {
    return ng_boot_read16(a);
}

/* bus_peek8 for symmetry */
uint8_t bus_peek8(uint32_t a) { return ng_boot_read8(a); }

/* Resident-native lookup: weak default; a target may link a strong version. */
__attribute__((weak)) ng_func_t ng_resident_lookup(uint32_t pc) { (void)pc; return NULL; }

static ng_func_t recomp_lookup(uint32_t a) {
    a &= 0xFFFFFF;
    if (s_recomp_disabled) return NULL;
    /* Resident-native hot routines (linked directly by a target build). */
    {
        extern ng_func_t ng_resident_lookup(uint32_t pc);
        ng_func_t rf = ng_resident_lookup(a);
        if (rf) return rf;
    }
    /* BIOS: overlay or full table. */
    if (a >= 0xC00000 && a < 0xD00000) {
#ifdef NG_BOOT_OVERLAY
        return overlay_lookup(a);
#else
        size_t lo = 0, hi = bios_ng_recomp_count;
        while (lo < hi) {
            size_t m = lo + ((hi - lo) >> 1);
            uint32_t e = bios_ng_recomp_table[m].addr;
            if (e < a) lo = m + 1; else hi = m;
        }
        if (lo < bios_ng_recomp_count && bios_ng_recomp_table[lo].addr == a)
            return bios_ng_recomp_table[lo].fn;
        return NULL;
#endif
    }
    /* Game P-ROM, guest low RAM and the WRAM code window: route all of
     * $000000-$0FFFFF (vector/P1), $100000-$1FFFFF (WRAM, the BIOS/game copy
     * executable code there), and $200000-$4FFFFF (P2 banks) to the overlay. */
    if (a < 0x500000u) {
#ifdef NG_BOOT_GUEST_MUSASHI
        /* Hybrid core: low RAM ($000000-$0FFFFF) and WRAM ($100000-$1FFFFF)
         * hold code the guest copies/rewrites at runtime. A STATIC recomp
         * overlay cannot be correct for those addresses (it was generated
         * from the P ROM, not the live RAM bytes), which is exactly why the
         * recomp-only path diverged at 0xA3DA. Return NULL so recomp_step()
         * falls back to the linked Musashi interpreter, which reads the live
         * RAM bytes. P-ROM banks ($200000-$4FFFFF) stay on the static overlay.
         * The verified dense handlers above are still consulted first. */
        if (a < 0x200000u) return NULL;
#endif
#ifdef NG_BOOT_GAME_OVERLAY
        return game_overlay_lookup(a);
#else
        return NULL;   /* no game recomp linked */
#endif
    }
    return NULL;
}

#ifdef NG_BOOT_OVERLAY
/* ---- recomp routine overlay / streaming ----
 *
 * Recompiled routines never call each other directly: every intra-ROM transfer
 * (JSR/JMP/branch out of the routine) stores the target in CPU.pc and returns to
 * the dispatcher. A routine is therefore an independent unit that only the
 * dispatcher enters, which is exactly what paging needs.
 *
 * `m68krecomp --files N` emits each load unit (page) as its own translation
 * unit, and tools/mk_recomp_pages.py turns those into a PC -> page map
 * (ng_recomp_pages[], sorted by address). A page that is not resident is brought
 * in through the platform hook ng_overlay_loader(); when the resident budget is
 * full the least-recently-used non-pinned page is evicted. The hook is the only
 * platform-specific part: on Saturn it copies the page's linked bytes from the
 * disc into the page's fixed arena address.
 */
extern int ng_overlay_loader(unsigned page);   /* platform: make `page` resident */

static uint8_t  s_page_resident[NG_RECOMP_PAGES];
static uint16_t s_page_clock[NG_RECOMP_PAGES];
static uint16_t s_page_stamp;
static unsigned s_page_budget;        /* 0 = every page resident (no paging) */
static int      s_page_pinned = -1;   /* page holding the reset PC: never evicted */

volatile uint32_t ng_boot_overlay_loads;      /* pages actually brought in */
volatile uint32_t ng_boot_overlay_faults;     /* dispatch into a non-resident page */
volatile uint32_t ng_boot_overlay_evictions;  /* pages thrown out to make room */
volatile uint32_t ng_boot_overlay_errors;     /* load failures / unpinnable budget */
volatile uint32_t ng_boot_overlay_resident;   /* pages resident right now */

void ng_overlay_set_budget(unsigned pages) {
    s_page_budget = pages > NG_RECOMP_PAGES ? NG_RECOMP_PAGES : pages;
}
unsigned ng_overlay_budget(void) { return s_page_budget; }

static int overlay_resident_count(void) {
    unsigned n = 0;
    for (unsigned i = 0; i < NG_RECOMP_PAGES; i++) n += s_page_resident[i] != 0;
    return (int)n;
}
static int overlay_victim(void) {
    int best = -1;
    uint16_t best_age = 0;
    for (unsigned i = 0; i < NG_RECOMP_PAGES; i++) {
        if (!s_page_resident[i] || (int)i == s_page_pinned) continue;
        if (best < 0 || s_page_clock[i] < best_age) { best = (int)i; best_age = s_page_clock[i]; }
    }
    return best;
}
/* Make `page` resident, evicting LRU if the budget is full. Returns 1 on success. */
static int overlay_fault(unsigned page) {
    if (!s_page_resident[page]) {
        ng_boot_overlay_faults++;                 /* true fault: page was not in */
        if (s_page_budget && overlay_resident_count() >= (int)s_page_budget) {
            int v = overlay_victim();
            if (v < 0) { ng_boot_overlay_errors++; return 0; }   /* budget is all pinned */
            s_page_resident[v] = 0;
            ng_boot_overlay_resident--;
            ng_boot_overlay_evictions++;
        }
        if (ng_overlay_loader(page) != 0) { ng_boot_overlay_errors++; return 0; }
        s_page_resident[page] = 1;
        ng_boot_overlay_resident++;
        ng_boot_overlay_loads++;
    }
    s_page_clock[page] = ++s_page_stamp;
    return 1;
}

static ng_func_t overlay_lookup(uint32_t a) {
    size_t lo = 0, hi = ng_recomp_pages_count;
    while (lo < hi) {
        size_t m = lo + ((hi - lo) >> 1);
        uint32_t e = ng_recomp_pages[m].addr;
        if (e < a) lo = m + 1; else hi = m;
    }
    if (lo >= ng_recomp_pages_count || ng_recomp_pages[lo].addr != a) return NULL;
    if (!overlay_fault(ng_recomp_pages[lo].page)) { ng_boot_recomp_bad++; return NULL; }
    return ng_recomp_pages[lo].fn;
}
void ng_overlay_pin_reset_page(uint32_t pc) {
    size_t lo = 0, hi = ng_recomp_pages_count;
    while (lo < hi) {
        size_t m = lo + ((hi - lo) >> 1);
        if (ng_recomp_pages[m].addr < pc) lo = m + 1; else hi = m;
    }
    s_page_pinned = (lo < ng_recomp_pages_count) ? (int)ng_recomp_pages[lo].page : -1;
}
#endif /* NG_BOOT_OVERLAY */


#ifndef NG_BOOT_RECOMP_ONLY
static void recomp_to_musashi(void) {
    m68k_set_reg(M68K_REG_SR, ng_get_sr());
    for (int i = 0; i < 8; i++) m68k_set_reg((m68k_register_t)(M68K_REG_D0 + i), ng_cpu.d[i]);
    for (int i = 0; i < 7; i++) m68k_set_reg((m68k_register_t)(M68K_REG_A0 + i), ng_cpu.a[i]);
    m68k_set_reg(M68K_REG_A7, ng_cpu.a[7]);
    m68k_set_reg(ng_cpu.s ? M68K_REG_USP : M68K_REG_ISP, ng_cpu.osp);
    m68k_set_reg(M68K_REG_PC, ng_cpu.pc);
    m68k_set_irq(0);   /* the dispatcher owns IRQ at routine boundaries */
}
static void recomp_from_musashi(void) {
    uint16_t sr = (uint16_t)m68k_get_reg(NULL, M68K_REG_SR);
    ng_cpu.s = (sr >> 13) & 1; ng_cpu.t = (sr >> 15) & 1; ng_cpu.ipl = (sr >> 8) & 7;
    ng_set_ccr(sr);
    for (int i = 0; i < 8; i++) ng_cpu.d[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_D0 + i));
    for (int i = 0; i < 8; i++) ng_cpu.a[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + i));
    ng_cpu.osp = m68k_get_reg(NULL, ng_cpu.s ? M68K_REG_USP : M68K_REG_ISP);
    ng_cpu.pc = m68k_get_reg(NULL, M68K_REG_PC);
    ng_cpu.stopped = 0;
}
#endif /* !NG_BOOT_RECOMP_ONLY */

static void recomp_take_irq(int level) {
    uint16_t old = ng_get_sr();
    ng_cpu.stopped = 0;
    ng_set_sr((uint16_t)(((old | 0x2000) & ~0x8700) | (level << 8)));
    ng_push32(ng_cpu.pc);
    ng_push16(old);
    ng_cpu.pc = ng_r32(0x60 + (uint32_t)level * 4);
    ng_cycles += 44;
    ng_boot_irq_taken++;
}
/* Dispatch PC trace (instrumentation): circular log of guest PCs + D0. */

void recomp_step(void) {
#ifdef NG_GUEST_FRAME_TRACE
    g_pc_ring[g_pc_ring_w++ & 255u] = ng_cpu.pc;
#endif
    int level = ng_irq_level();
    if (level > ng_cpu.ipl || level == 7) recomp_take_irq(level);
    if (ng_cpu.stopped) return;
    ng_func_t fn = recomp_lookup(ng_cpu.pc);
    if (fn) {
        uint64_t a = ng_icount;
        fn();
        ng_boot_instructions += (uint32_t)(ng_icount - a);
        ng_boot_last_pc = ng_cpu.pc;
        ng_boot_recomp_blocks++;
        return;
    }
#ifdef NG_BOOT_RECOMP_ONLY
    /* No interpreter in this build: a miss is a real fault, never silently
     * stepped. Report it, halt the guest and count it (visible to the probe). */
    ng_boot_recomp_bad++;
    ng_boot_recomp_interp++;        /* mis-dispatch count, reported as such */
    ng_cpu.stopped = 1;
    ng_boot_last_pc = ng_cpu.pc;
#else
    recomp_to_musashi();
    {
        /* Batch interpretation up to the dispatcher yield point (ng_next_event,
         * one raster line ahead). Syncing the full CPU file per instruction made
         * the guest-RAM fallback ~100x slower than pure Musashi; running a whole
         * slice per sync keeps the hybrid close to interpreter speed while the
         * dense native handlers still accelerate the profiled BIOS loops. */
        int64_t budget = ng_next_event - ng_cycles;
        if (budget < 1) budget = 1;
        if (budget > 4000) budget = 4000;
        int used = m68k_execute((int)budget);
        ng_cycles += used;
        recomp_from_musashi();
        if (used == 0) ng_cpu.stopped = 1;   /* STOP with no IRQ: wait for one */
    }
    ng_boot_recomp_interp++;
    ng_boot_last_pc = ng_cpu.pc;
#endif
}
void recomp_init(void) {
    ng_boot_setup_recomp_maps();
#ifdef NG_BOOT_OVERLAY
    /* Page 0 of the reset PC is pinned; the rest are loaded on demand. A budget
     * of 0 means "all resident", which is the no-paging reference. */
    ng_overlay_pin_reset_page(s_initial_pc);
    {
        unsigned resident = 0;
        for (unsigned i = 0; i < NG_RECOMP_PAGES; i++) {
            s_page_resident[i] = (uint8_t)(s_page_budget == 0);
            resident += s_page_resident[i] != 0;
        }
        ng_boot_overlay_resident = resident;
        if (s_page_budget == 0) ng_boot_overlay_loads = NG_RECOMP_PAGES;
    }
#endif
#ifdef NG_BOOT_RECOMP_ONLY
    /* Reset state comes straight from the reset vector page; no Musashi to sync. */
    ng_cpu.d[0] = ng_cpu.d[1] = ng_cpu.d[2] = ng_cpu.d[3] = 0;
    ng_cpu.d[4] = ng_cpu.d[5] = ng_cpu.d[6] = ng_cpu.d[7] = 0;
    for (int i = 0; i < 8; i++) ng_cpu.a[i] = 0;
    ng_cpu.s = 1; ng_cpu.t = 0; ng_cpu.ipl = 7;
    ng_cpu.x = ng_cpu.n = ng_cpu.z = ng_cpu.v = ng_cpu.c = 0;
    ng_cpu.a[7] = s_initial_ssp;
    ng_cpu.osp = 0;
    ng_cpu.pc = s_initial_pc;
    ng_cpu.stopped = 0;
#else
    recomp_from_musashi();   /* capture the reset state ng_boot_init set in m68k */
#endif
}
#endif /* NG_BOOT_RECOMP */
