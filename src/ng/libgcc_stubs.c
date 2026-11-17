/* 64-bit shift helpers for the rawcode overlay.
 *
 * The recompiled guest code (SH-2) calls libgcc's __*_di3 helpers for 68000
 * 64-bit shifts.  With 32-bit cycle counters the host app no longer references
 * them, so the linker dead-strips libgcc's copies and the rawcode packer
 * assigns placeholder addresses -- a guest call then jumps into unmapped
 * memory.  Defining them here keeps real addresses in the app ELF.
 * Semantics match the standard libgcc routines for all shift counts.
 */
typedef unsigned long long u64;
typedef long long i64;

u64 __attribute__((used)) __lshrdi3(u64 a, unsigned int b) {
    if (b == 0u) return a;
    if (b >= 64u) return 0u;
    return a >> b;
}

i64 __attribute__((used)) __ashldi3(i64 a, unsigned int b) {
    if (b == 0u) return a;
    if (b >= 64u) return (i64)0;
    return (i64)((u64)a << b);
}

i64 __attribute__((used)) __ashrdi3(i64 a, unsigned int b) {
    if (b == 0u) return a;
    if (b >= 64u) return (a < 0) ? (i64)-1 : (i64)0;
    return a >> b;
}
