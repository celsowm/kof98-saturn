/* bios_table_stub.c - empty BIOS recomp table for the hybrid core.
 *
 * In the hybrid build the BIOS runs on Musashi except for the profiled hot
 * routines adopted by the dense handler map (consulted first in
 * recomp_lookup()). No full BIOS recomp table is linked: it would not fit in
 * WRAM-H alongside Musashi's opcode tables. An empty table makes every other
 * BIOS PC fall through to recomp_step()'s Musashi fallback, which is correct.
 */
#include <stddef.h>
#include <neogeorecomp/neogeo.h>

const ng_func_entry_t bios_ng_recomp_table[1] = { {0, 0} };
const size_t bios_ng_recomp_count = 0;
