/* wram_rescue_routines.c - guest-copied WRAM code needed for the recomp-only
 * renderer-enabled bench to reach gameplay. Local build only; derived from
 * the user's own ROM. The BIOS copies a small routine to $100000 and jumps to
 * it; without a native entry the recomp-only guest halts before producing
 * gameplay sprites, so the renderer never draws. Bit-exact lowering from the
 * pinned m68krecomp output (see local/ymir/resident_routines.c). */
#include <stdint.h>
#include <neogeorecomp/cpu.h>
#include <neogeorecomp/bus.h>
#include <neogeorecomp/neogeo.h>

void wram_r_100000(void) {
    switch (CPU.pc) {
    case 0x100000u: goto L_100000;
    case 0x100004u: goto L_100004;
    case 0x100006u: goto L_100006;
    case 0x10000Au: goto L_10000A;
    case 0x10000Eu: goto L_10000E;
    default: ng_bad_dispatch(CPU.pc); return;
    }
L_100000: /* 100000: ori.b   #$0, D0 */
    { NG_INSN(8); uint8_t d = (uint8_t)CPU.d[0]; uint8_t r = ng_logic8((uint8_t)(d | (uint8_t)0x0u)); CPU.d[0] = (CPU.d[0] & 0xFFFFFF00u) | (uint8_t)(r); }
L_100004: /* 100004: move.l  A2, (A2)+ */
    { NG_INSN(12); uint32_t v = (uint32_t)CPU.a[2]; ng_logic32(v); uint32_t ad = CPU.a[2]; CPU.a[2] += 4; ng_w32(ad, v); }
L_100006: /* 100006: ori.b   #$10, D0 */
    { NG_INSN(8); uint8_t d = (uint8_t)CPU.d[0]; uint8_t r = ng_logic8((uint8_t)(d | (uint8_t)0x10u)); CPU.d[0] = (CPU.d[0] & 0xFFFFFF00u) | (uint8_t)(r); }
L_10000A: /* 10000A: ori.b   #$2e, D0 */
    { NG_INSN(8); uint8_t d = (uint8_t)CPU.d[0]; uint8_t r = ng_logic8((uint8_t)(d | (uint8_t)0x2Eu)); CPU.d[0] = (CPU.d[0] & 0xFFFFFF00u) | (uint8_t)(r); }
L_10000E: /* 10000E: dc.w $49ff; ILLEGAL */
    { NG_INSN(0); ng_exception(4, 0x10000Eu); return; }
}
