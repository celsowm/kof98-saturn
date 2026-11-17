/* ng_setjmp_shim.c - Saturn-only setjmp/longjmp.
 *
 * Musashi installs a bus-error trap with setjmp() at the top of every
 * m68k_execute(), and longjmp()s back to it only if a memory access raises a
 * bus error. This Neo Geo bus never raises a bus error (unmapped reads return
 * open-bus 0xFF), so longjmp() is never taken: setjmp() only needs to save
 * enough to return 0 and fall through, and longjmp() is a guarded stop.
 *
 * The real implementation is newlib's; this freestanding target links
 * -nostdlib, so provide one. If a bus error ever did occur, the spin makes it a
 * visible failure instead of silent corruption.
 */
#include <setjmp.h>

int setjmp(jmp_buf env) { (void)env; return 0; }

void longjmp(jmp_buf env, int val) {
    (void)env;
    (void)val;
    for (;;) { }   /* bus-error trap: never expected on this bus */
}