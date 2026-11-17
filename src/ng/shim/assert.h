/* Freestanding libc shims for building Musashi for the Saturn SH-2 target.
 * The 68000 configuration never calls these (the FPU helpers that do are for
 * the 68040 and are dropped by --gc-sections), but the headers must exist to
 * compile. Included ahead of the toolchain's (absent) libc headers. */
#ifndef NG_SHIM_ASSERT_H
#define NG_SHIM_ASSERT_H
#define assert(x) ((void)0)
#endif