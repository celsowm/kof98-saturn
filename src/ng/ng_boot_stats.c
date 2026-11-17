/* ng_boot_stats.c - probe-visible emulated-device counters.
 * Extracted from ng_boot.c (SRP): counting is a separate responsibility
 * from emulating the hardware. All counters are declared in ng_boot.h.
 */
#include "ng_boot.h"

/* ---- stats ---- */
volatile uint32_t ng_boot_instructions;
volatile uint32_t ng_boot_frames;
volatile uint32_t ng_boot_vblanks;
volatile uint32_t ng_boot_timer_irqs;
volatile uint32_t ng_boot_vram_writes;
volatile uint32_t ng_boot_pal_writes;
volatile uint32_t ng_boot_bios_instr;
volatile uint32_t ng_boot_io_writes;
volatile uint32_t ng_boot_last_pc;
volatile uint32_t ng_boot_cart_instr;      /* PC in P1/P2 cart ($000000 / $200000) */
volatile uint32_t ng_boot_wram_instr;      /* PC in work RAM ($100000) */
volatile uint32_t ng_boot_irq_taken;
volatile uint32_t ng_boot_status_reads;
volatile uint32_t ng_boot_status_start_reads;       /* interrupts taken by the 68000 */
